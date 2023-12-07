/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include "config.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <gio/gunixinputstream.h>

#include "fu-ifd-device.h"
#include "fu-intel-spi-common.h"
#include "fu-intel-spi-device.h"
#include "fu-intel-spi-struct.h"

struct _FuIntelSpiDevice {
	FuDevice parent_instance;
	FuIntelSpiKind kind;
	guint32 phys_spibar;
	gpointer spibar;
	guint16 hsfs;
	guint16 frap;
	guint32 freg[4];
	guint32 flvalsig;
	guint32 descriptor_map0;
	guint32 descriptor_map1;
	guint32 descriptor_map2;
	guint32 components_rcd;
	guint32 illegal_jedec;
	guint32 flpb;
	guint32 flash_master[4];
	guint32 protected_range[4];
};

#define FU_INTEL_SPI_PHYS_SPIBAR_SIZE 0x10000 /* bytes */
#define FU_INTEL_SPI_READ_TIMEOUT     10      /* ms */

#define PCI_BASE_ADDRESS_0 0x0010

#define HSFS_FDOPSS_BIT 13 /* Flash Descriptor Override Pin-Strap Status */
#define HSFS_FDV_BIT	14 /* Flash Descriptor Valid */

/**
 * FU_INTEL_SPI_DEVICE_FLAG_ICH:
 *
 * Device is an I/O Controller Hub.
 */
#define FU_INTEL_SPI_DEVICE_FLAG_ICH (1 << 0)
/**
 * FU_INTEL_SPI_DEVICE_FLAG_PCH:
 *
 * Device is a Platform Controller Hub.
 */
#define FU_INTEL_SPI_DEVICE_FLAG_PCH (1 << 1)

G_DEFINE_TYPE(FuIntelSpiDevice, fu_intel_spi_device, FU_TYPE_DEVICE)

static void
fu_intel_spi_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIntelSpiDevice *self = FU_INTEL_SPI_DEVICE(device);
	fu_string_append(str, idt, "Kind", fu_intel_spi_kind_to_string(self->kind));
	fu_string_append_kx(str, idt, "SPIBAR", self->phys_spibar);
	fu_string_append_kx(str, idt, "HSFS", self->hsfs);
	fu_string_append_kx(str, idt, "FRAP", self->frap);
	for (guint i = 0; i < 4; i++) {
		g_autofree gchar *title = g_strdup_printf("FREG%u", i);
		fu_string_append_kx(str, idt, title, self->freg[i]);
	}
	for (guint i = 0; i < 4; i++) {
		g_autofree gchar *title = g_strdup_printf("FLMSTR%u", i);
		fu_string_append_kx(str, idt, title, self->flash_master[i]);
	}
	fu_string_append_kx(str, idt, "FLVALSIG", self->flvalsig);
	fu_string_append_kx(str, idt, "FLMAP0", self->descriptor_map0);
	fu_string_append_kx(str, idt, "FLMAP1", self->descriptor_map1);
	fu_string_append_kx(str, idt, "FLMAP2", self->descriptor_map2);
	fu_string_append_kx(str, idt, "FLCOMP", self->components_rcd);
	fu_string_append_kx(str, idt, "FLILL", self->illegal_jedec);
	fu_string_append_kx(str, idt, "FLPB", self->flpb);

	/* PRx */
	for (guint i = 0; i < 4; i++) {
		guint32 limit = 0;
		guint32 base = 0;
		FuIfdAccess access = FU_IFD_ACCESS_NONE;
		g_autofree gchar *title = NULL;
		g_autofree gchar *tmp = NULL;

		if (self->protected_range[i] == 0x0)
			continue;
		if ((self->protected_range[i] >> 31) & 0b1)
			access |= FU_IFD_ACCESS_WRITE;
		if ((self->protected_range[i] >> 15) & 0b1)
			access |= FU_IFD_ACCESS_READ;
		if (access != FU_IFD_ACCESS_NONE) {
			base = ((self->protected_range[i] >> 0) & 0x1FFF) << 12;
			limit = (((self->protected_range[i] >> 16) & 0x1FFF) << 12) | 0xFFFF;
		}
		title = g_strdup_printf("PR%u", i);
		tmp = g_strdup_printf("blocked %s from 0x%x to 0x%x [0x%x]",
				      fu_ifd_access_to_string(access),
				      base,
				      limit,
				      self->protected_range[i]);
		fu_string_append(str, idt, title, tmp);
	}
}

static gboolean
fu_intel_spi_device_open(FuDevice *device, GError **error)
{
	FuIntelSpiDevice *self = FU_INTEL_SPI_DEVICE(device);
	int fd;
	g_autoptr(GInputStream) istr = NULL;

	/* this will fail if the kernel is locked down */
	fd = open("/dev/mem", O_SYNC | O_RDWR);
	if (fd == -1) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
#ifdef HAVE_ERRNO_H
			    "failed to open /dev/mem: %s",
			    strerror(errno));
#else
			    "failed to open /dev/mem");
#endif
		return FALSE;
	}
	istr = g_unix_input_stream_new(fd, TRUE);
	if (istr == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "failed to create input stream");
		return FALSE;
	}
	self->spibar = mmap(NULL,
			    FU_INTEL_SPI_PHYS_SPIBAR_SIZE,
			    PROT_READ | PROT_WRITE,
			    MAP_SHARED,
			    fd,
			    self->phys_spibar);
	if (self->spibar == MAP_FAILED) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
#ifdef HAVE_ERRNO_H
			    "failed to mmap SPIBAR: %s",
			    strerror(errno));
#else
			    "failed to mmap SPIBAR");
#endif
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_intel_spi_device_close(FuDevice *device, GError **error)
{
	FuIntelSpiDevice *self = FU_INTEL_SPI_DEVICE(device);

	/* close */
	if (self->spibar != NULL) {
		if (munmap(self->spibar, FU_INTEL_SPI_PHYS_SPIBAR_SIZE) == -1) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
#ifdef HAVE_ERRNO_H
				    "failed to unmap SPIBAR: %s",
				    strerror(errno));
#else
				    "failed to unmap SPIBAR");
#endif
			return FALSE;
		}
		self->spibar = NULL;
	}

	/* success */
	return TRUE;
}

static guint32
fu_intel_spi_device_read_reg(FuIntelSpiDevice *self, guint8 section, guint16 offset)
{
	guint32 control = 0;
	control |= (((guint32)section) << 12) & FDOC_FDSS;
	control |= (((guint32)offset) << 2) & FDOC_FDSI;
	fu_mmio_write32_le(self->spibar, PCH100_REG_FDOC, control);
	return fu_mmio_read32_le(self->spibar, PCH100_REG_FDOD);
}

static void
fu_intel_spi_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuIntelSpiDevice *self = FU_INTEL_SPI_DEVICE(device);
	FuIfdAccess access_global = FU_IFD_ACCESS_NONE;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(device, FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* check for read access from other regions */
	for (guint j = FU_IFD_REGION_BIOS; j < 4; j++) {
		FuIfdAccess access;
		access =
		    fu_ifd_region_to_access(FU_IFD_REGION_DESC, self->flash_master[j - 1], TRUE);
		fwupd_security_attr_add_metadata(attr,
						 fu_ifd_region_to_string(j),
						 fu_ifd_access_to_string(access));
		access_global |= access;
	}

	/* any region can write to the flash descriptor */
	if (access_global & FU_IFD_ACCESS_WRITE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* FLOCKDN is unset */
	if ((self->hsfs >> 15 & 0b1) == 0) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static gboolean
fu_intel_spi_device_probe(FuDevice *device, GError **error)
{
	FuIntelSpiDevice *self = FU_INTEL_SPI_DEVICE(device);

	/* verify this was set in the quirk file */
	if (self->kind == FU_INTEL_SPI_KIND_UNKNOWN) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "IntelSpiKind not set");
		return FALSE;
	}

	/* specified explicitly as a physical address */
	if (self->phys_spibar == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "IntelSpiBar not set");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_intel_spi_device_setup(FuDevice *device, GError **error)
{
	FuIntelSpiDevice *self = FU_INTEL_SPI_DEVICE(device);
	guint64 total_size = 0;
	guint8 comp1_density;
	guint8 comp2_density;
	gboolean me_is_locked;
	guint16 reg_pr0 = fu_device_has_private_flag(device, FU_INTEL_SPI_DEVICE_FLAG_ICH)
			      ? ICH9_REG_PR0
			      : PCH100_REG_FPR0;

	/* dump everything */
	for (guint i = 0; i < 0xff; i += 4) {
		guint32 tmp = fu_mmio_read32(self->spibar, i);
		g_print("SPIBAR[0x%02x] = 0x%x\n", i, tmp);
	}

	/* read from descriptor */
	self->hsfs = fu_mmio_read16(self->spibar, ICH9_REG_HSFS);
	self->frap = fu_mmio_read16(self->spibar, ICH9_REG_FRAP);
	for (guint i = FU_IFD_REGION_DESC; i < 4; i++)
		self->freg[i] = fu_mmio_read32(self->spibar, ICH9_REG_FREG0 + i * 4);
	self->flvalsig = fu_intel_spi_device_read_reg(self, 0, 0);
	self->descriptor_map0 = fu_intel_spi_device_read_reg(self, 0, 1);
	self->descriptor_map1 = fu_intel_spi_device_read_reg(self, 0, 2);
	self->descriptor_map2 = fu_intel_spi_device_read_reg(self, 0, 3);
	self->components_rcd = fu_intel_spi_device_read_reg(self, 1, 0);
	self->illegal_jedec = fu_intel_spi_device_read_reg(self, 1, 1);
	self->flpb = fu_intel_spi_device_read_reg(self, 1, 2);

	for (guint i = 0; i < 4; i++)
		self->flash_master[i] = fu_intel_spi_device_read_reg(self, 3, i);
	for (guint i = 0; i < 4; i++) {
		self->protected_range[i] =
		    fu_mmio_read32(self->spibar, reg_pr0 + i * sizeof(guint32));
	}

	/* set size */
	comp1_density = (self->components_rcd & 0x0f) >> 0;
	if (comp1_density != 0xf)
		total_size += 1ull << (19 + comp1_density);
	comp2_density = (self->components_rcd & 0xf0) >> 4;
	if (comp2_density != 0xf)
		total_size += 1ull << (19 + comp2_density);
	fu_device_set_firmware_size(device, total_size);

	me_is_locked = !(self->hsfs & (1 << HSFS_FDV_BIT)) || /* assume locked if not valid */
		       (self->hsfs & (1 << HSFS_FDOPSS_BIT)); /* use status bit if valid */
	/* add children */
	for (guint i = FU_IFD_REGION_BIOS; i < 4; i++) {
		g_autoptr(FuDevice) child = NULL;
		if (self->freg[i] == 0x0)
			continue;
		child = fu_ifd_device_new(fu_device_get_context(device), i, self->freg[i]);
		for (guint j = 1; j < 4; j++) {
			FuIfdAccess access;
			access = fu_ifd_region_to_access(i, self->flash_master[j - 1], TRUE);
			fu_ifd_device_set_access(FU_IFD_DEVICE(child), j, access);
		}

		if (i == FU_IFD_REGION_ME && me_is_locked)
			fu_device_add_flag(child, FWUPD_DEVICE_FLAG_LOCKED);

		fu_device_add_child(device, child);
	}

	return TRUE;
}

static gboolean
fu_intel_spi_device_wait(FuIntelSpiDevice *self, guint timeout_ms, GError **error)
{
	g_usleep(1);
	for (guint i = 0; i < timeout_ms * 100; i++) {
		guint16 hsfs = fu_mmio_read16(self->spibar, ICH9_REG_HSFS);
		if (hsfs & HSFS_FDONE)
			return TRUE;
		if (hsfs & HSFS_FCERR) {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "HSFS transaction error");
			return FALSE;
		}
		g_usleep(10);
	}
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "HSFS timed out");
	return FALSE;
}

static void
fu_intel_spi_device_set_addr(FuIntelSpiDevice *self, guint32 addr)
{
	guint32 addr_old = fu_mmio_read32(self->spibar, ICH9_REG_FADDR) & ~PCH100_FADDR_FLA;
	fu_mmio_write32(self->spibar, ICH9_REG_FADDR, (addr & PCH100_FADDR_FLA) | addr_old);
}

GBytes *
fu_intel_spi_device_dump(FuIntelSpiDevice *self,
			 FuDevice *device,
			 guint32 offset,
			 guint32 length,
			 FuProgress *progress,
			 GError **error)
{
	guint8 block_len = 0x40;
	g_autoptr(GByteArray) buf = g_byte_array_sized_new(length);

	/* set FDONE, FCERR, AEL */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	fu_mmio_write16(self->spibar, ICH9_REG_HSFS, fu_mmio_read16(self->spibar, ICH9_REG_HSFS));
	for (guint32 addr = offset; addr < offset + length; addr += block_len) {
		guint16 hsfc;
		guint32 buftmp32 = 0;

		/* set up read */
		fu_intel_spi_device_set_addr(self, addr);
		hsfc = fu_mmio_read16(self->spibar, ICH9_REG_HSFC);
		hsfc &= ~PCH100_HSFC_FCYCLE;
		hsfc &= ~HSFC_FDBC;

		/* set byte count */
		hsfc |= ((block_len - 1) << 8) & HSFC_FDBC;
		hsfc |= HSFC_FGO;
		fu_mmio_write16(self->spibar, ICH9_REG_HSFC, hsfc);
		if (!fu_intel_spi_device_wait(self, FU_INTEL_SPI_READ_TIMEOUT, error)) {
			g_prefix_error(error, "failed @0x%x: ", addr);
			return NULL;
		}

		/* copy out data */
		for (guint i = 0; i < block_len; i++) {
			if (i % 4 == 0)
				buftmp32 = fu_mmio_read32(self->spibar, ICH9_REG_FDATA0 + i);
			fu_byte_array_append_uint8(buf, buftmp32 >> ((i % 4) * 8));
		}

		/* progress */
		fu_progress_set_percentage_full(progress, addr - offset + block_len, length);
	}

	/* success */
	return g_bytes_new(buf->data, buf->len);
}

static GBytes *
fu_intel_spi_device_dump_firmware2(FuDevice *device, FuProgress *progress, GError **error)
{
	FuIntelSpiDevice *self = FU_INTEL_SPI_DEVICE(device);
	guint64 total_size = fu_device_get_firmware_size_max(device);
	return fu_intel_spi_device_dump(self, device, 0x0, total_size, progress, error);
}

static GBytes *
fu_intel_spi_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	return fu_intel_spi_device_dump_firmware2(device, progress, error);
}

static FuFirmware *
fu_intel_spi_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_ifd_firmware_new();
	g_autoptr(GBytes) blob = NULL;

	blob = fu_intel_spi_device_dump_firmware2(device, progress, error);
	if (blob == NULL)
		return NULL;
	if (!fu_firmware_parse(firmware, blob, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_intel_spi_device_set_quirk_kv(FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuIntelSpiDevice *self = FU_INTEL_SPI_DEVICE(device);
	if (g_strcmp0(key, "IntelSpiBar") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->phys_spibar = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "IntelSpiKind") == 0) {
		/* validate */
		self->kind = fu_intel_spi_kind_from_string(value);
		if (self->kind == FU_INTEL_SPI_KIND_UNKNOWN) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "%s not supported",
				    value);
			return FALSE;
		}

		/* get things like SPIBAR */
		fu_device_add_instance_strup(device, "ID", value);
		return fu_device_build_instance_id(device, error, "INTEL_SPI_CHIPSET", "ID", NULL);
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static void
fu_intel_spi_device_init(FuIntelSpiDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_device_set_physical_id(FU_DEVICE(self), "intel_spi");
	fu_device_register_private_flag(FU_DEVICE(self), FU_INTEL_SPI_DEVICE_FLAG_ICH, "ich");
	fu_device_register_private_flag(FU_DEVICE(self), FU_INTEL_SPI_DEVICE_FLAG_PCH, "pch");
}

static void
fu_intel_spi_device_class_init(FuIntelSpiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_intel_spi_device_to_string;
	klass_device->probe = fu_intel_spi_device_probe;
	klass_device->setup = fu_intel_spi_device_setup;
	klass_device->dump_firmware = fu_intel_spi_device_dump_firmware;
	klass_device->read_firmware = fu_intel_spi_device_read_firmware;
	klass_device->open = fu_intel_spi_device_open;
	klass_device->close = fu_intel_spi_device_close;
	klass_device->set_quirk_kv = fu_intel_spi_device_set_quirk_kv;
	klass_device->add_security_attrs = fu_intel_spi_device_add_security_attrs;
}
