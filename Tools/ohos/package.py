#!/usr/bin/env python3
#
# Assemble the WebKit / OpenHarmony distribution from the WebKit install tree
# (cmake --install of WebKitBuild/OHOS/<config>) and the vcpkg dependency tree
# (<build>/vcpkg_installed/<triplet>). Invoked by Tools/Scripts/build-webkit-ohos.
#
# Layout produced (under --out):
#   lib/libWPEWebKit-2.0.so            fat shared object (static leaf deps folded in), stripped
#   lib/*.so                           kept-dynamic libs (glib stack, gstreamer core, soup, ...)
#   lib/gio/modules/*.so               dlopen'd GIO modules (sidecar)
#   lib/gstreamer-1.0/*.so             dlopen'd GStreamer plugins (sidecar)
#   lib/wpe-webkit-2.0/.../*.so        injected bundle + web-process extensions (sidecar)
#   lib/pkgconfig/*.pc                 pkg-config files
#   include/...                        public headers
#   share/wpe-webkit-2.0/*.gresource   inspector resources
#
# The static leaf deps (icu, cairo, freetype, ...) do NOT appear as .so here — they are folded
# into libWPEWebKit-2.0.so. Anything expected-but-missing is reported as a warning so the first
# real build surfaces layout surprises instead of silently dropping files.

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile

# Top-level shared libs of the deps tree that stay dynamic (see WebKitLibraries/triplets/arm64-ohos.cmake).
# Matched as prefixes against lib/*.so* basenames.
KEEP_DYNAMIC_PREFIXES = (
    "libglib-2.0", "libgobject-2.0", "libgio-2.0", "libgmodule-2.0", "libgthread-2.0",
    "libintl", "libgirepository",
    "libgstreamer-1.0", "libgstbase-1.0", "libgstapp-1.0", "libgstaudio-1.0",
    "libgstvideo-1.0", "libgstgl-1.0", "libgstpbutils-1.0", "libgsttag-1.0",
    "libgstfft-1.0", "libgstriff-1.0", "libgstcodecparsers-1.0", "libgstallocators-1.0",
    "liborc-0.4",
    "libsoup-3.0",
)

FAT_LIB = "libWPEWebKit-2.0.so"

# Sidecar dirs under the install lib dir copied wholesale to dist lib/.
SIDECAR_DIRS = (
    "wpe-webkit-2.0",          # injected-bundle + web-process-extensions
)

# dlopen'd plugin/module dirs vcpkg installs under <install>/plugins/<src>; staged to dist
# lib/<dst> so the runtime finds them via the usual GStreamer / GIO search paths.
PLUGIN_DIRS = {
    "gstreamer": "gstreamer-1.0",          # GStreamer element plugins
    "glib-networking": "gio/modules",      # GIO TLS backend module (libsoup HTTPS)
}

# Helper executables WebKit installs under <install>/libexec/<rel>; spawned at runtime.
LIBEXEC_DIRS = (
    "wpe-webkit-2.0",          # WPEWebProcess, WPENetworkProcess
)


def find_strip() -> str | None:
    sdk = os.environ.get("OHOS_SDK_ROOT")
    if sdk:
        cand = pathlib.Path(sdk) / "native" / "llvm" / "bin" / "llvm-strip"
        if cand.exists():
            return str(cand)
    return shutil.which("llvm-strip") or shutil.which("strip")


def copy(src: pathlib.Path, dst: pathlib.Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if src.is_dir():
        shutil.copytree(src, dst, dirs_exist_ok=True, symlinks=True)
    else:
        shutil.copy2(src, dst, follow_symlinks=False)


def relocate_pkgconfig(pc_dir: pathlib.Path, prefix: pathlib.Path) -> None:
    # Make WebKit's own .pc files relocatable, matching vcpkg_fixup_pkgconfig for the deps.
    for pc in pc_dir.glob("*.pc"):
        lines = []
        for line in pc.read_text().splitlines():
            if line.startswith("prefix="):
                line = "prefix=${pcfiledir}/../.."
            else:
                line = line.replace(str(prefix), "${prefix}")
            lines.append(line)
        pc.write_text("\n".join(lines) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--webkit-install", required=True, help="WebKit install prefix (cmake --install)")
    ap.add_argument("--deps", required=True, help="vcpkg install tree for the triplet")
    ap.add_argument("--out", required=True, help="output staging dir")
    ap.add_argument("--tarball", help="tarball path (default <out>/wpewebkit-ohos.tar.xz)")
    ap.add_argument("--no-strip", action="store_true")
    args = ap.parse_args()

    wk = pathlib.Path(args.webkit_install).resolve()
    deps = pathlib.Path(args.deps).resolve()
    out = pathlib.Path(args.out).resolve()
    for tree in (wk, deps):
        if not (tree / "lib").is_dir():
            print(f"ERROR: {tree / 'lib'} not found — did the build run?", file=sys.stderr)
            return 1

    if out.exists():
        shutil.rmtree(out)
    out_lib = out / "lib"
    out_lib.mkdir(parents=True)
    warnings: list[str] = []
    strip = None if args.no_strip else find_strip()

    # fat lib
    fat = wk / "lib" / FAT_LIB
    if fat.exists():
        copy(fat, out_lib / FAT_LIB)
    else:
        # may carry a versioned name; grab any libWPEWebKit*.so
        matches = sorted((wk / "lib").glob("libWPEWebKit*.so*"))
        if matches:
            for m in matches:
                copy(m, out_lib / m.name)
        else:
            warnings.append(f"missing fat lib {FAT_LIB} (no libWPEWebKit*.so* in {wk / 'lib'})")

    # kept-dynamic top-level libs
    kept = 0
    for so in sorted((deps / "lib").glob("*.so*")):
        if any(so.name.startswith(p) for p in KEEP_DYNAMIC_PREFIXES):
            copy(so, out_lib / so.name)
            kept += 1
    if kept == 0:
        warnings.append("no kept-dynamic libs matched — verify glib/gstreamer/soup built shared")

    # sidecar dirs under lib/ (injected bundle + web-process extensions)
    for rel in SIDECAR_DIRS:
        src = wk / "lib" / rel
        if src.is_dir():
            copy(src, out_lib / rel)
        else:
            warnings.append(f"sidecar dir missing: lib/{rel}")

    # dlopen'd plugin/module dirs from <deps>/plugins/<src> -> dist lib/<dst>
    for src_name, dst_rel in PLUGIN_DIRS.items():
        src = deps / "plugins" / src_name
        sos = sorted(src.glob("*.so")) if src.is_dir() else []
        if sos:
            for so in sos:
                copy(so, out_lib / dst_rel / so.name)
        else:
            warnings.append(f"plugin dir empty/missing: plugins/{src_name} (-> lib/{dst_rel})")

    # helper executables from <webkit>/libexec/<rel> -> dist libexec/<rel>
    for rel in LIBEXEC_DIRS:
        src = wk / "libexec" / rel
        if src.is_dir():
            copy(src, out / "libexec" / rel)
        else:
            warnings.append(f"libexec dir missing: libexec/{rel}")

    # pkgconfig: deps first, then WebKit's own (made relocatable)
    out_pc = out_lib / "pkgconfig"
    if (deps / "lib" / "pkgconfig").is_dir():
        copy(deps / "lib" / "pkgconfig", out_pc)
    else:
        warnings.append("deps lib/pkgconfig missing")
    if (wk / "lib" / "pkgconfig").is_dir():
        webkit_pc = out / ".webkit-pc"
        copy(wk / "lib" / "pkgconfig", webkit_pc)
        relocate_pkgconfig(webkit_pc, wk)
        copy(webkit_pc, out_pc)
        shutil.rmtree(webkit_pc)
    else:
        warnings.append("WebKit lib/pkgconfig missing")

    # arch-dependent dev headers that live under lib/ (referenced by .pc Cflags). glib ships
    # the generated lib/glib-2.0/include/glibconfig.h there; glib-2.0.pc adds it as an include
    # dir, so a consumer can't compile against glib without it.
    glib_inc = deps / "lib" / "glib-2.0" / "include"
    if glib_inc.is_dir():
        copy(glib_inc, out_lib / "glib-2.0" / "include")
    else:
        warnings.append("lib/glib-2.0/include missing (glibconfig.h)")

    # headers: deps, then WebKit
    for tree in (deps, wk):
        if (tree / "include").is_dir():
            copy(tree / "include", out / "include")
        else:
            warnings.append(f"{tree / 'include'} missing")

    # shared resources (inspector.gresource etc.)
    share_wpe = wk / "share" / "wpe-webkit-2.0"
    if share_wpe.is_dir():
        copy(share_wpe, out / "share" / "wpe-webkit-2.0")

    # strip every .so shipped
    if strip:
        for so in out.rglob("*.so*"):
            if so.is_file() and not so.is_symlink():
                subprocess.run([strip, "--strip-unneeded", str(so)], check=False)
    elif not args.no_strip:
        warnings.append("no strip tool found (set OHOS_SDK_ROOT or install llvm-strip)")

    # tarball
    tarball = pathlib.Path(args.tarball) if args.tarball else out / "wpewebkit-ohos.tar.xz"
    tarball.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tarball, "w:xz") as tf:
        for top in ("lib", "libexec", "include", "share"):
            p = out / top
            if p.exists():
                tf.add(p, arcname=top)
    print(f">> tarball: {tarball} ({tarball.stat().st_size // 1024} KiB)")

    so_count = len(list(out.rglob("*.so*")))
    print(f">> staged {so_count} shared objects under {out}")
    for w in warnings:
        print(f"WARNING: {w}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
