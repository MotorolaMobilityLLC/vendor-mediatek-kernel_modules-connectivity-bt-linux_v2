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
#include "btmtk_define.h"
#include "btmtk_sdio.h"
#include "btmtk_main.h"

/*static const struct btmtksdio_data btmtk_sdio_7663 = {
	.fwname = FIRMWARE_MT7663,
};

static const struct btmtksdio_data btmtk_sdio_7961 = {
	.fwname = FIRMWARE_MT7668,
};*/
int btmtk_sdio_readl(u32 offset,  u32 *val, struct sdio_func *func);
int btmtk_sdio_writel(u32 offset, u32 val, struct sdio_func *func);

static const struct sdio_device_id btmtk_sdio_tabls[] = {
	/* Mediatek SD8688 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7663)/*,
			.driver_data = (unsigned long) &btmtk_sdio_7663*/ },

	/* Bring-up only */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7668)/*,
			.driver_data = (unsigned long) &btmtk_sdio_7663*/ },

	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7961)/*,
			.driver_data = (unsigned long) &btmtk_sdio_7961*/ },

	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE(sdio, btmtk_sdio_tabls);

int btmtk_sdio_set_own_back(struct btmtk_dev *bdev, int owntype, int retry)
{
	/*Set driver own*/
	int ret = 0, retry_ret = 0;
	u32 u32LoopCount = 0;
	u32 u32ReadCRValue = 0;
	u32 ownValue = 0;
	int i = 0;

	BTMTK_DBG("%s owntype %d\n", __func__, owntype);

	if (owntype == FW_OWN) {
		if (bdev->no_fw_own) {
			ret = btmtk_sdio_readl(SWPCDBGR, &u32ReadCRValue, bdev->func);
			printk_ratelimited(KERN_WARNING
				"%s no_fw_own is on, just return, u32ReadCRValue = 0x%08X, ret = %d\n",
				__func__, u32ReadCRValue, ret);
			return ret;
		}
	}

	ret = btmtk_sdio_readl(CHLPCR, &u32ReadCRValue, bdev->func);

	BTMTK_DBG("%s CHLPCR = 0x%0x",__func__, u32ReadCRValue);

	/* For CHLPCR, bit 8 could help us to check driver own or fw own
	 * 0: COM driver doesn't have ownership
	 * 1: COM driver has ownership
	 */
	if (owntype == DRIVER_OWN &&
			(u32ReadCRValue & 0x100) == 0x100) {
		goto set_own_end;
	} else if (owntype == FW_OWN &&
			(u32ReadCRValue & 0x100) == 0) {
		goto set_own_end;
	}

	if (owntype == DRIVER_OWN)
		ownValue = 0x00000200;
	else
		ownValue = 0x00000100;

retry_own:

	/* Write CR for Driver or FW own */
	ret = btmtk_sdio_writel(CHLPCR, ownValue, bdev->func);
	if (ret) {
		ret = -EINVAL;
		goto done;
	}

	u32LoopCount = 1000;

	/* To-do refactor own flow.
	do {
		usleep_range(100, 200);
		ret = btmtk_sdio_readl(CHLPCR, &u32ReadCRValue, bdev->func);
		u32LoopCount--;

		if (owntype == DRIVER_OWN) {

		} else (owntype == FW_OWN) {

		}
	} while (u32LoopCount > 0)*/


	if (owntype == DRIVER_OWN) {
		do {
			usleep_range(100, 200);
			ret = btmtk_sdio_readl(CHLPCR, &u32ReadCRValue, bdev->func);
			u32LoopCount--;
			BTMTK_DBG("%s DRIVER_OWN btmtk_sdio_readl CHLPCR 0x%x\n",
				__func__, u32ReadCRValue);
		} while ((u32LoopCount > 0) &&
			((u32ReadCRValue&0x100) != 0x100));

		if ((u32LoopCount == 0) && (0x100 != (u32ReadCRValue&0x100))
				&& (retry > 0)) {
			pr_warn("%s retry set_check driver own, CHLPCR 0x%x\n",
				__func__, u32ReadCRValue);
			for (i = 0; i < 3; i++) {
				ret = btmtk_sdio_readl(SWPCDBGR, &u32ReadCRValue, bdev->func);
				BTMTK_WARN("%s ret %d,SWPCDBGR 0x%x, and not sleep!\n",
					__func__, ret, u32ReadCRValue);
			}

			retry--;
			mdelay(20);
			goto retry_own;
		}
	} else {
		do {
			usleep_range(100, 200);
			ret = btmtk_sdio_readl(CHLPCR, &u32ReadCRValue, bdev->func);
			u32LoopCount--;
			BTMTK_DBG("%s FW_OWN btmtk_sdio_readl CHLPCR 0x%x\n",
				__func__, u32ReadCRValue);
		} while ((u32LoopCount > 0) && ((u32ReadCRValue&0x100) != 0));

		if ((u32LoopCount == 0) &&
				((u32ReadCRValue&0x100) != 0) &&
				(retry > 0)) {
			BTMTK_WARN("%s retry set_check FW own, CHLPCR 0x%x\n",
				__func__, u32ReadCRValue);
			retry--;
			goto retry_own;
		}
	}

	BTMTK_DBG("%s CHLPCR(0x%x), is 0x%x\n",
		__func__, CHLPCR, u32ReadCRValue);

	if (owntype == DRIVER_OWN) {
		if ((u32ReadCRValue&0x100) == 0x100)
			BTMTK_DBG("%s check %04x, is 0x100 driver own success\n",
				__func__, CHLPCR);
		else {
			BTMTK_DBG("%s check %04x, is %x shuld be 0x100\n",
				__func__, CHLPCR, u32ReadCRValue);
			ret = -EINVAL;
			goto done;
		}
	} else {
		if (0x0 == (u32ReadCRValue&0x100))
			BTMTK_DBG("%s check %04x, bit 8 is 0 FW own success\n",
				__func__, CHLPCR);
		else{
			BTMTK_DBG("%s bit 8 should be 0, %04x bit 8 is %04x\n",
				__func__, u32ReadCRValue,
				(u32ReadCRValue&0x100));
			ret = -EINVAL;
			goto done;
		}
	}

done:
	if (owntype == DRIVER_OWN) {
		if (ret) {
			BTMTK_ERR("%s set driver own fail\n", __func__);
			for (i = 0; i < 8; i++) {
				retry_ret = btmtk_sdio_readl(SWPCDBGR, &u32ReadCRValue, bdev->func);
				BTMTK_ERR("%s ret %d,SWPCDBGR 0x%x, then sleep 200ms\n",
					__func__, retry_ret, u32ReadCRValue);
				msleep(200);
			}
		} else
			BTMTK_DBG("%s set driver own success\n", __func__);
	} else if (owntype == FW_OWN) {
		if (ret)
			BTMTK_ERR("%s set FW own fail\n", __func__);
		else
			BTMTK_DBG("%s set FW own success\n", __func__);
	} else
		BTMTK_ERR("%s unknown type %d\n", __func__, owntype);

set_own_end:

	return ret;
}

int btmtk_cif_read_register(struct btmtk_dev *bdev, u32 reg, u32 *val)
{
	int ret;
	u8 cmd[] = {0x01, 0x6F, 0xFC, 0x0C,
				0x01, 0x08, 0x08, 0x00,
				0x02, 0x01, 0x00, 0x01,
				0x00, 0x00, 0x00, 0x00};

	u8 event[] = {0x04, 0xE4, 0x10, 0x02,
			0x08, 0x0C, 0x00, 0x00,
			0x00, 0x00, 0x01};

	/* To-do using structure for sdio header
	struct btmtk_sdio_hdr *sdio_hdr;
	sdio_hdr = (void *) cmd;
	sdio_hdr->len = cpu_to_le16(skb->len);
	sdio_hdr->reserved = cpu_to_le16(0);
	sdio_hdr->bt_type = hci_skb_pkt_type(skb); */

	BTMTK_INFO("%s: read cr %x\n", __func__, reg);

	memcpy(&cmd[MCU_ADDRESS_OFFSET_CMD], &reg, sizeof(reg));

	ret = btmtk_main_send_cmd(bdev, cmd, sizeof(cmd), event, sizeof(event), 20,
			20, BTMTK_EP_TYPE_OUT_CMD);

	memcpy(val, bdev->io_buf + MCU_ADDRESS_OFFSET_EVT - HCI_TYPE_SIZE, sizeof(u32));
	*val = le32_to_cpu(*val);

	BTMTK_DBG("%s: reg=%x, value=0x%08x", __func__, reg, *val);

	return 0;
}

int btmtk_cif_send_calibration(struct btmtk_dev *bdev)
{
	return 0;
}

static int btmtk_cif_allocate_memory(struct btmtk_dev *bdev)
{
	BTMTK_INFO("%s", __func__);

	if (bdev->rom_patch_bin_file_name == NULL) {
		bdev->rom_patch_bin_file_name = kzalloc(MAX_BIN_FILE_NAME_LEN, GFP_KERNEL);
		if (!bdev->rom_patch_bin_file_name) {
			BTMTK_ERR("%s: alloc memory fail (bdev->rom_patch_bin_file_name)", __func__);
			return -1;
		}
	}

	if (bdev->io_buf == NULL) {
		bdev->io_buf = kzalloc(IO_BUF_SIZE, GFP_KERNEL);
		if (!bdev->io_buf) {
			BTMTK_ERR("%s: alloc memory fail (bdev->io_buf)", __func__);
			return -1;
		}
	}

	if (bdev->woble_setting_file_name == NULL) {
		bdev->woble_setting_file_name = kzalloc(MAX_BIN_FILE_NAME_LEN, GFP_KERNEL);
		if (!bdev->woble_setting_file_name) {
			BTMTK_ERR("%s: alloc memory fail (bdev->woble_setting_file_name)", __func__);
			return -1;
		}
	}

	if (bdev->bt_cfg_file_name == NULL) {
		bdev->bt_cfg_file_name = kzalloc(MAX_BIN_FILE_NAME_LEN, GFP_KERNEL);
		if (!bdev->bt_cfg_file_name) {
			BTMTK_ERR("%s: alloc memory fail (bdev->bt_cfg_file_name)", __func__);
			return -1;
		}
	}

	if (bdev->transfer_buf == NULL) {
		bdev->transfer_buf = kzalloc(URB_MAX_BUFFER_SIZE, GFP_KERNEL);
		if (!bdev->transfer_buf) {
			BTMTK_ERR("%s: alloc memory fail (bdev->transfer_buf)", __func__);
			return -1;
		}
	}

	if (bdev->sdio_packet == NULL) {
		bdev->sdio_packet = kzalloc(URB_MAX_BUFFER_SIZE, GFP_KERNEL);
		if (!bdev->sdio_packet) {
			BTMTK_ERR("%s: alloc memory fail (bdev->transfer_buf)", __func__);
			return -1;
		}
	}

	BTMTK_INFO("%s: Done", __func__);
	return 0;
}

static void btmtk_cif_free_memory(struct btmtk_dev *bdev)
{
	if (!bdev) {
		BTMTK_ERR("%s: bdev is NULL!", __func__);
		return;
	}

	kfree(bdev->rom_patch_bin_file_name);
	bdev->rom_patch_bin_file_name = NULL;

	kfree(bdev->woble_setting_file_name);
	bdev->woble_setting_file_name = NULL;

	kfree(bdev->bt_cfg_file_name);
	bdev->bt_cfg_file_name = NULL;

	kfree(bdev->io_buf);
	bdev->io_buf = NULL;

	kfree(bdev->sdio_packet);
	bdev->sdio_packet = NULL;

	kfree(bdev->sdio_packet);
	bdev->sdio_packet = NULL;

	BTMTK_INFO("%s: Success", __func__);
}

/*
static int btmtk_sdio_suspend(struct device *dev)
{
	return 0;
}

static int btmtk_sdio_resume(struct device *dev)
{
	return 0;
}
*/

static int btsdio_open(struct hci_dev *hdev)
{
	return 0;
}

static int btsdio_close(struct hci_dev *hdev)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	cancel_work_sync(&bdev->reset_waker);

	return 0;
}

int btmtk_cif_open(struct hci_dev *hdev)
{
	BTMTK_INFO("%s enter!", __func__);
	return btsdio_open(hdev);
}
int btmtk_cif_close(struct hci_dev *hdev)
{
	BTMTK_INFO("%s enter!", __func__);
	return btsdio_close(hdev);
}

static int btmtk_sdio_writesb(u32 offset, u8 *val, int len, struct sdio_func *func)
{
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("%s func is NULL\n", __func__);
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		ret = sdio_writesb(func, offset, val, len);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d\n", __func__, ret);
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
		BTMTK_ERR("%s func is NULL\n", __func__);
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		ret = sdio_readsb(func, val, offset, len);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d\n", __func__, ret);
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
		BTMTK_ERR("%s func is NULL\n", __func__);
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		sdio_writeb(func, val, offset, &ret);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d\n", __func__, ret);
			break;
		}
	} while (ret);

	return ret;
}

int btmtk_sdio_writel(u32 offset, u32 val, struct sdio_func *func)
{
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("%s func is NULL\n", __func__);
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		sdio_writel(func, val, offset, &ret);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d\n", __func__, ret);
			break;
		}
	} while (ret);

	return ret;
}

int btmtk_sdio_readl(u32 offset,  u32 *val, struct sdio_func *func)
{
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("func is NULL\n");
		return -EIO;
	}

	do {
		sdio_claim_host(func);
		*val = sdio_readl(func, offset, &ret);
		sdio_release_host(func);
		retry_count++;
		if (retry_count > SDIO_RW_RETRY_COUNT) {
			BTMTK_ERR(" %s, ret:%d\n", __func__, ret);
			break;
		}
	} while (ret);

	return ret;
}

static int btmtk_sdio_readb(u32 offset, u8 *val, struct sdio_func *func)
{
	/*struct btmtk_dev *bdev;
	bdev = sdio_get_drvdata(func);*/
	u32 ret = 0;
	u32 retry_count = 0;

	if (func == NULL) {
		BTMTK_ERR("%s func is NULL\n", __func__);
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
static int btmtk_sdio_enable_interrupt(int enable, struct sdio_func *func)
{
	u32 ret = 0;
	u32 cr_value = 0;

	if (enable)
		cr_value |= C_FW_INT_EN_SET;
	else
		cr_value |= C_FW_INT_EN_CLEAR;

	ret = btmtk_sdio_writel(CHLPCR, cr_value, func);

	return ret;
}

int btmtk_cif_send_cmd(struct btmtk_dev *bdev, struct sk_buff *skb,
		int delay, int retry, int endpoint)
{
	int ret = 0;
	u8 MultiBluckCount = 0;
	u8 redundant = 0;
	int len = 0;
	u32 crAddr = 0, crValue = 0;

	/* for read/write CR */
	u8 notify_alt_evt[] = {0x0E, 0x04, 0x01, 0x03, 0x0c, 0x00};
	struct sk_buff *evt_skb;

	//btmtk_sdio_set_own_back(bdev, DRIVER_OWN, 20);

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

			BTMTK_INFO("%s crAddr=0x%08x crValue=0x%08x",
				__func__, crAddr, crValue);

			btmtk_sdio_writel(crAddr, crValue, bdev->func);
			evt_skb = skb_copy(skb, GFP_KERNEL);
			bt_cb(evt_skb)->pkt_type = HCI_EVENT_PKT;
			notify_alt_evt[2] = (crValue & 0xFF000000) >> 24;
			notify_alt_evt[3] = (crValue & 0x00FF0000) >> 16;
			notify_alt_evt[4] = (crValue & 0x0000FF00) >> 8;
			notify_alt_evt[5] = (crValue & 0x000000FF);
			memcpy(evt_skb->data, &notify_alt_evt, sizeof(notify_alt_evt));
			evt_skb->len = sizeof(notify_alt_evt);
			hci_recv_frame(bdev->hdev, evt_skb);
			goto exit;
		} else	if (skb->data[0] == 0x01 && skb->data[1] == 0x6f && skb->data[2] == 0xfc &&
				skb->data[3] == 0x09 && skb->data[4] == 0x01 &&
				skb->data[5] == 0xff && skb->data[6] == 0x05 &&
				skb->data[7] == 0x00 && skb->data[8] == 0x01) {

			crAddr = ((skb->data[9] & 0xff) << 24) + ((skb->data[10] & 0xff) << 16) +
				((skb->data[11]&0xff) << 8) + (skb->data[12]&0xff);

			btmtk_sdio_readl(crAddr, &crValue, bdev->func);
			BTMTK_INFO("%s read crAddr=0x%08x crValue=0x%08x",
					__func__, crAddr, crValue);
			evt_skb = skb_copy(skb, GFP_KERNEL);
			bt_cb(evt_skb)->pkt_type = HCI_EVENT_PKT;
			//memcpy(&notify_alt_evt[2], &crValue, sizeof(crValue));
			notify_alt_evt[2] = (crValue & 0xFF000000) >> 24;
			notify_alt_evt[3] = (crValue & 0x00FF0000) >> 16;
			notify_alt_evt[4] = (crValue & 0x0000FF00) >> 8;
			notify_alt_evt[5] = (crValue & 0x000000FF);
			memcpy(evt_skb->data, &notify_alt_evt, sizeof(notify_alt_evt));
			evt_skb->len = sizeof(notify_alt_evt);
			hci_recv_frame(bdev->hdev, evt_skb);
			goto exit;
		}
	}

	if (skb->data[0] == 0x02 && skb->data[1] == 0x00 && skb->data[2] == 0x44) {
		/* it's for ble iso, remove speicific header
		 * 02 00 44 len len + payload to 05 + payload
		 */
		skb_pull(skb, 4);
		skb->data[0] = HCI_ISO_PKT;
	}

	bdev->sdio_packet[0] = (4 + skb->len) & 0xFF;
	bdev->sdio_packet[1] = ((4 + skb->len) & 0xFF00) >> 8;

	memcpy(bdev->sdio_packet + MTK_SDIO_PACKET_HEADER_SIZE, skb->data,
		skb->len);
	len = skb->len + MTK_SDIO_PACKET_HEADER_SIZE;

	MultiBluckCount = len / SDIO_BLOCK_SIZE;
	redundant = len  % SDIO_BLOCK_SIZE;

	if (redundant)
		len  = (MultiBluckCount+1)*SDIO_BLOCK_SIZE;

	BTMTK_DBG_RAW(bdev->sdio_packet, len, "%s: sent, len =%d:", __func__, len);
	ret = btmtk_sdio_writesb(CTDR, bdev->sdio_packet, len, bdev->func);

exit:
#if 1
	if(skb) {
		kfree_skb(skb);
		skb = NULL;
		BTMTK_DBG("%s done then free skb done, \n",__func__);
	}
#endif
	return ret;
}

int btmtk_cif_recv_evt(struct btmtk_dev *bdev, int delay, int retry)
{
	int ret = 0;
	u32 u32ReadCRValue = 0;
	u32 u32ReadCRLEN = 0;
	int retry_count = retry;
	u32 sdio_header_length = 0;
	int rx_length = 0;
	int payload = 0;
	u16 hci_pkt_len = 0;
	u8 hci_type = 0;

	memset(bdev->io_buf, 0, IO_BUF_SIZE);

	do {
		/* keep polling method */
		/* If interrupt method is working, we can remove it */
		ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue, bdev->func);
		BTMTK_DBG("%s: loop Get CHISR 0x%08X\n",
			__func__, u32ReadCRValue);

		ret = btmtk_sdio_readl(0x0024, &u32ReadCRLEN, bdev->func);
		rx_length = (u32ReadCRLEN & RX_PKT_LEN) >> 16;
		if (rx_length == 0xFFFF) {
			BTMTK_WARN("%s: 0xFFFF==rx_length, error return -EIO\n", __func__);
			ret = -EIO;
			break;
		}

		if ((RX_DONE&u32ReadCRValue || bdev->rx_dnld_rdy == true) && rx_length) {
			BTMTK_DBG("%s: u32ReadCRValue = %08X\n", __func__, u32ReadCRValue);
			u32ReadCRValue &= 0xFFFB;
			ret = btmtk_sdio_writel(CHISR, u32ReadCRValue, bdev->func);
			BTMTK_DBG("%s: write = %08X\n", __func__, u32ReadCRValue);

			ret = btmtk_sdio_readsb(CRDR, bdev->transfer_buf, rx_length, bdev->func);
			BTMTK_DBG_RAW(bdev->transfer_buf, rx_length, "%s: raw data is :", __func__);
			sdio_header_length = (bdev->transfer_buf[1] << 8);
			sdio_header_length |= bdev->transfer_buf[0];
			bdev->rx_dnld_rdy = false;
			BTMTK_DBG("%s sdio header length %d, rx_length %d\n", __func__, sdio_header_length,
				rx_length);

			hci_type = bdev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE];
			switch (hci_type) {
			/* Please reference hci header format
			 * A = len
			 * acl : 02 xx xx AA AA + payload
			 * sco : 03 xx xx AA + payload
			 * evt : 04 xx AA + payload
			 * ISO : 05 xx xx AA AA + payload
			 */
			case HCI_ACLDATA_PKT:
				hci_pkt_len = bdev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 3] +
								(bdev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 4] << 8) + 5;
				break;
			case HCI_SCODATA_PKT:
				hci_pkt_len = bdev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 4] + 4;
				break;
			case HCI_EVENT_PKT:
				hci_pkt_len = bdev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 2] + 3;
				break;
			case HCI_ISO_PKT:
				hci_pkt_len = bdev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 3] +
								(bdev->transfer_buf[MTK_SDIO_PACKET_HEADER_SIZE + 4] << 8) + 4;
				bdev->io_buf[0] = HCI_ACLDATA_PKT;
				bdev->io_buf[1] = 0x00;
				bdev->io_buf[2] = 0x44;
				bdev->io_buf[3] = (hci_pkt_len & 0x00ff);
				bdev->io_buf[4] = ((hci_pkt_len & 0xff00) >> 8);
				memcpy(bdev->io_buf + 5, bdev->transfer_buf + MTK_SDIO_PACKET_HEADER_SIZE + 1, hci_pkt_len);
				memset(bdev->transfer_buf, 0, URB_MAX_BUFFER_SIZE);
				hci_pkt_len += 5;
				memcpy(bdev->transfer_buf, bdev->io_buf, hci_pkt_len);
				BTMTK_DBG_RAW(bdev->transfer_buf, hci_pkt_len, "%s: raw data is :", __func__);
				break;
			}
			ret = hci_pkt_len;
			bdev->recv_evt_len = hci_pkt_len;

			BTMTK_DBG("%s sdio header length %d, rx_length %d, hci_pkt_len = %d", __func__, sdio_header_length, rx_length, hci_pkt_len);
			btmtk_recv(bdev->hdev, bdev->transfer_buf + MTK_SDIO_PACKET_HEADER_SIZE, hci_pkt_len);
			if (bdev->transfer_buf[4] == HCI_EVENT_PKT) {
				payload = rx_length - bdev->transfer_buf[6] - 3;
				ret = rx_length - MTK_SDIO_PACKET_HEADER_SIZE - payload;
			}

			if (sdio_header_length != rx_length) {
				BTMTK_ERR("%s sdio header length %d, rx_length %d mismatch\n",
					__func__, sdio_header_length,
					rx_length);
				break;
			}

			if (sdio_header_length == 0) {
				BTMTK_WARN("%s: get sdio_header_length = %d\n",
					__func__, sdio_header_length);
				continue;
			}

			if (sdio_header_length == rx_length) {
				BTMTK_DBG("%s: done, hci_pkt_len = %d", __func__, hci_pkt_len);
				return ret;
			} else
				break;
		}

		retry_count--;
		if (retry_count <= 0) {
			BTMTK_WARN("%s: retry_count = %d,timeout\n",
				__func__, retry_count);
			//btmtk_sdio_print_debug_sr();
			ret = -EIO;
			break;
		}

		/* msleep(1); */
		mdelay(delay);
		BTMTK_INFO("%s: retry_count = %d,wait\n", __func__, retry_count);

		if (ret)
			break;
	} while (1);

	if (ret)
		return -EIO;

	BTMTK_DBG("%s: done", __func__);
	return ret;
}

static void btmtk_sdio_interrupt(struct sdio_func *func)
{
	struct btmtk_dev *bdev;
	int ret = 0;
	u32 u32ReadCRValue = 0;

	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return;

	btmtk_sdio_enable_interrupt(0, func);
	bdev->int_count++;

	ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue, func);
	BTMTK_DBG("%s CHISR 0x%08x\n", __func__, u32ReadCRValue);

	if (u32ReadCRValue & FIRMWARE_INT_BIT31) {
		/* It's read-only bit (WDT interrupt)
		 * Host can't modify it.
		 */
		ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue, func);
		BTMTK_DBG("%s CHISR 0x%08x\n", __func__, u32ReadCRValue);
		schedule_work(&bdev->reset_waker);
		return;
	}

	if (TX_EMPTY&u32ReadCRValue) {
		ret = btmtk_sdio_writel(CHISR, (TX_EMPTY | TX_COMPLETE_COUNT), func);
		bdev->tx_dnld_rdy = true;
		BTMTK_DBG("%s set tx_dnld_rdy 1\n", __func__);
	}

	if (RX_DONE&u32ReadCRValue) {
		bdev->rx_dnld_rdy = true;
		BTMTK_DBG("%s set rx_dnld_rdy 1\n", __func__);
		ret = btmtk_cif_recv_evt(bdev, 20, 20);
	}

	ret = btmtk_sdio_enable_interrupt(1, func);

	BTMTK_DBG("%s done", __func__);
}

static int btmtk_sdio_register_dev(struct btmtk_dev *bdev)
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
	BTMTK_INFO("before claim irq read SDIO_CCCR_IENx %x, func num %d\n",
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
		pr_err("cannot set SDIO block size\n");
		ret = -EIO;
		goto release_irq;
	}


	return 0;

release_irq:
	sdio_release_irq(func);

disable_func:
	sdio_disable_func(func);

failed:
	pr_info("%s fail\n", __func__);
	return ret;
}

static int btmtk_sdio_enable_host_int(struct btmtk_dev *bdev)
{
	int ret;
	u32 read_data = 0;

	if (!bdev || !bdev->func)
		return -EINVAL;

	/* workaround for some platform no host clock sometimes */

	btmtk_sdio_readl(CSDIOCSR, &read_data, bdev->func);
	BTMTK_INFO("%s read CSDIOCSR is 0x%X\n", __func__, read_data);
	read_data |= 0x4;
	btmtk_sdio_writel(CSDIOCSR, read_data, bdev->func);
	BTMTK_INFO("%s write CSDIOCSR is 0x%X\n", __func__, read_data);

	return ret;
}

static int btmtk_sdio_unregister_dev(struct btmtk_dev *bdev)
{
	if (bdev && bdev->func) {
		sdio_claim_host(bdev->func);
		sdio_release_irq(bdev->func);
		sdio_disable_func(bdev->func);
		sdio_release_host(bdev->func);
		sdio_set_drvdata(bdev->func, NULL);
	}
	return 0;
}

static int btmtk_sdio_set_write_clear(struct btmtk_dev *bdev)
{
	u32 u32ReadCRValue = 0;
	u32 ret = 0;

	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, bdev->func);
	if (ret) {
		BTMTK_ERR("%s read CHCR error", __func__);
		ret = EINVAL;
		return ret;
	}

	u32ReadCRValue |= 0x00000002;
	btmtk_sdio_writel(CHCR, u32ReadCRValue, bdev->func);
	BTMTK_INFO("%s write CHCR 0x%08X\n", __func__, u32ReadCRValue);
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, bdev->func);
	BTMTK_INFO("%s read CHCR 0x%08X\n", __func__, u32ReadCRValue);
	if (u32ReadCRValue&0x00000002)
		BTMTK_INFO("%s write clear\n", __func__);
	else
		BTMTK_INFO("%s read clear\n", __func__);

	return ret;
}

int btmtk_cif_subsys_reset(struct btmtk_dev *bdev)
{
	u32 u32ReadCRValue = 0;
	u32 ret = 0;

	/* write CHCR[3] 0 to 1 */
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, bdev->func);
	BTMTK_INFO("%s read CHCR 0x%08X\n", __func__, u32ReadCRValue);

	u32ReadCRValue &= 0xFFFFFFF7;
	BTMTK_INFO("%s write CHCR 0x%08X\n", __func__, u32ReadCRValue);
	btmtk_sdio_writel(CHCR, u32ReadCRValue, bdev->func);


	/* write CHCR[3] 0 to 1 */
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, bdev->func);
	BTMTK_INFO("%s read CHCR 0x%08X\n", __func__, u32ReadCRValue);

	u32ReadCRValue |= 0x00000008;
	BTMTK_INFO("%s write CHCR 0x%08X\n", __func__, u32ReadCRValue);
	btmtk_sdio_writel(CHCR, u32ReadCRValue, bdev->func);

	/* write CHCR[5] 1 */
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue, bdev->func);
	BTMTK_INFO("%s read CHCR 0x%08X\n", __func__, u32ReadCRValue);

	u32ReadCRValue |= 0x00000020;
	btmtk_sdio_writel(CHCR, u32ReadCRValue, bdev->func);

	/* Do-init cr */
	/* Disable the interrupts on the card */
	btmtk_sdio_enable_host_int(bdev);
	BTMTK_DBG("call btmtk_sdio_enable_host_int done\n");

	btmtk_sdio_set_own_back(bdev, DRIVER_OWN, 20);

	/* Set interrupt output */
	ret = btmtk_sdio_writel(CHIER, FIRMWARE_INT_BIT31 | FIRMWARE_INT|TX_FIFO_OVERFLOW |
			FW_INT_IND_INDICATOR | TX_COMPLETE_COUNT |
			TX_UNDER_THOLD | TX_EMPTY | RX_DONE, bdev->func);
	if (ret) {
		BTMTK_ERR("Set interrupt output fail(%d)", ret);
		ret = -EIO;
		return ret;
	}

	/* Enable interrupt output */
	ret = btmtk_sdio_writel(CHLPCR, C_FW_INT_EN_SET, bdev->func);
	if (ret) {
		BTMTK_ERR("enable interrupt output fail(%d)", ret);
		ret = -EIO;
		return ret;
	}

	/* Adopt write clear method */
	btmtk_sdio_set_write_clear(bdev);

	ret = btmtk_sdio_readl(0, &u32ReadCRValue, bdev->func);
	BTMTK_INFO("%s read chipid =  %x\n", __func__, u32ReadCRValue);

	return 0;
}

#if 0
static void btsdio_reset_waker(struct work_struct *work)
{
	struct btmtk_dev *bdev = container_of(work, struct btmtk_dev, reset_waker);
	u32 u32ReadCRValue = 0;

	BT_INFO("%s: Receive a byte (0xFF)", __func__);

	bdev->interface_state = BTMTK_STATE_FW_DUMP;
	bdev->fops_state = BTMTK_FOPS_STATE_INIT;
	bdev->sco_num = 0;
	btmtk_cif_subsys_reset(bdev);

	mdelay(500);
	btmtk_cap_init(bdev);
	btmtk_load_rom_patch(bdev);
	skb_queue_head_init(&bdev->tx_queue);
	btmtk_sdio_readl(CHLPCR, &u32ReadCRValue, bdev->func);
	BTMTK_DBG("%s read CHLPCR (0x%08X)\n", __func__, u32ReadCRValue);
	BTMTK_INFO("%s normal end\n", __func__);
}
#endif

static int btmtk_sdio_probe(struct sdio_func *func,
					const struct sdio_device_id *id)
{
	int err = -1;
	struct btmtk_dev *bdev = NULL;

	bdev = sdio_get_drvdata(func);
	if (!bdev) {
		BTMTK_ERR("[ERR] bdev is NULL");
		return -ENOMEM;
	}

	bdev->func = func;
	BTMTK_DBG("%s func device %X", __func__, bdev->func->device);

	/* it's for L0/L0.5 reset */
	INIT_WORK(&bdev->reset_waker, btmtk_reset_waker);
	/* The lock is for usb waker, reserve it for sdio part */
	spin_lock_init(&bdev->rxlock);


	if (btmtk_sdio_register_dev(bdev) < 0) {
		BTMTK_ERR("Failed to register BT device!\n");
		return -ENODEV;
	}

	/* Disable the interrupts on the card */
	btmtk_sdio_enable_host_int(bdev);
	BTMTK_DBG("call btmtk_sdio_enable_host_int done\n");

	sdio_set_drvdata(func, bdev);

	btmtk_cif_allocate_memory(bdev);

	btmtk_initialize_cfg_items(bdev);

	btmtk_allocate_hci_device(bdev, HCI_SDIO);

	btmtk_sdio_set_own_back(bdev, DRIVER_OWN, 20);

	/* Set interrupt output */
	err = btmtk_sdio_writel(CHIER, FIRMWARE_INT_BIT31 | FIRMWARE_INT_BIT15 |
			FIRMWARE_INT|TX_FIFO_OVERFLOW |
			FW_INT_IND_INDICATOR | TX_COMPLETE_COUNT |
			TX_UNDER_THOLD | TX_EMPTY | RX_DONE, bdev->func);
	if (err) {
		BTMTK_ERR("Set interrupt output fail(%d)", err);
		btmtk_free_hci_device(bdev, HCI_SDIO);
		btmtk_cif_free_memory(bdev);
		return -EIO;
	}

	/* Enable interrupt output */
	err = btmtk_sdio_writel(CHLPCR, C_FW_INT_EN_SET, bdev->func);
	if (err) {
		BTMTK_ERR("enable interrupt output fail(%d)", err);
		btmtk_free_hci_device(bdev, HCI_SDIO);
		btmtk_cif_free_memory(bdev);
		return -EIO;
	}

	/* write clear method */
	btmtk_sdio_set_write_clear(bdev);

	/* old method for chip id
	 * btmtk_sdio_readl(0, &u32ReadCRValue, bdev->func);
	 * BTMTK_INFO("%s read chipid =  %x\n", __func__, u32ReadCRValue);
	 */

	btmtk_cap_init(bdev);

	btmtk_load_bt_cfg(bdev->bt_cfg_file_name, &bdev->func->dev, bdev);

	err = btmtk_load_rom_patch(bdev);
	if (err < 0) {
		btmtk_free_hci_device(bdev, HCI_SDIO);
		btmtk_initialize_cfg_items(bdev);
		btmtk_cif_free_memory(bdev);
		BTMTK_ERR("btmtk load rom patch failed!");
		return err;
	}

	btmtk_register_hci_device(bdev);

	/* Need to add Woble flow */

	BTMTK_INFO("%s normal end\n", __func__);
	return 0;
}

static void btmtk_sdio_disconnect(struct sdio_func *func)
{
	struct btmtk_dev *bdev = sdio_get_drvdata(func);

	if (!bdev)
		return;

	btmtk_free_setting_file(bdev);
	btmtk_deregister_hci_device(bdev);
	btmtk_free_hci_device(bdev, HCI_SDIO);


	bdev->power_state = BTMTK_DONGLE_STATE_POWER_OFF;
	btmtk_cif_free_memory(bdev);
	btmtk_sdio_unregister_dev(bdev);

	btmtk_release_dev(bdev);
}

static int btmtk_cif_probe(struct sdio_func *func,
					const struct sdio_device_id *id)
{
	int ret = -1;
	int cif_event = 0;
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_dev *bdev = NULL;

	/* Mediatek Driver Version */
	BTMTK_INFO("%s: MTK BT Driver Version : %s", __func__, VERSION);

	BTMTK_DBG("vendor=0x%x, device=0x%x, class=%d, fn=%d",
			id->vendor, id->device, id->class,
			func->num);

	/* sdio interface numbers  */
	if (func->num != BTMTK_SDIO_FUNC) {
		BTMTK_INFO("func num is not match, func_num = %d", func->num);
		return -ENODEV;
	}

	/* Retrieve priv data and set to interface structure */
	bdev = btmtk_get_dev();
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

	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

	/* Do HIF events */
	btmtk_sdio_disconnect(func);

	/* Set End/Error state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
}

#ifdef CONFIG_PM
static int btmtk_cif_suspend(struct device *dev)
{
	int ret = 0;
	BTMTK_INFO("%s, enter", __func__);

	return ret;
}

static int btmtk_cif_resume(struct device *dev)
{
	int ret = 0;

	BTMTK_INFO("%s, enter", __func__);
	BTMTK_INFO("%s, end. ret = %d", __func__, ret);
	return ret;
}
#endif	// CONFIG_PM //


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
		.pm = &btmtk_sdio_pm_ops,
	}
};

static int sdio_register(void)
{
	BTMTK_INFO("%s", __func__);

	if (sdio_register_driver(&btmtk_sdio_driver) != 0) {
		return -ENODEV;
	}

	return 0;
}

static int sdio_deregister(void)
{
	BTMTK_INFO("%s", __func__);
	sdio_unregister_driver(&btmtk_sdio_driver);
	return 0;
}

int btmtk_cif_register(void)
{
	int retval = 0;

	BTMTK_INFO("%s", __func__);
	retval = sdio_register();
	if (retval)
		BTMTK_ERR("*** SDIO registration fail(%d)! ***", retval);
	else
		BTMTK_INFO("%s, SDIO registration success!", __func__);
	return retval;
}

int btmtk_cif_deregister(void)
{
	BT_INFO("%s", __func__);
	sdio_deregister();
	BT_INFO("%s: Done", __func__);
	return 0;
}

