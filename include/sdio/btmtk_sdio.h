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

#ifndef _BTMTK_SDIO_H_
#define _BTMTK_SDIO_H_
/* It's for reset procedure */
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>

#include <linux/of_gpio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#include "btmtk_define.h"

/**
 * Card-relate definition.
 */
#define SDIO_VENDOR_ID_MEDIATEK 0x037A

#define HCI_HEADER_LEN	4

#define MTK_STP_TLR_SIZE	2
#define STP_HEADER_LEN	4
#define STP_HEADER_CRC_LEN	2
#define HCI_MAX_COMMAND_SIZE	255
#define URB_MAX_BUFFER_SIZE	(4*1024)
#define BTMTK_SDIO_FUNC 2


/* common register address */
#define CCIR		0x0000
#define CHLPCR		0x0004
#define CSDIOCSR	0x0008
#define CHCR		0x000C
#define CHISR		0x0010
#define CHIER		0x0014
#define CTDR		0x0018
#define CRDR		0x001C
#define CTFSR		0x0020
#define CRPLR		0x0024
#define SWPCDBGR	0x0154
/* CHLPCR */
#define C_FW_INT_EN_SET			0x00000001
#define C_FW_INT_EN_CLEAR		0x00000002
/* CHISR */
#define RX_PKT_LEN				0xFFFF0000
#define FIRMWARE_INT			0x0000FE00
/* MCU notify host dirver for L0.5 reset */
#define FIRMWARE_INT_BIT31		0x80000000
/* MCU notify host driver for coredump */
#define FIRMWARE_INT_BIT15		0x00008000
#define TX_FIFO_OVERFLOW		0x00000100
#define FW_INT_IND_INDICATOR	0x00000080
#define TX_COMPLETE_COUNT		0x00000070
#define TX_UNDER_THOLD			0x00000008
#define TX_EMPTY				0x00000004
#define RX_DONE					0x00000002
#define FW_OWN_BACK_INT			0x00000001

/* MCU address offset */
#define MCU_ADDRESS_OFFSET_CMD 12
#define MCU_ADDRESS_OFFSET_EVT 16


typedef int (*pdwnc_func) (u8 fgReset);
typedef int (*reset_func_ptr2) (unsigned int gpio, int init_value);
typedef int (*set_gpio_low)(u8 gpio);
typedef int (*set_gpio_high)(u8 gpio);


/**
 * Send cmd dispatch evt
 */
#define RETRY_TIMES 10
#define HCI_EV_VENDOR			0xff
#define SDIO_BLOCK_SIZE                 256
#define SDIO_RW_RETRY_COUNT 500
#define MTK_SDIO_PACKET_HEADER_SIZE 4

/* Driver & FW own related */
#define DRIVER_OWN 0
#define FW_OWN 1

struct btmtk_sdio_hdr {
	/* For SDIO Header */
	__le16	len;
	__le16	reserved;
	/* For hci type */
	u8	bt_type;
} __packed;

struct btmtk_dev {
	struct hci_dev	*hdev;
	struct sdio_func *func;
	unsigned long	hdev_flags;

	bool keep_drv_on;
	u8 tx_empty;
	u8 rx_done;
	u32 int_count;
	bool no_fw_own;
	bool tx_dnld_rdy;
	bool rx_dnld_rdy;

	unsigned long	flags;

	struct work_struct	work;
	struct work_struct	waker;
	struct work_struct	reset_waker;

	int recv_evt_len;
	int	tx_in_flight;
	spinlock_t	txlock;
	spinlock_t	rxlock;

	struct sk_buff	*evt_skb;
	struct sk_buff	*sco_skb;
	struct sk_buff_head tx_queue;

	/* For ble iso packet size */
	int iso_threshold;

	unsigned int	sco_num;
	int	isoc_altsetting;
	int	suspend_count;

	/* For tx queue */
	unsigned long	tx_state;

	/* For rx queue */
	struct workqueue_struct	*workqueue;
	struct sk_buff_head	rx_q;
	struct work_struct	rx_work;
	struct sk_buff		*rx_skb;

	wait_queue_head_t p_wait_event_q;

	unsigned int	subsys_reset;
	unsigned int	chip_reset;
	unsigned char	*rom_patch_bin_file_name;
	unsigned char	power_state;
	unsigned char	fops_state;
	unsigned char	interface_state;
	unsigned int	chip_id;
	unsigned int	flavor;
	unsigned int	fw_version;
	unsigned char	dongle_index;
	struct btmtk_cif_state *cif_state;

	/* io buffer for receiving control transfer */
	unsigned char	*io_buf;
	unsigned char	*o_usb_buf;

	unsigned char	*setting_file;
	unsigned char	*woble_setting_file_name;
	unsigned int	woble_setting_len;

	struct fw_cfg_struct	woble_setting_apcf[WOBLE_SETTING_COUNT];
	struct fw_cfg_struct	woble_setting_apcf_fill_mac[WOBLE_SETTING_COUNT];
	struct fw_cfg_struct	woble_setting_apcf_fill_mac_location[WOBLE_SETTING_COUNT];

	struct fw_cfg_struct	woble_setting_radio_off;
	struct fw_cfg_struct	woble_setting_wakeup_type;
	struct fw_cfg_struct	woble_setting_radio_off_status_event;
	/* complete event */
	struct fw_cfg_struct	woble_setting_radio_off_comp_event;

	struct fw_cfg_struct	woble_setting_radio_on;
	struct fw_cfg_struct	woble_setting_radio_on_status_event;
	struct fw_cfg_struct	woble_setting_radio_on_comp_event;

	/* set apcf after resume(radio on) */
	struct fw_cfg_struct	woble_setting_apcf_resume[WOBLE_SETTING_COUNT];
	unsigned char	bdaddr[BD_ADDRESS_SIZE];
	unsigned int	woble_need_trigger_coredump;
	struct	wakeup_source	woble_ws;
	unsigned int	woble_need_set_radio_off_in_probe;

	unsigned char		*bt_cfg_file_name;
	struct bt_cfg_struct	bt_cfg;

	/* TODO, need to confirm the max size of urb data, also need to confirm
	 * whether intr_complete and bulk_complete and soc_complete can all share
	 * this urb_transfer_buf
	 */
	unsigned char	*transfer_buf;
	unsigned char	*sdio_packet;
	/* To-do
	 * We must be remove it
	 */
	unsigned char	*urb_transfer_buf;

	/* For Whole chip reset */
	pdwnc_func pf_pdwndFunc;
	reset_func_ptr2 pf_resetFunc2;
	set_gpio_low pf_lowFunc;
	set_gpio_high pf_highFunc;
};


int btmtk_cif_send_cmd(struct btmtk_dev *bdev, struct sk_buff *skb,
		int delay, int retry, int endpoint);
int btmtk_cif_send_calibration(struct btmtk_dev *bdev);
struct btmtk_dev *btmtk_cif_get_btmtk_dev_data(void);
int btmtk_cif_open(struct hci_dev *hdev);
int btmtk_cif_close(struct hci_dev *hdev);
int btmtk_cif_read_register(struct btmtk_dev *bdev, u32 reg, u32 *val);
int btmtk_cif_get_rom_patch_result(struct btmtk_dev *bdev);
int btmtk_cif_recv_evt(struct btmtk_dev *bdev, int delay, int retry);
int btmtk_cif_subsys_reset(struct btmtk_dev *bdev);

#endif
