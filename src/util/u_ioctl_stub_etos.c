/* SPDX-License-Identifier: MIT */
/*
 * etos has no ioctl() syscall (no device-file/fd model). This provides the
 * symbol so DRM-sync-file fence code (src/util/libsync.h, only reachable
 * from src/gallium/frontends/dri/dri2.c's real-hardware paths — dead code
 * on etos's software-only, no-DRM-device surfaceless build) links. If this
 * is ever actually called, that means some assumed-dead hardware-fence path
 * became reachable; fix the call site rather than "implementing" ioctl.
 */
#include <sys/ioctl.h> /* etos_compat/sys/ioctl.h: prototype (-Werror=missing-prototypes) */

int ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    (void)request;
    return -1;
}
