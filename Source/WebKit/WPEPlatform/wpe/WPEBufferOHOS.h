
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

#ifndef WPEBufferOHOS_h
#define WPEBufferOHOS_h

#if !defined(__WPE_PLATFORM_H_INSIDE__) && !defined(BUILDING_WEBKIT)
#error "Only <wpe/wpe-platform.h> can be included directly."
#endif

#ifndef __GI_SCANNER__

#include <glib-object.h>
#include <wpe/WPEDefines.h>
#include <wpe/WPEBuffer.h>

G_BEGIN_DECLS

typedef struct OH_NativeBuffer OH_NativeBuffer;

#define WPE_TYPE_BUFFER_OHOS (wpe_buffer_ohos_get_type())
WPE_API G_DECLARE_FINAL_TYPE (WPEBufferOHOS, wpe_buffer_ohos, WPE, BUFFER_OHOS, WPEBuffer)

WPE_API WPEBufferOHOS     *wpe_buffer_ohos_new                 (WPEDisplay       *display,
                                                                OH_NativeBuffer  *onb);

WPE_API OH_NativeBuffer   *wpe_buffer_ohos_get_native_buffer   (WPEBufferOHOS *buffer);

WPE_API guint32            wpe_buffer_ohos_get_format          (WPEBufferOHOS *buffer);

/* Explicit GPU sync fences (sync_file fds, EGL_ANDROID_native_fence_sync). Ownership transfers
 * on set; take returns the fd (or -1) and clears it. Rendering fence: producer -> consumer, wait
 * before sampling. Release fence: consumer -> producer, wait before reusing the buffer. */
WPE_API void               wpe_buffer_ohos_set_rendering_fence (WPEBufferOHOS *buffer, int fenceFD);
WPE_API int                wpe_buffer_ohos_take_rendering_fence(WPEBufferOHOS *buffer);
WPE_API void               wpe_buffer_ohos_set_release_fence   (WPEBufferOHOS *buffer, int fenceFD);
WPE_API int                wpe_buffer_ohos_take_release_fence  (WPEBufferOHOS *buffer);

G_END_DECLS

#endif /* !__GI_SCANNER__ */

#endif /* !WPEBufferOHOS_h */
