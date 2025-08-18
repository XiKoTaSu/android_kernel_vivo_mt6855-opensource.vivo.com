// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 vivo Inc.

/* registers definitions */
#define REG_ENABLE		0x01
#define REG_LED0_FLASH_BR	0x03
#define REG_LED1_FLASH_BR	0x04
#define REG_LED0_TORCH_BR	0x05
#define REG_LED1_TORCH_BR	0x06
#define REG_FLASH_TOUT		0x08
#define REG_FLAG1		0x0A
#define REG_FLAG2		0x0B

struct flash_status {
bool status_led1;
bool status_led2;
};
int flash_check_status(struct regmap *regmap, struct flash_status * flash_status);
