// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */


#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_log.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vddi_en_gpio;
	struct gpio_desc *vddr_en_gpio;
	struct gpio_desc *elvdd_ctrl_gpio;
	struct gpio_desc *oled_vci;
	bool prepared;
	bool enabled;

	int error;

	bool hbm_en;
	bool hbm_wait;
};

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[200];
};

#include "pd2214_samsung_s6e3fc3_fhdp_cmd90hz_mask_cmd.h"
static char bl_tb0[] = {0x51, 0x0F, 0xFF};

#define lcm_dcs_write_seq(ctx, seq...) \
({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

#define lcm_dcs_write_seq_static(ctx, seq...) \
({\
	static const u32 d[] = { seq };\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = lcm_dcs_read(ctx,  0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

static void push_table(struct lcm *ctx, struct LCM_setting_table *table, unsigned int count)
{
    unsigned int i, j;
    unsigned char temp[255] = {0};

    for (i = 0; i < count; i++) {
        unsigned int cmd = table[i].cmd;

        memset(temp, 0, sizeof(temp));
        switch (cmd) {
        case REGFLAG_DELAY:
            if (table[i].count <= 10)
                msleep(table[i].count);
            else
                msleep(table[i].count);
            break;
        case REGFLAG_END_OF_TABLE:
            break;
        default:
            temp[0] = cmd;
            for (j = 0; j < table[i].count; j++)
                temp[j+1] = table[i].para_list[j];

            lcm_dcs_write(ctx, temp, table[i].count + 1);
            break;
        }
    }
}


static int lcm_panel_power_enable(struct lcm *ctx)
{
	pr_info("%s lcm power enble start\n", __func__);
	/* enable vddi */
	gpiod_set_value(ctx->vddi_en_gpio, 1);
	usleep_range(1000, 1100);

	/*enable vddr*/
	gpiod_set_value(ctx->vddr_en_gpio, 1);
	usleep_range(12000, 12100);

	/* enable vci*/
	gpiod_set_value(ctx->oled_vci, 1);
	usleep_range(1000, 1100);


	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(1000, 1100);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(8000, 8100);
	pr_info("%s lcm power enble end\n", __func__);
	return 0;
}

static int lcm_panel_power_disable(struct lcm *ctx)
{
	usleep_range(2000, 2100);
	gpiod_set_value(ctx->reset_gpio, 0);

	usleep_range(2000, 2100);
	gpiod_set_value(ctx->vddr_en_gpio, 0);

	usleep_range(1000, 1100);
	gpiod_set_value(ctx->vddi_en_gpio, 0);

	usleep_range(2000, 2100);
	gpiod_set_value(ctx->oled_vci, 0);

	return 0;
}

/* add lcm report id interface for TP use */
/*unsigned int mdss_report_lcm_id(void)
{
	pr_info("%s; panel id = 0x23 return 0xFF\n", __func__);
	return 0x24;
}
EXPORT_SYMBOL(mdss_report_lcm_id);*/

static void lcm_panel_init(struct lcm *ctx)
{
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	pr_info("%s lcm panel init start\n", __func__);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(2000, 2100);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(2000, 2100);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(8000, 8100);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	push_table(ctx, init_setting, sizeof(init_setting) / sizeof(struct LCM_setting_table));
		
	lcm_dcs_write_seq(ctx, bl_tb0[0], bl_tb0[1], bl_tb0[2]);
	pr_info("%s lcm panel init end\n", __func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;
	pr_info("%s lcm unprepare start\n", __func__);
	push_table(ctx, lcm_suspend_setting, sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table));
	ctx->error = 0;
	ctx->prepared = false;
	lcm_panel_power_disable(ctx);
	pr_info("%s lcm unprepare end\n", __func__);
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s lcm prepare start\n", __func__);
	//lcm_panel_poweron(panel);
	if (ctx->prepared)
		return 0;
	lcm_panel_power_enable(ctx);
	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif
	pr_info("%s lcm prepare end\n", __func__);
	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	pr_info("%s lcm enable \n", __func__);
	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = 243851,
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP,
	.vdisplay = VAC,
	.vsync_start = VAC + VFP,
	.vsync_end = VAC + VFP + VSA,
	.vtotal = VAC + VFP + VSA + VBP,
	//.vrefresh = 60,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	unsigned char data[3];
	unsigned char id[3] = {0xb3, 0x2, 0x1};
	ssize_t ret;

	ret = mipi_dsi_dcs_read(dsi, 0x4, data, 3);
	if (ret < 0)
		pr_info("%s error\n", __func__);

	DDPINFO("ATA read data %x %x %x\n", data[0], data[1], data[2]);

	if (data[0] == id[0] &&
			data[1] == id[1] &&
			data[2] == id[2])
		return 1;

	DDPINFO("ATA expect read data is %x %x %x\n",
			id[0], id[1], id[2]);

	return 0;
}

static struct LCM_setting_table lcm_aod_high_mode[] = {
	/* aod 50nit*/
	{REGFLAG_CMD, 3, {0xF0, 0x5A, 0x5A} },
	{REGFLAG_CMD, 2, {0x53, 0x24} },
	{REGFLAG_CMD, 3, {0xF0, 0xA5, 0xA5} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static struct LCM_setting_table lcm_aod_low_mode[] = {
	/* aod 10nit*/
	{REGFLAG_CMD, 3, {0xF0, 0x5A, 0x5A} },
	{REGFLAG_CMD, 2, {0x53, 0x25} },
	{REGFLAG_CMD, 3, {0xF0, 0xA5, 0xA5} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static int lcm_setbacklight_cmdq(void *dsi,
		dcs_write_gce cb, void *handle, unsigned int level)
{
	bl_tb0[1] = level * 4 >> 8;
	bl_tb0[2] = level * 4 & 0xFF;

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	pr_info("%s backlight level=%d\n", __func__, level);
	return 0;
}

static int panel_hbm_set_cmdq(struct drm_panel *panel, void *dsi,
			      dcs_write_gce cb, void *handle, bool en)
{
	char hbm_tb[] = {0x53, 0xe8};
	struct lcm *ctx = panel_to_lcm(panel);

	if (!cb)
		return -1;

	if (ctx->hbm_en == en)
		goto done;

	if (en)
		hbm_tb[1] = 0xe8;
	else
		hbm_tb[1] = 0x28;

	cb(dsi, handle, hbm_tb, ARRAY_SIZE(hbm_tb));

	ctx->hbm_en = en;
	ctx->hbm_wait = true;

done:
	return 0;
}

static void panel_hbm_get_state(struct drm_panel *panel, bool *state)
{
	struct lcm *ctx = panel_to_lcm(panel);

	*state = ctx->hbm_en;
}

static void panel_hbm_get_wait_state(struct drm_panel *panel, bool *wait)
{
	struct lcm *ctx = panel_to_lcm(panel);

	*wait = ctx->hbm_wait;
}

static bool panel_hbm_set_wait_state(struct drm_panel *panel, bool wait)
{
	struct lcm *ctx = panel_to_lcm(panel);
	bool old = ctx->hbm_wait;

	ctx->hbm_wait = wait;
	return old;
}

static unsigned long panel_doze_get_mode_flags(struct drm_panel *panel,
	int doze_en)
{
	unsigned long mode_flags;

	if (doze_en) {
		mode_flags = MIPI_DSI_MODE_LPM
		       | MIPI_DSI_MODE_EOT_PACKET
		       | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	} else {
		mode_flags = MIPI_DSI_MODE_VIDEO
		       | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
		       | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET
		       | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	}

	return mode_flags;
}

static struct LCM_setting_table lcm_normal_to_aod_sam[] = {
	/* Internal VDO Packet generation enable*/
	{REGFLAG_CMD,3,{0x9F,0xA5, 0xA5}},
	{REGFLAG_CMD,2,{0x28,0x00}},
	{REGFLAG_CMD,3,{0x9F,0x5A, 0x5A}},
	{REGFLAG_DELAY, 17, {} },

	/*AOD Mode ON:High Mode*/
	{REGFLAG_CMD,3,{0xF0,0x5A, 0x5A}},
	{REGFLAG_CMD,3,{0xB0, 0x3A, 0x63} },
	{REGFLAG_CMD,2,{0x63, 0x01} },
	{REGFLAG_CMD,2,{0x91,0x01}},
	{REGFLAG_CMD,2,{0x53,0x24}},
	{REGFLAG_CMD,2,{0xBB,0x31}},
	{REGFLAG_CMD,3,{0xF0,0xA5, 0xA5}},
};

static int panel_doze_enable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	unsigned int i = 0;

	for (i = 0; i < (sizeof(lcm_normal_to_aod_sam) /
			sizeof(struct LCM_setting_table)); i++) {
		unsigned int cmd;

		cmd = lcm_normal_to_aod_sam[i].cmd;
		switch (cmd) {
		case REGFLAG_DELAY:
			msleep(lcm_normal_to_aod_sam[i].count);
			break;
		case REGFLAG_UDELAY:
			udelay(lcm_normal_to_aod_sam[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			cb(dsi, handle, lcm_normal_to_aod_sam[i].para_list,
				lcm_normal_to_aod_sam[i].count);
		}
	}

	return 0;
}

static int panel_doze_enable_start(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	int cmd = 0;

	panel_ext_reset(panel, 0);
	usleep_range(10 * 1000, 15 * 1000);
	panel_ext_reset(panel, 1);

	cmd = 0x28;
	cb(dsi, handle, &cmd, 1);
	cmd = 0x10;
	cb(dsi, handle, &cmd, 1);
	msleep(80);
	return 0;
}

static struct LCM_setting_table lcm_aod_to_normal[] = {
	/*AOD mode off*/
	{REGFLAG_CMD,3,{0xF0,0x5A, 0x5A}},
	{REGFLAG_CMD,3,{0xB0, 0x3A, 0x63} },
	{REGFLAG_CMD,2,{0x63, 0x00} },
	{REGFLAG_CMD,2,{0x53,0x20}},
	{REGFLAG_CMD,2,{0x91,0x02}},
	{REGFLAG_CMD,3,{0xF0,0xA5, 0xA5}},
};

static int panel_doze_disable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	unsigned int i = 0;

	/* Switch back to VDO mode */
	for (i = 0; i < (sizeof(lcm_aod_to_normal) /
			sizeof(struct LCM_setting_table)); i++) {
		unsigned int cmd;

		cmd = lcm_aod_to_normal[i].cmd;

		switch (cmd) {
		case REGFLAG_DELAY:
				msleep(lcm_aod_to_normal[i].count);
			break;
		case REGFLAG_UDELAY:
			udelay(lcm_aod_to_normal[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			cb(dsi, handle, lcm_aod_to_normal[i].para_list,
				lcm_aod_to_normal[i].count);
		}
	}

	return 0;
}

/* (x,y,w*h) total size = 931200 Bytes */
static char doze_area_cmd[45] = {
	0x81, 0x3C, /* cmd id, enabled area */
	0x0C, 0x07, 0x03, 0x21, 0x90, /* area 0 (192,50,672*350) */
	0x0C, 0x07, 0x19, 0x02, 0xEE, /* area 1 (192,400,672*350) */
	0x18, 0x06, 0x38, 0x45, 0x14, /* area 2 (384,960,576*400) */
	0x00, 0x06, 0x51, 0x46, 0xA4, /* area 3 (0,1300,576*400) */
	0x00, 0x00, 0x00, 0x00, 0x00, /* area 4 - not use */
	0x00, 0x00, 0x00, 0x00, 0x00, /* area 5 - not use */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* color_sel, depth, color */
	0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 /* gray, dummy */
};

static int panel_doze_area(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	cb(dsi, handle, doze_area_cmd, ARRAY_SIZE(doze_area_cmd));

	return 0;
}

static struct mtk_panel_params ext_params = {
	.data_rate = 597,
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9f,
	},
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,  //128
		.pic_height = FRAME_HEIGHT,
		.pic_width = 1080,
		.slice_height = DSC_SLICE_HEIGHT,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 512,
		.dec_delay = 526,
		.scale_value = 32,
		.increment_interval = 14924,
		.decrement_interval = 7,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 41,
		.slice_bpg_offset = 44,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		},
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.hbm_en_time = 0,
	.hbm_dis_time = 1,
	.doze_delay = 3,
};

static int panel_doze_post_disp_on(struct drm_panel *panel,
		void *dsi, dcs_write_gce cb, void *handle)
{

	int cmd = 0;

#ifdef VENDOR_EDIT
/* Hujie@PSW.MM.DisplayDriver.AOD, 2019/12/10, add for keylog*/
	pr_info("debug for lcm %s\n", __func__);
#endif

	cmd = 0x29;
	cb(dsi, handle, &cmd, 1);
	//msleep(2);

	return 0;
}
/*
static void vivo_lcm_get_id(struct drm_panel *panel, int type)
{
	struct vivo_display *vdisp = get_vivo_vdisp();

	LCD_INFO("get type =%d\n", type);

	switch(type){
	case LCM_GET_LCM_ID:
		if ((vdisp->lcm_software_id & 0xFF) == 0x4e) {
			if (vdisp->lcm_software_id >> 8 == 0x10)
				vdisp->panel_id = 0x21;
			else
				vdisp->panel_id = 0x22;
		} else if ((vdisp->lcm_software_id & 0xFF) == 0x4d) {
			vdisp->panel_id = 0x23;
		} else if ((vdisp->lcm_software_id & 0xFF) == 0x4f) {
			vdisp->panel_id = 0x24;
		} else if ((vdisp->lcm_software_id & 0xFF) == 0x5e) {
			vdisp->panel_id = 0x25;
		} else
			vdisp->panel_id = 0xff;
		break;
	case LCM_GET_CABC_ACL_THRESHOLD:
		vdisp->cabc_acl_threshold = 1500;
		break;
	case LCM_GET_ESD_CHECK_RELOAD_FW:
		vdisp->ctx->esd_check_reload_firmware = false;
	default:
		break;
	}
}
*/
#if 0
static int mtk_panel_ext_param_set(struct drm_panel *panel,
		 unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;

	LCD_INFO("----mode = %d---\n",	mode);

	if (mode == 0)
		ext->params = &ext_params;
	else
		ret = 1;

	return ret;
}
#endif
static int panel_set_aod_light_mode(void *dsi,
	dcs_write_gce cb, void *handle, unsigned int mode)
{
	int i = 0;

	pr_info("debug for lcm %s\n", __func__);

	if (mode >= 1) {
		for (i = 0; i < sizeof(lcm_aod_high_mode)/sizeof(struct LCM_setting_table); i++)
			cb(dsi, handle, lcm_aod_high_mode[i].para_list, lcm_aod_high_mode[i].count);
	} else {
		for (i = 0; i < sizeof(lcm_aod_low_mode)/sizeof(struct LCM_setting_table); i++)
			cb(dsi, handle, lcm_aod_low_mode[i].para_list, lcm_aod_low_mode[i].count);
	}
	pr_info("%s : %d !\n", __func__, mode);

	//memset(send_cmd, 0, RAMLESS_AOD_PAYLOAD_SIZE);
	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.hbm_set_cmdq = panel_hbm_set_cmdq,
	.hbm_get_state = panel_hbm_get_state,
	.hbm_get_wait_state = panel_hbm_get_wait_state,
	.hbm_set_wait_state = panel_hbm_set_wait_state,

	/* add for ramless AOD */
	.doze_get_mode_flags = panel_doze_get_mode_flags,
	.doze_enable = panel_doze_enable,
	.doze_enable_start = panel_doze_enable_start,
	.doze_area = panel_doze_area,
	.doze_disable = panel_doze_disable,
	.doze_post_disp_on = panel_doze_post_disp_on,
	.set_aod_light_mode = panel_set_aod_light_mode,
	//.lcm_get_id = vivo_lcm_get_id,
	//.ext_param_set = mtk_panel_ext_param_set,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lcm_get_modes(struct drm_panel *panel,
		struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 71;
	connector->display_info.height_mm = 153;

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static ssize_t aod_area_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i;

	for (i = 0; i < sizeof(doze_area_cmd) / sizeof(char); i++)
		pr_info("%s cmd = %d", __func__, doze_area_cmd[i]);
	return 0;
}

static ssize_t aod_area_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	//struct lcm *ctx = mipi_dsi_get_drvdata(dev);
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = sscanf(&buf[i], "%c", &doze_area_cmd[i]);
		pr_info("%s ret = %d, buf[%d]=%d", __func__, ret, i, buf[i]);
	}

	return ret;
}

static DEVICE_ATTR_RW(aod_area);

static struct attribute *aod_area_sysfs_attrs[] = {
	&dev_attr_aod_area.attr,
	NULL,
};

static struct attribute_group aod_area_sysfs_attr_group = {
	.attrs = aod_area_sysfs_attrs,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	pr_info("%s lcm probe  start\n", __func__);
	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);

	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}
	ctx->oled_vci = devm_gpiod_get(ctx->dev, "vci_en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->oled_vci)) { /* handle return value */
		dev_err(ctx->dev, "%s: cannot get vci-gpios %ld\n",
			__func__, PTR_ERR(ctx->oled_vci));
		return PTR_ERR(ctx->oled_vci);
	}
	devm_gpiod_put(ctx->dev, ctx->oled_vci);

	/* get vddi enable gpio*/
	ctx->vddi_en_gpio = devm_gpiod_get(dev, "vddi_en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddi_en_gpio)) {
		dev_err(dev, "%s: cannot get vddi_en-gpios %ld\n",
			__func__, PTR_ERR(ctx->vddr_en_gpio));
		return PTR_ERR(ctx->vddi_en_gpio);
	}
	devm_gpiod_put(dev, ctx->vddi_en_gpio);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "%s: cannot get reset-gpios %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

	/* get vddr enable gpio*/
	ctx->vddr_en_gpio = devm_gpiod_get(dev, "vddr_en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddr_en_gpio)) {
		dev_err(dev, "%s: cannot get vddr_en-gpios %ld\n",
			__func__, PTR_ERR(ctx->vddr_en_gpio));
		return PTR_ERR(ctx->vddr_en_gpio);
	}
	devm_gpiod_put(dev, ctx->vddr_en_gpio);

	ctx->elvdd_ctrl_gpio = devm_gpiod_get(dev, "elvdd_ctrl", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->elvdd_ctrl_gpio)) {
		dev_err(dev, "cannot get elvdd_ctrl_gpio %ld\n",
			PTR_ERR(ctx->elvdd_ctrl_gpio));
		return PTR_ERR(ctx->elvdd_ctrl_gpio);
	}
	devm_gpiod_put(dev, ctx->elvdd_ctrl_gpio);

	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs,
			DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	//mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	ctx->hbm_en = false;
	ret = sysfs_create_group(&dev->kobj, &aod_area_sysfs_attr_group);
	if (ret)
		return ret;
	pr_info("%s lcm probe end\n", __func__);
	pr_info("%s-lcm,samsung,s6e3fc3,cmd\n", __func__);

	return ret;
}

static int lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif

	return 0;
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "pd2214samsung,s6e3fc3,cmd90hz", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "pd2214_samsung_s6e3fc3_fhdp_cmd90hz",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Linus Wallei <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("MIPI-DSI s68fc01 Panel Driver");
MODULE_LICENSE("GPL v2");
