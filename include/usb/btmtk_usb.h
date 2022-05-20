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
#include "btmtk_main.h"
#include "btmtk_woble.h"
#include "btmtk_chip_reset.h"

#define HCI_MAX_COMMAND_SIZE	255
#define URB_MAX_BUFFER_SIZE	(4*1024)

#define BT0_MCU_INTERFACE_NUM 0
#define BT1_MCU_INTERFACE_NUM 3
#define BT_MCU_INTERFACE_NUM_MAX 4
#define BT_MCU_NUM_MAX 2

typedef int (*pdwnc_func) (u8 fgReset);
typedef int (*reset_func_ptr2) (unsigned int gpio, int init_value);
typedef int (*set_gpio_low)(u8 gpio);
typedef int (*set_gpio_high)(u8 gpio);

/**
 * Send cmd dispatch evt
 */
#define HCI_EV_VENDOR			0xff
#define HCI_USB_IO_BUF_SIZE		256


/* UHW CR mapping */
#define BT_MISC 0x70002510
#define BT_SUBSYS_RST 0x70002610
#define UDMA_INT_STA_BT 0x74000024
#define UDMA_INT_STA_BT1 0x74000308
#define BT_WDT_STATUS 0x740003A0
#define EP_RST_OPT 0x74011890
#define EP_RST_IN_OUT_OPT 0x00010001

#define BT_GDMA_DONE_ADDR_W 0x74000A0C
#define BT_GDMA_DONE_VALUE_W 0x00403FA9
#define BT_GDMA_DONE_ADDR_R 0x74000A08
#define BT_GDMA_DONE_VALUE_R 0xFFFFFFFB /* bit2: 0 - dma done, 1 - dma doing */

#define BT_DUMP_POWER_STATUS_ADDR_W 0x74000A24
#define BT_DUMP_POWER_STATUS_VALUE_W 0x9F1E0000
#define BT_DUMP_POWER_STATUS_ADDR_R 0x74000A20

#define BT_DUMP_BGF_SLEEP_STATUS_ADDR_W 0x74000A04
#define BT_DUMP_BGF_SLEEP_STATUS_VALUE_W 0x80000080
#define BT_DUMP_BGF_SLEEP_STATUS_ADDR_R 0x74000A00

#define BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W 0x74000A04
#define BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R 0x74000A00
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_1 0x80000000
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2 0x91800000
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_3 0x90880000
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_4 0x86280080
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_5 0x86280081
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_6 0x86280082
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_7 0x86280083
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_8 0x86280084
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_9 0x86280085
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_10 0x86280086
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_11 0x86280087
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_12 0x86280088
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_13 0x86280089
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_14 0x8A480080
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_15 0x8A480081
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_16 0x8A480082
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_17 0x8A480083
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_18 0x8A480084
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_19 0x8A480085
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_20 0x8A480086
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_21 0x8A480087
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_22 0x8A480088
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_23 0x8A480089
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_24 0x80000080
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_25 0x80000081
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_26 0x80000082
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_27 0x80000083
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_28 0x80000084
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_29 0x80000085
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_30 0x80000086
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_31 0x80000087
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_32 0x80000088
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_33 0x80000089
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_34 0x8000008A
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_35 0x8000008B
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_36 0x8000008C
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_37 0x8000008D
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_38 0x8000008E
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_39 0x8000008F
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_40 0x81000080
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_41 0x81000081
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_42 0x81000082
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_43 0x81000083
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_44 0x81000084
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_45 0x81000085
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_46 0x81000086
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_47 0x81000087
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_48 0x81000088
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_49 0x81000089
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_50 0x8100008A
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_51 0x8100008B
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_52 0x8100008C
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_53 0x8100008D
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_54 0x8100008E
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_55 0x8100008F
#define BT_DUMP_BGF_BT_DEBUG_LOG_NUM 55

/* CMD&Event sent by driver */
#define NOTIFY_ALT_EVT_LEN 7

#define LD_PATCH_CMD_LEN 9
#define LD_PATCH_EVT_LEN 8

#define READ_ADDRESS_EVT_HDR_LEN 7
#define READ_ADDRESS_EVT_PAYLOAD_OFFSET 7
#define WOBLE_DEBUG_EVT_TYPE 0xE8
#define BLE_EVT_TYPE 0x3E

#define WMT_TRIGGER_ASSERT_LEN 9

struct btmtk_cif_chip_reset {
	/* For Whole chip reset */
	pdwnc_func pf_pdwndFunc;
	reset_func_ptr2 pf_resetFunc2;
	set_gpio_low pf_lowFunc;
	set_gpio_high pf_highFunc;
};

struct btmtk_usb_dev {
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

	struct usb_device	*udev;
	struct usb_interface	*intf;
	struct usb_interface	*isoc;
	struct usb_interface	*iso_channel;


	struct usb_anchor	tx_anchor;
	struct usb_anchor	intr_anchor;
	struct usb_anchor	bulk_anchor;
	struct usb_anchor	isoc_anchor;
	struct usb_anchor	ctrl_anchor;
	struct usb_anchor	ble_isoc_anchor;

	__u8	cmdreq_type;
	__u8	cmdreq;

	int new_isoc_altsetting;
	int new_isoc_altsetting_interface;

	unsigned char	*o_usb_buf;

	unsigned char	*urb_intr_buf;
	unsigned char	*urb_bulk_buf;
	unsigned char	*urb_ble_isoc_buf;
	struct btmtk_woble	bt_woble;
};

#endif
