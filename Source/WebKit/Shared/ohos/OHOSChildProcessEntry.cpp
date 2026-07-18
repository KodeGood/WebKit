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

// OHOS child-process entry, compiled INTO libWPEWebKit-2.0.so (the single shipped WebKit
// library). AbilityKit spawns an auxiliary process with
// OH_Ability_CreateNativeChildProcess("libWPEWebKit-2.0.so", ...); it dlopens this same
// library in the child and calls the two exported entry points below. Because every role
// loads the same library, the Web/Network role is delivered in the env handshake
// (ProcessLauncherOHOS → OHOSChildBootstrapProtocol) rather than by which library was loaded.
//
// There is intentionally NO separate child .so and NO dlopen of WebKit here: WebKit is
// already loaded, so MainProc calls WebProcessMain()/NetworkProcessMain() directly. The
// env is applied before the process main runs (WebKit's fontconfig / GStreamer /
// injected-bundle / XDG init happens inside the process main, after the handshake).

#include "config.h"

#if OS(OHOS)

#include "ohos/OHOSChildBootstrapProtocol.h"
#include "ohos/WebKitOHOSProcessEntry.h" // webkit_ohos_create_connection_stub

#include <IPCKit/ipc_cparcel.h>
#include <IPCKit/ipc_cremote_object.h>
#include <IPCKit/ipc_error_code.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <hilog/log.h>
#include <mutex>
#include <string>
#include <unistd.h>

// WebKit process mains (WK_EXPORT in libWPEWebKit). Forward-declared to avoid pulling the
// WebProcess/NetworkProcess include trees into this early-startup TU.
namespace WebKit {
int WebProcessMain(int argc, char** argv);
int NetworkProcessMain(int argc, char** argv);
#if ENABLE(GPU_PROCESS)
int GPUProcessMain(int argc, char** argv);
#endif
}

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0xD001
#define LOG_TAG "WPEChildEntry"
#define ENTRY_LOGI(fmt, ...) OH_LOG_Print(LOG_APP, LOG_INFO, LOG_DOMAIN, LOG_TAG, fmt, ##__VA_ARGS__)
#define ENTRY_LOGE(fmt, ...) OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_DOMAIN, LOG_TAG, fmt, ##__VA_ARGS__)

namespace {

using OHOSChildBootstrap::Role;
using OHOSChildBootstrap::SandboxDirs;

std::mutex g_mutex;
std::condition_variable g_cv;
bool g_ready = false;
Role g_role = Role::Web;
uint64_t g_processIdentifier = 0;

const char* processNameForRole(Role role)
{
    switch (role) {
    case Role::Web:     return "WPEWebProcess";
    case Role::Network: return "WPENetworkProcess";
    case Role::GPU:     return "WPEGPUProcess";
    }
    return "WPEChildProcess";
}

// Apply the parent's sandbox dirs to the environment. Runs BEFORE the WebKit process main:
// fontconfig, GStreamer registry/plugins, the injected bundle and the XDG dirs are read
// during WebKit process init. Mirrors the values the app's UIProcess uses.
void applyEnvironment(const SandboxDirs& dirs)
{
    const std::string& cache = dirs.cacheDir;
    const std::string& files = dirs.filesDir;
    const std::string& bundleCode = dirs.bundleCodeDir;

    setenv("TMP", cache.c_str(), 1);
    setenv("TEMP", cache.c_str(), 1);
    setenv("TMPDIR", cache.c_str(), 1);
    setenv("XDG_CACHE_HOME", cache.c_str(), 1);
    setenv("XDG_RUNTIME_DIR", cache.c_str(), 1);

    setenv("FONTCONFIG_PATH", files.c_str(), 1);
    setenv("HOME", files.c_str(), 1);
    setenv("XDG_DATA_HOME", files.c_str(), 1);
    setenv("XDG_DATA_DIRS", files.c_str(), 1);
    setenv("XDG_CONFIG_HOME", files.c_str(), 1);
    setenv("XDG_CONFIG_DIRS", files.c_str(), 1);

    std::string gioModulesDir = bundleCode + "/libs/arm64/gio/modules/";
    setenv("GIO_EXTRA_MODULES", gioModulesDir.c_str(), 1);

    std::string injectedBundleDir = bundleCode + "/libs/arm64/wpe-webkit-2.0/injected-bundle/";
    setenv("WEBKIT_INJECTED_BUNDLE_PATH", injectedBundleDir.c_str(), 1);

    std::string gstPluginDir = bundleCode + "/libs/arm64/gstreamer-1.0/";
    setenv("GST_PLUGIN_SYSTEM_PATH", gstPluginDir.c_str(), 1);
    setenv("GST_PLUGIN_PATH", gstPluginDir.c_str(), 1);

    std::string gstRegistry = cache + "/gstreamer-1.0-registry.bin";
    setenv("GST_REGISTRY", gstRegistry.c_str(), 1);
    setenv("GST_REGISTRY_FORK", "no", 1);

    setenv("WEBKIT_SKIA_ENABLE_CPU_RENDERING", "1", 1);
    setenv("WEBKIT_SKIA_GPU_PAINTING_THREADS", "0", 1);

    // Uncomment to enable WebKit RELEASE_LOG channels (logLevelString() reads WEBKIT_DEBUG):
    // setenv("WEBKIT_DEBUG", "Process,IPC", 1);
}

// Runs on a binder thread. Applies the forwarded environment and hands back the WebKit
// Connection stub so the parent (ProcessLauncherOHOS) can build its side of the connection.
int onBootstrapRequest(uint32_t code, const OHIPCParcel* data, OHIPCParcel* reply, void* /*userData*/)
{
    if (code != OHOSChildBootstrap::kEnvBootstrapCode) {
        ENTRY_LOGE("unexpected code %{public}u", code);
        return OH_IPC_INNER_ERROR;
    }

    int32_t role = 0;
    int64_t processIdentifier = 0;
    OH_IPCParcel_ReadInt32(data, &role);
    OH_IPCParcel_ReadInt64(data, &processIdentifier);
    SandboxDirs dirs;
    const char* cacheDir = OH_IPCParcel_ReadString(data);
    const char* filesDir = OH_IPCParcel_ReadString(data);
    const char* tempDir = OH_IPCParcel_ReadString(data);
    const char* bundleCodeDir = OH_IPCParcel_ReadString(data);
    if (!cacheDir || !filesDir || !tempDir || !bundleCodeDir) {
        ENTRY_LOGE("missing env dirs");
        return OH_IPC_INNER_ERROR;
    }
    dirs.cacheDir = cacheDir;
    dirs.filesDir = filesDir;
    dirs.tempDir = tempDir;
    dirs.bundleCodeDir = bundleCodeDir;

    applyEnvironment(dirs);

    // WebKit is already loaded (this code lives in libWPEWebKit): create the Connection
    // receive stub directly; it is stashed for AuxiliaryProcessMain to adopt.
    OHIPCRemoteStub* webkitStub = webkit_ohos_create_connection_stub();
    if (!webkitStub) {
        ENTRY_LOGE("webkit_ohos_create_connection_stub returned null");
        return OH_IPC_INNER_ERROR;
    }

    OH_IPCParcel_WriteInt32(reply, static_cast<int32_t>(getpid()));
    if (OH_IPCParcel_WriteRemoteStub(reply, webkitStub) != OH_IPC_SUCCESS) {
        ENTRY_LOGE("failed to write webkit stub to reply");
        return OH_IPC_INNER_ERROR;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_role = static_cast<Role>(role);
        g_processIdentifier = static_cast<uint64_t>(processIdentifier);
        g_ready = true;
    }
    g_cv.notify_all();

    ENTRY_LOGI("env applied, stub returned (role=%{public}d id=%{public}lld)", role, static_cast<long long>(processIdentifier));
    return OH_IPC_SUCCESS;
}

} // namespace

extern "C" {

// Exported entry points dlsym'd by AbilityKit in the freshly spawned child.
WEBKIT_OHOS_EXPORT OHIPCRemoteStub* NativeChildProcess_OnConnect()
{
    ENTRY_LOGI("NativeChildProcess_OnConnect");
    return OH_IPCRemoteStub_Create(OHOSChildBootstrap::kBootstrapDescriptor, onBootstrapRequest, nullptr, nullptr);
}

WEBKIT_OHOS_EXPORT void NativeChildProcess_MainProc()
{
    ENTRY_LOGI("NativeChildProcess_MainProc - waiting for environment handshake");

    Role role = Role::Web;
    uint64_t processIdentifier = 0;
    {
        // Bounded wait: if the parent dies (or never sends the handshake) between spawn
        // and bootstrap, exit instead of lingering as a zombie child forever.
        constexpr auto handshakeTimeout = std::chrono::seconds(30);
        std::unique_lock<std::mutex> lock(g_mutex);
        if (!g_cv.wait_for(lock, handshakeTimeout, [] { return g_ready; })) {
            ENTRY_LOGE("environment handshake timed out; exiting");
            _exit(EXIT_FAILURE);
        }
        role = g_role;
        processIdentifier = g_processIdentifier;
    }

    // AuxiliaryProcessMain (OS(OHOS)) parses argv[1] as the process identifier; the binder
    // receive endpoint is picked up from the stub stashed by webkit_ohos_create_connection_stub().
    std::string processIdStr = std::to_string(processIdentifier);
    const char* processName = processNameForRole(role);
    char* argv[2];
    argv[0] = const_cast<char*>(processName);
    argv[1] = const_cast<char*>(processIdStr.c_str());

    ENTRY_LOGI("NativeChildProcess_MainProc - entering %{public}s (id=%{public}s)", processName, processIdStr.c_str());
    switch (role) {
    case Role::Web:
        WebKit::WebProcessMain(2, argv);
        break;
    case Role::Network:
        WebKit::NetworkProcessMain(2, argv);
        break;
    case Role::GPU:
#if ENABLE(GPU_PROCESS)
        WebKit::GPUProcessMain(2, argv);
#else
        ENTRY_LOGE("GPU process requested but GPU_PROCESS is disabled");
#endif
        break;
    }
    ENTRY_LOGI("NativeChildProcess_MainProc - process main returned");
}

} // extern "C"

#endif // OS(OHOS)
