# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

files_libvirgl = files(
  'virgl_buffer.c',
  'virgl_context.c',
  'virgl_encode.c',
  'virgl_query.c',
  'virgl_resource.c',
  'virgl_screen.c',
  'virgl_staging_mgr.c',
  'virgl_streamout.c',
  'virgl_transfer_queue.c',
  'virgl_texture.c',
  'virgl_tgsi.c',
  'virgl_video.c',
)

libvirgl = static_library(
  'virgl',
  [ files_libvirgl ],
  gnu_symbol_visibility : 'hidden',
  # virgl_context.c's unconditional `#include <libsync.h>` (any non-Windows
  # target) needs src/util/ itself on the include path -- inc_util (src/util/
  # meson.build) only exposes src/ (its parent), which resolves `<util/
  # libsync.h>` but not the bare `<libsync.h>` this file actually writes.
  # Never surfaced before this etos winsys (the first non-llvmpipe gallium
  # driver built by this fork): llvmpipe never pulls in libsync.h at all.
  include_directories : [inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_virtio,
                          include_directories('../../../util')],
  dependencies : [dep_libdrm, idep_mesautil, idep_xmlconfig, idep_nir],
)

virgl_deps = [libvirgl]
if get_option('etos-virgl')
  virgl_deps += libvirgletos
elif not with_platform_windows
  virgl_deps += libvirgldrm
  virgl_deps += libvirglvtest
endif

# GALLIUM_VIRGL_ETOS (not GALLIUM_VIRGL) for the etos-virgl variant: plain
# GALLIUM_VIRGL also gates two *other* target-helpers this build never
# builds the backing library for -- drm_helper.h's pipe_virtio_gpu_
# create_screen() (needs winsys/virgl/drm's virgl_drm_screen_create) and
# sw_helper.h's vtest-backed "virpipe" branch (needs winsys/virgl/vtest's
# virgl_vtest_winsys_wrap) -- both unconditionally compiled into
# src/gallium/targets/dri/dri_target.c whenever GALLIUM_VIRGL is defined,
# regardless of which winsys is actually linked, so defining it here left
# the final trianglevirgl link with undefined references to both. Neither
# drm_helper.h nor sw_helper.h's vtest branch checks GALLIUM_VIRGL_ETOS, so
# using that instead skips both dead code paths; the etos winsys's own
# GALLIUM_VIRGL_ETOS branch (sw_helper.h) is unaffected. Nothing in
# drivers/virgl or winsys/virgl/etos itself is gated on GALLIUM_VIRGL.
driver_virgl = declare_dependency(
  compile_args : get_option('etos-virgl') ? ['-DGALLIUM_VIRGL_ETOS'] : ['-DGALLIUM_VIRGL'],
  link_with : virgl_deps,
)

if with_tests
  subdir('tests')
endif
