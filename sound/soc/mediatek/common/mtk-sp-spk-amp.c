// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 MediaTek Inc.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
/* alsa sound header */
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "mtk-sp-spk-amp.h"
#if IS_ENABLED(CONFIG_SND_SOC_RT5509)
#include "../../codecs/rt5509.h"
#endif

#if IS_ENABLED(CONFIG_SND_SOC_RT5512)
#include "../../codecs/richtek/rt5512.h"
#endif

#if IS_ENABLED(CONFIG_SND_SOC_TFA98XX)
#include "../../codecs/tfa98xx/inc/tfa98xx_ext.h"
#endif

#if IS_ENABLED(CONFIG_SND_SOC_AW87339)
#include "aw87339.h"
#endif

#define MTK_SPK_NAME "Speaker Codec"
#define MTK_SPK_REF_NAME "Speaker Codec Ref"

static unsigned int mtk_spk_type;
static int mtk_spk_i2s_out = MTK_SPK_I2S_3, mtk_spk_i2s_in = MTK_SPK_I2S_0;
static struct mtk_spk_i2c_ctrl mtk_spk_list[MTK_SPK_TYPE_NUM] = {
	[MTK_SPK_NOT_SMARTPA] = {
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},

#if IS_ENABLED(CONFIG_SND_SOC_RT5509)
	[MTK_SPK_RICHTEK_RT5509] = {
		.i2c_probe = rt5509_i2c_probe,
		.i2c_remove = rt5509_i2c_remove,
		.i2c_shutdown = rt5509_i2c_shutdown,
		.codec_dai_name = "rt5509-aif1",
		.codec_name = "RT5509_MT_0",
	},
#endif

#if IS_ENABLED(CONFIG_SND_SOC_RT5512)
	[MTK_SPK_MEDIATEK_RT5512] = {
		.codec_dai_name = "rt5512-aif",
		.codec_name = "RT5512_MT_0",
	},
#endif /* CONFIG_SND_SOC_RT5512 */

#if IS_ENABLED(CONFIG_SND_SOC_TFA98XX)
	[MTK_SPK_GOODIX_TFA98XX] = {
		.codec_dai_name = "tfa98xx-aif",
		.codec_name = "tfa98xx",
	},
#endif /* CONFIG_SND_SOC_TFA98XX */

#if IS_ENABLED(CONFIG_SND_SOC_AW882XX)
	[MTK_SPK_MEDIATEK_AW882XX] = {
		.codec_dai_name = "aw882xx-aif",
		.codec_name = "aw882xx",
	},
#endif /* CONFIG_SND_SOC_AW882XX */

#if IS_ENABLED(CONFIG_SND_SOC_TFA9874)
	[MTK_SPK_NXP_TFA9874] = {
		.codec_dai_name = "tfa98xx-aif",
		.codec_name = "tfa98xx",
	},
#endif /* CONFIG_SND_SOC_TFA9874 */
#if IS_ENABLED(CONFIG_SND_SMARTPA_AW881XX)
	[MTK_SPK_AW_AWINIC_AW88xxx] = {
		.codec_dai_name = "aw881xx-aif",
		.codec_name = "aw881xx",
	},
#endif
/*add by yanghen for tfa9894*/
#if IS_ENABLED(CONFIG_SND_SOC_TFA9894)
	[MTK_SPK_NXP_TFA9894] = {
		.codec_dai_name = "SmartPA",
		.codec_name = "tfa98xx",
	},
#endif
};

void dump_mtk_spk_list(void)
{
	int i;
	for(i = 0; i < MTK_SPK_TYPE_NUM; i++) {
		if (mtk_spk_list[i].codec_dai_name) {
			printk("%s, mtk_spk_list[%d].codec_dai_name is (%s)\n",
				__func__, i, mtk_spk_list[i].codec_dai_name);
		}

		if (mtk_spk_list[i].codec_name) {
			printk("%s, mtk_spk_list[%d].codec_name is (%s)\n",
				__func__, i, mtk_spk_list[i].codec_name);
		}
	}
}

static int mtk_spk_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int i, ret = 0;

	dev_info(&client->dev, "%s()\n", __func__);

	mtk_spk_type = MTK_SPK_NOT_SMARTPA;
	for (i = 0; i < MTK_SPK_TYPE_NUM; i++) {
		if (mtk_spk_list[i].i2c_probe) {
			ret = mtk_spk_list[i].i2c_probe(client, id);
			if (ret)
				continue;
			mtk_spk_type = i;
			break;
		} else if (mtk_spk_list[i].codec_name && strcmp(mtk_spk_list[i].codec_name, "snd-soc-dummy") && \
			mtk_spk_list[i].codec_dai_name && strcmp(mtk_spk_list[i].codec_dai_name, "snd-soc-dummy-dai")) {
			mtk_spk_type = i;
			break;
		}
	}
	return ret;
}

static int mtk_spk_i2c_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "%s()\n", __func__);

	if (mtk_spk_list[mtk_spk_type].i2c_remove)
		mtk_spk_list[mtk_spk_type].i2c_remove(client);

	return 0;
}

static void mtk_spk_i2c_shutdown(struct i2c_client *client)
{
	dev_info(&client->dev, "%s()\n", __func__);

	if (mtk_spk_list[mtk_spk_type].i2c_shutdown)
		mtk_spk_list[mtk_spk_type].i2c_shutdown(client);
}

int mtk_spk_get_type(void)
{
	return mtk_spk_type;
}
EXPORT_SYMBOL(mtk_spk_get_type);

void mtk_spk_set_type(int spk_type)
{
	mtk_spk_type = spk_type;
}
EXPORT_SYMBOL(mtk_spk_set_type);

int mtk_spk_get_i2s_out_type(void)
{
	return mtk_spk_i2s_out;
}
EXPORT_SYMBOL(mtk_spk_get_i2s_out_type);

int mtk_spk_get_i2s_in_type(void)
{
	return mtk_spk_i2s_in;
}
EXPORT_SYMBOL(mtk_spk_get_i2s_in_type);

int mtk_ext_spk_get_status(void)
{
#ifdef CONFIG_SND_SOC_AW87339
	return aw87339_spk_status_get();
#else
	return 0;
#endif
}
EXPORT_SYMBOL(mtk_ext_spk_get_status);

void mtk_ext_spk_enable(int enable)
{
#ifdef CONFIG_SND_SOC_AW87339
	aw87339_spk_enable_set(enable);
#endif
}
EXPORT_SYMBOL(mtk_ext_spk_enable);

int mtk_spk_update_info(struct snd_soc_card *card,
			struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai_link;
	int ret, i;
	int i2s_out_dai_link_idx = -1;
	int i2s_in_dai_link_idx = -1;
	const int i2s_num = 2;
	unsigned int i2s_set[2];

	printk("%s, ===> mtk_spk_type is %d\n", __func__, mtk_spk_type);
	if (mtk_spk_type == MTK_SPK_NOT_SMARTPA) {
		goto BYPASS_UPDATE;
	}

	/* get spk i2s set */
	ret = of_property_read_u32_array(pdev->dev.of_node, "mediatek,spk-i2s",
					 i2s_set, i2s_num);
	if (ret) {
		dev_info(&pdev->dev,
			 "%s(), fail read mediatek,spk-i2s, use defalut i2s3/i2s0\n",
			 __func__);
		mtk_spk_i2s_out = MTK_SPK_I2S_3;
		mtk_spk_i2s_in = MTK_SPK_I2S_0;
	} else {
		mtk_spk_i2s_out = i2s_set[0];
		mtk_spk_i2s_in = i2s_set[1];
	}
	printk("%s, ===> mtk_spk_i2s_out is %d, mtk_spk_i2s_in is %d\n",
		__func__, mtk_spk_i2s_out, mtk_spk_i2s_in);

	if (mtk_spk_i2s_out > MTK_SPK_I2S_TYPE_NUM ||
	    mtk_spk_i2s_in > MTK_SPK_I2S_TYPE_NUM) {
		dev_err(&pdev->dev, "%s(), get mtk spk i2s fail\n",
			__func__);
		return -ENODEV;
	}

	/* find dai link of i2s in and i2s out */
	for_each_card_prelinks(card, i, dai_link) {
		if (i2s_out_dai_link_idx < 0 &&
		    strcmp(dai_link->cpus->dai_name, "I2S1") == 0 &&
		    mtk_spk_i2s_out == MTK_SPK_I2S_1) {
			i2s_out_dai_link_idx = i;
			dai_link->name = MTK_SPK_NAME;
			dai_link->codecs = kzalloc(sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
			dai_link->codecs->name = mtk_spk_list[mtk_spk_type].codec_name;
			dai_link->codecs->dai_name = mtk_spk_list[mtk_spk_type].codec_dai_name;
		} else if (i2s_out_dai_link_idx < 0 &&
			   strcmp(dai_link->cpus->dai_name, "I2S3") == 0 &&
			   mtk_spk_i2s_out == MTK_SPK_I2S_3) {
			i2s_out_dai_link_idx = i;
			dai_link->name = MTK_SPK_NAME;
			dai_link->codecs = kzalloc(sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
			dai_link->codecs->name = mtk_spk_list[mtk_spk_type].codec_name;
			dai_link->codecs->dai_name = mtk_spk_list[mtk_spk_type].codec_dai_name;
		} else if (i2s_out_dai_link_idx < 0 &&
			   strcmp(dai_link->cpus->dai_name, "I2S5") == 0 &&
			   mtk_spk_i2s_out == MTK_SPK_I2S_5) {
			i2s_out_dai_link_idx = i;
			dai_link->name = MTK_SPK_NAME;
			dai_link->codecs = kzalloc(sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
			dai_link->codecs->name = mtk_spk_list[mtk_spk_type].codec_name;
			dai_link->codecs->dai_name = mtk_spk_list[mtk_spk_type].codec_dai_name;
		}

		if (i2s_in_dai_link_idx < 0 &&
		    strcmp(dai_link->cpus->dai_name, "I2S0") == 0 &&
		    (mtk_spk_i2s_in == MTK_SPK_I2S_0 ||
		     mtk_spk_i2s_in == MTK_SPK_TINYCONN_I2S_0)) {
			i2s_in_dai_link_idx = i;
			dai_link->name = MTK_SPK_REF_NAME;
			dai_link->codecs = kzalloc(sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
			dai_link->codecs->name =  mtk_spk_list[mtk_spk_type].codec_name;
			dai_link->codecs->dai_name = mtk_spk_list[mtk_spk_type].codec_dai_name;
		} else if (i2s_in_dai_link_idx < 0 &&
			   strcmp(dai_link->cpus->dai_name, "I2S2") == 0 &&
			   (mtk_spk_i2s_in == MTK_SPK_I2S_2 ||
			    mtk_spk_i2s_in == MTK_SPK_TINYCONN_I2S_2)) {
			i2s_in_dai_link_idx = i;
			dai_link->name = MTK_SPK_REF_NAME;
			dai_link->codecs = kzalloc(sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
			dai_link->codecs->name = mtk_spk_list[mtk_spk_type].codec_name;
			dai_link->codecs->dai_name = mtk_spk_list[mtk_spk_type].codec_dai_name;
		}

		if (i2s_out_dai_link_idx >= 0 && i2s_in_dai_link_idx >= 0) {
			printk("%s, i2s_out_dai_link_idx used for (%s), i2s_in_dai_link_idx used for (%s)\n",
				__func__, MTK_SPK_NAME, MTK_SPK_REF_NAME);
			break;
		}
	}

	if (i2s_out_dai_link_idx < 0 || i2s_in_dai_link_idx < 0) {
		dev_err(&pdev->dev,
			"%s(), i2s cpu dai name error, i2s_out_dai_link_idx = %d, i2s_in_dai_link_idx = %d",
			__func__, i2s_out_dai_link_idx, i2s_in_dai_link_idx);
		return -ENODEV;
	}

	printk("%s, ===> i2s_out_dai_link_idx is %d, i2s_in_dai_link_idx is %d\n",
		__func__, i2s_out_dai_link_idx, i2s_in_dai_link_idx);

	printk("%s, ===>  dai_link->codecs->name is (%s), dai_link->codecs->dai_name is (%s)\n",
		__func__, dai_link->codecs->name, dai_link->codecs->dai_name);

	printk("%s, ===> dump dump_mtk_spk_list\n", __func__);
	dump_mtk_spk_list();

BYPASS_UPDATE:
	return 0;
}
EXPORT_SYMBOL(mtk_spk_update_info);

static const struct i2c_device_id mtk_spk_i2c_id[] = {
	{ "speaker_amp", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mtk_spk_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id mtk_spk_match_table[] = {
	{.compatible = "mediatek,speaker_amp",},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_spk_match_table);
#endif /* #ifdef CONFIG_OF */

static struct i2c_driver mtk_spk_i2c_driver = {
	.driver = {
		.name = "speaker_amp",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(mtk_spk_match_table),
	},
	.probe = mtk_spk_i2c_probe,
	.remove = mtk_spk_i2c_remove,
	.shutdown = mtk_spk_i2c_shutdown,
	.id_table = mtk_spk_i2c_id,
};

module_i2c_driver(mtk_spk_i2c_driver);

MODULE_DESCRIPTION("Mediatek speaker amp register driver");
MODULE_AUTHOR("Shane Chien <shane.chien@mediatek.com>");
MODULE_LICENSE("GPL v2");
