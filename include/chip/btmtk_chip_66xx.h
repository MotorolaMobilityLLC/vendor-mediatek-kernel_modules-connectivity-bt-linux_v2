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

#ifndef _BTMTK_CHIP_66xx_H_
#define _BTMTK_CHIP_66xx_H_

#include "btmtk_define.h"
#include "btmtk_main.h"
#if (USE_DEVICE_NODE == 1)
#include "btmtk_queue.h"
#include "connv3.h"
//#include "btmtk_proj_sp.h"
#endif

int btmtk_cif_chip_66xx_register(void);

#endif /* __BTMTK_CHIP_66xx_H__ */
