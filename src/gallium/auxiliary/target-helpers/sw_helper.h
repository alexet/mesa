
#ifndef SW_HELPER_H
#define SW_HELPER_H

#include <stdio.h>

#include "util/compiler.h"
#include "util/u_debug.h"
#include "target-helpers/sw_helper_public.h"
#include "frontend/sw_winsys.h"


/* Helper function to choose and instantiate one of the software rasterizers:
 * llvmpipe, softpipe.
 */

#ifdef GALLIUM_ZINK
#include "zink/zink_public.h"
#endif

#ifdef GALLIUM_D3D12
#include "d3d12/d3d12_public.h"
#endif

#ifdef GALLIUM_SOFTPIPE
#include "softpipe/sp_public.h"
#endif

#ifdef GALLIUM_LLVMPIPE
#include "llvmpipe/lp_public.h"
#endif

#ifdef GALLIUM_VIRGL
#include "virgl/virgl_public.h"
#include "virgl/vtest/virgl_vtest_public.h"
#endif

/* etos: construct the etos-native virgl_winsys directly and hand it to
 * virgl_create_screen(), bypassing the generic `sw_winsys*`-wrapping
 * "virpipe" path below entirely (that one talks the vtest socket protocol,
 * not etos's GpuDevice/GpuContext capabilities -- see
 * src/gallium/winsys/virgl/etos/virgl_etos_winsys.c's header).
 *
 * More than one etos backend may be compiled into the same build now, and
 * the choice between them is made *here*, at run time -- see the selection
 * comment on sw_screen_create_named below. */
#ifdef GALLIUM_VIRGL_ETOS
#include "virgl/etos/virgl_etos_winsys.h"
#endif

/* etos + radeonsi: same situation as GALLIUM_VIRGL_ETOS above, one driver
 * down. The surfaceless/swrast screen-creation path is the only one this
 * build has (there is no DRI device loader and no device node to enumerate),
 * so the real GPU driver has to be reachable from here. radeonsi opens the
 * card through an AmdgpuDevice capability rather than an fd, hence the -1.
 * See src/amd/common/etos/. */
#ifdef GALLIUM_RADEONSI_ETOS
#include "radeonsi/si_public.h"
#endif

/* How an etos build picks its GPU driver, when more than one is compiled in.
 *
 * There is no device enumeration to do it with: no DRI loader, no device
 * nodes, and no environment to read `GALLIUM_DRIVER` from. What there is,
 * is one `GpuDevice` capability at a well-known slot that answers `Kind()`
 * (idl/gpu.idl). So each driver is simply asked to open it, in turn, and
 * the one whose kind matches succeeds -- `etos_virgl_init` and
 * `etos_amdgpu_init` both check `Kind()` and decline anything that is not
 * theirs, so "try it and see" is a real answer here rather than a guess.
 *
 * That relies on declining being *harmless*, which it did not used to be:
 * both glues took ownership of the borrowed slot and closed it on the way
 * out, so the first driver to decline destroyed the device for the rest.
 * See the ManuallyDrop comments in utility/{virgl,amdgpu}-glue.
 *
 * Order is preference, not correctness -- at most one kind can ever match.
 */
static inline struct pipe_screen *
sw_screen_create_named(struct sw_winsys *winsys, const struct pipe_screen_config *config, const char *driver)
{
   struct pipe_screen *screen = NULL;

#if defined(GALLIUM_VIRGL_ETOS)
   if (screen == NULL && (strcmp(driver, "virgl-etos") == 0 || !driver[0])) {
      screen = virgl_etos_create_screen(config);
      if (screen)
         fprintf(stderr, "etos: gallium driver: virgl\n");
   }
#endif

#if defined(GALLIUM_RADEONSI_ETOS)
   if (screen == NULL && (strcmp(driver, "radeonsi-etos") == 0 || !driver[0])) {
      screen = radeonsi_screen_create(-1, config);
      if (screen)
         fprintf(stderr, "etos: gallium driver: radeonsi\n");
   }
#endif

#if defined(GALLIUM_LLVMPIPE)
   if (screen == NULL && (strcmp(driver, "llvmpipe") == 0 || !driver[0]))
      screen = llvmpipe_create_screen(winsys);
#endif

#if defined(GALLIUM_VIRGL)
   if (screen == NULL && strcmp(driver, "virpipe") == 0) {
      struct virgl_winsys *vws;
      vws = virgl_vtest_winsys_wrap(winsys);
      screen = virgl_create_screen(vws, NULL);
   }
#endif

#if defined(GALLIUM_SOFTPIPE)
   if (screen == NULL && strcmp(driver, "softpipe") == 0)
      screen = softpipe_create_screen(winsys);
#endif

#if defined(GALLIUM_ZINK)
   if (screen == NULL && strcmp(driver, "zink") == 0)
      screen = zink_create_screen(winsys, config);
#endif

#if defined(GALLIUM_D3D12)
   if (screen == NULL && strcmp(driver, "d3d12") == 0)
      screen = d3d12_create_dxcore_screen(winsys, NULL);
#endif

   return screen;
}

struct pipe_screen *
sw_screen_create_vk(struct sw_winsys *winsys, const struct pipe_screen_config *config, bool sw_vk)
{
   UNUSED bool only_sw = debug_get_bool_option("LIBGL_ALWAYS_SOFTWARE", false);
   const char *drivers[] = {
      (sw_vk ? "" : debug_get_option("GALLIUM_DRIVER", "")),
#if defined(GALLIUM_D3D12)
      (sw_vk || only_sw) ? "" : "d3d12",
#endif
#if defined(GALLIUM_LLVMPIPE)
      "llvmpipe",
#endif
#if defined(GALLIUM_SOFTPIPE)
      sw_vk ? "" : "softpipe",
#endif
   };

   for (unsigned i = 0; i < ARRAY_SIZE(drivers); i++) {
      struct pipe_screen *screen = sw_screen_create_named(winsys, config, drivers[i]);
      if (screen)
         return screen;
      /* If the env var is set, don't keep trying things */
      else if (i == 0 && drivers[i][0] != '\0')
         return NULL;
   }
   return NULL;
}

struct pipe_screen *
sw_screen_create_zink(struct sw_winsys *winsys, const struct pipe_screen_config *config, bool whatever)
{
#if defined(GALLIUM_ZINK)
   return zink_create_screen(winsys, config);
#else
   return NULL;
#endif
}

struct pipe_screen *
sw_screen_create(struct sw_winsys *winsys)
{
   return sw_screen_create_vk(winsys, NULL, false);
}
#endif
