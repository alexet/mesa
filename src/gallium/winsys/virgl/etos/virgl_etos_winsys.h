/*
 * Copyright 2026 Alexander Eyers-Taylor
 * SPDX-License-Identifier: MIT
 */
#ifndef VIRGL_ETOS_WINSYS_H
#define VIRGL_ETOS_WINSYS_H

#include "pipe/p_screen.h"

/* Builds the etos-native virgl_winsys and hands it straight to
 * virgl_create_screen() -- see this directory's virgl_etos_winsys.c for why
 * that bypasses pipe-loader/DRM/vtest entirely. Returns NULL if the
 * underlying etos_virgl_init() bridge call fails (e.g. this process was
 * never handed a GpuDevice capability -- see utility/virgl-glue's doc).
 */
struct pipe_screen *
virgl_etos_create_screen(const struct pipe_screen_config *config);

#endif
