# CMake toolchain file for building the WPE port for OpenHarmony from the
# WebKit tree. It drives vcpkg in manifest mode with the manifest in
# WebKitLibraries/ohos (the vcpkg.json at the repository root belongs to the
# Windows port), and chainloads vcpkg's OHOS toolchain, which wraps the SDK's
# ohos.toolchain.cmake.
#
# Requires the VCPKG_ROOT and OHOS_SDK_ROOT environment variables.
# Tools/Scripts/build-webkit-ohos sets everything up; see
# WebKitLibraries/ohos/README.md.

if (NOT DEFINED ENV{VCPKG_ROOT} OR "$ENV{VCPKG_ROOT}" STREQUAL "")
    message(FATAL_ERROR "VCPKG_ROOT is not set. Run Tools/ohos/bootstrap-vcpkg.py or point it at a vcpkg checkout.")
endif ()
if (NOT DEFINED ENV{OHOS_SDK_ROOT} OR NOT EXISTS "$ENV{OHOS_SDK_ROOT}/native/build/cmake/ohos.toolchain.cmake")
    message(FATAL_ERROR "OHOS_SDK_ROOT must point at the OpenHarmony SDK API directory (containing native/build/cmake/ohos.toolchain.cmake).")
endif ()

set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../ohos" CACHE PATH "")
if (NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET arm64-ohos CACHE STRING "")
endif ()
if (NOT DEFINED VCPKG_HOST_TRIPLET)
    set(VCPKG_HOST_TRIPLET x64-linux CACHE STRING "")
endif ()
set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "")

# Values the triplet would provide to a port build; vcpkg's ohos.cmake reads them.
include("${CMAKE_CURRENT_LIST_DIR}/../ohos/ohos-flags.cmake")
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

# vcpkg's ohos.cmake appends VCPKG_LINKER_FLAGS to the exe, shared and module
# linker flags. FIXME: -lintl and -lsharpyuv cover link dependencies of static
# libraries (gettext-libintl via glib, sharpyuv via libwebp) that the find
# modules don't propagate, hence the -L for the install tree. Drop them once
# the find modules pick up the private link deps.
set(VCPKG_LINKER_FLAGS "-L${_webkit_ohos_prefix}/lib -lintl -lsharpyuv -Wl,--undefined-version -Wl,-z,relro -Wl,-z,now -Wl,--gc-sections")

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/toolchains/ohos.cmake")
include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

# Qt for OpenHarmony for the Qt/QML plugin (ENABLE_WPE_QT_API). The SDK toolchain only
# searches packages under the find roots, so the Qt build dir has to be one of them.
if (WEBKIT_OHOS_QT_DIR)
    list(APPEND CMAKE_FIND_ROOT_PATH "${WEBKIT_OHOS_QT_DIR}")
    list(APPEND CMAKE_PREFIX_PATH "${WEBKIT_OHOS_QT_DIR}")
endif ()

set(ICU_ROOT "${_webkit_ohos_prefix}")
set(ZLIB_ROOT "${_webkit_ohos_prefix}")

# Only the cross install tree may answer pkg-config queries, never the host.
set(ENV{PKG_CONFIG_LIBDIR} "${_webkit_ohos_prefix}/lib/pkgconfig:${_webkit_ohos_prefix}/share/pkgconfig")
unset(ENV{PKG_CONFIG_PATH})

# gresources are compiled by the host glib that the manifest installs.
get_property(_webkit_ohos_in_try_compile GLOBAL PROPERTY IN_TRY_COMPILE)
if (NOT _webkit_ohos_in_try_compile)
    find_program(GLIB_COMPILE_RESOURCES_EXECUTABLE glib-compile-resources
        PATHS "${_webkit_ohos_installed}/${VCPKG_HOST_TRIPLET}/tools/glib"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH)
endif ()
