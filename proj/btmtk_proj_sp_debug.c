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

#include <btmtk_main.h>
#include "connv3.h"

#define BT_CR_DUMP_BUF_SIZE		(1024)
#define DBG_TAG	"[btmtk_dbg_sop]"

struct bt_dump_cr_buffer {
	uint8_t buffer[BT_CR_DUMP_BUF_SIZE];
	uint32_t cr_count;
	uint32_t count;
	uint8_t *pos;
	uint8_t *end;
};

struct bt_dump_cr_buffer g_btmtk_cr_dump;
struct btmtk_dev *g_dump_bdev;

static inline void BT_DUMP_CR_BUFFER_RESET(void)
{
	memset(g_btmtk_cr_dump.buffer, 0, BT_CR_DUMP_BUF_SIZE);
	g_btmtk_cr_dump.pos = &g_btmtk_cr_dump.buffer[0];
	g_btmtk_cr_dump.end = g_btmtk_cr_dump.pos + BT_CR_DUMP_BUF_SIZE - 1;
}

static inline void BT_DUMP_CR_INIT(uint32_t cr_count)
{
	BT_DUMP_CR_BUFFER_RESET();
	g_btmtk_cr_dump.count = 0;
	g_btmtk_cr_dump.cr_count = cr_count;
}

static inline int BT_DUMP_CR_PRINT(uint32_t value)
{
	uint32_t ret = 0;

	ret = snprintf(g_btmtk_cr_dump.pos,
				  (g_btmtk_cr_dump.end - g_btmtk_cr_dump.pos + 1),
				  "%08x ", value);
	if (ret < 0 || ret >= (g_btmtk_cr_dump.end - g_btmtk_cr_dump.pos + 1)) {
		BTMTK_ERR("%s %s: error in sprintf while dumping cr", DBG_TAG, __func__);
		if (g_btmtk_cr_dump.count)
			BTMTK_WARN("%s %s",DBG_TAG, g_btmtk_cr_dump.buffer);
		return -1;
	}

	g_btmtk_cr_dump.pos += ret;
	g_btmtk_cr_dump.count++;

	if ((g_btmtk_cr_dump.count & 0xF) == 0 ||
		 g_btmtk_cr_dump.count == g_btmtk_cr_dump.cr_count) {
		BTMTK_WARN("%s %s",DBG_TAG, g_btmtk_cr_dump.buffer);
		BT_DUMP_CR_BUFFER_RESET();
	}

	return 0;
}

int RHW_WRITE(uint32_t addr, uint32_t val)
{
	int ret = 0;
	/* ex: write dummy CR 0x022121cc = 0x44332222 */
	u8 cmd[RHW_PKT_LEN] = {0x40, 0x00, 0x00, 0x08, 0x00,
				0xCC, 0x21, 0x21, 0x02,
				0x22, 0x22, 0x33, 0x44};
	u8 evt[RHW_PKT_LEN] = {0x40, 0x00, 0x00, 0x08, 0x00,
				0xCC, 0x21, 0x21, 0x02,
				0x22, 0x22, 0x33, 0x44};

	BTMTK_DBG("%s: write addr[%x], value[0x%08x]", __func__, addr, val);
	memcpy(&cmd[RHW_ADDR_OFFSET_CMD], &addr, RHW_ADDR_LEN);
	memcpy(&cmd[RHW_VAL_OFFSET_CMD], &val, RHW_VAL_LEN);

	memcpy(&evt[RHW_ADDR_OFFSET_CMD], &addr, RHW_ADDR_LEN);

	ret = btmtk_main_send_cmd(g_dump_bdev, cmd, RHW_PKT_LEN, evt, RHW_PKT_COMP_LEN, DELAY_TIMES,
			RETRY_TIMES, BTMTK_TX_CMD_FROM_DRV);
	return ret ;

}

int RHW_READ(uint32_t addr, uint32_t *val)
{
	int ret = 0;
	/* ex: read dummy CR 0x022121cc */
	u8 cmd[RHW_PKT_LEN] = {0x41, 0x00, 0x00, 0x08, 0x00,
				0xCC, 0x21, 0x21, 0x02,
				0x00, 0x00, 0x00, 0x00};

	u8 evt[RHW_PKT_LEN] = {0x41, 0x00, 0x00, 0x08, 0x00,
				0xCC, 0x21, 0x21, 0x02,
				0x00, 0x00, 0x00, 0x00};

	memcpy(&cmd[RHW_ADDR_OFFSET_CMD], &addr, sizeof(addr));
	memcpy(&evt[RHW_ADDR_OFFSET_CMD], &addr, sizeof(addr));

	ret = btmtk_main_send_cmd(g_dump_bdev, cmd, RHW_PKT_LEN, evt, RHW_PKT_COMP_LEN, DELAY_TIMES,
			RETRY_TIMES, BTMTK_TX_CMD_FROM_DRV);

	if (ret >= 0) {
		memcpy(val, g_dump_bdev->io_buf + RHW_PKT_COMP_LEN, sizeof(u32));
		*val = le32_to_cpu(*val);
	} else
		*val = 0xdeaddead;

	BTMTK_DBG("%s: addr[%x], val[0x%08x]", __func__, addr, *val);

	return ret;

}

static inline void btmtk_dump_bg_mcu_core(void)
{
	uint32_t i = 0, org_value, value, cr_count = 0x26 + 4;

	BT_DUMP_CR_INIT(cr_count);
	BTMTK_INFO("%s [BG MCU core registers] - mcu_flg1, mcu_flg2 count[%d]", DBG_TAG, cr_count);
	RHW_WRITE(0x81025020, 0x00000201);
	RHW_READ(0x80000A00, &org_value);

	/* mcu flag1 */
	for (i = 0; i < 0x26; i++) {
		value = (org_value & 0xC0FFFFFF) | (i << 24);

		RHW_WRITE(0x80000A00, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}

	for (i = 0; i < 4; i++) {
		value = 0x0403 | (i << 16);

		RHW_WRITE(0x81025020, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}
}

static inline void btmtk_dump_dsp_debug_flags(void)
{
	uint32_t i = 0, value, cr_count = 0x16 + 1;

	BT_DUMP_CR_INIT(cr_count);
	BTMTK_INFO("%s [BG DSP debug flags] - mcu_flg3, mcu_flg4 count[%d]", DBG_TAG, cr_count);

	RHW_WRITE(0x81025020, 0x00000605);
	RHW_READ(0x8000040C, &value);
	BT_DUMP_CR_PRINT(value);

	for (i = 0; i <= 0x16; i++) {
		value = 0x0807 | (i << 16);

		RHW_WRITE(0x81025020, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}
}

static inline void btmtk_dump_mcusys_clk_gals_debug_flags(void)
{
	uint32_t i = 0, value, cr_count = 4 + 8;

	BT_DUMP_CR_INIT(cr_count);
	BTMTK_INFO("%s [BG MCUSYS CLK and GALS debug flags] - mcu_flg5, mcu_flg6 count[%d]", DBG_TAG, cr_count);

	/* mcu_flag5 */
	for (i = 0; i < 4; i++) {
		value = 0x0A09 | (i << 16);

		RHW_WRITE(0x81025020, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}

	/* mcu_flag6 */
	RHW_WRITE(0x81025020, 0x00000C0B);
	for (i = 0; i < 8; i++) {
		value =  i << 16;

		RHW_WRITE(0x80000408, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}
}

static inline void btmtk_dump_mcu_pc_lr(void)
{
	uint32_t i = 0, value, cr_count = 0x55;

	BT_DUMP_CR_INIT(cr_count);
	BTMTK_INFO("%s [BG MCU PC/LR log] - mcu_flg7[84:168] cpu_dbg_pc_log0 ~ conn_debug_port84 count[%d]"
				, DBG_TAG, cr_count);

	/* mcu_flag7 */
	RHW_WRITE(0x81025020, 0x00000E0D);
	for (i = 0; i <= 0x54; i++) {
		RHW_WRITE(0x80000400, i);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}
}

static inline void btmtk_dump_dsp_pc_lr(void)
{
	uint32_t i = 0, value, cr_count = 0x44;

	BT_DUMP_CR_INIT(cr_count);
	BTMTK_INFO("%s [BG DSP PC/LR log] - mcu_flg7[169:236] cpu1_dbg_pc_log0 ~ cpu1_lr count[%d]"
				, DBG_TAG, cr_count);

	/* mcu_flag7 */
	RHW_WRITE(0x81025020, 0x00000E0D);
	for (i = 0x55; i <= 0x98; i++) {
		RHW_WRITE(0x80000400, i);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}
}

static inline void btmtk_dump_peri_debug_flags(void)
{
	uint32_t i = 0, value, cr_count = 4 + 6;

	BT_DUMP_CR_INIT(cr_count);
	BTMTK_INFO("%s [BG PERI debug flags] - mcu_flg11, mcu_flg12 count[%d]", DBG_TAG, cr_count);

	/* mcu_flag11 */
	for (i = 0; i < 4; i++) {
		value = 0x00001615 | (i << 16);
		RHW_WRITE(0x81025020, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}

	/* mcu_flag12 */
	for (i = 0; i < 6; i++) {
		value = 0x00001817 | (i << 16);
		RHW_WRITE(0x81025020, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}
}

static inline void btmtk_dump_bus_debug_flags(void)
{
	uint32_t i = 0, value, cr_count = 10 + 15 + 17;

	BT_DUMP_CR_INIT(cr_count);
	BTMTK_INFO("%s [BG BUS debug flags] - mcu_flg13, mcu_flg14, mcu_flg16 count[%d]", DBG_TAG, cr_count);

	/* mcu_flag13 */
	RHW_WRITE(0x81025020, 0x00001A19);

	for (i = 0; i <= 9; i++) {
		value = (i << 12);
		RHW_WRITE(0x80000408, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}

	/* mcu_flg14 */
	for (i = 0; i < 0xF; i++) {
		value = 0x00001C1B | (i << 16);
		RHW_WRITE(0x81025020, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}

	/* mcu_flg16 */
	for (i = 0; i <= 0x10; i++) {
		value = 0x0000201F | (i << 16);
		RHW_WRITE(0x81025020, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}
}

static inline void btmtk_dump_dma_uart_debug_flags(void)
{
	uint32_t i = 0, value, cr_count = 8 + 1 + 14 + 17;

	BT_DUMP_CR_INIT(cr_count);
	BTMTK_INFO("%s [BG DMA and UART debug flags] - mcu_flg19, mcu_flg20, mcu_flg23, mcu_flg24 count[%d]"
				, DBG_TAG, cr_count);

	/* mcu_flag19 */
	RHW_WRITE(0x81025020, 0x00002625);
	for (i = 0; i <= 7; i++) {
		value = (i << 8);
		RHW_WRITE(0x80000408, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}

	/* mcu_flg20 */
	RHW_WRITE(0x81025020, 0x00002827);
	RHW_READ(0x8000040C, &value);
	if (BT_DUMP_CR_PRINT(value))
		return;

	/* mcu_flag23 */
	RHW_WRITE(0x81025020, 0x00002E2D);
	for (i = 0; i <= 0x0D; i++) {
		value = (i << 8);
		RHW_WRITE(0x80000404, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}

	/* mcu_flag24 */
	RHW_WRITE(0x81025020, 0x0000302F);
	for (i = 0; i <= 0x0F; i++) {
		value = (i << 16);
		RHW_WRITE(0x80000404, value);
		RHW_READ(0x8000040C, &value);
		if (BT_DUMP_CR_PRINT(value))
			return;
	}

	RHW_WRITE(0x80000404, 0x001E0000);
	BT_DUMP_CR_PRINT(value);
}

static inline void btmtk_dump_cryto_debug_flags(void)
{
	uint32_t value, cr_count = 1 + 1;

	BT_DUMP_CR_INIT(cr_count);
	BTMTK_INFO("%s [BG CRYPTO debug flags] - mcu_flg21, mcu_flg22 (optional) count[%d]", DBG_TAG, cr_count);

	/* mcu_flag21 */
	RHW_WRITE(0x81025020, 0x00002827);
	RHW_READ(0x8000040C, &value);
	BT_DUMP_CR_PRINT(value);

	/* mcu_flg22 */
	RHW_WRITE(0x81025020, 0x00002A29);
	RHW_READ(0x8000040C, &value);
	BT_DUMP_CR_PRINT(value);
}

/*	connv3_conninfra_bus_dump(enum connv3_drv_type drv_type, struct connv3_cr_cb *cb, void * priv_data)
 *
 *	drv_type: driver type
 *	cb: callback function provided by subsys to read/write CR
 *	addr is fw view
 */

static int btmtk_connv3_cr_read_cb(void* priv_data, unsigned int addr, unsigned int *value)
{
	return RHW_READ(addr, value);
}

static int btmtk_connv3_cr_write_cb(void* priv_data, unsigned int addr, unsigned int value)
{
	return RHW_WRITE(addr, value);
}

static int btmtk_connv3_cr_write_mask_cb(void* priv_data, unsigned int addr, unsigned int mask, unsigned int value)
{
	int ret = 0;
	uint32_t org_value;
	ret = RHW_READ(addr, &org_value);
	if (ret < 0) {
		BTMTK_ERR("%s: read [%x] err", __func__, addr);
		return ret;
	}

	org_value = (org_value & ~mask ) | value;

	return RHW_WRITE(addr, org_value);
}

struct connv3_cr_cb btmtk_connv3_cr_cb = {
	.read = btmtk_connv3_cr_read_cb,
	.write = btmtk_connv3_cr_write_cb,
	.write_mask = btmtk_connv3_cr_write_mask_cb
};

void btmtk_uart_sp_dump_debug_sop(struct btmtk_dev *bdev)
{
	if (bdev == NULL) {
		BTMTK_ERR("%s: bdev is NULL", __func__);
		return;
	}
	BTMTK_INFO("%s: start", __func__);
	g_dump_bdev = bdev;
	btmtk_dump_bg_mcu_core();
	btmtk_dump_dsp_debug_flags();
	btmtk_dump_mcusys_clk_gals_debug_flags();
	btmtk_dump_mcu_pc_lr();
	btmtk_dump_dsp_pc_lr();
	btmtk_dump_peri_debug_flags();
	btmtk_dump_bus_debug_flags();
	btmtk_dump_dma_uart_debug_flags();
	btmtk_dump_cryto_debug_flags();
	BTMTK_INFO("%s: connv3_conninfra_bus_dump start", __func__);
	connv3_conninfra_bus_dump(CONNV3_DRV_TYPE_BT, &btmtk_connv3_cr_cb, NULL);
	BTMTK_INFO("%s: end", __func__);
}

