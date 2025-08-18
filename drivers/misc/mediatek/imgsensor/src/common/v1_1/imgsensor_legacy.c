// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include "kd_camera_typedef.h"
#include "imgsensor_i2c.h"

#ifdef IMGSENSOR_LEGACY_COMPAT
void kdSetI2CSpeed(u16 i2cSpeed)
{

}

int iReadRegI2C(u8 *a_pSendData, u16 a_sizeSendData,
		u8 *a_pRecvData, u16 a_sizeRecvData,
		u16 i2cId)
{
	if (imgsensor_i2c_get_device() == NULL)
		return IMGSENSOR_RETURN_ERROR;

	return imgsensor_i2c_read(
			imgsensor_i2c_get_device(),
			a_pSendData,
			a_sizeSendData,
			a_pRecvData,
			a_sizeRecvData,
			i2cId,
			IMGSENSOR_I2C_SPEED);
}

//IIC is grounded, the camera can be opend ,check iic timeout
    int check_i2c_timeout(u16 addr, u16 i2cid)
    {
    int ret, temp;
    u8 pu_send_cmd[2];
    u16 get_byte;
    struct IMGSENSOR_I2C_CFG *pdevice = imgsensor_i2c_get_device();
    if ( pdevice == NULL||pdevice->pinst->pi2c_client == NULL){
    	pr_err("[%s] check_i2c_timeout pdevice =null \n",__FUNCTION__);
        return IMGSENSOR_RETURN_ERROR;
    	}

    temp = pdevice->pinst->pi2c_client->adapter->timeout;
    pdevice->pinst->pi2c_client->adapter->timeout = HZ/20;

    if(addr&0xff00){
        get_byte = 2;
        pu_send_cmd[0] = (addr>> 8);
        pu_send_cmd[1] = (addr & 0xff);
    } else {
        get_byte = 1;
        pu_send_cmd[0] = (addr & 0xff);
    }
    ret = iReadRegI2C(pu_send_cmd,get_byte,(u8*)&get_byte,1,i2cid);
    	//iReadRegI2C(pusendcmd , 2, (u8*)&get_byte, 2, imgsensor.i2c_write_id);
    pdevice->pinst->pi2c_client->adapter->timeout = temp;
	pr_info("[%s]  read get_byte = 0x%x  ret =%d \n",__FUNCTION__,get_byte,ret);
    return (ret==-2)?1:0;
    }
//IIC is grounded, the camera can be opend ,check iic timeout

int iReadRegI2CTiming(u8 *a_pSendData, u16 a_sizeSendData, u8 *a_pRecvData,
			u16 a_sizeRecvData, u16 i2cId, u16 timing)
{
	if (imgsensor_i2c_get_device() == NULL)
		return IMGSENSOR_RETURN_ERROR;

	return imgsensor_i2c_read(
			imgsensor_i2c_get_device(),
			a_pSendData,
			a_sizeSendData,
			a_pRecvData,
			a_sizeRecvData,
			i2cId,
			timing);
}

int iWriteRegI2C(u8 *a_pSendData, u16 a_sizeSendData, u16 i2cId)
{
	if (imgsensor_i2c_get_device() == NULL)
		return IMGSENSOR_RETURN_ERROR;

	return imgsensor_i2c_write(
			imgsensor_i2c_get_device(),
			a_pSendData,
			a_sizeSendData,
			a_sizeSendData,
			i2cId,
			IMGSENSOR_I2C_SPEED);
}

int iWriteRegI2CTiming(u8 *a_pSendData, u16 a_sizeSendData,
			u16 i2cId, u16 timing)
{
	if (imgsensor_i2c_get_device() == NULL)
		return IMGSENSOR_RETURN_ERROR;

	return imgsensor_i2c_write(
			imgsensor_i2c_get_device(),
			a_pSendData,
			a_sizeSendData,
			a_sizeSendData,
			i2cId,
			timing);
}

int iBurstWriteReg(u8 *pData, u32 bytes, u16 i2cId)
{
	if (imgsensor_i2c_get_device() == NULL)
		return IMGSENSOR_RETURN_ERROR;

	return imgsensor_i2c_write(
			imgsensor_i2c_get_device(),
			pData,
			bytes,
			bytes,
			i2cId,
			IMGSENSOR_I2C_SPEED);
}

int iBurstWriteReg_multi(u8 *pData, u32 bytes, u16 i2cId,
				u16 transfer_length, u16 timing)
{
	if (imgsensor_i2c_get_device() == NULL)
		return IMGSENSOR_RETURN_ERROR;

	return imgsensor_i2c_write(
			imgsensor_i2c_get_device(),
			pData,
			bytes,
			transfer_length,
			i2cId,
			timing);
}

//vivo lishuo add for otp remake 20210823 [B210820-3030] begin
#define MAX_DMA_TRANS_SIZE 255
bool otp_read(kal_uint16 reg_addr, kal_uint16 length, BYTE *data, kal_uint8 slave_addr)
{
	kal_uint16 i=0, j = 0,rest_num = 0, start_addr = reg_addr;

	char pu_send_cmd[2] = {(char)(start_addr >> 8),  (char)(start_addr & 0xFF)};
    if(data == NULL){
        return false;
    }

	j = length / MAX_DMA_TRANS_SIZE;
    rest_num = length%MAX_DMA_TRANS_SIZE;
	while(i < j){
		if (iReadRegI2C(pu_send_cmd,  2,  (u8 *)(data+i*MAX_DMA_TRANS_SIZE),  MAX_DMA_TRANS_SIZE,  slave_addr) < 0) {
			return false;
		}
		start_addr = reg_addr+i*MAX_DMA_TRANS_SIZE;
		pu_send_cmd[0]=(char)(start_addr >> 8);
		pu_send_cmd[1]=(char)(start_addr & 0xFF);
        i++;
	}

	if(rest_num > 0){
		if (iReadRegI2C(pu_send_cmd,  2,  (u8 *)(data+i*MAX_DMA_TRANS_SIZE),  rest_num,  slave_addr) < 0) {
			return false;
		}
	}
	printk("[otp_read]read slave%02x over,length %d\n",slave_addr,length);

    return true;
}
//vivo lishuo add for otp remake 20210823 [B210820-3030] end

#endif
