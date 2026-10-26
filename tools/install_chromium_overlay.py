#!/usr/bin/env python3
"""Install the Zitter desktop and test targets into an existing, pinned Chromium checkout.

No downloads, git staging, or broad replacement of Chromium source files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

import editor_service_patch as editor_services

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCY = '      "//zitter:profile_workspace_browser_tests",\n'
MARKER = ".zitter-overlay.json"


def patch_build(text):
    target = '  test("browser_tests") {'
    if text.count(target) != 1:
        raise ValueError("Expected exactly one desktop browser_tests target")
    start = text.index(target)
    anchor = "    deps = [\n"
    position = text.index(anchor, start) + len(anchor)
    if text.count(DEPENDENCY) == 1 and text[position:].startswith(DEPENDENCY):
        return text
    if "//zitter:profile_workspace_browser_tests" in text:
        raise ValueError("Existing Zitter dependency is in an unexpected location")
    return text[:position] + DEPENDENCY + text[position:]


def overlay_files():
    files = {"BUILD.gn": ROOT / "chromium/BUILD.gn"}
    for directory in ("zitter", "chromium/prototype", "chromium/app", "testdata"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.is_file():
                files[str(path.relative_to(ROOT))] = path
    return files


def patch_keychain(text):
    # Keep OSCrypt's random key and native Keychain protection, with our own
    # identity. Reusing Chromium's item can block behind another app's ACL.
    for old, new in (("Chromium Safe Storage", "Zitter Safe Storage"),
                     ('"Chromium";', '"Zitter";')):
        if text.count(new) == 1 and old not in text:
            continue
        if text.count(old) != 1 or new in text:
            raise ValueError("Unexpected upstream Keychain identity; review the patch")
        text = text.replace(old, new)
    return text



def patch_embedded_autofill(text):
    # Standalone WebContents have no Chrome address-save bubble. Keep profile
    # preferences intact, but advertise only capabilities the host provides.
    anchor = "bool ChromeAutofillClient::IsAutofillProfileEnabled() const {\n"
    guard = ('#if !BUILDFLAG(IS_ANDROID)\n'
             '  // Zitter: standalone hosts do not provide Chrome address-save UI.\n'
             '  if (!tabs::TabInterface::MaybeGetFromContents(web_contents())) {\n'
             '    return false;\n'
             '  }\n'
             '#endif\n')
    if text.count(anchor + guard) == 1:
        return text
    if text.count(anchor) != 1 or 'Zitter: standalone hosts' in text:
        raise ValueError("Unexpected upstream Autofill capability layout; review the patch")
    return text.replace(anchor, anchor + guard, 1)

def patch_media_dispatcher(text, header=False):
    # Keep all capture policy, request-state and output-protection handling in
    # Chromium's dispatcher; expose only registration of our tab-source adapter.
    if header:
        anchor = "  void ProcessMediaAccessRequest(content::WebContents* web_contents,\n"
        addition = ("  // Zitter: register capture handlers through the policy dispatcher.\n"
                    "  void AddMediaAccessHandler(std::unique_ptr<MediaAccessHandler> handler);\n\n")
    else:
        anchor = "void MediaCaptureDevicesDispatcher::ProcessMediaAccessRequest(\n"
        addition = ("// Zitter: register capture handlers through the policy dispatcher.\n"
                    "void MediaCaptureDevicesDispatcher::AddMediaAccessHandler(\n"
                    "    std::unique_ptr<MediaAccessHandler> handler) {\n"
                    "  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);\n"
                    "  CHECK(handler);\n"
                    "  media_access_handlers_.insert(media_access_handlers_.begin(), std::move(handler));\n"
                    "}\n\n")
    if text.count(addition + anchor) == 1 and text.count(anchor) == 1:
        return text
    if text.count(anchor) != 1 or "Zitter: register capture handlers" in text:
        raise ValueError("Unexpected upstream media dispatcher layout; review the registration hook")
    return text.replace(anchor, addition + anchor, 1)


def patch_component_registration(text):
    """Optional embedder exclusions, leaving upstream verification untouched."""
    include_anchor = '#include "base/check.h"\n'
    includes = ('#include "base/command_line.h"\n'
                '#include "base/strings/string_split.h"\n')
    anchor = ('bool CrxUpdateService::RegisterComponent(\n'
              '    const ComponentRegistration& component) {\n'
              '  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);\n')
    guard = ('  // Zitter: exclude unused components before registration or network access.\n'
             '  const std::string disabled_value =\n'
             '      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(\n'
             '          "component-updater-disabled-ids");\n'
             '  const auto disabled = base::SplitStringPiece(\n'
             '      disabled_value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);\n'
             '  if (std::find(disabled.begin(), disabled.end(), component.app_id) != disabled.end())\n'
             '    return false;\n')
    if text.count(anchor + guard) == 1 and text.count(include_anchor + includes) == 1:
        return text
    if (text.count(anchor) != 1 or text.count(include_anchor) != 1 or
            'Zitter: exclude unused components' in text):
        raise ValueError("Unexpected upstream component registration layout; review policy hook")
    return text.replace(include_anchor, include_anchor + includes, 1).replace(anchor, anchor + guard, 1)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_existing(destination):
    if not destination.exists():
        return
    marker = destination / MARKER
    if not marker.is_file():
        raise ValueError(f"Refusing to replace an unowned directory: {destination}")
    manifest = json.loads(marker.read_text())
    if manifest.get("owner") != "zitter-prototype":
        raise ValueError("Invalid overlay ownership marker")
    hashes = manifest["files"]
    actual = {str(p.relative_to(destination)) for p in destination.rglob("*")
              if p.is_file() and p.name != MARKER}
    if actual != set(hashes):
        raise ValueError("Overlay has added or deleted files; preserve local work first")
    for name, expected in hashes.items():
        if digest(destination / name) != expected:
            raise ValueError(f"Overlay has local modifications: {name}")


def install(checkout, dry_run=False):
    checkout = checkout.resolve()
    lock = json.loads((ROOT / "chromium/revision.json").read_text())
    revision = subprocess.check_output(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    if revision != lock["revision"]:
        raise ValueError(f"Chromium revision mismatch: {revision}; expected {lock['revision']}")
    editor_patch = editor_services.prepare(checkout)
    build = checkout / "chrome/test/BUILD.gn"
    original = build.read_text()
    patched = patch_build(original)
    keychain = checkout / "components/os_crypt/common/keychain_password_mac.mm"
    keychain_original = keychain.read_text()
    keychain_patched = patch_keychain(keychain_original)
    autofill = checkout / "chrome/browser/ui/autofill/chrome_autofill_client.cc"
    autofill_original = autofill.read_text()
    autofill_patched = patch_embedded_autofill(autofill_original)
    media_header = checkout / "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
    media_source = media_header.with_suffix(".cc")
    media_header_original, media_source_original = media_header.read_text(), media_source.read_text()
    media_header_patched = patch_media_dispatcher(media_header_original, header=True)
    media_source_patched = patch_media_dispatcher(media_source_original)
    component_service = checkout / "components/component_updater/component_updater_service.cc"
    component_original = component_service.read_text()
    component_patched = patch_component_registration(component_original)
    destination = checkout / "zitter"
    check_existing(destination)
    files = overlay_files()
    print(f"Chromium revision: {revision}")
    print(f"Overlay: {destination} ({len(files)} files)")
    print(f"Build hook: {build}")
    if dry_run:
        print("Dry run: no files changed")
        return
    # Prepare all files before touching the checkout's existing overlay.
    with tempfile.TemporaryDirectory(prefix="zitter-overlay-", dir=checkout) as staging:
        staged = Path(staging) / "zitter"
        staged.mkdir()
        hashes = {}
        for name, source in files.items():
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            hashes[name] = digest(target)
        (staged / MARKER).write_text(json.dumps(
            {"owner": "zitter-prototype", "revision": revision, "files": hashes}, indent=2) + "\n")
        backup = Path(staging) / "previous"
        if destination.exists():
            destination.rename(backup)
        try:
            staged.rename(destination)
            # Preserve other edits in this file; add only our single dependency.
            if patched != original:
                build.write_text(patched)
            if keychain_patched != keychain_original:
                keychain.write_text(keychain_patched)
            if autofill_patched != autofill_original:
                autofill.write_text(autofill_patched)
            if media_header_patched != media_header_original:
                media_header.write_text(media_header_patched)
            if media_source_patched != media_source_original:
                media_source.write_text(media_source_patched)
            if component_patched != component_original:
                component_service.write_text(component_patched)
            editor_services.apply(editor_patch)
        except Exception:
            if destination.exists():
                shutil.rmtree(destination)
            if backup.exists():
                backup.rename(destination)
            build.write_text(original)
            keychain.write_text(keychain_original)
            autofill.write_text(autofill_original)
            media_header.write_text(media_header_original)
            media_source.write_text(media_source_original)
            component_service.write_text(component_original)
            raise
    print("Installed. Run gn gen/check and build zitter or zitter_prototype; see README.md.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path, help="Chromium src directory")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        install(args.checkout, args.dry_run)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Overlay installation failed: {error}\n")


if __name__ == "__main__":
    main()
