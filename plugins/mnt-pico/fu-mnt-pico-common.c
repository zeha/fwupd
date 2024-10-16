/*
 * Copyright 2024 Chris hofstaedtler <Ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mnt-pico-common.h"

const gchar *
fu_mnt_pico_strerror(guint8 code)
{
	if (code == 0)
		return "success";
	return NULL;
}
