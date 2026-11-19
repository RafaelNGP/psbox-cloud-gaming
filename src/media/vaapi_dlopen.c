// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
//
// Runtime dlopen() loader for libva.so.2 and libva-drm.so.2 on Linux.
//
// Why this exists (AppImage portability):
// FFmpeg's VA-API hwcontext/hwaccel (hwcontext_vaapi.c, vaapi_decode.c,
// vaapi_h264.c) calls libva C functions directly instead of loading them via
// dlopen (unlike Vulkan Video). Directly linking -lva -lva-drm would add
// DT_NEEDED entries for libva.so.2 / libva-drm.so.2 to psbox:
//   - Bundling libva.so.2 in the AppImage breaks across distros because libva
//     dlopens host Mesa/Intel/AMD *_drv_video.so drivers from distro-specific
//     paths (/usr/lib64/dri, /usr/lib/dri, /usr/lib/x86_64-linux-gnu/dri) and
//     requires ABI compatibility with the host's drivers.
//   - Omitting libva.so.2 while keeping DT_NEEDED prevents psbox from even
//     launching on minimal Linux installs without libva2.
//
// Linking this hidden-visibility static shim after libavcodec.a / libavutil.a
// satisfies FFmpeg's va* references with zero DT_NEEDED on libva*, loads the
// host's native libva.so.2 and libva-drm.so.2 at runtime, and degrades cleanly
// to Vulkan Video / CPU decode if libva is absent.

#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include <va/va.h>
#include <va/va_drm.h>

typedef VADisplay (*pfn_vaGetDisplayDRM)(int fd);
typedef VAStatus (*pfn_vaInitialize)(VADisplay dpy, int* major_version, int* minor_version);
typedef VAStatus (*pfn_vaTerminate)(VADisplay dpy);
typedef const char* (*pfn_vaErrorStr)(VAStatus error_status);
typedef const char* (*pfn_vaQueryVendorString)(VADisplay dpy);
typedef int (*pfn_vaMaxNumProfiles)(VADisplay dpy);
typedef int (*pfn_vaMaxNumImageFormats)(VADisplay dpy);
typedef VAStatus (*pfn_vaQueryConfigProfiles)(VADisplay dpy, VAProfile* profile_list, int* num_profiles);
typedef VAStatus (*pfn_vaQueryImageFormats)(VADisplay dpy, VAImageFormat* format_list, int* num_formats);
typedef VAStatus (*pfn_vaQuerySurfaceAttributes)(VADisplay dpy, VAConfigID config, VASurfaceAttrib* attrib_list,
                                                 unsigned int* num_attribs);
typedef VAStatus (*pfn_vaCreateConfig)(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                                       VAConfigAttrib* attrib_list, int num_attribs, VAConfigID* config_id);
typedef VAStatus (*pfn_vaDestroyConfig)(VADisplay dpy, VAConfigID config_id);
typedef VAStatus (*pfn_vaCreateContext)(VADisplay dpy, VAConfigID config_id, int picture_width, int picture_height,
                                        int flag, VASurfaceID* render_targets, int num_render_targets,
                                        VAContextID* context);
typedef VAStatus (*pfn_vaDestroyContext)(VADisplay dpy, VAContextID context);
typedef VAStatus (*pfn_vaCreateSurfaces)(VADisplay dpy, unsigned int format, unsigned int width, unsigned int height,
                                         VASurfaceID* surfaces, unsigned int num_surfaces, VASurfaceAttrib* attrib_list,
                                         unsigned int num_attribs);
typedef VAStatus (*pfn_vaDestroySurfaces)(VADisplay dpy, VASurfaceID* surfaces, int num_surfaces);
typedef VAStatus (*pfn_vaCreateBuffer)(VADisplay dpy, VAContextID context, VABufferType type, unsigned int size,
                                       unsigned int num_elements, void* data, VABufferID* buf_id);
typedef VAStatus (*pfn_vaDestroyBuffer)(VADisplay dpy, VABufferID buffer_id);
typedef VAStatus (*pfn_vaMapBuffer)(VADisplay dpy, VABufferID buf_id, void** pbuf);
typedef VAStatus (*pfn_vaMapBuffer2)(VADisplay dpy, VABufferID buf_id, void** pbuf, uint32_t flags);
typedef VAStatus (*pfn_vaUnmapBuffer)(VADisplay dpy, VABufferID buf_id);
typedef VAStatus (*pfn_vaBeginPicture)(VADisplay dpy, VAContextID context, VASurfaceID render_target);
typedef VAStatus (*pfn_vaRenderPicture)(VADisplay dpy, VAContextID context, VABufferID* buffers, int num_buffers);
typedef VAStatus (*pfn_vaEndPicture)(VADisplay dpy, VAContextID context);
typedef VAStatus (*pfn_vaSyncSurface)(VADisplay dpy, VASurfaceID render_target);
typedef VAStatus (*pfn_vaCreateImage)(VADisplay dpy, VAImageFormat* format, int width, int height, VAImage* image);
typedef VAStatus (*pfn_vaDeriveImage)(VADisplay dpy, VASurfaceID surface, VAImage* image);
typedef VAStatus (*pfn_vaDestroyImage)(VADisplay dpy, VAImageID image);
typedef VAStatus (*pfn_vaGetImage)(VADisplay dpy, VASurfaceID surface, int x, int y, unsigned int width,
                                   unsigned int height, VAImageID image);
typedef VAStatus (*pfn_vaPutImage)(VADisplay dpy, VASurfaceID surface, VAImageID image, int src_x, int src_y,
                                   unsigned int src_width, unsigned int src_height, int dest_x, int dest_y,
                                   unsigned int dest_width, unsigned int dest_height);
#if VA_CHECK_VERSION(0, 36, 0)
typedef VAStatus (*pfn_vaAcquireBufferHandle)(VADisplay dpy, VABufferID buf_id, VABufferInfo* buf_info);
typedef VAStatus (*pfn_vaReleaseBufferHandle)(VADisplay dpy, VABufferID buf_id);
#endif
#if VA_CHECK_VERSION(1, 1, 0)
typedef VAStatus (*pfn_vaExportSurfaceHandle)(VADisplay dpy, VASurfaceID surface_id, uint32_t mem_type, uint32_t flags,
                                              void* descriptor);
#endif
#if VA_CHECK_VERSION(0, 38, 0)
typedef VAStatus (*pfn_vaSetDriverName)(VADisplay dpy, char* driver_name);
#endif
#if VA_CHECK_VERSION(1, 0, 0)
typedef VAMessageCallback (*pfn_vaSetErrorCallback)(VADisplay dpy, VAMessageCallback callback, void* user_context);
typedef VAMessageCallback (*pfn_vaSetInfoCallback)(VADisplay dpy, VAMessageCallback callback, void* user_context);
#endif

static struct {
    void* libva;
    void* libva_drm;
    pfn_vaGetDisplayDRM vaGetDisplayDRM;
    pfn_vaInitialize vaInitialize;
    pfn_vaTerminate vaTerminate;
    pfn_vaErrorStr vaErrorStr;
    pfn_vaQueryVendorString vaQueryVendorString;
    pfn_vaMaxNumProfiles vaMaxNumProfiles;
    pfn_vaMaxNumImageFormats vaMaxNumImageFormats;
    pfn_vaQueryConfigProfiles vaQueryConfigProfiles;
    pfn_vaQueryImageFormats vaQueryImageFormats;
    pfn_vaQuerySurfaceAttributes vaQuerySurfaceAttributes;
    pfn_vaCreateConfig vaCreateConfig;
    pfn_vaDestroyConfig vaDestroyConfig;
    pfn_vaCreateContext vaCreateContext;
    pfn_vaDestroyContext vaDestroyContext;
    pfn_vaCreateSurfaces vaCreateSurfaces;
    pfn_vaDestroySurfaces vaDestroySurfaces;
    pfn_vaCreateBuffer vaCreateBuffer;
    pfn_vaDestroyBuffer vaDestroyBuffer;
    pfn_vaMapBuffer vaMapBuffer;
    pfn_vaMapBuffer2 vaMapBuffer2;
    pfn_vaUnmapBuffer vaUnmapBuffer;
    pfn_vaBeginPicture vaBeginPicture;
    pfn_vaRenderPicture vaRenderPicture;
    pfn_vaEndPicture vaEndPicture;
    pfn_vaSyncSurface vaSyncSurface;
    pfn_vaCreateImage vaCreateImage;
    pfn_vaDeriveImage vaDeriveImage;
    pfn_vaDestroyImage vaDestroyImage;
    pfn_vaGetImage vaGetImage;
    pfn_vaPutImage vaPutImage;
#if VA_CHECK_VERSION(0, 36, 0)
    pfn_vaAcquireBufferHandle vaAcquireBufferHandle;
    pfn_vaReleaseBufferHandle vaReleaseBufferHandle;
#endif
#if VA_CHECK_VERSION(1, 1, 0)
    pfn_vaExportSurfaceHandle vaExportSurfaceHandle;
#endif
#if VA_CHECK_VERSION(0, 38, 0)
    pfn_vaSetDriverName vaSetDriverName;
#endif
#if VA_CHECK_VERSION(1, 0, 0)
    pfn_vaSetErrorCallback vaSetErrorCallback;
    pfn_vaSetInfoCallback vaSetInfoCallback;
#endif
} g_va;

static pthread_once_t g_va_once = PTHREAD_ONCE_INIT;

#define LOAD_VA_SYM(handle, name) g_va.name = (pfn_##name)dlsym((handle), #name)

static void vaapi_load_once(void) {
    g_va.libva = dlopen("libva.so.2", RTLD_LAZY | RTLD_LOCAL);
    g_va.libva_drm = dlopen("libva-drm.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (!g_va.libva || !g_va.libva_drm) {
        if (g_va.libva_drm) {
            dlclose(g_va.libva_drm);
            g_va.libva_drm = NULL;
        }
        if (g_va.libva) {
            dlclose(g_va.libva);
            g_va.libva = NULL;
        }
        return;
    }

    LOAD_VA_SYM(g_va.libva_drm, vaGetDisplayDRM);
    LOAD_VA_SYM(g_va.libva, vaInitialize);
    LOAD_VA_SYM(g_va.libva, vaTerminate);
    LOAD_VA_SYM(g_va.libva, vaErrorStr);
    LOAD_VA_SYM(g_va.libva, vaQueryVendorString);
    LOAD_VA_SYM(g_va.libva, vaMaxNumProfiles);
    LOAD_VA_SYM(g_va.libva, vaMaxNumImageFormats);
    LOAD_VA_SYM(g_va.libva, vaQueryConfigProfiles);
    LOAD_VA_SYM(g_va.libva, vaQueryImageFormats);
    LOAD_VA_SYM(g_va.libva, vaQuerySurfaceAttributes);
    LOAD_VA_SYM(g_va.libva, vaCreateConfig);
    LOAD_VA_SYM(g_va.libva, vaDestroyConfig);
    LOAD_VA_SYM(g_va.libva, vaCreateContext);
    LOAD_VA_SYM(g_va.libva, vaDestroyContext);
    LOAD_VA_SYM(g_va.libva, vaCreateSurfaces);
    LOAD_VA_SYM(g_va.libva, vaDestroySurfaces);
    LOAD_VA_SYM(g_va.libva, vaCreateBuffer);
    LOAD_VA_SYM(g_va.libva, vaDestroyBuffer);
    LOAD_VA_SYM(g_va.libva, vaMapBuffer);
    LOAD_VA_SYM(g_va.libva, vaMapBuffer2);
    LOAD_VA_SYM(g_va.libva, vaUnmapBuffer);
    LOAD_VA_SYM(g_va.libva, vaBeginPicture);
    LOAD_VA_SYM(g_va.libva, vaRenderPicture);
    LOAD_VA_SYM(g_va.libva, vaEndPicture);
    LOAD_VA_SYM(g_va.libva, vaSyncSurface);
    LOAD_VA_SYM(g_va.libva, vaCreateImage);
    LOAD_VA_SYM(g_va.libva, vaDeriveImage);
    LOAD_VA_SYM(g_va.libva, vaDestroyImage);
    LOAD_VA_SYM(g_va.libva, vaGetImage);
    LOAD_VA_SYM(g_va.libva, vaPutImage);
#if VA_CHECK_VERSION(0, 36, 0)
    LOAD_VA_SYM(g_va.libva, vaAcquireBufferHandle);
    LOAD_VA_SYM(g_va.libva, vaReleaseBufferHandle);
#endif
#if VA_CHECK_VERSION(1, 1, 0)
    LOAD_VA_SYM(g_va.libva, vaExportSurfaceHandle);
#endif
#if VA_CHECK_VERSION(0, 38, 0)
    LOAD_VA_SYM(g_va.libva, vaSetDriverName);
#endif
#if VA_CHECK_VERSION(1, 0, 0)
    LOAD_VA_SYM(g_va.libva, vaSetErrorCallback);
    LOAD_VA_SYM(g_va.libva, vaSetInfoCallback);
#endif
}

static void vaapi_ensure_loaded(void) {
    pthread_once(&g_va_once, vaapi_load_once);
}

VADisplay vaGetDisplayDRM(int fd) {
    vaapi_ensure_loaded();
    return g_va.vaGetDisplayDRM ? g_va.vaGetDisplayDRM(fd) : NULL;
}

VAStatus vaInitialize(VADisplay dpy, int* major_version, int* minor_version) {
    vaapi_ensure_loaded();
    return g_va.vaInitialize ? g_va.vaInitialize(dpy, major_version, minor_version) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaTerminate(VADisplay dpy) {
    vaapi_ensure_loaded();
    return g_va.vaTerminate ? g_va.vaTerminate(dpy) : VA_STATUS_ERROR_OPERATION_FAILED;
}

const char* vaErrorStr(VAStatus error_status) {
    vaapi_ensure_loaded();
    return g_va.vaErrorStr ? g_va.vaErrorStr(error_status) : "libva not available";
}

const char* vaQueryVendorString(VADisplay dpy) {
    vaapi_ensure_loaded();
    return g_va.vaQueryVendorString ? g_va.vaQueryVendorString(dpy) : NULL;
}

int vaMaxNumProfiles(VADisplay dpy) {
    vaapi_ensure_loaded();
    return g_va.vaMaxNumProfiles ? g_va.vaMaxNumProfiles(dpy) : 0;
}

int vaMaxNumImageFormats(VADisplay dpy) {
    vaapi_ensure_loaded();
    return g_va.vaMaxNumImageFormats ? g_va.vaMaxNumImageFormats(dpy) : 0;
}

VAStatus vaQueryConfigProfiles(VADisplay dpy, VAProfile* profile_list, int* num_profiles) {
    vaapi_ensure_loaded();
    return g_va.vaQueryConfigProfiles ? g_va.vaQueryConfigProfiles(dpy, profile_list, num_profiles)
                                      : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaQueryImageFormats(VADisplay dpy, VAImageFormat* format_list, int* num_formats) {
    vaapi_ensure_loaded();
    return g_va.vaQueryImageFormats ? g_va.vaQueryImageFormats(dpy, format_list, num_formats)
                                    : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaQuerySurfaceAttributes(VADisplay dpy, VAConfigID config, VASurfaceAttrib* attrib_list,
                                  unsigned int* num_attribs) {
    vaapi_ensure_loaded();
    return g_va.vaQuerySurfaceAttributes ? g_va.vaQuerySurfaceAttributes(dpy, config, attrib_list, num_attribs)
                                         : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaCreateConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib* attrib_list,
                        int num_attribs, VAConfigID* config_id) {
    vaapi_ensure_loaded();
    return g_va.vaCreateConfig ? g_va.vaCreateConfig(dpy, profile, entrypoint, attrib_list, num_attribs, config_id)
                               : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaDestroyConfig(VADisplay dpy, VAConfigID config_id) {
    vaapi_ensure_loaded();
    return g_va.vaDestroyConfig ? g_va.vaDestroyConfig(dpy, config_id) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaCreateContext(VADisplay dpy, VAConfigID config_id, int picture_width, int picture_height, int flag,
                         VASurfaceID* render_targets, int num_render_targets, VAContextID* context) {
    vaapi_ensure_loaded();
    return g_va.vaCreateContext
               ? g_va.vaCreateContext(dpy, config_id, picture_width, picture_height, flag, render_targets,
                                      num_render_targets, context)
               : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaDestroyContext(VADisplay dpy, VAContextID context) {
    vaapi_ensure_loaded();
    return g_va.vaDestroyContext ? g_va.vaDestroyContext(dpy, context) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaCreateSurfaces(VADisplay dpy, unsigned int format, unsigned int width, unsigned int height,
                          VASurfaceID* surfaces, unsigned int num_surfaces, VASurfaceAttrib* attrib_list,
                          unsigned int num_attribs) {
    vaapi_ensure_loaded();
    return g_va.vaCreateSurfaces
               ? g_va.vaCreateSurfaces(dpy, format, width, height, surfaces, num_surfaces, attrib_list, num_attribs)
               : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaDestroySurfaces(VADisplay dpy, VASurfaceID* surfaces, int num_surfaces) {
    vaapi_ensure_loaded();
    return g_va.vaDestroySurfaces ? g_va.vaDestroySurfaces(dpy, surfaces, num_surfaces)
                                  : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaCreateBuffer(VADisplay dpy, VAContextID context, VABufferType type, unsigned int size,
                        unsigned int num_elements, void* data, VABufferID* buf_id) {
    vaapi_ensure_loaded();
    return g_va.vaCreateBuffer ? g_va.vaCreateBuffer(dpy, context, type, size, num_elements, data, buf_id)
                               : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaDestroyBuffer(VADisplay dpy, VABufferID buffer_id) {
    vaapi_ensure_loaded();
    return g_va.vaDestroyBuffer ? g_va.vaDestroyBuffer(dpy, buffer_id) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaMapBuffer(VADisplay dpy, VABufferID buf_id, void** pbuf) {
    vaapi_ensure_loaded();
    return g_va.vaMapBuffer ? g_va.vaMapBuffer(dpy, buf_id, pbuf) : VA_STATUS_ERROR_OPERATION_FAILED;
}

#if VA_CHECK_VERSION(1, 21, 0)
VAStatus vaMapBuffer2(VADisplay dpy, VABufferID buf_id, void** pbuf, uint32_t flags) {
    vaapi_ensure_loaded();
    if (g_va.vaMapBuffer2) return g_va.vaMapBuffer2(dpy, buf_id, pbuf, flags);
    if (g_va.vaMapBuffer) return g_va.vaMapBuffer(dpy, buf_id, pbuf);
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}
#endif

VAStatus vaUnmapBuffer(VADisplay dpy, VABufferID buf_id) {
    vaapi_ensure_loaded();
    return g_va.vaUnmapBuffer ? g_va.vaUnmapBuffer(dpy, buf_id) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaBeginPicture(VADisplay dpy, VAContextID context, VASurfaceID render_target) {
    vaapi_ensure_loaded();
    return g_va.vaBeginPicture ? g_va.vaBeginPicture(dpy, context, render_target) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaRenderPicture(VADisplay dpy, VAContextID context, VABufferID* buffers, int num_buffers) {
    vaapi_ensure_loaded();
    return g_va.vaRenderPicture ? g_va.vaRenderPicture(dpy, context, buffers, num_buffers)
                                : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaEndPicture(VADisplay dpy, VAContextID context) {
    vaapi_ensure_loaded();
    return g_va.vaEndPicture ? g_va.vaEndPicture(dpy, context) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaSyncSurface(VADisplay dpy, VASurfaceID render_target) {
    vaapi_ensure_loaded();
    return g_va.vaSyncSurface ? g_va.vaSyncSurface(dpy, render_target) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaCreateImage(VADisplay dpy, VAImageFormat* format, int width, int height, VAImage* image) {
    vaapi_ensure_loaded();
    return g_va.vaCreateImage ? g_va.vaCreateImage(dpy, format, width, height, image)
                              : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaDeriveImage(VADisplay dpy, VASurfaceID surface, VAImage* image) {
    vaapi_ensure_loaded();
    return g_va.vaDeriveImage ? g_va.vaDeriveImage(dpy, surface, image) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaDestroyImage(VADisplay dpy, VAImageID image) {
    vaapi_ensure_loaded();
    return g_va.vaDestroyImage ? g_va.vaDestroyImage(dpy, image) : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaGetImage(VADisplay dpy, VASurfaceID surface, int x, int y, unsigned int width, unsigned int height,
                    VAImageID image) {
    vaapi_ensure_loaded();
    return g_va.vaGetImage ? g_va.vaGetImage(dpy, surface, x, y, width, height, image)
                           : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus vaPutImage(VADisplay dpy, VASurfaceID surface, VAImageID image, int src_x, int src_y, unsigned int src_width,
                    unsigned int src_height, int dest_x, int dest_y, unsigned int dest_width,
                    unsigned int dest_height) {
    vaapi_ensure_loaded();
    return g_va.vaPutImage ? g_va.vaPutImage(dpy, surface, image, src_x, src_y, src_width, src_height, dest_x, dest_y,
                                             dest_width, dest_height)
                           : VA_STATUS_ERROR_OPERATION_FAILED;
}

#if VA_CHECK_VERSION(0, 36, 0)
VAStatus vaAcquireBufferHandle(VADisplay dpy, VABufferID buf_id, VABufferInfo* buf_info) {
    vaapi_ensure_loaded();
    return g_va.vaAcquireBufferHandle ? g_va.vaAcquireBufferHandle(dpy, buf_id, buf_info)
                                      : VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus vaReleaseBufferHandle(VADisplay dpy, VABufferID buf_id) {
    vaapi_ensure_loaded();
    return g_va.vaReleaseBufferHandle ? g_va.vaReleaseBufferHandle(dpy, buf_id) : VA_STATUS_ERROR_UNIMPLEMENTED;
}
#endif

#if VA_CHECK_VERSION(1, 1, 0)
VAStatus vaExportSurfaceHandle(VADisplay dpy, VASurfaceID surface_id, uint32_t mem_type, uint32_t flags,
                               void* descriptor) {
    vaapi_ensure_loaded();
    return g_va.vaExportSurfaceHandle ? g_va.vaExportSurfaceHandle(dpy, surface_id, mem_type, flags, descriptor)
                                      : VA_STATUS_ERROR_UNIMPLEMENTED;
}
#endif

#if VA_CHECK_VERSION(0, 38, 0)
VAStatus vaSetDriverName(VADisplay dpy, char* driver_name) {
    vaapi_ensure_loaded();
    return g_va.vaSetDriverName ? g_va.vaSetDriverName(dpy, driver_name) : VA_STATUS_ERROR_UNIMPLEMENTED;
}
#endif

#if VA_CHECK_VERSION(1, 0, 0)
VAMessageCallback vaSetErrorCallback(VADisplay dpy, VAMessageCallback callback, void* user_context) {
    vaapi_ensure_loaded();
    return g_va.vaSetErrorCallback ? g_va.vaSetErrorCallback(dpy, callback, user_context) : NULL;
}

VAMessageCallback vaSetInfoCallback(VADisplay dpy, VAMessageCallback callback, void* user_context) {
    vaapi_ensure_loaded();
    return g_va.vaSetInfoCallback ? g_va.vaSetInfoCallback(dpy, callback, user_context) : NULL;
}
#endif
