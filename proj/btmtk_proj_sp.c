/**
 *  Copyright (c) 2018 MediaTek Inc.
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
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include "btmtk_main.h"
#include "connv3.h"
#include "conninfra.h"
#include "btmtk_proj_sp.h"
#include "btmtk_uart_tty.h"
#include <linux/platform_device.h>


#define READ_PMIC_STATE_CMD_LEN		16
#define READ_PMIC_STATE_EVENT_LEN	16

#define PRE_ON_PINCTRL_NAME		("bt_combo_gpio_pre_on")
#define POWER_ON_PINCTRL_NAME		("bt_combo_gpio_on")
#define RST_ON_PINCTRL_NAME		("bt_rst_on")
#define RST_OFF_PINCTRL_NAME		("bt_rst_off")
#define INIT_STATE_PINCTRL_NAME		("bt_combo_gpio_init")


#if (USE_DEVICE_NODE == 1)
static struct pinctrl *pinctrl_ptr;

static inline int btmtk_pinctrl_exec(const char *name)
{
	struct pinctrl_state *pinctrl;
	int ret = -1;

	BTMTK_INFO("%s start %s", __func__, name);
	if (IS_ERR(pinctrl_ptr)) {
		BTMTK_ERR("[ERR] %s: fail to get bt pinctrl", __func__);
		return -1;
	}

	pinctrl = pinctrl_lookup_state(pinctrl_ptr, name);
	if (!IS_ERR(pinctrl)) {
		ret = pinctrl_select_state(pinctrl_ptr, pinctrl);
		if (ret) {
			BTMTK_ERR("%s: pinctrl %s fail [%d]", __func__, name, ret);
			return -1;
		}
	} else {
		BTMTK_ERR("%s: pinctrl %s lookup fail", __func__, name);
		return -1;
	}

	return 0;
}
static int btmtk_pre_power_on_handler(void)
{
	/*
	 * Set BT_RST PU/OUPUT
	 * Setup BT UART
	 */
	btmtk_pinctrl_exec(RST_ON_PINCTRL_NAME);
	return btmtk_pinctrl_exec(PRE_ON_PINCTRL_NAME);
}

int btmtk_reset_pin_off(void)
{
	return btmtk_pinctrl_exec(RST_OFF_PINCTRL_NAME);
}


int btmtk_post_power_on_handler(void)
{
	/* Set PCM pin function */
	if (!btmtk_pinctrl_exec(POWER_ON_PINCTRL_NAME))
		return -1;

	return btmtk_read_pmic_state(NULL);
}

static int btmtk_power_on_notify_handler(void)
{
	/* Execute BT power on then power off (if BT is off before this callback */
	BTMTK_INFO("%s", __func__);
	return 0;
}

static int btmtk_pre_chip_rst_handler(enum connv3_drv_type drv, char *reason)
{
	/* Ask FW to do coredump */
	BTMTK_INFO("%s", __func__);
	return 0;
}

static int btmtk_post_chip_rst_handler(void)
{
	BTMTK_INFO("%s", __func__);
	btmtk_send_hw_err_to_host(NULL);
	return 0;
}

struct connv3_whole_chip_rst_cb btmtk_whole_chip_rst_cb = {
	.pre_whole_chip_rst = btmtk_pre_chip_rst_handler,
	.post_whole_chip_rst = btmtk_post_chip_rst_handler,
};

struct connv3_power_on_cb btmtk_pwr_on_cb = {
	.pre_power_on = btmtk_pre_power_on_handler,
	.power_on_notify = btmtk_power_on_notify_handler,
};

/* connv3_sub_drv_ops_cb
 *
 *    All callbacks needs by conninfra driver, 3 types of callback functions
 *    1. power on
 *    2. chip reset
 *    3. pre-calibration
 */
#if 0 // can not build
struct connv3_sub_drv_ops_cb btmtk_drv_cbs = {
	.pwr_on_cb = btmtk_pwr_on_cb,
	.rst_cb = btmtk_whole_chip_rst_cb,
	//.pre_cal_cb = NULL,
};
#endif

int btmtk_read_pmic_state(struct btmtk_dev *bdev)
{
	int ret = 0;
	u8 read_pmic_state_cmd[READ_PMIC_STATE_CMD_LEN] = { 0x00 };
	u8 read_pmic_state_event[READ_PMIC_STATE_EVENT_LEN] = { 0x00 };


	BTMTK_INFO("%s enter", __func__);
	ret = btmtk_main_send_cmd(bdev, read_pmic_state_cmd, READ_PMIC_STATE_CMD_LEN,
			read_pmic_state_event, READ_PMIC_STATE_EVENT_LEN, 0, 0, BTMTK_TX_PKT_FROM_HOST);
	if (ret < 0)
		BTMTK_ERR("%s: failed(%d)", __func__, ret);

	return ret;
}

int btmtk_set_pcm_pin_mux(void)
{
	return 0;
}

int btmtk_connv3_sub_drv_init(struct btmtk_dev *bdev)
{
	struct btmtk_uart_dev *cif_dev = NULL;
	struct tty_struct *tty = NULL;
	struct connv3_sub_drv_ops_cb btmtk_drv_cbs;

	btmtk_drv_cbs.pwr_on_cb = btmtk_pwr_on_cb;
	btmtk_drv_cbs.rst_cb = btmtk_whole_chip_rst_cb;
	BTMTK_INFO("%s start", __func__);
	if (!bdev) {
		BTMTK_ERR("[ERR] bdev is NULL");
		return -1;
	}

	cif_dev = (struct btmtk_uart_dev *)bdev->cif_dev;
	if (!cif_dev) {
		BTMTK_ERR("[ERR] cif_dev is NULL");
		return -1;
	}

	tty = cif_dev->tty;
	if (!tty) {
		BTMTK_ERR("[ERR] tty is NULL");
		return -1;
	}

	tty->dev->of_node = of_find_compatible_node(NULL, NULL, "mediatek,bt");
	if (!tty->dev->of_node)
		BTMTK_ERR("[ERR] %s: mediatek,bt of_node not found", __func__);

	pinctrl_ptr = devm_pinctrl_get(tty->dev);
	if (IS_ERR(pinctrl_ptr)) {
		BTMTK_ERR("[ERR] %s: fail to get bt pinctrl", __func__);
		return -1;
	}
	//btmtk_pinctrl_exec(INIT_STATE_PINCTRL_NAME);
	connv3_sub_drv_ops_register(CONNV3_DRV_TYPE_BT, &btmtk_drv_cbs);
	BTMTK_INFO("%s end", __func__);
	return 0;
}


int btmtk_connv3_sub_drv_deinit(void)
{
	return connv3_sub_drv_ops_unregister(CONNV3_DRV_TYPE_BT);
}
#endif // (USE_DEVICE_NODE == 1)

