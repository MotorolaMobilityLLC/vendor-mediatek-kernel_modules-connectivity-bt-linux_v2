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

#ifndef _BTMTK_CHIP_COMMON_6639_H_
#define _BTMTK_CHIP_COMMON_6639_H_

#include "btmtk_main.h"
#include "btmtk_define.h"

#define BACK_UP_CMD_LEN 10

/* calibration backup & restore */
#define CAL_DATA_EVNET_LEN 9
#define CAL_DATA_CMD_LEN 10


int btmtk_cif_chip_6639_register(void);

#endif /* __BTMTK_CHIP_COMMON_6639_H__ */
