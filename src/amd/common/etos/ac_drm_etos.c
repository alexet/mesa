/*
 * Copyright 2026 The etos authors
 * SPDX-License-Identifier: MIT
 *
 * The etos implementation of the `ac_drm_*` API — see ac_drm_etos.h for the
 * chain it sits in and why it replaces `ac_linux_drm.c` rather than adding a
 * branch to it.
 *
 * Unimplemented entry points return `-ENOTSUP` rather than being omitted.
 * Omitting them would be a link error at the end of a long build naming a
 * symbol with no context; returning `-ENOTSUP` fails at the call site, in a
 * driver that mostly works, with the driver's own error path to report it.
 * Each one says what it would take to implement it.
 */

#include "ac_drm_etos.h"
#include "ac_linux_drm.h"
#include "drm-uapi/drm.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/os_time.h"
#include "util/u_sync_provider.h"

/*
 * One device per process — amdgpu-glue holds the session, so this carries
 * only what Mesa insists on being handed back.
 */
struct ac_drm_device {
   struct util_sync_provider sync;
   /* The VA allocator's window, from AMDGPU_INFO_DEV_INFO. */
   uint64_t va_start;
   uint64_t va_end;
   uint64_t va_next;
   /* The 32-bit-addressable sub-window. Every allocation made with
    * AMDGPU_VA_RANGE_32_BIT must share one high dword, because the hardware
    * takes it once (COMPUTE_PGM_HI and friends) rather than per-address —
    * `ac_cmdbuf.h` asserts exactly that. So a 4 GiB-aligned range is carved
    * out up front and `address32_hi` reports its high half. */
   uint64_t va32_base;
   uint64_t va32_next;
   uint64_t va32_end;
};

#define ETOS_VA32_SIZE (1ull << 32)

static struct ac_drm_device etos_dev;

/* A VA reservation. libdrm does this bookkeeping in userspace too — it makes
 * no ioctl — so it is ours to do, not the protocol's. */
struct amdgpu_va {
   uint64_t base;
   uint64_t size;
};

static int etos_query(uint32_t query, uint32_t arg0, uint32_t arg1, void *out,
                      uint32_t size)
{
   uint32_t written = 0;

   if (!out || size == 0)
      return -EINVAL;

   memset(out, 0, size);
   if (etos_amdgpu_query(query, arg0, arg1, (uint8_t *)out, size, &written) != 0)
      return -EINVAL;
   return 0;
}

/* ── sync provider ───────────────────────────────────────────────────────
 *
 * radeonsi asks the device for one and uses it for every syncobj operation.
 * These forward to the protocol's own syncobj methods, which are DRM's
 * `drm_syncobj.c` handlers on the far side.
 *
 * The fd-shaped operations (handle_to_fd, fd_to_handle, import/export
 * sync_file) stay unsupported: they exist to pass a sync object *between
 * processes* as a file descriptor, and etos shares things as capabilities
 * rather than fds. Bridging that properly is `SyncobjToFence` in
 * idl/amdgpu.idl — a real capability, not an fd number — and nothing asks
 * for it yet.
 */
static int sync_create(struct util_sync_provider *p, uint32_t flags, uint32_t *handle)
{ (void)p; return etos_amdgpu_syncobj_create(flags, handle) ? -EINVAL : 0; }
static int sync_destroy(struct util_sync_provider *p, uint32_t handle)
{ (void)p; return etos_amdgpu_syncobj_destroy(handle) ? -EINVAL : 0; }
static int sync_enotsup_handle_to_fd(struct util_sync_provider *p, uint32_t handle, int *fd)
{ (void)p; (void)handle; (void)fd; return -ENOTSUP; }
static int sync_enotsup_fd_to_handle(struct util_sync_provider *p, int fd, uint32_t *handle)
{ (void)p; (void)fd; (void)handle; return -ENOTSUP; }
static int sync_enotsup_import_sync_file(struct util_sync_provider *p, uint32_t h, int fd)
{ (void)p; (void)h; (void)fd; return -ENOTSUP; }
static int sync_enotsup_export_sync_file(struct util_sync_provider *p, uint32_t h, int *fd)
{ (void)p; (void)h; (void)fd; return -ENOTSUP; }
/* Mesa's timeout is an absolute deadline in nanoseconds; the protocol takes
 * a duration, so the far side can convert against its own clock rather than
 * this one having to agree with it. A deadline in the past is a poll. */
static uint64_t sync_timeout_to_duration(int64_t timeout_nsec)
{
   int64_t now = (int64_t)os_time_get_nano();
   return timeout_nsec > now ? (uint64_t)(timeout_nsec - now) : 0;
}

static int sync_wait(struct util_sync_provider *p, uint32_t *handles, unsigned n,
                     int64_t timeout, unsigned flags, uint32_t *first)
{
   (void)p;
   return etos_amdgpu_syncobj_wait(handles, n, sync_timeout_to_duration(timeout), flags,
                                   first)
             ? -EINVAL
             : 0;
}
static int sync_reset(struct util_sync_provider *p, const uint32_t *handles, uint32_t n)
{ (void)p; return etos_amdgpu_syncobj_reset(handles, n) ? -EINVAL : 0; }
static int sync_signal(struct util_sync_provider *p, const uint32_t *handles, uint32_t n)
{ (void)p; return etos_amdgpu_syncobj_signal(handles, n) ? -EINVAL : 0; }
static int sync_enotsup_timeline_signal(struct util_sync_provider *p, const uint32_t *handles,
                                        uint64_t *points, uint32_t n)
{ (void)p; (void)handles; (void)points; (void)n; return -ENOTSUP; }
static int sync_enotsup_timeline_wait(struct util_sync_provider *p, uint32_t *handles,
                                      uint64_t *points, unsigned n, int64_t timeout,
                                      unsigned flags, uint32_t *first)
{ (void)p; (void)handles; (void)points; (void)n; (void)timeout; (void)flags; (void)first;
  return -ENOTSUP; }
static int sync_query(struct util_sync_provider *p, uint32_t *handles, uint64_t *points,
                      uint32_t n, uint32_t flags)
{ (void)p; (void)flags; return etos_amdgpu_syncobj_query(handles, n, points) ? -EINVAL : 0; }
static int sync_transfer(struct util_sync_provider *p, uint32_t dh, uint64_t dp,
                         uint32_t sh, uint64_t sp, uint32_t flags)
{ (void)p; return etos_amdgpu_syncobj_transfer(dh, dp, sh, sp, flags) ? -EINVAL : 0; }
static void sync_noop_finalize(struct util_sync_provider *p) { (void)p; }
static struct util_sync_provider *sync_clone(struct util_sync_provider *p) { return p; }

static void etos_sync_init(struct util_sync_provider *p)
{
   p->create = sync_create;
   p->destroy = sync_destroy;
   p->handle_to_fd = sync_enotsup_handle_to_fd;
   p->fd_to_handle = sync_enotsup_fd_to_handle;
   p->import_sync_file = sync_enotsup_import_sync_file;
   p->export_sync_file = sync_enotsup_export_sync_file;
   p->wait = sync_wait;
   p->reset = sync_reset;
   p->signal = sync_signal;
   p->timeline_signal = sync_enotsup_timeline_signal;
   p->timeline_wait = sync_enotsup_timeline_wait;
   p->query = sync_query;
   p->transfer = sync_transfer;
   p->finalize = sync_noop_finalize;
   p->clone = sync_clone;
}

/* ── device ──────────────────────────────────────────────────────────────*/

int ac_drm_device_initialize(int fd, bool is_virtio, uint32_t *major_version,
                             uint32_t *minor_version, ac_drm_device **device_handle)
{
   struct drm_amdgpu_info_device dev = {0};

   (void)fd;        /* there is no fd on etos */
   if (is_virtio)
      return -ENOTSUP;

   if (etos_amdgpu_init() != 0)
      return -ENODEV;

   etos_sync_init(&etos_dev.sync);

   /* The VA window this process may allocate from. `ac_drm_va_range_alloc`
    * hands pieces of it out; amdgpu validates every address anyway, so a
    * bug here is rejected rather than silently mapped. */
   if (etos_query(AMDGPU_INFO_DEV_INFO, 0, 0, &dev, sizeof(dev)) == 0) {
      etos_dev.va_start = dev.virtual_address_offset;
      etos_dev.va_end = dev.virtual_address_max;
   }
   if (etos_dev.va_end <= etos_dev.va_start) {
      /* DEV_INFO did not answer. Rather than guess a window — a wrong one
       * produces addresses the GPU faults on, which reads as corruption —
       * refuse to initialise. */
      return -ENODEV;
   }
   /* Carve the 32-bit window out first, 4 GiB-aligned, and start general
    * allocations above it. Done here rather than lazily so `address32_hi`
    * has an answer before the first allocation asks for one. */
   etos_dev.va32_base =
      (etos_dev.va_start + ETOS_VA32_SIZE - 1) & ~(ETOS_VA32_SIZE - 1);
   etos_dev.va32_end = etos_dev.va32_base + ETOS_VA32_SIZE;
   if (etos_dev.va32_end > etos_dev.va_end) {
      /* A VM too small to hold a 4 GiB window at all. Not a configuration
       * this port targets (GFX10 reports 256 TiB), and quietly continuing
       * would hand out 32-bit addresses that do not share a high dword. */
      return -ENODEV;
   }
   etos_dev.va32_next = etos_dev.va32_base;
   etos_dev.va_next = etos_dev.va32_end;

   /* drm-kmod's KMS_DRIVER_{MAJOR,MINOR}. Mesa gates optional features on
    * the minor (`ac_gpu_info.c` requires >= 54, checks up to 60), so this
    * has to be the real one. There is no drm_version carrier in the
    * protocol to read it from yet. */
   if (major_version)
      *major_version = 3;
   if (minor_version)
      *minor_version = 59;
   if (device_handle)
      *device_handle = &etos_dev;
   return 0;
}

struct util_sync_provider *ac_drm_device_get_sync_provider(ac_drm_device *dev)
{
   return dev ? &dev->sync : NULL;
}

uintptr_t ac_drm_device_get_cookie(ac_drm_device *dev)
{
   /* Mesa uses this only to tell two devices apart. One per process here. */
   return (uintptr_t)dev;
}

void ac_drm_device_deinitialize(ac_drm_device *dev)
{
   /* The session closes with the process, and drmd runs amdgpu's postclose
    * when its capabilities drop — tearing down the VM and releasing the
    * BOs. Nothing to add. */
   (void)dev;
}

int ac_drm_device_get_fd(ac_drm_device *dev)
{
   /* There is no fd. Mesa passes this to things that would ioctl on it, all
    * of which are replaced here — a negative value makes any path that did
    * slip through fail loudly rather than act on fd 0. */
   (void)dev;
   return -1;
}

/* ── queries ─────────────────────────────────────────────────────────────*/

int ac_drm_query_info(ac_drm_device *dev, unsigned info_id, unsigned size, void *value)
{
   (void)dev;
   return etos_query(info_id, 0, 0, value, size);
}

int ac_drm_query_hw_ip_info(ac_drm_device *dev, unsigned type, unsigned ip_instance,
                            struct drm_amdgpu_info_hw_ip *info)
{
   (void)dev;
   return etos_query(AMDGPU_INFO_HW_IP_INFO, type, ip_instance, info, sizeof(*info));
}

int ac_drm_query_hw_ip_count(ac_drm_device *dev, unsigned type, uint32_t *count)
{
   (void)dev;
   return etos_query(AMDGPU_INFO_HW_IP_COUNT, type, 0, count, sizeof(*count));
}

int ac_drm_query_firmware_version(ac_drm_device *dev, unsigned fw_type, unsigned ip_instance,
                                  unsigned index, uint32_t *version, uint32_t *feature)
{
   struct drm_amdgpu_info_firmware fw = {0};
   int r;

   (void)dev;
   /* The union's per-query fields are all u32-shaped at the same offset,
    * which is why the protocol's `Query` takes two generic words instead of
    * modelling each query's struct. */
   r = etos_query(AMDGPU_INFO_FW_VERSION, fw_type, ip_instance | (index << 16), &fw,
                  sizeof(fw));
   if (r)
      return r;

   if (version)
      *version = fw.ver;
   if (feature)
      *feature = fw.feature;
   return 0;
}

int ac_drm_query_gpu_info(ac_drm_device *dev, struct amdgpu_gpu_info *info)
{
   struct drm_amdgpu_info_device d = {0};
   int r;

   (void)dev;
   if (!info)
      return -EINVAL;

   r = etos_query(AMDGPU_INFO_DEV_INFO, 0, 0, &d, sizeof(d));
   if (r)
      return r;

   memset(info, 0, sizeof(*info));
   info->asic_id = d.device_id;
   info->chip_rev = d.chip_rev;
   info->chip_external_rev = d.external_rev;
   info->family_id = d.family;
   info->ids_flags = d.ids_flags;
   info->max_engine_clk = d.max_engine_clock;
   info->max_memory_clk = d.max_memory_clock;
   info->num_shader_engines = d.num_shader_engines;
   info->num_shader_arrays_per_engine = d.num_shader_arrays_per_engine;
   info->num_hw_gfx_contexts = d.num_hw_gfx_contexts;
   info->enabled_rb_pipes_mask = d.enabled_rb_pipes_mask;
   info->gpu_counter_freq = d.gpu_counter_freq;
   info->vram_type = d.vram_type;
   info->vram_bit_width = d.vram_bit_width;
   info->ce_ram_size = d.ce_ram_size;
   info->vce_harvest_config = d.vce_harvest_config;
   info->pci_rev_id = d.pci_rev;

   /* Left zero, deliberately: `gb_addr_cfg`, `mc_arb_ramcfg`, the tile-mode
    * arrays, `num_tile_pipes` and `pipe_interleave_bytes` are not in
    * `drm_amdgpu_info_device` at all — libdrm derives them from MMIO reads
    * (AMDGPU_INFO_READ_MMR_REG). Mesa consults them only through
    * `ac_fill_tiling_info`, which is pre-GFX9 tiling, and this port targets
    * GFX10.
    *
    * Called out rather than left silent because a zero here would produce
    * *wrong tiling* rather than an error if a pre-GFX9 ASIC ever came into
    * scope. `ac_drm_read_mm_registers` below is the route to filling them
    * when that day comes. */
   return 0;
}

int ac_drm_query_heap_info(ac_drm_device *dev, uint32_t heap, uint32_t flags,
                           struct amdgpu_heap_info *info)
{
   struct drm_amdgpu_memory_info mem = {0};
   int r;

   (void)dev;
   if (!info)
      return -EINVAL;

   r = etos_query(AMDGPU_INFO_MEMORY, 0, 0, &mem, sizeof(mem));
   if (r)
      return r;

   memset(info, 0, sizeof(*info));
   switch (heap) {
   case AMDGPU_GEM_DOMAIN_VRAM:
      if (flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED) {
         info->heap_size = mem.cpu_accessible_vram.total_heap_size;
         info->heap_usage = mem.cpu_accessible_vram.heap_usage;
         info->max_allocation = mem.cpu_accessible_vram.max_allocation;
      } else {
         info->heap_size = mem.vram.total_heap_size;
         info->heap_usage = mem.vram.heap_usage;
         info->max_allocation = mem.vram.max_allocation;
      }
      break;
   case AMDGPU_GEM_DOMAIN_GTT:
      info->heap_size = mem.gtt.total_heap_size;
      info->heap_usage = mem.gtt.heap_usage;
      info->max_allocation = mem.gtt.max_allocation;
      break;
   default:
      return -EINVAL;
   }
   return 0;
}

int ac_drm_query_pci_bus_info(ac_drm_device *dev, struct radeon_info *info)
{
   /* libdrm fills this from the DRM device node's sysfs path. etos has
    * neither, and the winsys only uses it for reporting — so it is left
    * untouched rather than invented. */
   (void)dev;
   (void)info;
   return -ENOTSUP;
}

void ac_drm_query_has_vm_always_valid(ac_drm_device *dev, struct radeon_info *info)
{
   /* Detected by attempting a VM_ALWAYS_VALID allocation on libdrm. Left
    * alone: the caller's default (not available) is the safe reading, and a
    * wrong `true` here would have the winsys skip adding BOs to submissions
    * that genuinely need to be there. */
   (void)dev;
   (void)info;
}

int ac_drm_query_sw_info(ac_drm_device *dev, enum amdgpu_sw_info info, void *value)
{
   /* Answered from this backend's own VA bookkeeping, exactly as libdrm
    * answers it from its own — there is no kernel query behind this. */
   if (!dev || !value)
      return -EINVAL;

   switch (info) {
   case amdgpu_sw_info_address32_hi:
      /* The high dword every AMDGPU_VA_RANGE_32_BIT allocation shares. */
      *(uint32_t *)value = (uint32_t)(dev->va32_base >> 32);
      return 0;
   case amdgpu_sw_info_address_prt_wa_control_bit:
      /* A per-ASIC workaround bit for partially-resident textures. Sparse
       * residency is not offered here, so there is nothing to work around. */
      *(uint32_t *)value = 0;
      return 0;
   default:
      return -EINVAL;
   }
}

int ac_drm_query_sensor_info(ac_drm_device *dev, unsigned sensor_type, unsigned size, void *value)
{
   (void)dev;
   return etos_query(AMDGPU_INFO_SENSOR, sensor_type, 0, value, size);
}

int ac_drm_query_video_caps_info(ac_drm_device *dev, unsigned cap_type, unsigned size, void *value)
{
   /* Video decode/encode is compiled out of drmd (VCN/UVD/JPEG are dormant),
    * so there is nothing behind this. */
   (void)dev; (void)cap_type; (void)size; (void)value;
   return -ENOTSUP;
}

int ac_drm_query_gpuvm_fault_info(ac_drm_device *dev, unsigned size, void *value)
{
   (void)dev;
   return etos_query(AMDGPU_INFO_GPUVM_FAULT, 0, 0, value, size);
}

int ac_drm_query_uq_fw_area_info(ac_drm_device *dev, unsigned type, unsigned ip_instance,
                                 struct drm_amdgpu_info_uq_metadata *info)
{
   /* User queues are not offered — see ac_drm_create_userqueue. */
   (void)dev; (void)type; (void)ip_instance; (void)info;
   return -ENOTSUP;
}

int ac_drm_read_mm_registers(ac_drm_device *dev, unsigned dword_offset, unsigned count,
                             uint32_t instance, uint32_t flags, uint32_t *values)
{
   /* AMDGPU_INFO_READ_MMR_REG. Needed only for pre-GFX9 tiling info (see
    * ac_drm_query_gpu_info); routed through the protocol's Query rather than
    * stubbed, since it is a plain query and costs nothing to support. */
   (void)dev;
   if (!values || count == 0)
      return -EINVAL;
   return etos_query(AMDGPU_INFO_READ_MMR_REG, dword_offset, instance | (flags << 16),
                     values, count * sizeof(uint32_t));
}

const char *ac_drm_get_marketing_name(ac_drm_device *device)
{
   /* libdrm reads this from its bundled amdgpu.ids table, which etos does
    * not ship. NULL is a value the caller already handles (it falls back to
    * the chip name from DEV_INFO). */
   (void)device;
   return NULL;
}

/* ── buffer objects ──────────────────────────────────────────────────────
 *
 * `ac_drm_bo` is a union of backend-private handles. Ours is the GEM handle
 * the protocol uses, carried in the pointer field — the same trick the
 * virtio backend plays with its own type.
 */
static inline uint32_t bo_handle(ac_drm_bo bo) { return (uint32_t)(uintptr_t)bo.abo; }
static inline ac_drm_bo bo_from_handle(uint32_t h)
{
   ac_drm_bo bo;
   memset(&bo, 0, sizeof(bo));
   bo.abo = (amdgpu_bo_handle)(uintptr_t)h;
   return bo;
}

int ac_drm_bo_alloc(ac_drm_device *dev, struct amdgpu_bo_alloc_request *alloc_buffer,
                    ac_drm_bo *bo)
{
   uint32_t handle = 0;

   (void)dev;
   if (!alloc_buffer || !bo)
      return -EINVAL;

   if (etos_amdgpu_bo_alloc(alloc_buffer->alloc_size, alloc_buffer->phys_alignment,
                            alloc_buffer->preferred_heap, alloc_buffer->flags,
                            &handle) != 0) {
      fprintf(stderr, "etos: bo_alloc failed: size=%llu heap=%x flags=%llx\n",
              (unsigned long long)alloc_buffer->alloc_size,
              alloc_buffer->preferred_heap,
              (unsigned long long)alloc_buffer->flags);
      return -ENOMEM;
   }

   *bo = bo_from_handle(handle);
   return 0;
}

int ac_drm_bo_free(ac_drm_device *dev, ac_drm_bo bo)
{
   (void)dev;
   etos_amdgpu_bo_free(bo_handle(bo));
   return 0;
}

int ac_drm_bo_cpu_map(ac_drm_device *dev, ac_drm_bo bo, void **cpu)
{
   void *p;

   (void)dev;
   if (!cpu)
      return -EINVAL;

   p = etos_amdgpu_bo_map(bo_handle(bo));
   if (!p)
      return -ENOMEM;
   *cpu = p;
   return 0;
}

int ac_drm_bo_cpu_unmap(ac_drm_device *dev, ac_drm_bo bo)
{
   (void)dev;
   etos_amdgpu_bo_unmap(bo_handle(bo));
   return 0;
}

int ac_drm_bo_query_info(ac_drm_device *dev, uint32_t bo_handle_in, struct amdgpu_bo_info *info)
{
   /* Only called for imported BOs, to learn an allocation this process did
    * not make. Import is not supported (see ac_drm_bo_import), so neither is
    * this — a zeroed struct would read as a zero-sized allocation. */
   (void)dev; (void)bo_handle_in; (void)info;
   return -ENOTSUP;
}

int ac_drm_bo_set_metadata(ac_drm_device *dev, uint32_t bo_handle_in,
                           struct amdgpu_bo_metadata *info)
{
   /* Tiling metadata for the display path, read back by whoever imports the
    * BO. Without import there is no reader. */
   (void)dev; (void)bo_handle_in; (void)info;
   return -ENOTSUP;
}

int ac_drm_bo_wait_for_idle(ac_drm_device *dev, ac_drm_bo bo, uint64_t timeout_ns, bool *busy)
{
   /* DRM_AMDGPU_GEM_WAIT_IDLE. The protocol has no per-BO idle wait — a
    * submission is waited on through its fence (`ac_drm_cs_query_fence_status`),
    * which is what the winsys uses for everything except this one
    * convenience path. */
   (void)dev; (void)bo; (void)timeout_ns;
   if (busy)
      *busy = false;
   return -ENOTSUP;
}

int ac_drm_bo_export(ac_drm_device *dev, ac_drm_bo bo, enum amdgpu_bo_handle_type type,
                     uint32_t *shared_handle)
{
   (void)dev;
   if (!shared_handle)
      return -EINVAL;

   switch (type) {
   case amdgpu_bo_handle_type_kms:
   case amdgpu_bo_handle_type_kms_noimport:
      /* Not "sharing" at all despite the name: this asks for the BO's GEM
       * handle within *this* session, which is what the VA operations and
       * the command stream refer to. On etos an `ac_drm_bo` carries exactly
       * that handle, so this is a read.
       *
       * It matters more than it looks. The winsys takes this route to learn
       * the handle for every buffer it maps, so returning an error here left
       * it passing 0 to bo_va_op — every VA map failed, and the first thing
       * that noticed was IB allocation, several layers away from the cause. */
      *shared_handle = bo_handle(bo);
      return 0;

   case amdgpu_bo_handle_type_dma_buf_fd:
   case amdgpu_bo_handle_type_gem_flink_name:
      /* These are the real cross-process ones, and etos has no fds or flink
       * names to hand out — sharing a buffer here means passing the
       * `AmdgpuBo` capability itself, a design question deferred in
       * idl/amdgpu.idl's header rather than a translation. */
      return -ENOTSUP;

   default:
      return -EINVAL;
   }
}

int ac_drm_bo_import(ac_drm_device *dev, enum amdgpu_bo_handle_type type,
                     uint32_t shared_handle, struct ac_drm_bo_import_result *output)
{
   (void)dev; (void)type; (void)shared_handle; (void)output;
   return -ENOTSUP;
}

int ac_drm_create_bo_from_user_mem(ac_drm_device *dev, void *cpu, uint64_t size, ac_drm_bo *bo)
{
   /* Userptr BOs: pin this process's own pages and let the GPU read them.
    * Needs `amdgpu_gem_userptr_ioctl` plus a way to name a client's pages to
    * drmd, which the protocol has no method for. */
   (void)dev; (void)cpu; (void)size; (void)bo;
   return -ENOTSUP;
}

/* ── GPU virtual address space ───────────────────────────────────────────*/

int ac_drm_bo_va_op(ac_drm_device *dev, uint32_t bo_handle_in, uint64_t offset, uint64_t size,
                    uint64_t addr, uint64_t flags, uint32_t ops)
{
   /* The non-raw form adds the default page flags libdrm applies. */
   return ac_drm_bo_va_op_raw(dev, bo_handle_in, offset, size, addr,
                              flags | AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE |
                                 AMDGPU_VM_PAGE_EXECUTABLE,
                              ops);
}

int ac_drm_bo_va_op_raw(ac_drm_device *dev, uint32_t bo_handle_in, uint64_t offset, uint64_t size,
                        uint64_t addr, uint64_t flags, uint32_t ops)
{
   (void)dev;
   if (etos_amdgpu_va_op(bo_handle_in, ops, addr, offset, size, flags) != 0) {
      fprintf(stderr, "etos: va_op failed: handle=%u ops=%u addr=%llx size=%llu\n",
              bo_handle_in, ops, (unsigned long long)addr, (unsigned long long)size);
      return -EINVAL;
   }
   return 0;
}

/*
 * The syncobj-carrying VA operation, emulated.
 *
 * Newer amdgpu UAPI lets a VA operation take input fences and signal an
 * output timeline point, so the VM update can be queued and ordered against
 * other work. The amdgpu this build runs (drm-kmod, KMS 3.59) has none of
 * those fields in `drm_amdgpu_gem_va`, so the three steps are done here
 * instead: wait the input fences, perform the mapping, signal the output
 * timeline.
 *
 * That is only correct because `VaOp` is *synchronous* — the mapping is in
 * the page tables by the time it returns (idl/amdgpu.idl says so, and it is
 * the ioctl's own behaviour), so there is nothing still in flight when the
 * timeline is signalled. If the VA path ever becomes asynchronous, this
 * becomes a lie that presents as a GPU reading a mapping that does not
 * exist yet, so it is worth stating plainly here.
 *
 * Not optional, either: Mesa hands a VM timeline syncobj to *every* VA
 * operation (amdgpu_bo.c) and then makes that timeline a dependency of the
 * submission using the mapping (amdgpu_cs.cpp). Rejecting the call, which is
 * what this did before, left the submission waiting on a point nothing ever
 * signalled — a hang rather than an error.
 */
int ac_drm_bo_va_op_raw2(ac_drm_device *dev, uint32_t bo_handle_in, uint64_t offset, uint64_t size,
                         uint64_t addr, uint64_t flags, uint32_t ops,
                         uint32_t vm_timeline_syncobj_out, uint64_t vm_timeline_point,
                         uint64_t input_fence_syncobj_handles, uint32_t num_syncobj_handles)
{
   int r;

   /* Wait for whatever the caller says must land first. A zero timeout
    * would poll; this blocks, because the mapping below must not be applied
    * until these have completed. */
   if (num_syncobj_handles && input_fence_syncobj_handles) {
      const uint32_t *fences = (const uint32_t *)(uintptr_t)input_fence_syncobj_handles;
      uint32_t first = 0;

      r = etos_amdgpu_syncobj_wait(fences, num_syncobj_handles, UINT64_MAX,
                                   DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, &first);
      if (r) {
         fprintf(stderr, "etos: va_op input-fence wait failed (%u fences)\n",
                 num_syncobj_handles);
         return -EINVAL;
      }
   }

   r = ac_drm_bo_va_op_raw(dev, bo_handle_in, offset, size, addr, flags, ops);
   if (r)
      return r;

   if (vm_timeline_syncobj_out) {
      r = etos_amdgpu_syncobj_timeline_signal(&vm_timeline_syncobj_out, &vm_timeline_point, 1);
      if (r) {
         fprintf(stderr, "etos: va_op timeline signal failed: syncobj=%u point=%llu\n",
                 vm_timeline_syncobj_out, (unsigned long long)vm_timeline_point);
         return -EINVAL;
      }
   }
   return 0;
}

int ac_drm_va_range_alloc(ac_drm_device *dev, enum amdgpu_gpu_va_range va_range_type,
                          uint64_t size, uint64_t va_base_alignment, uint64_t va_base_required,
                          uint64_t *va_base_allocated, amdgpu_va_handle *va_range_handle,
                          uint64_t flags)
{
   struct amdgpu_va *va;
   uint64_t base;
   if (!dev || !va_base_allocated || !va_range_handle || size == 0)
      return -EINVAL;
   if (va_range_type != amdgpu_gpu_va_range_general)
      return -ENOTSUP;

   /* Bump allocation, never reused. libdrm keeps a real free-list here; this
    * does not, and the general window is most of a 256 TiB address space
    * (`virtual_address_max`), so a process would have to churn an
    * implausible number of allocations to exhaust it. Worth revisiting if a
    * long-running compositor ever does — and note the 32-bit window below is
    * only 4 GiB, so it is the one that would run out first. */
   bool want32 = (flags & AMDGPU_VA_RANGE_32_BIT) != 0;
   uint64_t *next = want32 ? &etos_dev.va32_next : &etos_dev.va_next;
   uint64_t lo = want32 ? etos_dev.va32_base : etos_dev.va_start;
   uint64_t hi = want32 ? etos_dev.va32_end : etos_dev.va_end;

   base = va_base_required ? va_base_required : *next;
   if (va_base_alignment > 1)
      base = (base + va_base_alignment - 1) & ~(va_base_alignment - 1);

   if (base < lo || base + size > hi) {
      fprintf(stderr,
              "etos: va_range_alloc out of room: want32=%d size=%llu base=%llx "
              "window=[%llx,%llx)\n",
              (int)want32, (unsigned long long)size, (unsigned long long)base,
              (unsigned long long)lo, (unsigned long long)hi);
      return -ENOMEM;
   }

   va = calloc(1, sizeof(*va));
   if (!va)
      return -ENOMEM;
   va->base = base;
   va->size = size;

   if (!va_base_required)
      *next = base + size;

   *va_base_allocated = base;
   *va_range_handle = va;
   return 0;
}

uint64_t amdgpu_va_get_start_addr(amdgpu_va_handle va)
{
   return va ? va->base : 0;
}

int ac_drm_va_range_free(amdgpu_va_handle va_range_handle)
{
   free(va_range_handle);
   return 0;
}

/* The winsys calls libdrm's name for this directly. Same operation. */
int amdgpu_va_range_free(amdgpu_va_handle va_range_handle)
{
   return ac_drm_va_range_free(va_range_handle);
}

int ac_drm_va_range_query(ac_drm_device *dev, enum amdgpu_gpu_va_range type, uint64_t *start,
                          uint64_t *end)
{
   if (!dev || type != amdgpu_gpu_va_range_general)
      return -EINVAL;
   if (start)
      *start = dev->va_start;
   if (end)
      *end = dev->va_end;
   return 0;
}

/* ── contexts and submission ─────────────────────────────────────────────*/

int ac_drm_cs_ctx_create2(ac_drm_device *dev, uint32_t priority, uint32_t *ctx_id)
{
   (void)dev;
   if (!ctx_id)
      return -EINVAL;
   if (etos_amdgpu_ctx_create((int32_t)priority, ctx_id) != 0)
      return -ENOMEM;
   return 0;
}

int ac_drm_cs_ctx_free(ac_drm_device *dev, uint32_t ctx_id)
{
   (void)dev;
   if (etos_amdgpu_ctx_free(ctx_id) != 0)
      return -EINVAL;
   return 0;
}

int ac_drm_cs_ctx_stable_pstate(ac_drm_device *dev, uint32_t ctx_id, uint32_t op, uint32_t flags,
                                uint32_t *out_flags)
{
   /* Pins clocks for profiling. Nothing behind it here. */
   (void)dev; (void)ctx_id; (void)op; (void)flags;
   if (out_flags)
      *out_flags = 0;
   return -ENOTSUP;
}

int ac_drm_cs_query_reset_state2(ac_drm_device *dev, uint32_t ctx_id, uint64_t *flags)
{
   /* GPU reset / robustness reporting. drmd has no reset path yet — a hung
    * client's hang takes the card with it (AmdgpuAccelDesign.md §7), so
    * there is nothing truthful to report. */
   (void)dev; (void)ctx_id;
   if (flags)
      *flags = 0;
   return -ENOTSUP;
}

int ac_drm_cs_query_fence_status(ac_drm_device *dev, uint32_t ctx_id, uint32_t ip_type,
                                 uint32_t ip_instance, uint32_t ring, uint64_t fence_seq_no,
                                 uint64_t timeout_ns, uint64_t flags, uint32_t *expired)
{
   uint32_t signalled = 0;

   (void)dev; (void)flags;
   if (!expired)
      return -EINVAL;

   if (etos_amdgpu_wait_cs(ctx_id, ip_type, ip_instance, ring, fence_seq_no, timeout_ns,
                           &signalled) != 0)
      return -EINVAL;

   /* "expired" means completed, which is what `signalled` reports. A timeout
    * is a normal result on both sides, not an error. */
   *expired = signalled;
   return 0;
}

/* Bytes the entry array of a BO_HANDLES chunk occupies. Zero if the chunk
 * is too short to hold a `drm_amdgpu_bo_list_in` at all — a malformed chunk
 * drmd will reject, which is where that belongs. */
static uint32_t bo_list_entries_len(const struct drm_amdgpu_cs_chunk *chunk)
{
   const struct drm_amdgpu_bo_list_in *bl;

   if (chunk->length_dw * 4 < sizeof(struct drm_amdgpu_bo_list_in))
      return 0;
   bl = (const struct drm_amdgpu_bo_list_in *)(uintptr_t)chunk->chunk_data;
   return bl->bo_number * bl->bo_info_size;
}

/*
 * Flatten Mesa's chunk array into the payload idl/amdgpu.idl describes and
 * drivers/drmd/kpi/session.c parses:
 *
 *   u32 num_chunks; u32 _pad;
 *   struct drm_amdgpu_cs_chunk chunks[n];   // chunk_data = byte offset
 *   ... chunk data blobs ...
 *
 * The ioctl's `chunk_data` is a pointer into this process's address space,
 * which means nothing to drmd, so it becomes an offset into the same
 * payload. Everything a chunk *names* — BO handles, GPU virtual addresses,
 * syncobj handles — is already session-relative and crosses unchanged.
 *
 * One chunk needs the same treatment a second level down, and every real
 * submission carries it: `AMDGPU_CHUNK_ID_BO_HANDLES`, whose payload is a
 * `struct drm_amdgpu_bo_list_in` pointing at the array of BO list entries
 * through `bo_info_ptr`. That array is appended to the payload too, and
 * `bo_info_ptr` becomes its byte offset — the same rule, so drmd applies
 * the same rewrite. (The alternative, an explicit BoList object in the
 * protocol, is what idl/amdgpu.idl deliberately does not have: the list is
 * per-submission state, not a capability, and inlining it keeps a
 * submission one RPC.)
 */
int ac_drm_cs_submit_raw2(ac_drm_device *dev, uint32_t ctx_id, uint32_t bo_list_handle,
                          int num_chunks, struct drm_amdgpu_cs_chunk *chunks, uint64_t *seq_no)
{
   uint8_t *payload;
   const uint32_t header_len = 8;
   uint32_t table_len, total, off;
   struct drm_amdgpu_cs_chunk *wire;
   int i, r;

   (void)dev;
   (void)bo_list_handle; /* always 0 — see idl/amdgpu.idl on BO lists */

   if (num_chunks <= 0 || !chunks || !seq_no)
      return -EINVAL;

   table_len = (uint32_t)num_chunks * (uint32_t)sizeof(struct drm_amdgpu_cs_chunk);
   total = header_len + table_len;
   for (i = 0; i < num_chunks; i++) {
      /* Each blob padded to 8, so the payload layout does not depend on
       * chunk ordering. drmd itself only requires dword alignment. */
      total += (chunks[i].length_dw * 4 + 7) & ~7u;
      if (chunks[i].chunk_id == AMDGPU_CHUNK_ID_BO_HANDLES)
         total += (bo_list_entries_len(&chunks[i]) + 7) & ~7u;
   }

   payload = calloc(1, total);
   if (!payload)
      return -ENOMEM;

   ((uint32_t *)payload)[0] = (uint32_t)num_chunks;
   wire = (struct drm_amdgpu_cs_chunk *)(payload + header_len);

   off = header_len + table_len;
   for (i = 0; i < num_chunks; i++) {
      uint32_t len = chunks[i].length_dw * 4;

      wire[i].chunk_id = chunks[i].chunk_id;
      wire[i].length_dw = chunks[i].length_dw;
      wire[i].chunk_data = off;
      memcpy(payload + off, (const void *)(uintptr_t)chunks[i].chunk_data, len);
      off += (len + 7) & ~7u;

      if (chunks[i].chunk_id == AMDGPU_CHUNK_ID_BO_HANDLES) {
         const struct drm_amdgpu_bo_list_in *src =
            (const struct drm_amdgpu_bo_list_in *)(uintptr_t)chunks[i].chunk_data;
         struct drm_amdgpu_bo_list_in *dst =
            (struct drm_amdgpu_bo_list_in *)(payload + wire[i].chunk_data);
         uint32_t entries_len = bo_list_entries_len(&chunks[i]);

         memcpy(payload + off, (const void *)(uintptr_t)src->bo_info_ptr, entries_len);
         dst->bo_info_ptr = off;
         off += (entries_len + 7) & ~7u;
      }
   }

   r = etos_amdgpu_submit(ctx_id, payload, total, seq_no);
   free(payload);
   return r == 0 ? 0 : -EINVAL;
}

void ac_drm_cs_chunk_fence_info_to_data(uint32_t bo_handle_in, uint64_t offset,
                                        struct drm_amdgpu_cs_chunk_data *data)
{
   /* Pure marshalling, identical to libdrm's — no device involved. */
   memset(data, 0, sizeof(*data));
   data->fence_data.handle = bo_handle_in;
   data->fence_data.offset = offset * sizeof(uint64_t);
}

/* ── syncobjs ────────────────────────────────────────────────────────────
 *
 * All unsupported, matching the sync provider above and the protocol itself
 * (drivers/drmd/src/amdgpu/session.rs). Explicit synchronisation is a later
 * milestone; reporting it half-working would be worse than not at all,
 * because the winsys would build dependency chains nothing enforces.
 */
int ac_drm_cs_create_syncobj2(ac_drm_device *dev, uint32_t flags, uint32_t *handle)
{
   (void)dev;
   if (!handle)
      return -EINVAL;
   return etos_amdgpu_syncobj_create(flags, handle) ? -EINVAL : 0;
}

int ac_drm_cs_destroy_syncobj(ac_drm_device *dev, uint32_t handle)
{
   (void)dev;
   return etos_amdgpu_syncobj_destroy(handle) ? -EINVAL : 0;
}

int ac_drm_cs_syncobj_wait(ac_drm_device *dev, uint32_t *handles, unsigned num_handles,
                           int64_t timeout_nsec, unsigned flags, uint32_t *first_signaled)
{
   (void)dev;
   return etos_amdgpu_syncobj_wait(handles, num_handles,
                                   sync_timeout_to_duration(timeout_nsec), flags,
                                   first_signaled)
             ? -EINVAL
             : 0;
}

int ac_drm_cs_syncobj_query2(ac_drm_device *dev, uint32_t *handles, uint64_t *points,
                             unsigned num_handles, uint32_t flags)
{
   (void)dev;
   (void)flags;
   return etos_amdgpu_syncobj_query(handles, num_handles, points) ? -EINVAL : 0;
}

int ac_drm_cs_syncobj_transfer(ac_drm_device *dev, uint32_t dst_handle, uint64_t dst_point,
                               uint32_t src_handle, uint64_t src_point, uint32_t flags)
{
   (void)dev;
   return etos_amdgpu_syncobj_transfer(dst_handle, dst_point, src_handle, src_point, flags)
             ? -EINVAL
             : 0;
}

int ac_drm_cs_syncobj_timeline_wait(ac_drm_device *dev, uint32_t *handles, uint64_t *points,
                                    unsigned num_handles, int64_t timeout_nsec, unsigned flags,
                                    uint32_t *first_signaled)
{
   /* Timeline waits need drm_syncobj_timeline_wait_ioctl, which takes the
    * per-handle points array the binary wait has no room for. The protocol
    * carries only the binary form today; adding it is a method, not a
    * redesign. Unsupported rather than silently waiting on the wrong thing:
    * treating a timeline wait as binary would return as soon as the object
    * had *any* value, not the one asked for. */
   (void)dev; (void)handles; (void)points; (void)num_handles; (void)timeout_nsec;
   (void)flags; (void)first_signaled;
   return -ENOTSUP;
}

/* The fd-shaped operations. These exist to pass a sync object between
 * processes as a file descriptor; etos shares capabilities instead, and
 * idl/amdgpu.idl's SyncobjToFence is the bridge when something needs one. */
int ac_drm_cs_import_syncobj(ac_drm_device *dev, int shared_fd, uint32_t *handle)
{ (void)dev; (void)shared_fd; (void)handle; return -ENOTSUP; }
int ac_drm_cs_syncobj_export_sync_file(ac_drm_device *dev, uint32_t syncobj, int *sync_file_fd)
{ (void)dev; (void)syncobj; (void)sync_file_fd; return -ENOTSUP; }
int ac_drm_cs_syncobj_import_sync_file(ac_drm_device *dev, uint32_t syncobj, int sync_file_fd)
{ (void)dev; (void)syncobj; (void)sync_file_fd; return -ENOTSUP; }
int ac_drm_cs_syncobj_export_sync_file2(ac_drm_device *dev, uint32_t syncobj, uint64_t point,
                                        uint32_t flags, int *sync_file_fd)
{ (void)dev; (void)syncobj; (void)point; (void)flags; (void)sync_file_fd; return -ENOTSUP; }

/* ── user queues ─────────────────────────────────────────────────────────
 *
 * The modern submission path. Not offered: the winsys only uses it when the
 * kernel advertises support, and this one does not.
 */
int ac_drm_create_userqueue(ac_drm_device *dev, uint32_t ip_type, uint32_t doorbell_handle,
                            uint32_t doorbell_offset, uint64_t queue_va, uint64_t queue_size,
                            uint64_t wptr_va, uint64_t rptr_va, void *mqd_in, uint32_t flags,
                            uint32_t *queue_id)
{ (void)dev; (void)ip_type; (void)doorbell_handle; (void)doorbell_offset; (void)queue_va;
  (void)queue_size; (void)wptr_va; (void)rptr_va; (void)mqd_in; (void)flags; (void)queue_id;
  return -ENOTSUP; }
int ac_drm_free_userqueue(ac_drm_device *dev, uint32_t queue_id)
{ (void)dev; (void)queue_id; return -ENOTSUP; }
int ac_drm_userq_signal(ac_drm_device *dev, struct drm_amdgpu_userq_signal *signal_data)
{ (void)dev; (void)signal_data; return -ENOTSUP; }
int ac_drm_userq_wait(ac_drm_device *dev, struct drm_amdgpu_userq_wait *wait_data)
{ (void)dev; (void)wait_data; return -ENOTSUP; }

int ac_drm_vm_reserve_vmid(ac_drm_device *dev, uint32_t flags)
{ (void)dev; (void)flags; return -ENOTSUP; }
int ac_drm_vm_unreserve_vmid(ac_drm_device *dev, uint32_t flags)
{ (void)dev; (void)flags; return -ENOTSUP; }
