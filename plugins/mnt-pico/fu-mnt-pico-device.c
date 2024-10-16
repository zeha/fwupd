/*
 * Copyright 2024 Chris hofstaedtler <Ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdio.h>

#include "fu-mnt-pico-common.h"
#include "fu-mnt-pico-device.h"
#include "fu-mnt-pico-struct.h"

struct _FuMntPicoDevice {
	FuUsbDevice parent_instance;
};

#define RESET_REQUEST_BOOTSEL 0x01
#define RESET_REQUEST_FLASH   0x02

#define RESET_INTERFACE_SUBCLASS 0x00
#define RESET_INTERFACE_PROTOCOL 0x01

G_DEFINE_TYPE(FuMntPicoDevice, fu_mnt_pico_device, FU_TYPE_USB_DEVICE)

static void
fu_mnt_pico_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMntPicoDevice *self = FU_MNT_PICO_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "HI_TODO", 1);
}

static gboolean
fu_mnt_pico_device_reset_into_bootsel(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	guint16 reset_interface = 2; // TODO

	FuUsbDeviceClaimFlags flags = 0;

	if (!fu_usb_device_claim_interface(FU_USB_DEVICE(device), reset_interface, flags, error)) {
		g_prefix_error(error, "failed to claim HID interface: ");
		return FALSE;
	}

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(device),
					    FU_USB_DIRECTION_HOST_TO_DEVICE, // direction
					    FU_USB_REQUEST_TYPE_CLASS,	     // request_type
					    FU_USB_RECIPIENT_INTERFACE,	     // recipient
					    RESET_REQUEST_BOOTSEL,	     // request
					    0,				     // value
					    reset_interface,		     // idx
					    NULL,			     // data
					    0,				     // length
					    NULL,			     // actual_length
					    2000,
					    NULL,
					    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_debug("ignoring expected error %s", error_local->message);
		} else {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to restart device: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_mnt_pico_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMntPicoDevice *self = FU_MNT_PICO_DEVICE(device);

	if (!fu_mnt_pico_device_reset_into_bootsel(device, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mnt_pico_device_setup(FuDevice *device, GError **error)
{
	FuMntPicoDevice *self = FU_MNT_PICO_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_mnt_pico_device_parent_class)->setup(device, error)) {
		return FALSE;
	}

	/* TODO: get the version and other properties from the hardware while open */
	fu_device_set_version(device, "0.0.0");

	/* success */
	return TRUE;
}

static void
fu_mnt_pico_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 15, "reload");
}

static void
fu_mnt_pico_device_init(FuMntPicoDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.microsoft.uf2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_add_icon(FU_DEVICE(self), "icon-name");
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
}

static void
fu_mnt_pico_device_class_init(FuMntPicoDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_mnt_pico_device_to_string;
	device_class->setup = fu_mnt_pico_device_setup;
	device_class->detach = fu_mnt_pico_device_detach;
	device_class->set_progress = fu_mnt_pico_device_set_progress;
}
