/* SPDX-License-Identifier: MIT */
/*
 * Minimal etos stand-in for the BSD <sys/ioccom.h> ioctl-encoding macros.
 *
 * include/drm-uapi/drm.h only needs these to *define* DRM_IOCTL_* constants
 * at compile time (see its DRM_IO/DRM_IOR/DRM_IOW/DRM_IOWR macros); etos has
 * no ioctl() syscall and no real DRM device, so nothing ever actually issues
 * one of these encoded values — this header exists purely so the DRM-uapi
 * headers (pulled in transitively by src/loader, which the static EGL/DRI2
 * build still links against for its dead hardware-probe code paths) compile.
 * Encoding matches the standard BSD/Linux ioctl layout so the numeric
 * values, if ever inspected, are at least the conventional ones.
 */
#ifndef _ETOS_COMPAT_SYS_IOCCOM_H_
#define _ETOS_COMPAT_SYS_IOCCOM_H_

#define IOCPARM_MASK  0x1fff
#define IOC_VOID      0x20000000
#define IOC_OUT       0x40000000
#define IOC_IN        0x80000000
#define IOC_INOUT     (IOC_IN | IOC_OUT)

#define _IOC(inout, group, num, len) \
    ((unsigned long)((inout) | (((len) & IOCPARM_MASK) << 16) | ((group) << 8) | (num)))

#define _IO(g, n)        _IOC(IOC_VOID,  (g), (n), 0)
#define _IOR(g, n, t)    _IOC(IOC_OUT,   (g), (n), sizeof(t))
#define _IOW(g, n, t)    _IOC(IOC_IN,    (g), (n), sizeof(t))
#define _IOWR(g, n, t)   _IOC(IOC_INOUT, (g), (n), sizeof(t))

#endif /* _ETOS_COMPAT_SYS_IOCCOM_H_ */
