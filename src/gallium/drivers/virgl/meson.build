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

# GALLIUM_VIRGL_ETOS gates sw_helper.h's etos branch (src/gallium/auxiliary/
# target-helpers/sw_helper.h) -- separate from GALLIUM_VIRGL itself so the
# vtest-backed "virpipe" branch there (unreachable on etos anyway, since
# winsys/virgl/vtest isn't built for this option -- see ../../meson.build)
# stays untouched for any non-etos build that still sets GALLIUM_VIRGL.
driver_virgl = declare_dependency(
  compile_args : get_option('etos-virgl') ? ['-DGALLIUM_VIRGL', '-DGALLIUM_VIRGL_ETOS'] : ['-DGALLIUM_VIRGL'],
  link_with : virgl_deps,
)

if with_tests
  subdir('tests')
endif
