#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""macOS code signing shared by the Content packaging, notarization and checks.

The bundle identifier, the pinned signing identity and the inside-out
signature of a Chromium-style app bundle with its helpers and frameworks.
"""
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import plistlib
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SIGNING_CONFIG = ROOT / ".zitter-signing.json"
BUNDLE_ID = "dev.zitter.browser"


def signing_identity(explicit=None):
    identity = explicit or os.environ.get("ZITTER_CODESIGN_IDENTITY")
    if not identity and SIGNING_CONFIG.exists():
        identity = json.loads(SIGNING_CONFIG.read_text()).get("identity")
    if not identity or identity == "-":
        raise ValueError("Configure a permanent certificate with --sign-identity IDENTITY; "
                         "use --unsigned only for disposable test bundles")
    return identity


def runtime_signing_options(path, app):
    """Use the pinned Chromium entitlement split, including Aperitif helpers."""
    entitlements = ROOT / "chromium/signing"
    if path == app:
        return ["--options", "runtime,restrict,kill,library", "--entitlements",
                str(entitlements / "browser.plist")]
    if path.suffix == ".app" and path.name.endswith(("(Renderer).app", "(GPU).app",
                                                     "(Aperitif Renderer).app", "(Aperitif GPU).app")):
        # Match Chromium: JIT processes omit the explicit library-validation flag.
        return ["--options", "runtime,restrict,kill", "--entitlements",
                str(entitlements / "jit-helper.plist")]
    return ["--options", "runtime,restrict,kill,library"]


def sign_bundle(app, identity, hardened_runtime=False, *, bundle_id=BUNDLE_ID):
    """Sign nested code from the inside out, then verify the complete seal."""
    magic = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
             b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
             b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
    files = []
    bundles = []
    for path in app.rglob("*"):
        if path.is_symlink():
            continue
        if path.is_dir() and path.suffix in (".app", ".framework", ".xpc"):
            bundles.append(path)
        elif path.is_file():
            with path.open("rb") as stream:
                if stream.read(4) in magic:
                    files.append(path)
    # codesign treats a bundle's executable as the whole bundle. Seal it only
    # after its children, through the enclosing bundle path below.
    executables = set()
    for bundle in bundles + [app]:
        plist = bundle / ("Resources/Info.plist" if bundle.suffix == ".framework" else "Contents/Info.plist")
        with plist.open("rb") as stream:
            name = plistlib.load(stream)["CFBundleExecutable"]
        executable = bundle / name if bundle.suffix == ".framework" else bundle / "Contents/MacOS" / name
        executables.add(executable.resolve())
    # codesign can replace a file atomically, breaking its hard links. Sign every
    # physical pathname: signing one inode alias does not sign the other copies.
    files = [path for path in files if path.resolve() not in executables]
    # Do not use --deep for signing: each nested code object needs its own seal.
    def sign(path):
        command = ["/usr/bin/codesign", "--force", "--sign", identity, "--timestamp"]
        if hardened_runtime:
            command += runtime_signing_options(path, app)
        if path == app:
            command += ["--identifier", bundle_id]
        subprocess.run(command + [str(path)], check=True)
    # Independent dylibs can be signed concurrently; parent seals wait for all.
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(sign, files))
    for path in sorted(bundles, key=lambda p: len(p.parts), reverse=True) + [app]:
        sign(path)
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)], check=True)
