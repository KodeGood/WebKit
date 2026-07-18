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
#include "ArgumentCodersOHOS.h"

#if OS(OHOS)

#include "ArgumentCodersUnix.h"
#include "Decoder.h"
#include "Encoder.h"
#include <IPCKit/ipc_cremote_object.h>
#include <wtf/unix/UnixFileDescriptor.h>

namespace IPC {

void OHOSBinderHandle::reset()
{
    if (m_proxy) {
        OH_IPCRemoteProxy_Destroy(m_proxy);
        m_proxy = nullptr;
    }
    // m_stub is borrowed from the hosting Connection and is never destroyed here.
    m_stub = nullptr;
}

std::optional<UnixFileDescriptor> ArgumentCoder<UnixFileDescriptor>::decode(Decoder& decoder)
{
    if (auto attachment = decoder.takeLastAttachment()) {
        if (holdsAlternative<UnixFileDescriptor>(attachment.value()))
            return { WTFMove(get<UnixFileDescriptor>(attachment.value())) };
    }
    return std::nullopt;
}

void ArgumentCoder<RefPtr<OH_NativeBuffer>>::encode(Encoder& encoder, const RefPtr<OH_NativeBuffer>& buffer)
{
    encoder.addAttachment(RefPtr { buffer });
}

void ArgumentCoder<RefPtr<OH_NativeBuffer>>::encode(Encoder& encoder, RefPtr<OH_NativeBuffer>&& buffer)
{
    encoder.addAttachment(WTFMove(buffer));
}

std::optional<RefPtr<OH_NativeBuffer>> ArgumentCoder<RefPtr<OH_NativeBuffer>>::decode(Decoder& decoder)
{
    if (auto attachment = decoder.takeLastAttachment()) {
        if (holdsAlternative<RefPtr<OH_NativeBuffer>>(attachment.value()))
            return { WTFMove(get<RefPtr<OH_NativeBuffer>>(attachment.value())) };
    }
    return std::nullopt;
}

void ArgumentCoder<OHOSBinderHandle>::encode(Encoder& encoder, OHOSBinderHandle&& handle)
{
    encoder.addAttachment(WTFMove(handle));
}

std::optional<OHOSBinderHandle> ArgumentCoder<OHOSBinderHandle>::decode(Decoder& decoder)
{
    if (auto attachment = decoder.takeLastAttachment()) {
        if (holdsAlternative<OHOSBinderHandle>(attachment.value()))
            return { WTFMove(get<OHOSBinderHandle>(attachment.value())) };
    }
    return std::nullopt;
}

}

#endif // OS(OHOS)
