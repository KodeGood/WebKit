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

#pragma once

#if OS(OHOS)

#include "OHOSBinder.h"
#include <wtf/ArgumentCoder.h>
#include <wtf/ohos/RefPtrOHOS.h>

namespace IPC {

template<> struct ArgumentCoder<RefPtr<OH_NativeBuffer>> {
    static void encode(Encoder&, const RefPtr<OH_NativeBuffer>&);
    static void encode(Encoder&, RefPtr<OH_NativeBuffer>&&);
    static std::optional<RefPtr<OH_NativeBuffer>> decode(Decoder&);
};

// A binder endpoint rides as a message attachment; ConnectionOHOS::sendOutgoingMessage
// serializes it with OH_IPCParcel_WriteRemoteStub and the receive path materializes a
// proxy with OH_IPCParcel_ReadRemoteProxy. Move-only, so only an rvalue encoder.
template<> struct ArgumentCoder<OHOSBinderHandle> {
    static void encode(Encoder&, OHOSBinderHandle&&);
    static std::optional<OHOSBinderHandle> decode(Decoder&);
};

}

#endif // OS(OHOS)
