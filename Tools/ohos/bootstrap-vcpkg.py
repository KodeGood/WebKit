#!/usr/bin/env python3
#
# Clone microsoft/vcpkg at the baseline pinned in WebKitLibraries/ohos/vcpkg-configuration.json
# and bootstrap it. The vcpkg tool and its scripts (toolchains/ohos.cmake in particular) are
# pinned to the same commit the ports are resolved against.
#
# Default location is WebKitBuild/vcpkg. Re-running is cheap: if the checkout already sits on
# the baseline it returns early.

import argparse
import json
import pathlib
import platform
import subprocess
import sys

SOURCE_DIR = pathlib.Path(__file__).resolve().parents[2]
CONFIGURATION = SOURCE_DIR / "WebKitLibraries" / "ohos" / "vcpkg-configuration.json"


def baseline() -> tuple[str, str]:
    with open(CONFIGURATION) as f:
        registry = json.load(f)["default-registry"]
    return registry["repository"], registry["baseline"]


def bootstrap(checkout: pathlib.Path) -> None:
    repository, rev = baseline()

    if not (checkout / ".git").is_dir():
        checkout.parent.mkdir(parents=True, exist_ok=True)
        subprocess.check_call(["git", "clone", repository, str(checkout)])
    else:
        head = subprocess.check_output(["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
        if head == rev and (checkout / "vcpkg").exists():
            print(f"vcpkg already at {rev}")
            return
        subprocess.check_call(["git", "-C", str(checkout), "fetch", "origin"])

    print(f"Bootstrapping vcpkg@{rev}")
    subprocess.check_call(["git", "-C", str(checkout), "checkout", "--detach", rev])

    args = [str(checkout / "bootstrap-vcpkg.sh"), "-disableMetrics"]
    if "musl" in (platform.libc_ver()[0] or ""):
        args.append("-musl")
    subprocess.check_call(args, cwd=checkout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vcpkg-root", type=pathlib.Path, default=SOURCE_DIR / "WebKitBuild" / "vcpkg")
    args = parser.parse_args()

    checkout = args.vcpkg_root.resolve()
    bootstrap(checkout)
    print(f"vcpkg ready at {checkout}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
