# OpenHarmony arm64 with every dependency built as a shared library
# (development/debugging; see arm64-ohos.cmake for the shipping configuration).

set(VCPKG_ENV_PASSTHROUGH_UNTRACKED OHOS_SDK_ROOT)
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME OHOS)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-unknown-linux-ohos")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DOHOS_ARCH=arm64-v8a -DCMAKE_PLATFORM_NO_VERSIONED_SONAME=ON)
set(VCPKG_BUILD_TYPE release)

include("${CMAKE_CURRENT_LIST_DIR}/../ohos-flags.cmake")
set(VCPKG_HASH_ADDITIONAL_FILES "${CMAKE_CURRENT_LIST_DIR}/../ohos-flags.cmake")
set(VCPKG_C_FLAGS "${WEBKIT_OHOS_C_FLAGS}")
set(VCPKG_CXX_FLAGS "${WEBKIT_OHOS_C_FLAGS}")
