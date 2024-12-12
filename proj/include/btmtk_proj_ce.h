/*
 *  Copyright (c) 2016,2017 MediaTek Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
#ifndef _BTMTK_PROJ_CE_H_
#define _BTMTK_PROJ_CE_H_

#include "btmtk_uart_tty.h"

#define INIT_STATE_PINCTRL_NAME	("bt_ce_gpio_init")
#define PRE_ON_PINCTRL_NAME		("bt_ce_gpio_pre_on")
#define POWER_ON_TX_PINCTRL_NAME	("bt_ce_uart_tx_aux")
#define POWER_ON_RX_PINCTRL_NAME	("bt_ce_uart_rx_aux")
#define RST_ON_PINCTRL_NAME		("bt_rst_on")
#define RST_OFF_PINCTRL_NAME		("bt_rst_off")
#define FALCON_PMIC_DEFAULT_PINCTRL_NAME ("falcon_pmic_en_default")
#define FALCON_PMIC_SET_PINCTRL_NAME ("falcon_pmic_en_set")

#define BT_PMIC_EN_REGULATOR "bt1v8"

#if (USE_DEVICE_NODE == 0)
int btmtk_set_gpio_default(void);
int btmtk_pre_power_on_handler(struct btmtk_uart_dev *cif_dev);
int btmtk_ce_gpio_init(struct btmtk_uart_dev *cif_dev);
#endif // (USE_DEVICE_NODE == 0)
#endif

