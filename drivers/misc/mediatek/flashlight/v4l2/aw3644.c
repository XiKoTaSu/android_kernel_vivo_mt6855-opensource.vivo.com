// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 MediaTek Inc.

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-subdev.h>
// #include <aw3644.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/pm_runtime.h>
#include <linux/thermal.h>

/* add by vivo oumeiyin start */
#include "flashlight.h"
/* add by vivo oumeiyin end   */
#include "flash_common.h"

#if IS_ENABLED(CONFIG_MTK_FLASHLIGHT)
#include "flashlight-core.h"

#include <linux/power_supply.h>
#endif

#define AW3644_NAME	"aw3644"
#define AW3644_I2C_ADDR	(0x63)
#define AW3644_ON              1
#define AW3644_OFF             0
#define REG_DEVICE_ID 0x0C
#define REG_FLASHIC_ID 0x00
#define AW3644_DEVICE_ID 0x02
#define AW3644_FLASHIC_ID 0x36

/* fault mask */
#define FAULT_TIMEOUT	(1<<0)
#define FAULT_THERMAL_SHUTDOWN	(1<<2)
#define FAULT_LED0_SHORT_CIRCUIT	(1<<5)
#define FAULT_LED1_SHORT_CIRCUIT	(1<<4)

/*  FLASH Brightness
 *	min 11350uA, step 11720uA, max 1500000uA
 */
#define AW3644_FLASH_BRT_MIN 11350
#define AW3644_FLASH_BRT_STEP 11720
#define AW3644_FLASH_BRT_MAX 1500000
#define AW3644_FLASH_BRT_uA_TO_REG(a)	\
	((a) < AW3644_FLASH_BRT_MIN ? 0 :	\
	 (((a) - AW3644_FLASH_BRT_MIN) / AW3644_FLASH_BRT_STEP))
#define AW3644_FLASH_BRT_REG_TO_uA(a)		\
	((a) * AW3644_FLASH_BRT_STEP + AW3644_FLASH_BRT_MIN)

/*  FLASH TIMEOUT DURATION
 *	min 40ms, step 40ms, max 400ms
 */
#define AW3644_FLASH_TOUT_MIN 200
#define AW3644_FLASH_TOUT_STEP 40
#define AW3644_FLASH_TOUT_MAX 320

/*  TORCH BRT
 *	min 2550uA, step 2910uA, max 372000uA
 */
#define AW3644_TORCH_BRT_MIN 2550
#define AW3644_TORCH_BRT_STEP 2910
#define AW3644_TORCH_BRT_MAX 372000
#define AW3644_TORCH_BRT_uA_TO_REG(a)	\
	((a) < AW3644_TORCH_BRT_MIN ? 0 :	\
	 (((a) - AW3644_TORCH_BRT_MIN) / AW3644_TORCH_BRT_STEP))
#define AW3644_TORCH_BRT_REG_TO_uA(a)		\
	((a) * AW3644_TORCH_BRT_STEP + AW3644_TORCH_BRT_MIN)

#define AW3644_COOLER_MAX_STATE 5
static const int flash_state_to_current_limit[AW3644_COOLER_MAX_STATE] = {
	200000, 150000, 100000, 50000, 25000
};

enum aw3644_led_id {
	AW3644_LED0 = 0,
	AW3644_LED1,
	AW3644_LED0_LED1, //vivo camera or Flash Calibration enable LED1/2 at same time
	AW3644_LED_MAX
};

/* struct aw3644_platform_data
 *
 * @max_flash_timeout: flash timeout
 * @max_flash_brt: flash mode led brightness
 * @max_torch_brt: torch mode led brightness
 */
struct aw3644_platform_data {
	u32 max_flash_timeout;
	u32 max_flash_brt[AW3644_LED_MAX];
	u32 max_torch_brt[AW3644_LED_MAX];
};


enum led_enable {
	MODE_SHDN = 0x0,
	MODE_TORCH = 0x08,
	MODE_FLASH = 0x0C,
};

/**
 * struct aw3644_flash
 *
 * @dev: pointer to &struct device
 * @pdata: platform data
 * @regmap: reg. map for i2c
 * @lock: muxtex for serial access.
 * @led_mode: V4L2 LED mode
 * @ctrls_led: V4L2 controls
 * @subdev_led: V4L2 subdev
 */
struct aw3644_flash {
	struct device *dev;
	struct aw3644_platform_data *pdata;
	struct regmap *regmap;
	struct mutex lock;

	enum v4l2_flash_led_mode led_mode;
	struct v4l2_ctrl_handler ctrls_led[AW3644_LED_MAX];
	struct v4l2_subdev subdev_led[AW3644_LED_MAX];
	struct device_node *dnode[AW3644_LED_MAX];
	struct regulator *regulator_enable;
#if IS_ENABLED(CONFIG_MTK_FLASHLIGHT)
	struct flashlight_device_id flash_dev_id[AW3644_LED_MAX];
#endif
	struct thermal_cooling_device *cdev;
	int need_cooler;
	int enableType; //0:Single LED Control, 1:LED1/LED2 enable at same time
	unsigned long max_state;
	unsigned long target_state;
	unsigned long target_current;
	unsigned long ori_current;
};
static bool aw3644_judge(struct aw3644_flash *flash);
/* define usage count */
static int use_count;

static struct aw3644_flash *aw3644_flash_data;

struct flash_status *flash_status;

#define to_aw3644_flash(_ctrl, _no)	\
	container_of(_ctrl->handler, struct aw3644_flash, ctrls_led[_no])

/*CXB add for 161 gpio reset 0 start*/
static int aw3644_set_power(struct aw3644_flash *flash, bool on)
{
	int ret = 0;
	pr_info_ratelimited("%s enable:%d", __func__, on);
	if (flash->regulator_enable) {
		if (on == regulator_is_enabled(flash->regulator_enable))
			goto out;
		// pr_info("[%s]E on(%d) %s ", __func__, on, on ? "ON" : "OFF");
		if (on) {
			ret = regulator_enable(flash->regulator_enable);
			mdelay(2);
		} else {
			ret = regulator_disable(flash->regulator_enable);
		}
	} else {
		pr_info("regulator_enable is null");
		return ret;
	}
	out:
		// pr_info("[%s]X on(%d) %s ret(%d)", __func__, on, on ? "ON" : "OFF", ret);
		return ret;
}
static int aw3644_write_reg(struct aw3644_flash *flash, unsigned int reg,
				     unsigned int mask, unsigned int val)
{
	int rval = -EINVAL;
	aw3644_set_power(flash, AW3644_ON);
	rval = regmap_update_bits(flash->regmap, reg, mask, val);
	return rval;
}
static int aw3644_read_reg(struct aw3644_flash *flash, unsigned int reg, unsigned int *val)
{
	int rval = -EINVAL;
	aw3644_set_power(flash, AW3644_ON);
	rval = regmap_read(flash->regmap, reg, val);
	return rval;
}

/* enable mode control */
static int aw3644_mode_ctrl(struct aw3644_flash *flash)
{
	int rval = -EINVAL;

	pr_info_ratelimited("%s mode:%d", __func__, flash->led_mode);
	switch (flash->led_mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		rval = aw3644_write_reg(flash,
					  REG_ENABLE, 0x0C, MODE_SHDN);
		break;
	case V4L2_FLASH_LED_MODE_TORCH:
		rval = aw3644_write_reg(flash,
					  REG_ENABLE, 0x0C, MODE_TORCH);
		break;
	case V4L2_FLASH_LED_MODE_FLASH:
		rval = aw3644_write_reg(flash,
					  REG_ENABLE, 0x0C, MODE_FLASH);
		break;
	}
	return rval;
}

/* led1/2 enable/disable */
static int aw3644_enable_ctrl(struct aw3644_flash *flash,
			      enum aw3644_led_id led_no, bool on)
{
	int rval = 0;

	pr_info_ratelimited("%s led:%d enable:%d", __func__, led_no, on);

	flashlight_kicker_pbm(on);
/* add by vivo oumeiyin start */
#ifdef CONFIG_MTK_FLASHLIGHT_PT
/* add by vivo oumeiyin end  */
	if (flashlight_pt_is_low()) {
		pr_info_ratelimited("pt is low\n");
		return 0;
	}
/* add by vivo oumeiyin start */
#endif
/* add by vivo oumeiyin end  */

	if ( flash_status == NULL){
		pr_info_ratelimited("flash_status is null\n");
		return -1;
	}

	if (led_no == AW3644_LED0 || !flash_status->status_led2) {
		if (on)
			rval = aw3644_write_reg(flash,
						  REG_ENABLE, 0x01, 0x01);
		else
			rval = aw3644_write_reg(flash,
						  REG_ENABLE, 0x01, 0x00);
	} else if (led_no == AW3644_LED1 || !flash_status->status_led1) {
		if (on)
			rval = aw3644_write_reg(flash,
						  REG_ENABLE, 0x02, 0x02);
		else
			rval = aw3644_write_reg(flash,
						  REG_ENABLE, 0x02, 0x00);
	} else if (led_no == AW3644_LED0_LED1) {
		if (on)
			rval = aw3644_write_reg(flash,
						  REG_ENABLE, 0x03, 0x03);
		else
			rval = aw3644_write_reg(flash,
						  REG_ENABLE, 0x03, 0x00);
	} else {
		pr_info_ratelimited("led_no(%d) is error\n", led_no);
	}

	// if ( on &&  (led_no == AW3644_LED0_LED1) )
	// 	flash_check_status(flash->regmap, flash_status);

	return rval;
}

/* torch1/2 brightness control */
static int aw3644_torch_brt_ctrl(struct aw3644_flash *flash,
				 enum aw3644_led_id led_no, unsigned int brt)
{
	int rval;
	u8 br_bits;

	pr_info_ratelimited("%s %d brt:%u\n", __func__, led_no, brt);
	if (brt < AW3644_TORCH_BRT_MIN)
		return aw3644_enable_ctrl(flash, led_no, false);

	if (flash->need_cooler == 0) {
		flash->ori_current = brt;
	} else {
		if (brt > flash->target_current) {
			brt = flash->target_current;
			pr_info("thermal limit current:%d\n", brt);
		}
	}

	br_bits = AW3644_TORCH_BRT_uA_TO_REG(brt);
	if (led_no == AW3644_LED0)
		rval = aw3644_write_reg(flash,
					  REG_LED0_TORCH_BR, 0x7f, br_bits);
	else if (led_no == AW3644_LED1)
		rval = aw3644_write_reg(flash,
					  REG_LED1_TORCH_BR, 0x7f, br_bits);
	else {
		rval = aw3644_write_reg(flash,
					  REG_LED0_TORCH_BR, 0x7f, br_bits);
		rval = aw3644_write_reg(flash,
					  REG_LED1_TORCH_BR, 0x7f, br_bits);
	}

	return rval;
}

/* flash1/2 brightness control */
static int aw3644_flash_brt_ctrl(struct aw3644_flash *flash,
				 enum aw3644_led_id led_no, unsigned int brt)
{
	int rval;
	u8 br_bits;

	pr_info("%s %d brt:%u", __func__, led_no, brt);
	if (brt < AW3644_FLASH_BRT_MIN)
		return aw3644_enable_ctrl(flash, led_no, false);

	if (flash->need_cooler == 1 && brt > flash->target_current) {
		brt = flash->target_current;
		pr_info("thermal limit current:%d\n", brt);
	}

	br_bits = AW3644_FLASH_BRT_uA_TO_REG(brt);
	if (led_no == AW3644_LED0)
		rval = aw3644_write_reg(flash,
					  REG_LED0_FLASH_BR, 0x7f, br_bits);
	else if (led_no == AW3644_LED1)
		rval = aw3644_write_reg(flash,
					  REG_LED1_FLASH_BR, 0x7f, br_bits);
	else {
		rval = aw3644_write_reg(flash,
					  REG_LED0_FLASH_BR, 0x7f, br_bits);
		rval = aw3644_write_reg(flash,
					  REG_LED1_FLASH_BR, 0x7f, br_bits);
	}
	return rval;
}

/* flash1/2 timeout control */
static int aw3644_flash_tout_ctrl(struct aw3644_flash *flash,
				unsigned int tout)
{
	int rval;
	u8 tout_bits;

	if (tout == 200)
		tout_bits = 0x04;
	else
		tout_bits = (tout / AW3644_FLASH_TOUT_STEP) - 0x01;

	rval = aw3644_write_reg(flash,
				  REG_FLASH_TOUT, 0x1f, tout_bits);

	return rval;
}

/* v4l2 controls  */
static int aw3644_get_ctrl(struct v4l2_ctrl *ctrl, enum aw3644_led_id led_no)
{
	struct aw3644_flash *flash = to_aw3644_flash(ctrl, led_no);
	int rval = -EINVAL;

	mutex_lock(&flash->lock);

	if (ctrl->id == V4L2_CID_FLASH_FAULT) {
		s32 fault = 0;
		unsigned int reg_val = 0;

		rval = aw3644_read_reg(flash, REG_FLAG1, &reg_val);
		if (rval < 0)
			goto out;
		if (reg_val & FAULT_THERMAL_SHUTDOWN)
			fault |= V4L2_FLASH_FAULT_OVER_TEMPERATURE;
		if (reg_val & FAULT_TIMEOUT)
			fault |= V4L2_FLASH_FAULT_TIMEOUT;
		ctrl->cur.val = fault;
	}

out:
	mutex_unlock(&flash->lock);
	return rval;
}

static int aw3644_set_ctrl(struct v4l2_ctrl *ctrl, enum aw3644_led_id led_no)
{
	struct aw3644_flash *flash = to_aw3644_flash(ctrl, led_no);
	int rval = -EINVAL;

	pr_info("%s enableType:%d led:%d ID:%d", __func__, flash->enableType, led_no, ctrl->id);
	mutex_lock(&flash->lock);
	if(flash->enableType)
		led_no = AW3644_LED0_LED1;
	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		flash->led_mode = ctrl->val;
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			rval = aw3644_mode_ctrl(flash);
		else
			rval = 0;
		if (flash->led_mode == V4L2_FLASH_LED_MODE_NONE)
			aw3644_enable_ctrl(flash, led_no, false);
		else if (flash->led_mode == V4L2_FLASH_LED_MODE_TORCH)
			rval = aw3644_enable_ctrl(flash, led_no, true);
		break;

	case V4L2_CID_FLASH_STROBE_SOURCE:
		if (ctrl->val == V4L2_FLASH_STROBE_SOURCE_SOFTWARE) {
			pr_info("sw ctrl\n");
			rval = aw3644_write_reg(flash,
					REG_ENABLE, 0x2C, 0x00);
		} else if (ctrl->val == V4L2_FLASH_STROBE_SOURCE_EXTERNAL) {
			pr_info("hw trigger\n");
			rval = aw3644_write_reg(flash,
					REG_ENABLE, 0x2C, 0x24);
			rval = aw3644_enable_ctrl(flash, led_no, true);
		}
		if (rval < 0)
			goto err_out;
		break;

	case V4L2_CID_FLASH_STROBE:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			rval = -EBUSY;
			goto err_out;
		}
		flash->led_mode = V4L2_FLASH_LED_MODE_FLASH;
		rval = aw3644_mode_ctrl(flash);
		rval = aw3644_enable_ctrl(flash, led_no, true);
		break;

	case V4L2_CID_FLASH_STROBE_STOP:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			rval = -EBUSY;
			goto err_out;
		}
		aw3644_enable_ctrl(flash, led_no, false);
		flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
		rval = aw3644_mode_ctrl(flash);
		break;

	case V4L2_CID_FLASH_TIMEOUT:
		rval = aw3644_flash_tout_ctrl(flash, AW3644_FLASH_TOUT_MAX);
		break;

	case V4L2_CID_FLASH_INTENSITY:
		rval = aw3644_flash_brt_ctrl(flash, led_no, ctrl->val);
		break;

	case V4L2_CID_FLASH_TORCH_INTENSITY:
		rval = aw3644_torch_brt_ctrl(flash, led_no, ctrl->val);
		break;
	}

err_out:
	mutex_unlock(&flash->lock);
	return rval;
}

static int aw3644_led1_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return aw3644_get_ctrl(ctrl, AW3644_LED1);
}

static int aw3644_led1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return aw3644_set_ctrl(ctrl, AW3644_LED1);
}

static int aw3644_led0_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return aw3644_get_ctrl(ctrl, AW3644_LED0);
}

static int aw3644_led0_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return aw3644_set_ctrl(ctrl, AW3644_LED0);
}

static const struct v4l2_ctrl_ops aw3644_led_ctrl_ops[AW3644_LED_MAX] = {
	[AW3644_LED0] = {
			.g_volatile_ctrl = aw3644_led0_get_ctrl,
			.s_ctrl = aw3644_led0_set_ctrl,
			},
	[AW3644_LED1] = {
			.g_volatile_ctrl = aw3644_led1_get_ctrl,
			.s_ctrl = aw3644_led1_set_ctrl,
			}
};

static int aw3644_init_controls(struct aw3644_flash *flash,
				enum aw3644_led_id led_no)
{
	struct v4l2_ctrl *fault;
	u32 max_flash_brt = flash->pdata->max_flash_brt[led_no];
	u32 max_torch_brt = flash->pdata->max_torch_brt[led_no];
	struct v4l2_ctrl_handler *hdl = &flash->ctrls_led[led_no];
	const struct v4l2_ctrl_ops *ops = &aw3644_led_ctrl_ops[led_no];

	v4l2_ctrl_handler_init(hdl, 8);

	/* flash mode */
	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_FLASH_LED_MODE,
			       V4L2_FLASH_LED_MODE_TORCH, ~0x7,
			       V4L2_FLASH_LED_MODE_NONE);
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;

	/* flash source */
	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_FLASH_STROBE_SOURCE,
			       0x1, ~0x3, V4L2_FLASH_STROBE_SOURCE_SOFTWARE);

	/* flash strobe */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_STROBE, 0, 0, 0, 0);

	/* flash strobe stop */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_STROBE_STOP, 0, 0, 0, 0);

	/* flash strobe timeout */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_TIMEOUT,
			  AW3644_FLASH_TOUT_MIN,
			  flash->pdata->max_flash_timeout,
			  AW3644_FLASH_TOUT_STEP,
			  flash->pdata->max_flash_timeout);

	/* flash brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_INTENSITY,
			  AW3644_FLASH_BRT_MIN, max_flash_brt,
			  AW3644_FLASH_BRT_STEP, max_flash_brt);

	/* torch brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_TORCH_INTENSITY,
			  AW3644_TORCH_BRT_MIN, max_torch_brt,
			  AW3644_TORCH_BRT_STEP, max_torch_brt);

	/* fault */
	fault = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_FAULT, 0,
				  V4L2_FLASH_FAULT_OVER_VOLTAGE
				  | V4L2_FLASH_FAULT_OVER_TEMPERATURE
				  | V4L2_FLASH_FAULT_SHORT_CIRCUIT
				  | V4L2_FLASH_FAULT_TIMEOUT, 0, 0);
	if (fault != NULL)
		fault->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (hdl->error)
		return hdl->error;

	flash->subdev_led[led_no].ctrl_handler = hdl;
	return 0;
}

/* initialize device */
static const struct v4l2_subdev_ops aw3644_ops = {
	.core = NULL,
};

static const struct regmap_config aw3644_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xFF,
};

static void aw3644_v4l2_i2c_subdev_init(struct v4l2_subdev *sd,
		struct i2c_client *client,
		const struct v4l2_subdev_ops *ops)
{
	pr_info("%s\n", __func__);
	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_IS_I2C;
	/* the owner is the same as the i2c_client's driver owner */
	sd->owner = client->dev.driver->owner;
	sd->dev = &client->dev;
	/* i2c_client and v4l2_subdev point to one another */
	v4l2_set_subdevdata(sd, client);
	i2c_set_clientdata(client, sd);
	/* initialize name */
	snprintf(sd->name, sizeof(sd->name), "%s %d-%04x",
		client->dev.driver->name, i2c_adapter_id(client->adapter),
		client->addr);
}

static int aw3644_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int ret;

	pr_info("%s\n", __func__);
	if (!aw3644_judge(aw3644_flash_data)) {
		pr_info("%s:%d The device isn't AW3644", __func__,__LINE__);
		return -EINVAL;
	}

	ret = pm_runtime_get_sync(sd->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(sd->dev);
		return ret;
	}

	return 0;
}

static int aw3644_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pr_info("%s\n", __func__);
	pm_runtime_put(sd->dev);

	return 0;
}
static const struct v4l2_subdev_internal_ops aw3644_int_ops = {
	.open = aw3644_open,
	.close = aw3644_close,
};

static int aw3644_subdev_init(struct aw3644_flash *flash,
			      enum aw3644_led_id led_no, char *led_name)
{
	struct i2c_client *client = to_i2c_client(flash->dev);
	struct device_node *np = flash->dev->of_node, *child;
	const char *fled_name = "aw3644_flash";
	int rval;

	pr_info("%s %d", __func__, led_no);

	aw3644_v4l2_i2c_subdev_init(&flash->subdev_led[led_no],
				client, &aw3644_ops);
	flash->subdev_led[led_no].flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	flash->subdev_led[led_no].internal_ops = &aw3644_int_ops;
	strscpy(flash->subdev_led[led_no].name, led_name,
		sizeof(flash->subdev_led[led_no].name));

	for (child = of_get_child_by_name(np, fled_name); child;
			child = of_find_node_by_name(child, fled_name)) {
		int rv;
		u32 reg = 0;

		rv = of_property_read_u32(child, "reg", &reg);
		if (rv)
			continue;

		if (reg == led_no) {
			flash->dnode[led_no] = child;
			flash->subdev_led[led_no].fwnode =
				of_fwnode_handle(flash->dnode[led_no]);
		}
	}

	rval = aw3644_init_controls(flash, led_no);
	if (rval)
		goto err_out;
	rval = media_entity_pads_init(&flash->subdev_led[led_no].entity, 0, NULL);
	if (rval < 0)
		goto err_out;
	flash->subdev_led[led_no].entity.function = MEDIA_ENT_F_FLASH;

	rval = v4l2_async_register_subdev(&flash->subdev_led[led_no]);
	if (rval < 0)
		goto err_out;

	return rval;

err_out:
	v4l2_ctrl_handler_free(&flash->ctrls_led[led_no]);
	return rval;
}

/* flashlight init */
static int aw3644_init(struct aw3644_flash *flash)
{
	int rval = 0;
	unsigned int reg_val;
	pr_info("%s\n", __func__);
	aw3644_set_power(flash, AW3644_ON);

	/* set timeout */
	rval = aw3644_flash_tout_ctrl(flash, AW3644_FLASH_TOUT_MAX);
	if (rval < 0)
		return rval;
	/* output disable */
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
	rval = aw3644_mode_ctrl(flash);
	if (rval < 0)
		return rval;

	rval = aw3644_write_reg(flash,
				  REG_LED0_TORCH_BR, 0x80, 0x00);
	if (rval < 0)
		return rval;
	rval = aw3644_write_reg(flash,
				  REG_LED0_FLASH_BR, 0x80, 0x00);
	if (rval < 0)
		return rval;
	/* reset faults */
	rval = aw3644_read_reg(flash, REG_FLAG1, &reg_val);
	return rval;
}

/* flashlight uninit */
static int aw3644_uninit(struct aw3644_flash *flash)
{
	pr_info("%s\n", __func__);
	aw3644_set_power(flash, AW3644_OFF);

	return 0;
}

static int aw3644_flash_open(void)
{
	pr_info("%s\n", __func__);
	if (!aw3644_judge(aw3644_flash_data)) {
		pr_info("%s:%d The device isn't AW3644", __func__,__LINE__);
		return -EINVAL;
	}
	return 0;
}

static int aw3644_flash_release(void)
{
	return 0;
}

/* add by vivo oumeiyin start */
static int set_flashlight_state(struct aw3644_flash *flash, int channel, int state)
{

	u8 brt_bits = 0;
	int fac_torch_brt = 0, fac_flash_brt = 0;

	unsigned int rval = 0;
	int val;
	aw3644_set_power(flash, AW3644_ON);
	pr_info("set_flashlight_state check channel %d,state:%d \n", channel, state);
	switch (state) {
	case BBK_TORCH_LOW:
		fac_torch_brt = flash->flash_dev_id[channel].factory_torch_brt;
		brt_bits = AW3644_TORCH_BRT_uA_TO_REG(fac_torch_brt * 1000);
		pr_info("%s:%d [AT+BKSG=1 or AT+BKSG=1,1] brt = %dmA(0x%x)\n", __func__, __LINE__, fac_torch_brt, brt_bits);
		if(channel == 0){
			aw3644_write_reg(flash, REG_LED0_TORCH_BR, 0x7f, brt_bits);/*(Brightnees code x 2.91mA)+2.55ma Torch*/
			val = aw3644_read_reg(flash, REG_LED0_TORCH_BR,  &rval);
			pr_info("aw3644 torch rval = %d ,%d \n", val, rval);
			flash->led_mode = V4L2_FLASH_LED_MODE_TORCH;
			aw3644_mode_ctrl(flash);
			aw3644_enable_ctrl(flash, AW3644_LED0, true);
		}
		else if(channel == 1){
			aw3644_write_reg(flash, REG_LED1_TORCH_BR, 0x7f, brt_bits);/*(Brightnees code x 2.91mA)+2.55ma Torch*/
			val = aw3644_read_reg(flash, REG_LED1_TORCH_BR,  &rval);
			pr_info("aw3644 torch2 rval = %d ,%d \n", val, rval);
			flash->led_mode = V4L2_FLASH_LED_MODE_TORCH;
			aw3644_mode_ctrl(flash);
			aw3644_enable_ctrl(flash, AW3644_LED1, true);
		}
		break;
	case BBK_TORCH_OFF:
		pr_info("%s:%d [AT+BKSG=0 or AT+BKSG=0,1]\n", __func__, __LINE__);
		flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
		aw3644_mode_ctrl(flash);
		if(channel == 0){
			aw3644_enable_ctrl(flash, AW3644_LED0, false);
		}
		else if(channel == 1){
			aw3644_enable_ctrl(flash, AW3644_LED1, false);
		}
		aw3644_set_power(flash, AW3644_OFF);
		break;
	case FRONT_TORCH_ON:
		pr_info("%s:%d [AT+BKBGD=1] open front torch, ignore the case if PD program no front led \n", __func__, __LINE__);
		break;
	case FRONT_TORCH_OFF:
		pr_info("%s:%d [AT+BKBGD=0] close front torch, ignore the case if PD program no front led \n", __func__, __LINE__);
		break;
	case BBK_FLASH_AT_TEST:
		fac_flash_brt = flash->flash_dev_id[channel].factory_flash_brt;
		brt_bits = AW3644_FLASH_BRT_uA_TO_REG(fac_flash_brt * 1000);
		pr_info("%s:%d [AT+FLAMP=1,1 or AT+FLAMP=2,1] brt = %dmA(0x%x)\n", __func__, __LINE__, fac_flash_brt, brt_bits);
		if(channel == 0){
			aw3644_write_reg(flash, REG_LED0_FLASH_BR, 0x7f, brt_bits);/*(Brightnees code x 11.72mA)+11.35ma Torch*/
			val = aw3644_read_reg(flash, REG_LED0_FLASH_BR,  &rval);
			pr_info("aw3644 flash rval = %d ,%d \n", val, rval);
			flash->led_mode = V4L2_FLASH_LED_MODE_FLASH;
			aw3644_mode_ctrl(flash);
			aw3644_enable_ctrl(flash, AW3644_LED0, true);
		}
		else if(channel == 1){
			aw3644_write_reg(flash, REG_LED1_FLASH_BR, 0x7f, brt_bits);/*(Brightnees code x 11.72mA)+11.35ma Torch*/
			val = aw3644_read_reg(flash, REG_LED1_FLASH_BR,  &rval);
			pr_info("aw3644 flash2 rval = %d ,%d \n", val, rval);
			flash->led_mode = V4L2_FLASH_LED_MODE_FLASH;
			aw3644_mode_ctrl(flash);
			aw3644_enable_ctrl(flash, AW3644_LED1, true);
		}
		break;
	case BBK_FLASH_AT_OFF:
		pr_info("%s:%d [AT+FLAMP=1,0 or AT+FLAMP=2,0] \n", __func__, __LINE__);
		flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
		aw3644_mode_ctrl(flash);
		if(channel == 0){
			aw3644_enable_ctrl(flash, AW3644_LED0, false);
		}
		else if(channel == 1){
			aw3644_enable_ctrl(flash, AW3644_LED1, false);
		}
		aw3644_set_power(flash, AW3644_OFF);
		break;
	default:
		pr_info("set_flashlight_state No such command and arg\n");
		return -ENOTTY;
	}
	return 0;
}
/* add by vivo oumeiyin end */

static int aw3644_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;
	/* add by vivo oumeiyin start */
	int led_state;
	/* add by vivo oumeiyin end   */
	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	switch (cmd) {
	case FLASH_IOC_SET_ONOFF:
		pr_info_ratelimited("FLASH_IOC_SET_ONOFF(%d): %d\n",
				channel, (int)fl_arg->arg);
		if ((int)fl_arg->arg) {
			aw3644_torch_brt_ctrl(aw3644_flash_data, channel, 25000);
			aw3644_flash_data->led_mode = V4L2_FLASH_LED_MODE_TORCH;
			aw3644_mode_ctrl(aw3644_flash_data);
			aw3644_enable_ctrl(aw3644_flash_data, channel, true);
		} else {
			if (aw3644_flash_data->led_mode != V4L2_FLASH_LED_MODE_NONE) {
				aw3644_flash_data->led_mode = V4L2_FLASH_LED_MODE_NONE;
				aw3644_mode_ctrl(aw3644_flash_data);
				aw3644_enable_ctrl(aw3644_flash_data, channel, false);
			}
		}
		break;
	/* add by vivo oumeiyin start */
	case FLASH_IOCTL_SET_LED_STATE:
  		pr_info("FLASH_IOCTL_SET_LED_STATE(channel %d): arg: %d\n",
  				channel, (int)fl_arg->arg);
  		led_state = (int)fl_arg->arg;
  		set_flashlight_state(aw3644_flash_data, channel, led_state);
  		break;
	/* add by vivo oumeiyin end */
	default:
		pr_info("aw3644_ioctl No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

static int aw3644_set_driver(int set)
{
	int ret = 0;

	/* set chip and usage count */
	//mutex_lock(&aw3644_mutex);
	if (set) {
		if (!use_count)
			ret = aw3644_init(aw3644_flash_data);
		use_count++;
		pr_debug("Set driver: %d\n", use_count);
	} else {
		use_count--;
		if (!use_count)
			ret = aw3644_uninit(aw3644_flash_data);
		if (use_count < 0)
			use_count = 0;
		pr_debug("Unset driver: %d\n", use_count);
	}
	//mutex_unlock(&aw3644_mutex);

	return 0;
}

static ssize_t aw3644_strobe_store(struct flashlight_arg arg)
{
	aw3644_set_driver(1);
	//aw3644_set_level(arg.channel, arg.level);
	//aw3644_timeout_ms[arg.channel] = 0;
	//aw3644_enable(arg.channel);
	aw3644_torch_brt_ctrl(aw3644_flash_data, arg.channel,
				arg.level * 25000);
	aw3644_enable_ctrl(aw3644_flash_data, arg.channel, true);
	aw3644_flash_data->led_mode = V4L2_FLASH_LED_MODE_TORCH;
	aw3644_mode_ctrl(aw3644_flash_data);
	msleep(arg.dur);
	//aw3644_disable(arg.channel);
	aw3644_flash_data->led_mode = V4L2_FLASH_LED_MODE_NONE;
	aw3644_mode_ctrl(aw3644_flash_data);
	aw3644_enable_ctrl(aw3644_flash_data, arg.channel, false);
	aw3644_set_driver(0);
	return 0;
}

static int aw3644_cooling_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct aw3644_flash *flash = cdev->devdata;

	*state = flash->max_state;

	return 0;
}

static int aw3644_cooling_get_cur_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct aw3644_flash *flash = cdev->devdata;

	*state = flash->target_state;

	return 0;
}

static int aw3644_cooling_set_cur_state(struct thermal_cooling_device *cdev,
					unsigned long state)
{
	struct aw3644_flash *flash = cdev->devdata;
	int ret = 0;

	/* Request state should be less than max_state */
	if (state > flash->max_state)
		state = flash->max_state;
	if (state < 0)
		state = 0;

	if (flash->target_state == state)
		return 0;

	flash->target_state = state;
	pr_info("set thermal current:%d\n", flash->target_state);

	if (flash->target_state == 0) {
		flash->need_cooler = 0;
		flash->target_current = AW3644_FLASH_BRT_MAX;
		ret = aw3644_torch_brt_ctrl(flash, AW3644_LED0,
						flash->ori_current);
		ret = aw3644_torch_brt_ctrl(flash, AW3644_LED1,
						flash->ori_current);
	} else {
		flash->need_cooler = 1;
		flash->target_current =
			flash_state_to_current_limit[flash->target_state - 1];
		ret = aw3644_torch_brt_ctrl(flash, AW3644_LED0,
						flash->target_current);
		ret = aw3644_torch_brt_ctrl(flash, AW3644_LED1,
						flash->target_current);
	}
	return ret;
}

static struct thermal_cooling_device_ops aw3644_cooling_ops = {
	.get_max_state		= aw3644_cooling_get_max_state,
	.get_cur_state		= aw3644_cooling_get_cur_state,
	.set_cur_state		= aw3644_cooling_set_cur_state,
};

static struct flashlight_operations aw3644_flash_ops = {
	aw3644_flash_open,
	aw3644_flash_release,
	aw3644_ioctl,
	aw3644_strobe_store,
	aw3644_set_driver
};

static int aw3644_parse_dt(struct aw3644_flash *flash)
{
	struct device_node *np, *cnp;
	struct device *dev = flash->dev;
	u32 decouple = 0;
	int i = 0;

	if (!dev || !dev->of_node)
		return -ENODEV;

	np = dev->of_node;
	for_each_child_of_node(np, cnp) {
		if (of_property_read_u32(cnp, "type",
					&flash->flash_dev_id[i].type))
			goto err_node_put;
		if (of_property_read_u32(cnp,
					"ct", &flash->flash_dev_id[i].ct))
			goto err_node_put;
		if (of_property_read_u32(cnp,
					"part", &flash->flash_dev_id[i].part))
			goto err_node_put;

		if (of_property_read_u32(cnp,
					"factory_torch_brt", &flash->flash_dev_id[i].factory_torch_brt)) {
			pr_info("DTS factory_torch_brt isn't configured, use default value:min torch brightness");
			flash->flash_dev_id[i].factory_torch_brt = AW3644_TORCH_BRT_MIN;
		}
		if (of_property_read_u32(cnp,
					"factory_flash_brt", &flash->flash_dev_id[i].factory_flash_brt)) {
			pr_info("DTS factory_flash_brt isn't configured, use default value:min flash brightness");
			flash->flash_dev_id[i].factory_flash_brt = AW3644_FLASH_BRT_MIN;
		}

		snprintf(flash->flash_dev_id[i].name, FLASHLIGHT_NAME_SIZE,
				flash->subdev_led[i].name);
		flash->flash_dev_id[i].channel = i;
		flash->flash_dev_id[i].decouple = decouple;

		pr_info("Parse dt (type,ct,part,name,channel,decouple)=(%d,%d,%d,%s,%d,%d).\n",
				flash->flash_dev_id[i].type,
				flash->flash_dev_id[i].ct,
				flash->flash_dev_id[i].part,
				flash->flash_dev_id[i].name,
				flash->flash_dev_id[i].channel,
				flash->flash_dev_id[i].decouple);
		if (!aw3644_judge(aw3644_flash_data)) {
			pr_info("%s:%d The device isn't AW3644", __func__,__LINE__);
			return -EFAULT;
		}
		if (flashlight_dev_register_by_device_id(&flash->flash_dev_id[i],
			&aw3644_flash_ops))
			return -EFAULT;
		i++;
	}
	if (of_property_read_u32(np, "enableType", &flash->enableType))
		goto err_node_put;

	return 0;

err_node_put:
	of_node_put(cnp);
	return -EINVAL;
}
static bool aw3644_judge(struct aw3644_flash *flash)
{
	unsigned int flashic_id_val = 0;
	unsigned int device_id_val = 0;

	//return true: The IC is AW3644; false mean the IC isn't AW3644
	//flash ic reg address: 0x0C,AW3644 value = 0x30
	//device id reg addreee: 0x02, AW3644 value = 0x02
	// aw3644_set_power(flash, AW3644_ON);
	aw3644_read_reg(flash, REG_DEVICE_ID, &device_id_val);
	aw3644_read_reg(flash, REG_FLASHIC_ID, &flashic_id_val);
	// aw3644_set_power(flash, AW3644_OFF);
	pr_info("%s:%d DeviceID(0x%x) IC_ID(0x%x)", __func__, __LINE__, device_id_val, flashic_id_val);
	if (device_id_val == AW3644_DEVICE_ID && flashic_id_val == AW3644_FLASHIC_ID)
		return true;
	else
		return false;
}
static int aw3644_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct aw3644_flash *flash;
	struct aw3644_platform_data *pdata = dev_get_platdata(&client->dev);
	int rval;
	// bool ret = true;

	pr_info("%s:%d", __func__, __LINE__);

	flash = devm_kzalloc(&client->dev, sizeof(*flash), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;

	flash_status= devm_kzalloc(&client->dev, sizeof(*flash_status), GFP_KERNEL);
	if (flash_status == NULL)
		return -ENOMEM;

	flash->regmap = devm_regmap_init_i2c(client, &aw3644_regmap);
	flash->regulator_enable = devm_regulator_get(&client->dev, "flash_enable");
	if (IS_ERR(flash->regulator_enable)) {
			rval = PTR_ERR(flash->regulator_enable);
			return rval;
		}
    client->addr = 0x63;
	/* if there is no platform data, use chip default value */
	if (pdata == NULL) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (pdata == NULL)
			return -ENODEV;
		pdata->max_flash_timeout = AW3644_FLASH_TOUT_MAX;
		/* led 1 */
		pdata->max_flash_brt[AW3644_LED0] = AW3644_FLASH_BRT_MAX;
		pdata->max_torch_brt[AW3644_LED0] = AW3644_TORCH_BRT_MAX;
		/* led 2 */
		pdata->max_flash_brt[AW3644_LED1] = AW3644_FLASH_BRT_MAX;
		pdata->max_torch_brt[AW3644_LED1] = AW3644_TORCH_BRT_MAX;
	}
	flash->pdata = pdata;
	flash->dev = &client->dev;
	flash->enableType = 0;
	mutex_init(&flash->lock);
	aw3644_flash_data = flash;

	// ret = aw3644_judge(flash);
	// if (!ret)
	// 	return -ENODEV;

	rval = aw3644_subdev_init(flash, AW3644_LED0, "aw3644-led0");
	if (rval < 0)
		return rval;

	rval = aw3644_subdev_init(flash, AW3644_LED1, "aw3644-led1");
	if (rval < 0)
		return rval;

	pm_runtime_enable(flash->dev);

	rval = aw3644_parse_dt(flash);

	i2c_set_clientdata(client, flash);

	flash->max_state = AW3644_COOLER_MAX_STATE;
	flash->target_state = 0;
	flash->need_cooler = 0;
	flash->target_current = AW3644_FLASH_BRT_MAX;
	flash->ori_current = 0;
	flash->cdev = thermal_of_cooling_device_register(client->dev.of_node,
			"flashlight_cooler", flash, &aw3644_cooling_ops);
	if (IS_ERR(flash->cdev))
		pr_info("register thermal failed\n");

	flash_status->status_led1 = true;
	flash_status->status_led2 = true;

	pr_info("%s:%d", __func__, __LINE__);
	return 0;
}

static int aw3644_remove(struct i2c_client *client)
{
	struct aw3644_flash *flash = i2c_get_clientdata(client);
	unsigned int i;

	thermal_cooling_device_unregister(flash->cdev);
	for (i = AW3644_LED0; i < AW3644_LED_MAX; i++) {
		v4l2_device_unregister_subdev(&flash->subdev_led[i]);
		v4l2_ctrl_handler_free(&flash->ctrls_led[i]);
		media_entity_cleanup(&flash->subdev_led[i].entity);
	}

	pm_runtime_disable(&client->dev);

	pm_runtime_set_suspended(&client->dev);
	return 0;
}

static int __maybe_unused aw3644_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw3644_flash *flash = i2c_get_clientdata(client);

	pr_info("%s %d", __func__, __LINE__);

	return aw3644_uninit(flash);
}

static int __maybe_unused aw3644_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw3644_flash *flash = i2c_get_clientdata(client);

	pr_info("%s %d", __func__, __LINE__);

	return aw3644_init(flash);
}

static const struct i2c_device_id aw3644_id_table[] = {
	{AW3644_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, aw3644_id_table);

static const struct of_device_id aw3644_of_table[] = {
	{ .compatible = "mediatek,aw3644" },
	{ },
};
MODULE_DEVICE_TABLE(of, aw3644_of_table);

static const struct dev_pm_ops aw3644_pm_ops = {
//	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
//				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(aw3644_suspend, aw3644_resume, NULL)
};

static struct i2c_driver aw3644_i2c_driver = {
	.driver = {
		   .name = AW3644_NAME,
		   .pm = &aw3644_pm_ops,
		   .of_match_table = aw3644_of_table,
		   },
	.probe = aw3644_probe,
	.remove = aw3644_remove,
	.id_table = aw3644_id_table,
};

module_i2c_driver(aw3644_i2c_driver);

MODULE_AUTHOR("Roger-HY Wang <roger-hy.wang@mediatek.com>");
MODULE_DESCRIPTION("Texas Instruments AW3644 LED flash driver");
MODULE_LICENSE("GPL");
