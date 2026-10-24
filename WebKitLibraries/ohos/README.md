# WPE WebKit for OpenHarmony: in-tree vcpkg build

Builds the WPE port for OpenHarmony (`arm64-ohos`) directly from this WebKit tree. vcpkg
provides the third-party dependencies (manifest mode, like the Windows port); WebKit itself is
a regular CMake/Ninja build tree.

Everything OHOS-specific for the build lives in new files only, so rebasing onto a new WebKit
revision never conflicts with upstream build files:

```
WebKitLibraries/ohos/
  vcpkg.json                   dependency manifest (+ pinned version overrides)
  vcpkg-configuration.json     microsoft/vcpkg baseline, overlay ports/triplets
  ports/                       overlay ports (see "Overlay ports")
  ohos-flags.cmake             OHOS compile flags shared by triplets and toolchain
  OptionsOHOSCache.cmake       WebKit feature set (cmake -C)
WebKitLibraries/triplets/
  arm64-ohos.cmake             shipping triplet
  arm64-ohos-dynamic.cmake     dev triplet (all dependencies shared)
WebKitLibraries/toolchains/
  ohos-cross.cmake             CMAKE_TOOLCHAIN_FILE for WebKit (vcpkg + OHOS SDK)
Tools/Scripts/build-webkit-ohos  configure + build + install + package
Tools/ohos/bootstrap-vcpkg.py    clone/bootstrap vcpkg at the pinned baseline
Tools/ohos/package.py            assemble dist/ for the embedder
```

The root `vcpkg.json` / `vcpkg-configuration.json` belong to the Windows port and are not used;
the toolchain file points `VCPKG_MANIFEST_DIR` here.

## Prerequisites

- OpenHarmony SDK, with `OHOS_SDK_ROOT` set to the SDK API dir containing
  `native/build/cmake/ohos.toolchain.cmake` (API 23 or newer; the IME code needs >= 22).
- Host tools on `PATH`: `python3`, `git`, `cmake`, `ninja`, `pkg-config`, `ruby`, `gperf`,
  `unifdef`, `bison`, `flex`, `perl` and a host C/C++ toolchain (host tools such as
  `glib-compile-resources` are built by vcpkg for `x64-linux`).
- Autotools for ports that `autoreconf`: `autoconf`, `autoconf-archive`, `automake`, `libtool`.
- `VCPKG_ROOT` (optional): a vcpkg checkout. When unset, `build-webkit-ohos` bootstraps one in
  `WebKitBuild/vcpkg` at the baseline from `vcpkg-configuration.json`. A recent vcpkg is
  required, as OHOS support (`scripts/toolchains/ohos.cmake`) is new.

## Build

```sh
export OHOS_SDK_ROOT=/path/to/ohos/sdk/23
Tools/Scripts/build-webkit-ohos --release
```

The first configure runs `vcpkg install` for the manifest (log in
`WebKitBuild/OHOS/Release/vcpkg-manifest-install.log`) into
`WebKitBuild/OHOS/Release/vcpkg_installed`; vcpkg's binary cache (`~/.cache/vcpkg`) makes later
configures fast. The script then builds, installs into `WebKitBuild/OHOS/Release/install` and
packages `WebKitBuild/OHOS/Release/dist` (plus `dist/wpewebkit-ohos.tar.xz`).

Incremental work afterwards:

```sh
ninja -C WebKitBuild/OHOS/Release
Tools/Scripts/build-webkit-ohos --release      # rebuild + repackage dist/
```

Useful options: `--debug`, `--reconfigure`, `--no-package`, `--no-strip`,
`--triplet arm64-ohos-dynamic`, `--cmake-arg=-DENABLE_...=ON`.

`OptionsOHOSCache.cmake` only seeds a new CMake cache: like any `cmake -C` file it does not
overwrite values an existing build directory already has, so `--reconfigure` does not pick up an
edit to it. After changing it, build into a fresh build directory, or override the value in
the existing one with `--cmake-arg=-D<OPTION>=<value>`.

Plain CMake works too:

```sh
cmake -G Ninja -S . -B WebKitBuild/OHOS/Release \
  -C WebKitLibraries/ohos/OptionsOHOSCache.cmake \
  -DCMAKE_TOOLCHAIN_FILE=WebKitLibraries/toolchains/ohos-cross.cmake \
  -DCMAKE_BUILD_TYPE=Release
```

## Consuming the build

`dist/` has the same layout the out-of-tree `webkit-openharmony-vcpkg` produced:

```sh
# in webkitview-openharmony:
python3 tools/bootstrap.py -a arm64 install /path/to/WebKit/WebKitBuild/OHOS/Release/dist
```

## Static / dynamic split

`arm64-ohos` builds dependencies static by default; they fold into the shared
`libWPEWebKit-2.0.so`. The GLib stack, gettext-libintl, all of GStreamer, libsoup and the
glib-networking GIO module stay dynamic so dlopen'd plugins, the injected bundle and the
embedding app share one copy. All shared libraries are emitted **unversioned** (`libfoo.so`,
SONAME `libfoo.so`) because the OHOS HAP packager only accepts plain `lib*.so`; this is done at
build time by the `vcpkg-tool-meson` and `vcpkg-make` overlays and
`CMAKE_PLATFORM_NO_VERSIONED_SONAME`.

## Overlay ports

An overlay replaces the upstream port of the same name entirely (version `overrides` don't
apply to it), so each one has to be refreshed by hand when the baseline is bumped. Upstreaming
the OHOS changes to microsoft/vcpkg removes the need for them.

| Port | Reason for overlay |
| --- | --- |
| glib | OHOS support; honor `GIO_EXTRA_MODULES` in "secure" appspawn child processes so the TLS module loads |
| glib-networking | Build the GIO TLS module shared so it can be dlopen'd although the triplet is static |
| gstreamer | GL on OHOS: GLES2 + EGL with the headless EGL winsys (no desktop GL) |
| libepoxy | OHOS support (EGL/GLES only, like Android) |
| libgpg-error | OHOS lock-object header; musl has no `pthread_cancel`, always use libc pthreads |
| libvpx | No OHOS target in configure: generic-gnu with the OHOS clang triple |
| libxkbcommon | Upstream port is empty on Linux-likes and needs X11/Wayland; build from source without them |
| openh264 | OHOS support |
| openssl | Map OHOS to the `linux-<arch>` Configure targets |
| vcpkg-make | `ohos`-aware `config.sub`/`config.guess`; libtool emits unversioned sonames |
| vcpkg-tool-meson | Teach meson the `ohos` host system (unversioned sonames, like Android) |

OHOS patches inside a port are named `0100-add-ohos-support.patch`, `0101-...` for follow-ups.

## Why no external registry (unlike Windows)

The Windows port resolves `cairo`, `curl`, `icu`, `openssl` and `zlib` from the
[WebKitRequirements](https://github.com/WebKitForWindows/WebKitRequirements) git registry.
That repository used to ship prebuilt binaries downloaded into `WebKitLibraries/win`; with the
vcpkg integration (295042@main) it became a registry of overlay ports for things upstream vcpkg
didn't provide the way WebKit wants them (`openssl` is an empty port mapping to LibreSSL, `zlib`
maps to zlib-ng, a newer CMake-built ICU, a customized curl with release candidates, a CMake
build of cairo for Windows). Keeping them in a separate registry lets a separate team maintain
them on its own cadence, versioned by baseline, without portfiles going through WebKit review;
the stated plan is to migrate them to microsoft/vcpkg.

None of that is required by vcpkg: `vcpkg-configuration.json` accepts relative
`overlay-ports`, so the OHOS ports live here, isolated in their own directory. A registry would
only pay off if several repositories needed to consume these ports.
