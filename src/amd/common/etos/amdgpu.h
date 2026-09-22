/*
 * Copyright 2026 The etos authors
 * SPDX-License-Identifier: MIT
 *
 * A stand-in for libdrm's <amdgpu.h>, for the etos build
 * (-Detos-amdgpu=true).
 *
 * etos has no libdrm: `ac_drm_*` calls are answered by src/amd/common/etos
 * over the etos AmdgpuDevice/AmdgpuSession capability protocol, and there is
 * no DRM fd to ioctl. But `ac_linux_drm.h` includes <amdgpu.h> for its
 * *types* — the handles and structs that appear in `ac_drm_*` signatures —
 * so those still have to exist.
 *
 * This provides exactly those, and no function declarations: on this build
 * every libdrm entry point is behind `#ifndef HAVE_ETOS_AMDGPU` in
 * ac_linux_drm.c, so nothing here is ever called. Keeping the declarations
 * out is deliberate — a missing type is a compile error, but a declared
 * function with no definition is a link error at the end of a long build,
 * and an accidentally-reached libdrm path should fail the first way.
 *
 * Layouts match libdrm's where a field is read. They do not have to match
 * bit-for-bit, because both producer (our backend) and consumer (Mesa) are
 * compiled against *this* header — there is no libdrm ABI to be compatible
 * with. Fields Mesa never reads are kept anyway where they document what a
 * query returns.
 */

#ifndef _ETOS_AMDGPU_H_
#define _ETOS_AMDGPU_H_

#include <stdbool.h>
#include <stdint.h>

#include "drm-uapi/amdgpu_drm.h"

/* Opaque handles. On etos a "device" is a session on an AmdgpuDevice
 * capability and a "bo" is a GEM handle within it — see
 * src/amd/common/etos/ac_drm_etos.c. Declared as pointers to incomplete
 * structs, exactly as libdrm does, so nothing can dereference them. */
typedef struct amdgpu_device *amdgpu_device_handle;
typedef struct amdgpu_bo *amdgpu_bo_handle;
typedef struct amdgpu_va *amdgpu_va_handle;
typedef struct amdgpu_bo_list *amdgpu_bo_list_handle;
typedef struct amdgpu_context *amdgpu_context_handle;

/* Both of these are declared by Mesa itself in ac_linux_drm.h's `_WIN32`
 * fallback block, and by libdrm's amdgpu.h otherwise. The etos build takes
 * neither path — it keeps the real `drm-uapi/amdgpu_drm.h` (so UAPI structs
 * match drm-kmod's exactly, which the command-submission payload depends on)
 * and replaces only libdrm's header with this one. So they belong here. */
enum amdgpu_gpu_va_range {
   amdgpu_gpu_va_range_general = 0,
};

/* Flags for amdgpu_va_range_alloc. `32_BIT` asks for an address the GPU can
 * reach with a 32-bit pointer; `HIGH` asks for the upper half of the range.
 * Accepted and ignored by this backend's bump allocator — it hands out from
 * one window, and amdgpu validates every address anyway. */
#define AMDGPU_VA_RANGE_32_BIT 0x1
#define AMDGPU_VA_RANGE_HIGH   0x2

enum amdgpu_sw_info {
   amdgpu_sw_info_address32_hi = 0,
   amdgpu_sw_info_address_prt_wa_control_bit = 1,
};

enum amdgpu_bo_handle_type {
   amdgpu_bo_handle_type_gem_flink_name = 0,
   amdgpu_bo_handle_type_dma_buf_fd = 1,
   amdgpu_bo_handle_type_kms = 2,
   amdgpu_bo_handle_type_kms_noimport = 3,
};

/* Allocation request. `preferred_heap`/`flags` are AMDGPU_GEM_DOMAIN_* /
 * AMDGPU_GEM_CREATE_*, passed through to the protocol untouched. */
struct amdgpu_bo_alloc_request {
   uint64_t alloc_size;
   uint64_t phys_alignment;
   uint32_t preferred_heap;
   uint64_t flags;
};

/* Tiling/metadata a BO carries for the display path. Not used by the etos
 * backend yet (`bo_set_metadata` reports unsupported), but named in
 * `ac_drm_*` signatures. */
/* `tiling_info` is 64-bit in libdrm, and Mesa takes its address as a
 * `uint64_t *` (amdgpu_bo.c's ac_surface_compute_bo_metadata call). */
struct amdgpu_bo_metadata {
   uint64_t flags;
   uint64_t tiling_info;
   uint32_t size_metadata;
   uint32_t umd_metadata[64];
};

struct amdgpu_bo_info {
   uint64_t alloc_size;
   uint64_t phys_alignment;
   uint32_t preferred_heap;
   uint64_t alloc_flags;
   struct amdgpu_bo_metadata metadata;
};

struct amdgpu_heap_info {
   uint64_t heap_size;
   uint64_t heap_usage;
   uint64_t max_allocation;
};

/*
 * The register-derived device description. Mesa reads exactly four fields of
 * this (`ac_gpu_info.c`): `gb_addr_cfg`, `mc_arb_ramcfg`, and the two tile-mode
 * arrays — and the arrays only on pre-GFX9 hardware, which this port does not
 * target. The rest are libdrm's, kept so the struct documents what the real
 * query returns rather than silently omitting it.
 */
struct amdgpu_gpu_info {
   uint32_t asic_id;
   uint32_t chip_rev;
   uint32_t chip_external_rev;
   uint32_t family_id;
   uint64_t ids_flags;
   uint64_t max_engine_clk;
   uint64_t max_memory_clk;
   uint32_t num_shader_engines;
   uint32_t num_shader_arrays_per_engine;
   uint32_t avail_quad_shader_pipes;
   uint32_t max_quad_shader_pipes;
   uint32_t cache_size;
   uint32_t num_tile_pipes;
   uint32_t pipe_interleave_bytes;
   uint32_t num_hw_gfx_contexts;
   uint32_t rb_pipes;
   uint32_t enabled_rb_pipes_mask;
   uint32_t gpu_counter_freq;
   uint32_t backend_disable[4];
   uint32_t mc_arb_ramcfg;
   uint32_t gb_addr_cfg;
   uint32_t gb_tile_mode[32];
   uint32_t gb_macro_tile_mode[16];
   uint32_t pa_sc_raster_cfg[4];
   uint32_t pa_sc_raster_cfg1[4];
   uint32_t cu_active_number;
   uint32_t cu_ao_mask;
   uint32_t cu_bitmap[4][4];
   uint32_t vram_type;
   uint32_t vram_bit_width;
   uint32_t ce_ram_size;
   uint32_t vce_harvest_config;
   uint32_t pci_rev_id;
};

/*
 * `extern "C"` because these are the only *functions* this header declares
 * and it is included from C++ (radeonsi's amdgpu_bo.h chain). Everything
 * else here is types, which need no linkage guard — but a mangled call to a
 * C definition is a link error at the very end of the build, naming a
 * demangled symbol that looks like it should exist.
 */
#ifdef __cplusplus
extern "C" {
#endif

/*
 * The one libdrm function declared here rather than omitted, because the
 * etos backend genuinely implements it (ac_drm_etos.c) rather than
 * compiling the call out: `amdgpu_bo.h` reads a reservation's base address
 * through it from an inline, so there is no `#ifdef` to hide behind. Pure
 * accessor over a handle this backend minted — no device involved.
 */
uint64_t amdgpu_va_get_start_addr(amdgpu_va_handle va);

/* Likewise implemented rather than compiled out: the winsys frees a VA
 * reservation through libdrm's name directly (amdgpu_bo.c), not through
 * ac_drm_va_range_free. */
int amdgpu_va_range_free(amdgpu_va_handle va_range_handle);

#ifdef __cplusplus
}
#endif

#endif /* _ETOS_AMDGPU_H_ */
