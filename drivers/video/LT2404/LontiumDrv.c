#include "include/include.h"

/*
 * Project Name -  LT2404
 *
 * Chip    Name - LT8711UXC, LT8711UXC2, LT8711UXD, LT8711UXE1, LT8711UXE2
 *
 * The LT2404 project contains the LT8711UXC, LT8711UXC2, LT8711UXD, LT8711UXE1, LT8711UXE2 chips
*/

char *ChipName = "LT2404";

LT2404_Stu  *lt2404;
static struct task_struct *kthread_obj;
ktime_t last_low_level;
bool hpd_status = false;
static int major = 0;
static struct cdev chip_cdev;
static struct class *chip_class;

static int chip_open(struct inode *inode, struct file *filp)
{
    printk("open chip driver");
    return 0;
}

static int chip_close (struct inode *node, struct file *file)
{
	printk("close chip driver");
	return 0;
}

/**
 ***************************************************************************************************************
 * chip_read - Handles read operations from user space
 * @filp: Pointer to the file structure that was opened
 * @buf: Pointer to the user buffer containing the address to read from
 * @cnt: Number of bytes to read (should equal the size of Chip_Control_Args)
 * @offt: Pointer to the offset in the file (not used in this function)
 *
 * This function reads an address from the user buffer, calls the ReadI2C_Byte
 * function to read the value at that address, and writes the value back to
 * the user space buffer.
 *
 * Returns: Number of bytes written back on success, or -EFAULT on failure.
 ***************************************************************************************************************
 */
static ssize_t chip_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	Chip_Control_Args parameter;

	mutex_lock(&lt2404->ocm_lock);
	if (copy_from_user(&parameter, buf, sizeof(parameter)))
		return -EFAULT;

	parameter.value = ReadI2C_Byte(parameter.address);

	if (copy_to_user(buf, &parameter, sizeof(parameter)))
		return -EFAULT;

	mutex_unlock(&lt2404->ocm_lock);

	return sizeof(parameter);
}

/**
 *******************************************************************************************************************
 * chip_write - Handles write operations from user space to the I2C chip
 * @filp: Pointer to the file structure representing the open file
 * @buf: Pointer to the user buffer containing the data to be written
 * @cnt: Number of bytes to write (expected to be equal to the size of Chip_Control_Args)
 * @offt: Pointer to the offset in the file (not used in this function)
 *
 * This function reads a Chip_Control_Args structure from the user buffer,
 * extracts the address and value, and then writes the value to the specified
 * register address on the I2C device using the WriteI2C_Byte function.
 *
 * Returns: The number of bytes written on success, or -EFAULT if the copy from user fails.
 *****************************************************************************************************************
 */
static ssize_t chip_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	Chip_Control_Args parameter;

	mutex_lock(&lt2404->ocm_lock);

	if (copy_from_user(&parameter, buf, sizeof(parameter)))
		return -EFAULT;

	WriteI2C_Byte(parameter.address, parameter.value);

	mutex_unlock(&lt2404->ocm_lock);

	return sizeof(parameter);
}

//Define the device operation interface.
static struct file_operations chip_drv = {
	.owner	 = THIS_MODULE,
	.open    = chip_open,
	.release = chip_close,
	.read    = chip_read,
	.write   = chip_write,
};

//Register the file_operations struct into the kernel
//Creating a Device Node -/dev/ChipName
static int  chip_dev_init(void)
{
	int ret;
	dev_t devid;

	ret = alloc_chrdev_region(&devid, 0, 1, ChipName);
	if (ret < 0)
		return ret;

	major = MAJOR(devid);
	cdev_init(&chip_cdev, &chip_drv);
	cdev_add(&chip_cdev, devid, 1);
	chip_class = class_create("chip_class");
	if (IS_ERR(chip_class)) {
		unregister_chrdev(major, ChipName);
		return -1;
	}

	device_create(chip_class, NULL, MKDEV(major, 0), NULL, ChipName); 

	return 0;
}

static void  chip_dev_exit(void)
{
	device_destroy(chip_class, MKDEV(major, 0));
	class_destroy(chip_class);
	cdev_del(&chip_cdev);
	unregister_chrdev_region(MKDEV(major, 0), 1);
}

static const struct regmap_range chip_ranges[] = {
	{ .range_min = 0, .range_max = 0xffff },
};

static const struct regmap_access_table chip_table = {
	.yes_ranges = chip_ranges,
	.n_yes_ranges = ARRAY_SIZE(chip_ranges),
};

static const struct regmap_config chip_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &chip_table,
	.cache_type = REGCACHE_NONE,
};

/**
 * chip_parse_dts - Parse device tree to retrieve GPIO handles.
 * @dev: Pointer to the device structure representing the device.
 *
 * This function retrieves the GPIO handles for power, reset, and interrupt
 * signals defined in the device tree. It uses the `devm_gpiod_get_optional`
 * function to obtain these handles, setting default states where applicable.
 *
 * If any GPIO retrieval fails, an error message is logged, and an appropriate
 * error code is returned.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int chip_parse_dts(struct device *dev)
{
	//Gets the handle to power gpio in dts
	lt2404->power_gpio = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_HIGH);
	if (IS_ERR(lt2404->power_gpio)) {
		dev_err(dev, "Failed to get power GPIO\n");
		return PTR_ERR(lt2404->power_gpio);  // Return error code from the pointer
	}

	//Gets the handle to reset gpio in dts
	lt2404->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt2404->reset_gpio)) {
		dev_err(dev, "Failed to get reset GPIO\n");
		return PTR_ERR(lt2404->reset_gpio);
	}

	return 0;
}

static int chip_probe(struct i2c_client *client)
{
	int status;

	chip_dev_init();

	lt2404 = devm_kzalloc(&client->dev, sizeof(*lt2404), GFP_KERNEL);
	if (lt2404 == NULL)
		return -ENOMEM;

	lt2404->trans_i2c = client;
	lt2404->dev = &client->dev;

	lt2404->chip_regmap = devm_regmap_init_i2c(client, &chip_regmap_config);
	if (IS_ERR(lt2404->chip_regmap)) {
		dev_err(&client->dev, "Failed to initialize regmap\n");
		return PTR_ERR(lt2404->chip_regmap);
	}

	status = chip_parse_dts(lt2404->dev);
	if (status) {
		dev_err(&client->dev, "Failed to parse device tree: %d\n", status);
		return status;
	}

	i2c_set_clientdata(client, lt2404);
	mutex_init(&lt2404->ocm_lock);

	kthread_obj = kthread_run(LT2404_Main, NULL, "lt2404_kthread");
	if (IS_ERR(kthread_obj)) {
		dev_err(&client->dev, "Failed to create kernel thread\n");
		return PTR_ERR(kthread_obj);
	}

	return 0;
}

static void chip_remove(struct i2c_client *client)
{  
	mutex_destroy(&lt2404->ocm_lock);
	chip_dev_exit();
	printk("driver removed\n");
}

static int chip_suspend(struct device *dev)
{
	gpiod_set_value(lt2404->reset_gpio, 0);
	printk(KERN_INFO "%s Suspend", ChipName);

	return 0;
}

static int chip_resume(struct device *dev)
{
	//or use this
	gpiod_set_value(lt2404->reset_gpio, 1);
	printk(KERN_INFO "%s Resume", ChipName);

	return 0;
}

static const struct dev_pm_ops chip_pm_ops = {
	.suspend = chip_suspend,
	.resume =  chip_resume,
};

static const struct i2c_device_id chip_ids[] = {
	{"lontium,lt2404", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, chip_ids);

static const struct of_device_id chip_id_table[] = {
	{.compatible = "lontium,lt2404"},
	{ }
};
MODULE_DEVICE_TABLE(of, chip_id_table);

static struct i2c_driver chip_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "lt2404",
		.pm = &chip_pm_ops,
		.of_match_table = chip_id_table,
	},
	.probe    = chip_probe,
	.remove   = chip_remove,
	.id_table = chip_ids,
};
module_i2c_driver(chip_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LT2404 driver");
MODULE_AUTHOR("Qizhong Cheng <qzcheng@lontium.com>");
