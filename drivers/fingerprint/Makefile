# Each configuration option enables a list of files.
ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc
#include $(srctree)/drivers/misc/mediatek/gud/Makefile.include


obj-$(CONFIG_BBK_FP_ID)		+= fp_id.o
obj-$(CONFIG_BBK_FP_ID)		+= vfp_spi.o
obj-$(CONFIG_BBK_FPC1552)	+= fpc1552/
obj-$(CONFIG_BBK_GF3956)	+= gf3956/
obj-$(CONFIG_BBK_ICNF7312)	+= icnf7312/
obj-$(CONFIG_BBK_GF9578)	+= gf9578/