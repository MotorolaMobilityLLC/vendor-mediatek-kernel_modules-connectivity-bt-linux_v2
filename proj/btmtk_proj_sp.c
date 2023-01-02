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
#include "connfem.h"
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
int btmtk_pre_power_on_handler(void)
{
	/*
	 * Set BT_RST PU/OUPUT
	 * Setup BT UART
	 */
	int ret = 0;

	btmtk_pinctrl_exec(RST_ON_PINCTRL_NAME);
	ret = btmtk_pinctrl_exec(PRE_ON_PINCTRL_NAME);
	msleep(100);
	BTMTK_DBG("%s: wait 100ms", __func__);
	return ret;
}

int btmtk_reset_pin_off(void)
{
	BTMTK_DBG("%s: start", __func__);
	return btmtk_pinctrl_exec(RST_OFF_PINCTRL_NAME);
}

int btmtk_set_uart_auxFunc(void)
{
	BTMTK_DBG("%s: start", __func__);

	return btmtk_pinctrl_exec(POWER_ON_PINCTRL_NAME);
	//return btmtk_read_pmic_state(NULL);
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
			read_pmic_state_event, READ_PMIC_STATE_EVENT_LEN, 0, 0, BTMTK_TX_CMD_FROM_DRV);
	if (ret < 0)
		BTMTK_ERR("%s: failed(%d)", __func__, ret);

	return ret;
}

int btmtk_send_connfem_cmd(struct btmtk_dev *bdev)
{
	struct connfem_epaelna_fem_info fem_info;
	struct connfem_epaelna_flags_common common_flag;
	struct connfem_epaelna_pin_info pin_info;
	struct connfem_epaelna_flags_bt bt_flag;
	uint32_t ret = 0;
	uint8_t *cmd = NULL;
	uint8_t cmd_header[] = {0x01, 0x6F, 0xFC, 0x42, 0x01, 0x55, 0x3E, 0x00,
			0x01, 0x04, 0x03, 0x33, 0x00, 0x10};
	uint8_t event[] = {0x04, 0xE4, 0x06, 0x02, 0x55, 0x02, 0x00, 0x00, 0x01};
	uint32_t cmd_len = 0, i = 0, offset = 0;
	const uint32_t pin_struct_size = sizeof(struct connfem_epaelna_pin);

	BTMTK_INFO("%s", __func__);

	/* Get data from connfem_api */
	connfem_epaelna_get_fem_info(&fem_info);
	connfem_epaelna_get_pin_info(&pin_info);
	connfem_epaelna_get_flags(CONNFEM_SUBSYS_NONE, &common_flag);
	connfem_epaelna_get_flags(CONNFEM_SUBSYS_BT, &bt_flag);

	if (fem_info.part[CONNFEM_PORT_BT].vid == 0 &&
	    fem_info.part[CONNFEM_PORT_BT].pid == 0) {
		BTMTK_INFO("CONNFEM BTvid/pid == 0, ignore");
		return 0;
	}

	/*
	 * command and event example
	 *  0  1  2      3  4  5  6  7  8  9  A  B  C  D
	 * 01 6F FC length 01 55 LL LL 01 XX XX XX XX NN YYYYYY ..  YYYYYY AA BB BB CC DD DD DD DD
	 * lengthL : LL + 4
	 * LLLL : length = 1 + 4 + 1 + 3*num + 3 (only 1 byte length valid,
	 *					  value 251 should be maxium)
	 * XXXXXXXX : 4 byte,  efem ID
	 * NN : 1 byte, total efem number
	 * YYYYYY: 3 byte * number, u1AntSelNo,    u1FemPin,     u1Polarity;
	 * AA : bt flag
	 * BBBB : 2.4G part = VID + PID
	 * CC : 1 byte Rx Mode info
	 * DDDDDDDD: 4 bytes SPDT info
	 *
	 * RX: 04 E4 06 02 55 02 00 01 SS (SS : status)
	 */
	cmd_len = sizeof(cmd_header) + pin_info.count * pin_struct_size + 8;
	cmd = vmalloc(cmd_len);
	if (!cmd) {
		BTMTK_ERR("unable to allocate confem command");
		return -1;
	}

	memcpy(cmd, cmd_header, sizeof(cmd_header));

	/* assign WMT over HCI command length */
	cmd[3] = cmd_len - 4;

	/* assign payload length */
	cmd[6] = cmd_len - 8;

	/* assign femid */
	memcpy(&cmd[9], &fem_info.id, sizeof(fem_info.id));
	offset = sizeof(cmd_header);

	/* assign pin count */
	cmd[offset-1] = pin_info.count;

	/* assign pin mapping info */
	for (i = 0; i < pin_info.count; i++) {
		memcpy(&cmd[offset], &pin_info.pin[i], pin_struct_size);
		offset += pin_struct_size;
	}

	/* config priority: epa_elna > elna > epa > bypass */
	cmd[offset++] = (bt_flag.epa_elna) ? 3 :
			(bt_flag.epa) ? 2 :
			(bt_flag.elna) ? 1 : 0;

	cmd[offset++] = fem_info.part[CONNFEM_PORT_BT].vid;
	cmd[offset++] = fem_info.part[CONNFEM_PORT_BT].pid;

	cmd[offset++] = common_flag.rxmode;
	cmd[offset++] = common_flag.fe_ant_cnt;
	cmd[offset++] = common_flag.fe_main_bt_share_lp2g;
	cmd[offset++] = common_flag.fe_conn_spdt;
	cmd[offset++] = common_flag.fe_reserved;

	BTMTK_INFO_RAW(cmd, offset, "%s: Send: ", __func__);

	ret = btmtk_main_send_cmd(bdev, cmd, cmd_len,
			event, sizeof(event), 0, 0, BTMTK_TX_CMD_FROM_DRV);

	if (ret < 0)
		BTMTK_ERR("%s: failed(%d)", __func__, ret);

	vfree(cmd);
	return 0;
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
	int ret;

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

	ret = of_property_read_u32(tty->dev->of_node,"baudrate", &cif_dev->baudrate);
	if(ret < 0)
		BTMTK_ERR("[ERR] %s: mediatek,bt baudrate ret[%d]", __func__, ret);

	ret = of_property_read_u32(tty->dev->of_node,"hub-en", &cif_dev->hub_en);
	if(ret < 0)
		BTMTK_ERR("[ERR] %s: mediatek,bt hub-en ret[%d]", __func__, ret);

	ret = of_property_read_u32(tty->dev->of_node, "sleep-en", &cif_dev->sleep_en);
	if(ret < 0)
		BTMTK_ERR("[ERR] %s: mediatek,bt sleep-en ret[%d]", __func__, ret);

	/* temp: for disable sleep */
	cif_dev->sleep_en = 0;

	pinctrl_ptr = devm_pinctrl_get(tty->dev);
	if (IS_ERR(pinctrl_ptr)) {
		BTMTK_ERR("[ERR] %s: fail to get bt pinctrl", __func__);
		return -1;
	}
	//btmtk_pinctrl_exec(INIT_STATE_PINCTRL_NAME);
	connv3_sub_drv_ops_register(CONNV3_DRV_TYPE_BT, &btmtk_drv_cbs);
	BTMTK_INFO("%s end, baudrate[%d] hub_en[%d] sleep_en[%d]", __func__, cif_dev->baudrate, cif_dev->hub_en, cif_dev->sleep_en);
	return 0;
}


int btmtk_connv3_sub_drv_deinit(void)
{
	return connv3_sub_drv_ops_unregister(CONNV3_DRV_TYPE_BT);
}
#endif // (USE_DEVICE_NODE == 1)

