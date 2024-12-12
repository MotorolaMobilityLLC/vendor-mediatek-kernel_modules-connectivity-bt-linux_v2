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

#include "btmtk_chip_7922.h"
#include "btmtk_chip_common.h"


int btmtk_cif_chip_7922_register(void)
{
	int retval = 0;
	struct btmtk_main_info *bmain_info = btmtk_get_main_info();

	BTMTK_INFO("%s", __func__);

	bmain_info->hif_hook_chip.dl_delay_time = PATCH_DOWNLOAD_PHASE3_SECURE_BOOT_DELAY_TIME;

	return retval;
}

