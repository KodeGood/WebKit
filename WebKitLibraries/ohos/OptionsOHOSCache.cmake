# Initial cache (cmake -C) for the WPE port on OpenHarmony.
# Use together with -DCMAKE_TOOLCHAIN_FILE=WebKitLibraries/ohos/WebKitOHOS.toolchain.cmake.

set(PORT WPE CACHE STRING "")
set(OHOS ON CACHE BOOL "")
set(WPE_PLATFORM_BUFFER_OHOS ON CACHE BOOL "")

set(ENABLE_WPE_PLATFORM ON CACHE BOOL "")
set(ENABLE_WPE_PLATFORM_DRM OFF CACHE BOOL "")
set(ENABLE_WPE_PLATFORM_WAYLAND OFF CACHE BOOL "")

set(ENABLE_BREAKPAD OFF CACHE BOOL "")
set(ENABLE_BUBBLEWRAP_SANDBOX OFF CACHE BOOL "")
set(ENABLE_DOCUMENTATION OFF CACHE BOOL "")
set(ENABLE_INTROSPECTION OFF CACHE BOOL "")
set(ENABLE_JOURNALD_LOG OFF CACHE BOOL "")
set(ENABLE_SPEECH_SYNTHESIS OFF CACHE BOOL "")
set(ENABLE_SPELLCHECK OFF CACHE BOOL "")
set(ENABLE_WEBDRIVER OFF CACHE BOOL "")
set(BREAKPAD_MINIDUMP_DIR "/data/storage/el2/base/files/" CACHE STRING "")

set(USE_ATK OFF CACHE BOOL "")
set(USE_AVIF OFF CACHE BOOL "")
set(USE_GBM OFF CACHE BOOL "")
set(USE_GSTREAMER_GL ON CACHE BOOL "")
set(USE_JPEGXL OFF CACHE BOOL "")
set(USE_LCMS OFF CACHE BOOL "")
set(USE_LIBBACKTRACE OFF CACHE BOOL "")
set(USE_LIBDRM OFF CACHE BOOL "")
set(USE_LIBHYPHEN OFF CACHE BOOL "")
set(USE_SKIA_OPENTYPE_SVG OFF CACHE BOOL "")
set(USE_SYSPROF_CAPTURE OFF CACHE BOOL "")
set(USE_WOFF2 OFF CACHE BOOL "")

set(CMAKE_INSTALL_LIBDIR lib CACHE STRING "")
set(CMAKE_INSTALL_LIBEXECDIR libexec CACHE PATH "")

# libc++ 15 in the OHOS SDK still marks parts of the C++20 library (<ranges>,
# <format>) experimental; they are only available with -fexperimental-library.
set(CMAKE_CXX_FLAGS "-fexperimental-library" CACHE STRING "")

# FIXME: -lintl and -lsharpyuv cover link dependencies of static libraries
# (gettext-libintl via glib, sharpyuv via libwebp) that the find modules don't
# propagate. Drop them once the find modules pick up the private link deps.
set(_webkit_ohos_linker_flags "-lintl -Wl,--undefined-version -Wl,-z,relro -Wl,-z,now -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${_webkit_ohos_linker_flags}" CACHE STRING "")
set(CMAKE_SHARED_LINKER_FLAGS "${_webkit_ohos_linker_flags}" CACHE STRING "")
set(CMAKE_MODULE_LINKER_FLAGS "${_webkit_ohos_linker_flags}" CACHE STRING "")
set(CMAKE_CXX_STANDARD_LIBRARIES "-lsharpyuv" CACHE STRING "")
