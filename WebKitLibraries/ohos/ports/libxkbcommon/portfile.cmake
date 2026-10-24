# OHOS overlay of libxkbcommon. The upstream port produces an EMPTY package on non-Windows
# (it expects the system to provide libxkbcommon) and declares libx11/xcb/wayland as hard deps.
# Neither holds on OpenHarmony: there is no system libxkbcommon and no X11/Wayland. WPEPlatform only
# uses xkbcommon for keymap/keysym handling, so we always build it from source with the X11 and
# Wayland backends disabled (drops the entire X stack from the dependency graph).

set(PATCHES "")
if(VCPKG_TARGET_IS_WINDOWS)
    set(PATCHES build.patch)
endif()

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO xkbcommon/libxkbcommon
    REF "xkbcommon-${VERSION}"
    SHA512 450c33fe26af6b847cf7516685d0df63c2ba0126887e27595c40fe80e4d0487009aa6cdf38ff1270d595c7900cbf3c11e7aa4f9cff80fa361742b5ebb10d83bd
    HEAD_REF master
    PATCHES
        disable-test.patch
        ${PATCHES}
)

vcpkg_find_acquire_program(FLEX)
get_filename_component(FLEX_DIR "${FLEX}" DIRECTORY)
vcpkg_add_to_path(PREPEND "${FLEX_DIR}")

vcpkg_find_acquire_program(BISON)
get_filename_component(BISON_DIR "${BISON}" DIRECTORY)
vcpkg_add_to_path(PREPEND "${BISON_DIR}")

vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Denable-x11=false
        -Denable-wayland=false
        -Denable-xkbregistry=false
        -Denable-docs=false
        -Denable-tools=false
)
vcpkg_install_meson()
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
