#include "include/include.h"

#define FW_FILE			"LT2404.bin"
#define LT2404_SRAM_PAGE_SIZE	256
#define FW_BUFF_SIZE		(64 * 1024)      //64KB Firmware area size

const struct firmware *fw = NULL;
u8 Fw_Crc_Value;
u8 *fw_buffer;

/**
 *******************************************************************************
 * @brief   bit reverse
 * @param   [in] inVal	- in data
 * @param   [in] bits   - inverse bit
 * @return  out data
 * @note
 *******************************************************************************
 */
unsigned int BitsReverse(u32 inVal, u8 bits)
{
	u32 outVal = 0;
	u8 i;

	for (i = 0; i < bits; i++) {
		if (inVal & (1 << i))
			outVal |= 1 << (bits - 1 - i);
	}

	return outVal;
}

/**
 *******************************************************************************
 * @brief   get crc value
 * @param   [in] type   - CRC config
 * @param   [in] *buf   - data buffer
 * @param   [in] bufLen - data len
 * @return  crc value
 * @note
 *******************************************************************************
 */
unsigned int GetCRC(CrcInfoTypeS type, const u8 *buf, u64 bufLen)
{
	u8 width  = type.Width;
	u32  poly   = type.Poly;
	u32  crc    = type.CrcInit;
	u32  xorout = type.XorOut;
	bool refin  = type.RefIn;
	bool refout = type.RefOut;
	u8 n;
	u32  bits;
	u32  data;
	u8 i;

	n    =  (width < 8) ? 0 : (width - 8);
	crc  =  (width < 8) ? (crc<<(8 - width)) : crc;
	bits =  (width < 8) ? 0x80 : (1 << (width - 1));
	poly =  (width < 8) ? (poly<<(8 - width)) : poly;
	while (bufLen--)
	{
		data = *(buf++);
		if (refin == true)
			data = BitsReverse(data, 8);
		crc ^= (data << n);
		for(i = 0; i < 8; i++)
		{
			if(crc & bits) {
				crc = (crc << 1) ^ poly;
			} else {
				crc = crc << 1;
			}
		}
	}
	crc = (width < 8) ? (crc >> (8 - width)) : crc;
	if (refout == true)
	crc = BitsReverse(crc, width);
	crc ^= xorout;

	return (crc & ((2 << (width - 1)) - 1));
}

u8 calculate_crc(const u8 *upgradeData, u64 len)
{
	CrcInfoTypeS type = {
		.Width = 8,
		.Poly  = 0x31,
		.CrcInit = 0,
		.XorOut = 0,
		.RefOut = false,
		.RefIn = false,
	};
	u64 crc_size = FW_BUFF_SIZE - 1;
	u8 default_val = 0xFF;

	type.CrcInit = GetCRC(type, upgradeData, len);

	crc_size -= len;
	while(crc_size--)
	{
		type.CrcInit = GetCRC(type, &default_val, 1);
	}

	return type.CrcInit;
}

void LT2404_I2C_Enable(void)
{
	WriteI2C_Byte(0xff, 0xe0);
	WriteI2C_Byte(0xee, 0x01);
}

void LT2404_I2C_Disable(void)
{
	WriteI2C_Byte(0xff, 0xe0);
	WriteI2C_Byte(0xee, 0x00);
}

void LT2404_Power_On(void)
{
	gpiod_set_value(lt2404->power_gpio, 1);
	msleep(10);
}

void LT2404_Reset(void)
{
	gpiod_set_value(lt2404->reset_gpio, 1);
	msleep(5);
	gpiod_set_value(lt2404->reset_gpio, 0);
	msleep(5);
	gpiod_set_value(lt2404->reset_gpio, 1);
	msleep(5);

	printk("LT2404: reset chip\n");
}

u64 LT2404_Read_Version(void)
{
	u64 version = 0;

	WriteI2C_Byte(0xff, 0xe0);
	version = ((version << 8) | ReadI2C_Byte(0x80));
	version = ((version << 8) | ReadI2C_Byte(0x81));
	version = ((version << 8) | ReadI2C_Byte(0x82));
	version = ((version << 8) | ReadI2C_Byte(0x83));

	return version;
}

void LT2404_Config_Parameters(void)
{
	WriteI2C_Byte(0xFF, 0xE0);
	WriteI2C_Byte(0x5E, 0xC1);
	WriteI2C_Byte(0x58, 0x00);
	WriteI2C_Byte(0x59, 0x50);
	WriteI2C_Byte(0x5A, 0x10);
	WriteI2C_Byte(0x5A, 0x00);
	WriteI2C_Byte(0x58, 0x21);
}

void LT2404_Wren(void)
{
	WriteI2C_Byte(0x5a, 0x04);
	WriteI2C_Byte(0x5a, 0x00);
}

void LT2404_Wrdi(void)
{
	WriteI2C_Byte(0x5A, 0x08);
	WriteI2C_Byte(0x5A, 0x00);
}

void LT2404_I2C_To_Fifo(void)
{
	WriteI2C_Byte(0x5e, 0xc2);
	WriteI2C_Byte(0x5a, 0x20);
	WriteI2C_Byte(0x5a, 0x00);
	WriteI2C_Byte(0x58, 0x21);
}

void LT2404_I2C_To_Sram(void)
{
	WriteI2C_Byte(0xff, 0xe0);
	WriteI2C_Byte(0xee, 0x01);
	WriteI2C_Byte(0x52, 0x00);
	WriteI2C_Byte(0x53, 0x00);
	WriteI2C_Byte(0x55, 0x00);

	WriteI2C_Byte(0x5a, 0x04);
	WriteI2C_Byte(0x5a, 0x00);
	WriteI2C_Byte(0x51, 0xff);
	WriteI2C_Byte(0x55, 0x80);
	WriteI2C_Byte(0x5e, 0xc0);
	WriteI2C_Byte(0x58, 0x21);
}

void LT2404_CRC_To_Sram(void)
{
	WriteI2C_Byte(0x51, 0x00);
	WriteI2C_Byte(0x55, 0x80);
	WriteI2C_Byte(0x5e, 0xc0);
	WriteI2C_Byte(0x58, 0x21);
}

void LT2404_Sram_To_Flash(u64 addr)
{
	WriteI2C_Byte(0x5b, ((addr & 0xFF0000) >> 16));
	WriteI2C_Byte(0x5c, ((addr & 0xFF00) >> 8));
	WriteI2C_Byte(0x5d, (addr & 0xFF));
	WriteI2C_Byte(0x5A, 0x30);
	WriteI2C_Byte(0x5A, 0x00);
	WriteI2C_Byte(0x55, 0x00);
}

void LT2404_Flash_To_Fifo(u64 addr)
{
	WriteI2C_Byte(0x5e, 0x42);
	WriteI2C_Byte(0x5a, 0x20);
	WriteI2C_Byte(0x5a, 0x00);
	WriteI2C_Byte(0x5b, ((addr & 0xFF0000) >> 16));
	WriteI2C_Byte(0x5c, ((addr & 0xFF00) >> 8));
	WriteI2C_Byte(0x5d, (addr & 0xFF));
	WriteI2C_Byte(0x5a, 0x10);
	WriteI2C_Byte(0x5a, 0x00);
}

void LT2404_Fifo_To_Flash(u64 addr)
{
	WriteI2C_Byte(0x5b, ((addr & 0xFF0000) >> 16));
	WriteI2C_Byte(0x5c, ((addr & 0xFF00) >> 8));
	WriteI2C_Byte(0x5d, (addr & 0xFF));
	WriteI2C_Byte(0x5a, 0x10);
	WriteI2C_Byte(0x5a, 0x00);
}

u8 Flash_ReadFlashStatusReg(void)
{
	u8 ucFlashStatusReg = 0;

	WriteI2C_Byte(0xFF, 0xE1);//fifo_rst_n
	WriteI2C_Byte(0x03, 0x3F);
	WriteI2C_Byte(0x03, 0xFF);

	WriteI2C_Byte(0xFF, 0xE0);
	WriteI2C_Byte(0x5e, 0x41);
	WriteI2C_Byte(0x5a, 0x20);
	WriteI2C_Byte(0x5a, 0x00);
	WriteI2C_Byte(0x56, 0x05);//opcode=read status register
	WriteI2C_Byte(0x55, 0x24);//[5]=rd_reg_flag;[2]=opcode_en;[1:0]=cmd_len
	WriteI2C_Byte(0x55, 0x00);
	WriteI2C_Byte(0x58, 0x21);
	ucFlashStatusReg = ReadI2C_Byte(0x5f);
	printk("ucFlashStatusReg:%x\n", ucFlashStatusReg);

	return ucFlashStatusReg;
}

void LT2404_Block_Erase(void)
{
	u32 i = 0;
	u8 ucFlashStatuss = 0;
	u8 ucBlockNum = 0x00;
	u32 ulFlashAddr = 0x00;

	for (ucBlockNum = 0; ucBlockNum < 1; ucBlockNum++)
	{
		ulFlashAddr = ucBlockNum * 0x008000;
		WriteI2C_Byte(0xFF, 0xE0);
		WriteI2C_Byte(0xEE, 0x01);
		WriteI2C_Byte(0x5A, 0x04);
		WriteI2C_Byte(0x5A, 0x00);
		WriteI2C_Byte(0x5B, ulFlashAddr >> 16);//set flash address[23:16]
		WriteI2C_Byte(0x5C, ulFlashAddr >> 8);//set flash address[15:8]
		WriteI2C_Byte(0x5D, ulFlashAddr);//set flash address[7:0]
		WriteI2C_Byte(0x5A, 0x01);
		WriteI2C_Byte(0x5A, 0x00);
		msleep(100); //delay 100ms
		i = 0;
		while (1) {
			ucFlashStatuss = Flash_ReadFlashStatusReg(); //wait erase finish
			if ((ucFlashStatuss & 0x01) == 0)
			{
				break;
			}

			if(i > 50)
				break;
			i++;
			msleep(50); //delay 50ms
		}
	}

	printk("erase flash done.\n");
}

void LT2404_Load_Fw_To_Sram(void)
{
	WriteI2C_Byte(0xff, 0xe0);
	WriteI2C_Byte(0xee, 0x01);
	WriteI2C_Byte(0xff, 0xe1);
	WriteI2C_Byte(0x03, 0xdf);
	WriteI2C_Byte(0x03, 0xff);

	msleep(30);
	WriteI2C_Byte(0xff, 0xe0);
	WriteI2C_Byte(0xee, 0x01);
	WriteI2C_Byte(0x0d, 0x20);
	WriteI2C_Byte(0xff, 0x20);
	WriteI2C_Byte(0xff, 0xe0);
	WriteI2C_Byte(0x0d, 0x21);
	WriteI2C_Byte(0xff, 0x20);
	WriteI2C_Byte(0xff, 0xe0);
	WriteI2C_Byte(0x0d, 0x22);
	WriteI2C_Byte(0xff, 0x20);
}

void LT2404_Fifo_Reset(void)
{
	WriteI2C_Byte(0xFF, 0xE1);
	WriteI2C_Byte(0x03, 0xBF);
	WriteI2C_Byte(0x03, 0xFF);
	WriteI2C_Byte(0xFF, 0xE0);
}

int LT2404_Write_Data(const u8 *pfile, u64 filesize, u64 addr)
{
	int page = 0, num = 0, i = 0;

	page = (filesize % LT2404_SRAM_PAGE_SIZE) ?
		((filesize / LT2404_SRAM_PAGE_SIZE) + 1) :
		(filesize / LT2404_SRAM_PAGE_SIZE);
	if (page * LT2404_SRAM_PAGE_SIZE > 32 * 1024)
	{
		printk("File size is out of range!\n");
		return -1;
	}

	printk("Writing to SRAM: %u pages, total size = %llu bytes\n", page, filesize);
	for (num = 0; num < page; num++){
		LT2404_I2C_To_Sram();
		for (i = 0; i < LT2404_SRAM_PAGE_SIZE; i++){
			if((num * LT2404_SRAM_PAGE_SIZE + i) < filesize) {
				if (WriteI2C_Byte(0x59, *(pfile + (num *
				    LT2404_SRAM_PAGE_SIZE + i))) < 0) {
					printk("Error writing data at page %u, index %u\n", num, i);
					return -1;
				}
			} else {
				WriteI2C_Byte(0x59, 0xFF);
			}
		}
		LT2404_Wren();
		LT2404_Sram_To_Flash(addr);
		addr += LT2404_SRAM_PAGE_SIZE;
	}
	LT2404_Wrdi();

	return 0;
}

int LT2404_Write_CRC(u8 crc, u64 filesize, u64 addr)
{
	LT2404_CRC_To_Sram();
	WriteI2C_Byte(0x59, crc);
	LT2404_Wren();
	LT2404_Sram_To_Flash(addr);
	LT2404_Wrdi();

	return 0;
}

int LT2404_Upgrade_Judgment(void)
{
	u64 addr = FW_BUFF_SIZE - 1;
	u8 Read_Flash_Crc_Value;
	u8 CrcResult;

	//get crc from LTChip
	WriteI2C_Byte(0xff, 0xe0);
	CrcResult = ReadI2C_Byte(0x21);  //read 1 byte of CRC (calculated by chip hardware)
	printk("Cal original CRC: 0x%02X\n", CrcResult);

	LT2404_Config_Parameters();
	LT2404_Flash_To_Fifo(addr);

	//fifo to i2c
	WriteI2C_Byte(0x58, 0x21);
	Read_Flash_Crc_Value = ReadI2C_Byte(0x5f);
	udelay(150);

	LT2404_Wrdi();
	printk("Read Flash Firmware Crc=0x%02X\n", Read_Flash_Crc_Value);
	if (Fw_Crc_Value == Read_Flash_Crc_Value)
		return NOT_UPGRADE;
	else
		return UPGRADE;
}

void LT2404_Upgrade_Result(void)
{
	u8 CrcResult;

	//get crc from LTChip
	WriteI2C_Byte(0xff, 0xe0);
	CrcResult = ReadI2C_Byte(0x21);  //read 1 byte of CRC (calculated by chip hardware)
	printk("CrcResult CRC: 0x%02X\n", CrcResult);
	if (CrcResult == Fw_Crc_Value) {
		printk("LT2404 Upgrade Success\n");
	} else {
		printk("LT2404 Upgrade Failed\n");
	}
}



int LT2404_Firmware_Upgrade(void)
{
	int ret;

	LT2404_Config_Parameters();
	LT2404_Block_Erase();

	//SRAM write
	ret = LT2404_Write_Data(fw->data, fw->size, 0);
	if (ret < 0) {
		printk("Failed to write firmware data: %d\n", ret);
		return ret;
	}

	//FIFO write
	LT2404_Config_Parameters();
	ret = LT2404_Write_CRC(Fw_Crc_Value, 1, FW_BUFF_SIZE - 1);
	if (ret < 0) {
		printk("Failed to write CRC: %d\n", ret);
		return ret;
	}

	printk("Write Data done\n");
	return 0;
}


int LT2404_Prepare_Firmware_Data(void)
{
	int ret;

	ret = request_firmware(&fw, FW_FILE, lt2404->dev);
	if (ret) {
		printk("Failed to load firmware: %d\n", ret);
		return ret;
	}

	printk("Firmware loaded, size: %zu bytes\n", fw->size);

	if (fw->size > FW_BUFF_SIZE - 1) {
		printk("Firmware size exceeds 64KB limit\n");
		release_firmware(fw);
		return -1;
	}

	Fw_Crc_Value = calculate_crc(fw->data, fw->size);
	printk("/lib/firmware/  CRC: 0x%02X\n", Fw_Crc_Value);
	return 0;
}

int LT2404_Main(void *data)
{
	int ret;

	mutex_lock(&lt2404->ocm_lock);

	LT2404_Power_On();
	msleep(1000);
	LT2404_I2C_Enable();

	ret = LT2404_Prepare_Firmware_Data();
	if (ret < 0) {
		printk("Failed to prepare firmware data: %d\n", ret);
		LT2404_I2C_Disable();
		mutex_unlock(&lt2404->ocm_lock);
		return ret;
	}

	ret = LT2404_Upgrade_Judgment();
	if (ret == UPGRADE) {
		printk("The CRC is different, need to upgrade the firmware\n");
		ret = LT2404_Firmware_Upgrade();
		if (ret < 0) {
			printk("Upgrade failure\n");
			LT2404_I2C_Disable();
			mutex_unlock(&lt2404->ocm_lock);
			return ret;
		} else {
			LT2404_Load_Fw_To_Sram();
			LT2404_Upgrade_Result();
			LT2404_Wrdi();
		}
	} else if (ret == NOT_UPGRADE) {
		printk("CRC is same, not need update\n");
	} else {
		printk("Upgrade judgment failure\n");
		LT2404_I2C_Disable();
		mutex_unlock(&lt2404->ocm_lock);
		return ret;
	}

	LT2404_I2C_Disable();
	LT2404_Reset();
	release_firmware(fw);

	mutex_unlock(&lt2404->ocm_lock);

	return 0;
}

