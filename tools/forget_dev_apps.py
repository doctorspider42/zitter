#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Remove development copies of Zitter from the Launch Services database.

Builds, verifiers and DMG packaging create Zitter bundles in temporary and
build directories. Launch Services registers them when they are checked or
launched. Every registered copy then appears in the macOS default browser list
as "Zitter.app (version)". This keeps only the installed applications.
"""

import argparse
import re
import subprocess
import sys

LSREGISTER = ("/System/Library/Frameworks/CoreServices.framework/Frameworks/"
              "LaunchServices.framework/Support/lsregister")
KEEP = {"/Applications/Zitter.app"}
NAME = re.compile(r"/Zitter[^/]*\.app$")


def registered_copies(dump):
    copies = set()
    for line in dump.splitlines():
        if not line.startswith("path:"):
            continue
        path = re.sub(r"\s+\(0x[0-9a-f]+\)$", "", line.removeprefix("path:").strip())
        if NAME.search(path) and "/Contents/" not in path and path not in KEEP:
            copies.add(path)
    return sorted(copies)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="List the copies without removing them")
    options = parser.parse_args()
    if sys.platform != "darwin":
        return 0
    dump = subprocess.run([LSREGISTER, "-dump"], capture_output=True, text=True,
                          errors="replace").stdout
    copies = registered_copies(dump)
    for path in copies:
        if options.dry_run:
            print(path)
        else:
            subprocess.run([LSREGISTER, "-u", path], capture_output=True)
    if not options.dry_run and copies:
        print(f"Removed {len(copies)} development copies of Zitter from Launch Services")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
