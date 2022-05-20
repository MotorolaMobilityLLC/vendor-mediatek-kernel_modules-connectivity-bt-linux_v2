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

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "btmtk_config.h"
#include "btmtk_define.h"
#include "btmtk_usb.h"
#include "btmtk_main.h"

static struct usb_driver btusb_driver;

static const struct usb_device_id btusb_table[] = {
	/* Mediatek MT7961 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e8d, 0x7961, 0xe0, 0x01, 0x01) },
	/* Mediatek MT7915 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e8d, 0x7915, 0xe0, 0x01, 0x01) },
	/* Mediatek MT7663 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e8d, 0x7663, 0xe0, 0x01, 0x01) },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, btusb_table);

/* remove #define BTUSB_MAX_ISOC_FRAMES	10
 * ISCO_FRAMES max is 24
 */
#define BTUSB_MAX_ISOC_FRAMES	24

#define BTUSB_INTR_RUNNING	0
#define BTUSB_BULK_RUNNING	1
#define BTUSB_ISOC_RUNNING	2
#define BTUSB_SUSPENDING	3
#define BTUSB_DID_ISO_RESUME	4

#define DEVICE_VENDOR_REQUEST_IN	0xc0
#define DEVICE_CLASS_REQUEST_OUT	0x20
#define USB_CTRL_IO_TIMO		100

#define BTMTK_IS_BT_0_INTF(ifnum_base) \
	(ifnum_base == BT0_MCU_INTERFACE_NUM)

#define BTMTK_IS_BT_1_INTF(ifnum_base) \
	(ifnum_base == BT1_MCU_INTERFACE_NUM)

#define BTMTK_CIF_GET_DEV_PRIV(bdev, intf, ifnum_base) \
	do { \
		bdev = usb_get_intfdata(intf); \
		ifnum_base = intf->cur_altsetting->desc.bInterfaceNumber; \
	} while (0);

#define BTMTK_CIF_IS_NULL(bdev, cif_event) \
		(!bdev || !(&bdev->cif_state[cif_event]))

#define BTMTK_CIF_ASSIGN_STATE(bdev, cif_event, cif_state) \
	do { \
		cif_state = &bdev->cif_state[cif_event]; \
	} while (0);

#ifdef SUPPORT_STPBTFWLOG
/* fwlog_queue for picus and firmware dump */
struct usb_device *udev_0;

static dev_t g_devIDfwlog;
static int BT_majorfwlog;
static int print_dump_data_counter;
static struct cdev BT_cdevfwlog;
static wait_queue_head_t fw_log_inq;
struct sk_buff_head fwlog_queue;
struct class *pBTClass;
struct device *pBTDevfwlog;
static spinlock_t fwlog_lock;
static u8 btmtk_bluetooth_kpi;

#define FWLOG_QUEUE_COUNT			200
#define FWLOG_ASSERT_QUEUE_COUNT		6000
#define FWLOG_BLUETOOTH_KPI_QUEUE_COUNT		200
#define HCI_MAX_COMMAND_SIZE		255
#define HCI_MAX_COMMAND_BUF_SIZE	(HCI_MAX_COMMAND_SIZE * 3)

#define FWLOG_SPIN_LOCK(x)	spin_lock_irqsave(&fwlog_lock, x)
#define FWLOG_SPIN_UNLOCK(x)	spin_unlock_irqrestore(&fwlog_lock, x)
#endif

static int btmtk_cif_allocate_memory(struct btmtk_dev *bdev);
static void btmtk_cif_free_memory(struct btmtk_dev *bdev);

#ifdef SUPPORT_STPBTFWLOG
static int btmtk_skb_enq_fwlog(void *src, u32 len, u8 type, struct sk_buff_head *queue)
{
	struct sk_buff *skb_tmp = NULL;
	ulong flags = 0;
	int retry = 10;

	do {
		/* If we need hci type, len + 1 */
		skb_tmp = alloc_skb(type ? len + 1 : len, GFP_ATOMIC);
		if (skb_tmp != NULL)
			break;
		else if (retry <= 0) {
			pr_err("%s: alloc_skb return 0, error", __func__);
			return -ENOMEM;
		}
		pr_err("%s: alloc_skb return 0, error, retry = %d", __func__, retry);
	} while (retry-- > 0);

	if (type) {
		memcpy(&skb_tmp->data[0], &type, 1);
		memcpy(&skb_tmp->data[1], src, len);
		skb_tmp->len = len + 1;
	} else {
		memcpy(skb_tmp->data, src, len);
		skb_tmp->len = len;
	}

	FWLOG_SPIN_LOCK(flags);
	skb_queue_tail(queue, skb_tmp);
	FWLOG_SPIN_UNLOCK(flags);
	return 0;
}

static int btmtk_usb_dispatch_data_bluetooth_kpi(u8 *buf, int len, u8 type)
{
	static u8 fwlog_blocking_warn;
	int ret = 0;

	if (btmtk_bluetooth_kpi &&
		skb_queue_len(&fwlog_queue) < FWLOG_BLUETOOTH_KPI_QUEUE_COUNT) {
		/* sent event to queue, picus tool will log it for bluetooth KPI feature */
		if (btmtk_skb_enq_fwlog(buf, len, type, &fwlog_queue) == 0) {
			wake_up_interruptible(&fw_log_inq);
			fwlog_blocking_warn = 0;
		}
	} else {
		if (fwlog_blocking_warn == 0) {
			fwlog_blocking_warn = 1;
			pr_warn("btmtk_usb fwlog queue size is full(bluetooth_kpi)");
		}
	}
	return ret;
}

static int btmtk_usb_dispatch_acl(u8 *buf, int len)
{
	static u8 fwlog_picus_blocking_warn;
	static u8 fwlog_fwdump_blocking_warn;
	int ret = 0;

	if ((buf[0] == 0xff || buf[0] == 0xfe) && buf[1] == 0x05) {
		/* picus or syslog */
		if (skb_queue_len(&fwlog_queue) < FWLOG_QUEUE_COUNT) {
			/* sent picus data to queue, picus tool will log it */
			if (btmtk_skb_enq_fwlog(buf, len, 0, &fwlog_queue) == 0) {
				wake_up_interruptible(&fw_log_inq);
				fwlog_picus_blocking_warn = 0;
			}
		} else {
			if (fwlog_picus_blocking_warn == 0) {
				fwlog_picus_blocking_warn = 1;
				pr_warn("btmtk_usb fwlog queue size is full(picus)");
			}
		}
	} else if (buf[0] == 0x6f && buf[1] == 0xfc) {
		/* Coredump */
		if (skb_queue_len(&fwlog_queue) < FWLOG_ASSERT_QUEUE_COUNT) {
			/* sent coredump data to queue, picus tool will log it */
			if (btmtk_skb_enq_fwlog(buf, len, 0, &fwlog_queue) == 0) {
				wake_up_interruptible(&fw_log_inq);
				fwlog_fwdump_blocking_warn = 0;
			}
		} else {
			if (fwlog_fwdump_blocking_warn == 0) {
				fwlog_fwdump_blocking_warn = 1;
				pr_warn("btmtk_usb fwlog queue size is full(coredump)");
			}
		}
	}
	return ret;
}

static ssize_t btusb_fops_readfwlog(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	int copyLen = 0;
	ulong flags = 0;
	struct sk_buff *skb = NULL;

	BTMTK_DBG("%s: Start.\n", __func__);
	/* picus read a queue, it may occur performace issue */
	spin_lock_irqsave(&fwlog_lock, flags);
	if (skb_queue_len(&fwlog_queue))
		skb = skb_dequeue(&fwlog_queue);

	spin_unlock_irqrestore(&fwlog_lock, flags);
	if (skb == NULL)
		return 0;

	if (skb->len <= count) {
		if (copy_to_user(buf, skb->data, skb->len))
			BT_ERR("%s: copy_to_user failed!", __func__);

		copyLen = skb->len;
		BTMTK_DBG("%s: Reading data and buf is %02X %02X %02X", __func__,
				skb->data[0], skb->data[1], skb->data[2]);
	} else {
		BTMTK_DBG("%s: socket buffer length error(count: %d, skb.len: %d)", __func__, (int)count, skb->len);
	}
	kfree_skb(skb);
	return copyLen;
}

static ssize_t btusb_fops_writefwlog(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int i = 0, len = 0, ret = -1;

	u8 *i_fwlog_buf = kmalloc(HCI_MAX_COMMAND_BUF_SIZE, GFP_KERNEL);
	u8 *o_fwlog_buf = kmalloc(HCI_MAX_COMMAND_SIZE, GFP_KERNEL);

	if (count > HCI_MAX_COMMAND_BUF_SIZE) {
		BT_ERR("%s: your command is larger than maximum length, count = %zd\n",
				__func__, count);
		return -ENOMEM;
	}

	memset(i_fwlog_buf, 0, HCI_MAX_COMMAND_BUF_SIZE);
	memset(o_fwlog_buf, 0, HCI_MAX_COMMAND_SIZE);
	if (copy_from_user(i_fwlog_buf, buf, count) != 0) {
		BT_ERR("%s: Failed to copy data", __func__);
		return -ENODATA;
	}

	/* For bperf, EX: echo bperf=1 > /dev/stpbtfwlog */
	if (strcmp(i_fwlog_buf, "bperf=") >= 0) {
		u8 val = *(i_fwlog_buf + strlen("bperf=")) - 48;

		btmtk_bluetooth_kpi = val;
		BT_INFO("%s: set bluetooth KPI feature(bperf) to %d", __func__, btmtk_bluetooth_kpi);
		return count;
	}

	/* hci input command format : echo 01 be fc 01 05 > /dev/stpbtfwlog */
	/* We take the data from index three to end. */
	for (i = 0; i < count; i++) {
		char *pos = i_fwlog_buf + i;
		char temp_str[3] = {'\0'};
		long res = 0;

		if (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
			continue;
		} else if (*pos == '0' && (*(pos + 1) == 'x' || *(pos + 1) == 'X')) {
			i++;
			continue;
		} else if (!(*pos >= '0' && *pos <= '9') && !(*pos >= 'A' && *pos <= 'F')
			&& !(*pos >= 'a' && *pos <= 'f')) {
			BT_ERR("%s: There is an invalid input(%c)", __func__, *pos);
			return -EINVAL;
		}
		temp_str[0] = *pos;
		temp_str[1] = *(pos + 1);
		i++;
		ret = kstrtol(temp_str, 16, &res);
		if (ret == 0)
			o_fwlog_buf[len++] = (u8)res;
		else
			BT_ERR("%s: Convert %s failed(%d)", __func__, temp_str, ret);
	}

	if (o_fwlog_buf[0] != HCI_COMMAND_PKT) {
		BT_ERR("%s: Not support 0x%02X yet", __func__, o_fwlog_buf[0]);
		return -EPROTONOSUPPORT;
	}
	/* check HCI command length */
	if (len > HCI_MAX_COMMAND_SIZE) {
		BT_ERR("%s: command is larger than max buf size, length = %d", __func__, len);
		return -ENOMEM;
	}

        if (udev_0) {
	/* send HCI command */
	ret = usb_control_msg(udev_0, usb_sndctrlpipe(udev_0, 0),
				0, DEVICE_CLASS_REQUEST_OUT, 0, 0,
				(void *)o_fwlog_buf + 1, len, USB_CTRL_IO_TIMO);
	if (ret < 0) {
		BT_ERR("%s: command send failed(%d)", __func__, ret);
		return -EIO;
	}
	} else {
		return EINVAL;
	}
	BT_INFO("%s: Write end(len: %d)", __func__, len);
	return count;	/* If input is correct should return the same length */
}


static int btusb_fops_openfwlog(struct inode *inode, struct file *file)
{
	BTMTK_DBG("%s: Start.", __func__);

	return 0;
}

static int btusb_fops_closefwlog(struct inode *inode, struct file *file)
{
	BTMTK_DBG("%s: Start.", __func__);

	return 0;
}

static long btusb_fops_unlocked_ioctlfwlog(struct file *filp, unsigned int cmd, unsigned long arg)
{
	BTMTK_DBG("%s: Start.", __func__);

	return 0;
}

static unsigned int btusb_fops_pollfwlog(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	BTMTK_DBG("%s: Start.", __func__);
	poll_wait(file, &fw_log_inq, wait);
	if (skb_queue_len(&fwlog_queue) > 0)
		mask |= POLLIN | POLLRDNORM;			/* readable */

	BTMTK_DBG("%s: data count is %d!", __func__, skb_queue_len(&fwlog_queue));
	return mask;
}

const struct file_operations BT_fopsfwlog = {
	.open = btusb_fops_openfwlog,
	.release = btusb_fops_closefwlog,
	.read = btusb_fops_readfwlog,
	.write = btusb_fops_writefwlog,
	.poll = btusb_fops_pollfwlog,
	.unlocked_ioctl = btusb_fops_unlocked_ioctlfwlog
};

static int btusb_init(void)
{
	dev_t devIDfwlog = MKDEV(BT_majorfwlog, 0);
	int ret = 0;
	int cdevErr = 0;
	int majorfwlog = 0;

	if (pBTClass && pBTDevfwlog)
		return 0;

	spin_lock_init(&fwlog_lock);
	skb_queue_head_init(&fwlog_queue);
	init_waitqueue_head(&(fw_log_inq));

	ret = alloc_chrdev_region(&devIDfwlog, 0, 1, "BT_chrdevfwlog");
	if (ret) {
		BT_ERR("%s: fail to allocate chrdev", __func__);
		return ret;
	}

	BT_majorfwlog = majorfwlog = MAJOR(devIDfwlog);

	cdev_init(&BT_cdevfwlog, &BT_fopsfwlog);
	BT_cdevfwlog.owner = THIS_MODULE;

	cdevErr = cdev_add(&BT_cdevfwlog, devIDfwlog, 1);
	if (cdevErr)
		goto error;

	pBTClass = class_create(THIS_MODULE, "BT_chrdevfwlog");
	if (IS_ERR(pBTClass)) {
		BT_ERR("%s: class create fail, error code(%ld)\n", __func__, PTR_ERR(pBTClass));
		goto err1;
	}

	pBTDevfwlog = device_create(pBTClass, NULL, devIDfwlog, NULL, "stpbtfwlog");
	if (IS_ERR(pBTDevfwlog)) {
		BT_ERR("%s: device(stpbtfwlog) create fail, error code(%ld)", __func__, PTR_ERR(pBTDevfwlog));
		goto error;
	}
	BT_INFO("%s: BT_majorfwlog %d, devIDfwlog %d", __func__, BT_majorfwlog, devIDfwlog);

	g_devIDfwlog = devIDfwlog;
	init_waitqueue_head(&(fw_log_inq));

	return 0;

err1:
	if (pBTClass) {
		class_destroy(pBTClass);
		pBTClass = NULL;
	}

error:
	if (cdevErr == 0)
		cdev_del(&BT_cdevfwlog);

	if (ret == 0)
		unregister_chrdev_region(devIDfwlog, 1);

	return -1;
}

static int btusb_exit(void)
{
	dev_t devIDfwlog = g_devIDfwlog;

	BT_INFO("%s: Start\n", __func__);
	if (pBTDevfwlog) {
		device_destroy(pBTClass, devIDfwlog);
		pBTDevfwlog = NULL;
	}

	if (pBTClass) {
		class_destroy(pBTClass);
		pBTClass = NULL;
	}
	BT_INFO("%s: pBTDevfwlog, pBTClass done\n", __func__);
	cdev_del(&BT_cdevfwlog);
	unregister_chrdev_region(devIDfwlog, 1);
	BT_INFO("%s: BT_chrdevfwlog driver removed.\n", __func__);
	return 0;
}
#endif

static inline void btusb_free_frags(struct btmtk_dev *bdev)
{
	unsigned long flags;

	spin_lock_irqsave(&bdev->rxlock, flags);

	kfree_skb(bdev->evt_skb);
	bdev->evt_skb = NULL;

	kfree_skb(bdev->sco_skb);
	bdev->sco_skb = NULL;

	spin_unlock_irqrestore(&bdev->rxlock, flags);
}

static int btusb_recv_isoc(struct btmtk_dev *bdev, void *buffer, int count)
{
	struct sk_buff *skb;
	int err = 0;

	spin_lock(&bdev->rxlock);
	skb = bdev->sco_skb;

	while (count) {
		int len;

		if (!skb) {
			skb = bt_skb_alloc(HCI_MAX_SCO_SIZE, GFP_ATOMIC);
			if (!skb) {
				err = -ENOMEM;
				break;
			}

			hci_skb_pkt_type(skb) = HCI_SCODATA_PKT;
			hci_skb_expect(skb) = HCI_SCO_HDR_SIZE;
		}

		len = min_t(uint, hci_skb_expect(skb), count);
		memcpy(skb_put(skb, len), buffer, len);

		count -= len;
		buffer += len;
		hci_skb_expect(skb) -= len;

		if (skb->len == HCI_SCO_HDR_SIZE) {
			/* Complete SCO header */
			hci_skb_expect(skb) = hci_sco_hdr(skb)->dlen;

			if (skb_tailroom(skb) < hci_skb_expect(skb)) {
				kfree_skb(skb);
				skb = NULL;

				err = -EILSEQ;
				break;
			}
		}

		if (!hci_skb_expect(skb)) {
			/* Complete frame */
			hci_recv_frame(bdev->hdev, skb);
			skb = NULL;
		}
	}

	bdev->sco_skb = skb;
	spin_unlock(&bdev->rxlock);

	return err;
}

static void btusb_intr_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	int err;

	BTMTK_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (!bdev->urb_transfer_buf) {
			BT_ERR("%s: bdev->urb_transfer_buf is NULL!", __func__);
			return;
		}

		bdev->urb_transfer_buf[0] = HCI_EVENT_PKT;
		memcpy(bdev->urb_transfer_buf + 1, urb->transfer_buffer, urb->actual_length);

		BT_DBG("%s ,urb->actual_length = %d", __func__, urb->actual_length);
		BTMTK_DBG_RAW(bdev->urb_transfer_buf, urb->actual_length + 1, "%s, recv evt", __func__);
		BTMTK_DBG_RAW(urb->transfer_buffer, urb->actual_length, "%s, recv evt", __func__);
		if(bdev->urb_transfer_buf[1] == 0xFF && urb->actual_length == 1) {
			btmtk_set_chip_state(bdev, BTMTK_STATE_FW_DUMP);
			schedule_work(&bdev->reset_waker);
			goto intr_resub;
		}
		err = btmtk_recv(hdev, bdev->urb_transfer_buf, urb->actual_length + 1);
		if (err) {
			BT_ERR("%s corrupted event packet", hdev->name);
			hdev->stat.err_rx++;
		}

		/* TODO */
		/* btmtk_usb_dispatch_data_bluetooth_kpi(event_buf, urb->actual_length, HCI_EVENT_PKT); */
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_INTR_RUNNING, &bdev->flags))
		return;

intr_resub:
	usb_mark_last_busy(bdev->udev);
	usb_anchor_urb(urb, &bdev->intr_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_intr_reset_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	/* If WDT reset happened, fw will send a bytes (FF) to host */
	BTMTK_DBG("%s", hdev->name);

	if (!bdev->reset_intr_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;
	/* Default size is 16 */
	/* size = le16_to_cpu(data->intr_ep->wMaxPacketSize); */
	/* 7663 & 7668 & Buzzard Endpoint description.
	 * bEndpointAddress     0x8f  EP 15 IN
	 * wMaxPacketSize     0x0001  1x 1 bytes
	 */
	size = le16_to_cpu(HCI_MAX_EVENT_SIZE);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(bdev->udev, bdev->reset_intr_ep->bEndpointAddress);

	/* fw issue, we need to submit urb with a byte
	 * If driver set size = le16_to_cpu(HCI_MAX_EVENT_SIZE) to usb_fill_int_urb
	 * We can't get interrupt callback from bus.
	 */
	usb_fill_int_urb(urb, bdev->udev, pipe, buf, 1,
			 btusb_intr_complete, hdev, bdev->reset_intr_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &bdev->intr_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
				   hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_mtk_wmt_recv(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct sk_buff *skb;
	int err;

	BTMTK_DBG("%s : %s urb %p status %d count %d", __func__, hdev->name, urb, urb->status,
	       urb->actual_length);

	if (urb->status == 0 && urb->actual_length > 0) {
		BTMTK_DBG_RAW(urb->transfer_buffer, urb->actual_length, "%s, recv evt", __func__);
		hdev->stat.byte_rx += urb->actual_length;
		skb = bt_skb_alloc(HCI_MAX_EVENT_SIZE, GFP_ATOMIC);
		if (!skb) {
			hdev->stat.err_rx++;
		}

		hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
		memcpy(skb_put(skb, urb->actual_length), urb->transfer_buffer, urb->actual_length);
		BTMTK_DBG_RAW(skb->data, skb->len, "%s, skb recv evt", __func__);

		hci_recv_frame(hdev, skb);
		return;
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
			return;
	}

	usb_mark_last_busy(bdev->udev);

	/* The URB complete handler is still called with urb->actual_length = 0
	 * when the event is not available, so we should keep re-submitting
	 * URB until WMT event returns, Also, It's necessary to wait some time
	 * between the two consecutive control URBs to relax the target device
	 * to generate the event. Otherwise, the WMT event cannot return from
	 * the device successfully.
	 */
	udelay(100);

	usb_anchor_urb(urb, &bdev->ctrl_anchor);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			usb_unanchor_urb(urb);
	}
}

static int btusb_submit_wmt_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;
	unsigned int ifnum_base;

	BTMTK_DBG("%s : %s", __func__, hdev->name);

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(HCI_MAX_EVENT_SIZE);

	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	ifnum_base = bdev->intf->cur_altsetting->desc.bInterfaceNumber;

	if (BTMTK_IS_BT_0_INTF(ifnum_base)) {
		dr->bRequestType = 0xC0;
		dr->bRequest     = 0x01;
		dr->wIndex       = 0;
		dr->wValue       = 0x30;
		dr->wLength      = __cpu_to_le16(size);
	} else if (BTMTK_IS_BT_1_INTF(ifnum_base)) {
		dr->bRequestType = 0xA1;
		dr->bRequest     = 0x01;
		dr->wIndex       = 0x03;
		dr->wValue       = 0x30;
		dr->wLength      = __cpu_to_le16(size);
	}

	pipe = usb_rcvctrlpipe(bdev->udev, 0);

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		kfree(dr);
		return -ENOMEM;
	}

	usb_fill_control_urb(urb, bdev->udev, pipe, (void *)dr,
			     buf, size, btusb_mtk_wmt_recv, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &bdev->ctrl_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
					hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static int btusb_submit_intr_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	if (!bdev->intr_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	/* size = le16_to_cpu(data->intr_ep->wMaxPacketSize); */
	/* 7663 & 7668 & Buzzard Endpoint description.
	 * bEndpointAddress     0x81  EP 1 IN
	 * wMaxPacketSize     0x0010  1x 16 bytes
	 */
	size = le16_to_cpu(HCI_MAX_EVENT_SIZE);
	BT_INFO("%s: maximum packet size:%d", __func__, size);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(bdev->udev, bdev->intr_ep->bEndpointAddress);

	usb_fill_int_urb(urb, bdev->udev, pipe, buf, size,
			 btusb_intr_complete, hdev, bdev->intr_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &bdev->intr_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_bulk_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	int err;
#ifdef SUPPORT_STPBTFWLOG
	u8 *buf;
	u16 len;
#endif

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	/*
	 * This flag didn't support in kernel 4.x
	 * Driver will remove it
	 * if (!test_bit(HCI_RUNNING, &hdev->flags))
	 * return;
	 */
	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (!bdev->urb_transfer_buf) {
			BT_ERR("%s: bdev->urb_transfer_buf is NULL!", __func__);
			return;
		}

#ifdef SUPPORT_STPBTFWLOG
        buf = urb->transfer_buffer;
		len = buf[2] + ((buf[3] << 8) & 0xff00);
		if (buf[0] == 0x6f && buf[1] == 0xfc && len + 4 == urb->actual_length) {
			/* This is the fw dump data */
			/* print dump data to console */
			if (print_dump_data_counter < 20) {
				print_dump_data_counter++;
				BT_INFO("%s: FW dump data (%d): %s", __func__, print_dump_data_counter, &buf[4]);
			}
			btmtk_usb_dispatch_acl(buf, urb->actual_length);

			if (buf[urb->actual_length - 6] == ' ' &&
				buf[urb->actual_length - 5] == 'e' &&
				buf[urb->actual_length - 4] == 'n' &&
				buf[urb->actual_length - 3] == 'd') {
				/* This is the latest BULK_IN packet of FW dump. */
				BT_INFO("%s: btusb FW dump end", __func__);
			}
			/* should drop this packet */
			goto bulk_resub;
		} else if ((buf[0] == 0xff || buf[0] == 0xfe) && buf[1] == 0x05 && len + 4 == urb->actual_length) {
			btmtk_usb_dispatch_acl(buf, urb->actual_length);
			goto bulk_resub;
		}
#endif

		bdev->urb_transfer_buf[0] = HCI_ACLDATA_PKT;
		memcpy(bdev->urb_transfer_buf + 1, urb->transfer_buffer, urb->actual_length);

		err = btmtk_recv(hdev, bdev->urb_transfer_buf, urb->actual_length + 1);
		if (err) {
			BT_ERR("%s corrupted ACL packet", hdev->name);
			hdev->stat.err_rx++;
		}

		/* TODO */
#ifdef SUPPORT_STPBTFWLOG
		btmtk_usb_dispatch_data_bluetooth_kpi(buf, urb->actual_length, HCI_ACLDATA_PKT);
#endif
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_BULK_RUNNING, &bdev->flags))
		return;
#ifdef SUPPORT_STPBTFWLOG
bulk_resub:
#endif
	usb_anchor_urb(urb, &bdev->bulk_anchor);
	usb_mark_last_busy(bdev->udev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_bulk_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = HCI_MAX_FRAME_SIZE;

	BT_DBG("%s", hdev->name);

	if (!bdev->bulk_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(bdev->udev, bdev->bulk_rx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, bdev->udev, pipe, buf, size,
			  btusb_bulk_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(bdev->udev);
	usb_anchor_urb(urb, &bdev->bulk_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static int btusb_submit_intr_iso_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	if (!bdev->intr_iso_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;
	/* Default size is 16 */
	/* size = le16_to_cpu(data->intr_ep->wMaxPacketSize); */
	/* we need to consider the wMaxPacketSize in BLE ISO */
	size = le16_to_cpu(2000);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(bdev->udev, bdev->intr_iso_rx_ep->bEndpointAddress);
	BT_INFO("btusb_submit_intr_iso_urb : polling  0x%02X",  bdev->intr_iso_rx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, bdev->udev, pipe, buf, size,
			 btusb_bulk_complete, hdev, bdev->intr_iso_rx_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &bdev->intr_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
				   hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_isoc_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	int i, err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		for (i = 0; i < urb->number_of_packets; i++) {
			unsigned int offset = urb->iso_frame_desc[i].offset;
			unsigned int length = urb->iso_frame_desc[i].actual_length;

			if (urb->iso_frame_desc[i].status)
				continue;

			hdev->stat.byte_rx += length;

			if (btusb_recv_isoc(bdev, urb->transfer_buffer + offset,
					    length) < 0) {
				BT_ERR("%s corrupted SCO packet", hdev->name);
				hdev->stat.err_rx++;
			}
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_ISOC_RUNNING, &bdev->flags))
		return;

	usb_anchor_urb(urb, &bdev->isoc_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline void __fill_isoc_descriptor(struct urb *urb, int len, int mtu)
{
	int i, offset = 0;

	BT_DBG("len %d mtu %d", len, mtu);

	for (i = 0; i < BTUSB_MAX_ISOC_FRAMES && len >= mtu;
					i++, offset += mtu, len -= mtu) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = mtu;
	}

	if (len && i < BTUSB_MAX_ISOC_FRAMES) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = len;
		i++;
	}

	urb->number_of_packets = i;
}

static int btusb_submit_isoc_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	if (!bdev->isoc_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(bdev->isoc_rx_ep->wMaxPacketSize) *
						BTUSB_MAX_ISOC_FRAMES;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvisocpipe(bdev->udev, bdev->isoc_rx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, bdev->udev, pipe, buf, size, btusb_isoc_complete,
			 hdev, bdev->isoc_rx_ep->bInterval);

	urb->transfer_flags = URB_FREE_BUFFER | URB_ISO_ASAP;

	__fill_isoc_descriptor(urb, size,
			       le16_to_cpu(bdev->isoc_rx_ep->wMaxPacketSize));

	usb_anchor_urb(urb, &bdev->isoc_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	spin_lock(&bdev->txlock);
	bdev->tx_in_flight--;
	spin_unlock(&bdev->txlock);

	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static void btusb_isoc_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static int btusb_open(struct hci_dev *hdev)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	int err;
	unsigned int ifnum_base;

	BT_DBG("%s", hdev->name);

	err = usb_autopm_get_interface(bdev->intf);
	if (err < 0)
		return err;

	bdev->intf->needs_remote_wakeup = 1;

	if (test_and_set_bit(BTUSB_INTR_RUNNING, &bdev->flags))
		goto done;

	ifnum_base = bdev->intf->cur_altsetting->desc.bInterfaceNumber;

	if (is_mt7961(bdev->chip_id)) {
		BT_INFO("%s 7961 submit urb\n", __func__);
		if (BTMTK_IS_BT_0_INTF(ifnum_base)) {
			err = btusb_submit_intr_reset_urb(hdev, GFP_KERNEL);
			if (err < 0)
				goto failed;
			err = btusb_submit_intr_iso_urb(hdev, GFP_KERNEL);
			if (err < 0) {
				usb_kill_anchored_urbs(&bdev->intr_anchor);
				goto failed;
			}
		} else if (BTMTK_IS_BT_1_INTF(ifnum_base)) {
			/*need to do in bt_open in btmtk_main.c */
			/* btmtk_usb_send_power_on_cmd_7668(hdev); */
		}
	}
	err = btusb_submit_intr_urb(hdev, GFP_KERNEL);
	if (err < 0)
		goto failed;

	err = btusb_submit_bulk_urb(hdev, GFP_KERNEL);
	if (err < 0) {
		usb_kill_anchored_urbs(&bdev->intr_anchor);
		goto failed;
	}


	set_bit(BTUSB_BULK_RUNNING, &bdev->flags);

done:
	usb_autopm_put_interface(bdev->intf);
	return 0;

failed:
	clear_bit(BTUSB_INTR_RUNNING, &bdev->flags);
	usb_autopm_put_interface(bdev->intf);
	return err;
}

static void btusb_stop_traffic(struct btmtk_dev *bdev)
{
	usb_kill_anchored_urbs(&bdev->intr_anchor);
	usb_kill_anchored_urbs(&bdev->bulk_anchor);
	usb_kill_anchored_urbs(&bdev->isoc_anchor);
	usb_kill_anchored_urbs(&bdev->ctrl_anchor);
}

static int btusb_close(struct hci_dev *hdev)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s", hdev->name);

	cancel_work_sync(&bdev->work);
	cancel_work_sync(&bdev->waker);
	cancel_work_sync(&bdev->reset_waker);

	clear_bit(BTUSB_ISOC_RUNNING, &bdev->flags);
	clear_bit(BTUSB_BULK_RUNNING, &bdev->flags);
	clear_bit(BTUSB_INTR_RUNNING, &bdev->flags);

	btusb_stop_traffic(bdev);
	btusb_free_frags(bdev);

	err = usb_autopm_get_interface(bdev->intf);
	if (err < 0)
		goto failed;

	bdev->intf->needs_remote_wakeup = 0;
	usb_autopm_put_interface(bdev->intf);

failed:
	return 0;
}

/* Maybe will be used in the future*/
#if 0
static int btusb_flush(struct hci_dev *hdev)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	BT_DBG("%s", hdev->name);

	usb_kill_anchored_urbs(&bdev->tx_anchor);
	btusb_free_frags(bdev);

	return 0;
}
#endif

static struct urb *alloc_intr_iso_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned int pipe;

	if (!bdev->intr_iso_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	pipe = usb_sndintpipe(bdev->udev, bdev->intr_iso_tx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, bdev->udev, pipe,
			  skb->data, skb->len, btusb_tx_complete, skb, 1);

	skb->dev = (void *)hdev;

	return urb;
}

static struct urb *alloc_ctrl_bgf1_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	BT_DBG("%s\n", __func__);
	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr) {
		usb_free_urb(urb);
		return ERR_PTR(-ENOMEM);
	}

	dr->bRequestType = 0x21;
	dr->bRequest	 = 0x00;
	dr->wIndex	   = 3;
	dr->wValue	   = 0;
	dr->wLength	  = __cpu_to_le16(skb->len);

	pipe = usb_sndctrlpipe(bdev->udev, 0x00);

	usb_fill_control_urb(urb, bdev->udev, pipe, (void *)dr,
				 skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static struct urb *alloc_bulk_cmd_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned int pipe;

	BT_DBG("%s start\n", __func__);
	if (!bdev->bulk_cmd_tx_ep)
		return ERR_PTR(-ENODEV);

	BT_DBG("%s\n", __func__);
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	pipe = usb_sndbulkpipe(bdev->udev, bdev->bulk_cmd_tx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, bdev->udev, pipe,
			  skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static struct urb *alloc_ctrl_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr) {
		usb_free_urb(urb);
		return ERR_PTR(-ENOMEM);
	}

	dr->bRequestType = bdev->cmdreq_type;
	dr->bRequest     = bdev->cmdreq;
	dr->wIndex       = 0;
	dr->wValue       = 0;
	dr->wLength      = __cpu_to_le16(skb->len);

	pipe = usb_sndctrlpipe(bdev->udev, 0x00);

	usb_fill_control_urb(urb, bdev->udev, pipe, (void *)dr,
			     skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static struct urb *alloc_bulk_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned int pipe;

	if (!bdev->bulk_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	pipe = usb_sndbulkpipe(bdev->udev, bdev->bulk_tx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, bdev->udev, pipe,
			  skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static struct urb *alloc_isoc_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned int pipe;

	if (!bdev->isoc_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	pipe = usb_sndisocpipe(bdev->udev, bdev->isoc_tx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, bdev->udev, pipe,
			 skb->data, skb->len, btusb_isoc_tx_complete,
			 skb, bdev->isoc_tx_ep->bInterval);

	urb->transfer_flags  = URB_ISO_ASAP;

	__fill_isoc_descriptor(urb, skb->len,
			       le16_to_cpu(bdev->isoc_tx_ep->wMaxPacketSize));

	skb->dev = (void *)hdev;

	return urb;
}

static int submit_tx_urb(struct hci_dev *hdev, struct urb *urb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	int err;

	usb_anchor_urb(urb, &bdev->tx_anchor);

	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
			       hdev->name, urb, -err);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	} else {
		usb_mark_last_busy(bdev->udev);
	}

	usb_free_urb(urb);
	return err;
}

static int submit_or_queue_tx_urb(struct hci_dev *hdev, struct urb *urb)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	unsigned long flags;
	bool suspending;

	spin_lock_irqsave(&bdev->txlock, flags);
	suspending = test_bit(BTUSB_SUSPENDING, &bdev->flags);
	if (!suspending)
		bdev->tx_in_flight++;
	spin_unlock_irqrestore(&bdev->txlock, flags);

	if (!suspending)
		return submit_tx_urb(hdev, urb);

	schedule_work(&bdev->waker);

	usb_free_urb(urb);
	return 0;
}

static int btusb_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct urb *urb = NULL;
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	unsigned int ifnum_base;
#ifdef SUPPORT_HW_DVT
	struct sk_buff *evt_skb;
	uint8_t notify_alt_evt[] = {0x0E, 0x04, 0x01, 0x03, 0x0c, 0x00};
	u16 crBaseAddr = 0, crRegOffset = 0;
	int ret = 0;
#endif

	BT_DBG("%s", hdev->name);

	/* TODO */
	/* btmtk_usb_dispatch_data_bluetooth_kpi(skb->data, skb->len, bt_cb(skb)->pkt_type); */

	if (skb->len <= 0) {
		ret = -EFAULT;
		BTMTK_ERR("%s: target packet length:%zu is not allowed", __func__, (size_t)skb->len);
	}

	ifnum_base = bdev->intf->cur_altsetting->desc.bInterfaceNumber;

	skb_pull(skb, 1);
	BTMTK_DBG_RAW(skb->data, skb->len, "%s, send_frame", __func__);
	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
#ifdef SUPPORT_HW_DVT
		if (skb->len > 7) {
			if (skb->data[0] == 0x6f && skb->data[1] == 0xfc &&
					skb->data[2] == 0x06 && skb->data[3] == 0x01 &&
					skb->data[4] == 0xff && skb->data[5] == 0x03 &&
					skb->data[6] == 0x00 && skb->data[7] == 0x00) {
				/* return evt to upper layered */
				evt_skb = skb_copy(skb, GFP_KERNEL);
				bt_cb(evt_skb)->pkt_type = HCI_EVENT_PKT;
				memcpy(evt_skb->data, &notify_alt_evt, sizeof(notify_alt_evt));
				evt_skb->len = sizeof(notify_alt_evt);
				/* After set alternate setting, we will return evt to boots */
				hci_recv_frame(hdev, evt_skb);
				hdev->conn_hash.sco_num++;
				bdev->sco_num = hdev->conn_hash.sco_num;
				bdev->new_isoc_altsetting = skb->data[8];
				BTMTK_INFO("alt_setting = %d, new_isoc_altsetting_interface = %d\n",
						bdev->new_isoc_altsetting, bdev->new_isoc_altsetting_interface);
				schedule_work(&bdev->work);
				msleep(20);
				return 0;
			} else if (skb->data[0] == 0x6f && skb->data[1] == 0xfc &&
					skb->data[2] == 0x07 && skb->data[3] == 0x01 &&
					skb->data[4] == 0xff && skb->data[5] == 0x03 &&
					skb->data[6] == 0x00 && skb->data[7] == 0x00 && skb->data[9] == 0x00) {
				evt_skb = skb_copy(skb, GFP_KERNEL);
				bt_cb(evt_skb)->pkt_type = HCI_EVENT_PKT;
				memcpy(evt_skb->data, &notify_alt_evt, sizeof(notify_alt_evt));
				evt_skb->len = sizeof(notify_alt_evt);
				/* After set alternate setting, we will return evt to boots */
				hci_recv_frame(hdev, evt_skb);
				/* if sco_num == 0, btusb_work will set alternate setting to zero */
				hdev->conn_hash.sco_num--;
				bdev->sco_num = hdev->conn_hash.sco_num;
				bdev->new_isoc_altsetting_interface = skb->data[8];
				BTMTK_INFO("alt_setting to = %d, new_isoc_altsetting_interface = %d\n",
						bdev->new_isoc_altsetting, bdev->new_isoc_altsetting_interface);
				schedule_work(&bdev->work);
				/* If we don't sleep 50ms, it will failed to set alternate setting to zero */
				msleep(50);
				return 0;
			} else if (skb->data[0] == 0x6f && skb->data[1] == 0xfc &&
					skb->data[2] == 0x09 && skb->data[3] == 0x01 &&
					skb->data[4] == 0xff && skb->data[5] == 0x05 &&
					skb->data[6] == 0x00 && skb->data[7] == 0x01) {
				BTMTK_INFO("read CR skb->data = %02x %02x %02x %02x\n", skb->data[8],
					skb->data[9], skb->data[10], skb->data[11]);
				crBaseAddr = (skb->data[8]<<8) + skb->data[9];
				crRegOffset = (skb->data[10]<<8) + skb->data[11];
				BTMTK_INFO("base + offset = %04x %04x\n", crBaseAddr, crRegOffset);
				memset(bdev->io_buf, 0, USB_IO_BUF_SIZE);
				ret = usb_control_msg(bdev->udev, usb_rcvctrlpipe(bdev->udev, 0),
						1, 0xDE, crBaseAddr, crRegOffset,
						bdev->io_buf, 4, USB_CTRL_IO_TIMO);
				BTMTK_INFO("read CR buf = %02x %02x %02x %02x\n", bdev->io_buf[0],
					bdev->io_buf[1], bdev->io_buf[2], bdev->io_buf[3]);
				return 0;
			} else if (skb->data[0] == 0x6f && skb->data[1] == 0xfc &&
					skb->data[2] == 0x0D && skb->data[3] == 0x01 &&
					skb->data[4] == 0xff && skb->data[5] == 0x09 &&
					skb->data[6] == 0x00 && skb->data[7] == 0x02) {
				crBaseAddr = (skb->data[8] << 8) + skb->data[9];
				crRegOffset = (skb->data[10] << 8) + skb->data[11];
				BTMTK_INFO("base + offset = %04x %04x\n", crBaseAddr, crRegOffset);
				memset(bdev->o_usb_buf, 0, HCI_MAX_COMMAND_SIZE);
				bdev->o_usb_buf[0] = skb->data[12];
				bdev->o_usb_buf[1] = skb->data[13];
				bdev->o_usb_buf[2] = skb->data[14];
				bdev->o_usb_buf[3] = skb->data[15];
				ret = usb_control_msg(bdev->udev, usb_sndctrlpipe(bdev->udev, 0),
						2, 0x5E, crBaseAddr, crRegOffset,
						bdev->o_usb_buf, 4, USB_CTRL_IO_TIMO);
				BTMTK_INFO("write CR buf = %02x %02x %02x %02x\n",
					bdev->o_usb_buf[0], bdev->o_usb_buf[1], bdev->o_usb_buf[2], bdev->o_usb_buf[3]);
				return 0;
			}
		}
#endif

	if (skb->data[0] == 0x6f && skb->data[1] == 0xfc) {
		btusb_submit_wmt_urb(hdev, GFP_KERNEL);
		skb_push(skb, 1);
		skb->data[0] = 0x01;
		btmtk_cif_send_cmd(bdev, skb, 100, 20, BTMTK_EP_TYPE_OUT_CMD, 1);
		BTMTK_DBG_RAW(skb->data, skb->len, "%s, 6ffc send_frame", __func__);
		return 0;
	}

		if (BTMTK_IS_BT_0_INTF(ifnum_base)) {
			if (is_mt7961(bdev->chip_id))
#ifdef BGF0_CMD_BULK
				urb = alloc_bulk_cmd_urb(hdev, skb);
#else
				urb = alloc_ctrl_urb(hdev, skb);
#endif
			else if (is_mt7663(bdev->chip_id))
				urb = alloc_ctrl_urb(hdev, skb);

		} else if (BTMTK_IS_BT_1_INTF(ifnum_base)) {
			if (is_mt7961(bdev->chip_id)) {
#ifdef BGF1_CMD_BULK
				UNUSED(alloc_ctrl_bgf1_urb);
				urb = alloc_bulk_cmd_urb(hdev, skb);
#else
				urb = alloc_ctrl_bgf1_urb(hdev, skb);
#endif
			} else if (is_mt7663(bdev->chip_id))
				urb = alloc_ctrl_urb(hdev, skb);
		}
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.cmd_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_ACLDATA_PKT:
		if (skb->data[0] == 0x00 && skb->data[1] == 0x44)
			urb = alloc_intr_iso_urb(hdev, skb);
		else
			urb = alloc_bulk_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.acl_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_SCODATA_PKT:
		if (hci_conn_num(hdev, SCO_LINK) < 1) {
			BTMTK_INFO("btusb_send_frame hci_conn sco link = %d\n", hci_conn_num(hdev, SCO_LINK));
			/* We need to study how to solve this in hw_dvt case.*/
#ifndef SUPPORT_HW_DVT
			return -ENODEV;
#endif
		}

		urb = alloc_isoc_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.sco_tx++;
		return submit_tx_urb(hdev, urb);
	}

	return -EILSEQ;
}

/* Maybe will be used in the future*/
#if 0
static void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);

	BT_DBG("%s evt %d", hdev->name, evt);

	if (hci_conn_num(hdev, SCO_LINK) != bdev->sco_num) {
		bdev->sco_num = hci_conn_num(hdev, SCO_LINK);
		schedule_work(&bdev->work);
	}
}
#endif

static inline int __set_isoc_interface(struct hci_dev *hdev, int altsetting)
{
	struct btmtk_dev *bdev = hci_get_drvdata(hdev);
	struct usb_interface *intf = bdev->isoc;
	struct usb_endpoint_descriptor *ep_desc;
	int i, err;
	unsigned int ifnum_base;

	if (!bdev->isoc)
		return -ENODEV;

#ifdef SUPPORT_HW_DVT
	ifnum_base = bdev->intf->cur_altsetting->desc.bInterfaceNumber;
	if (BTMTK_IS_BT_0_INTF(ifnum_base))
		bdev->new_isoc_altsetting_interface = 1;
	else if (BTMTK_IS_BT_1_INTF(ifnum_base))
		bdev->new_isoc_altsetting_interface = 4;
	err = usb_set_interface(bdev->udev, bdev->new_isoc_altsetting_interface, altsetting);
#else
	err = usb_set_interface(bdev->udev, 1, altsetting);
#endif
	BTMTK_DBG("setting interface alt = %d, interface = %d", altsetting, bdev->new_isoc_altsetting_interface);
	if (err < 0) {
		BT_ERR("%s setting interface failed (%d)", hdev->name, -err);
		return err;
	}

	bdev->isoc_altsetting = altsetting;

	bdev->isoc_tx_ep = NULL;
	bdev->isoc_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!bdev->isoc_tx_ep && usb_endpoint_is_isoc_out(ep_desc)) {
			bdev->isoc_tx_ep = ep_desc;
			continue;
		}

		if (!bdev->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)) {
			bdev->isoc_rx_ep = ep_desc;
			continue;
		}
	}

	if (!bdev->isoc_tx_ep || !bdev->isoc_rx_ep) {
		BT_ERR("%s invalid SCO descriptors", hdev->name);
		return -ENODEV;
	}

	return 0;
}

static void btusb_work(struct work_struct *work)
{
	struct btmtk_dev *bdev = container_of(work, struct btmtk_dev, work);
	struct hci_dev *hdev = bdev->hdev;
	int new_alts;
	int err;

	if (bdev->sco_num > 0) {
		if (!test_bit(BTUSB_DID_ISO_RESUME, &bdev->flags)) {
			err = usb_autopm_get_interface(bdev->isoc ? bdev->isoc : bdev->intf);
			if (err < 0) {
				clear_bit(BTUSB_ISOC_RUNNING, &bdev->flags);
				usb_kill_anchored_urbs(&bdev->isoc_anchor);
				return;
			}

			set_bit(BTUSB_DID_ISO_RESUME, &bdev->flags);
		}

		if (hdev->voice_setting & 0x0020) {
			static const int alts[3] = { 2, 4, 5 };

			new_alts = alts[bdev->sco_num - 1];
		} else {
			new_alts = bdev->sco_num;
		}
#ifdef SUPPORT_HW_DVT
		new_alts = bdev->new_isoc_altsetting;
#endif

		if (bdev->isoc_altsetting != new_alts) {
			unsigned long flags;

			clear_bit(BTUSB_ISOC_RUNNING, &bdev->flags);
			usb_kill_anchored_urbs(&bdev->isoc_anchor);

			/* When isochronous alternate setting needs to be
			 * changed, because SCO connection has been added
			 * or removed, a packet fragment may be left in the
			 * reassembling state. This could lead to wrongly
			 * assembled fragments.
			 *
			 * Clear outstanding fragment when selecting a new
			 * alternate setting.
			 */
			spin_lock_irqsave(&bdev->rxlock, flags);
			kfree_skb(bdev->sco_skb);
			bdev->sco_skb = NULL;
			spin_unlock_irqrestore(&bdev->rxlock, flags);

			if (__set_isoc_interface(hdev, new_alts) < 0)
				return;
		}

		if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &bdev->flags)) {
			if (btusb_submit_isoc_urb(hdev, GFP_KERNEL) < 0)
				clear_bit(BTUSB_ISOC_RUNNING, &bdev->flags);
			else
				btusb_submit_isoc_urb(hdev, GFP_KERNEL);
		}
	} else {
		clear_bit(BTUSB_ISOC_RUNNING, &bdev->flags);
		usb_kill_anchored_urbs(&bdev->isoc_anchor);

		__set_isoc_interface(hdev, 0);
		if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &bdev->flags))
			usb_autopm_put_interface(bdev->isoc ? bdev->isoc : bdev->intf);
	}
}

static void btusb_waker(struct work_struct *work)
{
	struct btmtk_dev *bdev = container_of(work, struct btmtk_dev, waker);
	int err;

	err = usb_autopm_get_interface(bdev->intf);
	if (err < 0)
		return;

	usb_autopm_put_interface(bdev->intf);
}

static void btusb_reset_waker(struct work_struct *work)
{
	struct btmtk_dev *bdev = container_of(work, struct btmtk_dev, reset_waker);
	int ret;
	int val;

	BT_INFO("%s: Receive a byte (0xFF)", __func__);
	/* read interrupt EP15 CR */

	bdev->interface_state = BTMTK_STATE_FW_DUMP;
	clear_bit(BTUSB_ISOC_RUNNING, &bdev->flags);
	clear_bit(BTUSB_BULK_RUNNING, &bdev->flags);
	clear_bit(BTUSB_INTR_RUNNING, &bdev->flags);

	ret = btmtk_send_deinit_cmds(bdev);
	btusb_stop_traffic(bdev);

	/* read interrupt EP15 CR */
	btmtk_cif_read_uhw_register(bdev, 0x760003A0, &val);

	/* Write Reset CR to 1 */
	btmtk_cif_write_uhw_register(bdev, 0x70002610, 1);
	/* Read reset CR */
	btmtk_cif_read_uhw_register(bdev, 0x70002610, &val);

	btmtk_cif_write_uhw_register(bdev, 0x74000024, 0x000000FF);
	btmtk_cif_read_uhw_register(bdev, 0x74000024, &val);
	btmtk_cif_write_uhw_register(bdev, 0x74000308, 0x000000FF);
	btmtk_cif_read_uhw_register(bdev, 0x74000308, &val);

	/* Write Reset CR to 0 */
	btmtk_cif_write_uhw_register(bdev, 0x70002610, 0);

	/* Read reset CR */
	btmtk_cif_read_uhw_register(bdev, 0x70002610, &val);

	btmtk_cif_write_uhw_register(bdev, 0x74011890, 0x00010001);
	bdev->flavor = (bdev->flavor & 0x00000080) >> 7;
	BTMTK_INFO("%s: flavor1 = 0x%x", __func__, bdev->flavor);

	/* if flavor equals 1, it represent 7920, else it represent 7921 */
	if (bdev->flavor)
		snprintf(bdev->rom_patch_bin_file_name, MAX_BIN_FILE_NAME_LEN, "BT_RAM_CODE_MT%04x_1a_%x_hdr.bin",
				bdev->chip_id & 0xffff, (bdev->fw_version & 0xff) + 1);
	else
		snprintf(bdev->rom_patch_bin_file_name, MAX_BIN_FILE_NAME_LEN, "BT_RAM_CODE_MT%04x_1_%x_hdr.bin",
				bdev->chip_id & 0xffff, (bdev->fw_version & 0xff) + 1);

	btmtk_load_rom_patch(bdev);
}

static int btusb_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct btmtk_dev *bdev = NULL;
	unsigned int ifnum_base;
	int i, err;
	struct usb_interface *tmp_intf;

	ifnum_base = intf->cur_altsetting->desc.bInterfaceNumber;
	BTMTK_DBG("intf %p id %p, interfacenum = %d", intf, id, ifnum_base);

	bdev = usb_get_intfdata(intf);

	if (!bdev)
		return -ENOMEM;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		/* reset_intr_ep must be initialized before intr_ep,
		 * otherwise its address may be the intr_ep address
		 */
		if (!bdev->reset_intr_ep && ep_desc->bEndpointAddress == 0x8f &&
			usb_endpoint_is_int_in(ep_desc)) {
			BT_INFO("intr_reset_rx__ep i = %d  Endpoints 0x%02X, number_of_endpoints=%d",
				i, ep_desc->bEndpointAddress, intf->cur_altsetting->desc.bNumEndpoints);
			bdev->reset_intr_ep = ep_desc;
			continue;
		}

		/* bulk_cmd_tx_ep must be initialized before bulk_tx_ep,
		 * otherwise its address will be the bulk_tx_ep address
		 */
		if (!bdev->bulk_cmd_tx_ep && usb_endpoint_is_bulk_out(ep_desc) &&
			(ep_desc->bEndpointAddress == 0x01 || ep_desc->bEndpointAddress == 0x0b)) {
			bdev->bulk_cmd_tx_ep = ep_desc;
			BT_INFO(" bulk_cmd_tx_ep i = %d  Endpoints 0x%02X, number_of_endpoints=%d",
				i, ep_desc->bEndpointAddress, intf->cur_altsetting->desc.bNumEndpoints);
			continue;
		}

		if (!bdev->intr_ep && usb_endpoint_is_int_in(ep_desc)) {
			bdev->intr_ep = ep_desc;
			BT_INFO("intr_rx_ep i = %d  Endpoints 0x%02X, number_of_endpoints=%d",
				i, ep_desc->bEndpointAddress, intf->cur_altsetting->desc.bNumEndpoints);
			continue;
		}

		if (!bdev->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			bdev->bulk_tx_ep = ep_desc;
			BT_INFO("bulk_tx_ep i = %d  Endpoints 0x%02X, number_of_endpoints=%d",
				i, ep_desc->bEndpointAddress, intf->cur_altsetting->desc.bNumEndpoints);
			continue;
		}

		if (!bdev->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			bdev->bulk_rx_ep = ep_desc;
			BT_INFO("bulk_rx_ep i = %d  Endpoints 0x%02X, number_of_endpoints=%d",
				i, ep_desc->bEndpointAddress, intf->cur_altsetting->desc.bNumEndpoints);
			continue;
		}
	}

	if (!bdev->intr_ep || !bdev->bulk_tx_ep || !bdev->bulk_rx_ep)
		return -ENODEV;

	bdev->cmdreq_type = USB_TYPE_CLASS;
	bdev->cmdreq = 0x00;


	bdev->udev = interface_to_usbdev(intf);
	bdev->intf = intf;

	INIT_WORK(&bdev->work, btusb_work);
	INIT_WORK(&bdev->waker, btusb_waker);
	INIT_WORK(&bdev->reset_waker, btusb_reset_waker);
	init_usb_anchor(&bdev->tx_anchor);
	spin_lock_init(&bdev->txlock);

	init_usb_anchor(&bdev->intr_anchor);
	init_usb_anchor(&bdev->bulk_anchor);
	init_usb_anchor(&bdev->isoc_anchor);
	init_usb_anchor(&bdev->ctrl_anchor);
	spin_lock_init(&bdev->rxlock);

#ifdef SUPPORT_STPBTFWLOG
	/* create stpbtfwlog */
	btusb_init();
	udev_0 = interface_to_usbdev(intf);
	if (udev_0) {
		BTMTK_DBG("udev0 define");
	}
#endif

	btmtk_cif_allocate_memory(bdev);

	btmtk_allocate_hci_device(bdev, HCI_USB);

	SET_HCIDEV_DEV(bdev->hdev, &bdev->intf->dev);

	/* Mediatek load fw rom patch */
	BTMTK_INFO("MTK BT Driver Version : %s", VERSION);

	btmtk_cap_init(bdev);
	if(BTMTK_IS_BT_0_INTF(ifnum_base))
		err = btmtk_load_rom_patch(bdev);
	else
		BTMTK_INFO("interface = %d, don't download patch", ifnum_base);

	/* Interface numbers are hardcoded in the specification */
	if (BTMTK_IS_BT_0_INTF(ifnum_base)) {
		bdev->isoc = usb_ifnum_to_if(bdev->udev, 1);

		BT_INFO("set interface number 2 for iso ");
		bdev->iso_channel = usb_ifnum_to_if(bdev->udev, 2);
		usb_set_interface(bdev->udev, 2, 1);
		tmp_intf = bdev->iso_channel;
		i = 0;
		for (i = 0; i < tmp_intf->cur_altsetting->desc.bNumEndpoints; i++) {
			ep_desc = &tmp_intf->cur_altsetting->endpoint[i].desc;

			if (!bdev->intr_iso_tx_ep && usb_endpoint_is_int_out(ep_desc)) {
				bdev->intr_iso_tx_ep = ep_desc;
				BT_INFO("intr_iso_tx_ep i = %d	Endpoints 0x%02X, number_of_endpoints=%d",
					i, ep_desc->bEndpointAddress, intf->cur_altsetting->desc.bNumEndpoints);
				continue;
			}

			if (!bdev->intr_iso_rx_ep && usb_endpoint_is_int_in(ep_desc)) {
				bdev->intr_iso_rx_ep = ep_desc;
				BT_INFO("intr_iso_rx_ep i = %d	Endpoints 0x%02X, number_of_endpoints=%d",
					i, ep_desc->bEndpointAddress, intf->cur_altsetting->desc.bNumEndpoints);
				continue;
			}
		}
	} else if (BTMTK_IS_BT_1_INTF(ifnum_base)) {
		BT_INFO("interface number = 3, set interface number 4");
		bdev->isoc = usb_ifnum_to_if(bdev->udev, 4);
	}

	if (bdev->isoc) {
		err = usb_driver_claim_interface(&btusb_driver,
						 bdev->isoc, bdev);
		if (err < 0) {
			btmtk_free_hci_device(bdev, HCI_USB);
			btmtk_cif_free_memory(bdev);
			return err;
		}
	}

	usb_set_intfdata(intf, bdev);
	btmtk_register_hci_device(bdev);

	memset(bdev->bdaddr, 0, BD_ADDRESS_SIZE);
	//if (is_support_unify_woble(bdev)) {
	if (is_mt7961(bdev->chip_id)) {
		btmtk_load_woble_setting(bdev->woble_setting_file_name,
			&bdev->udev->dev,
			&bdev->woble_setting_len,
			bdev);
	}
	return 0;
}

static void btusb_disconnect(struct usb_interface *intf)
{
	struct btmtk_dev *bdev = usb_get_intfdata(intf);
	struct hci_dev *hdev;

	BT_DBG("intf %p", intf);

	if (!bdev)
		return;

	/* TODO */
	/* Need to check BTMTK_FOPS_STATE_OPENED, to notify restart */
	if (bdev->subsys_reset == HW_ERR_NONE) {
		bdev->subsys_reset = HW_ERR_CODE_USB_DISC;
	}

	hdev = bdev->hdev;
	usb_set_intfdata(bdev->intf, NULL);

	if (bdev->isoc)
		usb_set_intfdata(bdev->isoc, NULL);

	if (intf == bdev->intf) {
		if (bdev->isoc)
			usb_driver_release_interface(&btusb_driver, bdev->isoc);
	} else if (intf == bdev->isoc)
		usb_driver_release_interface(&btusb_driver, bdev->intf);

	btmtk_free_hci_device(bdev, HCI_USB);

	bdev->power_state = BTMTK_DONGLE_STATE_POWER_OFF;
	btmtk_cif_free_memory(bdev);
#ifdef SUPPORT_STPBTFWLOG
	btusb_exit();
#endif

	btmtk_release_dev(bdev);
}

#ifdef CONFIG_PM
static int btusb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct btmtk_dev *bdev = usb_get_intfdata(intf);
	int ret = -1;

	BT_DBG("intf %p", intf);

	if (bdev->suspend_count++)
		return 0;

	/* TODO */
	/* Need to check BTMTK_FOPS_STATE_OPENED
	 * Send Power_On cmd if not opened */

	spin_lock_irq(&bdev->txlock);
	if (!(PMSG_IS_AUTO(message) && bdev->tx_in_flight)) {
		set_bit(BTUSB_SUSPENDING, &bdev->flags);
		spin_unlock_irq(&bdev->txlock);
	} else {
		spin_unlock_irq(&bdev->txlock);
		bdev->suspend_count--;
		return -EBUSY;
	}

	cancel_work_sync(&bdev->work);

	btusb_stop_traffic(bdev);
	usb_kill_anchored_urbs(&bdev->tx_anchor);

	ret = btmtk_woble_wake_up(bdev);
	if (ret)
		BTMTK_ERR("%s: btmtk_usb_handle_entering_WoBLE_state return fail  %d", __func__, ret);

	BTMTK_INFO("%s END", __func__);

	return 0;
}

static int btusb_resume(struct usb_interface *intf)
{
	struct btmtk_dev *bdev = usb_get_intfdata(intf);
	struct hci_dev *hdev = bdev->hdev;
	int err = 0;

	BTMTK_INFO("%s begin", __func__);

	if (--bdev->suspend_count)
		return 0;

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (test_bit(BTUSB_INTR_RUNNING, &bdev->flags)) {
		err = btusb_submit_intr_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_INTR_RUNNING, &bdev->flags);
			goto done;
		}
	}

	if (test_bit(BTUSB_BULK_RUNNING, &bdev->flags)) {
		err = btusb_submit_bulk_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_BULK_RUNNING, &bdev->flags);
			goto done;
		}

		btusb_submit_bulk_urb(hdev, GFP_NOIO);
	}

	if (test_bit(BTUSB_ISOC_RUNNING, &bdev->flags)) {
		if (btusb_submit_isoc_urb(hdev, GFP_NOIO) < 0)
			clear_bit(BTUSB_ISOC_RUNNING, &bdev->flags);
		else
			btusb_submit_isoc_urb(hdev, GFP_NOIO);
	}

	spin_lock_irq(&bdev->txlock);
	clear_bit(BTUSB_SUSPENDING, &bdev->flags);
	spin_unlock_irq(&bdev->txlock);
	schedule_work(&bdev->work);

	err = btmtk_handle_leaving_WoBLE_state(bdev);
	if (err)
		BTMTK_ERR("%s: btmtk_handle_leaving_WoBLE_state return fail  %d", __func__, err);

	BTMTK_INFO("%s end", __func__);

	return 0;

done:
	spin_lock_irq(&bdev->txlock);
	clear_bit(BTUSB_SUSPENDING, &bdev->flags);
	spin_unlock_irq(&bdev->txlock);

	return err;
}
#endif

static int btmtk_cif_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	int ret = -1;
	int cif_event = 0;
	unsigned int ifnum_base;
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_dev *bdev = NULL;

	/* USB interface only.
	 * USB will need to identify thru descriptor's interface numbering.
	 */
	ifnum_base = intf->cur_altsetting->desc.bInterfaceNumber;
	BTMTK_DBG("intf %p id %p, interfacenum = %d", intf, id, ifnum_base);

	/* interface numbers are hardcoded in the spec */
	if (ifnum_base != BT0_MCU_INTERFACE_NUM && \
		ifnum_base != BT1_MCU_INTERFACE_NUM)
		return -ENODEV;

	/* Retrieve priv data and set to interface structure */
	bdev = btmtk_get_dev();
	usb_set_intfdata(intf, bdev);

	/* Retrieve current HIF event state */
	cif_event = HIF_EVENT_PROBE;
	if (BTMTK_CIF_IS_NULL(bdev, cif_event)) {
		/* Error */
		BTMTK_WARN("%s intf[%d] priv setting is NULL", __func__, ifnum_base);
		return -ENODEV;
	}

	BTMTK_CIF_ASSIGN_STATE(bdev, cif_event, cif_state);

	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

	/* Do HIF events */
	ret = btusb_probe(intf, id);

	/* Set End/Error state */
	if (ret == 0) {
		btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
	}
	else {
		btmtk_set_chip_state((void *)bdev, cif_state->ops_error);
	}

	return ret;
}

static void btmtk_cif_disconnect(struct usb_interface *intf)
{
	int cif_event = 0;
	unsigned int ifnum_base;
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_dev *bdev = NULL;

	BTMTK_CIF_GET_DEV_PRIV(bdev, intf, ifnum_base);

	/* Retrieve current HIF event state */
	cif_event = HIF_EVENT_DISCONNECT;
	if (BTMTK_CIF_IS_NULL(bdev, cif_event)) {
		/* Error */
		BTMTK_WARN("%s intf[%d] priv setting is NULL", __func__, ifnum_base);
		return;
	}

	BTMTK_CIF_ASSIGN_STATE(bdev, cif_event, cif_state);

	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

	/* Do HIF events */
	btusb_disconnect(intf);

	/* Set End/Error state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
}

#ifdef CONFIG_PM
static int btmtk_cif_suspend(struct usb_interface *intf, pm_message_t message)
{
	int ret = -1;
	unsigned int ifnum_base;
	int cif_event = 0;
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_dev *bdev = NULL;

	BTMTK_CIF_GET_DEV_PRIV(bdev, intf, ifnum_base);

	/* Retrieve current HIF event state */
	cif_event = HIF_EVENT_SUSPEND;
	if (BTMTK_CIF_IS_NULL(bdev, cif_event)) {
		/* Error */
		BTMTK_WARN("%s intf[%d] priv setting is NULL", __func__, ifnum_base);
		return -ENODEV;
	}

	BTMTK_CIF_ASSIGN_STATE(bdev, cif_event, cif_state);

	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

	/* Do HIF events */
	ret = btusb_suspend(intf, message);

	/* Set End/Error state */
	if (ret == 0) {
		btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
	}
	else {
		btmtk_set_chip_state((void *)bdev, cif_state->ops_error);
	}

	return ret;
}

static int btmtk_cif_resume(struct usb_interface *intf)
{
	int ret = -1;
	unsigned int ifnum_base;
	int cif_event = 0;
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_dev *bdev = NULL;

	BTMTK_CIF_GET_DEV_PRIV(bdev, intf, ifnum_base);

	/* Retrieve current HIF event state */
	cif_event = HIF_EVENT_RESUME;
	if (BTMTK_CIF_IS_NULL(bdev, cif_event)) {
		/* Error */
		BTMTK_WARN("%s intf[%d] priv setting is NULL", __func__, ifnum_base);
		return -ENODEV;
	}

	BTMTK_CIF_ASSIGN_STATE(bdev, cif_event, cif_state);

	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

	/* Do HIF events */
	ret = btusb_resume(intf);

	/* Set End/Error state */
	if (ret == 0) {
		btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
	}
	else {
		btmtk_set_chip_state((void *)bdev, cif_state->ops_error);
	}

	return ret;
}
#endif	// CONFIG_PM //

static struct usb_driver btusb_driver = {
	.name		= "btusb",
	.probe		= btmtk_cif_probe,
	.disconnect	= btmtk_cif_disconnect,
#ifdef CONFIG_PM
	.suspend	= btmtk_cif_suspend,
	.resume		= btmtk_cif_resume,
#endif
	.id_table	= btusb_table,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

int btmtk_cif_register(void)
{
	int retval = 0;

	BTMTK_INFO("%s", __func__);
	retval = usb_register(&btusb_driver);
	if (retval)
		BTMTK_ERR("*** USB registration fail(%d)! ***", retval);
	else
		BTMTK_INFO("%s, usb registration success!", __func__);
	return retval;
}

int btmtk_cif_deregister(void)
{
	BT_INFO("%s", __func__);
	usb_deregister(&btusb_driver);
	BT_INFO("%s: Done", __func__);
	return 0;
}

static int btmtk_cif_allocate_memory(struct btmtk_dev *bdev)
{
	BT_INFO("%s", __func__);

	if (bdev->rom_patch_bin_file_name == NULL) {
		bdev->rom_patch_bin_file_name = kzalloc(MAX_BIN_FILE_NAME_LEN, GFP_KERNEL);
		if (!bdev->rom_patch_bin_file_name) {
			BT_ERR("%s: alloc memory fail (bdev->rom_patch_bin_file_name)", __func__);
			return -1;
		}
	}

	if (bdev->io_buf == NULL) {
		bdev->io_buf = kzalloc(USB_IO_BUF_SIZE, GFP_KERNEL);
		if (!bdev->io_buf) {
			BT_ERR("%s: alloc memory fail (bdev->io_buf)", __func__);
			return -1;
		}
	}

	if (bdev->woble_setting_file_name == NULL) {
		bdev->woble_setting_file_name = kzalloc(MAX_BIN_FILE_NAME_LEN, GFP_KERNEL);
		if (!bdev->woble_setting_file_name) {
			BT_ERR("%s: alloc memory fail (g_data->woble_setting_file_name)", __func__);
			return -1;
		}
	}

	if (bdev->o_usb_buf == NULL) {
		bdev->o_usb_buf = kzalloc(HCI_MAX_COMMAND_SIZE, GFP_KERNEL);
		if (!bdev->o_usb_buf) {
			BT_ERR("%s: alloc memory fail (bdev->o_usb_buf)", __func__);
			return -1;
		}
	}

	if (bdev->urb_transfer_buf == NULL) {
		bdev->urb_transfer_buf = kzalloc(URB_MAX_BUFFER_SIZE, GFP_KERNEL);
		if (!bdev->urb_transfer_buf) {
			BT_ERR("%s: alloc memory fail (bdev->urb_transfer_buf)", __func__);
			return -1;
		}
	}

	BT_INFO("%s: Done", __func__);
	return 0;
}

static void btmtk_cif_free_memory(struct btmtk_dev *bdev)
{
	if (!bdev) {
		BT_ERR("%s: bdev is NULL!", __func__);
		return;
	}

	kfree(bdev->rom_patch_bin_file_name);
	bdev->rom_patch_bin_file_name = NULL;

	kfree(bdev->woble_setting_file_name);
	bdev->woble_setting_file_name = NULL;

	kfree(bdev->io_buf);
	bdev->io_buf = NULL;

	kfree(bdev->o_usb_buf);
	bdev->o_usb_buf = NULL;

	kfree(bdev->urb_transfer_buf);
	bdev->urb_transfer_buf = NULL;

	BT_INFO("%s: Success", __func__);
}

int btmtk_cif_write_uhw_register(struct btmtk_dev *bdev, u32 reg, u32 val)
{
	int ret = -1;
	__le16 reg_high;
	__le16 reg_low;
	u8 reset_buf[4];

	reg_high = ((reg >> 16) & 0xffff);
	reg_low = (reg & 0xffff);

	reset_buf[0] = (val & 0x00ff);
	reset_buf[1] = ((val >> 8) & 0x00ff);
	reset_buf[2] = ((val >> 16) & 0x00ff);
	reset_buf[3] = ((val >> 24) & 0x00ff);

	memset(bdev->io_buf, 0, USB_IO_BUF_SIZE);
	ret = usb_control_msg(bdev->udev, usb_sndctrlpipe(bdev->udev, 0),
			0x02,						/* bRequest */
			0x5E,						/* bRequestType */
			reg_high,					/* wValue */
			reg_low,					/* wIndex */
			reset_buf,
			4, USB_CTRL_IO_TIMO);

	BTMTK_DBG("%s: high=%x, reg_low=%x, val=%x", __func__, reg_high, reg_low, val);
	BTMTK_DBG("%s: reset_buf = %x %x %x %x", __func__, reset_buf[3], reset_buf[2], reset_buf[1], reset_buf[0]);

	if (ret < 0) {
		val = 0xffffffff;
		BT_ERR("%s: error(%d), reg=%x, value=%x", __func__, ret, reg, val);
		return ret;
	}
	return 0;
}

int btmtk_cif_read_uhw_register(struct btmtk_dev *bdev, u32 reg, u32 *val)
{
	int ret = -1;
	__le16 reg_high;
	__le16 reg_low;

	reg_high = ((reg >> 16) & 0xffff);
	reg_low = (reg & 0xffff);

	memset(bdev->io_buf, 0, USB_IO_BUF_SIZE);
	ret = usb_control_msg(bdev->udev, usb_rcvctrlpipe(bdev->udev, 0),
			0x01,						/* bRequest */
			0xDE,						/* bRequestType */
			reg_high,					/* wValue */
			reg_low,					/* wIndex */
			bdev->io_buf,
			4, USB_CTRL_IO_TIMO);

	BTMTK_DBG("%s: reg=%x, value=%x", __func__, reg, *bdev->io_buf);

	if (ret < 0) {
		*val = 0xffffffff;
		BT_ERR("%s: error(%d), reg=%x, value=%x", __func__, ret, reg, *val);
		return ret;
	}

	memmove(val, bdev->io_buf, sizeof(u32));
	*val = le32_to_cpu(*val);

	return 0;
}

int btmtk_cif_read_register(struct btmtk_dev *bdev, u32 reg, u32 *val)
{
	int ret = -1;
	__le16 reg_high;
	__le16 reg_low;

	reg_high = ((reg >> 16) & 0xffff);
	reg_low = (reg & 0xffff);

	memset(bdev->io_buf, 0, USB_IO_BUF_SIZE);
	ret = usb_control_msg(bdev->udev, usb_rcvctrlpipe(bdev->udev, 0),
			0x63,						/* bRequest */
			DEVICE_VENDOR_REQUEST_IN,	/* bRequestType */
			reg_high,					/* wValue */
			reg_low,					/* wIndex */
			bdev->io_buf,
			sizeof(u32), USB_CTRL_IO_TIMO);

	if (ret < 0) {
		*val = 0xffffffff;
		BT_ERR("%s: error(%d), reg=%x, value=%x", __func__, ret, reg, *val);
		return ret;
	}

	memmove(val, bdev->io_buf, sizeof(u32));
	*val = le32_to_cpu(*val);

	return 0;
}

int btmtk_cif_write_register(struct btmtk_dev *bdev, u32 reg, u32 val)
{
	int ret = -1;
	__le16 reg_high;
	__le16 reg_low;
	u8 buf[4];

	reg_high = ((reg >> 16) & 0xffff);
	reg_low = (reg & 0xffff);

	memset(bdev->io_buf, 0, USB_IO_BUF_SIZE);

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = (val & 0x00ff);
	buf[3] = ((val >> 8) & 0x00ff);

	ret = usb_control_msg(bdev->udev, usb_sndctrlpipe(bdev->udev, 0),
			0x66,						/* bRequest */
			0x40,	/* bRequestType */
			reg_high,					/* wValue */
			reg_low,					/* wIndex */
			buf,
			4, USB_CTRL_IO_TIMO);

	BTMTK_DBG("%s: buf = %x %x %x %x", __func__, buf[3], buf[2], buf[1], buf[0]);

	if (ret < 0) {
		val = 0xffffffff;
		BT_ERR("%s: error(%d), reg=%x, value=%x", __func__, ret, reg, val);
		return ret;
	}

	return 0;
}


int btmtk_cif_send_calibration(struct btmtk_dev *bdev)
{
	return 0;
}

static void btmtk_cif_load_rom_patch_complete(struct urb *urb)
{
	struct completion *sent_to_mcu_done = (struct completion *)urb->context;

	complete(sent_to_mcu_done);
}

int btmtk_cif_send_control_out(struct btmtk_dev *bdev, const uint8_t *cmd,
		const int cmd_len, int delay, int retry, bool wmt_cmd)
{
	int ret = 0;
	unsigned int ifnum_base;

	if (bdev == NULL || bdev->udev == NULL || bdev->hdev == NULL || bdev->io_buf == NULL ||
		bdev->o_usb_buf == NULL || cmd == NULL ||
		cmd_len > HCI_MAX_COMMAND_SIZE || cmd_len <= 0) {
		BT_ERR("%s: incorrect cmd pointer", __func__);
		ret = -1;
		goto exit;
	}

	if (bdev->power_state == BTMTK_DONGLE_STATE_POWER_OFF) {
		BT_ERR("%s: chip power isn't on, ignore this command, state is %d",
		__func__, bdev->power_state);
		ret = -1;
		goto exit;
	}

	ifnum_base = bdev->intf->cur_altsetting->desc.bInterfaceNumber;

	/* send hci or wmt command */
	memcpy(bdev->o_usb_buf, cmd, cmd_len);
	BTMTK_INFO_RAW(cmd, cmd_len, "%s: cmd:", __func__);
	if (BTMTK_IS_BT_0_INTF(ifnum_base))
		ret = usb_control_msg(bdev->udev, usb_sndctrlpipe(bdev->udev, 0),
				0x01, DEVICE_CLASS_REQUEST_OUT, 0x30, 0x00, (void *)bdev->o_usb_buf,
				cmd_len, USB_CTRL_IO_TIMO);
	else if (BTMTK_IS_BT_1_INTF(ifnum_base))
		ret = usb_control_msg(bdev->udev, usb_sndctrlpipe(bdev->udev, 0),
				0x00, 0x21, 0x00, 0x03, (void *)bdev->o_usb_buf, cmd_len, USB_CTRL_IO_TIMO);

	if (ret < 0)
		BT_ERR("%s: command send failed(%d)", __func__, ret);

exit:
	return ret;
}

int btmtk_cif_send_bulk_out(struct btmtk_dev *bdev, const uint8_t *cmd,
		const int cmd_len)
{
	int ret = 0;
	struct urb *urb;
	unsigned int pipe;
	struct completion sent_to_mcu_done;
	void *buf;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		ret = -ENOMEM;
		goto exit;
	}

	/* why need to alloc dma buffer??*/
	buf = usb_alloc_coherent(bdev->udev, UPLOAD_PATCH_UNIT, GFP_KERNEL, &urb->transfer_dma);
	if (!buf) {
		ret = -ENOMEM;
		goto exit;
	}
	init_completion(&sent_to_mcu_done);

	pipe = usb_sndbulkpipe(bdev->udev, bdev->bulk_tx_ep->bEndpointAddress);

	memcpy(buf, cmd, cmd_len);
	usb_fill_bulk_urb(urb,
			bdev->udev,
			pipe,
			buf,
			cmd_len,
			(usb_complete_t)btmtk_cif_load_rom_patch_complete,
			&sent_to_mcu_done);

	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		BT_ERR("%s: submit urb failed (%d)", __func__, ret);
		goto error;
	}

	if (!wait_for_completion_timeout
			(&sent_to_mcu_done, msecs_to_jiffies(1000))) {
		usb_kill_urb(urb);
		BT_ERR("%s: upload rom_patch timeout", __func__);
		ret = -ETIME;
	}

error:
	usb_free_coherent(bdev->udev, UPLOAD_PATCH_UNIT, buf, urb->transfer_dma);
exit:
	usb_free_urb(urb);
	return ret;

}

int btmtk_cif_send_cmd(struct btmtk_dev *bdev, struct sk_buff *skb,
		int delay, int retry, int endpoint, bool wmt_cmd)
{
	if (endpoint == BTMTK_EP_TYPE_OUT_CMD) {
		/* handle hci cmd and wmt cmd from driver */
		return btmtk_cif_send_control_out(bdev, skb->data + 1, skb->len - 1,
				delay, retry, wmt_cmd);
	} else if (endpoint == BTMTK_EP_TPYE_OUT_ACL) {
		/* bulk out for load rom patch*/
		return btmtk_cif_send_bulk_out(bdev, skb->data + 1, skb->len - 1);
	} else if (endpoint == BTMTK_EP_TYPE_OUT_OTHER) {
		/* handle hci cmd and acl pkt from host */
		return btusb_send_frame(bdev->hdev, skb);
	}
	return 0;
}

int btmtk_cif_recv_evt(struct btmtk_dev *bdev, int delay, int retry)
{
	int ret = -1;	/* if successful, 0 */
	unsigned int ifnum_base;

	if (bdev == NULL) {
		BT_ERR("%s: bdev == NULL!\n", __func__);
		return ret;
	}

	if (!bdev->udev || !bdev->hdev) {
		BT_ERR("%s: invalid parameters!\n", __func__);
		return ret;
	}

	ifnum_base = bdev->intf->cur_altsetting->desc.bInterfaceNumber;
get_response_again:
	/* ms delay */
	mdelay(delay);

	/* check WMT event */
	memset(bdev->io_buf, 0, USB_IO_BUF_SIZE);
	if (BTMTK_IS_BT_0_INTF(ifnum_base))
		ret = usb_control_msg(bdev->udev, usb_rcvctrlpipe(bdev->udev, 0),
				0x01, DEVICE_VENDOR_REQUEST_IN, 0x30, 0x00, bdev->io_buf,
				HCI_USB_IO_BUF_SIZE, USB_CTRL_IO_TIMO);
	else if (BTMTK_IS_BT_1_INTF(ifnum_base))
		ret = usb_control_msg(bdev->udev, usb_rcvctrlpipe(bdev->udev, 0),
				0x01, 0xA1, 0x30, 0x03, bdev->io_buf, HCI_USB_IO_BUF_SIZE,
				USB_CTRL_IO_TIMO);

	if (ret < 0) {
		BT_ERR("%s: event get failed(%d)", __func__, ret);
		return ret;
	}

	if (ret > 0) {
		BTMTK_INFO_RAW(bdev->io_buf, ret, "%s OK: EVT:", __func__);
		return ret; /* return read length */
	} else if (retry > 0) {
		BTMTK_WARN("%s: Trying to get response... (%d)", __func__, ret);
		retry--;
		goto get_response_again;
	} else
		BT_ERR("%s NG: do not got response:(%d)", __func__, ret);

	return -1;
}

int btmtk_cif_open(struct hci_dev *hdev)
{
	BT_INFO("%s enter!", __func__);
	return btusb_open(hdev);
}
int btmtk_cif_close(struct hci_dev *hdev)
{
	BT_INFO("%s enter!", __func__);
	return btusb_close(hdev);
}

