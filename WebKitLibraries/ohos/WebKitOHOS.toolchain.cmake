# CMake toolchain file for building the WPE port for OpenHarmony from the
# WebKit tree. It drives vcpkg in manifest mode with the manifest in this
# directory (the vcpkg.json at the repository root belongs to the Windows
# port), and chainloads vcpkg's OHOS toolchain, which wraps the SDK's
# ohos.toolchain.cmake.
#
# Requires the VCPKG_ROOT and OHOS_SDK_ROOT environment variables.
# Tools/Scripts/build-webkit-ohos sets everything up; see README.md.

if (NOT DEFINED ENV{VCPKG_ROOT} OR "$ENV{VCPKG_ROOT}" STREQUAL "")
    message(FATAL_ERROR "VCPKG_ROOT is not set. Run Tools/ohos/bootstrap-vcpkg.py or point it at a vcpkg checkout.")
endif ()
if (NOT DEFINED ENV{OHOS_SDK_ROOT} OR NOT EXISTS "$ENV{OHOS_SDK_ROOT}/native/build/cmake/ohos.toolchain.cmake")
    message(FATAL_ERROR "OHOS_SDK_ROOT must point at the OpenHarmony SDK API directory (containing native/build/cmake/ohos.toolchain.cmake).")
endif ()

set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "")
if (NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET arm64-ohos CACHE STRING "")
endif ()
if (NOT DEFINED VCPKG_HOST_TRIPLET)
    set(VCPKG_HOST_TRIPLET x64-linux CACHE STRING "")
endif ()
set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "")

# Values the triplet would provide to a port build; vcpkg's ohos.cmake reads them.
include("${CMAKE_CURRENT_LIST_DIR}/ohos-flags.cmake")
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_C_FLAGS "${WEBKIT_OHOS_C_FLAGS}")
set(VCPKG_CXX_FLAGS "${WEBKIT_OHOS_C_FLAGS}")
set(OHOS_ARCH arm64-v8a CACHE STRING "")
set(CMAKE_PLATFORM_NO_VERSIONED_SONAME ON CACHE BOOL "")

if (DEFINED VCPKG_INSTALLED_DIR)
    set(_webkit_ohos_installed "${VCPKG_INSTALLED_DIR}")
else ()
    set(_webkit_ohos_installed "${CMAKE_BINARY_DIR}/vcpkg_installed")
endif ()
set(_webkit_ohos_prefix "${_webkit_ohos_installed}/${VCPKG_TARGET_TRIPLET}")

# Static dependencies don't carry their transitive link deps through every
# find module, so make the install tree's lib dir searchable for the extra
# libraries named in OptionsOHOSCache.cmake.
set(VCPKG_LINKER_FLAGS "-L${_webkit_ohos_prefix}/lib")

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/toolchains/ohos.cmake")
include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

set(ICU_ROOT "${_webkit_ohos_prefix}")
set(ZLIB_ROOT "${_webkit_ohos_prefix}")

# Only the cross install tree may answer pkg-config queries, never the host.
# pkgconfig/ here adds an egl.pc shim: EGL comes from the SDK sysroot, which
# ships no .pc, but wpe-1.0.pc requires egl.
set(ENV{PKG_CONFIG_LIBDIR} "${_webkit_ohos_prefix}/lib/pkgconfig:${_webkit_ohos_prefix}/share/pkgconfig:${CMAKE_CURRENT_LIST_DIR}/pkgconfig")
unset(ENV{PKG_CONFIG_PATH})

# gresources are compiled by the host glib that the manifest installs.
get_property(_webkit_ohos_in_try_compile GLOBAL PROPERTY IN_TRY_COMPILE)
if (NOT _webkit_ohos_in_try_compile)
    find_program(GLIB_COMPILE_RESOURCES_EXECUTABLE glib-compile-resources
        PATHS "${_webkit_ohos_installed}/${VCPKG_HOST_TRIPLET}/tools/glib"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH)
endif ()
