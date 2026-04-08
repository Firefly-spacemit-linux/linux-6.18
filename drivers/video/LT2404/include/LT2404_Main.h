#ifndef  _LT2404_FW_UPGRADE_H_
#define  _LT2404_FW_UPGRADE_H_

#define UPGRADE  1
#define NOT_UPGRADE  0



typedef struct
{
    u8 Width;
    u32  Poly;
    u32  CrcInit;
    u32  XorOut;
    bool RefIn;
    bool RefOut;
}CrcInfoTypeS;


extern int LT2404_Main(void *data);

extern void LT2404_Power_On(void);

extern void LT2404_Reset(void);

extern void LT2404_Config_Parameters(void);
extern void LT2404_Wren(void);
extern void LT2404_Wrdi(void);
extern void LT2404_I2C_To_Fifo(void);
extern  void LT2404_I2C_To_Sram(void);
extern void LT2404_Sram_To_Flash(u64 addr);
extern void LT2404_Flash_To_Fifo(u64 addr);
extern void LT2404_Fifo_To_Flash(u64 addr);

extern void LT2404_Load_Fw_To_Sram(void);

extern void LT2404_Block_Erase(void);

extern int LT2404_Write_Data(const  u8 *pfile, u64 filesize, u64 addr);
extern int LT2404_Write_CRC(u8 crc, u64 filesize, u64 addr);


extern int LT2404_Upgrade_Judgment(void);

extern u64 LT2404_Read_Version(void);

extern int LT2404_Firmware_Upgrade(void);

extern void LT2404_I2C_Enable(void);
extern void LT2404_I2C_Disable(void);

extern void LT2404_Upgrade_Result(void);

extern int LT2404_Prepare_Firmware_Data(void);
extern u8 Flash_ReadFlashStatusReg(void);

extern u8 calculate_crc(const u8 *upgradeData, u64 len);

extern unsigned int GetCRC(CrcInfoTypeS type, const  u8 *buf, u64 bufLen);

extern const struct firmware *fw;

#endif
