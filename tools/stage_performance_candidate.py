#!/usr/bin/env python3
"""Stage an isolated candidate beside the frozen baseline Chromium overlay."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

import install_chromium_overlay as overlay


def instrument_app_probe(name, data):
    """Inject only opt-in measurement wiring into an otherwise frozen app."""
    text = data.decode()
    def replace(old, new):
        nonlocal text
        if new in text:
            return
        if text.count(old) != 1:
            raise ValueError(f"Unexpected application benchmark anchor in {name}")
        text = text.replace(old, new, 1)
    if name == "chromium/app/runtime.h":
        replace("namespace zitter {\n", "namespace zitter {\nclass AppPerformanceProbe;\n")
        replace("  std::unique_ptr<prototype::WorkspaceWindow> window_;",
                "  std::unique_ptr<prototype::WorkspaceWindow> window_;\n  std::unique_ptr<AppPerformanceProbe> performance_probe_;")
    elif name == "chromium/app/runtime.cc":
        replace('#include "chromium/app/runtime.h"',
                '#include "chromium/app/runtime.h"\n#include "chromium/app/performance_probe.h"')
        anchor = '  LOG(INFO) << "Zitter ready; persistent data: " << g_browser_process->profile_manager()->user_data_dir();'
        replace(anchor, anchor + '\n  if (base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-app-benchmark")) {\n'
                '    performance_probe_ = std::make_unique<AppPerformanceProbe>(window_.get(),\n'
                '        base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));\n'
                '    performance_probe_->Start();\n  }')
        replace("  window_.reset();", "  performance_probe_.reset();\n  window_.reset();")
    elif name == "BUILD.gn":
        replace('      "chromium/app/runtime.cc",',
                '      "chromium/app/runtime.cc",\n      "chromium/app/performance_probe.cc",\n      "chromium/app/performance_probe.h",')
    return text.encode()


def stage(checkout, variant="zitter_candidate", reference_overlay=None, current_tests=False, app_benchmark=False):
    if not re.fullmatch(r"zitter_[a-z][a-z0-9_]*", variant):
        raise ValueError("Variant must be a simple zitter_<name> directory/target name")
    lock = json.loads((overlay.ROOT / "chromium/revision.json").read_text())
    revision = subprocess.check_output(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    if revision != lock["revision"]:
        raise ValueError("Chromium revision does not match the pinned baseline")
    editor_patch = overlay.editor_services.prepare(checkout)
    destination = checkout / variant
    test_target = "zitter_prototype_" + variant[len("zitter_"):]
    if reference_overlay is None:
        files = overlay.overlay_files()
    else:
        reference_overlay = reference_overlay.resolve()
        if (reference_overlay / overlay.MARKER).is_file():
            overlay.check_existing(reference_overlay)
            manifest = json.loads((reference_overlay / overlay.MARKER).read_text())
        else:
            manifest = json.loads((reference_overlay / ".zitter-candidate.json").read_text())
            if manifest.get("owner") != "zitter-performance-candidate":
                raise ValueError("Unowned reference variant")
            actual = {str(path.relative_to(reference_overlay)) for path in reference_overlay.rglob("*")
                      if path.is_file() and path.name != ".zitter-candidate.json"}
            if actual != set(manifest["files"]):
                raise ValueError("Reference variant has added/deleted files")
            for name, digest in manifest["files"].items():
                if overlay.digest(reference_overlay / name) != digest:
                    raise ValueError(f"Reference variant has local edits: {name}")
        files = {name: reference_overlay / name for name in manifest["files"]}
        if current_tests:
            files.update({name: source for name, source in overlay.overlay_files().items()
                          if name.endswith("_browsertest.cc") or name.startswith("testdata/")})
    if app_benchmark:
        for name in ("chromium/app/performance_probe.cc", "chromium/app/performance_probe.h"):
            files[name] = overlay.ROOT / name
    marker = ".zitter-candidate.json"
    if destination.exists():
        recorded = json.loads((destination / marker).read_text())
        if recorded.get("owner") != "zitter-performance-candidate":
            raise ValueError(f"Unowned candidate directory: {destination}")
        existing = {str(path.relative_to(destination)) for path in destination.rglob("*") if path.is_file()}
        if existing != set(recorded["files"]) | {marker}:
            raise ValueError("Candidate contains untracked files; preserve/review them before staging")
        for name, digest in recorded["files"].items():
            if overlay.digest(destination / name) != digest:
                raise ValueError(f"Candidate has local edits: {name}")
    with tempfile.TemporaryDirectory(prefix="zitter-candidate-", dir=checkout) as directory:
        staged = Path(directory) / "staged"
        staged.mkdir()
        hashes = {}
        for name, source in files.items():
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            data = source.read_bytes()
            if app_benchmark and name in {"BUILD.gn", "chromium/app/runtime.cc", "chromium/app/runtime.h"}:
                data = instrument_app_probe(name, data)
            if name == "BUILD.gn":
                data = re.sub(rb'test\("zitter_prototype(?:_[a-z0-9_]+)?"\)',
                              f'test("{test_target}")'.encode(), data)
                data = re.sub(rb'executable\("zitter(?:_[a-z0-9_]+)?"\)',
                              f'executable("{variant}")'.encode(), data)
            elif name.endswith("_browsertest.cc"):
                data = re.sub(rb'zitter(?:_[a-z0-9_]+)?/testdata', f"{variant}/testdata".encode(), data)
            target.write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
            old = destination / name
            if old.is_file() and overlay.digest(old) == hashes[name]:
                shutil.copystat(old, target)
        (staged / marker).write_text(json.dumps({
            "owner": "zitter-performance-candidate", "revision": revision,
            "files": hashes,
        }, indent=2) + "\n")
        previous = Path(directory) / "previous"
        if destination.exists():
            destination.rename(previous)
        installed = False
        try:
            staged.rename(destination)
            installed = True
            overlay.editor_services.apply(editor_patch)
        except Exception:
            if installed:
                shutil.rmtree(destination)
            if previous.exists():
                previous.rename(destination)
            raise
    print(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", type=Path, default=overlay.ROOT / ".chromium/src")
    parser.add_argument("--variant", default="zitter_candidate")
    parser.add_argument("--reference-overlay", type=Path,
                        help="Copy product sources from a verified frozen overlay")
    parser.add_argument("--current-tests", action="store_true",
                        help="Use current tests/fixtures with frozen reference product sources")
    parser.add_argument("--app-benchmark", action="store_true",
                        help="Add opt-in regular-application measurement wiring to a frozen product")
    args = parser.parse_args()
    stage(args.checkout.resolve(), args.variant, args.reference_overlay, args.current_tests, args.app_benchmark)
