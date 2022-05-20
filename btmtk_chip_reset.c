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

#include "btmtk_chip_reset.h"

#if (KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE)
static void btmtk_reset_timer(unsigned long arg)
{
	struct btmtk_dev *bdev = (struct btmtk_dev *)arg;

	BTMTK_INFO("%s: chip_reset not trigger in %d seconds, trigger it directly",
		__func__, CHIP_RESET_TIMEOUT);
	schedule_work(&bdev->reset_waker);
}
#else
static void btmtk_reset_timer(struct timer_list *timer)
{
	struct btmtk_dev *bdev = from_timer(bdev, timer, chip_reset_timer);

	BTMTK_INFO("%s: chip_reset not trigger in %d seconds, trigger it directly",
		__func__, CHIP_RESET_TIMEOUT);
	schedule_work(&bdev->reset_waker);
}
#endif

void btmtk_reset_timer_add(struct btmtk_dev *bdev)
{
	BTMTK_INFO("%s: create chip_reset timer", __func__);
#if (KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE)
	init_timer(&bdev->chip_reset_timer);
	bdev->chip_reset_timer.function = btmtk_reset_timer;
	bdev->chip_reset_timer.data = (unsigned long)bdev;
	mod_timer(&bdev->chip_reset_timer, jiffies + CHIP_RESET_TIMEOUT * HZ);
#else
	timer_setup(&bdev->chip_reset_timer, btmtk_reset_timer, 0);
	mod_timer(&bdev->chip_reset_timer, jiffies + CHIP_RESET_TIMEOUT * HZ);
#endif
}

static void btmtk_reset_timer_del(struct btmtk_dev *bdev)
{
	del_timer_sync(&bdev->chip_reset_timer);
}

void btmtk_reset_waker(struct work_struct *work)
{
	struct btmtk_dev *bdev = container_of(work, struct btmtk_dev, reset_waker);
	struct btmtk_cif_state *cif_state = NULL;
	struct btmtk_main_info *bmain_info = btmtk_get_main_info();
	int cif_event = 0, err = 0;
	int state = BTMTK_STATE_INIT;

	btmtk_reset_timer_del(bdev);

	state = btmtk_get_chip_state(bdev);
	if (state == BTMTK_STATE_SUBSYS_RESET || bdev->subsys_reset || bdev->chip_reset) {
		BTMTK_INFO("%s: reset is ongoing, state = %d, subsys_rst = %d, chip_rst = %d",
			__func__, state, bdev->subsys_reset, bdev->chip_reset);
		return;
	}

	if (bdev->debug_type != DEBUG_SOP_NONE && bmain_info->hif_hook.dump_debug_sop)
		bmain_info->hif_hook.dump_debug_sop(bdev, bdev->debug_type);
	bdev->debug_type = DEBUG_SOP_NONE;

	DUMP_TIME_STAMP("chip_reset_start");
	cif_event = HIF_EVENT_SUBSYS_RESET;
	if (BTMTK_CIF_IS_NULL(bdev, cif_event)) {
		/* Error */
		BTMTK_WARN("%s priv setting is NULL", __func__);
		return;
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

	bdev->sco_num = 0;

	if (bmain_info->chip_reset_flag == 0) {
		if (bmain_info->hif_hook.subsys_reset) {
			bdev->subsys_reset = 1;

			DUMP_TIME_STAMP("subsys_chip_reset_start");
			err = bmain_info->hif_hook.subsys_reset(bdev);
			if (err < 0) {
				BTMTK_INFO("subsys reset failed, do whole chip reset!");
			} else {
				atomic_inc(&bmain_info->subsys_reset_count);
				DUMP_TIME_STAMP("subsys_chip_reset_end");

				bmain_info->reset_stack_flag = HW_ERR_CODE_CHIP_RESET;
				bdev->subsys_reset = 0;

				err = btmtk_cap_init(bdev);
				if (err < 0) {
					BTMTK_ERR("btmtk init failed!");
				} else {
					err = btmtk_load_rom_patch(bdev);
					if (err < 0) {
						BTMTK_INFO("btmtk load rom patch failed!");
					} else {
						btmtk_send_hw_err_to_host(bdev);
						btmtk_woble_wake_unlock(bdev);
						if (bmain_info->hif_hook.chip_reset_notify)
							bmain_info->hif_hook.chip_reset_notify(bdev);
					}
				}
			}
		} else {
			err = -1;
			BTMTK_INFO("%s: Not support subsys chip reset", __func__);
		}
	} else {
		err = -1;
		BTMTK_INFO("%s: chip_reset_flag is %d", __func__, bmain_info->chip_reset_flag);
	}

	if (err < 0) {
		/* L0.5 reset failed or not support, do whole chip reset */
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
	}

	DUMP_TIME_STAMP("chip_reset_end");
	/* Set End/Error state */
	if (err < 0)
		btmtk_set_chip_state((void *)bdev, cif_state->ops_error);
	else
		btmtk_set_chip_state((void *)bdev, cif_state->ops_end);
}

void btmtk_reset_trigger(struct btmtk_dev *bdev)
{
	int state = BTMTK_STATE_INIT;
	struct btmtk_main_info *bmain_info = btmtk_get_main_info();

	state = btmtk_get_chip_state(bdev);
	if (state == BTMTK_STATE_SUSPEND) {
		BTMTK_INFO("%s suspend state don't do chip reset!", __func__);
		return;
	}
	if (state == BTMTK_STATE_PROBE) {
		bmain_info->chip_reset_flag = 1;
		BTMTK_INFO("%s just do whole chip reset in probe stage!", __func__);
	}

	schedule_work(&bdev->reset_waker);
}

