/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include "bt_hw_dbg.h"

#define MT6653_BT_DEBUGSOP_DUMP_VERSION "20231114"

const struct bt_dbg_command mt6653_bgsys_top_b[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023a0c, 0, 0xc0000000, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000001, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000002, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000003, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000004, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000005, true, 0x20023a10},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_b = {
	"PSOP_5_1_B", "SectionB - vcore_off(bg_bt_cfg_Debug_Signal)",
	6, sizeof(mt6653_bgsys_top_b)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_b
};

const struct bt_dbg_command mt6653_bgsys_top_c[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023a0c, 0, 0xc0000100, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000101, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000102, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000103, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000104, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0000105, true, 0x20023a10},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_c = {
	"PSOP_5_1_C", "SectionC - vcore_off(bg_bt_clkgen_off_Debug_Signal)",
	6, sizeof(mt6653_bgsys_top_c)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_c
};

const struct bt_dbg_command mt6653_bgsys_top_d[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023a0c, 0, 0xc0000200, true, 0x20023a10},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_d = {
	"PSOP_5_1_D", "SectionD - vcore_off(bg_bt_met_Debug_Signal)",
	1, sizeof(mt6653_bgsys_top_d)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_d
};

const struct bt_dbg_command mt6653_bgsys_top_e[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023a1c, 0, 0x300040, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300041, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300042, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300043, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300044, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300045, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300046, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300047, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300048, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300049, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30004a, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30004b, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30004c, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30004d, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30004e, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30004f, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300050, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300051, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300052, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300053, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300054, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300055, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300056, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300057, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300058, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300059, true, 0x20023a20},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_e = {
	"PSOP_5_1_E", "SectionE - vcore_on(bg_bt_cfg_on_Debug_Signal)",
	26, sizeof(mt6653_bgsys_top_e)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_e
};

const struct bt_dbg_command mt6653_bgsys_top_f[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023a1c, 0, 0x300001, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300002, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300003, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300004, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300005, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300006, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300007, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300008, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300009, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30000a, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30000b, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30000c, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30000d, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x30000e, true, 0x20023a20},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_f = {
	"PSOP_5_1_F", "SectionF - vcore_on(bg_bt_rgu_on_Debug_Signal)",
	14, sizeof(mt6653_bgsys_top_f)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_f
};

const struct bt_dbg_command mt6653_bgsys_top_g[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023a1c, 0, 0x300080, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300081, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300082, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300083, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300084, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300100, true, 0x20023a20},
	{true, 0x20023a1c, 0, 0x300140, true, 0x20023a20},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_g = {
	"PSOP_5_1_G", "SectionG - vcore_on(bg_bt_rgu_on_Debug_Signal)",
	7, sizeof(mt6653_bgsys_top_g)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_g
};

const struct bt_dbg_command mt6653_bgsys_top_h[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023c04, 0, 0x30000, true, 0x20023c00},
	{true, 0x20023c04, 0, 0x30010, true, 0x20023c00},
	{true, 0x20023c04, 0, 0x30020, true, 0x20023c00},
	{true, 0x20023c04, 0, 0x30050, true, 0x20023c00},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_h = {
	"PSOP_5_1_H", "SectionH - vlp(bg_bt_misc_vlp_Debug_Signal)",
	4, sizeof(mt6653_bgsys_top_h)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_h
};

const struct bt_dbg_command mt6653_bgsys_top_i[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023c04, 0, 0x30040, true, 0x20023c00},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_i = {
	"PSOP_5_1_I", "SectionI - vlp(bg_bt_met_on_Debug_Signal)",
	1, sizeof(mt6653_bgsys_top_i)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_i
};

const struct bt_dbg_command mt6653_bgsys_top_j[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023c04, 0, 0x30100, true, 0x20023c00},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_j = {
	"PSOP_5_1_J", "SectionJ - vlp(bg_bt_kmem_Debug_Signal)",
	1, sizeof(mt6653_bgsys_top_j)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_j
};

const struct bt_dbg_command mt6653_bgsys_top_a[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{false, 0, 0, 0, true, 0x81030000},
	{false, 0, 0, 0, true, 0x81030020},
	{false, 0, 0, 0, true, 0x81030028},
	{false, 0, 0, 0, true, 0x81030030},
	{false, 0, 0, 0, true, 0x81030034},
	{false, 0, 0, 0, true, 0x81030128},
	{false, 0, 0, 0, true, 0x8103012c},
	{false, 0, 0, 0, true, 0x81021120},
	{false, 0, 0, 0, true, 0x81021148},
	{false, 0, 0, 0, true, 0x81021200},
	{false, 0, 0, 0, true, 0x81021210},
	{false, 0, 0, 0, true, 0x81021504},
	{false, 0, 0, 0, true, 0x81021508},
	{false, 0, 0, 0, true, 0x8102150c},
	{false, 0, 0, 0, true, 0x81021510},
	{false, 0, 0, 0, true, 0x81021640},
	{false, 0, 0, 0, true, 0x81021644},
	{false, 0, 0, 0, true, 0x81021648},
	{false, 0, 0, 0, true, 0x8102164c},
	{false, 0, 0, 0, true, 0x81021700},
	{false, 0, 0, 0, true, 0x81020004},
	{false, 0, 0, 0, true, 0x81020010},
	{false, 0, 0, 0, true, 0x8102006c},
	{false, 0, 0, 0, true, 0x81020088},
	{false, 0, 0, 0, true, 0x81020090},
	{false, 0, 0, 0, true, 0x80021100},
	{false, 0, 0, 0, true, 0x80021104},
	{false, 0, 0, 0, true, 0x80021108},
	{false, 0, 0, 0, true, 0x80021114},
	{false, 0, 0, 0, true, 0x80021118},
	{false, 0, 0, 0, true, 0x80021120},
	{false, 0, 0, 0, true, 0x80021124},
	{false, 0, 0, 0, true, 0x80021150},
	{false, 0, 0, 0, true, 0x80021168},
	{false, 0, 0, 0, true, 0x8002116c},
	{false, 0, 0, 0, true, 0x80021170},
	{false, 0, 0, 0, true, 0x80021174},
	{false, 0, 0, 0, true, 0x80021178},
	{false, 0, 0, 0, true, 0x8002117c},
	{false, 0, 0, 0, true, 0x80021400},
};

const struct bt_dump_list mt6653_dump_list_bgsys_top_a = {
	"PSOP_5_2_A", "SectionA - bg_top_apb_cr_bdg",
	40, sizeof(mt6653_bgsys_top_a)/sizeof(struct bt_dbg_command),
	mt6653_bgsys_top_a
};

const struct bt_dbg_command mt6653_bt_mcu_bus_timeout_hostcsr_a[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023a0c, 0, 0xc0041f00, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f01, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f02, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f03, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f04, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f05, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f06, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f07, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f08, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f09, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f0a, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f0b, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f0c, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f0d, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f0e, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f0f, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041f10, true, 0x20023a10},
};

const struct bt_dump_list mt6653_dump_list_bt_mcu_bus_timeout_hostcsr_a = {
	"PSOP_6_2_A", "SectionA - Bus timeout CR",
	17, sizeof(mt6653_bt_mcu_bus_timeout_hostcsr_a)/sizeof(struct bt_dbg_command),
	mt6653_bt_mcu_bus_timeout_hostcsr_a
};

const struct bt_dbg_command mt6653_bt_mcu_bus_timeout_hostcsr_b[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023a0c, 0, 0xc0041910, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041920, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041930, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041940, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041950, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041960, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041970, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041980, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041990, true, 0x20023a10},
};

const struct bt_dump_list mt6653_dump_list_bt_mcu_bus_timeout_hostcsr_b = {
	"PSOP_6_2_B", "SectionB - bus vdnr debug probe",
	9, sizeof(mt6653_bt_mcu_bus_timeout_hostcsr_b)/sizeof(struct bt_dbg_command),
	mt6653_bt_mcu_bus_timeout_hostcsr_b
};

const struct bt_dbg_command mt6653_bt_mcu_bus_timeout_hostcsr_c[] = {
	/* write, w_addr, mask, value, read, r_addr*/
	{true, 0x20023a0c, 0, 0xc0041b00, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b01, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b02, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b03, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b04, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b05, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b06, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b07, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b08, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b09, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b0a, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b0b, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b0c, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b0d, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b0e, true, 0x20023a10},
	{true, 0x20023a0c, 0, 0xc0041b0f, true, 0x20023a10},
};

const struct bt_dump_list mt6653_dump_list_bt_mcu_bus_timeout_hostcsr_c = {
	"PSOP_6_2_C", "SectionC - bus manual debug probe",
	16, sizeof(mt6653_bt_mcu_bus_timeout_hostcsr_c)/sizeof(struct bt_dbg_command),
	mt6653_bt_mcu_bus_timeout_hostcsr_c
};

