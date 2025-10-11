/*
 * Copyright (C) 2025 Jani Hautakangas <jani@kodegood.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "BufferFormatOHOS.h"

#if OS(OHOS)

#include <native_buffer/native_buffer.h>
#include <drm/drm_fourcc.h>

namespace WebCore {

std::optional<uint32_t> toOHNativeBufferFormat(const FourCC fourcc)
{
    switch (fourcc.value) {
    case DRM_FORMAT_RGBX8888:
        return NATIVEBUFFER_PIXEL_FMT_RGBX_8888;
    case DRM_FORMAT_RGBA8888:
        return NATIVEBUFFER_PIXEL_FMT_RGBA_8888;
    case DRM_FORMAT_RGB565:
        return NATIVEBUFFER_PIXEL_FMT_RGB_565;
    case DRM_FORMAT_RGBA1010102:
        return NATIVEBUFFER_PIXEL_FMT_RGBA_1010102;
    case DRM_FORMAT_RGB888:
        return NATIVEBUFFER_PIXEL_FMT_RGB_888;
    default:
        return std::nullopt;
    }
}

} // namespace WebCore

#endif // OS(OHOS)
