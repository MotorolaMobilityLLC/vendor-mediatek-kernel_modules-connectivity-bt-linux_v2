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
#include "btmtk_main.h"
#include "btmtk_woble.h"
#include "btmtk_buffer_mode.h"

#ifndef BTMTK_SDIO_DEBUG
#define BTMTK_SDIO_DEBUG 0
#endif

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
#define CSICR		0x00C0
#define PD2HRM0R	0x00DC
#define SWPCDBGR	0x0154
/* CHLPCR */
#define C_FW_INT_EN_SET			0x00000001
#define C_FW_INT_EN_CLEAR		0x00000002
/* CHISR */
#define RX_PKT_LEN				0xFFFF0000
#define FIRMWARE_INT			0x0000FE00
/* PD2HRM0R */
#define PD2HRM0R_DRIVER_OWN		0x00000001
#define PD2HRM0R_FW_OWN		0x00000000
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

#define BT_DUMP_BGF_SLEEP_STATUS_ADDR_W_1 0x00000044
#define BT_DUMP_BGF_SLEEP_STATUS_VALUE_W_1 0x1
#define BT_DUMP_BGF_SLEEP_STATUS_ADDR_W_2 0x00000030
#define BT_DUMP_BGF_SLEEP_STATUS_VALUE_W_2 0x80000080
#define BT_DUMP_BGF_SLEEP_STATUS_ADDR_R 0x0000002C

#define BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_1 0x00000044
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_1 0x1
#define BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2 0x00000030
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_1 0x80000000
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_2 0x91800000
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_3 0x90880000
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_4 0x86280080
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_5 0x86280081
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_6 0x86280082
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_7 0x86280083
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_8 0x86280084
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_9 0x86280085
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_10 0x86280086
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_11 0x86280087
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_12 0x86280088
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_13 0x86280089
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_14 0x8A480080
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_15 0x8A480081
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_16 0x8A480082
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_17 0x8A480083
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_18 0x8A480084
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_19 0x8A480085
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_20 0x8A480086
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_21 0x8A480087
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_22 0x8A480088
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_23 0x8A480089
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_24 0x80000080
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_25 0x80000081
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_26 0x80000082
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_27 0x80000083
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_28 0x80000084
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_29 0x80000085
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_30 0x80000086
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_31 0x80000087
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_32 0x80000088
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_33 0x80000089
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_34 0x8000008A
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_35 0x8000008B
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_36 0x8000008C
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_37 0x8000008D
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_38 0x8000008E
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_39 0x8000008F
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_40 0x81000080
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_41 0x81000081
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_42 0x81000082
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_43 0x81000083
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_44 0x81000084
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_45 0x81000085
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_46 0x81000086
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_47 0x81000087
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_48 0x81000088
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_49 0x81000089
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_50 0x8100008A
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_51 0x8100008B
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_52 0x8100008C
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_53 0x8100008D
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_54 0x8100008E
#define BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_55 0x8100008F
#define BT_DUMP_BGF_BT_DEBUG_LOG_NUM 55
#define BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R 0x0000002C

/* MCU address offset */
#define MCU_ADDRESS_OFFSET_CMD 12
#define MCU_ADDRESS_OFFSET_EVT 16

/* wifi CR */
#define CONDBGCR		0x0034
#define CONDBGCR_SEL		0x0040
#define SDIO_CTRL_EN		(1 << 31)
#define WM_MONITER_SEL		(~(0x40000000))
#define PC_MONITER_SEL		(~(0x20000000))
#define PC_IDX_SWH(val, idx)	((val & (~(0x3F << 16))) | ((0x3F & idx) << 16))

typedef int (*pdwnc_func) (u8 fgReset);
typedef int (*reset_func_ptr2) (unsigned int gpio, int init_value);
typedef int (*set_gpio_low)(u8 gpio);
typedef int (*set_gpio_high)(u8 gpio);

/**
 * Send cmd dispatch evt
 */
#define HCI_EV_VENDOR			0xff
#define SDIO_BLOCK_SIZE                 512
#define SDIO_RW_RETRY_COUNT 500
#define MTK_SDIO_PACKET_HEADER_SIZE 4

/* Driver & FW own related */
#define DRIVER_OWN 0
#define FW_OWN 1
#define SET_OWN_LOOP_COUNT 20

/* CMD&Event sent by driver */
#define READ_REGISTER_CMD_LEN		16
#define READ_REGISTER_EVT_HDR_LEN		11

#define FW_ASSERT_CMD_LEN 4
#define FW_ASSERT_CMD1_LEN 9
#define NOTIFY_ALT_EVT_LEN 7

#define READ_ADDRESS_EVT_HDR_LEN 7
#define READ_ADDRESS_EVT_PAYLOAD_OFFSET 7
#define WOBLE_DEBUG_EVT_TYPE 0xE8
#define BLE_EVT_TYPE 0x3E

#define LD_PATCH_CMD_LEN 10
#define LD_PATCH_EVT_LEN 8


struct btmtk_sdio_hdr {
	/* For SDIO Header */
	__le16	len;
	__le16	reserved;
	/* For hci type */
	u8	bt_type;
} __packed;

struct btmtk_sdio_thread {
	struct task_struct *task;
	wait_queue_head_t wait_q;
	void *priv;
	u8 thread_status;
};

struct btmtk_sdio_dev {
	struct sdio_func *func;
	struct btmtk_dev *bdev;

	bool patched;
	bool no_fw_own;
	atomic_t int_count;
	atomic_t tx_rdy;

	/* TODO, need to confirm the max size of urb data, also need to confirm
	 * whether intr_complete and bulk_complete and soc_complete can all share
	 * this urb_transfer_buf
	 */
	unsigned char	*transfer_buf;
	unsigned char	*sdio_packet;

	struct sk_buff_head tx_queue;
	struct btmtk_sdio_thread sdio_thread;
	struct btmtk_woble bt_woble;
	struct btmtk_buffer_mode_struct *buffer_mode;
};

int btmtk_sdio_read_bt_mcu_pc(u32 *val);
int btmtk_sdio_read_conn_infra_pc(u32 *val);

#endif
