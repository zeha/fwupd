/*
 * Copyright 2024 Chris hofstaedtler <Ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_MNT_PICO_DEVICE (fu_mnt_pico_device_get_type())
G_DECLARE_FINAL_TYPE(FuMntPicoDevice, fu_mnt_pico_device, FU, MNT_PICO_DEVICE, FuUsbDevice)
