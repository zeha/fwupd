/*
 * Copyright 2024 Chris hofstaedtler <Ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mnt-pico-device.h"
#include "fu-mnt-pico-plugin.h"

struct _FuMntPicoPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuMntPicoPlugin, fu_mnt_pico_plugin, FU_TYPE_PLUGIN)

static void
fu_mnt_pico_plugin_init(FuMntPicoPlugin *self)
{
}

static void
fu_mnt_pico_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	//	FuContext *ctx = fu_plugin_get_context(plugin);
	//	fu_context_add_quirk_key(ctx, "MntPicoStartAddr");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MNT_PICO_DEVICE);
}

static void
fu_mnt_pico_plugin_class_init(FuMntPicoPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_mnt_pico_plugin_constructed;
}
