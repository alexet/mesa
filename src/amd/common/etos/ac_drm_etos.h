/*
 * Copyright 2026 The etos authors
 * SPDX-License-Identifier: MIT
 *
 * The etos backend behind `ac_drm_*` — a third sibling of the libdrm and
 * virtio backends in ac_linux_drm.c, selected at configure time with
 * -Detos-amdgpu=true.
 *
 * Everything here bottoms out in `etos_amdgpu_*`, the C surface of
 * `utility/amdgpu-glue` (a Rust staticlib linked into the final binary),
 * which in turn speaks `idl/amdgpu.idl` to `drivers/drmd`. So the chain is:
 *
 *   radeonsi -> amdgpu winsys -> ac_drm_* -> this -> amdgpu-glue -> drmd
 *
 * There is no fd and no ioctl anywhere in it. That is the point: an
 * `ioctl(fd, cmd, void *)` is ambient authority over an integer carrying an
 * opaque blob, which is the model etos's capability system exists to avoid.
 * The translation from Mesa's POSIX-shaped expectations to capabilities
 * happens here, in the port, where it is visible — not in the libc, where
 * it would not be.
 */

#ifndef AC_DRM_ETOS_H
#define AC_DRM_ETOS_H

#include <stdbool.h>
#include <stdint.h>

#include "amdgpu.h"
#include "drm-uapi/amdgpu_drm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── utility/amdgpu-glue's C surface ─────────────────────────────────────
 *
 * Declared here rather than in a generated header: the crate is small, the
 * surface is stable, and a hand-written declaration that disagrees with the
 * Rust side is a link error rather than silent corruption (every function
 * takes and returns plain scalars and pointers).
 */
int   etos_amdgpu_init(void);
int   etos_amdgpu_query(uint32_t query, uint32_t arg0, uint32_t arg1,
                        uint8_t *out, uint32_t out_cap, uint32_t *out_len);
int   etos_amdgpu_bo_alloc(uint64_t size, uint64_t alignment, uint64_t domains,
                           uint64_t flags, uint32_t *out_handle);
void  etos_amdgpu_bo_free(uint32_t handle);
void *etos_amdgpu_bo_map(uint32_t handle);
void  etos_amdgpu_bo_unmap(uint32_t handle);
int   etos_amdgpu_va_op(uint32_t handle, uint32_t op, uint64_t va,
                        uint64_t offset, uint64_t size, uint64_t flags);
int   etos_amdgpu_ctx_create(int32_t priority, uint32_t *out_ctx);
int   etos_amdgpu_ctx_free(uint32_t ctx_id);
int   etos_amdgpu_submit(uint32_t ctx_id, const uint8_t *payload,
                         uint64_t payload_len, uint64_t *out_seq);
int   etos_amdgpu_wait_cs(uint32_t ctx_id, uint32_t ip, uint32_t ip_instance,
                          uint32_t ring, uint64_t seq_no, uint64_t timeout_ns,
                          uint32_t *signalled);

/* ── the backend ─────────────────────────────────────────────────────────
 *
 * One device per process (see amdgpu-glue's own single-session note), so
 * these carry no handle: the state lives in the glue crate, and these are
 * free functions over it.
 *
 * **Not yet wired in.** These are the operations, named `ac_drm_etos_*`.
 * The plan is for this file to go on and define the `ac_drm_*` API itself
 * and for `ac_linux_drm.c` to be dropped from the build when
 * `-Detos-amdgpu=true`, rather than for that file to grow a third `if
 * (dev->is_etos)` branch in every function.
 *
 * Replacing rather than branching, because on etos there is exactly one
 * backend: branching would leave ~59 dead libdrm tails referencing symbols
 * that do not exist here (link errors, or stubs papering over them), plus a
 * patch to a shared upstream file to rebase on every submodule bump.
 * Replacing costs one line of meson and leaves that file untouched.
 * `util/u_stub.h` makes it work: on the normal path `MESAPROC` is just
 * `extern` and `TAIL` is `;`, so the declarations in `ac_linux_drm.h` are
 * satisfied by definitions from any translation unit.
 */

int ac_drm_etos_device_initialize(uint32_t *major, uint32_t *minor);
void ac_drm_etos_device_deinitialize(void);

int ac_drm_etos_query_info(unsigned info_id, unsigned size, void *value);
int ac_drm_etos_query_hw_ip_info(unsigned type, unsigned ip_instance,
                                 struct drm_amdgpu_info_hw_ip *info);
int ac_drm_etos_query_hw_ip_count(unsigned type, uint32_t *count);
int ac_drm_etos_query_firmware_version(unsigned fw_type, unsigned ip_instance,
                                       unsigned index, uint32_t *version,
                                       uint32_t *feature);
int ac_drm_etos_query_gpu_info(struct amdgpu_gpu_info *info);
int ac_drm_etos_query_heap_info(uint32_t heap, uint32_t flags,
                                struct amdgpu_heap_info *info);
int ac_drm_etos_query_sw_info(unsigned info, void *value);

int ac_drm_etos_bo_alloc(struct amdgpu_bo_alloc_request *req, uint32_t *handle);
int ac_drm_etos_bo_free(uint32_t handle);
int ac_drm_etos_bo_cpu_map(uint32_t handle, void **cpu);
int ac_drm_etos_bo_cpu_unmap(uint32_t handle);
int ac_drm_etos_bo_query_info(uint32_t handle, struct amdgpu_bo_info *info);
int ac_drm_etos_bo_va_op_raw(uint64_t bo_handle, uint64_t offset, uint64_t size,
                             uint64_t addr, uint64_t flags, uint32_t ops);

int ac_drm_etos_cs_ctx_create2(uint32_t priority, uint32_t *ctx_id);
int ac_drm_etos_cs_ctx_free(uint32_t ctx_id);
int ac_drm_etos_cs_submit_raw2(uint32_t ctx_id, uint32_t bo_list_handle,
                               int num_chunks, struct drm_amdgpu_cs_chunk *chunks,
                               uint64_t *seq_no);
int ac_drm_etos_cs_query_fence_status(uint32_t ctx_id, uint32_t ip_type,
                                      uint32_t ip_instance, uint32_t ring,
                                      uint64_t fence_seq_no, uint64_t timeout_ns,
                                      uint64_t flags, uint32_t *expired);

#ifdef __cplusplus
}
#endif

#endif /* AC_DRM_ETOS_H */
