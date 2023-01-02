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
#ifndef _BTMTK_PROJ_SP_H_
#define _BTMTK_PROJ_SP_H_

#if (USE_DEVICE_NODE == 1)
#include "conn_power_throttling.h"

#define HCI_EVT_COMPLETE_EVT		0x0E
#define HCI_EVT_STATUS_EVT			0x0F
#define HCI_EVT_CC_STATUS_SUCCESS	0x00
#define HCI_CMD_DY_ADJ_PWR_QUERY	0x01
#define HCI_CMD_DY_ADJ_PWR_SET		0x02

typedef int (*BT_RX_EVT_HANDLER_CB) (uint8_t *buf, int len);

struct btmtk_dypwr_st {
	/* Power Throttling Feature */
	uint8_t buf[16];
	uint8_t len;
	int8_t set_val;
	int8_t dy_max_dbm;
	int8_t dy_min_dbm;
	int8_t lp_bdy_dbm;
	int8_t fw_sel_dbm;
	BT_RX_EVT_HANDLER_CB cb;
	enum conn_pwr_low_battery_level lp_cur_lv;
};

void btmtk_async_trx_work(struct work_struct *work);
int btmtk_pwrctrl_pre_on(struct btmtk_dev *bdev);
void btmtk_pwrctrl_post_off(void);
void btmtk_pwrctrl_register_evt(void);
int btmtk_query_tx_power(struct btmtk_dev *bdev, BT_RX_EVT_HANDLER_CB cb);
int btmtk_set_tx_power(struct btmtk_dev *bdev, int8_t req_val, BT_RX_EVT_HANDLER_CB cb);


int btmtk_read_pmic_state(struct btmtk_dev *bdev);

int btmtk_set_pcm_pin_mux(void);

int btmtk_set_gpio_default(void);
int btmtk_pre_power_on_handler(void);
int btmtk_set_uart_auxFunc(void);

int btmtk_connv3_sub_drv_init(struct btmtk_dev *bdev);
//int btmtk_connv3_sub_drv_init(struct platform_device *pdev);

int btmtk_connv3_sub_drv_deinit(void);

/* Debug sop api */
void btmtk_uart_sp_dump_debug_sop(struct btmtk_dev *bdev);
#endif // (USE_DEVICE_NODE == 1)
#endif
