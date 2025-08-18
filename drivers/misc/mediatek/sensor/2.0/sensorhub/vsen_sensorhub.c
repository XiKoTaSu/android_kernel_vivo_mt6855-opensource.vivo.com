/*
 * Copyright (C) 2021 VIVO SENSOR TEAM
 *
 */

#include <linux/kobject.h>
#include <linux/miscdevice.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "../core/hf_sensor_type.h"
#include "../core/hf_sensor_io.h"
#include "sensor_comm.h"
#include "custom_cmd.h"

#define LOG_TAG                     "[vsen_sensorhub] "
#define VSEN_ERR(fmt, args...)      pr_err(LOG_TAG fmt, ##args)
#define VSEN_INFO(fmt, args...)     pr_info(LOG_TAG fmt, ##args)

struct vsen_packet {
    uint8_t sensor_type;
    uint8_t padding[3];
    struct custom_cmd cmd;
};

#define VSEN_SENSOR_HUB             0x80
#define VSEN_SENSOR_HUB_CMD         _IOW(VSEN_SENSOR_HUB, 0x01, struct vsen_packet)

static DEFINE_MUTEX(ioctrl_mutex);

struct vsen_sensorhub_data {
	bool log_print;
};

static struct vsen_sensorhub_data *local_sensorhub_data;
static struct class *vsen_sensorhub_class;

static struct attribute *vsen_sensorhub_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(vsen_sensorhub);

ssize_t vsen_sensorhub_log_show(struct class *class, struct class_attribute *attr, char *buf)
{
	struct vsen_sensorhub_data *sensorhub_data = local_sensorhub_data;
	int log_debug = 0;

	if ((sensorhub_data != NULL) && (sensorhub_data->log_print))
		log_debug = 1;

	return snprintf(buf, 20, "%d\n", log_debug);
}

ssize_t vsen_sensorhub_log_store(struct class *class, struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct vsen_sensorhub_data *sensorhub_data = local_sensorhub_data;
	int log_debug = 0, err;

	if (sensorhub_data == NULL)
		return count;

	err = kstrtoint(buf, 10, &log_debug);
	if (err)
		VSEN_ERR("fail to get log level (%d)\n", err);

	if (log_debug != 0)
		sensorhub_data->log_print = true;
	else
		sensorhub_data->log_print = false;

	VSEN_INFO("debug level %d\n", sensorhub_data->log_print);
	return count;
}

static CLASS_ATTR_RW(vsen_sensorhub_log);

static int create_vsen_sensorhub_class(void)
{
	int ret = 0;

	if (!vsen_sensorhub_class) {
		vsen_sensorhub_class = class_create(THIS_MODULE, "vsen_sensorhub");
		if (IS_ERR(vsen_sensorhub_class))
			return PTR_ERR(vsen_sensorhub_class);
		vsen_sensorhub_class->dev_groups = vsen_sensorhub_groups;
		ret = class_create_file(vsen_sensorhub_class, &class_attr_vsen_sensorhub_log);
		if (ret) {
			VSEN_ERR("create class file failed, ret = %d\n", ret);
			return ret;
		}
	}
	return 0;
}

static void remove_vsen_sensorhub_class(void)
{
	class_remove_file(vsen_sensorhub_class, &class_attr_vsen_sensorhub_log);
}

static int vsen_sensorhub_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static ssize_t vsen_sensorhub_write(struct file *file, const char *buffer,
				size_t length, loff_t *offset)
{
	return 0;
}

static int vsen_sensorhub_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static long vsen_sensorhub_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vsen_sensorhub_data *sensorhub_data = local_sensorhub_data;
	void __user *ubuf = (void __user *)arg;
	int ret = 0;
	uint8_t sensor_type = 0;
	struct vsen_packet packet;
	struct custom_cmd *cust_cmd = NULL;

	if (sensorhub_data == NULL)
		return -EFAULT;

	memset(&packet, 0, sizeof(packet));
	mutex_lock(&ioctrl_mutex);
	switch (cmd) {
		case VSEN_SENSOR_HUB_CMD:
			ret = copy_from_user(&packet, ubuf, sizeof(packet));
			if (ret != 0) {
				ret = -EFAULT;
				goto exit_hub_ioctrl;
			}

			sensor_type = packet.sensor_type;
			if (unlikely(sensor_type >= SENSOR_TYPE_SENSOR_MAX)) {
				ret = -EINVAL;
				goto exit_hub_ioctrl;
			}

			cust_cmd = &packet.cmd;
			ret = custom_cmd_comm_with(sensor_type, cust_cmd);
			if (ret != 0) {
				ret =  -EINVAL;
				goto exit_hub_ioctrl;
			}

			ret = copy_to_user(ubuf, &packet, sizeof(packet));
			if (ret != 0) {
				ret = -EFAULT;
				goto exit_hub_ioctrl;
			}

			VSEN_INFO("sensor %d, cmd %d\n", sensor_type, cust_cmd->command);
			break;

		default:
			VSEN_ERR("unknown cmd: 0x%08x\n", cmd);
			ret = -ENOIOCTLCMD;
			break;
	}

exit_hub_ioctrl:
	mutex_unlock(&ioctrl_mutex);
	return ret;
}

static long vsen_sensorhub_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static const struct file_operations _vsen_sensorhub_fops = {
	.open = vsen_sensorhub_open,
	.write = vsen_sensorhub_write,
	.release = vsen_sensorhub_release,
	.unlocked_ioctl = vsen_sensorhub_unlocked_ioctl,
	.compat_ioctl = vsen_sensorhub_compat_ioctl,
};

static struct miscdevice vsen_sensorhub_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "vsen_sensorhub",
	.fops = &_vsen_sensorhub_fops,
};

static int __init vsen_sensorhub_init(void)
{
	int ret = 0;
	struct vsen_sensorhub_data *sensorhub_data = NULL;

	sensorhub_data = kzalloc(sizeof(struct vsen_sensorhub_data), GFP_KERNEL);
	if (!sensorhub_data) {
		VSEN_ERR("fail to alloc private data\n");
		return -ENOMEM;
	}
	/* Init private data */
	local_sensorhub_data = sensorhub_data;

	ret = misc_register(&vsen_sensorhub_device);
	if (ret) {
		kfree(sensorhub_data);
		VSEN_ERR("register misc failed\n");
		ret = -1;
	}
	VSEN_INFO("%s ret %d\n", __func__, ret);

	ret = create_vsen_sensorhub_class();
	VSEN_INFO("%s create class ret = %d\n", __func__, ret);

	return ret;
}

static void __exit vsen_sensorhub_cleanup(void)
{
	kfree(local_sensorhub_data);
	local_sensorhub_data = NULL;
	misc_deregister(&vsen_sensorhub_device);
	remove_vsen_sensorhub_class();
}

module_init(vsen_sensorhub_init);
module_exit(vsen_sensorhub_cleanup);

MODULE_AUTHOR("vsen team@vivo.com");
MODULE_LICENSE("GPL");
