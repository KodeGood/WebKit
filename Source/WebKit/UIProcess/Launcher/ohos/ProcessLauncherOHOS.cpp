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
#include "ProcessLauncher.h"

#if OS(OHOS)

#include "Connection.h"
#include "Logging.h"
#include "ohos/OHOSChildBootstrapProtocol.h"
#include <AbilityKit/ability_runtime/application_context.h>
#include <AbilityKit/native_child_process.h>
#include <IPCKit/ipc_cparcel.h>
#include <IPCKit/ipc_cremote_object.h>
#include <IPCKit/ipc_error_code.h>
#include <cinttypes>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <wtf/Lock.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RunLoop.h>

namespace WebKit {

// The child process loads the single shipped WebKit library and runs its exported
// NativeChildProcess_OnConnect()/NativeChildProcess_MainProc() (OHOSChildProcessEntry.cpp).
// Every role loads the same library; the role is carried in the env handshake below.
static constexpr const char* kChildLibName = "libWPEWebKit-2.0.so";

static bool roleForType(ProcessLauncher::ProcessType type, OHOSChildBootstrap::Role& role)
{
    switch (type) {
    case ProcessLauncher::ProcessType::Web:
        role = OHOSChildBootstrap::Role::Web;
        return true;
    case ProcessLauncher::ProcessType::Network:
        role = OHOSChildBootstrap::Role::Network;
        return true;
#if ENABLE(GPU_PROCESS)
    case ProcessLauncher::ProcessType::GPU:
        role = OHOSChildBootstrap::Role::GPU;
        return true;
#endif
    default:
        return false;
    }
}

// Reads a growable ApplicationContext dir (cache/files/temp/bundleCode). The C API reports
// the required size via `written` on PARAM_INVALID, so retry with a bigger buffer.
static bool queryApplicationContextDir(AbilityRuntime_ErrorCode (*fn)(char*, int32_t, int32_t*), std::string& out)
{
    constexpr int32_t maxSize = 64 * 1024;
    int32_t size = 512;
    std::vector<char> buffer;
    while (size <= maxSize) {
        buffer.assign(size, '\0');
        int32_t written = 0;
        AbilityRuntime_ErrorCode ret = fn(buffer.data(), size, &written);
        if (ret == ABILITY_RUNTIME_ERROR_CODE_NO_ERROR) {
            buffer[size - 1] = '\0';
            out.assign(buffer.data());
            return true;
        }
        if (ret == ABILITY_RUNTIME_ERROR_CODE_PARAM_INVALID) {
            size = (written > size && written < maxSize) ? (written + 1) : (size * 2);
            continue;
        }
        return false;
    }
    return false;
}

// The 4 sandbox dirs forwarded to the child so it can setenv() before dlopen(libWPEWebKit).
// Only queryable in the UIProcess (the app process holds a populated ApplicationContext
// singleton); the child appspawn fork has none, which is why we forward them.
static bool querySandboxDirs(OHOSChildBootstrap::SandboxDirs& dirs)
{
    return queryApplicationContextDir(&OH_AbilityRuntime_ApplicationContextGetCacheDir, dirs.cacheDir)
        && queryApplicationContextDir(&OH_AbilityRuntime_ApplicationContextGetFilesDir, dirs.filesDir)
        && queryApplicationContextDir(&OH_AbilityRuntime_ApplicationContextGetTempDir, dirs.tempDir)
        && queryApplicationContextDir(&OH_AbilityRuntime_ApplicationContextGetBundleCodeDir, dirs.bundleCodeDir);
}

// A launch awaiting its onProcessStarted callback. OH_Ability_OnNativeChildProcessStarted
// carries no user pointer, so we correlate the callback back to its ProcessLauncher via a
// per-type FIFO. Web and Network use distinct trampolines (so cross-type mismatch is
// impossible). Within a type, spawns are additionally SERIALIZED (spawnInFlight below):
// the started callback is an inbound binder call made by each child during its own
// startup, so two same-type spawns in flight could report in either order and mispair —
// at most one spawn per type may be awaiting its callback for FIFO correlation to hold.
struct PendingLaunch {
    Ref<ProcessLauncher> launcher;
    Ref<RunLoop> launchRunLoop;
    uint64_t processIdentifier { 0 };
    OHOSChildBootstrap::SandboxDirs dirs;
};

struct TypeLaunchState {
    std::deque<PendingLaunch> queue;    // Front entry is the one awaiting its callback (when spawnInFlight).
    bool spawnInFlight { false };
    // The per-type trampoline, stashed by launchProcess (the trampolines are private
    // ProcessLauncher members, so the free spawn helper reads it from here).
    OH_Ability_OnNativeChildProcessStarted onStarted { nullptr };
};

static Lock& pendingLock()
{
    static NeverDestroyed<Lock> lock;
    return lock.get();
}

static TypeLaunchState& launchState(ProcessLauncher::ProcessType type)
{
    static NeverDestroyed<TypeLaunchState> web;
    static NeverDestroyed<TypeLaunchState> network;
#if ENABLE(GPU_PROCESS)
    static NeverDestroyed<TypeLaunchState> gpu;
    if (type == ProcessLauncher::ProcessType::GPU)
        return gpu.get();
#endif
    return type == ProcessLauncher::ProcessType::Network ? network.get() : web.get();
}

static std::optional<PendingLaunch> takePendingLaunch(ProcessLauncher::ProcessType type)
{
    Locker locker { pendingLock() };
    auto& state = launchState(type);
    if (state.queue.empty())
        return std::nullopt;
    PendingLaunch pending = WTFMove(state.queue.front());
    state.queue.pop_front();
    return pending;
}

// Issues the next queued spawn for `type` if none is awaiting its callback. Safe to call
// from any thread (the launch thread and the binder callback thread both do). A static
// member so the failure path may call the private didFinishLaunchingProcess.
void ProcessLauncher::spawnNextLaunch(ProcessType type)
{
    for (;;) {
        OH_Ability_OnNativeChildProcessStarted onStarted = nullptr;
        {
            Locker locker { pendingLock() };
            auto& state = launchState(type);
            if (state.spawnInFlight || state.queue.empty())
                return;
            state.spawnInFlight = true;
            onStarted = state.onStarted;
        }

        int ret = OH_Ability_CreateNativeChildProcess(kChildLibName, onStarted);
        if (ret == NCP_NO_ERROR) {
            RELEASE_LOG(Process, "ProcessLauncherOHOS: spawn requested %s type=%d", kChildLibName, static_cast<int>(type));
            return;
        }

        RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: OH_Ability_CreateNativeChildProcess(%s) failed: %d", kChildLibName, ret);
        std::optional<PendingLaunch> failed;
        {
            Locker locker { pendingLock() };
            auto& state = launchState(type);
            state.spawnInFlight = false;
            if (!state.queue.empty()) {
                failed = WTFMove(state.queue.front());
                state.queue.pop_front();
            }
        }
        if (failed) {
            failed->launchRunLoop->dispatch([launcher = WTFMove(failed->launcher)]() mutable {
                launcher->didFinishLaunchingProcess(0, IPC::Connection::Identifier { });
            });
        }
        // Try the next queued launch, if any.
    }
}

// Process-global child-exit notifications, registered once at first launch: with the
// launcher living inside libWPEWebKit nobody else registers this, and without it a child
// dying outside the connection's death-recipient window (e.g. pre-handshake) is invisible
// in the logs. PID-based correlation to a PendingLaunch is not possible pre-handshake (the
// pid is only learned from the bootstrap reply), so this is observability, not recovery;
// post-handshake deaths are handled by the connection's binder death recipient.
static void onNativeChildProcessExit(int32_t pid, int32_t signal)
{
    RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: child process %d exited (signal %d)", pid, signal);
}

static void registerChildExitCallbackOnce()
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        int ret = OH_Ability_RegisterNativeChildProcessExitCallback(onNativeChildProcessExit);
        if (ret != NCP_NO_ERROR)
            RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: RegisterNativeChildProcessExitCallback failed: %d", ret);
    });
}

// Runs on a binder thread. Performs the synchronous environment handshake with the freshly
// spawned child (which applies the env, loads WebKit and returns its Connection stub), then
// hops back to the launch run loop to hand WebKit the connection proxy.
void ProcessLauncher::handleChildStarted(ProcessLauncher::ProcessType type, int errCode, OHIPCRemoteProxy* bootstrapProxy)
{
    auto pending = takePendingLaunch(type);

    // This callback resolves the in-flight spawn; the next same-type spawn may now be
    // issued (callbacks can no longer cross because only one is ever outstanding).
    {
        Locker locker { pendingLock() };
        launchState(type).spawnInFlight = false;
    }
    spawnNextLaunch(type);

    if (!pending) {
        RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: no pending launch for type %d", static_cast<int>(type));
        if (bootstrapProxy)
            OH_IPCRemoteProxy_Destroy(bootstrapProxy);
        return;
    }

    Ref<ProcessLauncher> launcher = WTFMove(pending->launcher);
    Ref<RunLoop> launchRunLoop = WTFMove(pending->launchRunLoop);
    const uint64_t processIdentifier = pending->processIdentifier;

    auto finish = [launcher = WTFMove(launcher), launchRunLoop](ProcessID pid, OHIPCRemoteProxy* webkitProxy) mutable {
        launchRunLoop->dispatch([launcher = WTFMove(launcher), pid, webkitProxy]() mutable {
            launcher->m_processID = pid;
            launcher->didFinishLaunchingProcess(pid, IPC::Connection::Identifier { webkitProxy });
        });
    };

    if (errCode || !bootstrapProxy) {
        RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: spawn failed type=%d errCode=%d", static_cast<int>(type), errCode);
        finish(0, nullptr);
        return;
    }

    // Bootstrap handshake: forward the role + sandbox dirs; the child (already running
    // libWPEWebKit) applies env, creates its Connection stub, and replies with its pid +
    // that stub (which we read back as a proxy).
    OHOSChildBootstrap::Role role = OHOSChildBootstrap::Role::Web;
    roleForType(type, role);

    OHIPCParcel* data = OH_IPCParcel_Create();
    OHIPCParcel* reply = OH_IPCParcel_Create();
    OH_IPCParcel_WriteInt32(data, static_cast<int32_t>(role));
    OH_IPCParcel_WriteInt64(data, static_cast<int64_t>(processIdentifier));
    OH_IPCParcel_WriteString(data, pending->dirs.cacheDir.c_str());
    OH_IPCParcel_WriteString(data, pending->dirs.filesDir.c_str());
    OH_IPCParcel_WriteString(data, pending->dirs.tempDir.c_str());
    OH_IPCParcel_WriteString(data, pending->dirs.bundleCodeDir.c_str());

    OH_IPC_MessageOption option { OH_IPC_REQUEST_MODE_SYNC, 0, nullptr };
    int ret = OH_IPCRemoteProxy_SendRequest(bootstrapProxy, OHOSChildBootstrap::kEnvBootstrapCode, data, reply, &option);

    int32_t childPid = 0;
    OHIPCRemoteProxy* webkitProxy = nullptr;
    if (ret == OH_IPC_SUCCESS) {
        OH_IPCParcel_ReadInt32(reply, &childPid);
        webkitProxy = OH_IPCParcel_ReadRemoteProxy(reply);
    } else
        RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: env SendRequest failed %d", ret);

    OH_IPCParcel_Destroy(data);
    OH_IPCParcel_Destroy(reply);
    OH_IPCRemoteProxy_Destroy(bootstrapProxy); // bootstrap channel no longer needed

    if (!webkitProxy) {
        finish(0, nullptr);
        return;
    }

    if (childPid <= 0) {
        // The pid comes from getpid() in the same reply as the proxy; a non-positive value
        // means a malformed reply, and without a real pid the process can never be
        // terminated. Treat the launch as failed rather than book-keeping a bogus pid.
        RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: child reported invalid pid %d; treating launch as failed", childPid);
        OH_IPCRemoteProxy_Destroy(webkitProxy);
        finish(0, nullptr);
        return;
    }

    RELEASE_LOG(Process, "ProcessLauncherOHOS: type=%d id=%" PRIu64 " pid=%d connected", static_cast<int>(type), processIdentifier, childPid);
    finish(childPid, webkitProxy);
}

void ProcessLauncher::onWebProcessStarted(int errCode, OHIPCRemoteProxy* proxy)
{
    handleChildStarted(ProcessLauncher::ProcessType::Web, errCode, proxy);
}

void ProcessLauncher::onNetworkProcessStarted(int errCode, OHIPCRemoteProxy* proxy)
{
    handleChildStarted(ProcessLauncher::ProcessType::Network, errCode, proxy);
}

#if ENABLE(GPU_PROCESS)
void ProcessLauncher::onGPUProcessStarted(int errCode, OHIPCRemoteProxy* proxy)
{
    handleChildStarted(ProcessLauncher::ProcessType::GPU, errCode, proxy);
}
#endif

void ProcessLauncher::launchProcess()
{
    OHOSChildBootstrap::Role role;
    OH_Ability_OnNativeChildProcessStarted onStarted = nullptr;
    switch (m_launchOptions.processType) {
    case ProcessLauncher::ProcessType::Web:
        onStarted = onWebProcessStarted;
        break;
    case ProcessLauncher::ProcessType::Network:
        onStarted = onNetworkProcessStarted;
        break;
#if ENABLE(GPU_PROCESS)
    case ProcessLauncher::ProcessType::GPU:
        onStarted = onGPUProcessStarted;
        break;
#endif
    default:
        break;
    }
    if (!onStarted || !roleForType(m_launchOptions.processType, role)) {
        RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: unsupported process type %d", static_cast<int>(m_launchOptions.processType));
        didFinishLaunchingProcess(0, IPC::Connection::Identifier { });
        return;
    }

    PendingLaunch pending { Ref { *this }, RunLoop::currentSingleton(), m_launchOptions.processIdentifier.toUInt64(), { } };
    if (!querySandboxDirs(pending.dirs)) {
        RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: ApplicationContext sandbox dirs unavailable");
        didFinishLaunchingProcess(0, IPC::Connection::Identifier { });
        return;
    }

    registerChildExitCallbackOnce();

    {
        Locker locker { pendingLock() };
        auto& state = launchState(m_launchOptions.processType);
        state.onStarted = onStarted;
        state.queue.push_back(WTFMove(pending));
    }

    // Spawn (or queue behind an in-flight same-type spawn). The child's bootstrap proxy
    // arrives asynchronously at `onStarted` (a binder thread), which runs the env
    // handshake, hands WebKit the connection proxy, and issues the next queued spawn.
    // The API has no thread affinity, so this is called inline from the UIProcess run
    // loop; a spawn failure is reported asynchronously via the launch run loop.
    RELEASE_LOG(Process, "ProcessLauncherOHOS: launch queued %s role=%d id=%" PRIu64, kChildLibName, static_cast<int>(role), m_launchOptions.processIdentifier.toUInt64());
    spawnNextLaunch(m_launchOptions.processType);
}

void ProcessLauncher::terminateProcess()
{
    if (m_isLaunching) {
        invalidate();
        return;
    }

    if (!m_processID)
        return;

    Ability_NativeChildProcess_ErrCode ret = OH_Ability_KillChildProcess(static_cast<int32_t>(m_processID));
    if (ret != NCP_NO_ERROR)
        RELEASE_LOG_ERROR(Process, "ProcessLauncherOHOS: OH_Ability_KillChildProcess(%d) failed: %d", static_cast<int>(m_processID), static_cast<int>(ret));

    m_processID = 0;
}

void ProcessLauncher::platformInvalidate()
{
}

} // namespace WebKit

#endif // OS(OHOS)
