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

#include "btmtk_chip_6639.h"
#include "btmtk_chip_common.h"


enum {
	BTMTK_CAL_DATA_STATE_START = 1,
	BTMTK_CAL_DATA_STATE_CONTINUE,
	BTMTK_CAL_DATA_STATE_END,
	BTMTK_CAL_DATA_STATE_UNKNOWN,
};

static char calibration_status = BTMTK_CAL_DATA_STATE_UNKNOWN;
#define CAL_DATA_TIMO		5000

void btmtk_cif_connac3_calibration_backup_free(struct btmtk_dev *bdev)
{
	BTMTK_INFO("%s enter", __func__);

	if (bdev->cali_backup.num) {
		BTMTK_INFO("%s: clear %d calibration data", __func__, bdev->cali_backup.num);
		btmtk_free_fw_cfg_struct(bdev->cali_backup.cal_data, CAL_DATA_PACKET_NUM);
		bdev->cali_backup.num = 0;
	}
	BTMTK_INFO("%s end", __func__);
}

int btmtk_cif_connac3_calibration_restore(struct btmtk_dev *bdev)
{
	int ret = 0;
	u32 packet_len = 0;
	int first_block = 1;
	u8 phase;
	int delay = PATCH_DOWNLOAD_PHASE1_2_DELAY_TIME;
	int retry = PATCH_DOWNLOAD_PHASE1_2_RETRY;
	u8 *image = NULL;
	u8 *data_buf = NULL;
	u8 cmd[CAL_DATA_CMD_LEN] = { 0x02, 0x6F, 0xFC, 0x06, 0x01, 0x14, 0x02, 0x00, 0x02, 0x00 };
	u8 event[CAL_DATA_EVNET_LEN] = {0x04, 0xE4, 0X06, 0x02, 0x14, 0x02, 0x00, 0x00, 0x00};/* event[8] is status*/
	//s32 sent_len_max = UPLOAD_PATCH_UNIT - PATCH_HEADER_SIZE;
	int i = 0;

	BTMTK_INFO("%s enter", __func__);

	if (bdev == NULL) {
		BTMTK_WARN("%s, invalid parameters!", __func__);
		ret = -1;
		goto exit;
	}

	ret = btmtk_main_send_cmd(bdev,
			cmd, CAL_DATA_CMD_LEN,
			event, CAL_DATA_EVNET_LEN,
			WMT_DELAY_TIMES, RETRY_TIMES, BTMTK_TX_CMD_FROM_DRV);
	if (ret < 0) {
		BTMTK_ERR("%s: failed(%d)", __func__, ret);
		ret = -1;
	} else if (ret == 0 && bdev->recv_evt_len > 0) {
		switch (bdev->io_buf[CAL_DATA_EVNET_LEN - 1]) {
		case 0:			 /* successful */
			BTMTK_INFO("%s: calibration restore begin!!", __func__);
			break;
		default:
			BTMTK_WARN("%s: no need to restore", __func__);
			ret = 0;
			goto exit;
		}
	}

	image = kmalloc(UPLOAD_PATCH_UNIT, GFP_ATOMIC);
	if (!image) {
		BTMTK_ERR("%s: alloc memory failed", __func__);
		ret = -1;
		goto exit;
	}

	for (i = 0; i < bdev->cali_backup.num; i++) {
		data_buf = bdev->cali_backup.cal_data[i].content;
		packet_len = bdev->cali_backup.cal_data[i].length;

		if (packet_len > 0) {
			if (first_block == 1) {
				if (i == (bdev->cali_backup.num - 1))
					phase = PATCH_PHASE3;
				else
					phase = PATCH_PHASE1;
				first_block = 0;
			} else if (packet_len == 1024) {
				if (i == (bdev->cali_backup.num - 1))
					phase = PATCH_PHASE3;
				else
					phase = PATCH_PHASE2;
			} else {
				phase = PATCH_PHASE3;
			}
			/* prepare HCI header */
			image[0] = 0x02;
			image[1] = 0x6F;
			image[2] = 0xFC;
			image[3] = (packet_len + 8) & 0xFF;
			image[4] = ((packet_len + 8) >> 8) & 0xFF;

			/* prepare WMT header */
			image[5] = 0x01;
			image[6] = 0x14;
			image[7] = (packet_len + 4) & 0xFF;
			image[8] = ((packet_len + 4) >> 8) & 0xFF;

			image[9] = 0x02;
			image[10] = phase;
			image[11] = (packet_len) & 0xFF;
			image[12] = ((packet_len) >> 8) & 0xFF;
			memcpy(&image[13], data_buf, packet_len);

			BTMTK_INFO("%s: current cal data num is %d", __func__, i);
			BTMTK_INFO_RAW(image, packet_len + 12, "%s, send, len = %d", __func__, (packet_len + 12));

			ret = btmtk_main_send_cmd(bdev, image, packet_len + 12,
					event, CAL_DATA_EVNET_LEN - 1, delay, retry, BTMTK_TX_ACL_FROM_DRV);
			if (ret < 0) {
				BTMTK_ERR("%s: send cal data failed, terminate", __func__);
				goto err;
			}
		} else
			break;
	}

err:
	kfree(image);
	image = NULL;

exit:
	return ret;

}

int btmtk_cif_connac3_calibration_backup_send(struct btmtk_dev *bdev)
{
	u8 cmd[BACK_UP_CMD_LEN] = {0x02, 0x6F, 0xFC, 0x05, 0x00, 0x01, 0x14, 0x01, 0x00, 0x03};
	int cmd_len = BACK_UP_CMD_LEN;
	int ret;
	unsigned long single_timo = 0, start_time = 0, total_timo = 0;

	BTMTK_INFO("%s enter", __func__);

	start_time = jiffies;
	/* check hci event /wmt event for SDIO/UART interface, check hci
	 * event for USB interface
	 */
	single_timo = jiffies + msecs_to_jiffies(CAL_DATA_TIMO);
	total_timo = jiffies + msecs_to_jiffies(CAL_DATA_TIMO * 4);

	btmtk_cif_connac3_calibration_backup_free(bdev);
	/* save calibration data in btmtk_dispatch_fwlog */
	do {
		BTMTK_INFO_RAW(cmd, cmd_len, "%s, send, len(%d)", __func__, cmd_len);
		ret = btmtk_main_send_cmd(bdev, cmd, cmd_len,
			NULL, 0, DELAY_TIMES, RETRY_TIMES, BTMTK_TX_ACL_FROM_DRV);
		while (time_before(jiffies, single_timo)) {
			usleep_range(10, 100);
			if (calibration_status <= BTMTK_CAL_DATA_STATE_END) {
				ret = 0;
				break;
			}
		}

		if (calibration_status == BTMTK_CAL_DATA_STATE_UNKNOWN) {
			BTMTK_ERR("%s: calibration_status error", __func__);
			ret = -1;
			break;
		}
		BTMTK_INFO("%s: calibration_status %d", __func__, calibration_status);
		if (calibration_status == BTMTK_CAL_DATA_STATE_END) {
			BTMTK_INFO("%s: calibration recv end", __func__);
			calibration_status = BTMTK_CAL_DATA_STATE_UNKNOWN;/* re-init for next time */
			ret = 0;
			break;
		}
		calibration_status = BTMTK_CAL_DATA_STATE_UNKNOWN;
	} while (time_before(jiffies, total_timo));

	BTMTK_INFO("%s end", __func__);

	return ret;
}

int btmtk_cif_connac3_calibration_backup_save(struct btmtk_dev *bdev, struct sk_buff *skb)
{
	u32 pkt_len = 0;

	BTMTK_INFO("%s", __func__);

	if ((bt_cb(skb)->pkt_type == HCI_ACLDATA_PKT) &&
				skb->data[0] == 0xff &&
				skb->data[1] <= 0x03) {
		/* calibration data status
		 * 0x01: start;
		 * 0x02: continue;
		 * 0x03: end
		 */
		BTMTK_INFO_RAW(skb->data, skb->len, "%s, len = %d", __func__, skb->len);
		calibration_status = skb->data[1];
		BTMTK_INFO("%s: calibration_status %d", __func__, calibration_status);
		/* FF 01 LL LL data ...*/
		pkt_len = (skb->data[3] << 8) + skb->data[2];
		bdev->cali_backup.cal_data[bdev->cali_backup.num].length = pkt_len;
		bdev->cali_backup.cal_data[bdev->cali_backup.num].content = kzalloc(pkt_len, GFP_KERNEL);
		memcpy(bdev->cali_backup.cal_data[bdev->cali_backup.num].content, skb->data + 4, pkt_len);
		bdev->cali_backup.num++;
		BTMTK_INFO("%s: recv %d calibration data", __func__, bdev->cali_backup.num);
		return 1; /* not send to host */
	}
	return 0;
}

static int btmtk_chip_reg_init(struct btmtk_dev *bdev)
{
	int ret = 0;
	struct btmtk_main_info *bmain_info = btmtk_get_main_info();

	ret = bmain_info->hif_hook.reg_read(bdev, FLAVOR, &bdev->flavor);
	if (ret < 0) {
		BTMTK_ERR("read flavor id failed");
		ret = -EIO;
		return ret;
	}
	ret = bmain_info->hif_hook.reg_read(bdev, FW_VERSION, &bdev->fw_version);
	if (ret < 0) {
		BTMTK_ERR("read fw version failed");
		ret = -EIO;
		return ret;
	}

	BTMTK_INFO("%s: Chip ID = 0x%x", __func__, bdev->chip_id);
	BTMTK_INFO("%s: flavor = 0x%x", __func__, bdev->flavor);
	BTMTK_INFO("%s: FW Ver = 0x%x", __func__, bdev->fw_version);

	memset(bdev->rom_patch_bin_file_name, 0, MAX_BIN_FILE_NAME_LEN);
	if ((bdev->fw_version & 0xff) == 0xff) {
		BTMTK_ERR("%s: failed, wrong FW version : 0x%x !", __func__, bdev->fw_version);
		ret = -1;
		return ret;
	}

	bdev->dualBT = 0;

	/* Bin filename format : "BT_RAM_CODE_MT%04x_%x_%x_hdr.bin"
	 *	$$$$ : chip id
	 *	% : fw version & 0xFF + 1 (in HEX)
	 */

	bdev->proj = (bdev->flavor & 0x00000001);
	bdev->flavor = (bdev->flavor & 0x00000080) >> 7;

	BTMTK_INFO("%s: proj = 0x%x", __func__, bdev->proj);
	BTMTK_INFO("%s: flavor1 = 0x%x", __func__, bdev->flavor);
	return ret;
}

static int btmtk_bt_open(struct btmtk_dev *bdev)
{
	int ret = 0;	/* if successful, 0 */

	BTMTK_INFO("%s", __func__);

	btmtk_dynamic_load_rom_patch(bdev, 1);

	ret = btmtk_send_cmd_to_fw(bdev,
			SET_EFEM1_CMD, SET_EFEM1_EVT,
			WMT_DELAY_TIMES, RETRY_TIMES, BTMTK_TX_CMD_FROM_DRV);
	if (ret < 0) {
		BTMTK_ERR("%s, btmtk_calibration_flow failed!", __func__);
		goto exit;
	}

exit:
	BTMTK_INFO("%s: ret %d", __func__, ret);
	return ret;

}

int btmtk_load_fw_patch(struct btmtk_dev *bdev)
{
	int err = 0;
	int retry = 20;
	int patch_status = 0;

	BTMTK_INFO("%s: enter", __func__);

	err = btmtk_load_rom_patch_connac3(bdev, BT_DOWNLOAD);
	if (err < 0) {
		BTMTK_ERR("%s: btmtk_load_rom_patch_connac3 bt patch failed!", __func__);
		return err;
	}

	UNUSED(retry);
	UNUSED(patch_status);
#if CFG_SUPPORT_BT_DL_WIFI_PATCH
	retry = 20;
	do {
		err = btmtk_send_cmd_to_fw(bdev,
				WIFI_PATCH_QUERY_CMD, WIFI_PATCH_QUERY_EVT,
				DELAY_TIMES, RETRY_TIMES, BTMTK_TX_CMD_FROM_DRV);
		if (err < 0) {
			BTMTK_ERR("%s: send wifi patch query failed!", __func__);
			return PATCH_ERR;
		}

		if (bdev->recv_evt_len >= WIFI_PATCH_QUERY_EVT_LEN) {
			patch_status = bdev->io_buf[WIFI_PATCH_QUERY_EVT_LEN];
			BTMTK_INFO_RAW(bdev->io_buf, bdev->recv_evt_len, "%s WIFI patch OK: EVT:", __func__);
		}

		BTMTK_INFO("%s: patch_status %d", __func__, patch_status);

		if (patch_status > PATCH_READY || patch_status == PATCH_ERR) {
			BTMTK_ERR("%s: patch_status error", __func__);
			err = 0;
			return err;
		} else if (patch_status == PATCH_READY) {
			BTMTK_INFO("%s: no need to load rom patch", __func__);
			err = 0;
			return err;
		} else if (patch_status == PATCH_IS_DOWNLOAD_BY_OTHER) {
			msleep(100);
			retry--;
		} else if (patch_status == PATCH_NEED_DOWNLOAD) {
			break;	/* Download ROM patch directly */
		}
	} while (retry > 0);

	if (patch_status == PATCH_IS_DOWNLOAD_BY_OTHER) {
		BTMTK_WARN("%s: Hold by another fun more than 2 seconds", __func__);
		err = 0;
		return err;
	}

	err = btmtk_load_rom_patch_connac3(bdev, WIFI_DOWNLOAD);
	if (err < 0) {
		BTMTK_WARN("%s: btmtk_load_rom_patch_connac3 wifi patch failed!", __func__);
		err = 0;
		return err;
	}

	err = btmtk_send_cmd_to_fw(bdev,
			WIFI_PATCH_ENABLE_CMD, WIFI_PATCH_ENABLE_EVT,
			DELAY_TIMES, RETRY_TIMES, BTMTK_TX_CMD_FROM_DRV);
	if (err < 0) {
		BTMTK_ERR("%s: send wifi patch enable failed!", __func__);
		return PATCH_ERR;
	}
	BTMTK_INFO("%s: wifi patch download end", __func__);
#endif
	return err;
}

int btmtk_cif_chip_6639_register(void)
{
	int retval = 0;
	struct btmtk_main_info *bmain_info = btmtk_get_main_info();

	BTMTK_INFO("%s", __func__);

	bmain_info->hif_hook_chip.cap_init = btmtk_chip_reg_init;
	bmain_info->hif_hook_chip.restore = btmtk_cif_connac3_calibration_restore;
	bmain_info->hif_hook_chip.backup_send = btmtk_cif_connac3_calibration_backup_send;
	bmain_info->hif_hook_chip.backup_save = btmtk_cif_connac3_calibration_backup_save;
	bmain_info->hif_hook_chip.backup_free = btmtk_cif_connac3_calibration_backup_free;
	bmain_info->hif_hook_chip.bt_open_handler = btmtk_bt_open;
	bmain_info->hif_hook_chip.load_patch = btmtk_load_fw_patch;

	return retval;
}

