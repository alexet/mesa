# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_amd = include_directories('.')
# etos: common/etos carries a stand-in <amdgpu.h> (libdrm's types only --
# there is no libdrm on etos), which ac_linux_drm.h includes by that name.
# Added to inc_amd rather than to one target's own list because every AMD
# target that includes that header needs it.
if get_option('etos-amdgpu')
  inc_amd = include_directories('.', 'common/etos')
endif

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif

if with_amd_vk or with_gallium_radeonsi
  subdir('addrlib')
  subdir('common')
  if amd_with_llvm
    subdir('llvm')
  else
    libamd_common_llvm = []
  endif
  subdir('compiler')
endif

if with_amd_vk
  subdir('vulkan')
  if with_aco_tests
    subdir('compiler/tests')
  endif
endif

if with_gallium_radeonsi
  subdir('vpelib')
  subdir('gmlib')
  subdir('lanczoslib')
endif
