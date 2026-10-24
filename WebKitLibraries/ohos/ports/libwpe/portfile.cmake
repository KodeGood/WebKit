vcpkg_download_distfile(ARCHIVE
    URLS "https://wpewebkit.org/releases/libwpe-${VERSION}.tar.xz"
    FILENAME "libwpe-${VERSION}.tar.xz"
    SHA512 44519848f2a7e32d9e5031cd6fafc2a2f402eb5213c5745aa361808387dc6bdfe666ab7b0ebd719061c20dcacf95d51e18f6b58dbfec8c6fc5d96a638ce9b21b
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    PATCHES
        # Static libwpe otherwise compiles loader-static.c, which needs an external
        # _wpe_loader_interface from a statically-linked backend. OHOS uses the WPEPlatform
        # backend (no legacy wpe_loader_interface), so force the dlopen-based loader.
        0001-use-dynamic-loader-for-static-build.patch
)

vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Denable-xkb=true
        -Dbuild-docs=false
)

vcpkg_install_meson()

vcpkg_copy_pdbs()

vcpkg_fixup_pkgconfig()

file(INSTALL "${SOURCE_PATH}/COPYING" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
