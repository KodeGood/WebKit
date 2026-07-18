/*
 * Copyright (C) 2026 Jani Hautakangas <jani@kodegood.com>
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if OS(OHOS)

#include <utility>
#include <wtf/Compiler.h>
#include <wtf/Noncopyable.h>

// Opaque IPCKit types (libipc_capi.so). Redundant with the forward declarations in
// Connection.h / ConnectionHandle.h; repeating an identical `typedef struct` is well
// formed and keeps this header self-contained.
typedef struct OHIPCRemoteStub OHIPCRemoteStub;
typedef struct OHIPCRemoteProxy OHIPCRemoteProxy;

namespace IPC {

// A transferable OHOS binder endpoint used as the serializable payload of an
// IPC::ConnectionHandle and as an IPC::Attachment arm.
//
// It is asymmetric by construction: the producing side wraps a *stub* it borrows
// (serialized with OH_IPCParcel_WriteRemoteStub — the stub keeps being owned by the
// Connection that hosts it), and the receiving side holds a *proxy* it owns
// (materialized with OH_IPCParcel_ReadRemoteProxy). Ownership of a decoded proxy stays
// with this object until releaseProxy() hands it to a Connection; otherwise the
// destructor releases it. The coder lives in ohos/ArgumentCodersOHOS.cpp.
//
// A decoded (proxy-holding) handle can be re-encoded: WebKit brokers connections by
// decoding a ConnectionHandle in one process and forwarding it to another (e.g. the
// UIProcess relays the NetworkProcess's client handle to the WebProcess), in which case
// the send path serializes the proxy with OH_IPCParcel_WriteRemoteProxy; the receiver
// materializes a proxy to the same underlying stub either way.
class OHOSBinderHandle {
    WTF_MAKE_NONCOPYABLE(OHOSBinderHandle);
public:
    OHOSBinderHandle() = default;

    // Borrowing wrapper around a stub for the encode side.
    explicit OHOSBinderHandle(OHIPCRemoteStub* stub)
        : m_stub(stub)
    {
    }

    // Owning wrapper around a decoded proxy for the receive side.
    explicit OHOSBinderHandle(OHIPCRemoteProxy* proxy)
        : m_proxy(proxy)
    {
    }

    OHOSBinderHandle(OHOSBinderHandle&& other)
        : m_stub(std::exchange(other.m_stub, nullptr))
        , m_proxy(std::exchange(other.m_proxy, nullptr))
    {
    }

    OHOSBinderHandle& operator=(OHOSBinderHandle&& other)
    {
        if (this != &other) {
            reset();
            m_stub = std::exchange(other.m_stub, nullptr);
            m_proxy = std::exchange(other.m_proxy, nullptr);
        }
        return *this;
    }

    ~OHOSBinderHandle() { reset(); }

    explicit operator bool() const { return m_stub || m_proxy; }

    OHIPCRemoteStub* stub() const { return m_stub; }
    OHIPCRemoteProxy* proxy() const { return m_proxy; }

    // Transfers ownership of a decoded proxy to the caller.
    OHIPCRemoteProxy* releaseProxy() WARN_UNUSED_RETURN { return std::exchange(m_proxy, nullptr); }

private:
    // Destroys an owned proxy (if any). Out of line so this header stays free of the
    // IPCKit dependency; defined in ohos/ArgumentCodersOHOS.cpp.
    void reset();

    OHIPCRemoteStub* m_stub { nullptr };   // Borrowed (never destroyed here).
    OHIPCRemoteProxy* m_proxy { nullptr };  // Owned until releaseProxy().
};

} // namespace IPC

#endif // OS(OHOS)
