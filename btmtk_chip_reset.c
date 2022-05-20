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

#include "btmtk_main.h"
#include "btmtk_woble.h"

void btmtk_reset_waker(struct work_struct *work)
{
	struct btmtk_dev *bdev = container_of(work, struct btmtk_dev, reset_waker);
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_main_info *bmain_info = btmtk_get_main_info();
	int cif_event = 0, err = 0;

	DUMP_TIME_STAMP("chip_reset_start");
	cif_event = HIF_EVENT_SUBSYS_RESET;
	if (BTMTK_CIF_IS_NULL(bdev, cif_event)) {
		/* Error */
		BTMTK_WARN("%s priv setting is NULL", __func__);
		goto Finish;
	}

	while (!bdev->bt_cfg.support_dongle_reset) {
		BTMTK_ERR("%s chip_reset is not support", __func__);
		msleep(2000);
	}

	cif_state = &bdev->cif_state[cif_event];

	/* Set Entering state */
	btmtk_set_chip_state((void *)bdev, cif_state->ops_enter);

	BTMTK_INFO("%s: Receive a byte (0xFF)", __func__);
	/* read interrupt EP15 CR */

	bdev->subsys_reset = 1;
	bdev->sco_num = 0;

	if (bmain_info->chip_reset_flag == 0) {
		if (bmain_info->hif_hook.subsys_reset) {
			DUMP_TIME_STAMP("subsys_chip_reset_start");
			err = bmain_info->hif_hook.subsys_reset(bdev);
			atomic_inc(&bmain_info->subsys_reset_count);
			DUMP_TIME_STAMP("subsys_chip_reset_end");
		} else {
			BTMTK_INFO("%s: Not support subsys chip reset", __func__);
		}
	} else {
		err = -1;
		BTMTK_INFO("%s: chip_reset_flag is %d", __func__, bmain_info->chip_reset_flag);
	}

	if (err) {
		/* L0.5 reset failed, do whole chip reset */
		/* We will add support dongle reset flag, reading from bt.cfg */
		bdev->subsys_reset = 0;
		/* TODO: need to confirm with usb host when suspend fail, to do chip reset,
		 * because usb3.0 need to toggle reset pin after hub_event unfreeze,
		 * otherwise, it will not occur disconnect on Capy Platform. When Mstar
		 * chip has usb3.0 port, we will use Mstar platform to do comparison
		 * test, then found the final solution.
		 */
		/* msleep(2000); */
		if (bmain_info->hif_hook.whole_reset) {
			DUMP_TIME_STAMP("whole_chip_reset_start");
			bmain_info->hif_hook.whole_reset(bdev);
			atomic_inc(&bmain_info->whole_reset_count);
			DUMP_TIME_STAMP("whole_chip_reset_end");
		} else {
			BTMTK_INFO("%s: Not support whole chip reset", __func__);
		}
		goto Finish;
	}

	/* It's a test code for stress test (whole chip reset & L0.5 reset) */
#if 0
	if (bdev->bt_cfg.support_dongle_reset == 0) {
		err = btmtk_cif_subsys_reset(bdev);
		if (err) {
			/* L0.5 reset failed, do whole chip reset */
			if (main_info.hif_hook->whole_reset)
				main_info.hif_hook.whole_reset(bdev);
			goto Finish;
		}
	} else {
		/* L0.5 reset failed, do whole chip reset */
		/* TODO: need to confirm with usb host when suspend fail, to do chip reset,
		 * because usb3.0 need to toggle reset pin after hub_event unfreeze,
		 * otherwise, it will not occur disconnect on Capy Platform. When Mstar
		 * chip has usb3.0 port, we will use Mstar platform to do comparison
		 * test, then found the final solution.
		 */
		/* msleep(2000); */
		if (main_info.hif_hook->whole_reset)
			main_info.hif_hook.whole_reset(bdev);
		/* btmtk_send_hw_err_to_host(bdev); */
		goto Finish;
	}
#endif

	bmain_info->reset_stack_flag = HW_ERR_CODE_CHIP_RESET;
	bdev->subsys_reset = 0;

	err = btmtk_cap_init(bdev);
	if (err < 0) {
		BTMTK_ERR("btmtk init failed!");
		goto Finish;
	}

	err = btmtk_load_rom_patch(bdev);
	if (err < 0) {
		BTMTK_ERR("btmtk load rom patch failed!");
		goto Finish;
	}
	btmtk_send_hw_err_to_host(bdev);
	btmtk_woble_wake_unlock(bdev);

Finish:
	bmain_info->hif_hook.chip_reset_notify(bdev);
	DUMP_TIME_STAMP("chip_reset_end");

	/* Set End/Error state */
	if (err < 0)
		btmtk_set_chip_state((void *)bdev, cif_state->ops_error);
	else
		btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
}

