/* SPDX-License-Identifier: MIT */
/*
 * Minimal etos stand-in for <sys/ioctl.h>. etos has no ioctl() syscall (no
 * device-file/fd model to speak of); this header exists only to satisfy
 * compile-time #include/declaration requirements in Mesa code paths that
 * are dead weight in a software-only (no real DRM device) surfaceless
 * build, e.g. src/util/libsync.h's DRM sync-file fence helpers. If a link
 * ever actually pulls in a call to ioctl(), that's a sign some code path
 * assumed to be dead (real GPU/DRM fences) is reachable after all — fix
 * that call site rather than adding an implementation here.
 */
#ifndef _ETOS_COMPAT_SYS_IOCTL_H_
#define _ETOS_COMPAT_SYS_IOCTL_H_

/* Linux/glibc's <sys/ioctl.h> transitively provides the _IO/_IOR/_IOW/_IOWR
 * request-encoding macros (via asm-generic/ioctl.h); mirror that here so
 * code that only includes <sys/ioctl.h> (not <sys/ioccom.h> directly, e.g.
 * src/util/libsync.h) still sees them. */
#include <sys/ioccom.h>

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int __fd, unsigned long __request, ...);

#ifdef __cplusplus
}
#endif

#endif /* _ETOS_COMPAT_SYS_IOCTL_H_ */
