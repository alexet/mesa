/*
 * Copyright 2026 The etos authors
 * SPDX-License-Identifier: MIT
 *
 * A stand-in for libdrm's <xf86drm.h>, for the etos build
 * (-Detos-amdgpu=true). Companion to this directory's <amdgpu.h>.
 *
 * Everything libdrm's core header offers is about a DRM *device node* — an
 * fd, its capabilities, its bus address, its driver name and version. etos
 * has no device nodes: the GPU is reached through an AmdgpuDevice
 * capability, and there is no fd anywhere in the chain (see
 * ac_drm_etos.h). So none of this has an etos equivalent to forward to, and
 * the honest implementation of each is "no".
 *
 * These are `static inline` rather than a library, for the same reason the
 * <amdgpu.h> stand-in declares no functions: whatever calls them should
 * fail visibly at the call site, in code that can report it, rather than
 * pulling in a fake library that pretends.
 *
 * Mesa's own `_WIN32` fallbacks in ac_gpu_info.c and ac_surface.c define
 * this same set inline, for exactly the same reason. Taking that path on
 * etos does not work — those blocks also skip includes etos does want, and
 * their `static readlink` collides with mlibc's real one — so the
 * definitions live here instead and both files compile unmodified.
 */

#ifndef _ETOS_XF86DRM_H_
#define _ETOS_XF86DRM_H_

#include <stddef.h>
#include <stdint.h>

/* ioctl encoding macros, from libdrm's <drm/drm.h>. Needed only so
 * src/util/os_drm.h *compiles*: its drm_ioctl_write/_write_read inlines are
 * never called on etos (every path that would has been replaced by
 * ac_drm_etos.c), but the header is included regardless. Real values rather
 * than dummies, so that anything which did reach them would at least be
 * encoding the request the same way Linux does. */
#define DRM_IOCTL_BASE   'd'
#define DRM_COMMAND_BASE 0x40
#define DRM_IOC_NONE     0U
#define DRM_IOC_WRITE    1U
#define DRM_IOC_READ     2U
#define DRM_IOC(dir, base, nr, size) \
   (((dir) << 30) | ((size) << 16) | ((base) << 8) | (nr))

#define DRM_NODE_PRIMARY 0
#define DRM_NODE_RENDER  2

/* Capability queries against a device node. No node, no capabilities: the
 * caller's "unsupported" path is the correct one. Returning failure rather
 * than 0-with-value-0 because a caller that checks the return distinguishes
 * "not supported" from "supported, and the answer is zero". */
static inline int drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
   (void)fd; (void)capability; (void)value;
   return -1;
}

/* A human-readable name for a format modifier, for debug output only.
 * Callers free the result and handle NULL. */
static inline char *drmGetFormatModifierName(uint64_t modifier)
{
   (void)modifier;
   return NULL;
}

typedef struct _drmVersion {
   int version_major;
   int version_minor;
   int version_patchlevel;
   int name_len;
   char *name;
   int date_len;
   char *date;
   int desc_len;
   char *desc;
} drmVersion, *drmVersionPtr;

/* radeonsi calls this to learn the kernel driver's name and version — it
 * checks for "virtio_gpu" to decide whether to use the native-context
 * path, and gates features on the major. Both questions are answered
 * authoritatively elsewhere on etos: the backend reports the real DRM
 * interface version from ac_drm_device_initialize, and the virtio path is
 * never taken. NULL, which the caller already handles. */
static inline drmVersionPtr drmGetVersion(int fd)
{
   (void)fd;
   return NULL;
}

static inline void drmFreeVersion(drmVersionPtr v) { (void)v; }

typedef struct _drmPciBusInfo {
   uint16_t domain;
   uint8_t bus;
   uint8_t dev;
   uint8_t func;
} drmPciBusInfo, *drmPciBusInfoPtr;

typedef struct _drmDevice {
   char **nodes;
   int available_nodes;
   int bustype;
   union {
      drmPciBusInfoPtr pci;
   } businfo;
} drmDevice, *drmDevicePtr;

static inline int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device)
{
   (void)fd; (void)flags; (void)device;
   return -1;
}

static inline void drmFreeDevice(drmDevicePtr *device) { (void)device; }

/* Which kind of device node an fd refers to, and the render node's path.
 * There are no nodes and no fd — the winsys uses these only to build a
 * display-device name for its own bookkeeping, and handles both failing. */
static inline int drmGetNodeTypeFromFd(int fd)
{
   (void)fd;
   return -1;
}

/* dma-buf import. Sharing a buffer on etos means handing the AmdgpuBo
 * capability over, not passing an fd — so there is nothing to import from. */
static inline int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
   (void)fd; (void)prime_fd; (void)handle;
   return -1;
}

static inline char *drmGetRenderDeviceNameFromFd(int fd)
{
   (void)fd;
   return NULL;
}

#endif /* _ETOS_XF86DRM_H_ */
