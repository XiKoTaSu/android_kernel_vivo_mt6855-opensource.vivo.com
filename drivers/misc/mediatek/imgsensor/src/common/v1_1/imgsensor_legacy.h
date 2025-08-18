/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
//vivo lishuo add for otp remake 20210823 [B210820-3030] begin
#ifndef __IMGSENSOR_LEGACY_H__
#define __IMGSENSOR_LEGACY_H__

extern int iReadRegI2C(u8 *a_pSendData, u16 a_sizeSendData,u8 *a_pRecvData, u16 a_sizeRecvData, u16 i2cId);

extern bool otp_read(kal_uint16 reg_addr, kal_uint16 length, BYTE *data, kal_uint8 slave_addr);
extern int iReadRegI2CTiming(u8 *a_pSendData, u16 a_sizeSendData, u8 *a_pRecvData, u16 a_sizeRecvData, u16 i2cId, u16 timing);

extern int iWriteRegI2C(u8 *a_pSendData, u16 a_sizeSendData, u16 i2cId);
extern int iWriteRegI2CTiming(u8 *a_pSendData, u16 a_sizeSendData, u16 i2cId, u16 timing);

extern int iBurstWriteReg_multi(u8 *pData, u32 bytes, u16 i2cId, u16 transfer_length, u16 timing);

#endif
//vivo lishuo add for otp remake 20210823 [B210820-3030] end

