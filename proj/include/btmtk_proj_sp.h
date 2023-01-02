/*
 *  Copyright (c) 2016,2017 MediaTek Inc.
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

#if (USE_DEVICE_NODE == 1)
int btmtk_read_pmic_state(struct btmtk_dev *bdev);


int btmtk_set_pcm_pin_mux(void);

int btmtk_reset_pin_off(void);

int btmtk_connv3_sub_drv_init(struct btmtk_dev *bdev);
//int btmtk_connv3_sub_drv_init(struct platform_device *pdev);

int btmtk_connv3_sub_drv_deinit(void);
#endif // (USE_DEVICE_NODE == 1)

