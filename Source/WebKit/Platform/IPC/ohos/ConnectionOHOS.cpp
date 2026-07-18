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

#include "config.h"
#include "Connection.h"

#if OS(OHOS)

#include "Attachment.h"
#include "Decoder.h"
#include "Encoder.h"
#include "Logging.h"
#include "OHOSBinder.h"
#include "OHOSReceiveContext.h"
#include "ohos/WebKitOHOSProcessEntry.h"
#include <WebCore/SharedMemory.h>
#include <IPCKit/ipc_cparcel.h>
#include <IPCKit/ipc_cremote_object.h>
#include <IPCKit/ipc_cskeleton.h>
#include <IPCKit/ipc_error_code.h>
#include <native_buffer/native_buffer.h>
#include <wtf/HashMap.h>
#include <wtf/Lock.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/StdLibExtras.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/UniqueRef.h>
#include <wtf/Variant.h>
#include <wtf/Vector.h>
#include <wtf/ohos/RefPtrOHOS.h>

namespace IPC {

// All application binder messages travel under a single transaction code; WebKit's own
// message name lives in the encoded body. Must be within [0x01, 0x00ffffff].
static constexpr uint32_t k_messageCode = 1;

// Shared interface descriptor for every WPEWebKit connection stub.
static constexpr const char* k_stubDescriptor = "com.kodegood.wpewebkit.Connection";

// IPCKit parcel body cap is ~200 KB; bodies at/above this go out-of-line (FrameFlagOutOfLineBody).
static constexpr int32_t k_maxInlineBodySize = 192 * 1024;

// Attachment type tags, matching the IPC::Attachment variant arms (OHOS).
enum class AttachmentTag : int32_t {
    FileDescriptor = 0,
    NativeBuffer = 1,
    Binder = 2,
    // Absent optional fd (UnixFileDescriptor == -1; WebKit's encoding for an empty handle).
    // Binder rejects WriteFileDescriptor(-1), so carry a marker instead (cf. ConnectionUnix isNull()).
    NullFileDescriptor = 3,
};

// Framing flags (first int32 after the sequence number).
enum FrameFlags : int32_t {
    FrameFlagHasReverseStub = 1 << 0,
    // Body over the inline cap: carried in a SharedMemory (memfd) region. The frame holds
    // {oolBodySize, fd} and no inline body; binder translates the fd to the receiver.
    FrameFlagOutOfLineBody = 1 << 1,
};

// Guards a corrupt frame from triggering a huge mmap (well under OHOS MAX_RAWDATA_SIZE, 128 MB).
static constexpr int32_t k_maxOutOfLineBodySize = 64 * 1024 * 1024;

// An incoming message extracted from a parcel on a binder thread, ready to be replayed
// onto the connection work queue in order. Forward-declared in Connection.h.
struct IncomingParcel {
    uint32_t sequence { 0 };
    std::unique_ptr<Decoder> decoder;           // Null for a handshake-only frame.
    OHIPCRemoteProxy* reverseProxy { nullptr };  // Non-null when the frame announces the peer's stub.

    IncomingParcel() = default;
    IncomingParcel(IncomingParcel&& other)
        : sequence(other.sequence)
        , decoder(WTFMove(other.decoder))
        , reverseProxy(std::exchange(other.reverseProxy, nullptr))
    {
    }
    IncomingParcel& operator=(IncomingParcel&& other)
    {
        if (this != &other) {
            sequence = other.sequence;
            decoder = WTFMove(other.decoder);
            if (reverseProxy)
                OH_IPCRemoteProxy_Destroy(reverseProxy);
            reverseProxy = std::exchange(other.reverseProxy, nullptr);
        }
        return *this;
    }
    // Owns an unadopted reverse proxy: if a buffered/queued frame is destroyed without
    // being processed (teardown race), the proxy must not leak.
    ~IncomingParcel()
    {
        if (reverseProxy)
            OH_IPCRemoteProxy_Destroy(reverseProxy);
    }
};

static IncomingParcel extractIncomingParcel(const OHIPCParcel*);

// -----------------------------------------------------------------------------------
// OHOSReceiveContext (declared in OHOSReceiveContext.h). Method bodies live here, out of
// line, so the header stays free of the Connection / IncomingParcel / IPCKit definitions.
// -----------------------------------------------------------------------------------

namespace {

// Registry of stubs created outside a Connection (child OnConnect / connection-pair),
// keyed so the adopting Connection can recover the matching context. File-static so the
// HashMap of Ref<OHOSReceiveContext> stays out of the header.
Lock& receiveContextRegistryLock()
{
    static NeverDestroyed<Lock> lock;
    return lock.get();
}

HashMap<OHIPCRemoteStub*, Ref<OHOSReceiveContext>>& receiveContextRegistry()
{
    static NeverDestroyed<HashMap<OHIPCRemoteStub*, Ref<OHOSReceiveContext>>> map;
    return map.get();
}

} // namespace

OHOSReceiveContext::OHOSReceiveContext() = default;
OHOSReceiveContext::~OHOSReceiveContext() = default;

Ref<OHOSReceiveContext> OHOSReceiveContext::create()
{
    return adoptRef(*new OHOSReceiveContext);
}

void OHOSReceiveContext::registerForStub(OHIPCRemoteStub* stub, Ref<OHOSReceiveContext>&& context, bool frameworkOwned)
{
    context->m_frameworkOwned = frameworkOwned;
    Locker locker { receiveContextRegistryLock() };
    receiveContextRegistry().set(stub, WTFMove(context));
}

OHOSReceiveContext::Adopted OHOSReceiveContext::adoptForStub(OHIPCRemoteStub* stub)
{
    Locker locker { receiveContextRegistryLock() };
    auto it = receiveContextRegistry().find(stub);
    if (it == receiveContextRegistry().end())
        return { };
    bool frameworkOwned = it->value->m_frameworkOwned;
    RefPtr context = it->value.ptr();
    // Deliberately NOT removed here: the registry keeps pinning the context until the stub
    // is destroyed (unregisterForStub), so a binder request callback racing Connection
    // teardown can never dereference a freed context via the stub's raw userData.
    return { WTFMove(context), frameworkOwned };
}

void OHOSReceiveContext::unregisterForStub(OHIPCRemoteStub* stub)
{
    Locker locker { receiveContextRegistryLock() };
    receiveContextRegistry().remove(stub);
}

void OHOSReceiveContext::bind(Connection& connection)
{
    Locker locker { m_lock };
    m_connection = &connection;
    // Replay anything that arrived before the Connection existed, in order.
    auto pending = std::exchange(m_pending, { });
    for (auto& incoming : pending)
        postLocked(WTFMove(incoming));
}

void OHOSReceiveContext::detach()
{
    Locker locker { m_lock };
    m_connection = nullptr;
}

// Extraction happens under the lock so that a concurrent detach()/bind() cannot interleave,
// and so ordering matches send order (binder delivers oneway transactions to a single stub
// serially; the lock makes extract+post atomic even if that assumption ever breaks).
int OHOSReceiveContext::handleRequest(const OHIPCParcel* data)
{
    Locker locker { m_lock };
    auto incoming = extractIncomingParcel(data);
    postLocked(WTFMove(incoming));
    return OH_IPC_SUCCESS;
}

void OHOSReceiveContext::handleDeath()
{
    Locker locker { m_lock };
    if (m_connection)
        m_connection->dispatchBinderDeathOnQueue();
}

void OHOSReceiveContext::postLocked(IncomingParcel&& incoming)
{
    if (!m_connection) {
        m_pending.append(WTFMove(incoming));
        return;
    }
    m_connection->dispatchIncomingParcelOnQueue(WTFMove(incoming));
}

// -----------------------------------------------------------------------------------
// Parcel <-> message framing
// -----------------------------------------------------------------------------------

// Runs on a binder thread. The parcel is valid only for the duration of the request
// callback, so every byte and handle we need is copied/adopted out here; Decoder::create
// copies the body and the attachment handles are owned.
IncomingParcel extractIncomingParcel(const OHIPCParcel* data)
{
    IncomingParcel incoming;

    int32_t sequence = 0;
    OH_IPCParcel_ReadInt32(data, &sequence);
    incoming.sequence = static_cast<uint32_t>(sequence);

    int32_t flags = 0;
    OH_IPCParcel_ReadInt32(data, &flags);

    if (flags & FrameFlagHasReverseStub)
        incoming.reverseProxy = OH_IPCParcel_ReadRemoteProxy(data);

    // Out-of-line body: {size, fd} here (send order); inline body is empty, bytes mapped below.
    const bool outOfLineBody = flags & FrameFlagOutOfLineBody;
    int32_t outOfLineBodySize = 0;
    UnixFileDescriptor outOfLineFd;
    if (outOfLineBody) {
        OH_IPCParcel_ReadInt32(data, &outOfLineBodySize);
        int32_t rawFd = -1;
        OH_IPCParcel_ReadFileDescriptor(data, &rawFd);
        outOfLineFd = UnixFileDescriptor { rawFd, UnixFileDescriptor::Adopt };
        if (outOfLineBodySize <= 0 || outOfLineBodySize > k_maxOutOfLineBodySize || rawFd < 0) {
            RELEASE_LOG_ERROR(IPC, "ConnectionOHOS: bogus OOL body size %d (fd %d); dropping frame", outOfLineBodySize, rawFd);
            return incoming;
        }
    }

    int32_t attachmentCount = 0;
    OH_IPCParcel_ReadInt32(data, &attachmentCount);

    // Sanity-cap wire-supplied sizes: the peer is trusted, but a corrupted frame should
    // surface as a dropped message (and the sequence watchdog firing), not an OOM.
    static constexpr int32_t k_maxAttachmentCount = 64;
    if (attachmentCount < 0 || attachmentCount > k_maxAttachmentCount) {
        RELEASE_LOG_ERROR(IPC, "ConnectionOHOS: bogus attachment count %d; dropping frame", attachmentCount);
        return incoming;
    }

    Vector<Attachment> attachments;
    if (attachmentCount > 0)
        attachments.grow(attachmentCount);

    for (int32_t i = 0; i < attachmentCount; ++i) {
        int32_t tag = 0;
        OH_IPCParcel_ReadInt32(data, &tag);

        Attachment attachment;
        switch (static_cast<AttachmentTag>(tag)) {
        case AttachmentTag::FileDescriptor: {
            int32_t fd = -1;
            OH_IPCParcel_ReadFileDescriptor(data, &fd);
            attachment = UnixFileDescriptor { fd, UnixFileDescriptor::Adopt };
            break;
        }
        case AttachmentTag::NullFileDescriptor: {
            // Absent/optional handle; reconstruct an empty descriptor (value() == -1).
            attachment = UnixFileDescriptor { };
            break;
        }
        case AttachmentTag::NativeBuffer: {
            OH_NativeBuffer* buffer = nullptr;
            // ReadFromParcel increments the refcount; adopt it rather than add another.
            OH_NativeBuffer_ReadFromParcel(const_cast<OHIPCParcel*>(data), &buffer);
            attachment = adoptRef(buffer);
            break;
        }
        case AttachmentTag::Binder: {
            OHIPCRemoteProxy* proxy = OH_IPCParcel_ReadRemoteProxy(data);
            attachment = OHOSBinderHandle { proxy };
            break;
        }
        }

        // Stored reversed: Decoder::takeLastAttachment() pops from the back, so this
        // yields attachments in the same order the sender's ArgumentCoders encoded them.
        attachments[attachmentCount - i - 1] = WTFMove(attachment);
    }

    int32_t bodySize = 0;
    OH_IPCParcel_ReadInt32(data, &bodySize);
    if (bodySize < 0 || bodySize > k_maxInlineBodySize) {
        RELEASE_LOG_ERROR(IPC, "ConnectionOHOS: bogus body size %d; dropping frame", bodySize);
        return incoming;
    }

    if (outOfLineBody) {
        // Decoder copies the body, so the mapping and fd can be released once this callback returns.
        WebCore::SharedMemory::Handle handle(WTFMove(outOfLineFd), static_cast<uint64_t>(outOfLineBodySize));
        RefPtr<WebCore::SharedMemory> memory = WebCore::SharedMemory::map(WTFMove(handle), WebCore::SharedMemory::Protection::ReadOnly);
        if (!memory) {
            RELEASE_LOG_ERROR(IPC, "ConnectionOHOS: failed to map OOL body (%d bytes); dropping frame", outOfLineBodySize);
            return incoming;
        }
        incoming.decoder = Decoder::create(memory->span().first(static_cast<size_t>(outOfLineBodySize)), WTFMove(attachments));
    } else if (bodySize > 0) {
        const uint8_t* body = OH_IPCParcel_ReadBuffer(data, bodySize);
        if (body)
            incoming.decoder = Decoder::create(unsafeMakeSpan(body, static_cast<size_t>(bodySize)), WTFMove(attachments));
    }

    return incoming;
}

bool Connection::sendOutgoingMessage(UniqueRef<Encoder>&& encoder)
{
    ASSERT(m_isConnected);
    if (!m_peerProxy) {
        RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::sendOutgoingMessage: no peer proxy yet");
        return false;
    }

    // A send failure of any kind is fatal to the connection: the generic layer has
    // already dequeued the message (Connection::sendOutgoingMessages takeFirst()), so a
    // false return silently loses it — a hang for sync messages, protocol desync for
    // state-bearing ones. Closing the connection turns that into a visible process
    // termination/relaunch instead.
    auto failFatally = [&]() -> bool {
        m_connectionQueue->dispatch([protectedThis = Ref { *this }] { protectedThis->connectionDidClose(); });
        return false;
    };

    auto body = encoder->span();

    // Over the inline cap: copy the body into a SharedMemory (memfd) and carry its fd, so only
    // the fd handle — not the bytes — rides in the binder transaction.
    const bool useOutOfLineBody = body.size() >= static_cast<size_t>(k_maxInlineBodySize);
    RefPtr<WebCore::SharedMemory> outOfLineMemory;
    UnixFileDescriptor outOfLineFd;
    if (useOutOfLineBody) {
        if (body.size() > static_cast<size_t>(k_maxOutOfLineBodySize)) {
            RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::sendOutgoingMessage: body %zu exceeds OOL cap msg=%s",
                body.size(), description(encoder->messageName()).characters());
            return failFatally();
        }
        outOfLineMemory = WebCore::SharedMemory::copySpan(body);
        if (!outOfLineMemory) {
            RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::sendOutgoingMessage: SharedMemory alloc failed for %zu bytes msg=%s",
                body.size(), description(encoder->messageName()).characters());
            return failFatally();
        }
        auto handle = outOfLineMemory->createHandle(WebCore::SharedMemory::Protection::ReadOnly);
        if (!handle) {
            RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::sendOutgoingMessage: SharedMemory handle failed msg=%s",
                description(encoder->messageName()).characters());
            return failFatally();
        }
        outOfLineFd = handle->releaseHandle();
    }

    OHIPCParcel* parcel = OH_IPCParcel_Create();
    if (!parcel) {
        RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::sendOutgoingMessage: parcel allocation failed");
        return failFatally();
    }

    bool writeFailed = false;
    auto checkWrite = [&](int result) {
        if (result != OH_IPC_SUCCESS)
            writeFailed = true;
    };

    checkWrite(OH_IPCParcel_WriteInt32(parcel, static_cast<int32_t>(m_outgoingSequence++)));
    // Reverse-stub is sent separately; this path only sets the OOL flag. Receive reads in this order.
    checkWrite(OH_IPCParcel_WriteInt32(parcel, useOutOfLineBody ? FrameFlagOutOfLineBody : 0));
    if (useOutOfLineBody) {
        checkWrite(OH_IPCParcel_WriteInt32(parcel, static_cast<int32_t>(body.size())));
        checkWrite(OH_IPCParcel_WriteFileDescriptor(parcel, outOfLineFd.value()));
    }

    auto attachments = encoder->releaseAttachments();
    checkWrite(OH_IPCParcel_WriteInt32(parcel, static_cast<int32_t>(attachments.size())));
    for (auto& attachment : attachments) {
        WTF::switchOn(attachment,
            [&](UnixFileDescriptor& fd) {
                if (fd.value() < 0) {
                    // Absent/optional handle: carry a null marker, no fd transferred.
                    checkWrite(OH_IPCParcel_WriteInt32(parcel, static_cast<int32_t>(AttachmentTag::NullFileDescriptor)));
                    return;
                }
                checkWrite(OH_IPCParcel_WriteInt32(parcel, static_cast<int32_t>(AttachmentTag::FileDescriptor)));
                checkWrite(OH_IPCParcel_WriteFileDescriptor(parcel, fd.value()));
            },
            [&](RefPtr<OH_NativeBuffer>& buffer) {
                checkWrite(OH_IPCParcel_WriteInt32(parcel, static_cast<int32_t>(AttachmentTag::NativeBuffer)));
                checkWrite(OH_NativeBuffer_WriteToParcel(buffer.get(), parcel));
            },
            [&](OHOSBinderHandle& binder) {
                checkWrite(OH_IPCParcel_WriteInt32(parcel, static_cast<int32_t>(AttachmentTag::Binder)));
                // A locally created handle wraps our stub; a handle decoded from another
                // process (connection brokering, e.g. UIProcess forwarding the
                // NetworkProcess's client handle to the WebProcess) holds a proxy. Either
                // endpoint serializes; the receiver reads a proxy to the same stub.
                if (OHIPCRemoteStub* stub = binder.stub())
                    checkWrite(OH_IPCParcel_WriteRemoteStub(parcel, stub));
                else if (OHIPCRemoteProxy* proxy = binder.proxy())
                    checkWrite(OH_IPCParcel_WriteRemoteProxy(parcel, proxy));
                else
                    writeFailed = true;
            });
    }

    // Inline body: 0 when carried out-of-line (the receiver rebuilds it from the mapped memory).
    const int32_t inlineBodySize = useOutOfLineBody ? 0 : static_cast<int32_t>(body.size());
    checkWrite(OH_IPCParcel_WriteInt32(parcel, inlineBodySize));
    if (!useOutOfLineBody && !body.empty())
        checkWrite(OH_IPCParcel_WriteBuffer(parcel, body.data(), static_cast<int32_t>(body.size())));

    if (writeFailed) {
        // A short/partial frame would desync the peer's decoder; never send it.
        RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::sendOutgoingMessage: parcel write failed (msg=%s, body=%zu)",
            description(encoder->messageName()).characters(), body.size());
        OH_IPCParcel_Destroy(parcel);
        return failFatally();
    }

    OH_IPC_MessageOption option { OH_IPC_REQUEST_MODE_ASYNC, 0, nullptr };
    int result = OH_IPCRemoteProxy_SendRequest(m_peerProxy, k_messageCode, parcel, nullptr, &option);
    OH_IPCParcel_Destroy(parcel);

    if (result != OH_IPC_SUCCESS) {
        RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::sendOutgoingMessage: SendRequest failed (%d)", result);
        return failFatally();
    }
    return true;
}

// Sends the reverse-channel handshake: an empty-body frame carrying our own stub so the
// peer can build a proxy back to us (OH_IPCParcel_ReadRemoteProxy). Only the initiator
// (the side that holds a peer proxy at construction) sends this, exactly once.
void Connection::sendReverseChannelHandshake()
{
    ASSERT(m_isInitiator);
    ASSERT(m_peerProxy && m_localStub);
    if (m_didAnnounceLocalStub)
        return;

    OHIPCParcel* parcel = OH_IPCParcel_Create();
    if (!parcel)
        return;

    OH_IPCParcel_WriteInt32(parcel, static_cast<int32_t>(m_outgoingSequence++));
    OH_IPCParcel_WriteInt32(parcel, FrameFlagHasReverseStub);
    OH_IPCParcel_WriteRemoteStub(parcel, m_localStub);
    OH_IPCParcel_WriteInt32(parcel, 0 /* attachmentCount */);
    OH_IPCParcel_WriteInt32(parcel, 0 /* bodySize */);

    OH_IPC_MessageOption option { OH_IPC_REQUEST_MODE_ASYNC, 0, nullptr };
    int result = OH_IPCRemoteProxy_SendRequest(m_peerProxy, k_messageCode, parcel, nullptr, &option);
    OH_IPCParcel_Destroy(parcel);

    RELEASE_LOG(IPC, "ConnectionOHOS::sendReverseChannelHandshake: announced local stub (%d)", result);
    m_didAnnounceLocalStub = true;
}

// -----------------------------------------------------------------------------------
// Receive path (connection work queue)
// -----------------------------------------------------------------------------------

void Connection::dispatchIncomingParcelOnQueue(IncomingParcel&& incoming)
{
    m_connectionQueue->dispatch([protectedThis = Ref { *this }, this, incoming = WTFMove(incoming)]() mutable {
        if (incoming.sequence != m_expectedIncomingSequence) {
            // Ordering is the transport's core assumption; a genuinely reordered frame
            // means message invariants are already broken, so fail loudly (process
            // relaunch) instead of continuing with silently corrupted state.
            RELEASE_LOG_ERROR(IPC, "ConnectionOHOS: out-of-order frame: got %u expected %u; closing connection", incoming.sequence, m_expectedIncomingSequence);
            connectionDidClose();
            return;
        }
        ++m_expectedIncomingSequence;

        if (incoming.reverseProxy) {
            // The peer announced its stub; adopt the proxy as our send endpoint and open
            // the connection for outgoing traffic. (A duplicate announcement's proxy is
            // released by ~IncomingParcel.)
            if (!m_peerProxy) {
                m_peerProxy = std::exchange(incoming.reverseProxy, nullptr);
                installDeathRecipient();
            }
            if (!m_isConnected) {
                m_isConnected = true;
                RELEASE_LOG(IPC, "ConnectionOHOS: reverse channel established, connection open");
                sendOutgoingMessages();
            }
        }

        if (incoming.decoder)
            processIncomingMessage(makeUniqueRefFromNonNullUniquePtr(WTFMove(incoming.decoder)));
    });
}

void Connection::dispatchBinderDeathOnQueue()
{
    m_connectionQueue->dispatch([protectedThis = Ref { *this }, this] {
        RELEASE_LOG(IPC, "ConnectionOHOS: peer died");
        connectionDidClose();
    });
}

int Connection::didReceiveBinderRequest(uint32_t code, const OHIPCParcel* data, OHIPCParcel*, void* userData)
{
    if (code != k_messageCode)
        return OH_IPC_SUCCESS;
    return static_cast<OHOSReceiveContext*>(userData)->handleRequest(data);
}

void Connection::didReceiveBinderDeath(void* userData)
{
    static_cast<OHOSReceiveContext*>(userData)->handleDeath();
}

// Balances the ref() taken when a stub / death recipient was created with a context as its
// userData. IPCKit runs the destroy callback from the underlying remote object's teardown,
// after any in-flight callback dispatch has completed (OH_IPCRemoteStub_Destroy itself only
// deletes the C wrapper and does NOT quiesce callbacks), so holding a ref until this fires
// is what actually guarantees the raw userData can never dangle.
static void didDestroyBinderUserData(void* userData)
{
    static_cast<OHOSReceiveContext*>(userData)->deref();
}

void Connection::installDeathRecipient()
{
    if (!m_peerProxy || m_deathRecipient)
        return;
    m_receiveContext->ref(); // Balanced by didDestroyBinderUserData.
    m_deathRecipient = OH_IPCDeathRecipient_Create(didReceiveBinderDeath, didDestroyBinderUserData, m_receiveContext.get());
    if (m_deathRecipient)
        OH_IPCRemoteProxy_AddDeathRecipient(m_peerProxy, m_deathRecipient);
    else
        m_receiveContext->deref();
}

// -----------------------------------------------------------------------------------
// Platform hooks
// -----------------------------------------------------------------------------------

void Connection::platformInitialize(Identifier&& identifier)
{
    m_peerProxy = identifier.peerProxy;
    m_localStub = identifier.localStub;
    // The initiator is whichever side holds a proxy to the peer at construction: it can
    // send immediately and must announce its own stub over the reverse channel.
    m_isInitiator = !!m_peerProxy;
}

void Connection::platformOpen()
{
    if (m_localStub) {
        // Acceptor path: the stub already exists (child OnConnect / createConnectionIdentifierPair).
        auto adopted = OHOSReceiveContext::adoptForStub(m_localStub);
        if (!adopted.context) {
            RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::platformOpen: no receive context registered for adopted stub");
            m_receiveContext = OHOSReceiveContext::create();
            m_ownsLocalStub = false;
        } else {
            m_receiveContext = WTFMove(adopted.context);
            // We destroy the stub on teardown unless the framework owns it (child OnConnect).
            m_ownsLocalStub = !adopted.frameworkOwned;
        }
    } else {
        // Initiator path: create our receive endpoint now.
        m_receiveContext = OHOSReceiveContext::create();
        m_receiveContext->ref(); // Balanced by didDestroyBinderUserData.
        m_localStub = OH_IPCRemoteStub_Create(k_stubDescriptor, didReceiveBinderRequest, didDestroyBinderUserData, m_receiveContext.get());
        RELEASE_ASSERT(m_localStub);
        m_ownsLocalStub = true;
        // Pin the context in the registry until the stub is destroyed, so a binder request
        // callback racing platformInvalidate can never use a freed context.
        OHOSReceiveContext::registerForStub(m_localStub, Ref { *m_receiveContext }, false /* frameworkOwned */);
    }

    // Routes (and replays any buffered) binder callbacks to this connection.
    m_receiveContext->bind(*this);

    if (m_isInitiator) {
        installDeathRecipient();
        // Serialize the connect + first sends onto the connection queue so the outgoing
        // sequence counter and every SendRequest run on a single thread, preserving order.
        m_connectionQueue->dispatch([protectedThis = Ref { *this }, this] {
            m_isConnected = true;
            RELEASE_LOG(IPC, "ConnectionOHOS: initiator open, announcing local stub");
            sendReverseChannelHandshake();
            sendOutgoingMessages();
        });
    }
    // Acceptor: stays un-connected until the initiator's handshake delivers m_peerProxy.
}

void Connection::platformInvalidate()
{
    m_isConnected = false;

    if (m_receiveContext)
        m_receiveContext->detach();

    if (m_deathRecipient) {
        if (m_peerProxy)
            OH_IPCRemoteProxy_RemoveDeathRecipient(m_peerProxy, m_deathRecipient);
        OH_IPCDeathRecipient_Destroy(m_deathRecipient);
        m_deathRecipient = nullptr;
    }

    // Destroying the stub quiesces the binder thread pool: in-flight request callbacks
    // for this stub complete before Destroy() returns, so no callback outlives detach().
    // Even if that IPCKit guarantee does not hold, the registry entry (removed only
    // below, after Destroy returns) keeps the context alive for any straggler callback.
    if (m_ownsLocalStub && m_localStub) {
        OH_IPCRemoteStub_Destroy(m_localStub);
        OHOSReceiveContext::unregisterForStub(m_localStub);
    }
    m_localStub = nullptr;

    if (m_peerProxy) {
        OH_IPCRemoteProxy_Destroy(m_peerProxy);
        m_peerProxy = nullptr;
    }

    m_receiveContext = nullptr;
}

bool Connection::platformPrepareForOpen()
{
    return true;
}

bool Connection::platformCanSendOutgoingMessages() const
{
    return m_isConnected && m_peerProxy;
}

// -----------------------------------------------------------------------------------
// Identifier / connection-pair plumbing
// -----------------------------------------------------------------------------------

Connection::Identifier::Identifier(Handle&& handle)
    : peerProxy(handle.releaseProxy())
{
}

OHIPCRemoteStub* Connection::createChildReceiveStub()
{
    // Framework-owned: the returned stub's lifetime belongs to AbilityKit, so the registry
    // pins its context for the process lifetime (see OHOSReceiveContext).
    auto context = OHOSReceiveContext::create();
    context->ref(); // Balanced by didDestroyBinderUserData.
    OHIPCRemoteStub* stub = OH_IPCRemoteStub_Create(k_stubDescriptor, didReceiveBinderRequest, didDestroyBinderUserData, context.ptr());
    if (!stub) {
        RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::createChildReceiveStub: stub creation failed");
        context->deref();
        return nullptr;
    }
    OHOSReceiveContext::registerForStub(stub, WTFMove(context), true /* frameworkOwned */);
    return stub;
}

std::optional<Connection::ConnectionIdentifierPair> Connection::createConnectionIdentifierPair()
{
    // Create a fresh receive stub. The local ("server") end adopts it as its receive
    // endpoint; the serialized ConnectionHandle carries it so the remote end can build a
    // proxy and drive the reverse-channel handshake.
    auto context = OHOSReceiveContext::create();
    context->ref(); // Balanced by didDestroyBinderUserData.
    OHIPCRemoteStub* stub = OH_IPCRemoteStub_Create(k_stubDescriptor, didReceiveBinderRequest, didDestroyBinderUserData, context.ptr());
    if (!stub) {
        RELEASE_LOG_ERROR(IPC, "ConnectionOHOS::createConnectionIdentifierPair: stub creation failed");
        context->deref();
        return std::nullopt;
    }
    OHOSReceiveContext::registerForStub(stub, WTFMove(context), false /* frameworkOwned */);
    return ConnectionIdentifierPair { Identifier { stub }, ConnectionHandle { stub } };
}

} // namespace IPC

// Exported C ABI (Shared/ohos/WebKitOHOSProcessEntry.h): the in-library child entry calls
// this during the bootstrap handshake to create and stash its Connection receive stub.
OHIPCRemoteStub* webkit_ohos_create_connection_stub(void)
{
    OHIPCRemoteStub* stub = IPC::Connection::createChildReceiveStub();
    if (stub)
        WebKit::setOHOSConnectionStub(stub);
    return stub;
}

#endif // OS(OHOS)
