/*
 * Copyright 2026 The etos authors
 * SPDX-License-Identifier: MIT
 *
 * See ac_drm_etos.h for what this is and why it exists.
 */

#include "ac_drm_etos.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * `drm_amdgpu_info` in one call. The protocol passes the reply through as
 * the UAPI struct for whichever query was asked, verbatim (idl/amdgpu.idl),
 * so this is a thin size-checked wrapper rather than a translation.
 */
static int etos_query(uint32_t query, uint32_t arg0, uint32_t arg1, void *out,
                      uint32_t size)
{
   uint32_t written = 0;

   if (!out || size == 0)
      return -EINVAL;

   memset(out, 0, size);
   if (etos_amdgpu_query(query, arg0, arg1, (uint8_t *)out, size, &written) != 0)
      return -EINVAL;

   /* A short reply means the driver's struct for this query is smaller than
    * the one Mesa compiled against — a real UAPI skew, not something to
    * paper over by leaving the tail as whatever was on the stack. The
    * memset above has already zeroed it, which is what the kernel would
    * have done for a field it does not know about. */
   return 0;
}

int ac_drm_etos_device_initialize(uint32_t *major, uint32_t *minor)
{
   if (etos_amdgpu_init() != 0)
      return -ENODEV;

   /* The DRM interface version drmd's amdgpu reports. Mesa gates optional
    * features on the minor (`ac_gpu_info.c` requires >= 54 and checks up to
    * 60), so this has to be the real one rather than a placeholder —
    * reported by the driver itself via AMDGPU_INFO_... would be better, but
    * there is no query for it; it is the drm_version, which on etos has no
    * carrier yet. 3.59 is what 3rd-party/drm-kmod's KMS_DRIVER_MINOR says.
    */
   if (major)
      *major = 3;
   if (minor)
      *minor = 59;
   return 0;
}

void ac_drm_etos_device_deinitialize(void)
{
   /* The session closes with the process. Nothing to do here that dropping
    * the capabilities does not already do — and drmd runs amdgpu's
    * `postclose` when it does, tearing down the VM and releasing the BOs. */
}

int ac_drm_etos_query_info(unsigned info_id, unsigned size, void *value)
{
   return etos_query(info_id, 0, 0, value, size);
}

int ac_drm_etos_query_hw_ip_info(unsigned type, unsigned ip_instance,
                                 struct drm_amdgpu_info_hw_ip *info)
{
   return etos_query(AMDGPU_INFO_HW_IP_INFO, type, ip_instance, info,
                     sizeof(*info));
}

int ac_drm_etos_query_hw_ip_count(unsigned type, uint32_t *count)
{
   return etos_query(AMDGPU_INFO_HW_IP_COUNT, type, 0, count, sizeof(*count));
}

int ac_drm_etos_query_firmware_version(unsigned fw_type, unsigned ip_instance,
                                       unsigned index, uint32_t *version,
                                       uint32_t *feature)
{
   struct drm_amdgpu_info_firmware fw = {0};
   int r;

   /* `index` rides in the same word as the IP instance for this query — the
    * union's per-query fields are all u32-shaped and start at the same
    * offset, which is why the protocol's `Query` takes two generic args
    * rather than modelling each query's own struct. */
   r = etos_query(AMDGPU_INFO_FW_VERSION, fw_type, ip_instance | (index << 16),
                  &fw, sizeof(fw));
   if (r)
      return r;

   if (version)
      *version = fw.ver;
   if (feature)
      *feature = fw.feature;
   return 0;
}

int ac_drm_etos_query_gpu_info(struct amdgpu_gpu_info *info)
{
   struct drm_amdgpu_info_device dev = {0};
   int r;

   if (!info)
      return -EINVAL;

   r = etos_query(AMDGPU_INFO_DEV_INFO, 0, 0, &dev, sizeof(dev));
   if (r)
      return r;

   memset(info, 0, sizeof(*info));
   info->asic_id = dev.device_id;
   info->chip_rev = dev.chip_rev;
   info->chip_external_rev = dev.external_rev;
   info->family_id = dev.family;
   info->ids_flags = dev.ids_flags;
   info->max_engine_clk = dev.max_engine_clock;
   info->max_memory_clk = dev.max_memory_clock;
   info->num_shader_engines = dev.num_shader_engines;
   info->num_shader_arrays_per_engine = dev.num_shader_arrays_per_engine;
   info->cache_size = dev.gs_vgt_table_depth;
   info->num_tile_pipes = dev.num_tile_pipes;
   info->pipe_interleave_bytes = dev.pipe_interleave_size;
   info->num_hw_gfx_contexts = dev.num_hw_gfx_contexts;
   info->enabled_rb_pipes_mask = dev.enabled_rb_pipes_mask;
   info->gpu_counter_freq = dev.gpu_counter_freq;
   info->vram_type = dev.vram_type;
   info->vram_bit_width = dev.vram_bit_width;
   info->ce_ram_size = dev.ce_ram_size;
   info->vce_harvest_config = dev.vce_harvest_config;
   info->pci_rev_id = dev.pci_rev;

   /* The two register-derived fields Mesa actually reads. */
   info->gb_addr_cfg = dev.gb_addr_cfg;

   /* `mc_arb_ramcfg` and the tile-mode arrays come from MMIO reads
    * (AMDGPU_INFO_READ_MMR_REG) in libdrm. Left zero here: `ac_gpu_info.c`
    * only consults them through `ac_fill_tiling_info`, which is pre-GFX9
    * tiling, and this port targets GFX10. If a pre-GFX9 ASIC is ever in
    * scope, this is the place that has to grow a register-read query — a
    * zero here would silently produce wrong tiling rather than an error,
    * so it is called out rather than left to be discovered.
    */
   return 0;
}

int ac_drm_etos_query_heap_info(uint32_t heap, uint32_t flags,
                                struct amdgpu_heap_info *info)
{
   struct drm_amdgpu_memory_info mem = {0};
   int r;

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

int ac_drm_etos_query_sw_info(unsigned info, void *value)
{
   /* libdrm answers exactly one of these (`amdgpu_sw_info_address32_hi`)
    * from state it keeps itself, not from the kernel. The winsys uses it to
    * place 32-bit-addressable allocations. Reported as unsupported rather
    * than guessed: a wrong high half here puts every such allocation at an
    * address the GPU cannot reach, which would look like corruption rather
    * than a failed query. */
   (void)info;
   (void)value;
   return -ENOTSUP;
}

int ac_drm_etos_bo_alloc(struct amdgpu_bo_alloc_request *req, uint32_t *handle)
{
   if (!req || !handle)
      return -EINVAL;

   if (etos_amdgpu_bo_alloc(req->alloc_size, req->phys_alignment,
                            req->preferred_heap, req->flags, handle) != 0)
      return -ENOMEM;
   return 0;
}

int ac_drm_etos_bo_free(uint32_t handle)
{
   etos_amdgpu_bo_free(handle);
   return 0;
}

int ac_drm_etos_bo_cpu_map(uint32_t handle, void **cpu)
{
   void *p;

   if (!cpu)
      return -EINVAL;

   p = etos_amdgpu_bo_map(handle);
   if (!p)
      return -ENOMEM;
   *cpu = p;
   return 0;
}

int ac_drm_etos_bo_cpu_unmap(uint32_t handle)
{
   etos_amdgpu_bo_unmap(handle);
   return 0;
}

int ac_drm_etos_bo_query_info(uint32_t handle, struct amdgpu_bo_info *info)
{
   (void)handle;
   if (!info)
      return -EINVAL;
   /* The protocol has `BoQueryInfo`, but the winsys only calls this for
    * imported BOs (to learn an allocation it did not make), and import is
    * not supported on etos yet — buffer sharing between processes is
    * "hand the AmdgpuBo capability over", a design question deferred in
    * idl/amdgpu.idl's header. Unsupported rather than a zeroed struct,
    * which would read as a zero-sized allocation. */
   return -ENOTSUP;
}

int ac_drm_etos_bo_va_op_raw(uint64_t bo_handle, uint64_t offset, uint64_t size,
                             uint64_t addr, uint64_t flags, uint32_t ops)
{
   if (etos_amdgpu_va_op((uint32_t)bo_handle, ops, addr, offset, size, flags) != 0)
      return -EINVAL;
   return 0;
}

int ac_drm_etos_cs_ctx_create2(uint32_t priority, uint32_t *ctx_id)
{
   if (!ctx_id)
      return -EINVAL;
   if (etos_amdgpu_ctx_create((int32_t)priority, ctx_id) != 0)
      return -ENOMEM;
   return 0;
}

int ac_drm_etos_cs_ctx_free(uint32_t ctx_id)
{
   if (etos_amdgpu_ctx_free(ctx_id) != 0)
      return -EINVAL;
   return 0;
}

/*
 * Flatten Mesa's chunk array into the payload `idl/amdgpu.idl` describes and
 * `drivers/drmd/kpi/session.c` parses:
 *
 *   u32 num_chunks; u32 _pad;
 *   struct drm_amdgpu_cs_chunk chunks[n];   // chunk_data = byte offset
 *   ... chunk data blobs ...
 *
 * The ioctl's `chunk_data` is a pointer into this process's address space,
 * which means nothing to drmd, so it becomes an offset into the same
 * payload. Everything a chunk *names* — BO handles, GPU virtual addresses,
 * syncobj handles — is already session-relative and crosses unchanged.
 */
int ac_drm_etos_cs_submit_raw2(uint32_t ctx_id, uint32_t bo_list_handle,
                               int num_chunks, struct drm_amdgpu_cs_chunk *chunks,
                               uint64_t *seq_no)
{
   uint8_t *payload;
   uint32_t header_len = 8;
   uint32_t table_len, total, off;
   struct drm_amdgpu_cs_chunk *wire;
   int i, r;

   if (num_chunks <= 0 || !chunks || !seq_no)
      return -EINVAL;

   table_len = (uint32_t)num_chunks * sizeof(struct drm_amdgpu_cs_chunk);
   total = header_len + table_len;
   for (i = 0; i < num_chunks; i++) {
      /* Each blob is padded to 8 so the next one stays aligned; drmd only
       * requires dword alignment, but keeping it at 8 means the payload
       * layout does not depend on chunk ordering. */
      total += (chunks[i].length_dw * 4 + 7) & ~7u;
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
   }

   r = etos_amdgpu_submit(ctx_id, payload, total, seq_no);
   free(payload);

   (void)bo_list_handle; /* always 0 — see idl/amdgpu.idl on BO lists */
   return r == 0 ? 0 : -EINVAL;
}

int ac_drm_etos_cs_query_fence_status(uint32_t ctx_id, uint32_t ip_type,
                                      uint32_t ip_instance, uint32_t ring,
                                      uint64_t fence_seq_no, uint64_t timeout_ns,
                                      uint64_t flags, uint32_t *expired)
{
   uint32_t signalled = 0;

   if (!expired)
      return -EINVAL;

   (void)flags;
   if (etos_amdgpu_wait_cs(ctx_id, ip_type, ip_instance, ring, fence_seq_no,
                           timeout_ns, &signalled) != 0)
      return -EINVAL;

   /* Mesa's "expired" means "completed", which is what `signalled` reports.
    * A timeout is a normal result on both sides, not an error. */
   *expired = signalled;
   return 0;
}
