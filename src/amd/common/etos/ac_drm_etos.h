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
int   etos_amdgpu_syncobj_create(uint32_t flags, uint32_t *out_handle);
int   etos_amdgpu_syncobj_destroy(uint32_t handle);
int   etos_amdgpu_syncobj_wait(const uint32_t *handles, uint32_t count,
                               uint64_t timeout_ns, uint32_t flags,
                               uint32_t *first_signalled);
int   etos_amdgpu_syncobj_reset(const uint32_t *handles, uint32_t count);
int   etos_amdgpu_syncobj_signal(const uint32_t *handles, uint32_t count);
int   etos_amdgpu_syncobj_transfer(uint32_t dst, uint64_t dst_point, uint32_t src,
                                   uint64_t src_point, uint32_t flags);
int   etos_amdgpu_syncobj_query(const uint32_t *handles, uint32_t count,
                                uint64_t *out_points);
int   etos_amdgpu_syncobj_timeline_signal(const uint32_t *handles,
                                          const uint64_t *points, uint32_t count);

/* The `ac_drm_*` API this file implements is declared by
 * `ac_linux_drm.h`, not here — that is the whole point of replacing
 * `ac_linux_drm.c` rather than branching it. Include that header for the
 * signatures.
 *
 * **Why replace and not branch.** On etos there is exactly one backend, so
 * a third `if (dev->is_etos)` per function would leave ~59 dead libdrm
 * tails referencing symbols that do not exist here — link errors, or stubs
 * papering over them — plus a patch to a shared upstream file to rebase on
 * every submodule bump. Replacing costs one line of meson and leaves that
 * file untouched. `util/u_stub.h` is what makes it work: on the normal path
 * `MESAPROC` is just `extern` and `TAIL` is `;`, so those declarations are
 * satisfied by definitions from any translation unit.
 */

#ifdef __cplusplus
}
#endif

#endif /* AC_DRM_ETOS_H */
