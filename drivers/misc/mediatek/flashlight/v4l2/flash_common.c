// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 vivo Inc.
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>
#include <linux/pinctrl/consumer.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/pm_runtime.h>
#include <linux/thermal.h>

#include "flash_common.h"

static void flash_check_ocp(struct regmap *regmap, struct flash_status *flash_status)
{
	int ret = 0;
	unsigned int reg_val = 0;

	ret = regmap_read(regmap, REG_FLAG1, &reg_val);
	if (ret < 0)
		pr_info_ratelimited("regmap_read REG_ENABLE fail \n");

	if (reg_val & BIT(5)) {
		flash_status->status_led1 = false;
	}
	if (reg_val & BIT(4)) {
		flash_status->status_led2 = false;
	}
}

static void flash_check_ovp(struct regmap *regmap, struct flash_status *flash_status)
{
	unsigned int reg_flag2 = 0;
	unsigned int reg_enable = 0;
	u8 retry = 1;
	int ret = 0;

recheck:
	regmap_read(regmap, REG_ENABLE, &reg_enable);
	if (ret < 0)
		pr_info_ratelimited("regmap_read REG_ENABLE fail \n");

	regmap_read(regmap, REG_FLAG2, &reg_flag2);
	if (ret < 0)
		pr_info_ratelimited("regmap_read REG_FLAG2 fail \n");

	pr_info_ratelimited("reg_enable = %d; reg_flag2 = %d\n", reg_enable,
			    reg_flag2);

	if (reg_flag2) {
		switch (reg_enable & 0x03) {
		case 0x03: {
			flash_status->status_led1 = false;

			reg_enable &= ~BIT(0);

			reg_enable |= (0x2 << 2);

			regmap_update_bits(regmap, REG_ENABLE, 0xFF,
					   reg_enable);

			msleep(1);

			if (retry == 1) {
				retry = 0;
				goto recheck;
			}
		} break;
		case 0x02: {
			flash_status->status_led2 = false;
			flash_status->status_led1 = true;
		} break;
		default:
			pr_info_ratelimited("flag2(%#x)\n", reg_flag2);
		}
	}
}

int flash_check_status(struct regmap *regmap, struct flash_status *flash_status)
{
	int ret = 0;
	unsigned int reg_val = 0;

	if (regmap == NULL || flash_status == NULL) {
		pr_info_ratelimited(" regmap or flash_status is null\n");
		return -1;
	}

	pr_info_ratelimited(" flash_check_status\n");

	ret = regmap_read(regmap, REG_ENABLE, &reg_val);
	if (ret < 0)
		pr_info_ratelimited(" regmap_read REG_ENABLE fail \n");

	if ((reg_val & (0x08)) != 0x08) {
		flash_check_ocp(regmap, flash_status);

		flash_check_ovp(regmap, flash_status);

		ret = regmap_update_bits(regmap, REG_ENABLE, 0x0C, 0x08);
		if (ret < 0)
			pr_info_ratelimited(" regmap_read REG_ENABLE fail \n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(flash_check_status);

MODULE_DESCRIPTION("Texas Instruments flash common driver");
MODULE_LICENSE("GPL");
