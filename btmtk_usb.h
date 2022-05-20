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

#ifndef _BTMTK_USB_H_
#define _BTMTK_USB_H_
#include <linux/usb.h>
#include "btmtk_define.h"

#define HCI_MAX_COMMAND_SIZE	255
#define URB_MAX_BUFFER_SIZE	(4*1024)

#define BT0_MCU_INTERFACE_NUM 0
#define BT1_MCU_INTERFACE_NUM 3


/**
 * Send cmd dispatch evt
 */
#define HCI_EV_VENDOR			0xff
#define HCI_USB_IO_BUF_SIZE		256

/** For MTK Endpoint desc. */
#define BGF0_CMD_BULK
#define BGF1_CMD_BULK
#define SUPPORT_HW_DVT


enum {
	BTMTK_EP_TYPE_OUT_CMD = 0,	/*EP type out for hci cmd and wmt cmd */
	BTMTK_EP_TPYE_OUT_ACL,	/* EP type out for acl pkt with load rompatch */
	BTMTK_EP_TYPE_OUT_OTHER,	/* EP type out for pkt from host, include acl and hci */
};

struct btmtk_dev {
	struct hci_dev	*hdev;
	unsigned long	hdev_flags;

	struct usb_device	*udev;
	struct usb_interface	*intf;
	struct usb_interface	*isoc;
	struct usb_interface	*iso_channel;

	unsigned long	flags;

	struct work_struct	work;
	struct work_struct	waker;
	struct work_struct	reset_waker;

	struct usb_anchor	tx_anchor;
	int	tx_in_flight;
	spinlock_t	txlock;

	struct usb_anchor	intr_anchor;
	struct usb_anchor	bulk_anchor;
	struct usb_anchor	isoc_anchor;
	struct usb_anchor	ctrl_anchor;
	struct usb_anchor	ble_isoc_anchor;
	spinlock_t	rxlock;

	struct sk_buff	*evt_skb;
	struct sk_buff	*sco_skb;

	struct usb_endpoint_descriptor	*intr_ep;
	/* EP10 OUT */
	struct usb_endpoint_descriptor	*intr_iso_tx_ep;
	/* EP10 IN */
	struct usb_endpoint_descriptor	*intr_iso_rx_ep;
	/* BULK CMD EP1 OUT or EP 11 OUT */
	struct usb_endpoint_descriptor	*bulk_cmd_tx_ep;
	/* EP15 in for reset */
	struct usb_endpoint_descriptor	*reset_intr_ep;
	struct usb_endpoint_descriptor	*bulk_tx_ep;
	struct usb_endpoint_descriptor	*bulk_rx_ep;
	struct usb_endpoint_descriptor	*isoc_tx_ep;
	struct usb_endpoint_descriptor	*isoc_rx_ep;

	__u8	cmdreq_type;
	__u8	cmdreq;

	unsigned int	sco_num;
	int	isoc_altsetting;

#ifdef SUPPORT_HW_DVT
	int new_isoc_altsetting;
	int new_isoc_altsetting_interface;
#endif
	int	suspend_count;

	/* For tx queue */
	unsigned long	tx_state;

	/* For rx queue */
	struct workqueue_struct	*workqueue;
	struct sk_buff_head	rx_q;
	struct work_struct	rx_work;
	struct sk_buff		*rx_skb;

	wait_queue_head_t	p_wait_event_q;

	unsigned int	subsys_reset;
	unsigned char	*rom_patch_bin_file_name;
	unsigned int	chip_id;
	unsigned int	flavor;
	unsigned int	fw_version;
	unsigned char	power_state;
	unsigned char	fops_state;
	unsigned char	interface_state;
	struct btmtk_cif_state *cif_state;

	/* io buffer for usb control transfer */
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
	unsigned char	*urb_transfer_buf;
};

int btmtk_cif_send_cmd(struct btmtk_dev *bdev, struct sk_buff *skb,
		int delay, int retry, int endpoint);
int btmtk_cif_send_calibration(struct btmtk_dev *bdev);
int btmtk_cif_open(struct hci_dev *hdev);
int btmtk_cif_close(struct hci_dev *hdev);
int btmtk_cif_read_register(struct btmtk_dev *bdev, u32 reg, u32 *val);
int btmtk_cif_write_register(struct btmtk_dev *bdev, u32 reg, u32 val);
int btmtk_cif_get_rom_patch_result(struct btmtk_dev *bdev);
int btmtk_cif_recv_evt(struct btmtk_dev *bdev, int delay, int retry);
int btmtk_cif_write_uhw_register(struct btmtk_dev *bdev, u32 reg, u32 val);
int btmtk_cif_read_uhw_register(struct btmtk_dev *bdev, u32 reg, u32 *val);
#endif
