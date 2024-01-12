/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */
/******************************************************************************
 *
 * This file is provided under a dual license.  When you use or
 * distribute this software, you may choose to be licensed under
 * version 2 of the GNU General Public License ("GPLv2 License")
 * or BSD License.
 *
 * GPLv2 License
 *
 * Copyright(C) 2019 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 *
 * BSD LICENSE
 *
 * Copyright(C) 2019 MediaTek Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <command.h>
#include <common.h>
#include <MsDebug.h>
#include <usb.h>
#include <stdio.h>
#include <LD_usbbt.h>
#include <LD_btmtk_usb.h>
#include <utility.h>

#define MAX_ROOT_PORTS 8
#include <string.h>
#include <errno.h>

#include <stdlib.h>
#include <fs.h>
#include <dm.h>
#include <dm/uclass-internal.h>

usb_vid_pid array_mtk_vid_pid[] = {
	{0x0E8D, 0x7668, "MTK7668"},	// 7668
	{0x0E8D, 0x76A0, "MTK7662T"},	// 7662T
	{0x0E8D, 0x76A1, "MTK7632T"},	// 7632T
	{0x0E8D, 0x7663, "MTK7663"},	//7663
	{0x0E8D, 0x7961, "MTK7961"},	//7961
	{0x0E8D, 0x7902, "MTK7902"},	//7902
	{0x0E8D, 0x6639, "MTK6639"},	//6639
};

int max_mtk_wifi_id = (sizeof(array_mtk_vid_pid) / sizeof(array_mtk_vid_pid[0]));
usb_vid_pid *pmtk_wifi = &array_mtk_vid_pid[0];

static mtkbt_dev_t *g_DrvData = NULL;

VOID *os_memcpy(VOID *dst, const VOID *src, UINT32 len)
{
	return memcpy(dst, src, len);
}

VOID *os_memmove(VOID *dest, const void *src,UINT32 len)
{
	return memmove(dest, src, len);
}

VOID *os_memset(VOID *s, int c, size_t n)
{
	return memset(s,c,n);
}

VOID *os_kzalloc(size_t size, unsigned int flags)
{
	VOID *ptr = malloc(size);
	if (ptr == NULL) {
		usb_debug("malloc is fail, ptr is %p\n", ptr);
		return ptr;
	}

	os_memset(ptr, 0, size);
	return ptr;
}

int LD_load_code(unsigned char **image, char *partition, char *file, mtkbt_dev_t *dev, u32 *code_len)
{
	int ret = 0;
	loff_t actread_size = 0;
	char device_name[8] = {0};
	unsigned int device = 0;
	char storage_info[8] = {0};
	u32 alloc_len = 0;

	usb_debug("partition: %s\n", partition);
	usb_debug("file pathname: %s\n", file);

	ret = sys_get_boot_device(device_name, sizeof(device_name));
	if(ret < 0) {
		usb_debug("Error: Get booting device name failure, Unknown storage device.\n");
		return -1;
	}

	ret = sys_get_storage_info(device, partition, storage_info);
	if(ret < 0) {
		usb_debug("Error: sys_get_storage_info failure\n");
		return -1;
	}

	if (fs_set_blk_dev(device_name, storage_info, FS_TYPE_EXT)) {
		usb_debug("Error: partition select failure\n");
		return -1;
	}

	ret = fs_size(file, (loff_t *)code_len);
	if (ret < 0) {
		usb_debug("Get file(%s) size fail\n", file);
		return -1;
	}
	usb_debug("file_size: %d\n", *code_len);

	alloc_len = (*code_len / 4096 + 1) * 4096;
	// malloc buffer to store bt patch file data
	*image = malloc(alloc_len);
	if (*image == NULL) {
		usb_debug("malloc fail\n");
		*code_len = 0;
		return -1;
	}

	ret = sys_get_storage_info(device, partition, storage_info);
	if(ret < 0) {
		usb_debug("Error: sys_get_storage_info failure\n");
		return -1;
	}

	if (fs_set_blk_dev(device_name, storage_info, FS_TYPE_EXT)) {
		usb_debug("Error: partition select failure!\n");
		goto err;
	}

	ret = fs_read(file, (unsigned long)*image, 0, 0, &actread_size);
	if(ret < 0) {
		usb_debug("fs_read %s fail\n", file);
		usb_debug("%s: fs_read fail errno(%d)\n", __func__, errno);
		goto err;
	}
	usb_debug("fs_read ok, file_size:%d, actread_size:%lld\n", *code_len, actread_size);

	if (actread_size > *code_len || actread_size < 0) {
		usb_debug("actread_size(%lld) is error, fs_read fail\n", actread_size);
		goto err;
	}

	UBOOT_DEBUG("Load file(%s:%s) OK\n", partition, file);
	UBOOT_DUMP((unsigned long)*image, 0x200);
	return 0;

err:
	free(*image);
	*image = NULL;
	*code_len = 0;
	return -1;
}

void LD_load_code_from_bin(unsigned char **image, char *bin_name, char *path, mtkbt_dev_t *dev, u32 *code_len)
{
	#define ENV_BT_FW_PATH	   "BTFWBinPath"

	char mtk_patch_bin_patch[128] = "\0";
	char *bt_env;
	char *partition[] = {"bootdata", "tvconfig", "vendor", "userdata", "system", "APP"};
	int i = 0;

	/** implement by mstar/MTK
	* path: /system/etc/firmware/mt76XX_patch_eX_hdr.bin
	* If argument "path" is NULL, access "/etc/firmware" directly like as request_firmware
	* if argument "path" is not NULL, so far only support directory "userdata"
	*	 NOTE: latest vfs_mount seems decided this time access directory
	*/
	if (path) {
		(void)snprintf(mtk_patch_bin_patch, sizeof(mtk_patch_bin_patch), "%s/%s", path, bin_name);
		printf("File: %s\n", mtk_patch_bin_patch);
	} else {
		(void)snprintf(mtk_patch_bin_patch, sizeof(mtk_patch_bin_patch), "%s/%s", "/firmware", bin_name);
		printf("mtk_patch_bin_patch: %s\n", mtk_patch_bin_patch);
	}

	bt_env = env_get(ENV_BT_FW_PATH);
	if (bt_env == NULL) {
		/* get PATH failed */
		printf("bt_env is NULL\n");
		printf("sizeof(partition) / sizeof(char *) = %lu\n", sizeof(partition) / sizeof(char *));
		for (i = 0; i < sizeof(partition) / sizeof(char *); i++) {
			if (LD_load_code(image, partition[i], mtk_patch_bin_patch, dev, code_len) == 0)
				return;
		}
	} else {
		printf("bt_env: %s\n", bt_env);
		LD_load_code(image, bt_env, mtk_patch_bin_patch, dev, code_len);
	}

	return;

}

static int usb_bt_bulk_msg(
		mtkbt_dev_t *dev,
		u32 epType,
		u8 *data,
		int size,
		int* realsize,
		int timeout /* not used */
)
{
	int ret =0 ;
	if(dev == NULL || dev->udev == NULL || dev->bulk_tx_ep ==  NULL) {
		usb_debug("bulk out error 00\n");
		return -1;
	}

	if(epType == MTKBT_BULK_TX_EP) {
//		usb_debug_raw(data, size, "%s: usb_bulk_msg:", __func__);
		ret = usb_bulk_msg(dev->udev,usb_sndbulkpipe(dev->udev,dev->bulk_tx_ep->bEndpointAddress),data,size,realsize,2000);
		if(ret) {
			usb_debug("bulk out error 01, ret = %d\n", ret);
			return -1;
		}

		if(*realsize == size) {
			//usb_debug("bulk out success 01,size =0x%x\n",size);
			return 0;
		} else {
			usb_debug("bulk out fail 02,size =0x%x,realsize =0x%x\n",size,*realsize);
		}
	}
	return -1;
}

static int usb_bt_control_msg(
		mtkbt_dev_t *dev,
		u32 epType,
		u8 request,
		u8 requesttype,
		u16 value,
		u16 index,
		u8 *data,
		int data_length,
		int timeout  /* not used */
)
{
	int ret = -1;

	if(epType == MTKBT_CTRL_TX_EP) {
//		usb_debug_raw(data, data_length, "%s: usb_control_msg:", __func__);
		ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), request,
				requesttype, value, index, data, data_length,timeout);
	} else if (epType == MTKBT_CTRL_RX_EP) {
		ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0), request,
				requesttype, value, index, data, data_length,timeout);
	} else {
		usb_debug("control message wrong Type =0x%x\n",epType);
	}

	if (ret < 0) {
		usb_debug("Err1(%d)\n", ret);
		return ret;
	}
	return ret;
}

static int usb_bt_interrupt_msg(
		mtkbt_dev_t *dev,
		u32 epType,
		u8 *data,
		int size,
		int* realsize,
		int timeout  /* unit of 1ms */
)
{
	int ret = -1;

	usb_debug("epType = 0x%x\n",epType);

	if(epType == MTKBT_INTR_EP) {
		//ret = usb_submit_int_msg(dev->udev,usb_rcvintpipe(dev->udev,dev->intr_ep->bEndpointAddress),data,size,realsize,timeout);
		ret = usb_submit_int_msg(dev->udev,usb_rcvintpipe(dev->udev,dev->intr_ep->bEndpointAddress),data,size,timeout);
	}

	if(ret < 0 ) {
		usb_debug("Err1(%d)\n", ret);
		return ret;
	}
	usb_debug("ret = 0x%x\n",ret);
	return ret;
}

static HC_IF usbbt_host_interface =
{
		usb_bt_bulk_msg,
		usb_bt_control_msg,
		usb_bt_interrupt_msg,
};

static void Ldbtusb_diconnect (btusbdev_t *dev)
{
	LD_btmtk_usb_disconnect(g_DrvData);

	if(g_DrvData) {
		os_kfree(g_DrvData);
	}
	g_DrvData = NULL;
}

static int Ldbtusb_SetWoble(btusbdev_t *dev)
{
	if(!g_DrvData) {
		usb_debug("usb set woble fail ,because no drv data\n");
		return -1;
	} else {
		LD_btmtk_usb_SetWoble(g_DrvData);
		usb_debug("usb set woble end\n");
	}
	return 0;
}

static u32 chipid;

int Ldbtusb_connect (btusbdev_t *dev, int flag)
{
	int ret = 0;

	usb_debug("%s begin\n", __func__);
	// For Mstar
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_interface *iface;
	int i;
	iface = &dev->config.if_desc[0];

	if(g_DrvData == NULL) {
		g_DrvData = os_kmalloc(sizeof(mtkbt_dev_t),MTK_GFP_ATOMIC);

		if (!g_DrvData) {
			usb_debug("Not enough memory for mtkbt virtual usb device.\n");
			return -1;
		} else {
			os_memset(g_DrvData,0,sizeof(mtkbt_dev_t));
			g_DrvData->udev = dev;
			g_DrvData->connect = Ldbtusb_connect;
			g_DrvData->disconnect = Ldbtusb_diconnect;
			g_DrvData->SetWoble = Ldbtusb_SetWoble;
		}
	} else {
			return -1;
	}

	// For Mstar
	for (i = 0; i < iface->desc.bNumEndpoints; i++) {
		ep_desc = &iface->ep_desc[i];
		usb_debug("dev->endpoints[%d].bmAttributes = 0x%x\n", i, ep_desc->bmAttributes);
		usb_debug("dev->endpoints[%d].bEndpointAddress = 0x%x\n", i, ep_desc->bEndpointAddress);

		if ((ep_desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) {
			if (ep_desc->bEndpointAddress & USB_DIR_IN) {
				usb_debug("set endpoints[%d] to bulk_rx_ep\n", i);
				g_DrvData->bulk_rx_ep = ep_desc;
			} else {
				if (ep_desc->bEndpointAddress != 0x1) {
					usb_debug("set endpoints[%d] to bulk_tx_ep\n", i);
					g_DrvData->bulk_tx_ep = ep_desc;
				}
			}
			continue;
		}

		/* is it an interrupt endpoint? */
		if (((ep_desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
			&& ep_desc->bEndpointAddress != 0x8f) {
			usb_debug("set endpoints[%d] to intr_ep\n", i);
			g_DrvData->intr_ep = ep_desc;
			continue;
		}
	}
	if (!g_DrvData->intr_ep || !g_DrvData->bulk_tx_ep || !g_DrvData->bulk_rx_ep) {
		os_kfree(g_DrvData);
		g_DrvData = NULL;
		usb_debug("btmtk_usb_probe end Error 3\n");
		return -1;
	}

	/* Init HostController interface */
	g_DrvData->hci_if = &usbbt_host_interface;
	g_DrvData->chipid = chipid;

	/* btmtk init */
	ret = LD_btmtk_usb_probe(g_DrvData, flag);

	if (ret != 0) {
		usb_debug("usb probe fail\n");
		if(g_DrvData) {
		   os_kfree(g_DrvData);
		}
		g_DrvData = NULL;
		return -1;
	} else {
		usb_debug("usbbt probe success\n");
	}
	usb_debug("%s end\n", __func__);
	return ret;
}

u8 LDbtusb_getWoBTW(void)
{
	return LD_btmtk_usb_getWoBTW();
}

#ifndef CONFIG_DM_USB
static int checkUsbDevicePort(struct usb_device *udev, usb_vid_pid *pmtk_dongle, u8 port)
{
	struct usb_device *pdev = NULL;
	int i;
	int dongleCount = 0;
#if defined (CONFIG_USB_PREINIT)
	usb_stop(port);
	if (usb_post_init(port) == 0)
#else
	if (usb_init() == 0)
#endif
	{
		for (i = 0; i < usb_get_dev_num(); i++) {
			pdev = usb_get_dev_index(i); // get device
			if (pdev != NULL) {
				for (dongleCount = 0; dongleCount < max_mtk_wifi_id; dongleCount++) {
					if ((pdev->descriptor.idVendor == pmtk_dongle[dongleCount].vid)
						&& (pdev->descriptor.idProduct == pmtk_dongle[dongleCount].pid)) {
						UBOOT_TRACE("OK\n");
						memcpy(udev, pdev, sizeof(struct usb_device));
						chipid = pmtk_dongle[dongleCount].pid;
						return 0;
					}
				}
			}
		}
	}
	return -1;
}
#endif

#ifdef CONFIG_DM_USB
typedef int (*usb_dev_func_t)(struct usb_device *dstudev, struct usb_device *udev, usb_vid_pid *pmtk_dongle);
static int usb_show_info(struct usb_device *dstudev, struct usb_device *udev, usb_vid_pid *pmtk_dongle)
{
	int ret = 0;
	int dongleCount = 0;
	struct udevice *child;

	usb_debug("%s begin\n", __func__);

	for (dongleCount = 0; dongleCount < max_mtk_wifi_id; dongleCount++) {
		if ((udev->descriptor.idVendor == pmtk_dongle[dongleCount].vid)
			&& (udev->descriptor.idProduct == pmtk_dongle[dongleCount].pid)) {
			usb_debug("%s OK, usb device be found!\n", __func__);
			memcpy(dstudev, udev, sizeof(struct usb_device));
			chipid = pmtk_dongle[dongleCount].pid;
			return 0;
		}
	}

	for (device_find_first_child(udev->dev, &child);
		child;
		device_find_next_child(&child)) {
		if (device_active(child) &&
			(device_get_uclass_id(child) != UCLASS_USB_EMUL) &&
			(device_get_uclass_id(child) != UCLASS_BLK)) {
			udev = dev_get_parent_priv(child);
			ret = usb_show_info(dstudev, udev, pmtk_dongle);
			if (ret == -1)
				continue;
			else if (ret == 0)
				return 0;
		}
	}

	usb_debug("%s end\n", __func__);

	return -1;
}

static int usb_for_each_root_dev(struct usb_device *dstudev, usb_dev_func_t func, usb_vid_pid *pmtk_dongle)
{
	struct udevice *bus;
	int ret = 0;

	usb_debug("%s begin\n", __func__);

	if(usb_init() != 0)
		return -1;

	for (uclass_find_first_device(UCLASS_USB, &bus);
		bus;
		uclass_find_next_device(&bus)) {
		struct usb_device *udev;
		struct udevice *dev;

		if (!device_active(bus))
			continue;

		device_find_first_child(bus, &dev);
		if (dev && device_active(dev)) {
			udev = dev_get_parent_priv(dev);
			ret = func(dstudev, udev, pmtk_dongle);
			if (ret == 0)
				break;
		}
	}

	usb_debug("%s end\n", __func__);

	return ret;
}
#endif
static int findUsbDevice(struct usb_device* udev)
{
	int ret = -1;

	usb_debug("%s begin\n", __func__);

#ifdef CONFIG_DM_USB
	if(udev == NULL) {
		UBOOT_ERROR("udev can not be NULL\n");
		return -1;
	}

	ret = usb_for_each_root_dev(udev, usb_show_info,  pmtk_wifi);
	if (ret == -1) {
		UBOOT_ERROR("Not find usb device\n");
		return -1;
	}
	usb_debug("usb device has been finded\n");
	usb_debug("udev->descriptor.idVendor = %d\n", udev->descriptor.idVendor);
	usb_debug("udev->descriptor.idProduct = %d\n", udev->descriptor.idProduct);
	return 0;
#else
	u8 idx = 0;
	char portNumStr[10] = "\0";
	char* pBTUsbPort = NULL;
	UBOOT_TRACE("IN\n");
	if (udev == NULL) {
		UBOOT_ERROR("udev can not be NULL\n");
		return -1;
	}
	#define BT_USB_PORT "bt_usb_port"
	pBTUsbPort = env_get(BT_USB_PORT);
	if (pBTUsbPort != NULL) {
		// search mtk bt usb port
		idx = simple_strtol(pBTUsbPort, NULL, 10);
		usb_debug("find mtk bt usb device from usb prot[%d]\n", idx);
		ret = checkUsbDevicePort(udev, pmtk_wifi, idx);
		if (ret == 0) {
			return 0;
		}
	}

	// not find mt bt usb device from given usb port, so poll every usb port.
	#if defined(ENABLE_FIFTH_EHC)
	const char u8UsbPortCount = 5;
	#elif defined(ENABLE_FOURTH_EHC)
	const char u8UsbPortCount = 4;
	#elif defined(ENABLE_THIRD_EHC)
	const char u8UsbPortCount = 3;
	#elif defined(ENABLE_SECOND_EHC)
	const char u8UsbPortCount = 2;
	#else
	const char u8UsbPortCount = 1;
	#endif
	for (idx = 0; idx < u8UsbPortCount; idx++) {
		ret = checkUsbDevicePort(udev, pmtk_wifi, idx);
		if (ret == 0) {
			// set bt_usb_port to store mt bt usb device port
			(void)snprintf(portNumStr, sizeof(portNumStr), "%d", idx);
			env_set(BT_USB_PORT, portNumStr);
			env_save();
			return 0;
		}
	}
	if (pBTUsbPort != NULL) {
		// env BT_USB_PORT is involid, so delete it
		env_set(BT_USB_PORT, NULL);
		env_save();
	}
	UBOOT_ERROR("Not find usb device\n");
	return -1;
#endif
	usb_debug("%s end\n", __func__);
}

int do_setMtkBT(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int ret = 0;
	char* pBTUsbPort = NULL;
	int usbPort = 0;
	struct usb_device udev;
	memset(&udev, 0, sizeof(struct usb_device));
	UBOOT_TRACE("IN\n");
	if (argc < 1)
	{
		cmd_usage(cmdtp);
		return -1;
	}
	// MTK USB controller
	ret = findUsbDevice(&udev);
	if (ret != 0)
	{
		UBOOT_ERROR("find bt usb device failed\n");
		return -1;
	}
	ret = Ldbtusb_connect(&udev, 0);
	if(ret != 0){
		UBOOT_ERROR("connect to bt usb device failed\n");
		return -1;
	}
	ret = Ldbtusb_SetWoble(&udev);
	if(ret != 0)
	{
		UBOOT_ERROR("set bt usb device woble cmd failed\n");
		return -1;
	}

	usb_debug("ready to do usb_stop\n");

	pBTUsbPort = env_get(BT_USB_PORT);
	if(pBTUsbPort != NULL)
	{
		// search mtk bt usb port
		usbPort = simple_strtol(pBTUsbPort, NULL, 10);
		if (usbPort < 0 || usbPort >= MAX_ROOT_PORTS) {
			UBOOT_ERROR("usbPort(%d) is not in correct scope\n", usbPort);
			return -1;
		}
		usb_debug("stop usb port: %d\n", usbPort);
		if(usb_stop() != 0){
			usb_debug("usb_stop fail\n");
		}
	} else {
		usb_debug("no BT_USB_PORT\n");
	}

	UBOOT_TRACE("OK\n");
	return ret;
}

int do_getMtkBTWakeT(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	 int ret = 0;
	struct usb_device udev;
	memset(&udev, 0, sizeof(struct usb_device));
	UBOOT_TRACE("IN\n");
	if (argc < 1)
	{
		cmd_usage(cmdtp);
		return -1;
	}
	// MTK USB controller
	ret = findUsbDevice(&udev);
	if (ret != 0)
	{
		UBOOT_ERROR("find bt usb device failed\n");
		return -1;
	}
	ret = Ldbtusb_connect(&udev, 1);
	if(ret != 0)
	{
		UBOOT_ERROR("connect to bt usb device failed\n");
		return -1;
	}

	if(ret != 0)
	{
		UBOOT_ERROR("set bt usb device woble cmd failed\n");
		return -1;
	}
	UBOOT_TRACE("OK\n");
	return ret;
}

