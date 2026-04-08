#ifndef _LONTIUMDRV_H_
#define _LONTIUMDRV_H_


 typedef struct {
    struct device *dev;
	struct mutex ocm_lock;
	struct gpio_desc *reset_gpio;
    struct gpio_desc *power_gpio;
	struct gpio_desc *interrupt_gpio;
	struct i2c_client *trans_i2c;
	struct regmap *chip_regmap;
}LT2404_Stu;



typedef struct
{
    u8 address;
    u8 value; 
} Chip_Control_Args;

typedef struct  {
    u16 Hact;
    u16 Vact;
	u8 framerate;
}Resolution_Stu;


typedef struct EDID_Info {
    u8 *data;   // 指向 EDID 数据的指针
    int length; // EDID 数据长度
}EDID_Info_Stu;



extern  LT2404_Stu  *lt2404;
extern char *ChipName;





#endif
