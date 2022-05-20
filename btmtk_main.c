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
#include "btmtk_define.h"
#include "btmtk_main.h"

#define MTKBT_UNSLEEPABLE_LOCK(x, y)	spin_lock_irqsave(x, y)
#define MTKBT_UNSLEEPABLE_UNLOCK(x, y)	spin_unlock_irqsave(x, y)

/* TODO, need to modify the state mutex for each hci dev*/
static DEFINE_MUTEX(btmtk_chip_state_mutex);
#define CHIP_STATE_MUTEX_LOCK()	mutex_lock(&btmtk_chip_state_mutex)
#define CHIP_STATE_MUTEX_UNLOCK()	mutex_unlock(&btmtk_chip_state_mutex)
static DEFINE_MUTEX(btmtk_fops_state_mutex);
#define FOPS_MUTEX_LOCK()	mutex_lock(&btmtk_fops_state_mutex)
#define FOPS_MUTEX_UNLOCK()	mutex_unlock(&btmtk_fops_state_mutex)

/**
 * Global parameters(mtkbt_)
 */
uint8_t btmtk_log_lvl = BTMTK_LOG_LVL_DEF;

static int btmtk_get_chip_state(struct btmtk_dev *bdev)
{
	return bdev->interface_state;
}

static void btmtk_set_chip_state(struct btmtk_dev *bdev, int new_state)
{
	static const char * const state_msg[] = {
		"INIT", "DISCONNECT", "PROBE", "WORKING", "SUSPEND", "RESUME", "FW_DUMP", "STANDBY",
	};

	BTMTK_INFO("%s: %s(%d) -> %s(%d)", __func__, state_msg[bdev->interface_state],
			bdev->interface_state, state_msg[new_state], new_state);
	bdev->interface_state = new_state;
}

static int btmtk_fops_get_state(struct btmtk_dev *bdev)
{
	return bdev->fops_state;
}

static void btmtk_fops_set_state(struct btmtk_dev *bdev, int new_state)
{
	static const char * const fstate_msg[] = {"INIT", "OPENED", "CLOSING", "CLOSED"};

	BTMTK_INFO("%s: FOPS_%s(%d) -> FOPS_%s(%d)", __func__, fstate_msg[bdev->fops_state],
			bdev->fops_state, fstate_msg[new_state], new_state);
	bdev->fops_state = new_state;
}

static int main_init(void)
{
	return 0;
}

static int main_exit(void)
{
	return 0;
}

/* HCI receive mechnism */


static inline struct sk_buff *h4_recv_buf(struct hci_dev *hdev,
					  struct sk_buff *skb,
					  const unsigned char *buffer,
					  int count,
					  const struct h4_recv_pkt *pkts,
					  int pkts_count)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	if (bdev == NULL || hdev == NULL || buffer == NULL) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		return ERR_PTR(-EINVAL);
	}
	/* Check for error from previous call */
	if (IS_ERR(skb))
		skb = NULL;

	while (count) {
		int i, len;

		if (!count)
			break;

		if (!skb) {
			for (i = 0; i < pkts_count; i++) {
				if (buffer[0] != (&pkts[i])->type)
					continue;

				skb = bt_skb_alloc((&pkts[i])->maxlen,
						   GFP_ATOMIC);
				if (!skb)
					return ERR_PTR(-ENOMEM);

				hci_skb_pkt_type(skb) = (&pkts[i])->type;
				hci_skb_expect(skb) = (&pkts[i])->hlen;
				break;
			}

			/* Check for invalid packet type */
			if (!skb)
				return ERR_PTR(-EILSEQ);

			count -= 1;
			buffer += 1;
		}

		len = min_t(uint, hci_skb_expect(skb) - skb->len, count);
		memcpy(skb_put(skb, len), buffer, len);
		/* If kernel version > 4.x */
		/* skb_put_data(skb, buffer, len); */

		count -= len;
		buffer += len;

		/* Check for partial packet */
		if (skb->len < hci_skb_expect(skb))
			continue;

		for (i = 0; i < pkts_count; i++) {
			if (hci_skb_pkt_type(skb) == (&pkts[i])->type)
				break;
		}

		if (i >= pkts_count) {
			kfree_skb(skb);
			return ERR_PTR(-EILSEQ);
		}

		if (skb->len == (&pkts[i])->hlen) {
			u16 dlen;

			switch ((&pkts[i])->lsize) {
			case 0:
				/* No variable data length */
				dlen = 0;
				break;
			case 1:
				/* Single octet variable length */
				dlen = skb->data[(&pkts[i])->loff];
				hci_skb_expect(skb) += dlen;

				if (skb_tailroom(skb) < dlen) {
					kfree_skb(skb);
					return ERR_PTR(-EMSGSIZE);
				}
				break;
			case 2:
				/* Double octet variable length */
				dlen = get_unaligned_le16(skb->data +
							  (&pkts[i])->loff);
				hci_skb_expect(skb) += dlen;

				if (skb_tailroom(skb) < dlen) {
					kfree_skb(skb);
					return ERR_PTR(-EMSGSIZE);
				}
				break;
			default:
				/* Unsupported variable length */
				kfree_skb(skb);
				return ERR_PTR(-EILSEQ);
			}

			if (!dlen) {
				/* No more data, complete frame */
				(&pkts[i])->recv(hdev, skb);
				skb = NULL;
			}
		} else {
			/* Complete frame */
			(&pkts[i])->recv(hdev, skb);
			skb = NULL;
		}
	}

	return skb;
}

static const struct h4_recv_pkt mtk_recv_pkts[] = {
	{ H4_RECV_ACL,      .recv = btmtk_recv_acl },
	{ H4_RECV_SCO,      .recv = hci_recv_frame },
	{ H4_RECV_EVENT,    .recv = btmtk_recv_event },
};
#if ENABLESTP
static inline struct sk_buff *mtk_add_stp(struct btmtk_dev *bdev, struct sk_buff *skb)
{
	struct mtk_stp_hdr *shdr;
	int dlen, err = 0, type = 0;
	u8 stp_crc[] = {0x00, 0x00};

	if (unlikely(skb_headroom(skb) < sizeof(*shdr)) ||
		(skb_tailroom(skb) < MTK_STP_TLR_SIZE)) {
		BTMTK_DBG("%s, add pskb_expand_head, headroom = %d, tailroom = %d",
				__func__, skb_headroom(skb), skb_tailroom(skb));

		err = pskb_expand_head(skb, sizeof(*shdr), MTK_STP_TLR_SIZE,
					   GFP_ATOMIC);
	}
	dlen = skb->len;
	shdr = (void *) skb_push(skb, sizeof(*shdr));
	shdr->prefix = 0x80;
	shdr->dlen = cpu_to_be16((dlen & 0x0fff) | (type << 12));
	shdr->cs = 0;
	// Add the STP trailer
	// kernel version > 4.20
	// skb_put_zero(skb, MTK_STP_TLR_SIZE);
	// kernel version < 4.20
	skb_put(skb, sizeof(stp_crc));

	return skb;
}

static const unsigned char *
mtk_stp_split(struct btmtk_dev *bdev, const unsigned char *data, int count,
	      int *sz_h4)
{
	struct mtk_stp_hdr *shdr;

	/* The cursor is reset when all the data of STP is consumed out */
	if (!bdev->stp_dlen && bdev->stp_cursor >= 6) {
		bdev->stp_cursor = 0;
		BTMTK_ERR("reset cursor = %d\n", bdev->stp_cursor);
	}

	/* Filling pad until all STP info is obtained */
	while (bdev->stp_cursor < 6 && count > 0) {
		bdev->stp_pad[bdev->stp_cursor] = *data;
		pr_err("fill stp format (%02x, %d, %d)\n",
		   bdev->stp_pad[bdev->stp_cursor], bdev->stp_cursor, count);
		bdev->stp_cursor++;
		data++;
		count--;
	}

	/* Retrieve STP info and have a sanity check */
	if (!bdev->stp_dlen && bdev->stp_cursor >= 6) {
		shdr = (struct mtk_stp_hdr *)&bdev->stp_pad[2];
		bdev->stp_dlen = be16_to_cpu(shdr->dlen) & 0x0fff;
		pr_err("stp format (%02x, %02x)",
			   shdr->prefix, bdev->stp_dlen);

		/* Resync STP when unexpected data is being read */
		if (shdr->prefix != 0x80 || bdev->stp_dlen > 2048) {
			BTMTK_ERR("stp format unexpect (%02x, %02x)",
				   shdr->prefix, bdev->stp_dlen);
			BTMTK_ERR("reset cursor = %d\n", bdev->stp_cursor);
			bdev->stp_cursor = 2;
			bdev->stp_dlen = 0;
		}
	}

	/* Directly quit when there's no data found for H4 can process */
	if (count <= 0)
		return NULL;

	/* Tranlate to how much the size of data H4 can handle so far */
	*sz_h4 = min_t(int, count, bdev->stp_dlen);

	/* Update the remaining size of STP packet */
	bdev->stp_dlen -= *sz_h4;

	/* Data points to STP payload which can be handled by H4 */
	return data;
}
#endif

int btmtk_recv(struct hci_dev *hdev, const u8 *data, size_t count)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	const unsigned char *p_left = data;
	int sz_left = count;
	int err;
#if ENABLESTP
	const unsigned char **p_h4 = NULL;
	int sz_h4 = 0, adv = 0;
#endif

	if (bdev == NULL || hdev == NULL || data == NULL) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		return -EINVAL;
	}

	while (sz_left > 0) {
		/*  The serial data received from MT7622 BT controller is
		 *  at all time padded around with the STP header and tailer.
		 *
		 *  A full STP packet is looking like
		 *   -----------------------------------
		 *  | STP header  |  H:4   | STP tailer |
		 *   -----------------------------------
		 *  but it doesn't guarantee to contain a full H:4 packet which
		 *  means that it's possible for multiple STP packets forms a
		 *  full H:4 packet that means extra STP header + length doesn't
		 *  indicate a full H:4 frame, things can fragment. Whose length
		 *  recorded in STP header just shows up the most length the
		 *  H:4 engine can handle currently.
		 */
#if ENABLESTP
		p_h4 = mtk_stp_split(bdev, p_left, sz_left, &sz_h4);
		if (!p_h4)
			break;

		adv = p_h4 - p_left;
		sz_left -= adv;
		p_left += adv;
#endif

#if ENABLESTP
		bdev->rx_skb = h4_recv_buf(hdev, bdev->rx_skb, p_h4,
					   sz_h4, mtk_recv_pkts,
					   ARRAY_SIZE(mtk_recv_pkts));
#else
		bdev->rx_skb = h4_recv_buf(hdev, bdev->rx_skb, data,
					   count, mtk_recv_pkts,
					   ARRAY_SIZE(mtk_recv_pkts));
#endif

		if (IS_ERR(bdev->rx_skb)) {
			err = PTR_ERR(bdev->rx_skb);
			pr_err("Frame reassembly failed (%d)", err);
			bdev->rx_skb = NULL;
			return err;
		}

#if ENABLESTP
		sz_left -= sz_h4;
		p_left += sz_h4;
#else
		sz_left -= count;
		p_left += count;
#endif
	}

	return 0;
}

int btmtk_dispatch_acl(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	if (skb->data[0] == 0x6f && skb->data[1] == 0xfc && skb->len > 12) {
		/* sent coredump data to queue, picus tool will log it */
		/* coredump data done
		 * For Example : TotalTimeForDump=0xxxxxxx, (xx secs)
		 */
		if (skb->data[4] == 0x54 && skb->data[5] == 0x6F &&
			skb->data[6] == 0x74 && skb->data[7] == 0x61 &&
			skb->data[8] == 0x6C && skb->data[9] == 0x54 &&
			skb->data[10] == 0x69 && skb->data[11] == 0x6D &&
			skb->data[12] == 0x65) {
			/* coredump end, do reset */
			BTMTK_INFO("%s coredump done", __func__);
			msleep(3000);
			/* TODO: Chip reset*/
			bdev->subsys_reset = HW_ERR_CODE_CORE_DUMP;
		}
		return 1;
	} else if (skb->data[0] == 0xff && skb->data[1] == 0x05) {
		BTMTK_DBG("%s correct picus log by ACL", __func__);
		/*TODO: sent picus data to queue, picus tool will log it */
		return 1;
	}
	return 0;
}

int btmtk_dispatch_event(struct hci_dev *hdev, struct sk_buff *skb)
{

	/* For Picus */
	if (skb->data[0] == 0xff && skb->data[2] == 0x50)
		BTMTK_DBG("%s correct picus log format by EVT", __func__);

	return 0;
}

int btmtk_recv_acl(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	if (bdev == NULL || bdev->workqueue == NULL || hdev == NULL || skb == NULL) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		return -EINVAL;
	}

	skb_queue_tail(&bdev->rx_q, skb);
	queue_work(bdev->workqueue, &bdev->rx_work);

	/* remove it, if workqueue can't be scheduled, you can reuse it */
#if 0
	skip_pkt = btmtk_dispatch_acl(hdev, skb);
	if (skip_pkt == 0)
		err = hci_recv_frame(hdev, skb);
#endif
	return 0;
}


int btmtk_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct hci_event_hdr *hdr = (void *)skb->data;
	int err = 0;

	if (bdev == NULL || bdev->workqueue == NULL || hdev == NULL || skb == NULL) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		err = -EINVAL;
		goto err_out;
	}
	/* Fix up the vendor event id with 0xff for vendor specific instead
	 * of 0xe4 so that event send via monitoring socket can be parsed
	 * properly.
	 */
	if (hdr->evt == 0xe4) {
		BTMTK_DBG("%s hdr->evt is %02x", __func__, hdr->evt);
		hdr->evt = HCI_EV_VENDOR;
	}

	/* When someone waits for the WMT event, the skb is being cloned
	 * and being processed the events from there then.
	 */
	if (test_bit(BTMTK_TX_WAIT_VND_EVT, &bdev->tx_state)) {
		bdev->evt_skb = skb_clone(skb, GFP_KERNEL);

		if (!bdev->evt_skb) {
			err = -ENOMEM;
			BTMTK_ERR("%s WMT event, clone to evt_skb failed, err = %d", __func__, err);
			goto err_out;
		}

		if (test_and_clear_bit(BTMTK_TX_WAIT_VND_EVT, &bdev->tx_state)) {
			BTMTK_INFO("%s clear bit BTMTK_TX_WAIT_VND_EVT", __func__);
			wake_up(&bdev->p_wait_event_q);
			BTMTK_INFO("%s wake_up p_wait_event_q", __func__);
		}
		goto err_out;
	}
	BTMTK_DBG_RAW(skb->data, skb->len, "%s, recv evt(hci_recv_frame)", __func__);

	skb_queue_tail(&bdev->rx_q, skb);
	queue_work(bdev->workqueue, &bdev->rx_work);

	/* remove it, if workqueue can't be scheduled, you can reuse it */
#if 0
	skip_pkt = btmtk_dispatch_event(hdev, skb);
	if (skip_pkt == 0)
		err = hci_recv_frame(hdev, skb);

	if (err < 0) {
		BTMTK_ERR("%s hci_recv_failed, err = %d", __func__, err);
		goto err_out;
	}
#endif
	return 0;

err_out:
	kfree_skb(skb);
	return err;
}

int btmtk_compare_evt(struct btmtk_dev *bdev, const uint8_t *event,
		int event_len, int recv_evt_len)
{
	int ret = -1;

	if (bdev && bdev->io_buf && event && recv_evt_len >= event_len - 1) {
		if (memcmp(bdev->io_buf, event + 1, event_len - 1) == 0) {
			ret = recv_evt_len;
			goto exit;
		} else {
			BTMTK_INFO("%s compare fail\n", __func__);
			BTMTK_INFO_RAW(event + 1, event_len - 1, "%s: event_need_compare:", __func__);
			BTMTK_INFO_RAW(bdev->io_buf, recv_evt_len, "%s: RCV:", __func__);
			goto exit;
		}
	} else
		BTMTK_ERR("%s invalid parameter!\n", __func__);

exit:
	BTMTK_DBG("%s : ret length = %d\n", __func__, ret);
	return ret;
}

int btmtk_main_send_cmd(struct btmtk_dev *bdev, const uint8_t *cmd,
		const int cmd_len, const uint8_t *event, const int event_len, int delay,
		int retry, int endpoint, const int tx_state, bool wmt_cmd)
{
	struct sk_buff *skb = NULL;
	int ret = 0;
	int recv_evt_len = 0;

	if (bdev == NULL || bdev->hdev == NULL ||
		cmd == NULL || cmd_len <= 0) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		ret = -EINVAL;
		goto exit;
	}

	skb = alloc_skb(cmd_len + BT_SKB_RESERVE, GFP_ATOMIC);
	if (skb == NULL) {
		BTMTK_ERR("%s allocate skb failed!!", __func__);
		goto err_free_skb;
	}
	/* Reserv for core and drivers use */
	skb_reserve(skb, 7);
	bt_cb(skb)->pkt_type = HCI_COMMAND_PKT;
	memcpy(skb->data, cmd, cmd_len);
	skb->len = cmd_len;

#if ENABLESTP
	skb = mtk_add_stp(bdev, skb);
#endif

	if (skb->len < 30)
		BTMTK_DBG_RAW(skb->data, skb->len, "%s, send, len = %d", __func__, skb->len);

	set_bit(tx_state, &bdev->tx_state);

	ret = btmtk_cif_send_cmd(bdev, skb, delay, retry, endpoint, wmt_cmd);
	if (ret < 0) {
		BTMTK_ERR("%s btmtk_cif_send_cmd failed!!", __func__);
		goto err_free_skb;
	}

	if ((bdev->hdev->bus != HCI_USB || !wmt_cmd) && (endpoint == BTMTK_EP_TYPE_OUT_CMD)) {
		if (!wait_event_timeout(bdev->p_wait_event_q,
				bdev->evt_skb != NULL || tx_state == BTMTK_TX_SKIP_VENDOR_EVT,
				msecs_to_jiffies(2000))) {
			BTMTK_ERR("%s wait_event_timeout, ret = %d, bdev->evt_skb = %p tx_state = %d!!",
				__func__, ret, bdev->evt_skb, tx_state);
			ret = -1;
		}

		if (bdev->evt_skb != NULL) {
			memcpy(bdev->io_buf, bdev->evt_skb->data, bdev->evt_skb->len);
			recv_evt_len = bdev->evt_skb->len;
		}
		BTMTK_DBG("%s recv_evt_len = %d!!", __func__, recv_evt_len);
		goto cmp_evt;
	}
	if (event) {
		recv_evt_len = btmtk_cif_recv_evt(bdev, delay, retry);
		if (recv_evt_len < 0) {
			BTMTK_ERR("%s btmtk_cif_recv_evt failed!!", __func__);
			ret = -1;
			goto err_free_skb;
		}
	}

cmp_evt:
	ret = btmtk_compare_evt(bdev, event, event_len, recv_evt_len);

err_free_skb:
	test_and_clear_bit(BTMTK_TX_WAIT_VND_EVT, &bdev->tx_state);
	kfree_skb(skb);
	kfree_skb(bdev->evt_skb);
	bdev->evt_skb = NULL;

exit:
	return ret;
}

static int btmtk_check_need_load_rom_patch(struct btmtk_dev *bdev)
{
	u8 cmd[] = { 0x01, 0x6F, 0xFC, 0x05, 0x01, 0x17, 0x01, 0x00, 0x01 };
	u8 event[] = { 0x04, 0xE4, 0x05, 0x02, 0x17, 0x01, 0x00, /* 0x02 */ };	/* event[6] is key */
	int ret = -1;

	if (!bdev) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		ret = -EINVAL;
		return ret;
	}

	ret = btmtk_main_send_cmd(bdev, cmd, sizeof(cmd), event, sizeof(event), 20,
			0, BTMTK_EP_TYPE_OUT_CMD, BTMTK_TX_WAIT_VND_EVT, true);
	/* can't get correct event */
	if (ret < 0)
		return PATCH_ERR;

	if (ret == sizeof(event))
		return bdev->io_buf[6];

	return PATCH_ERR;
}

static void btmtk_load_code_from_bin(const struct firmware **fw_firmware,
					char *bin_name, struct device *dev, u8 **image, u32 *code_len)
{
	int err = 0;
	int retry = 10;

	if (!bin_name) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		return;
	}

	do {
		err = request_firmware(fw_firmware, bin_name, dev);
		if (err == 0) {
			break;
		} else if (retry <= 0) {
			*fw_firmware = NULL;
			pr_err("%s: request_firmware %d times fail!!! err = %d", __func__, 10, err);
			return;
		}
		pr_err("%s: request_firmware fail!!! err = %d, retry = %d", __func__, err, retry);
		msleep(100);
	} while (retry-- > 0);

	*image = (u8 *)(*fw_firmware)->data;
	*code_len = (*fw_firmware)->size;
}

static void btmtk_print_bt_patch_info(struct btmtk_dev *bdev, u8 *fwbuf)
{
	struct _PATCH_HEADER *patchHdr = NULL;
	struct _Global_Descr *globalDesrc = NULL;

	if (fwbuf == NULL) {
		BTMTK_WARN("%s, fwbuf is NULL!", __func__);
		return;
	}

	patchHdr = (struct _PATCH_HEADER *)fwbuf;

	if (is_mt7961(bdev->chip_id))
		globalDesrc = (struct _Global_Descr *)(fwbuf + FW_ROM_PATCH_HEADER_SIZE);

	BTMTK_INFO("[btmtk] =============== Patch Info ==============");
	if (patchHdr) {
		BTMTK_INFO("[btmtk] Built Time = %s", patchHdr->ucDateTime);
		BTMTK_INFO("[btmtk] Hw Ver = 0x%04x", patchHdr->u2HwVer);
		BTMTK_INFO("[btmtk] Sw Ver = 0x%04x", patchHdr->u2SwVer);
		BTMTK_INFO("[btmtk] Magic Number = 0x%08x", patchHdr->u4MagicNum);

		BTMTK_INFO("[btmtk] Platform = %c%c%c%c",
				patchHdr->ucPlatform[0],
				patchHdr->ucPlatform[1],
				patchHdr->ucPlatform[2],
				patchHdr->ucPlatform[3]);
	} else
		BTMTK_WARN("%s, patchHdr is NULL!", __func__);

	if (globalDesrc) {
		BTMTK_INFO("[btmtk] Patch Ver = 0x%08x", globalDesrc->u4PatchVer);
		BTMTK_INFO("[btmtk] Section num = 0x%08x", globalDesrc->u4SectionNum);
	} else
		BTMTK_WARN("%s, globalDesrc is NULL!", __func__);
	BTMTK_INFO("[btmtk] =========================================");
}

static void btmtk_print_wifi_patch_info(struct btmtk_dev *bdev, u8 *fwbuf)
{
	struct _PATCH_HEADER *patchHdr = NULL;
	struct _Global_Descr *globalDesrc = NULL;

	if (fwbuf == NULL) {
		BTMTK_WARN("%s, fwbuf is NULL!", __func__);
		return;
	}

	patchHdr = (struct _PATCH_HEADER *)fwbuf;

	if (is_mt7961(bdev->chip_id))
		globalDesrc = (struct _Global_Descr *)(fwbuf + FW_ROM_PATCH_HEADER_SIZE);

	BTMTK_INFO("[btmtk] =============== Wifi Patch Info ==============");
	if (patchHdr) {
		BTMTK_INFO("[btmtk] Built Time = %s", patchHdr->ucDateTime);
		BTMTK_INFO("[btmtk] Hw Ver = 0x%04x",
			((patchHdr->u2HwVer & 0x00ff) << 8) | ((patchHdr->u2HwVer & 0xff00) >> 8));
		BTMTK_INFO("[btmtk] Sw Ver = 0x%04x",
			((patchHdr->u2SwVer & 0x00ff) << 8) | ((patchHdr->u2SwVer & 0xff00) >> 8));
		BTMTK_INFO("[btmtk] Magic Number = 0x%08x", be2cpu32(patchHdr->u4MagicNum));

		BTMTK_INFO("[btmtk] Platform = %c%c%c%c",
				patchHdr->ucPlatform[0],
				patchHdr->ucPlatform[1],
				patchHdr->ucPlatform[2],
				patchHdr->ucPlatform[3]);
	} else
		BTMTK_WARN("%s, patchHdr is NULL!", __func__);

	if (globalDesrc) {
		BTMTK_INFO("[btmtk] Patch Ver = 0x%08x",
			be2cpu32(globalDesrc->u4PatchVer));
		BTMTK_INFO("[btmtk] Section num = 0x%08x",
			be2cpu32(globalDesrc->u4SectionNum));
	} else
		BTMTK_WARN("%s, globalDesrc is NULL!", __func__);
	BTMTK_INFO("[btmtk] =========================================");
}

static int btmtk_send_wmt_download_cmd(struct btmtk_dev *bdev, u8 *cmd,
		int cmd_len, u8 *event, int event_len, struct _Section_Map *sectionMap,
		u8 fw_state, u8 dma_flag, bool patch_flag)
{
	int payload_len = 0;
	int ret = -1;
	int i = 0;
	u32 revert_SecSpec = 0;

	if (bdev == NULL || cmd == NULL || event == NULL || sectionMap == NULL) {
		BTMTK_ERR("%s: invalid parameter!", __func__);
		return ret;
	}

	/* need refine this cmd to mtk_wmt_hdr struct*/
	/* prepare HCI header */
	cmd[0] = 0x01;
	cmd[1] = 0x6F;
	cmd[2] = 0xFC;

	/* prepare WMT header */
	cmd[4] = 0x01;
	cmd[5] = 0x01; /* opcode */

	if (fw_state == 0) {
		/* prepare WMT DL cmd */
		payload_len = SEC_MAP_NEED_SEND_SIZE + 2;

		cmd[3] = (payload_len + 4) & 0xFF; /* length*/
		cmd[6] = payload_len & 0xFF;
		cmd[7] = (payload_len >> 8) & 0xFF;
		cmd[8] = 0x00; /* which is the FW download state 0 */
		cmd[9] = dma_flag; /* 1:using DMA to download, 0:using legacy wmt cmd*/
		cmd_len = SEC_MAP_NEED_SEND_SIZE + PATCH_HEADER_SIZE;

		if (patch_flag) {
			for (i = 0; i < SECTION_SPEC_NUM; i++) {
				revert_SecSpec = be2cpu32(sectionMap->u4SecSpec[i]);
				memcpy(&cmd[10] + i * sizeof(u32), (u8 *)&revert_SecSpec, sizeof(u32));
			}
		} else
			memcpy(&cmd[10], (u8 *)sectionMap + FW_ROM_PATCH_SEC_MAP_SIZE - SEC_MAP_NEED_SEND_SIZE,
					SEC_MAP_NEED_SEND_SIZE);
		BTMTK_INFO_RAW(cmd, cmd_len, "%s: CMD:", __func__);

		ret = btmtk_main_send_cmd(bdev, cmd, cmd_len,
				event, sizeof(event) - 1, 20, 0, BTMTK_EP_TYPE_OUT_CMD,
				BTMTK_TX_WAIT_VND_EVT, true);
		if (ret < 0) {
			BTMTK_ERR("%s: send wmd dl cmd failed, terminate!", __func__);
			return PATCH_ERR;
		}

		if (ret == event_len)
			return bdev->io_buf[6];

		return PATCH_ERR;
	} else if (fw_state == 3 && dma_flag == PATCH_DOWNLOAD_USING_DMA) {
		cmd_len = 9;
		cmd[3] = (cmd_len - 4) & 0xFF; /* length*/
		cmd[6] = 0x01; /* payload length */
		cmd[7] = 0x00; /* palyload length */
		cmd[8] = 0x03; /* which is the FW download state 3: finished */
		ret = btmtk_main_send_cmd(bdev, cmd, cmd_len,
				event, sizeof(event), 0, 0, BTMTK_EP_TYPE_OUT_CMD,
				BTMTK_TX_WAIT_VND_EVT, true);
		if (ret < 0)
			BTMTK_ERR("%s: send wmd dl cmd failed, terminate!", __func__);
	} else
		BTMTK_ERR("%s: fw state is error!", __func__);

	return ret;
}

static int btmtk_load_fw_patch_using_dma(struct btmtk_dev *bdev, u8 *image,
		u8 *fwbuf, int section_dl_size, int section_offset)
{
	int cur_len = 0;
	int ret = 0;
	s32 sent_len;

	if (bdev == NULL || image == NULL || fwbuf == NULL) {
		BTMTK_ERR("%s: invalid parameters!", __func__);
		ret = -1;
		goto exit;
	}
	/* send fw raw data, need to confirm with fw about format*/
	while (1) {
		sent_len = (section_dl_size - cur_len) >= UPLOAD_PATCH_UNIT ?
				UPLOAD_PATCH_UNIT : (section_dl_size - cur_len);

		if (sent_len > 0) {
			memcpy(image, fwbuf + section_offset + cur_len, sent_len);
			ret = btmtk_main_send_cmd(bdev, image, sent_len, NULL, -1, 0, 0,
					BTMTK_EP_TPYE_OUT_ACL, BTMTK_TX_WAIT_VND_EVT, false);
			if (ret < 0) {
				BTMTK_ERR("%s: send patch failed, terminate", __func__);
				goto exit;
			}
			cur_len += sent_len;
		} else
			break;
	}

exit:
	return ret;
}

static int btmtk_load_fw_patch_using_wmt_cmd(struct btmtk_dev *bdev,
		u8 *image, u8 *fwbuf, u8 *event, int event_len, u32 patch_len, int offset)
{
	int ret = 0;
	u32 cur_len = 0;
	s32 sent_len;
	int first_block = 1;
	u8 phase;
	int delay = PATCH_DOWNLOAD_PHASE1_2_DELAY_TIME;
	int retry = PATCH_DOWNLOAD_PHASE1_2_RETRY;

	if (bdev == NULL || image == NULL || fwbuf == NULL) {
		BTMTK_WARN("%s, invalid parameters!", __func__);
		ret = -1;
		goto exit;
	}

	/* loading rom patch */
	while (1) {
		s32 sent_len_max = UPLOAD_PATCH_UNIT - PATCH_HEADER_SIZE;

		sent_len = (patch_len - cur_len) >= sent_len_max ? sent_len_max : (patch_len - cur_len);

		if (sent_len > 0) {
			if (first_block == 1) {
				if (sent_len < sent_len_max)
					phase = PATCH_PHASE3;
				else
					phase = PATCH_PHASE1;
				first_block = 0;
			} else if (sent_len == sent_len_max) {
				if (patch_len - cur_len == sent_len_max)
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
			image[3] = (sent_len + 5) & 0xFF;
			image[4] = ((sent_len + 5) >> 8) & 0xFF;

			/* prepare WMT header */
			image[5] = 0x01;
			image[6] = 0x01;
			image[7] = (sent_len + 1) & 0xFF;
			image[8] = ((sent_len + 1) >> 8) & 0xFF;

			image[9] = phase;
			memcpy(&image[10], fwbuf + offset + cur_len, sent_len);
			if (phase == PATCH_PHASE3) {
				delay = PATCH_DOWNLOAD_PHASE3_DELAY_TIME;
				retry = PATCH_DOWNLOAD_PHASE3_RETRY;
			}

			ret = btmtk_main_send_cmd(bdev, image, sent_len + PATCH_HEADER_SIZE,
					event, event_len, delay, retry, BTMTK_EP_TPYE_OUT_ACL,
					BTMTK_TX_WAIT_VND_EVT, false);
			if (ret < 0) {
				BTMTK_INFO("%s: send patch failed, terminate", __func__);
				goto exit;
			}

			cur_len += sent_len;
			BTMTK_INFO("%s: sent_len = %d, cur_len = %d, phase = %d", __func__,
					sent_len, cur_len, phase);
		} else
			break;
	}

exit:
	return ret;
}

static void btmtk_send_hw_err_to_host(struct btmtk_dev *bdev)
{
	struct sk_buff *skb = NULL;

	if (bdev && bdev->subsys_reset) {
		skb = alloc_skb(BT_SKB_RESERVE, GFP_ATOMIC);
		if (skb == NULL) {
			BTMTK_ERR("%s allocate skb failed!!", __func__);
		} else {
			BTMTK_INFO("%s: send hw_err!!", __func__);
			hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
			skb->data[0] = 0x10;
			skb->data[1] = 0x01;
			skb->data[2] = bdev->subsys_reset;
			hci_recv_frame(bdev->hdev, skb);
		}
		bdev->subsys_reset = 0;
	}
}

static int btmtk_send_fw_rom_patch_79xx(struct btmtk_dev *bdev,
		u8 *fwbuf, bool patch_flag)
{
	u8 *pos;
	int loop_count = 0;
	int ret = 0;
	u32 section_num = 0;
	u32 section_offset = 0;
	u32 dl_size = 0;
	int patch_status = 0;
	int retry = 20;
	u8 dma_flag = PATCH_DOWNLOAD_USING_WMT;
	struct _Section_Map *sectionMap;
	struct _Global_Descr *globalDescr;
	u8 event[] = {0x04, 0xE4, 0x05, 0x02, 0x01, 0x01, 0x00, 0x00}; /* event[7] is status*/

	if (fwbuf == NULL) {
		BTMTK_WARN("%s, fwbuf is NULL!", __func__);
		ret = -1;
		goto exit;
	}

	globalDescr = (struct _Global_Descr *)(fwbuf + FW_ROM_PATCH_HEADER_SIZE);

	BTMTK_INFO("%s: loading rom patch...\n", __func__);

	if (patch_flag)
		section_num = be2cpu32(globalDescr->u4SectionNum);
	else
		section_num = globalDescr->u4SectionNum;
	BTMTK_INFO("%s: section_num = 0x%08x\n", __func__, section_num);

	pos = kmalloc(UPLOAD_PATCH_UNIT, GFP_ATOMIC);
	if (!pos) {
		BTMTK_ERR("%s: alloc memory failed", __func__);
		goto exit;
	}

	do {
		sectionMap = (struct _Section_Map *)(fwbuf + FW_ROM_PATCH_HEADER_SIZE +
				FW_ROM_PATCH_GD_SIZE + FW_ROM_PATCH_SEC_MAP_SIZE * loop_count);

		if (patch_flag) {
			section_offset = be2cpu32(sectionMap->u4SecOffset);
			dl_size = be2cpu32(sectionMap->bin_info_spec.u4DLSize);
		} else {
			section_offset = sectionMap->u4SecOffset;
			dl_size = sectionMap->bin_info_spec.u4DLSize;
		}
		BTMTK_INFO("%s: loop_count = %d, section_offset = 0x%08x, download patch_len = 0x%08x\n",
				__func__, loop_count, section_offset, dl_size);

		if (dl_size > 0) {
			retry = 20;
			do {
				patch_status = btmtk_send_wmt_download_cmd(bdev, pos, 0,
						event, sizeof(event) - 1, sectionMap, 0, dma_flag, patch_flag);
				BTMTK_INFO("%s: patch_status %d", __func__, patch_status);

				if (patch_status > PATCH_READY || patch_status == PATCH_ERR) {
					BTMTK_ERR("%s: patch_status error", __func__);
					ret = -1;
					goto err;
				} else if (patch_status == PATCH_READY) {
					BTMTK_INFO("%s: no need to load rom patch section%d", __func__, loop_count);
					goto next_section;
				} else if (patch_status == PATCH_IS_DOWNLOAD_BY_OTHER) {
					msleep(100);
					retry--;
				} else if (patch_status == PATCH_NEED_DOWNLOAD) {
					break;  /* Download ROM patch directly */
				}
			} while (retry > 0);

			if (patch_status == PATCH_IS_DOWNLOAD_BY_OTHER) {
				BTMTK_WARN("%s: Hold by another fun more than 2 seconds", __func__);
				ret = -1;
				goto err;
			}

			if (dma_flag == PATCH_DOWNLOAD_USING_DMA) {
				/* using DMA to download fw patch*/
				ret = btmtk_load_fw_patch_using_dma(bdev, pos, fwbuf, dl_size,
						section_offset);
				if (ret < 0) {
					BTMTK_ERR("%s: btmtk_load_fw_patch_using_dma failed!", __func__);
					goto err;
				}
			} else {
				/* using legacy wmt cmd to download fw patch */
				ret = btmtk_load_fw_patch_using_wmt_cmd(bdev, pos, fwbuf, event,
						sizeof(event) - 1, dl_size, section_offset);
				if (ret < 0) {
					BTMTK_ERR("%s: btmtk_load_fw_patch_using_wmt_cmd failed!", __func__);
					goto err;
				}
			}
		}

		/*FW Download finished */
		if (loop_count == section_num - 1) {
			if (patch_flag) {
				patch_status = btmtk_send_wmt_download_cmd(bdev, pos, 0, event,
							sizeof(event) - 1, sectionMap, 0, dma_flag, patch_flag);
				if (patch_status == PATCH_READY)
					BTMTK_INFO("%s: Wifi patch already download %d", __func__, patch_status);
				else
					BTMTK_ERR("%s: Wifi patch download failed!", __func__);
			} else {
				if (dma_flag == PATCH_DOWNLOAD_USING_DMA) {
					ret = btmtk_send_wmt_download_cmd(bdev, pos, 0, event,
						sizeof(event) - 1, sectionMap, 3, dma_flag, patch_flag);
					if (ret < 0) {
						BTMTK_ERR("%s: send wmd dl cmd state 3 failed, terminate!", __func__);
						goto err;
					}
				}
				BTMTK_INFO("%s: loading bt rom patch... Done", __func__);
				btmtk_send_hw_err_to_host(bdev);
			}
		}
next_section:
		continue;
	} while (++loop_count < section_num);

err:
	kfree(pos);
	pos = NULL;

exit:
	return ret;
}

int btmtk_load_rom_patch_79xx(struct btmtk_dev *bdev, bool patch_flag)
{
	int ret = 0;
	const struct firmware *fw_firmware = NULL;
	u8 *rom_patch = NULL;
	unsigned int rom_patch_len = 0;

	if (!bdev) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		return -EINVAL;
	}

	if (patch_flag) {
		if (bdev->flavor) {
			/* if flavor equals 1, it represent 7920, else it represent 7921*/
			snprintf(bdev->rom_patch_bin_file_name, MAX_BIN_FILE_NAME_LEN,
					"WIFI_MT%04x_patch_mcu_%xa_%x_hdr.bin",
					bdev->chip_id & 0xffff, bdev->flavor, (bdev->fw_version & 0xff) + 1);
		} else
			snprintf(bdev->rom_patch_bin_file_name, MAX_BIN_FILE_NAME_LEN,
					"WIFI_MT%04x_patch_mcu_%x_%x_hdr.bin",
					bdev->chip_id & 0xffff, bdev->flavor, (bdev->fw_version & 0xff) + 1);
	}

	btmtk_load_code_from_bin(&fw_firmware, bdev->rom_patch_bin_file_name, NULL,
			&rom_patch, &rom_patch_len);
	BTMTK_INFO("%s: rom_patch_len %d", __func__, rom_patch_len);

	if (!rom_patch) {
		BTMTK_ERR("%s: please assign a rom patch(/etc/firmware/%s)or(/lib/firmware/%s)",
			__func__, bdev->rom_patch_bin_file_name, bdev->rom_patch_bin_file_name);
		ret = -1;
		goto err;
	}

	if (patch_flag)
		/*Display rom patch info*/
		btmtk_print_wifi_patch_info(bdev, rom_patch);
	else
		btmtk_print_bt_patch_info(bdev, rom_patch);

	ret = btmtk_send_fw_rom_patch_79xx(bdev, rom_patch, patch_flag);
	if (ret < 0) {
		BTMTK_ERR("%s, btmtk_send_fw_rom_patch_79xx failed!", __func__);
		goto err;
	}

	BTMTK_INFO("btmtk_load_wifi_rom_patch_79xx end");

err:
	if (fw_firmware)
		release_firmware(fw_firmware);
	return ret;
}

int btmtk_load_rom_patch_766x(struct btmtk_dev *bdev)
{
	u32 patch_len = 0;
	int ret = 0;
	int patch_status = 0;
	int retry = 20;
	u8 *pos = NULL;
	u8 event[] = {0x04, 0xE4, 0x05, 0x02, 0x01, 0x01, 0x00, 0x00};
	const struct firmware *fw_firmware = NULL;
	u8 *rom_patch = NULL;
	unsigned int rom_patch_len = 0;
	struct _PATCH_HEADER *patchHdr;

	if (!bdev) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		return -EINVAL;
	}

	btmtk_load_code_from_bin(&fw_firmware, bdev->rom_patch_bin_file_name, NULL,
			&rom_patch, &rom_patch_len);

	do {
		patch_status = btmtk_check_need_load_rom_patch(bdev);
		BTMTK_INFO("%s: patch_status %d", __func__, patch_status);

		if (patch_status > MT766X_PATCH_NEED_DOWNLOAD || patch_status == PATCH_ERR) {
			BTMTK_ERR("%s: patch_status error", __func__);
			ret = -1;
			goto err1;
		} else if (patch_status == MT766X_PATCH_READY) {
			BTMTK_INFO("%s: no need to load rom patch", __func__);
			goto patch_end;
		} else if (patch_status == MT766X_PATCH_IS_DOWNLOAD_BY_OTHER) {
			msleep(100);
			retry--;
		} else if (patch_status == MT766X_PATCH_NEED_DOWNLOAD) {
/* TODO*/
#if 0
			if (is_mt7663(g_card)) {
				if (btmtk_sdio_send_wmt_cfg())
					BTMTK_ERR("send wmt cfg failed!");
			}
#endif
			break;  /* Download ROM patch directly */
		}
	} while (retry > 0);

	if (patch_status == PATCH_IS_DOWNLOAD_BY_OTHER) {
		BTMTK_WARN("%s: Hold by another fun more than 2 seconds", __func__);
		ret = -1;
		goto err1;
	}

	patchHdr = (struct _PATCH_HEADER *)rom_patch;
	/*Display rom patch info*/
	btmtk_print_bt_patch_info(bdev, rom_patch);

	pos = kmalloc(UPLOAD_PATCH_UNIT, GFP_ATOMIC);
	if (!pos) {
		BTMTK_ERR("%s: alloc memory failed", __func__);
		ret = -1;
		goto err1;
	}

	patch_len = rom_patch_len - PATCH_INFO_SIZE;

	BTMTK_INFO("%s: loading rom patch...\n", __func__);
	BTMTK_INFO("%s: patch_len = %d\n", __func__, patch_len);
	ret = btmtk_load_fw_patch_using_wmt_cmd(bdev, pos, rom_patch, event,
			sizeof(event) - 1, patch_len, PATCH_INFO_SIZE);
	if (ret < 0) {
		BTMTK_ERR("%s, btmtk_send_fw_rom_patch_766x failed!", __func__);
		goto err0;
	}

	ret = btmtk_send_wmt_reset(bdev);
	if (ret < 0) {
		BTMTK_ERR("%s: btmtk_send_wmt_reset failed!", __func__);
		goto err0;
	}
	BTMTK_INFO("%s: loading rom patch... Done", __func__);

	btmtk_send_hw_err_to_host(bdev);

patch_end:
	bdev->power_state = BTMTK_DONGLE_STATE_POWER_OFF;
	BTMTK_INFO("btmtk_load_rom_patch end");

err0:
	kfree(pos);
	pos = NULL;

err1:
	if (fw_firmware)
		release_firmware(fw_firmware);
	return ret;
}

int btmtk_load_rom_patch(struct btmtk_dev *bdev)
{
	int err = -1;

	if (!bdev || !bdev->hdev) {
		BTMTK_ERR("%s: invalid parameters!", __func__);
		return err;
	}

	CHIP_STATE_MUTEX_LOCK();
	btmtk_set_chip_state(bdev, BTMTK_STATE_PROBE);
	CHIP_STATE_MUTEX_UNLOCK();

	if (is_mt7663(bdev->chip_id))
		err = btmtk_load_rom_patch_766x(bdev);
	else if (is_mt7961(bdev->chip_id)) {
		if (bdev->hdev->id == 0) {
			err = btmtk_load_rom_patch_79xx(bdev, BT_DOWNLOAD);
			if (err < 0) {
				BTMTK_ERR("%s: btmtk_load_rom_patch_79xx bt patch failed!", __func__);
				return err;
			}

			err = btmtk_load_rom_patch_79xx(bdev, WIFI_DOWNLOAD);
			if (err < 0) {
				BTMTK_WARN("%s: btmtk_load_rom_patch_79xx wifi patch failed!", __func__);
				err = 0;
			}
		} else {
			BTMTK_INFO("%s: hci%d no need to download rom patch", __func__, bdev->hdev->id);
			err = 0;
		}
	} else
		BTMTK_WARN("%s: unknown chip id (%d)", __func__, bdev->chip_id);

	if (err >= 0) {
		CHIP_STATE_MUTEX_LOCK();
		btmtk_set_chip_state(bdev, BTMTK_STATE_WORKING);
		CHIP_STATE_MUTEX_UNLOCK();
	}

	return err;
}

static int btmtk_calibration_flow(struct btmtk_dev *bdev)
{
	if (!bdev) {
		BTMTK_ERR("%s: bdev is NULL !", __func__);
		return -1;
	}

	btmtk_cif_send_calibration(bdev);
	BTMTK_INFO("%s done", __func__);
	return 0;
}

int btmtk_send_wmt_reset(struct btmtk_dev *bdev)
{
	/* Support 7668 and 7663 */
	u8 cmd[] = { 0x01, 0x6F, 0xFC, 0x05, 0x01, 0x07, 0x01, 0x00, 0x04 };
	/* To-Do, for event check */
	u8 event[] = { 0x04, 0xE4, 0x05, 0x02, 0x07, 0x01, 0x00, 0x00 };
	int ret = -1;

	if (!bdev) {
		BTMTK_ERR("%s: bdev is NULL !", __func__);
		return ret;
	}
	ret = btmtk_main_send_cmd(bdev, cmd, sizeof(cmd), event, sizeof(event), 20,
			0, BTMTK_EP_TYPE_OUT_CMD, BTMTK_TX_WAIT_VND_EVT, true);

	BTMTK_INFO("%s done", __func__);
	return ret;
}

int btmtk_send_wmt_power_on_cmd_766x(struct btmtk_dev *bdev)
{
	/* Support 7668 and 7663 */
	u8 cmd[] = { 0x01, 0x6F, 0xFC, 0x06, 0x01, 0x06, 0x02, 0x00, 0x00, 0x01 };
	/* To-Do, for event check */
	u8 event[] = { 0x04, 0xE4, 0x05, 0x02, 0x06, 0x01, 0x00 };	/* event[6] is key */
	int ret = -1;

	if (!bdev) {
		BTMTK_ERR("%s: bdev is NULL !", __func__);
		return ret;
	}

	bdev->power_state = BTMTK_DONGLE_STATE_POWERING_ON;
	ret = btmtk_main_send_cmd(bdev, cmd, sizeof(cmd), event, sizeof(event), 100,
			20, BTMTK_EP_TYPE_OUT_CMD, BTMTK_TX_WAIT_VND_EVT, true);
	if (ret < 0) {
		BTMTK_ERR("%s: failed(%d)", __func__, ret);
		bdev->power_state = BTMTK_DONGLE_STATE_ERROR;
		ret = -1;
	} else if (ret > 0) {
		switch (bdev->io_buf[6]) {
		case 0:			 /* successful */
			BTMTK_INFO("%s: OK", __func__);
			bdev->power_state = BTMTK_DONGLE_STATE_POWER_ON;
			break;
		case 2:			 /* TODO:retry */
			BTMTK_INFO("%s: need to try again", __func__);
			break;
		default:
			BTMTK_WARN("%s: Unknown result: %02X", __func__, bdev->io_buf[6]);
			bdev->power_state = BTMTK_DONGLE_STATE_ERROR;
			ret = -1;
			break;
		}
	}

	return ret;
}

int btmtk_send_wmt_power_off_cmd_766x(struct btmtk_dev *bdev)
{
	/* Support 7668 and 7663 */
	u8 cmd[] = { 0x01, 0x6F, 0xFC, 0x06, 0x01, 0x06, 0x02, 0x00, 0x00, 0x00 };
	/* To-Do, for event check */
	u8 event[] = { 0x04, 0xE4, 0x05, 0x02, 0x06, 0x01, 0x00 };
	int ret = -1;

	if (!bdev) {
		BTMTK_ERR("%s: bdev is NULL !", __func__);
		return ret;
	}

	if (bdev->power_state == BTMTK_DONGLE_STATE_POWER_OFF) {
		BTMTK_WARN("%s: power_state already power off", __func__);
		return 0;
	}

	bdev->power_state = BTMTK_DONGLE_STATE_POWERING_OFF;
	ret = btmtk_main_send_cmd(bdev, cmd, sizeof(cmd), event, sizeof(event), 20,
			20, BTMTK_EP_TYPE_OUT_CMD, BTMTK_TX_WAIT_VND_EVT, true);
	if (ret < 0) {
		BTMTK_ERR("%s: failed(%d)", __func__, ret);
		bdev->power_state = BTMTK_DONGLE_STATE_ERROR;
		return ret;
	}

	bdev->power_state = BTMTK_DONGLE_STATE_POWER_OFF;
	BTMTK_INFO("%s done", __func__);
	return ret;
}

#if 0
int btmtk_uart_send_wakeup_cmd(struct hci_dev *hdev)
{
	u8 cmd[] = { 0xFF };
	/* To-Do, for event check */
	/* u8 event[] = { 0x04, 0xE4, 0x06, 0x02, 0x03, 0x02, 0x00, 0x00, 0x03}; */

	btmtk_main_send_cmd(hdev, cmd, sizeof(cmd), BTMTKUART_TX_WAIT_VND_EVT);
	return 0;
}


int btmtk_uart_send_set_uart_cmd(struct hci_dev *hdev)
{
	u8 cmd[] = { 0x01, 0x6F, 0xFC, 0x09, 0x01, 0x04, 0x05, 0x00, 0x01, 0x00, 0x10, 0x0E, 0x00};
	/* To-Do, for event check */
	/* u8 event[] = {0x04, 0xE4, 0x06, 0x02, 0x04, 0x02, 0x00, 0x00, 0x01}; */
	btmtk_main_send_cmd(hdev, cmd, sizeof(cmd), BTMTKUART_TX_WAIT_VND_EVT);

	return 0;
}

#endif

#if ENABLESTP
static int btmtk_send_set_stp_cmd(struct btmtk_dev *bdev)
{
	u8 cmd[] = { 0x01, 0x6F, 0xFC, 0x09, 0x01, 0x04, 0x05, 0x00, 0x03, 0x11, 0x0E, 0x00, 0x00};
	/* To-Do, for event check */
	u8 event[] = { 0x04, 0xE4, 0x06, 0x02, 0x04, 0x02, 0x00, 0x00, 0x03};
	int ret = 0;

	ret = btmtk_main_send_cmd(bdev, cmd, sizeof(cmd), event, sizeof(event), 0, 0,
			BTMTK_EP_TYPE_OUT_CMD, BTMTK_TX_WAIT_VND_EVT, true);

	BTMTK_INFO("%s done", __func__);
	return ret;
}

static int btmtk_send_set_stp1_cmd(struct btmtk_dev *bdev)
{
	u8 cmd[] = {0x01, 0x6F, 0xFC, 0x0C, 0x01, 0x08, 0x08, 0x00, 0x02, 0x01, 0x00, 0x01, 0x08, 0x00, 0x00, 0x80};
	/* To-Do, for event check */
	u8 event[] = {0x04, 0xE4, 0x10, 0x02, 0x08,
			0x0C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x80, 0x63, 0x76, 0x00, 0x00};
	int ret = 0;

	ret = btmtk_main_send_cmd(bdev, cmd, sizeof(cmd), event, sizeof(event), 0, 0,
			BTMTK_EP_TYPE_OUT_CMD, BTMTK_TX_WAIT_VND_EVT, true);

	BTMTK_INFO("%s done", __func__);
	return ret;
}
#endif

static int btmtk_send_hci_tci_set_sleep_cmd_766x(struct btmtk_dev *bdev)
{
	u8 cmd[] = { 0x01, 0x7A, 0xFC, 0x07, 0x05, 0x40, 0x06, 0x40, 0x06, 0x00, 0x00 };
	/* To-Do, for event check */
	u8 event[] = { 0x04, 0x0E, 0x04, 0x01, 0x7A, 0xFC, 0x00 };
	int ret = -1;

	if (!bdev) {
		BTMTK_ERR("%s: bdev is NULL !", __func__);
		return ret;
	}

	ret = btmtk_main_send_cmd(bdev, cmd, sizeof(cmd), event, sizeof(event), 0, 0,
			BTMTK_EP_TYPE_OUT_CMD, BTMTK_TX_WAIT_VND_EVT, false);

	BTMTK_INFO("%s done", __func__);

	return ret;
}


int btmtk_send_init_cmds(struct btmtk_dev *bdev)
{
	int ret = -1;

	if (!bdev) {
		BTMTK_ERR("%s: bdev is NULL !", __func__);
		return ret;
	}

	BTMTK_INFO("%s", __func__);

#if ENABLESTP
	btmtk_send_set_stp_cmd(bdev);
	btmtk_send_set_stp1_cmd(bdev);
#endif
	ret = btmtk_calibration_flow(bdev);
	if (ret < 0) {
		BTMTK_ERR("%s, btmtk_calibration_flow failed!", __func__);
		return ret;
	}
	ret = btmtk_send_wmt_power_on_cmd_766x(bdev);
	if (ret < 0) {
		if (bdev->power_state != BTMTK_DONGLE_STATE_POWER_ON) {
			BTMTK_ERR("%s, btmtk_send_wmt_power_on_cmd_766x failed!", __func__);
			if (bdev->subsys_reset == HW_ERR_NONE)
				bdev->subsys_reset = HW_ERR_CODE_POWER_ON;
			/* TODO */
			/* btmtk_usb_toggle_rst_pin(); */
		}
		return ret;
	}

	if (is_mt7663(bdev->chip_id))
		ret = btmtk_send_hci_tci_set_sleep_cmd_766x(bdev);

	return ret;
}


int btmtk_send_deinit_cmds(struct btmtk_dev *bdev)
{
	if (!bdev) {
		BTMTK_ERR("%s: bdev is NULL !", __func__);
		return -1;
	}

	BTMTK_INFO("%s", __func__);

	return btmtk_send_wmt_power_off_cmd_766x(bdev);
}

static int btmtk_send_assert_cmd_bulk(struct btmtk_dev *bdev)
{
	int ret = -1;
	u8 buf[] = { 0x02, 0x6F, 0xFC, 0x05, 0x00, 0x01, 0x02, 0x01, 0x00, 0x08 };
	struct sk_buff *skb = NULL;

	if (!bdev) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		ret = -EINVAL;
		goto exit;
	}

	skb = alloc_skb(sizeof(buf) + BT_SKB_RESERVE, GFP_ATOMIC);
	if (!skb) {
		BTMTK_ERR("%s allocate skb failed!!", __func__);
		goto exit;
	}
	/* Reserv for core and drivers use */
	skb_reserve(skb, 7);
	bt_cb(skb)->pkt_type = HCI_ACLDATA_PKT;
	memcpy(skb->data, buf, sizeof(buf));
	skb->len = sizeof(buf);

	ret = btmtk_cif_send_cmd(bdev, skb, 0, 0, BTMTK_EP_TYPE_OUT_OTHER, false);
	if (ret < 0)
		BTMTK_ERR("%s failed!!", __func__);
	else
		BTMTK_INFO("%s: OK", __func__);

	kfree_skb(skb);

exit:
	return ret;
}

static int btmtk_send_assert_cmd(struct btmtk_dev *bdev)
{
	int ret = 0;

	/* TODO, check chip state */
#if 0
	int state = btmtk_usb_get_state();

	if (state == BTMTK_USB_STATE_FW_DUMP || state == BTMTK_USB_STATE_SUSPEND_FW_DUMP
			|| state == BTMTK_USB_STATE_RESUME_FW_DUMP) {
		BTUSB_WARN("%s: FW dumping already!!!", __func__);
		return ret;
	}
#endif

	BTMTK_INFO("%s: send assert cmd", __func__);

	ret = btmtk_send_assert_cmd_bulk(bdev);
	if (ret < 0) {
		BTMTK_ERR("%s: send assert cmd fail, tigger hw reset only", __func__);
		/* TODO */
		/* btmtk_usb_start_reset_dongle_progress(); */
	}

	return ret;
}

/**
 * Kernel HCI Interface Registeration
 */
static int bt_flush(struct hci_dev *hdev)
{
	return 0;
}

static int bt_close(struct hci_dev *hdev)
{
	int ret = -1;
	int state = BTMTK_STATE_INIT;
	int fstate = BTMTK_FOPS_STATE_INIT;
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	if (!bdev || !hdev) {
		BTMTK_ERR("%s: invalid parameters!", __func__);
		return ret;
	}

	FOPS_MUTEX_LOCK();
	fstate = btmtk_fops_get_state(bdev);
	if (fstate != BTMTK_FOPS_STATE_OPENED) {
		BTMTK_WARN("%s: fops is not allow close(%d)", __func__, fstate);
		FOPS_MUTEX_UNLOCK();
		return 0;
	}
	btmtk_fops_set_state(bdev, BTMTK_FOPS_STATE_CLOSING);
	FOPS_MUTEX_UNLOCK();

	CHIP_STATE_MUTEX_LOCK();
	state = btmtk_get_chip_state(bdev);
	CHIP_STATE_MUTEX_UNLOCK();
	if (state != BTMTK_STATE_WORKING) {
		BTMTK_WARN("%s: not in working state and standby state(%d).", __func__, state);
		FOPS_MUTEX_LOCK();
		btmtk_fops_set_state(bdev, BTMTK_FOPS_STATE_CLOSED);
		FOPS_MUTEX_UNLOCK();
		return 0;
	}

	BTMTK_INFO("%s", __func__);
	ret = btmtk_send_deinit_cmds(bdev);
	if (ret < 0) {
		BTMTK_ERR("%s, btmtk_send_deinit_cmds failed", __func__);
		if (bdev->power_state != BTMTK_DONGLE_STATE_POWER_OFF) {
			BTMTK_ERR("Power off dongle failed, reset it");
			if (bdev->subsys_reset == HW_ERR_NONE)
				bdev->subsys_reset = HW_ERR_CODE_POWER_OFF;
			btmtk_send_assert_cmd(bdev);
		}
		return ret;
	}

	btmtk_cif_close(hdev);

	FOPS_MUTEX_LOCK();
	btmtk_fops_set_state(bdev, BTMTK_FOPS_STATE_CLOSED);
	FOPS_MUTEX_UNLOCK();

	bdev->subsys_reset = HW_ERR_NONE;
	return 0;
}

static int bt_open(struct hci_dev *hdev)
{
	int ret = -1;
	int state = BTMTK_STATE_INIT;
	int fstate = BTMTK_FOPS_STATE_INIT;
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	if (!bdev || !hdev) {
		BTMTK_ERR("%s: invalid parameters!", __func__);
		goto failed;
	}

	CHIP_STATE_MUTEX_LOCK();
	state = btmtk_get_chip_state(bdev);
	CHIP_STATE_MUTEX_UNLOCK();
	if (state == BTMTK_STATE_INIT || state == BTMTK_STATE_DISCONNECT) {
		ret = -EAGAIN;
		goto failed;
	}

	if (state != BTMTK_STATE_WORKING) {
		BTMTK_WARN("%s: not in working state(%d).", __func__, state);
		ret = -ENODEV;
		goto failed;
	}

	FOPS_MUTEX_LOCK();
	fstate = btmtk_fops_get_state(bdev);
	FOPS_MUTEX_UNLOCK();
	if (fstate == BTMTK_FOPS_STATE_OPENED) {
		BTMTK_WARN("%s: fops opened!", __func__);
		ret = 0;
		goto failed;
	}

	if (fstate == BTMTK_FOPS_STATE_CLOSING) {
		BTMTK_WARN("%s: fops close is on-going !", __func__);
		ret = -EAGAIN;
		goto failed;
	}

	BTMTK_INFO("%s", __func__);
	ret = btmtk_cif_open(hdev);
	if (ret < 0) {
		BTMTK_ERR("%s, btmtk_cif_open failed", __func__);
		goto failed;
	}

	ret = btmtk_send_init_cmds(bdev);
	if (ret < 0) {
		BTMTK_ERR("%s, btmtk_send_init_cmds failed", __func__);
		goto failed;
	}

	FOPS_MUTEX_LOCK();
	btmtk_fops_set_state(bdev, BTMTK_FOPS_STATE_OPENED);
	FOPS_MUTEX_UNLOCK();

	return 0;

failed:
	return ret;
}

static int bt_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	int ret = -1;
	int state = BTMTK_STATE_INIT;
	int fstate = BTMTK_FOPS_STATE_INIT;
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	if (hdev == NULL || skb == NULL) {
		BTMTK_ERR("%s, invalid parameters!", __func__);
		goto exit;
	}

	FOPS_MUTEX_LOCK();
	fstate = btmtk_fops_get_state(bdev);
	FOPS_MUTEX_UNLOCK();
	if (fstate != BTMTK_FOPS_STATE_OPENED) {
		BTMTK_WARN("%s: fops is not open yet(%d)!", __func__, fstate);
		ret = -ENODEV;
		goto err;
	}

	CHIP_STATE_MUTEX_LOCK();
	state = btmtk_get_chip_state(bdev);
	CHIP_STATE_MUTEX_UNLOCK();
	if (state != BTMTK_STATE_WORKING) {
		BTMTK_WARN("%s: current is in suspend/resume/standby (%d).", __func__, state);
		msleep(3000);
		ret = -EAGAIN;
		goto err;
	}

	if (bdev->power_state == BTMTK_DONGLE_STATE_POWER_OFF) {
		BTMTK_WARN("%s: dongle state already power off, do not write", __func__);
		ret = -EFAULT;
		goto err;
	}

	memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);
#if ENABLESTP
	skb = mtk_add_stp(bdev, skb);
#endif

	ret = btmtk_cif_send_cmd(bdev, skb, 0, 0, BTMTK_EP_TYPE_OUT_OTHER, false);
	if (ret < 0)
		BTMTK_ERR("%s failed!!", __func__);
	else
		goto exit;

err:
	kfree_skb(skb);

exit:
	return ret;
}

static int bt_setup(struct hci_dev *hdev)
{
	BTMTK_INFO("%s", __func__);
	return 0;
}

static void btmtk_rx_work(struct work_struct *work)
{
	int err = 0, skip_pkt = 0;
	struct btmtk_dev *bdev = container_of(work, struct btmtk_dev, rx_work);
	struct sk_buff *skb;

	BTMTK_DBG("%s enter!", __func__);

	while ((skb = skb_dequeue(&bdev->rx_q))) {
		if (skb->len == 1 && bdev->urb_transfer_buf[1] == 0xFF) {
			/* We can't use usb_control_msg in interrupt.
			 * If you use usb_control_msg , it will cause crash.
			 */
			/* TODO: need to rewrite reset interrupt on EP15 for all interfaces,
			 * maybe need to move reset_waker to btmtk_dev for common
			 */
			schedule_work(&bdev->reset_waker);
			continue;
		}
		skip_pkt = btmtk_dispatch_acl(bdev->hdev, skb);
		if (skip_pkt == 0)
			err = hci_recv_frame(bdev->hdev, skb);
		if (err < 0) {
			BTMTK_ERR("%s btmtk_rx_work failed, err = %d", __func__, err);
			return;
		}
	}
}

void btmtk_free_hci_device(struct btmtk_dev *bdev, int hci_bus_type)
{
	int fstate = BTMTK_FOPS_STATE_INIT;

	if (!bdev)
		return;

	/* Flush RX works */
	flush_work(&bdev->rx_work);

	/* Drop queues */
	skb_queue_purge(&bdev->rx_q);
	destroy_workqueue(bdev->workqueue);

	hci_unregister_dev(bdev->hdev);
	hci_free_dev(bdev->hdev);

	FOPS_MUTEX_LOCK();
	fstate = btmtk_fops_get_state(bdev);
	FOPS_MUTEX_UNLOCK();
	if (fstate == BTMTK_FOPS_STATE_OPENED || fstate == BTMTK_FOPS_STATE_CLOSING) {
		BTMTK_WARN("%s: fstate = %d , set subsys_reset", __func__, fstate);
		if (bdev->subsys_reset == HW_ERR_NONE)
			bdev->subsys_reset = HW_ERR_CODE_USB_DISC;
	}

	CHIP_STATE_MUTEX_LOCK();
	btmtk_set_chip_state(bdev, BTMTK_STATE_DISCONNECT);
	CHIP_STATE_MUTEX_UNLOCK();

	BTMTK_INFO("%s done", __func__);
}

int btmtk_allocate_hci_device(struct btmtk_dev *bdev, int hci_bus_type)
{
	struct hci_dev *hdev;
	int err = 0;

	if (!bdev) {
		BTMTK_ERR("%s, bdev is NULL!", __func__);
		err = -EINVAL;
		goto exit;
	}

	BTMTK_INFO("%s", __func__);
	/* Add hci device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		BTMTK_ERR("%s, hdev is NULL!", __func__);
		err = -ENOMEM;
		goto exit;
	}

	hdev->bus = hci_bus_type;
	hci_set_drvdata(hdev, bdev);

	/* HCI_PRIMARY = 0x00 */
	hdev->dev_type = 0x00;

	bdev->hdev = hdev;

	/* register hci callback */
	hdev->open	   = bt_open;
	hdev->close    = bt_close;
	hdev->flush    = bt_flush;
	hdev->send	   = bt_send_frame;
	hdev->setup    = bt_setup;

	INIT_WORK(&bdev->rx_work, btmtk_rx_work);

	init_waitqueue_head(&bdev->p_wait_event_q);

	skb_queue_head_init(&bdev->rx_q);

	bdev->workqueue = alloc_workqueue("BTMTK_RX_WQ", WQ_HIGHPRI | WQ_UNBOUND |
					  WQ_MEM_RECLAIM, 1);
	if (!bdev->workqueue) {
		BTMTK_ERR("%s, bdev->workqueue is NULL!", __func__);
		err = -ENOMEM;
		goto exit;
	}

	BTMTK_INFO("%s done", __func__);

exit:
	return err;
}

int btmtk_register_hci_device(struct btmtk_dev *bdev)
{
	struct hci_dev *hdev;
	int err = 0;

	hdev = bdev->hdev;

	err = hci_register_dev(hdev);
	/* After hci_register_dev completed
	 * It will set dev_flags to HCI_SETUP
	 * That cause vendor_lib create socket failed
	 */
	if (err < 0) {
		BTMTK_INFO("%s can't register", __func__);
		hci_free_dev(hdev);
		goto exit;
	}

#if CFG_SUPPORT_BLUEZ

#else
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
		test_and_clear_bit(HCI_SETUP, &hdev->dev_flags);
#else
		hci_dev_test_and_clear_flag(hdev, HCI_SETUP);
#endif
#endif /* CFG_SUPPORT_BLUEZ */

exit:
	return err;
}

void btmtk_cap_init(struct btmtk_dev *bdev)
{
	if (!bdev) {
		BTMTK_ERR("%s, bdev is NULL!", __func__);
		return;
	}

	btmtk_cif_read_register(bdev, CHIP_ID, &bdev->chip_id);
	if (is_mt7961(bdev->chip_id)) {
		btmtk_cif_read_register(bdev, FLAVOR, &bdev->flavor);
		btmtk_cif_read_register(bdev, FW_VERSION, &bdev->fw_version);
		goto exit;
	} else {
		BTMTK_ERR("Unknown Mediatek device(%04X)\n", bdev->chip_id);
		return;
	}

exit:
	BTMTK_INFO("%s: Chip ID = 0x%x", __func__, bdev->chip_id);
	BTMTK_INFO("%s: flavor = 0x%x", __func__, bdev->flavor);
	BTMTK_INFO("%s: FW Ver = 0x%x", __func__, bdev->fw_version);

	memset(bdev->rom_patch_bin_file_name, 0, MAX_BIN_FILE_NAME_LEN);
	if ((bdev->fw_version & 0xff) == 0xff) {
		BTMTK_ERR("%s: failed, wrong FW version : 0x%x !", __func__, bdev->fw_version);
		return;
	}

	/* Bin filename format : "BT_RAM_CODE_MT%04x_%x_%x_hdr.bin"
	 *  $$$$ : chip id
	 *  % : fw version & 0xFF + 1 (in HEX)
	 */
	bdev->flavor = (bdev->flavor & 0x00000080) >> 7;
	BTMTK_INFO("%s: flavor1 = 0x%x", __func__, bdev->flavor);

	/* if flavor equals 1, it represent 7920, else it represent 7921 */
	if (bdev->flavor)
		snprintf(bdev->rom_patch_bin_file_name, MAX_BIN_FILE_NAME_LEN, "BT_RAM_CODE_MT%04x_1a_%x_hdr.bin",
				bdev->chip_id & 0xffff, (bdev->fw_version & 0xff) + 1);
	else
		snprintf(bdev->rom_patch_bin_file_name, MAX_BIN_FILE_NAME_LEN, "BT_RAM_CODE_MT%04x_1_%x_hdr.bin",
				bdev->chip_id & 0xffff, (bdev->fw_version & 0xff) + 1);

	BTMTK_INFO("%s: rom patch file name is %s", __func__, bdev->rom_patch_bin_file_name);
}

/**
 * Kernel Module init/exit Functions
 */
static int __init main_driver_init(void)
{
	int ret = 0;

	BTMTK_INFO("%s", __func__);
	ret = main_init();
	if (ret < 0)
		return ret;

	ret = btmtk_cif_register();
	if (ret < 0)
		BTMTK_ERR("*** USB registration failed(%d)! ***", ret);

	BTMTK_INFO("%s: Done", __func__);
	return ret;
}

static void __exit main_driver_exit(void)
{
	BTMTK_INFO("%s", __func__);
	btmtk_cif_deregister();
	main_exit();
}
module_init(main_driver_init);
module_exit(main_driver_exit);

/**
 * Module Common Information
 */
MODULE_DESCRIPTION("Mediatek Bluetooth Driver");
MODULE_VERSION(VERSION SUBVER);
MODULE_LICENSE("GPL");
