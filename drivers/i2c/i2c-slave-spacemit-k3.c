// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C Slave Backend for Spacemit K3 Cluster Server
 * * I2C Slave Protocol Description:
 * Master writes 2 bytes to request specific data, then reads the response:
 * - 0x2E, 0x03 : Request 4-byte CPU temperature (Little Endian).
 * - 0x2E, 0x04 : Request full sys_info_pkt (64 bytes).
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/sysinfo.h>
#include <linux/cpumask.h>
#include <linux/namei.h>
#include <linux/statfs.h>
#include <linux/byteorder/generic.h>
#include <linux/cpufreq.h>
#include <linux/thermal.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define MIN_VALID_DISK_SIZE	2
#define I2C_NO_READ_TIMEOUT_SEC	5
#define THERMAL_ZONE_NAME	"thermal_top"

/* little endian */
struct spacemit_sys_info {
	char model[32];
	u8 cpu_cores;
	__le32 cpu_freq_khz;
	__le32 ram_mb;
	__le32 disk_gb;
	char serial_number[16];
	u8 reserved[3];
} __packed;

struct spacemit_slave_data {
	struct i2c_client *client;

	u8 rx_buf[2];
	int rx_idx;
	u8 tx_buf[sizeof(struct spacemit_sys_info)];
	int tx_idx;
	int current_tx_len;

	u32 current_temp_mc;
	struct delayed_work temp_poll_work;

	struct spacemit_sys_info sys_info_pkt;
	seqlock_t info_lock;

	bool fs_info_loaded;
	int vfs_retry_count;

	struct thermal_zone_device *tz;

	/* debugfs */
	struct dentry *dbg_dir;
	atomic_t write_received_cnt;
	atomic_t read_requested_cnt;
	atomic_t read_processed_cnt;
	atomic_t stop_cnt;
	unsigned long last_read_jiffies;
};

static void poll_cpu_temp_work_func(struct work_struct *work)
{
	struct spacemit_slave_data *data = container_of(work, struct spacemit_slave_data, temp_poll_work.work);
	const char *model_str = NULL, *serial_str = NULL;
	u32 freq_khz = 0, disk_gb = 0, ram_mb = 0;
	struct spacemit_sys_info tmp_pkt = {0};
	bool freq_ok = false, disk_ok = false;
	struct kstatfs st;
	struct sysinfo i;
	struct path path;
	unsigned long last_read;
	struct thermal_zone_device *tz;

	if (!data->fs_info_loaded && data->vfs_retry_count < 10) {

		/* model */
		if (of_property_read_string(of_root, "model", &model_str) == 0)
			strscpy(tmp_pkt.model, model_str, sizeof(tmp_pkt.model));
		else
			strscpy(tmp_pkt.model, "Unknown", sizeof(tmp_pkt.model));

		/* serial number */
		if (of_property_read_string(of_root, "serial-number", &serial_str) == 0) {
			strscpy(tmp_pkt.serial_number, serial_str, sizeof(tmp_pkt.serial_number));
		} else {
			strscpy(tmp_pkt.serial_number, "Unknown", sizeof(tmp_pkt.serial_number));
		}

		/* cpu core number */
		tmp_pkt.cpu_cores = (u8)num_possible_cpus();

		/* ram */
		si_meminfo(&i);
		ram_mb = (u32)(((u64)i.totalram * i.mem_unit) / (1024 * 1024));
		tmp_pkt.ram_mb = cpu_to_le32(ram_mb);

		/* cpu frequency */
		freq_khz = cpufreq_quick_get_max(0);
		if (freq_khz) {
			tmp_pkt.cpu_freq_khz = cpu_to_le32(freq_khz);
			freq_ok = true;
		}

		/* disk size mounted in / */
		if (kern_path("/", LOOKUP_FOLLOW, &path) == 0) {
			if (vfs_statfs(&path, &st) == 0) {
				disk_gb = (u32)(((u64)st.f_blocks * st.f_frsize) >> 30);
				if (disk_gb > MIN_VALID_DISK_SIZE) {
					tmp_pkt.disk_gb = cpu_to_le32(disk_gb);
					disk_ok = true;
				}
			}
			path_put(&path);
		}

		write_seqlock(&data->info_lock);
		memcpy(&data->sys_info_pkt, &tmp_pkt, sizeof(tmp_pkt));
		write_sequnlock(&data->info_lock);

		if (freq_ok && disk_ok)
			data->fs_info_loaded = true;
		else
			data->vfs_retry_count++;
	}

	/* cpu temperature */
	if (!data->tz) {
		tz = thermal_zone_get_zone_by_name(THERMAL_ZONE_NAME);
		if (!IS_ERR(tz)) {
			data->tz = tz;
			dev_info(&data->client->dev,
				 "Thermal zone '%s' available, temp tracking enabled.\n",
				 THERMAL_ZONE_NAME);
		} else {
			dev_info(&data->client->dev,
				 "Thermal zone '%s' get failed, retry later.\n",
				 THERMAL_ZONE_NAME);
		}
	}

	if (data->tz) {
		int temp_mc = 0;
		if (thermal_zone_get_temp(data->tz, &temp_mc) == 0) {
			WRITE_ONCE(data->current_temp_mc, (u32)temp_mc);
		}
	}

	/* I2C master (BMC) no-read watchdog: warn if no read for a while */
	last_read = READ_ONCE(data->last_read_jiffies);
	if (last_read &&
	    time_after(jiffies, last_read + I2C_NO_READ_TIMEOUT_SEC * HZ)) {
		dev_warn_ratelimited(&data->client->dev,
			"No I2C master read for %u s (last read %lu jiffies ago)\n",
			I2C_NO_READ_TIMEOUT_SEC, (unsigned long)(jiffies - last_read));
	}

	/* run again in 1s */
	schedule_delayed_work(&data->temp_poll_work, HZ);
}

static int spacemit_slave_callback(struct i2c_client *client,
				   enum i2c_slave_event event, u8 *val)
{
	struct spacemit_slave_data *data = i2c_get_clientdata(client);
	__le32 le_temp;
	u32 temp_val;

	switch (event) {
	case I2C_SLAVE_WRITE_REQUESTED:
		data->rx_idx = 0;
		break;

	case I2C_SLAVE_WRITE_RECEIVED:
		atomic_inc(&data->write_received_cnt);
		if (data->rx_idx < 2) {
			data->rx_buf[data->rx_idx++] = *val;
		}
		break;

	case I2C_SLAVE_READ_REQUESTED:
		atomic_inc(&data->read_requested_cnt);
		WRITE_ONCE(data->last_read_jiffies, jiffies);
		data->tx_idx = 0;

		if (data->rx_idx == 2 && data->rx_buf[0] == 0x2E) {
			if (data->rx_buf[1] == 0x03) {
				data->current_tx_len = 4;
				temp_val = READ_ONCE(data->current_temp_mc);

				le_temp = cpu_to_le32(temp_val);
				memcpy(data->tx_buf, &le_temp, 4);
			} else if (data->rx_buf[1] == 0x04) {
				unsigned int seq;

				data->current_tx_len = sizeof(struct spacemit_sys_info);
				do {
					seq = read_seqbegin(&data->info_lock);
					memcpy(data->tx_buf, &data->sys_info_pkt, data->current_tx_len);
				} while (read_seqretry(&data->info_lock, seq));
			} else {
				data->current_tx_len = 4;
				memset(data->tx_buf, 0xFF, 4);
			}
		} else {
			data->current_tx_len = 4;
			memset(data->tx_buf, 0xFF, 4);
		}

		*val = data->tx_buf[data->tx_idx++];
		break;

	case I2C_SLAVE_READ_PROCESSED:
		atomic_inc(&data->read_processed_cnt);
		if (data->tx_idx < data->current_tx_len) {
			*val = data->tx_buf[data->tx_idx++];
		} else {
			*val = 0xFF;
		}
		break;

	case I2C_SLAVE_STOP:
		atomic_inc(&data->stop_cnt);
		data->rx_idx = 0;
		data->tx_idx = 0;
		break;

	default:
		break;
	}
	return 0;
}

/* ---------------- debugfs ---------------- */

static int sys_info_show(struct seq_file *m, void *v)
{
	struct spacemit_slave_data *data = m->private;
	u8 buf[sizeof(struct spacemit_sys_info)];
	unsigned int seq;

	do {
		seq = read_seqbegin(&data->info_lock);
		memcpy(buf, &data->sys_info_pkt, sizeof(buf));
	} while (read_seqretry(&data->info_lock, seq));

	seq_hex_dump(m, "", DUMP_PREFIX_OFFSET, 16, 1, buf, sizeof(buf), false);
	return 0;
}

static int sys_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, sys_info_show, inode->i_private);
}

static const struct file_operations sys_info_fops = {
	.owner = THIS_MODULE,
	.open = sys_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int status_show(struct seq_file *m, void *v)
{
	struct spacemit_slave_data *data = m->private;
	unsigned long last_read = READ_ONCE(data->last_read_jiffies);

	seq_printf(m, "temp_mc:          %u mC\n", READ_ONCE(data->current_temp_mc));
	seq_printf(m, "fs_info_loaded:   %d\n", data->fs_info_loaded);
	seq_printf(m, "vfs_retry_count:  %d\n", data->vfs_retry_count);
	seq_printf(m, "write_received:   %d\n", atomic_read(&data->write_received_cnt));
	seq_printf(m, "read_requested:   %d\n", atomic_read(&data->read_requested_cnt));
	seq_printf(m, "read_processed:   %d\n", atomic_read(&data->read_processed_cnt));
	seq_printf(m, "stop:             %d\n", atomic_read(&data->stop_cnt));

	if (last_read)
		seq_printf(m, "last_read_ago_ms: %lu\n",
			   jiffies_to_msecs(jiffies - last_read));
	else
		seq_printf(m, "last_read_ago_ms: never\n");

	return 0;
}

static int status_open(struct inode *inode, struct file *file)
{
	return single_open(file, status_show, inode->i_private);
}

static const struct file_operations status_fops = {
	.owner = THIS_MODULE,
	.open = status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void spacemit_slave_debugfs_init(struct spacemit_slave_data *data)
{
	struct dentry *dir;

	dir = debugfs_create_dir("spacemit_i2c_slave", NULL);
	if (IS_ERR_OR_NULL(dir))
		return;

	data->dbg_dir = dir;

	debugfs_create_u32("temp_mc", 0444, dir, &data->current_temp_mc);
	debugfs_create_bool("fs_info_loaded", 0444, dir, &data->fs_info_loaded);
	debugfs_create_u32("vfs_retry_count", 0444, dir, (u32 *)&data->vfs_retry_count);
	debugfs_create_atomic_t("write_received", 0444, dir, &data->write_received_cnt);
	debugfs_create_atomic_t("read_requested", 0444, dir, &data->read_requested_cnt);
	debugfs_create_atomic_t("read_processed", 0444, dir, &data->read_processed_cnt);
	debugfs_create_atomic_t("stop", 0444, dir, &data->stop_cnt);
	debugfs_create_file("sys_info_pkt", 0444, dir, data, &sys_info_fops);
	debugfs_create_file("status", 0444, dir, data, &status_fops);
}

static void spacemit_slave_debugfs_remove(struct spacemit_slave_data *data)
{
	debugfs_remove_recursive(data->dbg_dir);
	data->dbg_dir = NULL;
}

static int spacemit_slave_probe(struct i2c_client *client)
{
	struct spacemit_slave_data *data;
	int ret;

	dev_info(&client->dev, "Initializing\n");
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);

	seqlock_init(&data->info_lock);

	data->tz = thermal_zone_get_zone_by_name(THERMAL_ZONE_NAME);
	if (IS_ERR(data->tz)) {
		dev_warn(&client->dev, "Thermal zone '%s' not available yet, will retry.\n",
			 THERMAL_ZONE_NAME);
		data->tz = NULL;
	}

	client->flags |= I2C_CLIENT_SLAVE;
	ret = i2c_slave_register(client, spacemit_slave_callback);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&data->temp_poll_work, poll_cpu_temp_work_func);
	schedule_delayed_work(&data->temp_poll_work, msecs_to_jiffies(100));

	spacemit_slave_debugfs_init(data);

	dev_info(&client->dev, "Spacemit Slave Loaded.\n");
	return 0;
}

static void spacemit_slave_remove(struct i2c_client *client)
{
	struct spacemit_slave_data *data = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&data->temp_poll_work);
	spacemit_slave_debugfs_remove(data);
	i2c_slave_unregister(client);
}

static const struct of_device_id spacemit_slave_of_match[] = {
	{ .compatible = "spacemit,i2c-slave" },
	{}
};
MODULE_DEVICE_TABLE(of, spacemit_slave_of_match);

static const struct i2c_device_id spacemit_slave_id[] = {
	{ "spacemit-i2c-slave" },
	{}
};
MODULE_DEVICE_TABLE(i2c, spacemit_slave_id);

static struct i2c_driver spacemit_slave_driver = {
	.driver = {
		.name = "spacemit_i2c_slave",
		.of_match_table = spacemit_slave_of_match,
	},
	.probe = spacemit_slave_probe,
	.remove = spacemit_slave_remove,
	.id_table = spacemit_slave_id,
};

module_i2c_driver(spacemit_slave_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C Slave Backend for Spacemit K3 System Info");
