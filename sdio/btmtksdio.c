/*
 *
 *  Generic Bluetooth USB driver
 *
 *  Copyright (C) 2005-2008  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "btmtk_sdio.h"

static char event_need_compare[EVENT_COMPARE_SIZE] = {0};
static char event_need_compare_len;
static char event_compare_status;

static DEFINE_MUTEX(btmtk_sdio_own_mutex);
#define SDIO_OWN_MUTEX_LOCK()	mutex_lock(&btmtk_sdio_own_mutex)
#define SDIO_OWN_MUTEX_UNLOCK()	mutex_unlock(&btmtk_sdio_own_mutex)


static DEFINE_MUTEX(btmtk_sdio_ops_mutex);
#define SDIO_OPS_MUTEX_LOCK()	mutex_lock(&btmtk_sdio_ops_mutex)
#define SDIO_OPS_MUTEX_UNLOCK()	mutex_unlock(&btmtk_sdio_ops_mutex)

static DEFINE_MUTEX(btmtk_sdio_debug_mutex);
#define SDIO_DEBUG_MUTEX_LOCK()	mutex_lock(&btmtk_sdio_debug_mutex)
#define SDIO_DEBUG_MUTEX_UNLOCK()	mutex_unlock(&btmtk_sdio_debug_mutex)

/* static const struct btmtksdio_data btmtk_sdio_7663 = {
 *	.fwname = FIRMWARE_MT7663,
 * };
 *
 * static const struct btmtksdio_data btmtk_sdio_7961 = {
 *	.fwname = FIRMWARE_MT7668,
 * };
 */
static int btmtk_sdio_readl(u32 offset,  u32 *val, struct sdio_func *func);
static int btmtk_sdio_writel(u32 offset, u32 val, struct sdio_func *func);

#define DUMP_FW_PC(cif_dev)			\
do {							\
	u32 __value = 0;				\
	btmtk_sdio_read_bt_mcu_pc(&__value);		\
	BTMTK_INFO("%s, BT mcu pc: 0x%08X", __func__, __value);	\
	btmtk_sdio_read_conn_infra_pc(&__value);	\
	BTMTK_INFO("%s, conn infra pc: 0x%08X", __func__, __value);	\
} while (0)

static struct btmtk_sdio_dev g_sdio_dev;

static const struct sdio_device_id btmtk_sdio_tabls[] = {
	/* Mediatek SD8688 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7663)
		/*,
		 *.driver_data = (unsigned long) &btmtk_sdio_7663
		 */ },

	/* Bring-up only */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7668)
		/*,
		 *.driver_data = (unsigned long) &btmtk_sdio_7663
		 */ },

	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7961)
		/*,
		 *.driver_data = (unsigned long) &btmtk_sdio_7961
		 */ },

	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE(sdio, btmtk_sdio_tabls);

#if BTMTK_SDIO_DEBUG
#define RX_DEBUG_ENTRY_NUM 50
enum {
	CHISR_r_1 = 0,
	CHISR_r_2,
	CRPLR_r,
	PD2HRM0R_r,
	SDIO_DEBUG_CR_MAX,
	RX_TIMESTAMP,
	RX_BUF
};

struct rx_debug_struct {
	unsigned int rx_intr_timestamp;
	u32 cr[SDIO_DEBUG_CR_MAX];
	u8 buf[16];
};
static struct rx_debug_struct rx_debug[RX_DEBUG_ENTRY_NUM];
static int rx_debug_index;

static int rx_done_cnt;
static int tx_empty_cnt;
static int intr_cnt;
static int driver_own_cnt;
static int fw_own_cnt;

static unsigned int btmtk_sdio_hci_snoop_get_microseconds(void)
{
	struct timeval now;

	btmtk_do_gettimeofday(&now);
	return now.tv_sec * 1000000 + now.tv_usec;
}

void rx_debug_print(void)
{
	int i;
	int j = rx_debug_index;

	BTMTK_ERR("%s: rx_done_cnt=%d, tx_empty_cnt=%d, intr_cnt=%d, driver_own_cnt=%d, fw_own_cnt=%d",
		__func__, rx_done_cnt, tx_empty_cnt, intr_cnt, driver_own_cnt, fw_own_cnt);
	for (i = 0; i < RX_DEBUG_ENTRY_NUM; i++) {
		BTMTK_ERR("%02d: timestamp=%u, CHISR_r_1=0x%08x, CHISR_r_2=0x%08x, CRPLR=0x%08x, PD2HRM0R=0x%08x,",
			i, rx_debug[j].rx_intr_timestamp,
			rx_debug[j].cr[CHISR_r_1], rx_debug[j].cr[CHISR_r_2],
			rx_debug[j].cr[CRPLR_r], rx_debug[j].cr[PD2HRM0R_r]);
		BTMTK_ERR("buf = %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
			rx_debug[j].buf[0], rx_debug[j].buf[1], rx_debug[j].buf[2], rx_debug[j].buf[3],
			rx_debug[j].buf[4], rx_debug[j].buf[5], rx_debug[j].buf[6], rx_debug[j].buf[7],
			rx_debug[j].buf[8], rx_debug[j].buf[9], rx_debug[j].buf[10], rx_debug[j].buf[11],
			rx_debug[j].buf[12], rx_debug[j].buf[13], rx_debug[j].buf[14], rx_debug[j].buf[15]);
		if (j == 0)
			j = RX_DEBUG_ENTRY_NUM;
		j--;
	}
}

void rx_debug_save(int type, u32 value, u8 *buf)
{
	switch (type) {
	case CHISR_r_1:
	case CHISR_r_2:
	case CRPLR_r:
	case PD2HRM0R_r:
		rx_debug[rx_debug_index].cr[type] = value;
		break;
	case RX_TIMESTAMP:
		rx_debug_index++;
		if (rx_debug_index == RX_DEBUG_ENTRY_NUM)
			rx_debug_index = 0;
		rx_debug[rx_debug_index].rx_intr_timestamp = btmtk_sdio_hci_snoop_get_microseconds();
		break;
	case RX_BUF:
		memset(rx_debug[rx_debug_index].buf, 0, 16);
		memcpy(rx_debug[rx_debug_index].buf, buf, 16);
		break;
	}
}
#endif

static void btmtk_sdio_dump_bgfsys_sleep_status(struct btmtk_dev *bdev)
{
	struct btmtk_sdio_dev *cif_dev = NULL;
	u32 value = 0;

	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

	btmtk_sdio_writel(BT_DUMP_BGF_SLEEP_STATUS_ADDR_W_1,
		BT_DUMP_BGF_SLEEP_STATUS_VALUE_W_1, cif_dev->func);
	btmtk_sdio_writel(BT_DUMP_BGF_SLEEP_STATUS_ADDR_W_2,
		BT_DUMP_BGF_SLEEP_STATUS_VALUE_W_2, cif_dev->func);
	btmtk_sdio_readl(BT_DUMP_BGF_SLEEP_STATUS_ADDR_R, &value, cif_dev->func);
	BTMTK_INFO("%s W(0x%08X) = 0x%08X, W(0x%08X) = 0x%08X, R(0x%08X) = 0x%08X",
		__func__,
		BT_DUMP_BGF_SLEEP_STATUS_ADDR_W_1, BT_DUMP_BGF_SLEEP_STATUS_VALUE_W_1,
		BT_DUMP_BGF_SLEEP_STATUS_ADDR_W_2, BT_DUMP_BGF_SLEEP_STATUS_VALUE_W_2,
		BT_DUMP_BGF_SLEEP_STATUS_ADDR_R, value);
}

static void btmtk_sdio_dump_bgf_bt_debug_log(struct btmtk_dev *bdev)
{
	struct dump_debug_cr dump_cr[BT_DUMP_BGF_BT_DEBUG_LOG_NUM] = {
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_1,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_2,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_3,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_4,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_5,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_6,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_7,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_8,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_9,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_10,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_11,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_12,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_13,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_14,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_15,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_16,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_17,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_18,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_19,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_20,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_21,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_22,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_23,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_24,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_25,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_26,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_27,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_28,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_29,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_30,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_31,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_32,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_33,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_34,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_35,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_36,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_37,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_38,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_39,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_40,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_41,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_42,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_43,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_44,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_45,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_46,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_47,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_48,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_49,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_50,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_51,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_52,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_53,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_54,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R },
		{ BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_2, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_2_55,
		BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_R }
		};
	struct btmtk_sdio_dev *cif_dev = NULL;
	u32 value = 0, i = 0;

	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

	btmtk_sdio_writel(BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_1, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_1, cif_dev->func);
	for (i = 0; i < BT_DUMP_BGF_BT_DEBUG_LOG_NUM; i++) {
		btmtk_sdio_writel(dump_cr[i].addr_w, dump_cr[i].value_w, cif_dev->func);
		btmtk_sdio_readl(dump_cr[i].addr_r, &value, cif_dev->func);
		BTMTK_INFO("%s W(0x%08X) = 0x%08X, W(0x%08X) = 0x%08X, R(0x%08X) = 0x%08X",
			__func__,
			BT_DUMP_BGF_BT_DEBUG_LOG_ADDR_W_1, BT_DUMP_BGF_BT_DEBUG_LOG_VALUE_W_1,
			dump_cr[i].addr_w, dump_cr[i].value_w,
			dump_cr[i].addr_r, value);
	}
}

static void btmtk_sdio_dump_debug_sop_sleep(struct btmtk_dev *bdev)
{
	/* dump Power_Status(Power_check), SDIO not support */

	/* dump BGFSYS_Sleep_Status(Power_check) */
	btmtk_sdio_dump_bgfsys_sleep_status(bdev);

	/* dump BGF_BT_DEBUG_LOG(BGFSYS_status) */
	btmtk_sdio_dump_bgf_bt_debug_log(bdev);

	/* dump mcu_sleep_wakeup_debug(BGFSYS_status),
	 * only for PCIE, USB/SDIO not support
	 */
}

static void btmtk_sdio_dump_debug_sop_wakeup(struct btmtk_dev *bdev)
{
	/* dump Power_Status(Power_check), SDIO not support */

	/* dump bgf_bt_debug_log(BGFSYS_status) */
	btmtk_sdio_dump_bgf_bt_debug_log(bdev);
}

static void btmtk_sdio_dump_debug_sop_no_response(struct btmtk_dev *bdev)
{
	/* dump all bus timeout CR, only for PCIE, USB/SDIO not support */

	/* dump bgf_bt_debug_log(BGFSYS_status) */
	btmtk_sdio_dump_bgf_bt_debug_log(bdev);
}

static void btmtk_sdio_dump_debug_sop(struct btmtk_dev *bdev, int type)
{
	switch (type) {
	case DEBUG_SOP_SLEEP:
		btmtk_sdio_dump_debug_sop_sleep(bdev);
		break;
	case DEBUG_SOP_WAKEUP:
		btmtk_sdio_dump_debug_sop_wakeup(bdev);
		break;
	case DEBUG_SOP_NO_RESPONSE:
		btmtk_sdio_dump_debug_sop_no_response(bdev);
		break;
	default:
		BTMTK_INFO("%s type(%d) not support", __func__, type);
	}
}

static void btmtk_sdio_cif_mutex_lock(struct btmtk_dev *bdev)
{
	SDIO_OPS_MUTEX_LOCK();
}

static void btmtk_sdio_cif_mutex_unlock(struct btmtk_dev *bdev)
{
	SDIO_OPS_MUTEX_UNLOCK();
}

void btmtk_sdio_set_no_fwn_own(struct btmtk_sdio_dev *cif_dev, int flag)
{
	if (cif_dev->no_fw_own != flag)
		BTMTK_INFO("%s set no_fw_own %d", __func__, flag);
	cif_dev->no_fw_own = flag;
}

static int btmtk_sdio_set_fw_own(struct btmtk_sdio_dev *cif_dev, int retry)
{
	/*Set fw own*/
	int ret = 0;
	u32 u32LoopCount = 0;
	u32 u32PollNum = 0;
	u32 u32CHLPCRValue = 0;
	u32 u32PD2HRM0RValue = 0;
	u32 ownValue = 0;
	u32 i = 0;
	u8 chlpcr_driver_own = 0;
	u8 pd2hrm0r_driver_own = 0;

	BTMTK_DBG("%s", __func__);

	if (cif_dev->no_fw_own)
		return 0;

	SDIO_OWN_MUTEX_LOCK();
	atomic_set(&cif_dev->fw_own_timer_flag, FW_OWN_TIMER_CANCEL);

	/* For CHLPCR, bit 8 could help us to check driver own or fw own
	 * 0: COM driver doesn't have ownership
	 * 1: COM driver has ownership
	 */
	ret = btmtk_sdio_readl(CHLPCR, &u32CHLPCRValue, cif_dev->func);
	chlpcr_driver_own = ((u32CHLPCRValue & 0x100) == 0x100) ? 1 : 0;

	if (cif_dev->patched == 1) {
		ret = btmtk_sdio_readl(PD2HRM0R, &u32PD2HRM0RValue, cif_dev->func);
		pd2hrm0r_driver_own =
			((u32PD2HRM0RValue & PD2HRM0R_DRIVER_OWN)
				== PD2HRM0R_DRIVER_OWN) ? 1 : 0;
	} else {
		pd2hrm0r_driver_own = 0;
	}

	BTMTK_DBG("CHLPCR: 0x%0x, PD2HRM0R: 0x%0x",
		u32CHLPCRValue, u32PD2HRM0RValue);

	if (!chlpcr_driver_own && !pd2hrm0r_driver_own) {
		ret = 0;
		goto unlock;
	}

	ownValue = 0x00000100;
retry_own:
	if (cif_dev->patched == 1) {
		/* write CSICR to notify FW to set PD2HRM0R to 0 */
		ret = btmtk_sdio_writel(CSICR, 1, cif_dev->func);
		if (ret) {
			ret = -EINVAL;
			goto done;
		}

		ret = btmtk_sdio_writel(CSICR, 0xC0, cif_dev->func);
		if (ret) {
			ret = -EINVAL;
			goto done;
		}

		u32LoopCount = SET_OWN_LOOP_COUNT;
		pd2hrm0r_driver_own = 0;
		do {
			usleep_range(200, 300);
			u32LoopCount--;
			u32PollNum++;
			ret = btmtk_sdio_readl(PD2HRM0R, &u32PD2HRM0RValue, cif_dev->func);
			BTMTK_DBG("%s set driver own PD2HRM0R = 0x%0x", __func__, u32PD2HRM0RValue);
			pd2hrm0r_driver_own =
				((u32PD2HRM0RValue & PD2HRM0R_DRIVER_OWN)
					== PD2HRM0R_DRIVER_OWN) ? 1 : 0;
		} while ((u32LoopCount > 0) && pd2hrm0r_driver_own);

		if (pd2hrm0r_driver_own) {
			if (retry > 0) {
				BTMTK_WARN("%s retry set_check fw own(%d), PD2HRM0R:0x%x",
					__func__, u32PollNum, u32PD2HRM0RValue);
				for (i = 0; i < 3; i++)
					DUMP_FW_PC(cif_dev);

				retry--;
				mdelay(5);
				goto retry_own;
			} else {
				ret = -EINVAL;
			}
		}
	}

	/* Write CR for Driver or FW own */
	if (ret == 0) {
		ret = btmtk_sdio_writel(CHLPCR, ownValue, cif_dev->func);
		if (ret) {
			ret = -EINVAL;
			goto done;
		}
	}
done:
#if BTMTK_SDIO_DEBUG
	fw_own_cnt++;
#endif
	if (ret) {
		BTMTK_ERR("%s set FW own fail", __func__);
		btmtk_sdio_dump_debug_sop(cif_dev->bdev, DEBUG_SOP_SLEEP);
	} else
		BTMTK_DBG("%s set FW own success", __func__);

unlock:
	SDIO_OWN_MUTEX_UNLOCK();
	return ret;
}

#if (KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE)
static void btmtk_fw_own_timer(unsigned long arg)
{
	struct btmtk_sdio_dev *cif_dev = (struct btmtk_sdio_dev *)arg;

	atomic_set(&cif_dev->fw_own_timer_flag, FW_OWN_TIMER_RUNNING);
	wake_up_interruptible(&cif_dev->sdio_thread.wait_q);
}
#else
static void btmtk_fw_own_timer(struct timer_list *timer)
{
	struct btmtk_sdio_dev *cif_dev = from_timer(cif_dev, timer, fw_own_timer);

	atomic_set(&cif_dev->fw_own_timer_flag, FW_OWN_TIMER_RUNNING);
	wake_up_interruptible(&cif_dev->sdio_thread.wait_q);
}
#endif

static void btmtk_sdio_create_fw_own_timer(struct btmtk_sdio_dev *cif_dev)
{
	if (atomic_read(&cif_dev->fw_own_timer_flag) == FW_OWN_TIMER_CANCEL) {
		BTMTK_DBG("%s: create fw own timer", __func__);
		atomic_set(&cif_dev->fw_own_timer_flag, FW_OWN_TIMER_INIT);
#if (KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE)
		init_timer(&cif_dev->fw_own_timer);
		cif_dev->fw_own_timer.function = btmtk_fw_own_timer;
		cif_dev->fw_own_timer.data = (unsigned long)cif_dev;
#else
		timer_setup(&cif_dev->fw_own_timer, btmtk_fw_own_timer, 0);
#endif
	}
	mod_timer(&cif_dev->fw_own_timer, jiffies + FW_OWN_TIMEOUT * HZ / 1000);
}

static int btmtk_sdio_set_driver_own(struct btmtk_sdio_dev *cif_dev, int retry)
{
	/*Set driver own*/
	int ret = 0;
	u32 u32LoopCount = 0;
	u32 u32PollNum = 0;
	u32 u32CHLPCRValue = 0;
	u32 u32PD2HRM0RValue = 0;
	u32 ownValue = 0;
	u32 i = 0;
	u8 chlpcr_driver_own = 0;
	u8 pd2hrm0r_driver_own = 0;

	BTMTK_DBG("%s", __func__);

	if (cif_dev->no_fw_own == 0)
		btmtk_sdio_create_fw_own_timer(cif_dev);

	SDIO_OWN_MUTEX_LOCK();
	/* For CHLPCR, bit 8 could help us to check driver own or fw own
	 * 0: COM driver doesn't have ownership
	 * 1: COM driver has ownership
	 */
	ret = btmtk_sdio_readl(CHLPCR, &u32CHLPCRValue, cif_dev->func);
	chlpcr_driver_own = ((u32CHLPCRValue & 0x100) == 0x100) ? 1 : 0;

	if (cif_dev->patched == 1) {
		ret = btmtk_sdio_readl(PD2HRM0R, &u32PD2HRM0RValue, cif_dev->func);
		pd2hrm0r_driver_own =
			((u32PD2HRM0RValue & PD2HRM0R_DRIVER_OWN)
				== PD2HRM0R_DRIVER_OWN) ? 1 : 0;
	} else {
		pd2hrm0r_driver_own = 1;
	}

	BTMTK_DBG("CHLPCR: 0x%0x, PD2HRM0R: 0x%0x",
		u32CHLPCRValue, u32PD2HRM0RValue);

	if (chlpcr_driver_own && pd2hrm0r_driver_own) {
		ret = 0;
		goto unlock;
	}

	ownValue = 0x00000200;
retry_own:
	/* Write CR for Driver or FW own */
	ret = btmtk_sdio_writel(CHLPCR, ownValue, cif_dev->func);
	if (ret) {
		ret = -EINVAL;
		goto done;
	}

	u32LoopCount = SET_OWN_LOOP_COUNT;
	do {
		usleep_range(200, 300);
		u32LoopCount--;
		u32PollNum++;
		ret = btmtk_sdio_readl(CHLPCR, &u32CHLPCRValue, cif_dev->func);
		BTMTK_DBG("%s set driver own CHLPCR = 0x%0x", __func__, u32CHLPCRValue);
		chlpcr_driver_own = ((u32CHLPCRValue & 0x100) == 0x100) ? 1 : 0;
		if (cif_dev->patched == 1) {
			ret = btmtk_sdio_readl(PD2HRM0R, &u32PD2HRM0RValue, cif_dev->func);
			BTMTK_DBG("%s set driver own PD2HRM0R = 0x%0x", __func__, u32PD2HRM0RValue);
			pd2hrm0r_driver_own =
				((u32PD2HRM0RValue & PD2HRM0R_DRIVER_OWN)
					== PD2HRM0R_DRIVER_OWN) ? 1 : 0;
		} else {
			pd2hrm0r_driver_own = 1;
		}
	} while ((u32LoopCount > 0) && (!chlpcr_driver_own || !pd2hrm0r_driver_own));

	if (!chlpcr_driver_own || !pd2hrm0r_driver_own) {
		if (retry > 0) {
			BTMTK_WARN("%s retry set_check driver own(%d), CHLPCR:0x%x, PD2HRM0R:0x%x",
				__func__, u32PollNum, u32CHLPCRValue, u32PD2HRM0RValue);
			for (i = 0; i < 3; i++)
				DUMP_FW_PC(cif_dev);

			retry--;
			mdelay(5);
			goto retry_own;
		} else {
			ret = -EINVAL;
		}
	}
done:

#if BTMTK_SDIO_DEBUG
	driver_own_cnt++;
#endif
	if (ret) {
		BTMTK_ERR("%s set driver own fail", __func__);
		for (i = 0; i < 8; i++) {
			DUMP_FW_PC(cif_dev);
			msleep(200);
		}
		btmtk_sdio_dump_debug_sop(cif_dev->bdev, DEBUG_SOP_WAKEUP);
	} else
		BTMTK_DBG("%s set driver own success", __func__);
unlock:
	SDIO_OWN_MUTEX_UNLOCK();

	return ret;
}

static int btmtk_sdio_keep_driver_own(struct btmtk_sdio_dev *cif_dev, int enable)
{
	int ret = 0;

	if (enable == 1) {
		btmtk_sdio_set_no_fwn_own(cif_dev, 1);
		ret = btmtk_sdio_set_driver_own(cif_dev, RETRY_TIMES);
	} else {
		btmtk_sdio_set_no_fwn_own(cif_dev, 0);
		ret = btmtk_sdio_set_fw_own(cif_dev, RETRY_TIMES);
	}
	return ret;
}

#if 0
int btmtk_sdio_set_own_back(struct btmtk_sdio_dev *cif_dev, int owntype, int retry)
{
	/*Set driver own*/
	int ret = 0;
	u32 u32LoopCount = 0;
	u32 u32PollNum = 0;
	u32 u32CHLPCRValue = 0;
	u32 u32PD2HRM0RValue = 0;
	u32 ownValue = 0;
	u32 i = 0;
	u8 chlpcr_driver_own = 0;
	u8 pd2hrm0r_driver_own = 0;

	BTMTK_DBG("%s owntype %d", __func__, owntype);

	if (owntype == FW_OWN) {
		if (cif_dev->no_fw_own)
			return 0;
	} else if (owntype != DRIVER_OWN) {
		BTMTK_ERR("%s unknown type %d", __func__, owntype);
		return -EINVAL;
	}

	SDIO_OWN_MUTEX_LOCK();
	/* For CHLPCR, bit 8 could help us to check driver own or fw own
	 * 0: COM driver doesn't have ownership
	 * 1: COM driver has ownership
	 */
	ret = btmtk_sdio_readl(CHLPCR, &u32CHLPCRValue, cif_dev->func);
	chlpcr_driver_own = ((u32CHLPCRValue & 0x100) == 0x100) ? 1 : 0;

	if (cif_dev->patched == 1) {
		ret = btmtk_sdio_readl(PD2HRM0R, &u32PD2HRM0RValue, cif_dev->func);
		pd2hrm0r_driver_own =
			((u32PD2HRM0RValue & PD2HRM0R_DRIVER_OWN)
				== PD2HRM0R_DRIVER_OWN) ? 1 : 0;
	} else {
		pd2hrm0r_driver_own = (owntype == DRIVER_OWN) ? 1 : 0;
	}

	BTMTK_DBG("owntype: %d, CHLPCR: 0x%0x, PD2HRM0R: 0x%0x",
		owntype, u32CHLPCRValue, u32PD2HRM0RValue);

	if (owntype == DRIVER_OWN && chlpcr_driver_own && pd2hrm0r_driver_own) {
		ret = 0;
		goto unlock;
	} else if (owntype == FW_OWN && !chlpcr_driver_own && !pd2hrm0r_driver_own) {
		ret = 0;
		goto unlock;
	}

	if (owntype == DRIVER_OWN)
		ownValue = 0x00000200;
	else
		ownValue = 0x00000100;

retry_own:
	if (owntype == DRIVER_OWN) {
		/* Write CR for Driver or FW own */
		ret = btmtk_sdio_writel(CHLPCR, ownValue, cif_dev->func);
		if (ret) {
			ret = -EINVAL;
			goto done;
		}

		u32LoopCount = SET_OWN_LOOP_COUNT;
		do {
			usleep_range(100, 200);
			u32LoopCount--;
			u32PollNum++;
			ret = btmtk_sdio_readl(CHLPCR, &u32CHLPCRValue, cif_dev->func);
			BTMTK_DBG("%s set driver own CHLPCR = 0x%0x", __func__, u32CHLPCRValue);
			chlpcr_driver_own = ((u32CHLPCRValue & 0x100) == 0x100) ? 1 : 0;
			if (cif_dev->patched == 1) {
				ret = btmtk_sdio_readl(PD2HRM0R, &u32PD2HRM0RValue, cif_dev->func);
				BTMTK_DBG("%s set driver own PD2HRM0R = 0x%0x", __func__, u32PD2HRM0RValue);
				pd2hrm0r_driver_own =
					((u32PD2HRM0RValue & PD2HRM0R_DRIVER_OWN)
						== PD2HRM0R_DRIVER_OWN) ? 1 : 0;
			} else {
				pd2hrm0r_driver_own = (owntype == DRIVER_OWN) ? 1 : 0;
			}
		} while ((u32LoopCount > 0) && (!chlpcr_driver_own || !pd2hrm0r_driver_own));

		if (!chlpcr_driver_own || !pd2hrm0r_driver_own) {
			if (retry > 0) {
				BTMTK_WARN("%s retry set_check driver own(%d), CHLPCR:0x%x, PD2HRM0R:0x%x",
					__func__, u32PollNum, u32CHLPCRValue, u32PD2HRM0RValue);
				for (i = 0; i < 3; i++)
					DUMP_FW_PC(cif_dev);

				retry--;
				mdelay(5);
				goto retry_own;
			} else {
				ret = -EINVAL;
			}
		}
	} else {
		if (cif_dev->patched == 1) {
			/* write CSICR to notify FW to set PD2HRM0R to 0 */
			ret = btmtk_sdio_writel(CSICR, 1, cif_dev->func);
			if (ret) {
				ret = -EINVAL;
				goto done;
			}

			ret = btmtk_sdio_writel(CSICR, 0xC0, cif_dev->func);
			if (ret) {
				ret = -EINVAL;
				goto done;
			}

			u32LoopCount = SET_OWN_LOOP_COUNT;
			pd2hrm0r_driver_own = 0;
			do {
				usleep_range(100, 200);
				u32LoopCount--;
				u32PollNum++;
				ret = btmtk_sdio_readl(PD2HRM0R, &u32PD2HRM0RValue, cif_dev->func);
				BTMTK_DBG("%s set driver own PD2HRM0R = 0x%0x", __func__, u32PD2HRM0RValue);
				pd2hrm0r_driver_own =
					((u32PD2HRM0RValue & PD2HRM0R_DRIVER_OWN)
						== PD2HRM0R_DRIVER_OWN) ? 1 : 0;
			} while ((u32LoopCount > 0) && pd2hrm0r_driver_own);

			if (pd2hrm0r_driver_own) {
				if (retry > 0) {
					BTMTK_WARN("%s retry set_check fw own(%d), PD2HRM0R:0x%x",
						__func__, u32PollNum, u32PD2HRM0RValue);
					for (i = 0; i < 3; i++)
						DUMP_FW_PC(cif_dev);

					retry--;
					mdelay(5);
					goto retry_own;
				} else {
					ret = -EINVAL;
				}
			}
		}

		/* Write CR for Driver or FW own */
		if (ret == 0) {
			ret = btmtk_sdio_writel(CHLPCR, ownValue, cif_dev->func);
			if (ret) {
				ret = -EINVAL;
				goto done;
			}
		}
	}
done:
	if (owntype == DRIVER_OWN) {
#if BTMTK_SDIO_DEBUG
		driver_own_cnt++;
#endif
		if (ret) {
			BTMTK_ERR("%s set driver own fail", __func__);
			for (i = 0; i < 8; i++) {
				DUMP_FW_PC(cif_dev);
				msleep(200);
			}
			btmtk_sdio_dump_debug_sop(cif_dev->bdev, DEBUG_SOP_WAKEUP);
		} else
			BTMTK_DBG("%s set driver own success", __func__);
	} else {
#if BTMTK_SDIO_DEBUG
		fw_own_cnt++;
#endif
		if (ret) {
			BTMTK_ERR("%s set FW own fail", __func__);
			btmtk_sdio_dump_debug_sop(cif_dev->bdev, DEBUG_SOP_SLEEP);
		} else
			BTMTK_DBG("%s set FW own success", __func__);
	}

unlock:
	SDIO_OWN_MUTEX_UNLOCK();
	return ret;
}
#endif

static int btmtk_sdio_read_register(struct btmtk_dev *bdev, u32 reg, u32 *val)
{
	int ret;
	u8 cmd[READ_REGISTER_CMD_LEN] = {0x01, 0x6F, 0xFC, 0x0C,
				0x01, 0x08, 0x08, 0x00,
				0x02, 0x01, 0x00, 0x01,
				0x00, 0x00, 0x00, 0x00};

	u8 event[READ_REGISTER_EVT_HDR_LEN] = {0x04, 0xE4, 0x10, 0x02,
			0x08, 0x0C, 0x00, 0x00,
			0x00, 0x00, 0x01};

	/* To-do using structure for sdio header
	 * struct btmtk_sdio_hdr *sdio_hdr;
	 * sdio_hdr = (void *) cmd;
	 * sdio_hdr->len = cpu_to_le16(skb->len);
	 * sdio_hdr->reserved = cpu_to_le16(0);
	 * sdio_hdr->bt_type = hci_skb_pkt_type(skb);
	 */

	BTMTK_INFO("%s: read cr %x", __func__, reg);

	memcpy(&cmd[MCU_ADDRESS_OFFSET_CMD], &reg, sizeof(reg));

	ret = btmtk_main_send_cmd(bdev, cmd, READ_REGISTER_CMD_LEN, event, READ_REGISTER_EVT_HDR_LEN, DELAY_TIMES,
			RETRY_TIMES, BTMTK_TX_CMD_FROM_DRV);

	memcpy(val, bdev->io_buf + MCU_ADDRESS_OFFSET_EVT - HCI_TYPE_SIZE, sizeof(u32));
	*val = le32_to_cpu(*val);

	BTMTK_INFO("%s: reg=%x, value=0x%08x", __func__, reg, *val);

	return 0;
}

static int btmtk_sdio_write_register(struct btmtk_dev *bdev, u32 reg, u32 val)
{
	BTMTK_INFO("%s: reg=%x, value=0x%08x, not support", __func__, reg, val);
	return 0;
}

static int btmtk_cif_allocate_memory(struct btmtk_sdio_dev *cif_dev)
{
	int ret = -1;

	if (cif_dev->transfer_buf == NULL) {
		cif_dev->transfer_buf = kzalloc(URB_MAX_BUFFER_SIZE, GFP_KERNEL);
		if (!cif_dev->transfer_buf) {
			BTMTK_ERR("%s: alloc memory fail (bdev->transfer_buf)", __func__);
			goto end;
		}
	}

	if (cif_dev->sdio_packet == NULL) {
		cif_dev->sdio_packet = kzalloc(URB_MAX_BUFFER_SIZE, GFP_KERNEL);
		if (!cif_dev->sdio_packet) {
			BTMTK_ERR("%s: alloc memory fail (bdev->transfer_buf)", __func__);
			goto err;
		}
	}

	BTMTK_INFO("%s: Done", __func__);
	return 0;
err:
	kfree(cif_dev->transfer_buf);
	cif_dev->transfer_buf = NULL;
end:
	return ret;
}

static void btmtk_cif_free_memory(struct btmtk_sdio_dev *cif_dev)
{
	kfree(cif_dev->transfer_buf);
	cif_dev->transfer_buf = NULL;

	kfree(cif_dev->sdio_packet);
	cif_dev->sdio_packet = NULL;

	BTMTK_INFO("%s: Success", __func__);
}

int btmtk_sdio_read_wifi_mcu_pc(u8 PcLogSel, u32 *val)
{
	int ret = 0;
	unsigned int value = 0;

	if (!g_sdio_dev.func)
		return -EINVAL;

	SDIO_DEBUG_MUTEX_LOCK();

	ret = btmtk_sdio_readl(CONDBGCR_SEL, &value, g_sdio_dev.func);
	value |= SDIO_CTRL_EN;
	value &= WM_MONITER_SEL;
	value &= PC_MONITER_SEL;
	value = PC_IDX_SWH(value, PcLogSel);

	ret = btmtk_sdio_writel(CONDBGCR_SEL, value, g_sdio_dev.func);
	ret = btmtk_sdio_readl(CONDBGCR, val, g_sdio_dev.func);

	SDIO_DEBUG_MUTEX_UNLOCK();

	return 0;
}
EXPORT_SYMBOL(btmtk_sdio_read_wifi_mcu_pc);

int btmtk_sdio_read_bt_mcu_pc(u32 *val)
{
	if (!g_sdio_dev.func)
		return -EINVAL;

	SDIO_DEBUG_MUTEX_LOCK();

	btmtk_sdio_writel(0x30, 0xFD, g_sdio_dev.func);
	btmtk_sdio_readl(0x2c, val, g_sdio_dev.func);

	SDIO_DEBUG_MUTEX_UNLOCK();

	return 0;
}
EXPORT_SYMBOL(btmtk_sdio_read_bt_mcu_pc);

int btmtk_sdio_read_conn_infra_pc(u32 *val)
{
	if (!g_sdio_dev.func)
		return -EINVAL;

	SDIO_DEBUG_MUTEX_LOCK();

	btmtk_sdio_writel(0x3C, 0x9F1E0000, g_sdio_dev.func);
	btmtk_sdio_readl(0x38, val, g_sdio_dev.func);

	SDIO_DEBUG_MUTEX_UNLOCK();

	return 0;
}
EXPORT_SYMBOL(btmtk_sdio_read_conn_infra_pc);

typedef bool (*wifi_driver_own)(uint8_t enable);
static wifi_driver_own wifi_driver_own_ptr;
static void btmtk_sdio_set_wifi_driver_own(uint8_t enable)
{
	if (!wifi_driver_own_ptr)
		wifi_driver_own_ptr =
			(wifi_driver_own)btmtk_kallsyms_lookup_name("halPreventFwOwnEn");

	if (wifi_driver_own_ptr) {
		BTMTK_INFO("%s set wifi own to %d", __func__, enable);
		wifi_driver_own_ptr(enable);
	} else {
		BTMTK_INFO("%s wifi_driver_own_ptr is NULL", __func__);
	}
}

int btmtk_sdio_set_driver_own_for_subsys_reset(int enable)
{
	if (!g_sdio_dev.func)
		return -ENODEV;

	BTMTK_INFO("%s enter! enable = %d", __func__, enable);
	if (enable == 1) {
		btmtk_sdio_set_no_fwn_own(&g_sdio_dev, 1);
		return 0;
	} else if (enable == 0) {
		btmtk_sdio_set_no_fwn_own(&g_sdio_dev, 0);
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(btmtk_sdio_set_driver_own_for_subsys_reset);

static int btmtk_sdio_open(struct hci_dev *hdev)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_sdio_dev *cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

	BTMTK_INFO("%s enter!", __func__);
	skb_queue_purge(&cif_dev->tx_queue);

#if BTMTK_SDIO_DEBUG
	rx_done_cnt = 0;
	tx_empty_cnt = 0;
	intr_cnt = 0;
	driver_own_cnt = 0;
	fw_own_cnt = 0;
#endif

	return 0;
}

static int btmtk_sdio_close(struct hci_dev *hdev)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	BTMTK_INFO("%s enter!", __func__);
	cancel_work_sync(&bdev->reset_waker);
	return 0;
}

static void btmtk_sdio_open_done(struct btmtk_dev *bdev)
{
	struct btmtk_sdio_dev *cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

	BTMTK_INFO("%s enter!", __func__);
	(void)btmtk_buffer_mode_send(cif_dev->buffer_mode);
}

static int btmtk_sdio_writesb(u32 offset, u8 *val, int len, struct sdio_func *func)
{
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("%s func is NULL", __func__);
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		ret = sdio_writesb(func, offset, val, len);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d", __func__, ret);
			break;
		}
	} while (ret);

	return ret;
}

static int btmtk_sdio_readsb(u32 offset, u8 *val, int len, struct sdio_func *func)
{
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("%s func is NULL", __func__);
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		ret = sdio_readsb(func, val, offset, len);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d", __func__, ret);
			break;
		}
	} while (ret);

	return ret;
}

int btmtk_sdio_writeb(u32 offset, u8 val, struct sdio_func *func)
{
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("%s func is NULL", __func__);
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		sdio_writeb(func, val, offset, &ret);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d", __func__, ret);
			break;
		}
	} while (ret);

	return ret;
}

static int btmtk_sdio_writel(u32 offset, u32 val, struct sdio_func *func)
{
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("%s func is NULL", __func__);
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		sdio_writel(func, val, offset, &ret);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d", __func__, ret);
			break;
		}
	} while (ret);

	return ret;
}

static int btmtk_sdio_readl(u32 offset,  u32 *val, struct sdio_func *func)
{
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("func is NULL");
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		*val = sdio_readl(func, offset, &ret);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d", __func__, ret);
			break;
		}
	} while (ret);

	return ret;
}

static int btmtk_sdio_readb(u32 offset, u8 *val, struct sdio_func *func)
{
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("%s func is NULL", __func__);
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		*val = sdio_readb(func, offset, &ret);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d", __func__, ret);
			break;
		}
	} while (ret);

	return ret;
}

static void btmtk_sdio_print_debug_sr(struct btmtk_sdio_dev *cif_dev)
{
	u32 ret = 0;
	u32 CCIR_Value = 0;
	u32 CHLPCR_Value = 0;
	u32 CSDIOCSR_Value = 0;
	u32 CHISR_Value = 0;
	u32 CHIER_Value = 0;
	u32 CTFSR_Value = 0;
	u32 CRPLR_Value = 0;
	u32 SWPCDBGR_Value = 0;
	unsigned char X0_Value = 0;
	unsigned char X4_Value = 0;
	unsigned char X5_Value = 0;
	unsigned char F8_Value = 0;
	unsigned char F9_Value = 0;
	unsigned char FA_Value = 0;
	unsigned char FB_Value = 0;
	unsigned char FC_Value = 0;
	unsigned char FD_Value = 0;
	unsigned char FE_Value = 0;
	unsigned char FF_Value = 0;

	ret = btmtk_sdio_readl(CCIR, &CCIR_Value, cif_dev->func);
	ret = btmtk_sdio_readl(CHLPCR, &CHLPCR_Value, cif_dev->func);
	ret = btmtk_sdio_readl(CSDIOCSR, &CSDIOCSR_Value, cif_dev->func);
	ret = btmtk_sdio_readl(CHISR, &CHISR_Value, cif_dev->func);
	ret = btmtk_sdio_readl(CHIER, &CHIER_Value, cif_dev->func);
	ret = btmtk_sdio_readl(CTFSR, &CTFSR_Value, cif_dev->func);
	ret = btmtk_sdio_readl(CRPLR, &CRPLR_Value, cif_dev->func);
	ret = btmtk_sdio_readl(SWPCDBGR, &SWPCDBGR_Value, cif_dev->func);
	sdio_claim_host(cif_dev->func);
	X0_Value = sdio_f0_readb(cif_dev->func, 0x00, &ret);
	X4_Value = sdio_f0_readb(cif_dev->func, 0x04, &ret);
	X5_Value = sdio_f0_readb(cif_dev->func, 0x05, &ret);
	F8_Value = sdio_f0_readb(cif_dev->func, 0xF8, &ret);
	F9_Value = sdio_f0_readb(cif_dev->func, 0xF9, &ret);
	FA_Value = sdio_f0_readb(cif_dev->func, 0xFA, &ret);
	FB_Value = sdio_f0_readb(cif_dev->func, 0xFB, &ret);
	FC_Value = sdio_f0_readb(cif_dev->func, 0xFC, &ret);
	FD_Value = sdio_f0_readb(cif_dev->func, 0xFD, &ret);
	FE_Value = sdio_f0_readb(cif_dev->func, 0xFE, &ret);
	FF_Value = sdio_f0_readb(cif_dev->func, 0xFF, &ret);
	sdio_release_host(cif_dev->func);
	BTMTK_INFO("CCIR: 0x%x, CHLPCR: 0x%x, CSDIOCSR: 0x%x, CHISR: 0x%x",
		CCIR_Value, CHLPCR_Value, CSDIOCSR_Value, CHISR_Value);
	BTMTK_INFO("CHIER: 0x%x, CTFSR: 0x%x, CRPLR: 0x%x, SWPCDBGR: 0x%x",
		CHIER_Value, CTFSR_Value, CRPLR_Value, SWPCDBGR_Value);
	BTMTK_INFO("CCCR 00: 0x%x, 04: 0x%x, 05: 0x%x",
		X0_Value, X4_Value, X5_Value);
	BTMTK_INFO("F8: 0x%x, F9: 0x%x, FA: 0x%x, FB: 0x%x",
		F8_Value, F9_Value, FA_Value, FB_Value);
	BTMTK_INFO("FC: 0x%x, FD: 0x%x, FE: 0x%x, FF: 0x%x",
		FC_Value, FD_Value, FE_Value, FF_Value);
}

static int btmtk_sdio_enable_interrupt(int enable, struct sdio_func *func)
{
	u32 ret = 0;
	u32 cr_value = 0;

	BTMTK_DBG("%s enable=%d", __func__, enable);
	if (enable)
		cr_value |= C_FW_INT_EN_SET;
	else
		cr_value |= C_FW_INT_EN_CLEAR;

	ret = btmtk_sdio_writel(CHLPCR, cr_value, func);

	return ret;
}

int btmtk_sdio_send_cmd(struct btmtk_dev *bdev, struct sk_buff *skb,
		int delay, int retry, int pkt_type)
{
	int ret = 0;
	u32 crAddr = 0, crValue = 0;
	ulong flags;
	struct btmtk_sdio_dev *cif_dev = NULL;

	/* for fw assert */
	u8 fw_assert_cmd[FW_ASSERT_CMD_LEN] = { 0x01, 0x5B, 0xFD, 0x00 };
	u8 fw_assert_cmd1[FW_ASSERT_CMD1_LEN] = { 0x01, 0x6F, 0xFC, 0x05, 0x01, 0x02, 0x01, 0x00, 0x08 };
	/* for read/write CR */
	u8 notify_alt_evt[NOTIFY_ALT_EVT_LEN] = {0x04, 0x0E, 0x04, 0x01, 0x03, 0x0c, 0x00};
	struct sk_buff *evt_skb;

	if (bdev == NULL) {
		BTMTK_ERR("bdev is NULL");
		ret = -1;
		kfree_skb(skb);
		skb = NULL;
		goto exit;
	}
	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;
	if (cif_dev == NULL) {
		BTMTK_ERR("cif_dev is NULL, bdev=%p", bdev);
		ret = -1;
		kfree_skb(skb);
		skb = NULL;
		goto exit;
	}

	/* For read write CR */
	if (skb->len > 9) {
		if (skb->data[0] == 0x01 && skb->data[1] == 0x6f && skb->data[2] == 0xfc &&
				skb->data[3] == 0x0D && skb->data[4] == 0x01 &&
				skb->data[5] == 0xff && skb->data[6] == 0x09 &&
				skb->data[7] == 0x00 && skb->data[8] == 0x02) {
			crAddr = ((skb->data[9] & 0xff) << 24) + ((skb->data[10] & 0xff) << 16)
				+ ((skb->data[11] & 0xff) << 8) + (skb->data[12] & 0xff);
			crValue = ((skb->data[13] & 0xff) << 24) + ((skb->data[14] & 0xff) << 16)
				+ ((skb->data[15] & 0xff) << 8) + (skb->data[16] & 0xff);

			BTMTK_INFO("%s crAddr = 0x%08x crValue = 0x%08x",
				__func__, crAddr, crValue);

			btmtk_sdio_writel(crAddr, crValue, cif_dev->func);
			evt_skb = skb_copy(skb, GFP_KERNEL);
			if (evt_skb) {
				bt_cb(evt_skb)->pkt_type = notify_alt_evt[0];
				notify_alt_evt[3] = (crValue & 0xFF000000) >> 24;
				notify_alt_evt[4] = (crValue & 0x00FF0000) >> 16;
				notify_alt_evt[5] = (crValue & 0x0000FF00) >> 8;
				notify_alt_evt[6] = (crValue & 0x000000FF);
				memcpy(evt_skb->data, &notify_alt_evt[1], NOTIFY_ALT_EVT_LEN - 1);
				evt_skb->len = NOTIFY_ALT_EVT_LEN - 1;
				hci_recv_frame(bdev->hdev, evt_skb);
				kfree_skb(skb);
				skb = NULL;
			} else {
				BTMTK_ERR("%s skb_copy failed", __func__);
			}
			goto exit;
		} else if (skb->data[0] == 0x01 && skb->data[1] == 0x6f && skb->data[2] == 0xfc &&
				skb->data[3] == 0x09 && skb->data[4] == 0x01 &&
				skb->data[5] == 0xff && skb->data[6] == 0x05 &&
				skb->data[7] == 0x00 && skb->data[8] == 0x01) {

			crAddr = ((skb->data[9] & 0xff) << 24) +
				((skb->data[10] & 0xff) << 16) +
				((skb->data[11] & 0xff) << 8) +
				(skb->data[12] & 0xff);

			btmtk_sdio_readl(crAddr, &crValue, cif_dev->func);
			BTMTK_INFO("%s read crAddr = 0x%08x crValue = 0x%08x",
					__func__, crAddr, crValue);
			evt_skb = skb_copy(skb, GFP_KERNEL);
			if (evt_skb) {
				bt_cb(evt_skb)->pkt_type = notify_alt_evt[0];
				/* memcpy(&notify_alt_evt[2], &crValue, sizeof(crValue)); */
				notify_alt_evt[3] = (crValue & 0xFF000000) >> 24;
				notify_alt_evt[4] = (crValue & 0x00FF0000) >> 16;
				notify_alt_evt[5] = (crValue & 0x0000FF00) >> 8;
				notify_alt_evt[6] = (crValue & 0x000000FF);
				memcpy(evt_skb->data, &notify_alt_evt[1], NOTIFY_ALT_EVT_LEN - 1);
				evt_skb->len = NOTIFY_ALT_EVT_LEN - 1;
				hci_recv_frame(bdev->hdev, evt_skb);
				kfree_skb(skb);
				skb = NULL;
			} else {
				BTMTK_ERR("%s skb_copy failed", __func__);
			}
			goto exit;
		}
	}

	if (skb->data[0] == MTK_HCI_ACLDATA_PKT && skb->data[1] == 0x00 && skb->data[2] == 0x44) {
		/* it's for ble iso, remove speicific header
		 * 02 00 44 len len + payload to 05 + payload
		 */
		skb_pull(skb, 4);
		skb->data[0] = HCI_ISO_PKT;
	}

	if ((skb->len == FW_ASSERT_CMD_LEN &&
		!memcmp(skb->data, fw_assert_cmd, FW_ASSERT_CMD_LEN))
		|| (skb->len == FW_ASSERT_CMD1_LEN &&
		!memcmp(skb->data, fw_assert_cmd1, FW_ASSERT_CMD1_LEN))) {
		BTMTK_INFO_RAW(skb->data, skb->len, "%s: Trigger FW assert, dump CR", __func__);
#if BTMTK_SDIO_DEBUG
		rx_debug_print();
#endif
		btmtk_sdio_keep_driver_own(cif_dev, 1);
		btmtk_sdio_print_debug_sr(cif_dev);
	}

	spin_lock_irqsave(&bdev->txlock, flags);
	skb_queue_tail(&cif_dev->tx_queue, skb);
	spin_unlock_irqrestore(&bdev->txlock, flags);
	wake_up_interruptible(&cif_dev->sdio_thread.wait_q);

exit:
	return ret;
}

static int btmtk_cif_recv_evt(struct btmtk_dev *bdev)
{
	int ret = 0;
	u32 u32ReadCRValue = 0;
	u32 u32ReadCRLEN = 0;
	u32 sdio_header_length = 0;
	int rx_length = 0;
	int payload = 0;
	u16 hci_pkt_len = 0;
	u8 hci_type = 0;
	struct btmtk_sdio_dev *cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

	memset(bdev->io_buf, 0, IO_BUF_SIZE);

	/* keep polling method */
	/* If interrupt method is working, we can remove it */
	ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue, cif_dev->func);
	BTMTK_DBG("%s: loop Get CHISR 0x%08X",
		__func__, u32ReadCRValue);
#if BTMTK_SDIO_DEBUG
	rx_debug_save(CHISR_r_2, u32ReadCRValue, NULL);
#endif

	ret = btmtk_sdio_readl(CRPLR, &u32ReadCRLEN, cif_dev->func);
#if BTMTK_SDIO_DEBUG
	rx_debug_save(CRPLR_r, u32ReadCRLEN, NULL);
#endif

	rx_length = (u32ReadCRLEN & RX_PKT_LEN) >> 16;
	if (rx_length == 0xFFFF || rx_length == 0) {
		BTMTK_WARN("%s: rx_length = %d, error return -EIO", __func__, rx_length);
		return -EIO;
	}

	BTMTK_DBG("%s: u32ReadCRValue = %08X", __func__, u32ReadCRValue);
	u32ReadCRValue &= 0xFFFB;
	ret = btmtk_sdio_writel(CHISR, u32ReadCRValue, cif_dev->func);
	BTMTK_DBG("%s: write = %08X", __func__, u32ReadCRValue);
	ret = btmtk_sdio_readl(PD2HRM0R, &u32ReadCRValue, cif_dev->func);
#if BTMTK_SDIO_DEBUG
	rx_debug_save(PD2HRM0R_r, u32ReadCRValue, NULL);
#endif

	ret = btmtk_sdio_readsb(CRDR, cif_dev->transfer_buf, rx_length, cif_dev->func);
#if BTMTK_SDIO_DEBUG
	rx_debug_save(RX_BUF, 0, cif_dev->transfer_buf);
#endif
	sdio_header_length = (cif_dev->transfer_buf[1] << 8);
	sdio_header_length |= cif_dev->transfer_buf[0];
	if (sdio_header_length != rx_length) {
		BTMTK_ERR("%s sdio header length %d, rx_length %d mismatch, trigger assert",
			__func__, sdio_header_length, rx_length);
		BTMTK_INFO_RAW(cif_dev->transfer_buf, rx_length, "%s: raw data is :", __func__);
		btmtk_send_assert_cmd(bdev);
		return -EIO;
	}

	BTMTK_DBG_RAW(cif_dev->transfer_buf, rx_length, "%s: raw data is :", __func__);

	hci_type = cif_dev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE];
	switch (hci_type) {
	/* Please reference hci header format
	 * A = len
	 * acl : 02 xx xx AA AA + payload
	 * sco : 03 xx xx AA + payload
	 * evt : 04 xx AA + payload
	 * ISO : 05 xx xx AA AA + payload
	 */
	case HCI_ACLDATA_PKT:
		hci_pkt_len = cif_dev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 3] +
						(cif_dev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 4] << 8) + 5;
		break;
	case HCI_SCODATA_PKT:
		hci_pkt_len = cif_dev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 4] + 4;
		break;
	case HCI_EVENT_PKT:
		hci_pkt_len = cif_dev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 2] + 3;
		break;
	case HCI_ISO_PKT:
		hci_pkt_len = cif_dev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 3] +
						(cif_dev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 4] << 8) + 4;
		bdev->io_buf[0] = HCI_ACLDATA_PKT;
		bdev->io_buf[1] = 0x00;
		bdev->io_buf[2] = 0x44;
		bdev->io_buf[3] = (hci_pkt_len & 0x00ff);
		bdev->io_buf[4] = ((hci_pkt_len & 0xff00) >> 8);
		memcpy(bdev->io_buf + 5, cif_dev->transfer_buf + MTK_SDIO_PACKET_HEADER_SIZE + 1, hci_pkt_len);
		memset(cif_dev->transfer_buf, 0, URB_MAX_BUFFER_SIZE);
		hci_pkt_len += 5;
		memcpy(cif_dev->transfer_buf + MTK_SDIO_PACKET_HEADER_SIZE, bdev->io_buf, hci_pkt_len);
		BTMTK_DBG_RAW(cif_dev->transfer_buf, hci_pkt_len, "%s: raw data is :", __func__);
		break;
	}
	ret = hci_pkt_len;
	bdev->recv_evt_len = hci_pkt_len;

	BTMTK_DBG("%s sdio header length %d, rx_length %d, hci_pkt_len = %d",
			__func__, sdio_header_length, rx_length, hci_pkt_len);
	ret = btmtk_recv(bdev->hdev, cif_dev->transfer_buf + MTK_SDIO_PACKET_HEADER_SIZE, hci_pkt_len);
	if (cif_dev->transfer_buf[4] == HCI_EVENT_PKT) {
		payload = rx_length - cif_dev->transfer_buf[6] - 3;
		ret = rx_length - MTK_SDIO_PACKET_HEADER_SIZE - payload;
	}

	BTMTK_DBG("%s: done", __func__);
	return ret;
}

int btmtk_sdio_event_filter(struct btmtk_dev *bdev, struct sk_buff *skb)
{
	const u8 read_address_event[READ_ADDRESS_EVT_HDR_LEN] = { 0x4, 0x0E, 0x0A, 0x01, 0x09, 0x10, 0x00 };

	if (event_compare_status == BTMTK_EVENT_COMPARE_STATE_NEED_COMPARE &&
		skb->len >= event_need_compare_len) {
		if (memcmp(skb->data, &read_address_event[1], READ_ADDRESS_EVT_HDR_LEN - 1) == 0
			&& (skb->len == (READ_ADDRESS_EVT_HDR_LEN - 1 + BD_ADDRESS_SIZE))) {
			memcpy(bdev->bdaddr, &skb->data[READ_ADDRESS_EVT_PAYLOAD_OFFSET - 1], BD_ADDRESS_SIZE);
			BTMTK_INFO("GET BDADDR = %02X:%02X:%02X:%02X:%02X:%02X",
				bdev->bdaddr[0], bdev->bdaddr[1], bdev->bdaddr[2],
				bdev->bdaddr[3], bdev->bdaddr[4], bdev->bdaddr[5]);

			event_compare_status = BTMTK_EVENT_COMPARE_STATE_COMPARE_SUCCESS;
		} else if (memcmp(skb->data, event_need_compare,
					event_need_compare_len) == 0) {
			/* if it is wobx debug event, just print in kernel log, drop it
			 * by driver, don't send to stack
			 */
			if (skb->data[0] == WOBLE_DEBUG_EVT_TYPE)
				BTMTK_INFO_RAW(skb->data, skb->len, "%s: wobx debug log:", __func__);

			/* If driver need to check result from skb, it can get from io_buf */
			/* Such as chip_id, fw_version, etc. */
			bdev->io_buf[0] = bt_cb(skb)->pkt_type;
			memcpy(&bdev->io_buf[1], skb->data, skb->len);
			event_compare_status = BTMTK_EVENT_COMPARE_STATE_COMPARE_SUCCESS;
			BTMTK_DBG("%s, compare success", __func__);
		} else {
			if (skb->data[0] != BLE_EVT_TYPE) {
				/* Don't care BLE event */
				BTMTK_INFO("%s compare fail", __func__);
				BTMTK_INFO_RAW(event_need_compare, event_need_compare_len,
					"%s: event_need_compare:", __func__);
				BTMTK_INFO_RAW(skb->data, skb->len, "%s: skb->data:", __func__);
			}
			return 0;
		}
		return 1;
	}
	return 0;
}

int btmtk_sdio_send_and_recv(struct btmtk_dev *bdev,
		struct sk_buff *skb,
		const uint8_t *event, const int event_len,
		int delay, int retry, int pkt_type)
{
	unsigned long comp_event_timo = 0, start_time = 0;
	int ret = -1;

	if (event) {
		if (event_len > EVENT_COMPARE_SIZE) {
			BTMTK_ERR("%s, event_len (%d) > EVENT_COMPARE_SIZE(%d), error",
				__func__, event_len, EVENT_COMPARE_SIZE);
			ret = -1;
			goto exit;
		}
		event_compare_status = BTMTK_EVENT_COMPARE_STATE_NEED_COMPARE;
		memcpy(event_need_compare, event + 1, event_len - 1);
		event_need_compare_len = event_len - 1;

		start_time = jiffies;
		/* check hci event /wmt event for SDIO/UART interface, check hci
		 * event for USB interface
		 */
		comp_event_timo = jiffies + msecs_to_jiffies(WOBLE_COMP_EVENT_TIMO);
		BTMTK_DBG("event_need_compare_len %d, event_compare_status %d",
			event_need_compare_len, event_compare_status);
	} else {
		event_compare_status = BTMTK_EVENT_COMPARE_STATE_COMPARE_SUCCESS;
	}

	BTMTK_DBG_RAW(skb->data, skb->len, "%s, send, len = %d", __func__, skb->len);

	ret = btmtk_sdio_send_cmd(bdev, skb, delay, retry, pkt_type);
	if (ret < 0) {
		BTMTK_ERR("%s btmtk_sdio_send_cmd failed!!", __func__);
		goto fw_assert;
	}

	do {
		/* check if event_compare_success */
		if (event_compare_status == BTMTK_EVENT_COMPARE_STATE_COMPARE_SUCCESS) {
			ret = 0;
			break;
		}
		usleep_range(10, 100);
	} while (time_before(jiffies, comp_event_timo));

	event_compare_status = BTMTK_EVENT_COMPARE_STATE_NOTHING_NEED_COMPARE;
	goto exit;
fw_assert:
	btmtk_send_assert_cmd(bdev);
exit:
	return ret;
}

static void btmtk_sdio_interrupt(struct sdio_func *func)
{
	struct btmtk_dev *bdev;
	struct btmtk_sdio_dev *cif_dev;

#if BTMTK_SDIO_DEBUG
	rx_debug_save(RX_TIMESTAMP, 0, NULL);
	intr_cnt++;
#endif

	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return;
	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

	btmtk_sdio_enable_interrupt(0, cif_dev->func);
	atomic_set(&cif_dev->int_count, 1);
	wake_up_interruptible(&cif_dev->sdio_thread.wait_q);
}

static int btmtk_sdio_load_fw_patch_using_dma(struct btmtk_dev *bdev, u8 *image,
		u8 *fwbuf, int section_dl_size, int section_offset)
{
	int cur_len = 0;
	int ret = -1;
	s32 sent_len = 0;
	s32 sdio_len = 0;
	u32 u32ReadCRValue = 0;
	u32 block_count = 0;
	u32 redundant = 0;
	u32 delay_count = 0;
	struct btmtk_sdio_dev *cif_dev = NULL;
	u8 cmd[LD_PATCH_CMD_LEN] = {0x02, 0x6F, 0xFC, 0x05, 0x00, 0x01, 0x01, 0x01, 0x00, PATCH_PHASE3};
	u8 event[LD_PATCH_EVT_LEN] = {0x04, 0xE4, 0x05, 0x02, 0x01, 0x01, 0x00, 0x00}; /* event[7] is status*/

	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

	if (bdev == NULL || image == NULL || fwbuf == NULL) {
		BTMTK_ERR("%s: invalid parameters!", __func__);
		return -1;
	}

	BTMTK_INFO("%s: loading rom patch... start", __func__);
	btmtk_sdio_enable_interrupt(0, cif_dev->func);
	while (section_dl_size != cur_len) {
		if (!atomic_read(&cif_dev->tx_rdy)) {
			ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue, cif_dev->func);
			if ((TX_EMPTY & u32ReadCRValue) != 0) {
				ret = btmtk_sdio_writel(CHISR, (TX_EMPTY | TX_COMPLETE_COUNT), cif_dev->func);
				if (ret != 0) {
					BTMTK_ERR("%s: btmtk_sdio_writel fail", __func__);
					goto enable_intr;
				}
				atomic_set(&cif_dev->tx_rdy, 1);
			} else if (delay_count > 1000) {
				BTMTK_ERR("%s: delay_count > 1000", __func__);
				goto enable_intr;
			} else {
				udelay(200);
				++delay_count;
				continue;
			}
		}

		sent_len = (section_dl_size - cur_len) >= (UPLOAD_PATCH_UNIT - MTK_SDIO_PACKET_HEADER_SIZE) ?
			(UPLOAD_PATCH_UNIT - MTK_SDIO_PACKET_HEADER_SIZE) : (section_dl_size - cur_len);

		BTMTK_DBG("%s: sent_len = %d, cur_len = %d, delay_count = %d",
				__func__, sent_len, cur_len, delay_count);

		sdio_len = sent_len + MTK_SDIO_PACKET_HEADER_SIZE;
		memset(image, 0, sdio_len);
		image[0] = (sdio_len & 0x00FF);
		image[1] = (sdio_len & 0xFF00) >> 8;
		image[2] = 0;
		image[3] = 0;

		memcpy(image + MTK_SDIO_PACKET_HEADER_SIZE,
			fwbuf + section_offset + cur_len,
			sent_len);

		block_count = sdio_len / SDIO_BLOCK_SIZE;
		redundant = sdio_len % SDIO_BLOCK_SIZE;
		if (redundant)
			sdio_len = (block_count + 1) * SDIO_BLOCK_SIZE;

		ret = btmtk_sdio_writesb(CTDR, image, sdio_len, cif_dev->func);
		atomic_set(&cif_dev->tx_rdy, 0);
		cur_len += sent_len;

		if (ret < 0) {
			BTMTK_ERR("%s: send patch failed, terminate", __func__);
			goto enable_intr;
		}
	}
	btmtk_sdio_enable_interrupt(1, cif_dev->func);

	BTMTK_INFO("%s: send dl cmd", __func__);
	ret = btmtk_main_send_cmd(bdev,
			cmd, LD_PATCH_CMD_LEN,
			event, LD_PATCH_EVT_LEN,
			PATCH_DOWNLOAD_PHASE3_DELAY_TIME,
			PATCH_DOWNLOAD_PHASE3_RETRY,
			BTMTK_TX_ACL_FROM_DRV);
	if (ret < 0) {
		BTMTK_ERR("%s: send wmd dl cmd failed, terminate!", __func__);
		return ret;
	}

	BTMTK_INFO("%s: loading rom patch... Done", __func__);
	return ret;

enable_intr:
	btmtk_sdio_enable_interrupt(1, cif_dev->func);
	return ret;
}

static int btmtk_sdio_register_dev(struct btmtk_sdio_dev *bdev)
{
	struct sdio_func *func;
	u8	u8ReadCRValue = 0;
	int ret = 0;

	if (!bdev || !bdev->func) {
		BTMTK_ERR("Error: card or function is NULL!");
		ret = -EINVAL;
		goto failed;
	}

	func = bdev->func;

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	sdio_release_host(func);
	if (ret) {
		BTMTK_ERR("sdio_enable_func() failed: ret=%d", ret);
		ret = -EIO;
		goto failed;
	}

	btmtk_sdio_readb(SDIO_CCCR_IENx, &u8ReadCRValue, func);
	BTMTK_INFO("before claim irq read SDIO_CCCR_IENx %x, func num %d",
		u8ReadCRValue, func->num);

	sdio_claim_host(func);
	ret = sdio_claim_irq(func, btmtk_sdio_interrupt);
	sdio_release_host(func);
	if (ret) {
		BTMTK_ERR("sdio_claim_irq failed: ret=%d", ret);
		ret = -EIO;
		goto disable_func;
	}

	BTMTK_INFO("sdio_claim_irq success: ret=%d", ret);

	btmtk_sdio_readb(SDIO_CCCR_IENx, &u8ReadCRValue, func);
	BTMTK_INFO("after claim irq read SDIO_CCCR_IENx %x", u8ReadCRValue);

	sdio_claim_host(func);
	ret = sdio_set_block_size(func, SDIO_BLOCK_SIZE);
	sdio_release_host(func);
	if (ret) {
		pr_err("cannot set SDIO block size");
		ret = -EIO;
		goto release_irq;
	}


	return 0;

release_irq:
	sdio_release_irq(func);

disable_func:
	sdio_disable_func(func);

failed:
	pr_info("%s fail", __func__);
	return ret;
}

static int btmtk_sdio_enable_host_int(struct btmtk_sdio_dev *cif_dev)
{
	int ret = 0;
	u32 read_data = 0;

	if (!cif_dev || !cif_dev->func)
		return -EINVAL;

	/* workaround for some platform no host clock sometimes */

	ret = btmtk_sdio_readl(CSDIOCSR, &read_data, cif_dev->func);
	BTMTK_INFO("%s read CSDIOCSR is 0x%X, ret = %d", __func__, read_data, ret);
	read_data |= 0x4;
	ret = btmtk_sdio_writel(CSDIOCSR, read_data, cif_dev->func);
	BTMTK_INFO("%s write CSDIOCSR is 0x%X, ret = %d", __func__, read_data, ret);

	return ret;
}

#if 0
int btmtk_sdio_disable_host_int(struct btmtk_sdio_dev *cif_dev)
{
	int ret;
	u32 read_data = 0;

	if (!bdev || !bdev->func)
		return -EINVAL;

	/* workaround for some platform no host clock sometimes */

	btmtk_sdio_readl(CSDIOCSR, &read_data, cif_dev->func);
	BTMTK_INFO("%s read CSDIOCSR is 0x%X\n", __func__, read_data);
	read_data = read_data & ~(0x04);
	btmtk_sdio_writel(CSDIOCSR, read_data, cif_dev->func);
	BTMTK_INFO("%s write CSDIOCSR is 0x%X\n", __func__, read_data);

	return ret;
}
#endif

static int btmtk_sdio_unregister_dev(struct btmtk_sdio_dev *cif_dev)
{
	if (cif_dev && cif_dev->func) {
		sdio_claim_host(cif_dev->func);
		sdio_release_irq(cif_dev->func);
		sdio_disable_func(cif_dev->func);
		sdio_release_host(cif_dev->func);
		sdio_set_drvdata(cif_dev->func, NULL);
		cif_dev->func = NULL;
	}
	return 0;
}

static int btmtk_sdio_set_write_clear(struct btmtk_sdio_dev *cif_dev)
{
	u32 u32ReadCRValue = 0;
	u32 ret = 0;

	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, cif_dev->func);
	if (ret) {
		BTMTK_ERR("%s read CHCR error", __func__);
		ret = EINVAL;
		return ret;
	}

	u32ReadCRValue |= 0x00000002;
	btmtk_sdio_writel(CHCR, u32ReadCRValue, cif_dev->func);
	BTMTK_INFO("%s write CHCR 0x%08X", __func__, u32ReadCRValue);
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, cif_dev->func);
	BTMTK_INFO("%s read CHCR 0x%08X", __func__, u32ReadCRValue);
	if (u32ReadCRValue&0x00000002)
		BTMTK_INFO("%s write clear", __func__);
	else
		BTMTK_INFO("%s read clear", __func__);

	return ret;
}

static int btmtk_sdio_poll_subsys_done(struct btmtk_sdio_dev *cif_dev)
{
	u32 u32ReadCRValue = 0;
	int retry = 100;

/*	btmtk_sdio_writel(0x30, 0xFD, cif_dev->func);
 *	BTMTK_INFO("%s write 0x30 = 0xFD, retry = %d", __func__, retry);
 */
	while (retry-- > 0) {
/*		btmtk_sdio_readl(0x2c, &u32ReadCRValue, cif_dev->func);
 *		BTMTK_INFO("%s read 0x2c = 0x%08X, retry = %d", __func__, u32ReadCRValue, retry);
 *		btmtk_sdio_readl(CHLPCR, &u32ReadCRValue, cif_dev->func);
 *		BTMTK_INFO("%s read CHLPCR 0x%08X, retry = %d", __func__, u32ReadCRValue, retry);
 *		btmtk_sdio_readl(SWPCDBGR, &u32ReadCRValue, cif_dev->func);
 *		BTMTK_INFO("%s read SWPCDBGR 0x%08X, retry = %d", __func__, u32ReadCRValue, retry);
 */
		btmtk_sdio_readl(CHCR, &u32ReadCRValue, cif_dev->func);
		BTMTK_INFO("%s read CHCR 0x%08X, retry = %d", __func__, u32ReadCRValue, retry);
		if (u32ReadCRValue & (0x1 << 8))
			return 0;
		msleep(20);
	}

	return -1;
}

static int btmtk_sdio_subsys_reset(struct btmtk_dev *bdev)
{
	struct btmtk_sdio_dev *cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;
	u32 u32ReadCRValue = 0;
	int retry = RETRY_TIMES;
	u32 ret = 0;

	do {
		/* After WDT, CHLPCR maybe can't show driver/fw own status
		 * BT SW should check PD2HRM0R bit 0
		 * 1: Driver own. 0: FW own
		 */
		btmtk_sdio_keep_driver_own(cif_dev, 1);
		ret = btmtk_sdio_readl(PD2HRM0R, &u32ReadCRValue, cif_dev->func);
		msleep(DELAY_TIMES);
		retry--;
	} while (((u32ReadCRValue & PD2HRM0R_DRIVER_OWN) != PD2HRM0R_DRIVER_OWN)
			&& retry > 0);

	BTMTK_INFO("%s read PD2HRM0R 0x%08X", __func__, u32ReadCRValue);
	cif_dev->patched = 0;

	btmtk_sdio_set_wifi_driver_own(1);

	/* write CHCR[3] 0 */
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, cif_dev->func);
	BTMTK_INFO("%s read CHCR 0x%08X", __func__, u32ReadCRValue);
	u32ReadCRValue &= 0xFFFFFFF7;
	BTMTK_INFO("%s write CHCR 0x%08X", __func__, u32ReadCRValue);
	btmtk_sdio_writel(CHCR, u32ReadCRValue, cif_dev->func);

	/* write CHCR[3] to 1 */
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, cif_dev->func);
	BTMTK_INFO("%s read CHCR 0x%08X", __func__, u32ReadCRValue);
	u32ReadCRValue |= 0x00000008;
	BTMTK_INFO("%s write CHCR 0x%08X", __func__, u32ReadCRValue);
	btmtk_sdio_writel(CHCR, u32ReadCRValue, cif_dev->func);

	/* write CHCR[5] to 0 */
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, cif_dev->func);
	BTMTK_INFO("%s read CHCR 0x%08X", __func__, u32ReadCRValue);
	u32ReadCRValue &= 0xFFFFFFDF;
	BTMTK_INFO("%s write CHCR 0x%08X", __func__, u32ReadCRValue);
	btmtk_sdio_writel(CHCR, u32ReadCRValue, cif_dev->func);

	/* write CHCR[5] to 1 */
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, cif_dev->func);
	BTMTK_INFO("%s read CHCR 0x%08X", __func__, u32ReadCRValue);
	u32ReadCRValue |= 0x00000020;
	BTMTK_INFO("%s write CHCR 0x%08X", __func__, u32ReadCRValue);
	btmtk_sdio_writel(CHCR, u32ReadCRValue, cif_dev->func);

	/* Poll subsys reset done */
	if (btmtk_sdio_poll_subsys_done(cif_dev))
		return -EIO;

	/* Do-init cr */
	/* Disable the interrupts on the card */
	btmtk_sdio_enable_host_int(cif_dev);
	BTMTK_DBG("call btmtk_sdio_enable_host_int done");

	/* Set interrupt output */
	ret = btmtk_sdio_writel(CHIER, FIRMWARE_INT_BIT31 | FIRMWARE_INT|TX_FIFO_OVERFLOW |
			FW_INT_IND_INDICATOR | TX_COMPLETE_COUNT |
			TX_UNDER_THOLD | TX_EMPTY | RX_DONE, cif_dev->func);
	if (ret) {
		BTMTK_ERR("Set interrupt output fail(%d)", ret);
		ret = -EIO;
		return ret;
	}

	/* Enable interrupt output */
	ret = btmtk_sdio_writel(CHLPCR, C_FW_INT_EN_SET, cif_dev->func);
	if (ret) {
		BTMTK_ERR("enable interrupt output fail(%d)", ret);
		ret = -EIO;
		return ret;
	}

	/* Adopt write clear method */
	btmtk_sdio_set_write_clear(cif_dev);

	ret = btmtk_sdio_readl(0, &u32ReadCRValue, cif_dev->func);
	BTMTK_INFO("%s read chipid =  %x", __func__, u32ReadCRValue);

	return ret;
}

static int btmtk_sdio_whole_reset(struct btmtk_dev *bdev)
{
	int ret = -1;
	struct btmtk_sdio_dev *cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;
	struct mmc_card *card = cif_dev->func->card;
	struct mmc_host *host = NULL;

	if ((card == NULL) || (card->host  == NULL)) {
		BTMTK_ERR("mmc structs are NULL");
		return ret;
	}

	host = card->host;
	if (host->rescan_entered != 0) {
		host->rescan_entered = 0;
		BTMTK_INFO("set mmc_host rescan to 0");
	}
	cif_dev->patched = 0;

	BTMTK_INFO("mmc_remove_host");
	mmc_remove_host(host);

	/* Replace hooked SDIO driver probe to new API;
	 * 1. It will be new kthread(state) after mmc_add_host;
	 * 2. Extend flexibility to notify us that HW reset was triggered,
	 * more flexiable on reviving in exchanging old/new kthread(state).
	 */
	BTMTK_INFO("mmc_add_host");
	ret = mmc_add_host(host);

	BTMTK_INFO("mmc_add_host return %d", ret);
	return ret;
}

/* bt_tx_wait_for_msg
 *
 *    Check needing action of current bt status to wake up bt thread
 *
 * Arguments:
 *    [IN] bdev     - bt driver control strcuture
 *
 * Return Value:
 *    return check  - 1 for waking up bt thread, 0 otherwise
 *
 */
static u32 btmtk_thread_wait_for_msg(struct btmtk_sdio_dev *cif_dev)
{
	u32 ret = 0;

	if (!skb_queue_empty(&cif_dev->tx_queue) && (atomic_read(&cif_dev->tx_rdy))) {
		BTMTK_DBG("tx queue is not empty");
		ret |= BTMTK_SDIO_THREAD_TX;
	}

	if (atomic_read(&cif_dev->int_count)) {
		BTMTK_DBG("cif_dev->int_count is %d", atomic_read(&cif_dev->int_count));
		ret |= BTMTK_SDIO_THREAD_RX;
	}

	if (kthread_should_stop()) {
		BTMTK_DBG("kthread_should_stop");
		ret |= BTMTK_SDIO_THREAD_STOP;
	}

	if (atomic_read(&cif_dev->fw_own_timer_flag) == FW_OWN_TIMER_RUNNING) {
		BTMTK_DBG("fw own");
		ret |= BTMTK_SDIO_THREAD_FW_OWN;
	}

	return ret;
}

static int btmtk_tx_pkt(struct btmtk_sdio_dev *cif_dev, struct sk_buff *skb)
{
	u8 MultiBluckCount = 0;
	u8 redundant = 0;
	int len = 0;
	int ret = 0;

	BTMTK_DBG("btmtk_tx_pkt");

	cif_dev->sdio_packet[0] = (4 + skb->len) & 0xFF;
	cif_dev->sdio_packet[1] = ((4 + skb->len) & 0xFF00) >> 8;

	memcpy(cif_dev->sdio_packet + MTK_SDIO_PACKET_HEADER_SIZE, skb->data,
		skb->len);
	len = skb->len + MTK_SDIO_PACKET_HEADER_SIZE;
	BTMTK_DBG_RAW(cif_dev->sdio_packet, len,
			"%s: sent, len =%d:", __func__, len);

	MultiBluckCount = len / SDIO_BLOCK_SIZE;
	redundant = len % SDIO_BLOCK_SIZE;
	if (redundant)
		len = (MultiBluckCount+1)*SDIO_BLOCK_SIZE;

	atomic_set(&cif_dev->tx_rdy, 0);
	ret = btmtk_sdio_writesb(CTDR, cif_dev->sdio_packet, len, cif_dev->func);
	if (ret < 0)
		BTMTK_ERR("ret = %d", ret);
	kfree_skb(skb);
	return ret;
}

static int btmtk_sdio_interrupt_process(struct btmtk_dev *bdev)
{
	struct btmtk_sdio_dev *cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;
	int ret = 0;
	u32 u32ReadCRValue = 0;

	ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue, cif_dev->func);
#if BTMTK_SDIO_DEBUG
	rx_debug_save(CHISR_r_1, u32ReadCRValue, NULL);
#endif
	BTMTK_DBG("%s CHISR 0x%08x", __func__, u32ReadCRValue);

	if (u32ReadCRValue & FIRMWARE_INT_BIT15)
		btmtk_sdio_set_no_fwn_own(cif_dev, 1);

	if (u32ReadCRValue & FIRMWARE_INT_BIT31) {
		/* It's read-only bit (WDT interrupt)
		 * Host can't modify it.
		 */
		ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue, cif_dev->func);
		BTMTK_INFO("%s CHISR 0x%08x", __func__, u32ReadCRValue);
		/* FW can't send TX_EMPTY for 0xFD5B */
		atomic_set(&cif_dev->tx_rdy, 1);
		DUMP_TIME_STAMP("notify_chip_reset");
		btmtk_reset_trigger(bdev);
		return ret;
	}

	if (TX_EMPTY & u32ReadCRValue) {
		ret = btmtk_sdio_writel(CHISR, (TX_EMPTY | TX_COMPLETE_COUNT), cif_dev->func);
		atomic_set(&cif_dev->tx_rdy, 1);
		BTMTK_DBG("%s set tx_rdy true", __func__);
		if (ret < 0)
			BTMTK_ERR(" %s, ret:%d", __func__, ret);
#if BTMTK_SDIO_DEBUG
		tx_empty_cnt++;
#endif
	}

	if (RX_DONE & u32ReadCRValue) {
		ret = btmtk_cif_recv_evt(bdev);
		BTMTK_DBG("%s recv_evt, ret = %d", __func__, ret);
	}

	ret = btmtk_sdio_enable_interrupt(1, cif_dev->func);
	BTMTK_DBG("%s done, ret = %d", __func__, ret);
	return ret;
}


/*
 * This function handles the event generated by firmware, rx data
 * received from firmware, and tx data sent from kernel.
 */
static int btmtk_sdio_main_thread(void *data)
{
	struct btmtk_dev *bdev = data;
	struct btmtk_sdio_dev *cif_dev = NULL;
	struct sk_buff *skb;
	int ret = 0;
	ulong flags;
	u32 thread_flag = 0;
/*	struct sched_param param = { .sched_priority = 90 }; RR 90 is the same as audio*/

	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

/*	sched_setscheduler(current, SCHED_RR, &param); */

	BTMTK_INFO("btmtk_sdio_main_thread start running...");
	atomic_set(&cif_dev->sdio_thread.thread_status, 1);

	for (;;) {
		wait_event_interruptible(cif_dev->sdio_thread.wait_q,
			(thread_flag = btmtk_thread_wait_for_msg(cif_dev)));
		if (thread_flag & BTMTK_SDIO_THREAD_STOP) {
			BTMTK_WARN("sdio_thread: break from main thread");
			break;
		}
		BTMTK_DBG("btmtk_sdio_main_thread doing...");

		if (thread_flag & BTMTK_SDIO_THREAD_FW_OWN) {
			ret = btmtk_sdio_set_fw_own(cif_dev, RETRY_TIMES);
			if (ret) {
				BTMTK_ERR("set fw own return fail");
				bdev->debug_type = DEBUG_SOP_SLEEP;
				btmtk_reset_trigger(bdev);
				continue;
			}
		}

		if (thread_flag & (BTMTK_SDIO_THREAD_TX | BTMTK_SDIO_THREAD_RX)) {
			ret = btmtk_sdio_set_driver_own(cif_dev, RETRY_TIMES);
			if (ret) {
				BTMTK_ERR("set driver own return fail");
				bdev->debug_type = DEBUG_SOP_WAKEUP;
				btmtk_reset_trigger(bdev);
				continue;
			}
		}

		/* Do interrupt */
		if (thread_flag & BTMTK_SDIO_THREAD_RX) {
			BTMTK_DBG("go int");
			atomic_set(&cif_dev->int_count, 0);
			if (btmtk_sdio_interrupt_process(bdev)) {
				btmtk_reset_trigger(bdev);
				continue;
			}
		} else {
			BTMTK_DBG("go tx");
		}

		if (thread_flag & BTMTK_SDIO_THREAD_TX) {
			spin_lock_irqsave(&bdev->txlock, flags);
			skb = skb_dequeue(&cif_dev->tx_queue);
			spin_unlock_irqrestore(&bdev->txlock, flags);
			if (skb) {
				ret = btmtk_tx_pkt(cif_dev, skb);
				if (ret) {
					BTMTK_ERR("tx pkt return fail %d", ret);
					btmtk_reset_trigger(bdev);
					continue;
				}
			}
		}
	}

	atomic_set(&cif_dev->sdio_thread.thread_status, 0);
	BTMTK_WARN("end");
	return 0;
}

static int btmtk_sdio_probe(struct sdio_func *func,
					const struct sdio_device_id *id)
{
	int err = -1;
	struct btmtk_dev *bdev = NULL;
	struct btmtk_sdio_dev *cif_dev = NULL;

	bdev = sdio_get_drvdata(func);
	if (!bdev) {
		BTMTK_ERR("[ERR] bdev is NULL");
		return -ENOMEM;
	}

	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

	cif_dev->func = func;
	cif_dev->bdev = bdev;
	BTMTK_INFO("%s func device %p", __func__, func);
	cif_dev->patched = 0;

	/* it's for L0/L0.5 reset */
	INIT_WORK(&bdev->reset_waker, btmtk_reset_waker);
	spin_lock_init(&bdev->txlock);
	spin_lock_init(&bdev->rxlock);

	if (btmtk_sdio_register_dev(cif_dev) < 0) {
		BTMTK_ERR("Failed to register BT device!");
		return -ENODEV;
	}

	/* Disable the interrupts on the card */
	btmtk_sdio_enable_host_int(cif_dev);
	BTMTK_DBG("call btmtk_sdio_enable_host_int done");

	sdio_set_drvdata(func, bdev);

	btmtk_sdio_keep_driver_own(cif_dev, 1);

	/* create tx/rx thread */
	init_waitqueue_head(&cif_dev->sdio_thread.wait_q);
	skb_queue_head_init(&cif_dev->tx_queue);
	atomic_set(&cif_dev->int_count, 0);
	atomic_set(&cif_dev->tx_rdy, 1);
	cif_dev->sdio_thread.task = kthread_run(btmtk_sdio_main_thread,
				bdev, "btmtk_sdio_main_thread");
	if (IS_ERR(cif_dev->sdio_thread.task)) {
		BTMTK_DBG("btmtk_sdio_ps failed to start!");
		err = PTR_ERR(cif_dev->sdio_thread.task);
		goto unreg_sdio;

	}

	/* Set interrupt output */
	err = btmtk_sdio_writel(CHIER, FIRMWARE_INT_BIT31 | FIRMWARE_INT_BIT15 |
			FIRMWARE_INT|TX_FIFO_OVERFLOW |
			FW_INT_IND_INDICATOR | TX_COMPLETE_COUNT |
			TX_UNDER_THOLD | TX_EMPTY | RX_DONE, cif_dev->func);
	if (err) {
		BTMTK_ERR("Set interrupt output fail(%d)", err);
		err = -EIO;
		goto free_thread;
	}

	/* Enable interrupt output */
	err = btmtk_sdio_writel(CHLPCR, C_FW_INT_EN_SET, cif_dev->func);
	if (err) {
		BTMTK_ERR("enable interrupt output fail(%d)", err);
		err = -EIO;
		goto free_thread;
	}

	/* write clear method */
	btmtk_sdio_set_write_clear(cif_dev);

	/* old method for chip id
	 * btmtk_sdio_readl(0, &u32ReadCRValue, bdev->func);
	 * BTMTK_INFO("%s read chipid =  %x", __func__, u32ReadCRValue);
	 */

	err = btmtk_cif_allocate_memory(cif_dev);
	if (err < 0) {
		BTMTK_ERR("[ERR] btmtk_cif_allocate_memory failed!");
		goto free_thread;
	}

	err = btmtk_main_cif_initialize(bdev, HCI_SDIO);
	if (err < 0) {
		BTMTK_ERR("[ERR] btmtk_main_cif_initialize failed!");
		goto free_mem;
	}

	err = btmtk_load_rom_patch(bdev);
	if (err < 0) {
		BTMTK_ERR("btmtk load rom patch failed!");
		goto deinit;
	}

	cif_dev->patched = 1;

	err = btmtk_woble_initialize(bdev, &cif_dev->bt_woble);
	if (err < 0) {
		BTMTK_ERR("btmtk_woble_initialize failed!");
		goto deinit;
	}

	btmtk_buffer_mode_initialize(bdev, &cif_dev->buffer_mode);

	err = btmtk_register_hci_device(bdev);
	if (err < 0) {
		BTMTK_ERR("btmtk_register_hci_device failed!");
		goto free_setting;
	}

	btmtk_sdio_writel(0x40, 0x9F1E0000, cif_dev->func);

	goto end;

free_setting:
	btmtk_woble_uninitialize(&cif_dev->bt_woble);
deinit:
	btmtk_main_cif_uninitialize(bdev, HCI_SDIO);
free_mem:
	btmtk_cif_free_memory(cif_dev);
free_thread:
	kthread_stop(cif_dev->sdio_thread.task);
	wake_up_interruptible(&cif_dev->sdio_thread.wait_q);
	BTMTK_INFO("wake_up_interruptible main_thread done");
unreg_sdio:
	btmtk_sdio_unregister_dev(cif_dev);
end:
	BTMTK_INFO("%s normal end, ret = %d", __func__, err);
	btmtk_sdio_keep_driver_own(cif_dev, 0);

	return 0;
}

static void btmtk_sdio_stop_main_thread(struct btmtk_sdio_dev *cif_dev)
{
	skb_queue_purge(&cif_dev->tx_queue);
	if (!IS_ERR(cif_dev->sdio_thread.task)) {
		kthread_stop(cif_dev->sdio_thread.task);
		wake_up_interruptible(&cif_dev->sdio_thread.wait_q);

		while (atomic_read(&cif_dev->sdio_thread.thread_status)) {
			BTMTK_INFO("wait btmtk_sdio_main_thread stop");
			msleep(100);
		}

		BTMTK_INFO("btmtk_sdio_main_thread stop success!");
	}
}

static void btmtk_sdio_disconnect(struct sdio_func *func)
{
	struct btmtk_dev *bdev = sdio_get_drvdata(func);
	struct btmtk_sdio_dev *cif_dev = NULL;

	if (!bdev)
		return;

	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;
	btmtk_sdio_stop_main_thread(cif_dev);
	btmtk_woble_uninitialize(&cif_dev->bt_woble);
	btmtk_cif_free_memory(cif_dev);
	btmtk_sdio_unregister_dev(cif_dev);

	btmtk_main_cif_disconnect_notify(bdev, HCI_SDIO);
}

static int btmtk_cif_probe(struct sdio_func *func,
					const struct sdio_device_id *id)
{
	int ret = -1;
	int cif_event = 0;
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_dev *bdev = NULL;

	/* Mediatek Driver Version */
	BTMTK_INFO("%s: MTK BT Driver Version: %s", __func__, VERSION);

	BTMTK_DBG("vendor=0x%x, device=0x%x, class=%d, fn=%d",
			id->vendor, id->device, id->class,
			func->num);
	DUMP_TIME_STAMP("probe_start");

	/* sdio interface numbers  */
	if (func->num != BTMTK_SDIO_FUNC) {
		BTMTK_INFO("%s: func num is not match, func_num = %d", __func__, func->num);
		return -ENODEV;
	}

	/* Retrieve priv data and set to interface structure */
	bdev = btmtk_get_dev();
	if (!bdev) {
		BTMTK_INFO("%s: bdev is NULL", __func__);
		return -ENODEV;
	}

	bdev->intf_dev = &func->dev;
	bdev->cif_dev = &g_sdio_dev;
	sdio_set_drvdata(func, bdev);

	/* Retrieve current HIF event state */
	cif_event = HIF_EVENT_PROBE;
	if (BTMTK_CIF_IS_NULL(bdev, cif_event)) {
		/* Error */
		BTMTK_WARN("%s priv setting is NULL", __func__);
		return -ENODEV;
	}

	cif_state = &bdev->cif_state[cif_event];

	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

	/* Do HIF events */
	ret = btmtk_sdio_probe(func, id);

	/* Set End/Error state */
	if (ret == 0)
		btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
	else
		btmtk_set_chip_state((void *)bdev, cif_state->ops_error);

	DUMP_TIME_STAMP("probe_end");
	return ret;
}

static void btmtk_cif_disconnect(struct sdio_func *func)
{
	int cif_event = 0;
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_dev *bdev = NULL;

	bdev = sdio_get_drvdata(func);

	/* Retrieve current HIF event state */
	cif_event = HIF_EVENT_DISCONNECT;
	if (BTMTK_CIF_IS_NULL(bdev, cif_event)) {
		/* Error */
		BTMTK_WARN("%s priv setting is NULL", __func__);
		return;
	}

	cif_state = &bdev->cif_state[cif_event];

	btmtk_sdio_cif_mutex_lock(bdev);
	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

	/* Do HIF events */
	btmtk_sdio_disconnect(func);

	/* Set End/Error state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
	btmtk_sdio_cif_mutex_unlock(bdev);
}

#ifdef CONFIG_PM
static int btmtk_cif_suspend(struct device *dev)
{
	int ret = 0;
	int cif_event = 0;
	struct btmtk_cif_state *cif_state = NULL;
	int state = BTMTK_STATE_INIT;
	struct sdio_func *func = NULL;
	struct btmtk_dev *bdev = NULL;
	struct btmtk_sdio_dev *cif_dev = NULL;
	struct btmtk_woble *bt_woble = NULL;
	mmc_pm_flag_t pm_flags;
	struct irq_desc *desc;

	BTMTK_INFO("%s, enter", __func__);

	if (!dev)
		return 0;
	func = dev_to_sdio_func(dev);
	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return 0;

	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;
	bt_woble = &cif_dev->bt_woble;

	if (bdev->suspend_count++) {
		BTMTK_WARN("Has suspended. suspend_count: %d, end", bdev->suspend_count);
		return 0;
	}

	state = btmtk_get_chip_state(bdev);
	/* Retrieve current HIF event state */
	if (state == BTMTK_STATE_FW_DUMP) {
		BTMTK_WARN("%s: FW dumping ongoing, don't dos suspend flow!!!", __func__);
		cif_event = HIF_EVENT_FW_DUMP;
	} else
		cif_event = HIF_EVENT_SUSPEND;
#if 0
	if (BTMTK_CIF_IS_NULL(bdev, cif_event)) {
		/* Error */
		BTMTK_WARN("%s intf[%d] priv setting is NULL", __func__, ifnum_base);
		return -ENODEV;
	}
#endif
	cif_state = &bdev->cif_state[cif_event];

	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

#if CFG_SUPPORT_DVT
	BTMTK_INFO("%s: SKIP Driver woble_suspend flow", __func__);
#else
	ret = btmtk_woble_suspend(bt_woble);
	if (ret < 0)
		BTMTK_ERR("%s: btmtk_woble_suspend return fail %d", __func__, ret);
#endif

	if (bdev->bt_cfg.support_woble_by_eint) {
		if (bt_woble->wobt_irq != 0 && atomic_read(&(bt_woble->irq_enable_count)) == 0) {
			/* clear irq data before enable woble irq to avoid DUT wake up
			 *  automatically by edge trigger, sync from Jira CONN-50629
			 */
			desc = irq_to_desc(bt_woble->wobt_irq);
			if (desc)
				desc->irq_data.chip->irq_ack(&desc->irq_data);
			else
				BTMTK_INFO("%s:can't get desc\n", __func__);

			BTMTK_INFO("%s, enable BT IRQ:%d", __func__, bt_woble->wobt_irq);
			irq_set_irq_wake(bt_woble->wobt_irq, 1);
			enable_irq(bt_woble->wobt_irq);
			atomic_inc(&(bt_woble->irq_enable_count));
		} else
			BTMTK_INFO("%s, irq_enable count:%d", __func__, atomic_read(&(bt_woble->irq_enable_count)));
	}

	pm_flags = sdio_get_host_pm_caps(func);
	if (!(pm_flags & MMC_PM_KEEP_POWER)) {
		BTMTK_ERR("%s, %s cannot remain alive while suspended(0x%x)", __func__,
			sdio_func_id(func), pm_flags);
	}

	pm_flags = MMC_PM_KEEP_POWER;
	ret = sdio_set_host_pm_flags(func, pm_flags);
	if (ret) {
		BTMTK_ERR("%s, set flag 0x%x err %d", __func__, pm_flags, (int)ret);
		ret = -ENOSYS;
	}

	/* Set End/Error state */
	if (ret == 0)
		btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
	else
		btmtk_set_chip_state((void *)bdev, cif_state->ops_error);

	BTMTK_INFO("%s, end. ret = %d", __func__, ret);
	return ret;
}

static int btmtk_cif_resume(struct device *dev)
{
	u8 ret = 0;
	struct sdio_func *func = NULL;
	struct btmtk_dev *bdev = NULL;
	struct btmtk_sdio_dev *cif_dev = NULL;
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_woble *bt_woble = NULL;

	BTMTK_INFO("%s, enter", __func__);

	if (!dev)
		return 0;
	func = dev_to_sdio_func(dev);
	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return 0;

	cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;
	bt_woble = &cif_dev->bt_woble;

	bdev->suspend_count--;
	if (bdev->suspend_count) {
		BTMTK_INFO("data->suspend_count %d, return 0", bdev->suspend_count);
		return 0;
	}

	if (bdev->bt_cfg.support_woble_by_eint) {
		if (bt_woble->wobt_irq != 0 && atomic_read(&(bt_woble->irq_enable_count)) == 1) {
			BTMTK_INFO("disable BT IRQ:%d", bt_woble->wobt_irq);
			atomic_dec(&(bt_woble->irq_enable_count));
			disable_irq_nosync(bt_woble->wobt_irq);
		} else
			BTMTK_INFO("irq_enable count:%d", atomic_read(&(bt_woble->irq_enable_count)));
	}

	cif_state = &bdev->cif_state[HIF_EVENT_RESUME];

	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

#if CFG_SUPPORT_DVT
	BTMTK_INFO("%s: SKIP Driver woble_resume flow", __func__);
#else
	ret = btmtk_woble_resume(bt_woble);
	if (ret < 0)
		BTMTK_ERR("%s: btmtk_woble_resume return fail %d", __func__, ret);
#endif
	/* Set End/Error state */
	if (ret == 0)
		btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
	else
		btmtk_set_chip_state((void *)bdev, cif_state->ops_error);

	BTMTK_INFO("end");
	return 0;
}
#endif	/* CONFIG_PM */


#ifdef CONFIG_PM
static const struct dev_pm_ops btmtk_sdio_pm_ops = {
	.suspend = btmtk_cif_suspend,
	.resume = btmtk_cif_resume,
};
#endif

static struct sdio_driver btmtk_sdio_driver = {
	.name = "btsdio",
	.id_table = btmtk_sdio_tabls,
	.probe = btmtk_cif_probe,
	.remove = btmtk_cif_disconnect,
	.drv = {
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &btmtk_sdio_pm_ops,
#endif
	}
};

static int sdio_register(void)
{
	BTMTK_INFO("%s", __func__);

	if (sdio_register_driver(&btmtk_sdio_driver) != 0)
		return -ENODEV;

	return 0;
}

static int sdio_deregister(void)
{
	BTMTK_INFO("%s", __func__);
	sdio_unregister_driver(&btmtk_sdio_driver);
	return 0;
}

static void btmtk_sdio_chip_reset_notify(struct btmtk_dev *bdev)
{
	struct btmtk_sdio_dev *cif_dev = (struct btmtk_sdio_dev *)bdev->cif_dev;

	if (cif_dev == NULL) {
		BTMTK_INFO("%s, cif_dev is NULL", __func__);
		return;
	}
	btmtk_sdio_keep_driver_own(cif_dev, 0);
	cif_dev->patched = 1;
	atomic_set(&cif_dev->tx_rdy, 1);

	btmtk_sdio_set_wifi_driver_own(0);
}

int btmtk_cif_register(void)
{
	int retval = 0;
	struct hif_hook_ptr hook;

	BTMTK_INFO("%s", __func__);

	memset(&hook, 0, sizeof(hook));
	hook.open = btmtk_sdio_open;
	hook.close = btmtk_sdio_close;
	hook.reg_read = btmtk_sdio_read_register;
	hook.reg_write = btmtk_sdio_write_register;
	hook.send_cmd = btmtk_sdio_send_cmd;
	hook.send_and_recv = btmtk_sdio_send_and_recv;
	hook.event_filter = btmtk_sdio_event_filter;
	hook.subsys_reset = btmtk_sdio_subsys_reset;
	hook.whole_reset = btmtk_sdio_whole_reset;
	hook.chip_reset_notify = btmtk_sdio_chip_reset_notify;
	hook.cif_mutex_lock = btmtk_sdio_cif_mutex_lock;
	hook.cif_mutex_unlock = btmtk_sdio_cif_mutex_unlock;
	hook.open_done = btmtk_sdio_open_done;
	hook.dl_dma = btmtk_sdio_load_fw_patch_using_dma;
	hook.dump_debug_sop = btmtk_sdio_dump_debug_sop;
	btmtk_reg_hif_hook(&hook);

	retval = sdio_register();
	if (retval)
		BTMTK_ERR("*** SDIO registration fail(%d)! ***", retval);
	else
		BTMTK_INFO("%s, SDIO registration success!", __func__);
	return retval;
}

int btmtk_cif_deregister(void)
{
	BTMTK_INFO("%s", __func__);

	sdio_deregister();
	BTMTK_INFO("%s: Done", __func__);
	return 0;
}

