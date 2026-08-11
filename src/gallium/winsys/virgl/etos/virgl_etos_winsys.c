/*
 * Copyright 2026 Alexander Eyers-Taylor
 * SPDX-License-Identifier: MIT
 *
 * The etos-native virgl_winsys: talks the etos GpuDevice/GpuContext
 * capability protocol via utility/virgl-glue's extern "C" bridge
 * (etos_virgl_*), instead of DRM ioctls (../drm) or the vtest socket
 * protocol (../vtest). See utility/virgl-glue/src/lib.rs's module doc for
 * the design this mirrors: no resource caching (every resource_create
 * allocates fresh), and no real fence objects -- every op that would
 * return a fence already blocks until the real virtio-gpu round-trip
 * completes on the etos side (drivers/virtio-gpud's fences are synchronous
 * throughout), so any non-NULL `struct pipe_fence_handle *` here is treated
 * as already-signaled.
 *
 * struct virgl_hw_res is winsys-private -- confirmed against every real
 * user in src/gallium/drivers/virgl (virgl_context.c, virgl_resource.c,
 * virgl_staging_mgr.c), which only ever reaches a
 * resource's lifetime through vws->resource_reference(), never a raw field
 * access (see this directory's meson.build comment) -- so it only needs
 * what this file itself uses, not the drm winsys's full layout.
 */

#include <stdlib.h>
#include <string.h>

#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/macros.h"
#include "util/u_memory.h"

#include "virgl_etos_winsys.h"
#include "virgl/virgl_public.h"
#include "virgl/virgl_winsys.h"

/* utility/virgl-glue/src/lib.rs's extern "C" surface. */
extern int etos_virgl_init(void);
extern int etos_virgl_supports_fences(void);
extern int etos_virgl_get_capset_info(uint32_t capset_index, uint32_t *out_capset_id,
                                       uint32_t *out_max_version, uint32_t *out_max_size);
extern int etos_virgl_get_capset(uint32_t capset_id, uint32_t version, uint8_t *out_buf,
                                  size_t buf_len, size_t *out_written);
extern int etos_virgl_resource_create(uint64_t size, uint32_t virgl_format, uint32_t image_type,
                                       uint32_t width, uint32_t height, uint32_t depth,
                                       uint32_t layers, uint32_t mip_levels, uint32_t samples,
                                       uint32_t bind, uint32_t *out_handle);
extern int etos_virgl_resource_destroy(uint32_t handle);
extern int etos_virgl_resource_size(uint32_t handle, uint64_t *out_size);
extern int etos_virgl_resource_map(uint32_t handle, void **out_ptr);
extern int etos_virgl_transfer_to_host(uint32_t handle, uint32_t x, uint32_t y, uint32_t z,
                                        uint32_t w, uint32_t h, uint32_t d, uint32_t level,
                                        uint32_t stride, uint32_t layer_stride, uint64_t offset);
extern int etos_virgl_transfer_from_host(uint32_t handle, uint32_t x, uint32_t y, uint32_t z,
                                          uint32_t w, uint32_t h, uint32_t d, uint32_t level,
                                          uint32_t stride, uint32_t layer_stride, uint64_t offset);
extern int etos_virgl_submit(const uint8_t *commands, size_t len);
extern int etos_virgl_wait_idle(void);

/* Real virtio-gpu wire structs cap `virtio_gpu_ctx_create.debug_name` at 64
 * bytes and this is purely a host-side debug label -- see
 * drivers/virtio-gpud/src/proto.rs's CtxCreate doc -- so its own bytes never
 * reach this file; nothing here needs to match that constant.
 */

struct virgl_hw_res {
   uint32_t handle;
   uint32_t refcount;
   uint64_t size;
   void *ptr; /* NULL until first resource_map */
};

static inline uint32_t
virgl_etos_image_type(enum pipe_texture_target target)
{
   switch (target) {
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      return 1; /* ImageType::Image1d */
   case PIPE_TEXTURE_3D:
      return 3; /* ImageType::Image3d */
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return 4; /* ImageType::CubeMap */
   case PIPE_BUFFER:
      return 0; /* linear sentinel -- see gpu_context.rs's module doc */
   default:
      return 2; /* ImageType::Image2d -- covers 2D/2D_ARRAY/RECT alike */
   }
}

static struct virgl_hw_res *
virgl_etos_resource_create(struct virgl_winsys *vws,
                            enum pipe_texture_target target,
                            UNUSED const void *map_front_private,
                            uint32_t format, uint32_t bind,
                            uint32_t width, uint32_t height,
                            uint32_t depth, uint32_t array_size,
                            uint32_t last_level, uint32_t nr_samples,
                            UNUSED uint32_t flags, uint32_t size)
{
   uint32_t image_type = virgl_etos_image_type(target);
   uint32_t virgl_format = pipe_to_virgl_format((enum pipe_format)format);
   uint32_t handle;

   if (etos_virgl_resource_create(size, virgl_format, image_type, width, height, depth,
                                   array_size, last_level + 1, nr_samples, bind, &handle) != 0)
      return NULL;

   struct virgl_hw_res *res = CALLOC_STRUCT(virgl_hw_res);
   if (!res) {
      etos_virgl_resource_destroy(handle);
      return NULL;
   }
   res->handle = handle;
   res->refcount = 1;
   res->size = size;
   res->ptr = NULL;
   return res;
}

static void
virgl_etos_resource_reference(UNUSED struct virgl_winsys *vws,
                               struct virgl_hw_res **dres,
                               struct virgl_hw_res *sres)
{
   struct virgl_hw_res *old = *dres;

   if (sres)
      sres->refcount++;

   if (old && --old->refcount == 0) {
      etos_virgl_resource_destroy(old->handle);
      FREE(old);
   }

   *dres = sres;
}

static void *
virgl_etos_resource_map(UNUSED struct virgl_winsys *vws, struct virgl_hw_res *res)
{
   if (!res->ptr)
      etos_virgl_resource_map(res->handle, &res->ptr);
   return res->ptr;
}

static void
virgl_etos_resource_wait(UNUSED struct virgl_winsys *vws, UNUSED struct virgl_hw_res *res)
{
   /* Every op that could leave `res` busy (transfer_put/get, submit_cmd)
    * already blocked until the real virtio-gpu round-trip completed -- see
    * this file's header. */
}

static bool
virgl_etos_resource_is_busy(UNUSED struct virgl_winsys *vws, UNUSED struct virgl_hw_res *res)
{
   return false;
}

static struct virgl_hw_res *
virgl_etos_resource_create_from_handle(UNUSED struct virgl_winsys *vws,
                                        UNUSED struct winsys_handle *whandle,
                                        UNUSED struct pipe_resource *templ,
                                        UNUSED uint32_t *plane, UNUSED uint32_t *stride,
                                        UNUSED uint32_t *plane_offset,
                                        UNUSED uint64_t *modifier, UNUSED uint32_t *blob_mem)
{
   /* No dma-buf/window-system import support -- single-process surfaceless
    * rendering has nothing to import from. */
   return NULL;
}

static void
virgl_etos_resource_set_type(UNUSED struct virgl_winsys *vws, UNUSED struct virgl_hw_res *res,
                              UNUSED uint32_t format, UNUSED uint32_t bind,
                              UNUSED uint32_t width, UNUSED uint32_t height,
                              UNUSED uint32_t usage, UNUSED uint64_t modifier,
                              UNUSED uint32_t plane_count, UNUSED const uint32_t *plane_strides,
                              UNUSED const uint32_t *plane_offsets)
{
}

static bool
virgl_etos_resource_get_handle(UNUSED struct virgl_winsys *vws, UNUSED struct virgl_hw_res *res,
                                UNUSED uint32_t stride, UNUSED struct winsys_handle *whandle)
{
   /* No window-system export support -- see resource_create_from_handle. */
   return false;
}

static uint32_t
virgl_etos_resource_get_storage_size(UNUSED struct virgl_winsys *vws, struct virgl_hw_res *res)
{
   return (uint32_t)res->size;
}

static int
virgl_etos_transfer_put(UNUSED struct virgl_winsys *vws, struct virgl_hw_res *res,
                         const struct pipe_box *box, uint32_t stride, uint32_t layer_stride,
                         uint32_t buf_offset, uint32_t level)
{
   return etos_virgl_transfer_to_host(res->handle, (uint32_t)box->x, (uint32_t)box->y,
                                       (uint32_t)box->z, (uint32_t)box->width,
                                       (uint32_t)box->height, (uint32_t)box->depth, level,
                                       stride, layer_stride, buf_offset);
}

static int
virgl_etos_transfer_get(UNUSED struct virgl_winsys *vws, struct virgl_hw_res *res,
                         const struct pipe_box *box, uint32_t stride, uint32_t layer_stride,
                         uint32_t buf_offset, uint32_t level)
{
   return etos_virgl_transfer_from_host(res->handle, (uint32_t)box->x, (uint32_t)box->y,
                                         (uint32_t)box->z, (uint32_t)box->width,
                                         (uint32_t)box->height, (uint32_t)box->depth, level,
                                         stride, layer_stride, buf_offset);
}

static struct virgl_cmd_buf *
virgl_etos_cmd_buf_create(UNUSED struct virgl_winsys *ws, uint32_t size)
{
   struct virgl_cmd_buf *buf = CALLOC_STRUCT(virgl_cmd_buf);
   if (!buf)
      return NULL;
   /* Mesa's own command encoder (virgl_encode.c) writes directly into
    * buf->buf[buf->cdw++] -- this is a plain dword array Mesa manages, not
    * winsys-private state (unlike struct virgl_hw_res). */
   buf->buf = MALLOC(size * sizeof(uint32_t));
   if (!buf->buf) {
      FREE(buf);
      return NULL;
   }
   buf->cdw = 0;
   return buf;
}

static void
virgl_etos_cmd_buf_destroy(struct virgl_cmd_buf *buf)
{
   if (buf)
      FREE(buf->buf);
   FREE(buf);
}

static void
virgl_etos_emit_res(UNUSED struct virgl_winsys *vws, struct virgl_cmd_buf *buf,
                     struct virgl_hw_res *res, UNUSED bool write_buffer)
{
   /* Every resource is already CTX_ATTACH_RESOURCE'd to this context at
    * bind() time (drivers/virtio-gpud/src/gpu_context.rs), so unlike the
    * DRM winsys's emit_res (which also builds a bo_handles list consumed by
    * the EXECBUFFER ioctl) this only needs to write the resource handle
    * dword Mesa's command encoder expects at the current write position --
    * see virgl_encode.c's virgl_encoder_emit_resource(). */
   buf->buf[buf->cdw++] = res->handle;
}

static int
virgl_etos_submit_cmd(UNUSED struct virgl_winsys *vws, struct virgl_cmd_buf *buf,
                       struct pipe_fence_handle **fence)
{
   int ret = etos_virgl_submit((const uint8_t *)buf->buf, (size_t)buf->cdw * sizeof(uint32_t));
   buf->cdw = 0;
   if (ret != 0)
      return ret;
   if (fence)
      *fence = (struct pipe_fence_handle *)(uintptr_t)1; /* already-signaled sentinel */
   return 0;
}

static bool
virgl_etos_res_is_referenced(UNUSED struct virgl_winsys *vws, UNUSED struct virgl_cmd_buf *buf,
                              UNUSED struct virgl_hw_res *res)
{
   /* Safe to always report "not referenced": submit_cmd already blocks
    * until the GPU has finished with everything in `buf` before returning
    * (see this file's header), so there is never a submission still
    * outstanding by the time a caller could ask this question. */
   return false;
}

/* VIRTIO_GPU_CAPSET_VIRGL / _VIRGL2 (virtio_gpu.h): GET_CAPSET_INFO's
 * `capset_index` is an *enumeration position*, not the capset id itself --
 * a host offering both capsets reports VIRGL (id 1, GLSL capped around
 * 1.40/ES 3.00 -- exactly the "GLSL 3.30 is not supported" failure this
 * fixes) and VIRGL2 (id 2, real GL 3.3+ core support) at *different*
 * indices. virgl_drm_winsys.c's own get_caps() asks for cap_set_id=2
 * directly (a DRM ioctl parameter, falling back to 1 on EINVAL) since Linux
 * lets it address a capset by id; virtio-gpu's GET_CAPSET_INFO has no such
 * shortcut, so this searches every index for id 2 first. */
#define VIRGL_CAPSET_VIRGL 1
#define VIRGL_CAPSET_VIRGL2 2
#define VIRGL_CAPSET_MAX_INDEX 8

static int
virgl_etos_get_caps(UNUSED struct virgl_winsys *vws, struct virgl_drm_caps *caps)
{
   virgl_ws_fill_new_caps_defaults(caps);

   uint32_t best_capset_id = 0, best_max_version = 0, best_max_size = 0;
   for (uint32_t index = 0; index < VIRGL_CAPSET_MAX_INDEX; index++) {
      uint32_t capset_id, max_version, max_size;
      if (etos_virgl_get_capset_info(index, &capset_id, &max_version, &max_size) != 0)
         break; /* no more capsets at or past this index */

      if (capset_id == VIRGL_CAPSET_VIRGL2) {
         best_capset_id = capset_id;
         best_max_version = max_version;
         best_max_size = max_size;
         break; /* VIRGL2 is strictly better than VIRGL -- stop searching */
      }
      if (best_capset_id == 0 && capset_id == VIRGL_CAPSET_VIRGL) {
         best_capset_id = capset_id;
         best_max_version = max_version;
         best_max_size = max_size;
         /* keep searching in case a later index offers VIRGL2 */
      }
   }
   if (best_capset_id == 0)
      return -1;

   size_t copy_len = best_max_size < sizeof(caps->caps) ? best_max_size : sizeof(caps->caps);
   size_t written = 0;
   if (etos_virgl_get_capset(best_capset_id, best_max_version, (uint8_t *)&caps->caps, copy_len,
                              &written) != 0)
      return -1;

   return 0;
}

static struct pipe_fence_handle *
virgl_etos_cs_create_fence(UNUSED struct virgl_winsys *vws, UNUSED int fd)
{
   /* No external sync-fd import support -- see this file's header on why
    * fences are a trivial always-signaled sentinel here. */
   return NULL;
}

static bool
virgl_etos_fence_wait(UNUSED struct virgl_winsys *vws, UNUSED struct pipe_fence_handle *fence,
                       UNUSED uint64_t timeout)
{
   return true;
}

static void
virgl_etos_fence_reference(UNUSED struct virgl_winsys *vws, struct pipe_fence_handle **dst,
                            struct pipe_fence_handle *src)
{
   *dst = src;
}

static void
virgl_etos_flush_frontbuffer(UNUSED struct virgl_winsys *vws, UNUSED struct virgl_cmd_buf *cbuf,
                              UNUSED struct virgl_hw_res *res, UNUSED unsigned level,
                              UNUSED unsigned layer, UNUSED void *winsys_drawable_handle,
                              UNUSED struct pipe_box *sub_box)
{
   /* No window-system integration -- EGL surfaceless has no front buffer to
    * flush (matches llvmpipe's own winsys/sw/null path on etos). */
}

static void
virgl_etos_fence_server_sync(UNUSED struct virgl_winsys *vws, UNUSED struct virgl_cmd_buf *cbuf,
                              UNUSED struct pipe_fence_handle *fence)
{
}

static int
virgl_etos_fence_get_fd(UNUSED struct virgl_winsys *vws, UNUSED struct pipe_fence_handle *fence)
{
   return -1;
}

static int
virgl_etos_get_fd(UNUSED struct virgl_winsys *vws)
{
   return -1;
}

static void
virgl_etos_winsys_destroy(struct virgl_winsys *vws)
{
   FREE(vws);
}

struct pipe_screen *
virgl_etos_create_screen(const struct pipe_screen_config *config)
{
   if (etos_virgl_init() != 0)
      return NULL;

   struct virgl_winsys *vws = CALLOC_STRUCT(virgl_winsys);
   if (!vws)
      return NULL;

   vws->supports_fences = etos_virgl_supports_fences();
   vws->supports_encoded_transfers = 0;
   vws->supports_coherent = 0;

   vws->destroy = virgl_etos_winsys_destroy;
   vws->get_fd = virgl_etos_get_fd;
   vws->transfer_put = virgl_etos_transfer_put;
   vws->transfer_get = virgl_etos_transfer_get;
   vws->resource_create = virgl_etos_resource_create;
   vws->resource_reference = virgl_etos_resource_reference;
   vws->resource_map = virgl_etos_resource_map;
   vws->resource_wait = virgl_etos_resource_wait;
   vws->resource_is_busy = virgl_etos_resource_is_busy;
   vws->resource_create_from_handle = virgl_etos_resource_create_from_handle;
   vws->resource_set_type = virgl_etos_resource_set_type;
   vws->resource_get_handle = virgl_etos_resource_get_handle;
   vws->resource_get_storage_size = virgl_etos_resource_get_storage_size;
   vws->cmd_buf_create = virgl_etos_cmd_buf_create;
   vws->cmd_buf_destroy = virgl_etos_cmd_buf_destroy;
   vws->emit_res = virgl_etos_emit_res;
   vws->submit_cmd = virgl_etos_submit_cmd;
   vws->res_is_referenced = virgl_etos_res_is_referenced;
   vws->get_caps = virgl_etos_get_caps;
   vws->cs_create_fence = virgl_etos_cs_create_fence;
   vws->fence_wait = virgl_etos_fence_wait;
   vws->fence_reference = virgl_etos_fence_reference;
   vws->flush_frontbuffer = virgl_etos_flush_frontbuffer;
   vws->fence_server_sync = virgl_etos_fence_server_sync;
   vws->fence_get_fd = virgl_etos_fence_get_fd;

   struct pipe_screen *screen = virgl_create_screen(vws, config);
   if (!screen)
      vws->destroy(vws);
   return screen;
}
