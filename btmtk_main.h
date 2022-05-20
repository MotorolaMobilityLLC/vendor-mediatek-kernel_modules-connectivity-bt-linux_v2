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
#ifndef __BTMTK_MAIN_H__
#define __BTMTK_MAIN_H__
#include "btmtk_define.h"
#include "btmtk_chip_if.h"

int btmtk_allocate_hci_device(struct btmtk_dev *bdev, int hci_bus_type);
int btmtk_register_hci_device(struct btmtk_dev *bdev);
void btmtk_free_hci_device(struct btmtk_dev *bdev, int hci_bus_type);
int btmtk_recv(struct hci_dev *hdev, const u8 *data, size_t count);
int btmtk_recv_event(struct hci_dev *hdev, struct sk_buff *skb);
int btmtk_recv_acl(struct hci_dev *hdev, struct sk_buff *skb);
int btmtk_dispatch_event(struct hci_dev *hdev, struct sk_buff *skb);
int btmtk_dispatch_acl(struct hci_dev *hdev, struct sk_buff *skb);
int btmtk_send_init_cmds(struct btmtk_dev *hdev);
int btmtk_send_deinit_cmds(struct btmtk_dev *hdev);
int btmtk_main_send_cmd(struct btmtk_dev *bdev, const uint8_t *cmd,
		const int cmd_len, const uint8_t *event, const int event_len, int delay,
		int retry, int endpoint, const int tx_state, bool wmt_cmd);
int btmtk_send_wmt_reset(struct btmtk_dev *hdev);
int btmtk_send_wmt_power_on_cmd_766x(struct btmtk_dev *hdev);
int btmtk_send_wmt_power_off_cmd_766x(struct btmtk_dev *hdev);
int btmtk_load_rom_patch_766x(struct btmtk_dev *hdev);
int btmtk_uart_send_wakeup_cmd(struct hci_dev *hdev);
int btmtk_uart_send_set_uart_cmd(struct hci_dev *hdev);
void btmtk_cap_init(struct btmtk_dev *bdev);
int btmtk_load_rom_patch(struct btmtk_dev *bdev);


//static inline struct sk_buff *mtk_add_stp(struct btmtk_dev *bdev, struct sk_buff *skb);

#define hci_dev_test_and_clear_flag(hdev, nr)  test_and_clear_bit((nr), (hdev)->dev_flags)

/* h4_recv */
#define hci_skb_pkt_type(skb) bt_cb((skb))->pkt_type
#define hci_skb_expect(skb) bt_cb((skb))->expect
#define hci_skb_opcode(skb) bt_cb((skb))->hci.opcode

/* HCI bus types */
#define HCI_VIRTUAL	0
#define HCI_USB		1
#define HCI_PCCARD	2
#define HCI_UART	3
#define HCI_RS232	4
#define HCI_PCI		5
#define HCI_SDIO	6
#define HCI_SPI		7
#define HCI_I2C		8
#define HCI_SMD		9

/* this for 7663 need download patch staus
 * 0:
 * patch download is not complete/BT get patch semaphore fail (WiFi get semaphore success)
 * 1:
 * patch download is complete
 * 2:
 * patch download is not complete/BT get patch semaphore success
 */
#define MT766X_PATCH_IS_DOWNLOAD_BY_OTHER 0
#define MT766X_PATCH_READY 1
#define MT766X_PATCH_NEED_DOWNLOAD 2

/* this for 79XX need download patch staus
 * 0:
 * patch download is not complete, BT driver need to download patch
 * 1:
 * patch is downloading by Wifi,BT driver need to retry until status = PATCH_READY
 * 2:
 * patch download is complete, BT driver no need to download patch
 */
#define PATCH_ERR -1
#define PATCH_NEED_DOWNLOAD 0
#define PATCH_IS_DOWNLOAD_BY_OTHER 1
#define PATCH_READY 2

/* 0:
 * using legacy wmt cmd/evt to download fw patch, usb/sdio just support 0 now
 * 1:
 * using DMA to download fw patch
 */
#define PATCH_DOWNLOAD_USING_WMT 0
#define PATCH_DOWNLOAD_USING_DMA 1

#define PATCH_DOWNLOAD_PHASE1_2_DELAY_TIME 10
#define PATCH_DOWNLOAD_PHASE1_2_RETRY 20
#define PATCH_DOWNLOAD_PHASE3_DELAY_TIME 500
#define PATCH_DOWNLOAD_PHASE3_RETRY 2

enum {
	BTMTK_DONGLE_STATE_UNKNOWN,
	BTMTK_DONGLE_STATE_POWERING_ON,
	BTMTK_DONGLE_STATE_POWER_ON,
	BTMTK_DONGLE_STATE_WOBLE,
	BTMTK_DONGLE_STATE_POWERING_OFF,
	BTMTK_DONGLE_STATE_POWER_OFF,
	BTMTK_DONGLE_STATE_ERROR
};

enum {
	HW_ERR_NONE = 0x00,
	HW_ERR_CODE_CHIP_RESET = 0xF0,
	HW_ERR_CODE_USB_DISC = 0xF1,
	HW_ERR_CODE_CORE_DUMP = 0xF2,
	HW_ERR_CODE_POWER_ON = 0xF3,
	HW_ERR_CODE_POWER_OFF = 0xF4,
	HW_ERR_CODE_WOBLE = 0xF5,
	HW_ERR_CODE_SET_SLEEP_CMD = 0xF6,
	HW_ERR_CODE_RESET_STACK_AFTER_WOBLE = 0xF7,
};

/* Please keep sync with btmtk_set_state function */
enum {
	BTMTK_STATE_INIT,
	BTMTK_STATE_DISCONNECT,
	BTMTK_STATE_PROBE,
	BTMTK_STATE_WORKING,
	BTMTK_STATE_SUSPEND,
	BTMTK_STATE_RESUME,
	BTMTK_STATE_FW_DUMP,
	BTMTK_STATE_STANDBY,
};

/* Please keep sync with btmtk_fops_set_state function */
enum {
	BTMTK_FOPS_STATE_INIT,
	BTMTK_FOPS_STATE_OPENED,	/* open in fops_open */
	BTMTK_FOPS_STATE_CLOSING,	/* during closing */
	BTMTK_FOPS_STATE_CLOSED,	/* closed */
};

struct h4_recv_pkt {
	u8  type;	/* Packet type */
	u8  hlen;	/* Header length */
	u8  loff;	/* Data length offset in header */
	u8  lsize;	/* Data length field size */
	u16 maxlen;	/* Max overall packet length */
	int (*recv)(struct hci_dev *hdev, struct sk_buff *skb);
};

#pragma pack(1)
struct _PATCH_HEADER {
	u8 ucDateTime[16];
	u8 ucPlatform[4];
	u16 u2HwVer;
	u16 u2SwVer;
	u32 u4MagicNum;
};

struct _Global_Descr {
	u32 u4PatchVer;
	u32 u4SubSys;
	u32 u4FeatureOpt;
	u32 u4SectionNum;
};

struct _Section_Map {
	u32 u4SecType;
	u32 u4SecOffset;
	u32 u4SecSize;
	union {
		u32 u4SecSpec[SECTION_SPEC_NUM];
		struct {
			u32 u4DLAddr;
			u32 u4DLSize;
			u32 u4SecKeyIdx;
			u32 u4AlignLen;
			u32 reserved[9];
		} bin_info_spec;
	};
};
#pragma pack()

#define H4_RECV_ACL \
	.type = HCI_ACLDATA_PKT, \
	.hlen = HCI_ACL_HDR_SIZE, \
	.loff = 2, \
	.lsize = 2, \
	.maxlen = HCI_MAX_FRAME_SIZE \

#define H4_RECV_SCO \
	.type = HCI_SCODATA_PKT, \
	.hlen = HCI_SCO_HDR_SIZE, \
	.loff = 2, \
	.lsize = 1, \
	.maxlen = HCI_MAX_SCO_SIZE

#define H4_RECV_EVENT \
	.type = HCI_EVENT_PKT, \
	.hlen = HCI_EVENT_HDR_SIZE, \
	.loff = 1, \
	.lsize = 1, \
	.maxlen = HCI_MAX_EVENT_SIZE


static inline int is_mt7961(u32 chip_id)
{
	chip_id &= 0xFFFF;
	if (chip_id == 0x7961)
		return 1;
	return 0;
}

static inline int is_mt7663(u32 chip_id)
{
	chip_id &= 0xFFFF;
	if (chip_id == 0x7663)
		return 1;
	return 0;
}

#endif /* __BTMTK_MAIN_H__ */
