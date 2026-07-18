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

// Shared env-bootstrap contract between the OHOS process launcher (ProcessLauncherOHOS) and
// the child-process entry (OHOSChildProcessEntry) — both compiled into the single
// libWPEWebKit-2.0.so. On OHOS the child process loads libWPEWebKit-2.0.so itself (via
// OH_Ability_CreateNativeChildProcess), so there are no separate child .so files; the Web /
// Network role is carried in this handshake rather than by which library was loaded.
//
// Deliberately dependency-free (no WTF, no OHOS headers) so both sides can include it early.

#include <cstdint>
#include <string>

namespace OHOSChildBootstrap {

// Binder request code for the one synchronous handshake the parent sends over the child's
// bootstrap stub. Must be in [0x01, 0x00ffffff] and distinct from the Connection code (1).
constexpr uint32_t kEnvBootstrapCode = 0x1000;

// Interface descriptor of the child's bootstrap stub (distinct from the Connection stub's
// "com.kodegood.wpewebkit.Connection").
constexpr const char* kBootstrapDescriptor = "com.kodegood.wpewebkit.Bootstrap";

// Which auxiliary process the (single) libWPEWebKit child should become. Carried in the
// handshake because every role loads the same library.
enum class Role : int32_t {
    Web = 0,
    Network = 1,
    GPU = 2,
};

// The four app-sandbox dirs the parent reads from ApplicationContext and forwards to the
// child, which setenv()s them before it runs the WebKit process main. Wire order of the
// request parcel:
//   int32 role, int64 processIdentifier,
//   string cacheDir, string filesDir, string tempDir, string bundleCodeDir
// Reply parcel:
//   int32 childPid, RemoteStub webkitConnectionStub
struct SandboxDirs {
    std::string cacheDir;
    std::string filesDir;
    std::string tempDir;
    std::string bundleCodeDir;
};

} // namespace OHOSChildBootstrap
