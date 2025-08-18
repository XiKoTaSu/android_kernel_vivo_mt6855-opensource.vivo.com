/*
 * Copyright (C) 2020 Novatek
 *
 */

#ifndef __VIS_DISPLAY_H
#define __VIS_DISPLAY_H

#include <linux/pinctrl/consumer.h>
#include <linux/spi/spi.h>

#define DEVICE_ID_325 0x00
#define FW_VERSION_325 0x03
#define AP_DREIVER_VERSION 0x25

struct vis_frame_dimension {
	short s16PositionX;
	short s16PositionY;
	unsigned short u16Hight;
	unsigned short u16Width;
};

struct vis_frame_info {
	struct vis_frame_dimension stDimension;
	unsigned int u32LineLengthPixelClock;
	unsigned short u16LengthLines;
	unsigned short u16HorizontalBlanking;
	unsigned short u16VerticalBlanking;
	unsigned int u32OutPixelClock;
	unsigned char u8HorizontalBinning;
	unsigned char u8VerticalBinning;
	unsigned short u16FrameRate;
	unsigned short u16LaneCount;
	unsigned char u8DownScaleFactor;
	unsigned int u32SettleTimeNs;
	bool is3Phase;
};

enum EN_SENSOR_CMD {
	EN_SENSOR_CMD_STREAM_ENABLE = 0x00,
	EN_SENSOR_CMD_CONFIG_FRAME_RATE = 0x01,
	EN_SENSOR_CMD_CONFIG_RES_HEIGHT = 0x02,
	EN_SENSOR_CMD_CONFIG_RES_WIDTH = 0x03,
	EN_SENSOR_CMD_VERSION = 0x04,
	EN_SENSOR_CMD_NR_METADATA = 0x10,
	EN_SENSOR_CMD_NR_H = 0x11,
	EN_SENSOR_CMD_NR_V = 0x12,
	EN_SENSOR_CMD_NR_FR = 0x13,
	EN_SENSOR_CMD_NR_ISO = 0x14,
	EN_SENSOR_CMD_NR_GMV = 0x15,
	EN_SENSOR_CMD_NR_LUX = 0x16,
	EN_SENSOR_CMD_NR_GAIN = 0x17,
	EN_SENSOR_CMD_NR_GMV_X = 0x18,
	EN_SENSOR_CMD_NR_GMV_Y = 0x19,
	EN_SENSOR_CMD_NR_GMV_Z = 0x1A,
	EN_SENSOR_CMD_CAMERA_ID = 0x1B,
	EN_SENSOR_CMD_TX1_PTS_ENABLE = 0x1C,
	EN_SENSOR_CMD_MAX = 0xF0
};

enum EN_DSI_CMD {
	EN_DSI_CMD_POWER_ON,
	EN_DSI_CMD_POWER_OFF,
	EN_DSI_CMD_RESET,
	EN_DSI_CMD_IS_DUAL,
	EN_DSI_CMD_IS_MIPI_READY,
	EN_DSI_CMD_IS_ANALOG_BYPASS,
	EN_DSI_CMD_LOCK_USECASE,
	EN_DSI_CMD_UNLOCK_USECASE,
	EN_DSI_CMD_IS_USECASE_SETTING,
	EN_DSI_CMD_PANEL_ID,
	EN_DSI_CMD_DATARATE,
	EN_DSI_CMD_MAX
};

enum TE0_STATUS {
	TE0_BEFORE_OFF,
	TE0_AFTER_ON
};

extern int vis_register_driver(struct spi_driver *sdrv);
extern int vis_unregister_driver(struct spi_driver *sdrv);
extern int vis_is_probed(void);
extern int vis_set_frame_info(struct vis_frame_info *info);
extern int vis_sensor_cmd_send(enum EN_SENSOR_CMD enCmdType, unsigned int u32Length, unsigned char *pu8Buffer);
extern int vis_sensor_cmd_recv(enum EN_SENSOR_CMD enCmdType, unsigned int u32Length, unsigned char *pu8Buffer);

//DSI
extern int vis_dsi_cmd_send(enum EN_DSI_CMD enCmdType, unsigned int u32Length, unsigned char *pu8Buffer);
extern int vis_dsi_cmd_recv(enum EN_DSI_CMD enCmdType, unsigned int u32Length, unsigned char *pu8Buffer);
extern int vis_te0_status_note(enum TE0_STATUS enCmdType, unsigned int u32Length, unsigned char *pu8Buffer);
extern int vis_fps_notify(unsigned int u32Fps);
extern int vis_mipi_trigger_notify(void);
extern void vis_display_log_dump(void);

#endif // __VIS_DISPLAY_H
