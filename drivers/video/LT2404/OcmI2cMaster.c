#include "include/include.h"

int WriteI2C_Byte(u8 addr,u8 val)
{
	int ret = 0;

	ret = regmap_write(lt2404->chip_regmap, addr, val);
	if (ret < 0) {
		printk("regmap_write error: i2c addr=0x%02x, reg addr=0x%02x, error=%d",
			lt2404->trans_i2c->addr, addr,ret);
		return ret;
	}

	return 0;
}

int ReadI2C_Byte(u8 addr)
{
	int ret = 0;
	unsigned int val = 0;

	ret = regmap_read(lt2404->chip_regmap, addr, &val);
	if (ret < 0) {
		printk("regmap_read error: i2c addr=0x%02x, reg addr=0x%02x, error=%d",
		lt2404->trans_i2c->addr, addr, ret);
		return ret;
	}

	return (u8)val;
}

int WriteI2C_ByteN(u8 addr, void *val, size_t len)
{
	int ret = 0;

	ret = regmap_bulk_write(lt2404->chip_regmap, addr, val,len );
	if (ret < 0) {
		printk("regmap_bulk_write error: i2c addr=0x%02x, reg addr=0x%02x, error=%d",
			lt2404->trans_i2c->addr, addr, ret);
		return ret;
	}

	return 0;
}

int ReadI2C_ByteN(u8 addr,void *val, size_t len)
{
	int ret;

	ret = regmap_bulk_read(lt2404->chip_regmap, addr, val, len);
	if (ret) {
		printk("regmap_bulk_read error: i2c addr=0x%02x, reg addr=0x%02x, error=%d",
			lt2404->trans_i2c->addr, addr, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(WriteI2C_Byte);
EXPORT_SYMBOL(ReadI2C_Byte);
