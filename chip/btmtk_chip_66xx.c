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

#include "btmtk_chip_66xx.h"
#include "btmtk_chip_common.h"

static int btmtk_load_fw_patch(struct btmtk_dev *bdev)
{
	int err = 0;

	BTMTK_INFO("%s: enter", __func__);

	err = btmtk_load_rom_patch_connac3(bdev, BT_DOWNLOAD);
	if (err < 0) {
		BTMTK_ERR("%s: btmtk_load_rom_patch_connac3 bt patch failed!", __func__);
		return err;
	}

	return err;
}

static int btmtk_cif_rx_packet_handler(struct btmtk_dev *bdev, struct sk_buff *skb)
{
	BTMTK_DBG("%s start", __func__);
#if (USE_DEVICE_NODE == 1)
	return rx_skb_enqueue(skb);
#else
	return 0;
#endif
}

static void btmtk_recv_error_handler(struct hci_dev *hdev,
		const u8 *buf, u32 len, const u8 *dbg_buf, u32 dbg_len)
{
	btmtk_set_sleep(hdev, FALSE);
}

static int btmtk_cif_bt_setup(struct hci_dev *hdev)
{
	int ret = 0;
	struct btmtk_main_info *bmain_info = btmtk_get_main_info();

	BTMTK_INFO("%s", __func__);

	ret = bmain_info->hif_hook.open(hdev);
	if (ret)
		BTMTK_ERR("%s: fail", __func__);
	return ret;
}

static int btmtk_cif_bt_flush(struct hci_dev *hdev)
{
	struct btmtk_main_info *bmain_info = btmtk_get_main_info();
	struct btmtk_dev *bdev =  hci_get_drvdata(hdev);

	BTMTK_INFO("%s", __func__);

	return bmain_info->hif_hook.flush(bdev);
}

int btmtk_cif_chip_66xx_register(void)
{
	int retval = 0;
	struct btmtk_main_info *bmain_info = btmtk_get_main_info();

	BTMTK_INFO("%s", __func__);

	bmain_info->hif_hook_chip.cap_init = NULL;
	bmain_info->hif_hook_chip.load_patch = btmtk_load_fw_patch;
	bmain_info->hif_hook_chip.err_handler = btmtk_recv_error_handler;
	bmain_info->hif_hook_chip.rx_handler = btmtk_cif_rx_packet_handler;
	bmain_info->hif_hook_chip.bt_setup_handler = btmtk_cif_bt_setup;
	bmain_info->hif_hook_chip.bt_flush_handler = btmtk_cif_bt_flush;
	bmain_info->hif_hook_chip.bt_tx_frame_handler = NULL;
	bmain_info->hif_hook_chip.bt_check_power_status = NULL;
	bmain_info->hif_hook_chip.bt_set_pinmux = NULL;
	bmain_info->hif_hook_chip.dispatch_fwlog = btmtk_dispatch_fwlog;
	bmain_info->hif_hook_chip.support_woble = 0;

	return retval;
}

