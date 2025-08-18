/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

struct mtk_extcon_info {
	struct device *dev;
	struct extcon_dev *edev;
	struct usb_role_switch *role_sw;
	unsigned int c_role; /* current data role */
	struct workqueue_struct *extcon_wq;
	struct regulator *vbus;
	unsigned int vbus_vol;
	unsigned int vbus_cur;
	bool vbus_on;
	struct power_supply *usb_psy;
	struct notifier_block psy_nb;
	struct delayed_work wq_psy;
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	struct tcpc_device *tcpc_dev;
	struct notifier_block tcpc_nb;
#endif
	bool bypss_typec_sink;
	/* id gpio */
	struct gpio_desc *id_gpiod;
	unsigned int id_irq;
	struct delayed_work wq_detcable;

	unsigned int dr;
	bool host_mode_enabled;
	bool typec_hw_det;
	struct delayed_work host_disabled_work;
	struct mutex switch_mutex;
	struct wakeup_source *switch_lock;
};

struct usb_role_info {
	struct mtk_extcon_info *extcon;
	struct delayed_work dwork;
	unsigned int d_role; /* desire data role */
};

enum {
	DUAL_PROP_MODE_UFP = 0,
	DUAL_PROP_MODE_DFP,
	DUAL_PROP_MODE_NONE,
};

enum {
	DUAL_PROP_PR_SRC = 0,
	DUAL_PROP_PR_SNK,
	DUAL_PROP_PR_NONE,
};


enum host_mode {
	HOST_MODE_DISABLE = 0,
	HOST_MODE_ENABLE,
	HOST_MODE_DISABLE_HW_DET,
	HOST_MODE_ENABLE_HW_DET,
};

extern void mt_usbaudio_connect(void);
extern void mt_usbaudio_disconnect(void);
