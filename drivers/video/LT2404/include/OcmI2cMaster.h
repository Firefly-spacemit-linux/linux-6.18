#ifndef	  _OCMI2CMASTER_H_
#define	  _OCMI2CMASTER_H_

#define MAX_NUMBER_BYTES  128


extern int WriteI2C_Byte(u8 addr,u8 val);
extern int ReadI2C_Byte(u8 addr);


extern int WriteI2C_ByteN(u8 addr, void *val, size_t len);
extern int ReadI2C_ByteN(u8 addr,void *val,size_t len);



#endif