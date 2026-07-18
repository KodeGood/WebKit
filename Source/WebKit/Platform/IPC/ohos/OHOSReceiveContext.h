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

#include <wtf/Lock.h>
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/Vector.h>

typedef struct OHIPCParcel OHIPCParcel;
typedef struct OHIPCRemoteStub OHIPCRemoteStub;

namespace IPC {

class Connection;
struct IncomingParcel;

// Thread-safe bridge between binder thread-pool callbacks and a Connection. It also
// buffers frames that arrive before the Connection binds (the child process's stub is
// live from NativeChildProcess_OnConnect(), possibly before its Connection is built),
// replaying them in order once bound. Method bodies are out of line in ConnectionOHOS.cpp
// so this header stays free of the Connection / IncomingParcel / IPCKit definitions; the
// class is complete here only so Connection's RefPtr member can be destroyed anywhere.
class OHOSReceiveContext : public ThreadSafeRefCounted<OHOSReceiveContext> {
public:
    static Ref<OHOSReceiveContext> create();
    ~OHOSReceiveContext();

    // Registry so a Connection built from an already-created stub (child OnConnect path,
    // or createConnectionIdentifierPair) can find the context the stub was created with.
    // Every stub's context is pinned here from creation until the stub is destroyed
    // (unregisterForStub), so the stub's raw userData never dangles even if a binder
    // request callback races Connection teardown. frameworkOwned marks a stub whose
    // lifetime is not ours (the child's OnConnect stub, owned by the AbilityKit
    // framework): its context stays registered for the process lifetime.
    static void registerForStub(OHIPCRemoteStub*, Ref<OHOSReceiveContext>&&, bool frameworkOwned);
    static void unregisterForStub(OHIPCRemoteStub*);

    struct Adopted {
        RefPtr<OHOSReceiveContext> context;
        bool frameworkOwned { false };
    };
    static Adopted adoptForStub(OHIPCRemoteStub*);

    void bind(Connection&);
    void detach();

    // Called on a binder thread-pool thread.
    int handleRequest(const OHIPCParcel*);
    void handleDeath();

private:
    OHOSReceiveContext();
    void postLocked(IncomingParcel&&) WTF_REQUIRES_LOCK(m_lock);

    Lock m_lock;
    Connection* m_connection WTF_GUARDED_BY_LOCK(m_lock) { nullptr };
    Vector<IncomingParcel> m_pending WTF_GUARDED_BY_LOCK(m_lock);
    bool m_frameworkOwned { false };   // Set once at registration; read on adoption.
};

} // namespace IPC

#endif // OS(OHOS)
