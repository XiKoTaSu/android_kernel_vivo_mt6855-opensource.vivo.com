#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/types.h>

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "../imgsensor_i2c.h"
#include "gc02m1bpd2278mipimono_Sensor.h"

#define PFX "MAIN2[2E2]_EEPROM_OTP"
#define LOG_INF(format,  args...)	pr_debug(PFX "[%s] " format,  __FUNCTION__,  ##args)

extern void kdSetI2CSpeed(u16 i2cSpeed);
#define VIVO_OTP_DATA_SIZE 0x11
#define VIVO_EEPROM_WRITE_ID 0x6e
#define VIVO_I2C_SPEED 400
#define VIVO_OTP_ADDR_SIZE 0x11

#define MAX_FPC_SIZE 12
#define MAX_SN_SIZE 24 + MAX_FPC_SIZE*2
#define MAX_MATERIAL_SIZE 8
#define MAX_FUSEID_SIZE (MAX_SN_SIZE+MAX_MATERIAL_SIZE)
#define MAX_Module_SIZE 13


unsigned int vivo_otp_data_gc02m1bpd2278[VIVO_OTP_DATA_SIZE];
static unsigned const int SN_addr = 0x01;
static unsigned const int FPC_addr = 0x0D;

static int checksum = 1;
extern char sn_inf_main2_gc02m1bpd2278[MAX_SN_SIZE + 1];
extern char material_inf_main2_gc02m1bpd2278[MAX_MATERIAL_SIZE + 1];
extern char fuse_id_main2_gc02m1bpd2278[MAX_FUSEID_SIZE + 1];
extern char module_tag_main2_gc02m1bpd2278[MAX_Module_SIZE + 1];
char fpc_main2_gc02m1bpd2278[MAX_FPC_SIZE + 1];

struct CAM_MODULE_INFO {
  char MODULE_ID;
  char MODULE_NAME[13];
};

static struct CAM_MODULE_INFO module_info[]={
  {'S', "Sunny"},
  {'T', "Truly"},
  {'Q', "Qtech"},
  {'A', "Semco"},
  {'F', "Ofilm"},
  {'W', "Sunwin"},
  {'C', "Shinephotics"},
  {'Y', "Qtech"},
  {'D', "Sunny"},
  {'E', "Sunwin"},
  {'G', "Ofilm"},
  {'H', "Shinephotics"},
  {0,"0"}
};

static kal_uint8 addrList[VIVO_OTP_ADDR_SIZE] = {
	0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8
};

static void write_cmos_sensor(kal_uint32 addr, kal_uint32 para)
{
	int ret;
	char pu_send_cmd[2] = { (char)(addr & 0xff), (char)(para & 0xff) };

	ret = iWriteRegI2C(pu_send_cmd, 2, VIVO_EEPROM_WRITE_ID);
	if (ret)
		LOG_INF("write cmos sensor fail, ret = %d\n", ret);
}

static kal_uint16 read_cmos_sensor(kal_uint32 addr)
{
	int ret;
	kal_uint16 get_byte = 0;
	char pu_send_cmd[1] = { (char)(addr & 0xff) };

	ret = iReadRegI2C(pu_send_cmd, 1, (u8 *) &get_byte, 1, VIVO_EEPROM_WRITE_ID);
	if (ret)
		LOG_INF("read cmos sensor fail, ret = %d\n", ret);

	return get_byte;
}

static void vivo_read_eeprom(kal_uint16 addr,  unsigned int *data)
{
	kdSetI2CSpeed(VIVO_I2C_SPEED);

	write_cmos_sensor(0xfe, 0x02);
	write_cmos_sensor(0x17, addr);
	write_cmos_sensor(0xf3, 0x34);
	*data = read_cmos_sensor(0x19);
}
extern unsigned int is_atboot;/*guojunzheng add*/
int MAIN2_2e2_otp_read(void)
{
	int i = 0;
	MUINT8 module_tag = 0;
	struct CAM_MODULE_INFO *pModuleInfo = module_info;
	/* This operation takes a long time, we need to skip it. guojunzheng add begin */
	#if 1
	if (is_atboot == 1) {
		LOG_INF("[lxd++]AT mode skip gc02m1bpd2278mipimono_otp_read\n");
		return 1;
	}
	#endif	

	write_cmos_sensor(0xf3, 0x30);
	for (i = 0; i < VIVO_OTP_ADDR_SIZE; i++) {
		vivo_read_eeprom(addrList[i],  &vivo_otp_data_gc02m1bpd2278[i]);
		//LOG_INF("read_vivo_eeprom temp addrList:0x%0x Data[0x%0x]:0x%x\n",addrList[i], i, vivo_otp_data_gc02m1bpd2278[i]);
	}

	if ((vivo_otp_data_gc02m1bpd2278[0x00] & 0x02) == 0x02){
		checksum = 0;
		LOG_INF("invalid otp data. error!!!\n");
		return 0;
	}else if ((vivo_otp_data_gc02m1bpd2278[0x00] & 0x01) == 0x00){
		checksum = 0;
		LOG_INF("OTP data is empty!!!\n");
		return 0;
	}

	strcpy(material_inf_main2_gc02m1bpd2278, "02371518");

	for (i = 0; i < 12; i++) {
		sprintf((sn_inf_main2_gc02m1bpd2278 + 2 * i), "%02x", (MUINT8)vivo_otp_data_gc02m1bpd2278[i + SN_addr]);
		LOG_INF("vivo_otp_data_gc02m1bpd2278[0x%x] = 0x%x\n", i +SN_addr, vivo_otp_data_gc02m1bpd2278[i+SN_addr]);
	}
	LOG_INF("sn_inf_main2_gc02m1bpd2278 = %s\n", sn_inf_main2_gc02m1bpd2278);

	for (i = 0; i < MAX_FPC_SIZE/3 ; i++){
		sprintf((fpc_main2_gc02m1bpd2278 + 3 * i), "%03d", vivo_otp_data_gc02m1bpd2278[i + FPC_addr]);
		LOG_INF("vivo_otp_data_gc02m1bpd2278[0x%x] = 0x%x\n", i + FPC_addr, vivo_otp_data_gc02m1bpd2278[i + FPC_addr]);
	}
	LOG_INF("fpc_main2_gc02m1bpd2278 = %s\n", fpc_main2_gc02m1bpd2278);
	for(i = 0; i < MAX_FPC_SIZE; i++){
		sprintf((sn_inf_main2_gc02m1bpd2278 + 2 * i + 24), "%02x", fpc_main2_gc02m1bpd2278[i]);
	}
	LOG_INF("sn+fpc = %s\n", sn_inf_main2_gc02m1bpd2278);
	strcpy(fuse_id_main2_gc02m1bpd2278, sn_inf_main2_gc02m1bpd2278);
	strcpy(fuse_id_main2_gc02m1bpd2278 + MAX_SN_SIZE, material_inf_main2_gc02m1bpd2278);

	module_tag = vivo_otp_data_gc02m1bpd2278[SN_addr + 1];
	while(pModuleInfo->MODULE_ID != 0){
		if(pModuleInfo->MODULE_ID == module_tag){
			strcpy(module_tag_main2_gc02m1bpd2278, pModuleInfo->MODULE_NAME);
			LOG_INF("get module tag -- [%s]!!!\n", module_tag_main2_gc02m1bpd2278);
			break;
		}
		pModuleInfo++;
	}
	if(pModuleInfo->MODULE_ID == 0){
		LOG_INF("[Error] wrong module tag -- [%c]!!!\n", module_tag);
	}
	return 1;
}
