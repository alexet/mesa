# Copyright 2026 Alexander Eyers-Taylor
# SPDX-License-Identifier: MIT
#
# The etos-native virgl winsys: no libdrm (unlike ../drm), no vtest socket
# (unlike ../vtest) -- talks etos's GpuDevice/GpuContext capability protocol
# via utility/virgl-glue's extern "C" bridge instead. That bridge crate is
# built by the outer cargo/tools/build-userspace.sh pipeline, not by this
# Meson build; its static archive path is threaded in from the cross-file
# (see tools/build-mesa-virgl.sh / builders/mesa-virgl-build) the same way
# tools/etos-mesa.cross-file.in already threads llvm-config in for llvmpipe.

libvirgletos = static_library(
  'virgletos',
  'virgl_etos_winsys.c',
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_gallium_drivers, inc_virtio,
  ],
  dependencies : [idep_mesautil],
  gnu_symbol_visibility : 'hidden',
)
