# Compiler flags shared by the OpenHarmony vcpkg triplets (dependencies) and by
# WebKitLibraries/toolchains/ohos-cross.cmake (WebKit itself), so both are
# built the same way. Kept outside triplets/ so vcpkg does not mistake it for a
# triplet.

set(WEBKIT_OHOS_C_FLAGS "-D__MUSL__ -DOHOS -D__OHOS__ -DPIC -fPIC")
if (DEFINED ENV{OHOS_SDK_ROOT})
    string(APPEND WEBKIT_OHOS_C_FLAGS " -isystem $ENV{OHOS_SDK_ROOT}/native/sysroot/usr/include/aarch64-linux-ohos")
endif ()
