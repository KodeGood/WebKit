/*
 * Copyright (C) 2025 Jani Hautakangas <jani@kodegood.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WPEBufferOHOS.h"

#if OS(OHOS)
#include <drm/drm_fourcc.h>
#include <epoxy/egl.h>
#include <native_buffer/native_buffer.h>
#include <native_window/external_window.h>
#include <unistd.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/glib/WTFGType.h>
#include <wtf/unix/UnixFileDescriptor.h>

/**
 * WPEBufferOHOS:
 *
 */
struct _WPEBufferOHOSPrivate {
    OH_NativeBuffer* onb;
    EGLImage eglImage { EGL_NO_IMAGE };
    int renderingFenceFD { -1 };
    int releaseFenceFD { -1 };
};

static void closeFenceFD(int& fd)
{
    if (fd >= 0)
        close(fd);
    fd = -1;
}
WEBKIT_DEFINE_FINAL_TYPE(WPEBufferOHOS, wpe_buffer_ohos, WPE_TYPE_BUFFER, WPEBuffer)

static std::function<EGLImage(EGLDisplay, OH_NativeBuffer*)> s_createImage = nullptr;
static PFNEGLDESTROYIMAGEKHRPROC s_eglDestroyImage = nullptr;
static PFNEGLCREATEIMAGEKHRPROC s_eglCreateImageKHR = nullptr;
static PFNEGLCREATEIMAGEPROC s_eglCreateImage = nullptr;

static EGLImage createImageEGL15(EGLDisplay display, OH_NativeBuffer* onb)
{
    auto onwb = OH_NativeWindow_CreateNativeWindowBufferFromNativeBuffer(onb);
    static constexpr std::array<EGLAttrib, 3> attributes = { EGL_IMAGE_PRESERVED, EGL_TRUE, EGL_NONE };
    return s_eglCreateImage(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_OHOS, onwb, attributes.data());
}

static EGLImage createImageKHRImageBase(EGLDisplay display, OH_NativeBuffer* onb)
{
    auto onwb = OH_NativeWindow_CreateNativeWindowBufferFromNativeBuffer(onb);
    static constexpr std::array<EGLint, 3> attributes = { EGL_IMAGE_PRESERVED, EGL_TRUE, EGL_NONE };
    return s_eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_OHOS, onwb, attributes.data());
}

static void wpeBufferOHOSDisposeEGLImageIfNeeded(WPEBufferOHOS* ohosBuffer)
{
    RELEASE_ASSERT(s_eglDestroyImage);

    auto* priv = ohosBuffer->priv;
    if (priv->eglImage == EGL_NO_IMAGE)
        return;

    auto* eglImage = std::exchange(priv->eglImage, EGL_NO_IMAGE);
    auto* display = wpe_buffer_get_display(WPE_BUFFER(ohosBuffer));
    if (!display)
        return;

    if (auto* eglDisplay = wpe_display_get_egl_display(display, nullptr))
        s_eglDestroyImage(eglDisplay, eglImage);
}

static void wpeBufferOHOSDispose(GObject* object)
{
    auto* ohosBuffer = WPE_BUFFER_OHOS(object);

    wpeBufferOHOSDisposeEGLImageIfNeeded(ohosBuffer);
    g_clear_pointer(&ohosBuffer->priv->onb, OH_NativeBuffer_Unreference);
    closeFenceFD(ohosBuffer->priv->renderingFenceFD);
    closeFenceFD(ohosBuffer->priv->releaseFenceFD);

    G_OBJECT_CLASS(wpe_buffer_ohos_parent_class)->dispose(object);
}

static gpointer wpeBufferOHOSImportToEGLImage(WPEBuffer* buffer, GError** error)
{
    auto* priv = WPE_BUFFER_OHOS(buffer)->priv;
    auto* display = wpe_buffer_get_display(buffer);
    if (!display) {
        priv->eglImage = EGL_NO_IMAGE;
        g_set_error_literal(error, WPE_BUFFER_ERROR, WPE_BUFFER_ERROR_IMPORT_FAILED, "The WPE display of the buffer has already been closed");
        return nullptr;
    }

    if (priv->eglImage)
        return priv->eglImage;

    GUniqueOutPtr<GError> eglError;
    auto* eglDisplay = wpe_display_get_egl_display(display, &eglError.outPtr());
    if (eglDisplay == EGL_NO_DISPLAY) {
        g_set_error(error, WPE_BUFFER_ERROR, WPE_BUFFER_ERROR_IMPORT_FAILED, "Failed to get EGLDisplay when importing buffer to EGL image: %s", eglError->message);
        return nullptr;
    }

    if (!s_createImage) {
        if (epoxy_egl_version(eglDisplay) >= 15) {
            s_eglCreateImage = reinterpret_cast<PFNEGLCREATEIMAGEPROC>(epoxy_eglGetProcAddress("eglCreateImage"));
            s_eglDestroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEPROC>(epoxy_eglGetProcAddress("eglDestroyImage"));
            if (s_eglCreateImage && s_eglDestroyImage)
                s_createImage = createImageEGL15;
        }
        if (!s_createImage && epoxy_has_egl_extension(eglDisplay, "EGL_KHR_image_base")) {
            s_eglCreateImageKHR = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(epoxy_eglGetProcAddress("eglCreateImageKHR"));
            s_eglDestroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(epoxy_eglGetProcAddress("eglDestroyImageKHR"));
            if (s_eglCreateImageKHR && s_eglDestroyImage)
                s_createImage = createImageKHRImageBase;
        }
        if (!s_createImage) {
            g_set_error_literal(error, WPE_BUFFER_ERROR, WPE_BUFFER_ERROR_IMPORT_FAILED, "No EGLImage support, EGL 1.5 or EGL_KHR_image needed");
            return nullptr;
        }
    }
    RELEASE_ASSERT(s_createImage);
    RELEASE_ASSERT(s_eglDestroyImage);

    priv->eglImage = s_createImage(eglDisplay, priv->onb);
    if (!priv->eglImage)
        g_set_error(error, WPE_BUFFER_ERROR, WPE_BUFFER_ERROR_IMPORT_FAILED, "Failed to import buffer to EGL image: eglCreateImageKHR failed with error %#04x", eglGetError());
    return priv->eglImage;
}

static void wpe_buffer_ohos_class_init(WPEBufferOHOSClass* bufferOHOSClass)
{
    GObjectClass* objectClass = G_OBJECT_CLASS(bufferOHOSClass);
    objectClass->dispose = wpeBufferOHOSDispose;

    // TODO: Implement import_to_pixels vfunc.
    WPEBufferClass* bufferClass = WPE_BUFFER_CLASS(bufferOHOSClass);
    bufferClass->import_to_egl_image = wpeBufferOHOSImportToEGLImage;
}

/**
 * wpe_buffer_ohos_new: (constructor):
 * @display: a #WPEDisplay
 * @onb: an #OH_NativeBuffer
 *
 * Create a new #WPEBufferOHOS for the given buffer.
 *
 * The reference count of the @onb will be incremented using
 * %OH_NativeBuffer_Reference().
 *
 * Returns: (transfer full): a #WPEBufferOHOS
 */
WPEBufferOHOS* wpe_buffer_ohos_new(WPEDisplay* display, OH_NativeBuffer* onb)
{
    g_return_val_if_fail(WPE_IS_DISPLAY(display), nullptr);
    g_return_val_if_fail(onb != nullptr, nullptr);

    OH_NativeBuffer_Reference(onb);

    OH_NativeBuffer_Config config;
    OH_NativeBuffer_GetConfig(onb, &config);

    auto* buffer = WPE_BUFFER_OHOS(g_object_new(WPE_TYPE_BUFFER_OHOS,
        "display", display,
        "width", config.width,
        "height", config.height,
        nullptr));

    buffer->priv->onb = onb;

    return buffer;
}

/**
 * wpe_buffer_ohos_get_native_buffer:
 * @buffer: a #WPEBufferOHOS
 *
 * Get the underlying #OH_NativeBuffer for @buffer.
 * Note that the returned #OH_NativeBuffer might be destroyed along with
 * the @buffer; and `OH_NativeBuffer_Reference()` may be called on the
 * returned value to ensure that the underlying #OH_NativeBuffer stays
 * valid.
 *
 * Returns: (transfer none): a valid #OH_NativeBuffer pointer.
 */
OH_NativeBuffer* wpe_buffer_ohos_get_native_buffer(WPEBufferOHOS* buffer)
{
    g_return_val_if_fail(WPE_IS_BUFFER_OHOS(buffer), nullptr);

    return buffer->priv->onb;
}

/**
 * wpe_buffer_ohos_get_format:
 * @buffer: a #WPEBufferOHOS
 *
 * Get the pixel format of the @buffer as a DRM FourCC code.
 *
 * Returns: a DRM FourCC code.
 */
guint32 wpe_buffer_ohos_get_format(WPEBufferOHOS* buffer)
{
    g_return_val_if_fail(WPE_IS_BUFFER_OHOS(buffer), 0);

    OH_NativeBuffer_Config config;
    OH_NativeBuffer_GetConfig(buffer->priv->onb, &config);

    switch (config.format) {
    case NATIVEBUFFER_PIXEL_FMT_RGBA_8888:
        return DRM_FORMAT_RGBA8888;
    case NATIVEBUFFER_PIXEL_FMT_RGBX_8888:
        return DRM_FORMAT_RGBX8888;
    case NATIVEBUFFER_PIXEL_FMT_RGB_888:
        return DRM_FORMAT_RGB888;
    case NATIVEBUFFER_PIXEL_FMT_RGB_565:
        return DRM_FORMAT_RGB565;
    case NATIVEBUFFER_PIXEL_FMT_RGBA_1010102:
        return DRM_FORMAT_RGBA1010102;
    default:
        RELEASE_ASSERT_NOT_REACHED();
        return 0;
    }

    RELEASE_ASSERT_NOT_REACHED();
    return 0;
}

void wpe_buffer_ohos_set_rendering_fence(WPEBufferOHOS* buffer, int fenceFD)
{
    g_return_if_fail(WPE_IS_BUFFER_OHOS(buffer));
    closeFenceFD(buffer->priv->renderingFenceFD);
    buffer->priv->renderingFenceFD = fenceFD;
}

int wpe_buffer_ohos_take_rendering_fence(WPEBufferOHOS* buffer)
{
    g_return_val_if_fail(WPE_IS_BUFFER_OHOS(buffer), -1);
    return std::exchange(buffer->priv->renderingFenceFD, -1);
}

void wpe_buffer_ohos_set_release_fence(WPEBufferOHOS* buffer, int fenceFD)
{
    g_return_if_fail(WPE_IS_BUFFER_OHOS(buffer));
    closeFenceFD(buffer->priv->releaseFenceFD);
    buffer->priv->releaseFenceFD = fenceFD;
}

int wpe_buffer_ohos_take_release_fence(WPEBufferOHOS* buffer)
{
    g_return_val_if_fail(WPE_IS_BUFFER_OHOS(buffer), -1);
    return std::exchange(buffer->priv->releaseFenceFD, -1);
}
#endif // OS(OHOS)
