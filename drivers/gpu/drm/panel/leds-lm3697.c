 /*
* Copyright (C) 2008-2009 Motorola, Inc.
* Author: Alina Yakovleva <qvdh43@motorola.com>
*
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
//#include <linux/gpio_event.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <leds-lm3697.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
//#include "vivo_src_panel_common.h"


#define LM3697_NAME "lm3697_i2c"
#define lm3697_DEBUG 1
#define VENDOR_ID_REGISTER 		0x00
#define VENDOR_ID_LM3697_OLD 	0x00
#define VENDOR_ID_LM3697_NEW 	0x01
#define VENDOR_ID_KTD3137 		0x18
#define LCD_INFO(fmt, ...)				\
	pr_info("DSI_LCD_INFO: %s(%d): " fmt,		\
	       __func__, __LINE__, ##__VA_ARGS__)

static struct i2c_client *lm3697_client;
static int old_backlight_value = 1;

struct lm3697_data {
uint16_t addr;
    struct i2c_client *client;
    struct input_dev *idev;
    struct lm3697_platform_data *pdata;
    struct led_classdev led_dev;
    struct led_classdev led_dev_tcmd;
    struct led_classdev led_dev_nr;
    struct led_classdev button_led;
    struct led_classdev button_led_tcmd;
    struct led_classdev webtop_led;
    #ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend early_suspend;
    #endif
    unsigned initialized;
unsigned in_webtop_mode;
    uint8_t revision;
    uint8_t enable_reg;
atomic_t suspended;
    atomic_t in_suspend;
    unsigned bvalue;
    unsigned saved_bvalue;
    struct work_struct work;
};

static DEFINE_MUTEX(lm3697_mutex);

struct lm3697_reg {
    const char *name;
    uint8_t reg;
};

struct lm3697_reg lm3697_regs[] = {
    {"OUTPUT_CFG_REG", LM3697_OUTPUT_CFG_REG},
    {"CONTROL_A_START_UP_RAMP_REG", LM3697_CONTROL_A_START_UP_RAMP_REG},
    {"CONTROL_B_START_UP_RAMP_REG", LM3697_CONTROL_B_START_UP_RAMP_REG},
    {"RUN_TIME_RAMP_TIME_REG", LM3697_RUN_TIME_RAMP_TIME_REG},
    {"RUN_TIME_RAMP_CFG_REG", LM3697_RUN_TIME_RAMP_CFG_REG},
    {"CTRL_BR_CFG_REG", LM3697_CTRL_BR_CFG_REG},
    {"CTRL_A_FS_CURR_REG", LM3697_CTRL_A_FS_CURR_REG},
    {"CTRL_B_FS_CURR_REG", LM3697_CTRL_B_FS_CURR_REG},
    {"FEEDBACK_ENABLE_REG", LM3697_FEEDBACK_ENABLE_REG},
    {"BOOST_CONTROL_REG", LM3697_BOOST_CONTROL_REG},
    {"PWM_CFG_REG", LM3697_PWM_CFG_REG},
    {"CONTROL_A_BRIGHT_LSB_REG", LM3697_CONTROL_A_BRIGHT_LSB_REG},
    {"CONTROL_A_BRIGHT_MSB_REG", LM3697_CONTROL_A_BRIGHT_MSB_REG},
    {"CONTROL_B_BRIGHT_LSB_REG", LM3697_CONTROL_B_BRIGHT_LSB_REG},
    {"CONTROL_B_BRIGHT_MSB_REG", LM3697_CONTROL_B_BRIGHT_MSB_REG},
    {"CONTROL_BANK_ENABLE_REG", LM3697_CONTROL_BANK_ENABLE_REG},
    {"HVLED_OPEN_FAULTS_REG", LM3697_HVLED_OPEN_FAULTS_REG},
    {"HVLED_SHORT_FAULTS_REG", LM3697_HVLED_SHORT_FAULTS_REG},
    {"LED_FAULT_ENABLE_REG", LM3697_LED_FAULT_ENABLE_REG},
};

struct lm3697_reg ktd3136_regs[] = {
    {"KTD3161_DEVICE_ID_REG",		KTD3161_DEVICE_ID_REG},
    {"KTD3161_SW_RESET_REG", 		KTD3161_SW_RESET_REG},
    {"KTD3161_MODE_REG", 			KTD3161_MODE_REG},
    {"KTD3161_CONTROL_REG", 		KTD3161_CONTROL_REG},
    {"KTD3161_LED_CUR_LSB_REG", 	KTD3161_LED_CUR_LSB_REG},
    {"KTD3161_LED_CUR_MSB_REG", 	KTD3161_LED_CUR_MSB_REG},
    {"KTD3161_PWM_REG", 			KTD3161_PWM_REG},
    {"KTD3161_TURNIMG_RAMP_REG", 	KTD3161_TURNIMG_RAMP_REG},
    {"KTD3161_TRANSITION_REG", 		KTD3161_TRANSITION_REG},
    {"KTD3161_STATUS_REG", 			KTD3161_STATUS_REG},
};

static int lm3697_read_reg (struct i2c_client *client,
    unsigned reg, uint8_t *value)
{
    uint8_t buf[1];
    int ret = 0;

	if (!client) {
		LCD_INFO ("%s: client is NULL\n", __func__);
		return -EINVAL;
	}

    if (!value)
	    return -EINVAL;
    buf[0] = reg;
    ret = i2c_master_send (client, buf, 1);
    if (ret > 0) {
	    msleep_interruptible (1);
	    ret = i2c_master_recv (client, buf, 1);
	    if (ret > 0)
		    *value = buf[0];
    }
    return ret;
}

static int lm3697_write_reg (struct i2c_client *client,
	unsigned char reg,	uint8_t value,	const char *caller)
{
	uint8_t buf[2] = {reg, value};
	int ret = 0;
	if (!client) {
		printk ("%s: client is NULL\n",  __func__);
		return -EINVAL;
	}

	ret = i2c_master_send (client, buf,  2);
	if (ret < 0)
		printk ("%s: i2c_master_send error %d\n",
			caller, ret);
	return ret;
}


static int bl_ic_id_read (struct i2c_client *client)
{
	uint8_t buf[1];
	int ret = 0;
	//struct vivo_display *vdisp = get_vivo_vdisp();
	mutex_lock (&lm3697_mutex);
	ret = lm3697_read_reg(client, VENDOR_ID_REGISTER, buf);
	if ((ret < 0) || ((buf[0] != VENDOR_ID_KTD3137) && (buf[0] != VENDOR_ID_LM3697_OLD) && (buf[0] != VENDOR_ID_LM3697_NEW))) {
		// vdisp->bk_lcmic_id = vdisp->bk_lcmic_id | BL_IC_SMT_DETECT_FAIL;
		LCD_INFO("lm3697_client error%d", ret);
	}
	//LCD_INFO("lm3697_client run init, read id ret =%d, bl ic id =0x%x, bk_lcmic_id = 0x%x\n", ret, buf[0], vdisp->bk_lcmic_id);
	LCD_INFO("lm3697_client run init, read id ret =%d, bl ic id =0x%x, \n", ret, buf[0]);
	mutex_unlock (&lm3697_mutex);
	return 0;
}

static int lm3697_write_bytes(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = lm3697_client;
	char write_data[2] = { 0 };
	write_data[0] = addr;
	write_data[1] = value;
	ret = i2c_master_send(client, write_data, 2);
	if (ret < 0)
		LCD_INFO("lm3697 write data fail !!\n");
	return ret;
}

int lm3697_init_registers(void)
{
	int ret = 0;
	//struct vivo_display *vdisp = get_vivo_vdisp();

	LCD_INFO("%s: enter, bl_hw_version\n", __func__);
	mdelay(1);
	if (1) {
		lm3697_write_bytes(LM3697_OUTPUT_CFG_REG, 0x00);
		lm3697_write_bytes(LM3697_RUN_TIME_RAMP_TIME_REG, 0x12);
		lm3697_write_bytes(LM3697_CTRL_BR_CFG_REG, 0x00);
		lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_LSB_REG, 0x00);
		lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_MSB_REG, 0x00);

#ifdef CONFIG_VIVO_I2C_BACKLIGHT_CURRENT_21MA
		lm3697_write_bytes(LM3697_CTRL_A_FS_CURR_REG, 0x15);
#else
		lm3697_write_bytes(LM3697_CTRL_A_FS_CURR_REG, 0x13);
#endif

#ifdef CONFIG_VIVO_LCM_LM3697_BOOST_CONTROL
		lm3697_write_bytes(LM3697_BOOST_CONTROL_REG, 0x0D);
#elif defined(CONFIG_VIVO_LCM_LM3697_LED_9_10_SERIAL)
		lm3697_write_bytes(LM3697_BOOST_CONTROL_REG, 0x07);
#elif defined(CONFIG_VIVO_LCM_LM3697_LED_4_5_SERIAL)
		lm3697_write_bytes(LM3697_BOOST_CONTROL_REG, 0x03);
#else
		lm3697_write_bytes(LM3697_BOOST_CONTROL_REG, 0x05);
#endif
		lm3697_write_bytes(LM3697_PWM_CFG_REG, 0x00);

#ifdef CONFIG_VIVO_LCM_LM3697_LED_3_FEEDBACK
		lm3697_write_bytes(LM3697_FEEDBACK_ENABLE_REG, 0x07);
#else
		lm3697_write_bytes(LM3697_FEEDBACK_ENABLE_REG, 0x03);
#endif

		lm3697_write_bytes(LM3697_CONTROL_BANK_ENABLE_REG, 0x01);
	} else {

#ifdef CONFIG_VIVO_I2C_BACKLIGHT_CURRENT_21MA
		lm3697_write_bytes(0x02, 0xA8);
#else
		lm3697_write_bytes(0x02, 0x98);
#endif
		mdelay(1);
		lm3697_write_bytes(0x03, 0x68);
		lm3697_write_bytes(0x04, 0x00);
		lm3697_write_bytes(0x05, 0x00);
#ifdef CONFIG_VIVO_LCM_LM3697_LED_3_FEEDBACK
		lm3697_write_bytes(0x06, 0x27);
#else
		lm3697_write_bytes(0x06, 0x23);
#endif
		lm3697_write_bytes(0x07, 0x33);
		lm3697_write_bytes(0x08, 0x15);
		lm3697_write_bytes(0x09, 0x00);
#ifdef CONFIG_VIVO_I2C_BACKLIGHT_CURRENT_21MA
		lm3697_write_bytes(0x02, 0xA9);
#else
		lm3697_write_bytes(0x02, 0x99);
#endif

	}
	return ret;
}

void bbk_backlight_disable(void)
{
	struct i2c_client *client;
	client = lm3697_client;

	if (!client) {
		LCD_INFO ("%s: client is NULL\n", __func__);
		return ;
	}

	lm3697_write_bytes (LM3697_CONTROL_BANK_ENABLE_REG, 0x00);
	lm3697_write_bytes (LM3697_FEEDBACK_ENABLE_REG, 0x00);
}

static void lm3697_backlight_set (struct led_classdev *led_cdev,
enum led_brightness value)
{
	struct i2c_client *client;
	//struct vivo_display *vdisp = get_vivo_vdisp();

	client = lm3697_client;

	if (!client) {
		LCD_INFO ("%s:, client is NULL: [value=%d] \n", __func__, value);
		return ;
	}

	mutex_lock (&lm3697_mutex);
	if (value <= 0) {
		if (1) {
			lm3697_write_bytes (LM3697_CONTROL_BANK_ENABLE_REG, 0x00);
			lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_LSB_REG, 0x00);
			lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_MSB_REG, 0x00);
		} else {
			lm3697_write_bytes (0x06, 0x80);
			lm3697_write_bytes (0x04, 0x00);
			lm3697_write_bytes (0x05, 0x00);
		}
		old_backlight_value = 0;
	} else {
		if (old_backlight_value == 0)
			lm3697_init_registers();
		if (1) {
			lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_LSB_REG, 0x00);
			lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_MSB_REG, value);
			lm3697_write_bytes (LM3697_CONTROL_BANK_ENABLE_REG, 0x01);
		} else {
			lm3697_write_bytes (0x04, 0x00);
			lm3697_write_bytes (0x05, value);
		}
		old_backlight_value = value;
	}
	mutex_unlock (&lm3697_mutex);
	LCD_INFO ("lm3697 set: [value=%d], bl_hw_version= \n", value);
	return ;
}

void lm3697_brightness_set(int value)
{
	struct i2c_client *client;
	int backlight_lsb = 0;
	int backlight_msb = 0;
	//struct vivo_display *vdisp = get_vivo_vdisp();

	client = lm3697_client;

	if (!client) {
		LCD_INFO ("%s:, client is NULL: [value=%d] \n", __func__, value);
		return ;
	}
	mutex_lock (&lm3697_mutex);
	if (value <= 0) {
		if (1) {
			lm3697_write_bytes (LM3697_CONTROL_BANK_ENABLE_REG, 0x00);
			lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_LSB_REG, 0x00);
			lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_MSB_REG, 0x00);
		} else {
			lm3697_write_bytes (0x06, 0x80);
			lm3697_write_bytes (0x04, 0x00);
			lm3697_write_bytes (0x05, 0x00);
		}
		old_backlight_value = 0;
	} else {
		if (old_backlight_value == 0)
			lm3697_init_registers();
		backlight_lsb = value & 0x07;
		backlight_msb = (value >> 3) & 0xFF;
		/*LCD_INFO ("lm3697 set: backlight_lsb is %0x,  backlight_msb is %02x\n", backlight_lsb, backlight_msb);*/
		if (1) {
			lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_LSB_REG, backlight_lsb);
			lm3697_write_bytes (LM3697_CONTROL_A_BRIGHT_MSB_REG, backlight_msb);

			if (old_backlight_value == 0) {
				lm3697_write_bytes (LM3697_CONTROL_BANK_ENABLE_REG, 0x01);
			}
		} else {
			lm3697_write_bytes (0x04, backlight_lsb);
			lm3697_write_bytes (0x05, backlight_msb);
		}
		old_backlight_value = value;
	}

	mutex_unlock (&lm3697_mutex);
	LCD_INFO ("lm3697 set: [value=%d] \n", value);
	return ;
}
EXPORT_SYMBOL(lm3697_brightness_set);
void lm3697_full_scale_current_set(int state)
{

	struct i2c_client *client;
	static int pre_index = 0;
	int destin_index = 0, i = 0;
	//struct vivo_display *vdisp = get_vivo_vdisp();

	int ktd3136[] = {0x99, 0xa1, 0xa9, 0xb1, 0xb9, 0xc1, 0xc9, 0xd1};
	int lm3697[] = {0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a};
	client = lm3697_client;

	if (!client) {
		printk ("%s:,  client is NULL \n");
		return;
	}

	if (state == 0)//20.2mA
		destin_index = 0;
	else if (state == 1)//24.2mA
		destin_index = 5;
	else if (state == 2)//25.8mA
		destin_index = 7;

	if (old_backlight_value == 0)
		return;

	if (destin_index > pre_index) {
		for (i = pre_index; i <= destin_index; i++) {
			if (0)
				lm3697_write_reg(client, 0x02, ktd3136[i], __func__);
			else
				lm3697_write_reg(client, LM3697_CTRL_A_FS_CURR_REG, lm3697[i], __func__);
			usleep_range(100 * 1000, 120 * 1000);
		}
	} else {
		for (i = pre_index; i >= destin_index; i--) {
			if (0)
				lm3697_write_reg(client, 0x02, ktd3136[i], __func__);
			else
				lm3697_write_reg(client, LM3697_CTRL_A_FS_CURR_REG, lm3697[i], __func__);
			usleep_range(100 * 1000, 120 * 1000);
		}
	}
	pre_index = destin_index;
	return;
}
EXPORT_SYMBOL(lm3697_full_scale_current_set);

static ssize_t lm3697_registers_debug (struct i2c_client *client)
{
		int reg_count = 0, i = 0;
		struct lm3697_reg *regs_p;
		int n = 0;
		uint8_t value;
		reg_count = sizeof(lm3697_regs) / sizeof(lm3697_regs[0]);
		regs_p = lm3697_regs;
		printk(KERN_INFO "%s: reading registers\n", __func__);
		for (i = 0, n = 0; i < reg_count; i++) {
			lm3697_read_reg(client, regs_p[i].reg, &value);
			LCD_INFO("%-20s (0x%x) = 0x%02X\n",
					regs_p[i].name, regs_p[i].reg, value);
		}
	return 0;
}

static ssize_t lm3697_registers_show (struct device *dev,
struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev->parent,
	struct i2c_client, dev);
	struct lm3697_data *driver_data = i2c_get_clientdata(client);
	int reg_count = 0;
	int i, n = 0;
	uint8_t value = 0;
	struct lm3697_reg *regs_p;

	if (1) {
		reg_count = sizeof(lm3697_regs) / sizeof(lm3697_regs[0]);
		regs_p = lm3697_regs;
	} else {
		reg_count = sizeof(ktd3136_regs) / sizeof(ktd3136_regs[0]);
		regs_p = ktd3136_regs;
	}

	if (atomic_read(&driver_data->suspended)) {
		printk(KERN_INFO "%s: can't read: driver suspended\n", __func__);
		n += scnprintf(buf + n, PAGE_SIZE - n,
			"Unable to read lm3697 registers: driver suspended\n");
	} else {
		printk(KERN_INFO "%s: reading registers\n", __func__);
		for (i = 0, n = 0; i < reg_count; i++) {
			lm3697_read_reg(client, regs_p[i].reg, &value);
			n += scnprintf(buf + n, PAGE_SIZE - n,
				"%-20s (0x%x) = 0x%02X\n",
					regs_p[i].name, regs_p[i].reg, value);
		}
	}

	return n;
}

static ssize_t lm3697_registers_store (struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct i2c_client *client = container_of(dev->parent, struct i2c_client, dev);
	struct lm3697_data *driver_data = i2c_get_clientdata(client);
	unsigned reg;
	unsigned value;

	if (atomic_read(&driver_data->suspended)) {
		printk(KERN_INFO "%s: can't write: driver suspended\n", __func__);
		return -ENODEV;
	}
	sscanf(buf, "%x %x", &reg, &value);
	if (value > 0xFF)
		return -EINVAL;
	printk(KERN_INFO "%s: writing reg 0x%x = 0x%x\n", __func__, reg, value);
	mutex_lock(&lm3697_mutex);
	lm3697_write_bytes(reg, (uint8_t)value);
	mutex_unlock(&lm3697_mutex);

	return size;
}
static DEVICE_ATTR(registers, 0664, lm3697_registers_show, lm3697_registers_store);

/* This function is called by i2c_probe */
static int lm3697_probe (struct i2c_client *client,
    const struct i2c_device_id *id)
{
	int ret = 0;
	struct lm3697_platform_data *pdata = NULL;
	struct lm3697_data *driver_data;

	LCD_INFO("%s: enter, I2C address = 0x%x, flags = 0x%x\n", client->name, client->addr, client->flags);

	if (!client) {
		LCD_INFO ("%s: client is NULL\n", __func__);
		return -EINVAL;
	}

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
		sizeof(struct lm3697_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}
	} else {
		pdata = client->dev.platform_data;
	}

	if (pdata == NULL) {
		LCD_INFO ("%s: platform data required\n", __func__);
		return -EINVAL;
	}

    /* We should be able to read and write byte data */
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
	    LCD_INFO ("%s: I2C_FUNC_I2C not supported\n", __func__);
	    return -ENOTSUPP;
    }
    driver_data = kzalloc(sizeof(struct lm3697_data), GFP_KERNEL);
    if (driver_data == NULL) {
	    LCD_INFO ("%s: kzalloc failed\n", __func__);
	    return -ENOMEM;
    }
    memset (driver_data, 0, sizeof (*driver_data));

    driver_data->client = client;
    driver_data->pdata = pdata;
    if (driver_data->pdata->ctrl_a_fs_current > 0xFF)
	    driver_data->pdata->ctrl_a_fs_current = 0xFF;
    if (driver_data->pdata->ctrl_b_fs_current > 0xFF)
	    driver_data->pdata->ctrl_b_fs_current = 0xFF;

    i2c_set_clientdata (client, driver_data);

    /* Initialize chip */
    if (pdata->init) {
	    pdata->init();
    }

    if (pdata->power_on) {
	    pdata->power_on();
    }

    /* Register LED class */
    driver_data->led_dev.name = lm3697_LED_NAME;
    driver_data->led_dev.brightness_set = lm3697_backlight_set;
    ret = led_classdev_register (&client->dev, &driver_data->led_dev);
    if (ret) {
	    printk(KERN_ERR "%s: led_classdev_register %s failed: %d\n", __func__, lm3697_LED_NAME, ret);
    }

    atomic_set(&driver_data->in_suspend, 0);
    atomic_set(&driver_data->suspended, 0);
    ret = device_create_file(driver_data->led_dev.dev, &dev_attr_registers);
    if (ret) {
      printk(KERN_ERR "%s: unable to create suspend device file for %s: %d\n", __func__, lm3697_LED_NAME, ret);
	goto device_create_file_failed2;
    }

	dev_set_drvdata (&client->dev, driver_data);
	lm3697_client = client;
	driver_data->initialized = 1;
	ret = bl_ic_id_read(client);
	lm3697_brightness_set(0);
	lm3697_brightness_set(1024);
	lm3697_registers_debug(client);
	return 0;

device_create_file_failed2:
    device_remove_file (driver_data->led_dev.dev, &dev_attr_registers);
    kfree (driver_data);
    return ret;
}



/*****************************************************************************
 * Data Structure
 *****************************************************************************/

static const struct i2c_device_id lm3697_id[] = {
	{LM3697_NAME, 0},
	{}
};


static int lm3697_remove (struct i2c_client *client)
{
    struct lm3697_data *driver_data = i2c_get_clientdata(client);

    device_remove_file (driver_data->led_dev.dev, &dev_attr_registers);
    kfree (driver_data);
    return 0;
}

static struct i2c_driver lm3697_i2c_driver = {
	.id_table = lm3697_id,
	.probe = lm3697_probe,
	.remove = lm3697_remove,
	/* .detect               = mt6605_detect, */
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "lm3697_i2c",
		   },
};

static int __init lm3697_init(void)
{

	LCD_INFO("init lm3697_init \n");
	i2c_add_driver(&lm3697_i2c_driver);
	return 0;
}

static void __exit lm3697_exit(void)
{
	i2c_del_driver(&lm3697_i2c_driver);
}

module_init(lm3697_init);
module_exit(lm3697_exit);


MODULE_DESCRIPTION("LM3697 DISPLAY BACKLIGHT DRIVER");
MODULE_AUTHOR("Alina Yakovleva, Motorola, qvdh43@motorola.com");
MODULE_LICENSE("GPL v2");
