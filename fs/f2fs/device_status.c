/*
 * fs/f2fs/device_status.c
 *
 * Copyright (c) 2019 VIVO Co., Ltd.
 *             http://www.vivo.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/init.h>
#include <linux/f2fs_fs.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/backlight.h>
#include <linux/notifier.h>

#include "f2fs.h"
#include "segment.h"
#include "gc.h"

/* USB state: 1 -> connect, 0 -> disconnect */
int usb_state(struct f2fs_sb_info *sbi)
{
	return atomic_read(&sbi->usb_state);
}

/* screen state: 1 -> off */
int screen_state(struct f2fs_sb_info *sbi)
{
	union power_supply_propval backlight;
	struct power_supply *psy;
	int ret;

	psy = power_supply_get_by_name("display");
	if (!psy) {
		pr_err("Couldn't get dispsy\n");
		return 0;
	}

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &backlight);
	if (ret < 0) {
		pr_err("Couldn't get present from dis rc=%d\n", ret);
		return 0;
	}
	power_supply_put(psy);

	if (backlight.intval)
		return 0;
	else
		return 1;
}

bool f2fs_gc_usb_screen_status(struct f2fs_sb_info *sbi)
{
	return usb_state(sbi) && screen_state(sbi);
}

static void f2fs_usb_status_work(struct work_struct *work)
{
	int rc;
	union power_supply_propval usb_in = {0,};
	struct power_supply *usb_psy;
	struct f2fs_sb_info *sbi = container_of(work,
			struct f2fs_sb_info, usb_status_work.work);

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		pr_err("Couldn't get usbpsy\n");
		return;
	}

	rc = power_supply_get_property(usb_psy, POWER_SUPPLY_PROP_PRESENT, &usb_in);
	if (rc < 0) {
		pr_err("Couldn't get present from USB rc=%d\n", rc);
		return;
	}

	if (usb_in.intval)
		atomic_set(&sbi->usb_state, 1);
	else
		atomic_set(&sbi->usb_state, 0);

	pr_info("%s lcw usb: %d, lcd: %d, dirty segs: %u, free segs: %u, user block: %u, "
		"used: %d%%, undiscard: %u\n", __func__, usb_state(sbi),
		screen_state(sbi), dirty_segments(sbi), free_segments(sbi),
		sbi->user_block_count, utilization(sbi),
		SM_I(sbi)->dcc_info ? SM_I(sbi)->dcc_info->undiscard_blks : 0);
}

static int f2fs_usb_state_notifier_callback(struct notifier_block *nb,
					       unsigned long event, void *data)
{
	struct power_supply *usb_psy = data;
	struct f2fs_sb_info *sbi;

	if (!usb_psy) {
		pr_err("%s: data is null\n", __func__);
		return NOTIFY_OK;
	}

	if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if (strcmp(usb_psy->desc->name, "usb") == 0) {
		sbi = container_of(nb, struct f2fs_sb_info, usb_nb);
		schedule_delayed_work(&sbi->usb_status_work, 0);
	}

	return NOTIFY_OK;
}

void init_device_callback(struct f2fs_sb_info *sbi)
{
	int ret;

	if (!sbi)
		return;

	atomic_set(&sbi->usb_state, 0);

	INIT_DELAYED_WORK(&sbi->usb_status_work, f2fs_usb_status_work);

	sbi->usb_nb.notifier_call = f2fs_usb_state_notifier_callback;
	ret = power_supply_reg_notifier(&sbi->usb_nb);
	if (ret)
		pr_err("failed to reg power supply notifier: %d\n", ret);
}

void exit_device_callback(struct f2fs_sb_info *sbi)
{
	cancel_delayed_work_sync(&sbi->usb_status_work);
	power_supply_unreg_notifier(&sbi->usb_nb);
	sbi->usb_nb.notifier_call = NULL;
}
