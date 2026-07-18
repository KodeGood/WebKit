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

#include <cstdint>

typedef struct OHIPCRemoteStub OHIPCRemoteStub;

// libWPEWebKit is built with hidden visibility; this is dlsym'd by the child entry libs, so
// it must be exported (and kept from gc-sections).
#define WEBKIT_OHOS_EXPORT __attribute__((visibility("default")))

// Helper bridging OHOS binder objects into WebKit's process model. On OHOS, WebKit owns the
// AbilityKit spawn itself (UIProcess/Launcher/ohos/ProcessLauncherOHOS.cpp) and the child
// process loads libWPEWebKit directly (Shared/ohos/OHOSChildProcessEntry.cpp), so this is an
// in-library helper — the child entry calls it directly after the env handshake. (It carries
// default visibility so it survives the webkitglib-symbols.map version script.)
#ifdef __cplusplus
extern "C" {
#endif

// Child (WebProcess/NetworkProcess/GPUProcess). The in-library child entry
// (OHOSChildProcessEntry.cpp) calls this after the env handshake: WebKit creates the receive
// stub (the child's Connection endpoint), registers it, and stashes it for the subsequent
// AuxiliaryProcessMain to adopt. Returns null on failure.
WEBKIT_OHOS_EXPORT OHIPCRemoteStub* webkit_ohos_create_connection_stub(void);

#ifdef __cplusplus
}

namespace WebKit {
// WebKit-internal: set by webkit_ohos_create_connection_stub(), consumed by
// AuxiliaryProcessMainCommon::parseCommandLine() to build the client Connection.
void setOHOSConnectionStub(OHIPCRemoteStub*);
OHIPCRemoteStub* takeOHOSConnectionStub();
}
#endif

#endif // OS(OHOS)
