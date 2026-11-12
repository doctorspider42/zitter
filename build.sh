#!/bin/zsh
# Build the latest Zitter (Rust UI) app bundle; with --run, also launch it.
#
#   ./build.sh                 stage, build and package build/ZitterRustMetal.app
#   ./build.sh --run           ...then launch it with an isolated profile
#   ./build.sh --content       build and package the standalone Content lab
#   ./build.sh --content --plan preview the local Content build without compiling
#   ./build.sh --content --build-only build the local Content target without packaging
#   ./build.sh --content --archive package a sealed Content app as a local ZIP
#   ./build.sh --content --run launch the Content lab with temporary profiles
#   ./build.sh --run --raster metal   extra arguments go to tools/run_metal_sidebar.py
set -euo pipefail

ROOT=${0:A:h}
SRC=$ROOT/.chromium/src
OUT=out/ZitterOptimized
APP=$ROOT/build/ZitterRustMetal.app

content=false
if [[ ${1:-} == --content ]]; then content=true; shift; fi
plan=false
if $content && [[ ${1:-} == --plan ]]; then plan=true; shift; fi
build_only=false
if $content && [[ ${1:-} == --build-only ]]; then build_only=true; shift; fi
archive=false
if $content && [[ ${1:-} == --archive ]]; then archive=true; shift; fi
run=false
if [[ ${1:-} == --run ]]; then run=true; shift; fi
if [[ ($# -gt 0 && $run == false) || (($plan == true || $build_only == true) && $archive == true) ||
      ($run == true && ($plan == true || $build_only == true || $archive == true)) ]]; then
  echo "usage: $0 [--content] [--run [launcher args...]] | $0 --content [--plan [--build-only] | --build-only | --archive]" >&2
  exit 2
fi

# Install the complete browser overlay, including bootstrap, renderer hooks and
# vendored ad-blocking crates. The installer refuses modified/unowned sources.
ZITTER_PYTHON_BIN=${ZITTER_PYTHON:-$ROOT/.chromium/depot_tools/python-bin/python3}
if [[ ! -x $ZITTER_PYTHON_BIN ]]; then ZITTER_PYTHON_BIN=$(command -v python3); fi

if $content; then
  default_output=out/ZitterContentBundle
  if $build_only; then default_output=out/ZitterContentDev; fi
  OUT=${ZITTER_CONTENT_OUT:-$default_output}
  package=true
  if $plan || $build_only; then package=false; fi
  package_dir=${ZITTER_CONTENT_PACKAGE_DIR:-$ROOT/build}
  APP="${package_dir:A}/ZitterContentLab.app"
  if $archive; then APP="${package_dir:A}/ZitterContentLab-local.zip"; fi
  "$ZITTER_PYTHON_BIN" - "$ROOT" "$SRC/$OUT" "$build_only" "$package" <<'PYTHON'
from pathlib import Path
import platform
import re
import sys
root, output = map(Path, sys.argv[1:3])
build_only, package = (arg == "true" for arg in sys.argv[3:])
output.mkdir(parents=True, exist_ok=True)
path = output / "args.gn"
if not path.exists():
    args = (root / "chromium/release.args.gn").read_text()
    if not build_only:
        args = args.replace("is_component_build = true", "is_component_build = false")
    cpu = {"arm64": "arm64", "x86_64": "x64"}[platform.machine()]
    args = re.sub(r'^target_cpu = .*$', f'target_cpu = "{cpu}"', args, flags=re.M)
    path.write_text(args + "\nuse_thin_lto = false\nchrome_pgo_phase = 0\n")
args = path.read_text()
required_false = ["use_remoteexec", "use_siso"]
if package:
    required_false.extend(("is_debug", "is_component_build"))
for key in required_false:
    if not re.search(rf"^{key}\s*=\s*false\s*$", args, re.M):
        raise SystemExit(f"The local Content build requires {key} = false")
if package and not all(re.search(pattern, args, re.M) for pattern in (
    r"^proprietary_codecs[ \t]*=[ \t]*true[ \t]*(?:#.*)?$",
    r'^ffmpeg_branding[ \t]*=[ \t]*"Chrome"[ \t]*(?:#.*)?$',
)):
    raise SystemExit('The Content bundle requires proprietary_codecs = true and ffmpeg_branding = "Chrome"')
PYTHON
  if $package; then
    content_source_commit=$(git -C "$ROOT" rev-parse HEAD)
    content_source_dirty=false
    if [[ -n $(git -C "$ROOT" status --porcelain --untracked-files=normal) ]]; then
      content_source_dirty=true
    fi
  fi
  "$ZITTER_PYTHON_BIN" "$ROOT/tools/stage_content_embedder.py" "$SRC"
  (cd "$SRC" && buildtools/mac/gn gen "$OUT" --root-target=//zitter_content:zitter_content --fail-on-unused-args)
  content_graph_summary=$(PYTHONPATH="$ROOT/tools" "$ZITTER_PYTHON_BIN" - "$SRC" "$OUT" <<'PYTHON'
import json
from pathlib import Path
import sys
from stage_content_embedder import verify_content_dependencies

checkout = Path(sys.argv[1])
try:
    summary = verify_content_dependencies(checkout, checkout / sys.argv[2])
except ValueError as error:
    raise SystemExit(str(error)) from error
print(json.dumps(summary))
print(f"Content GN graph: {summary['gn_dependency_count']:,} dependencies, zero under //chrome",
      file=sys.stderr)
print("Content //chrome metadata inputs: " + (", ".join(summary["chrome_metadata_inputs"]) or "none"),
      file=sys.stderr)
PYTHON
)
  if $plan; then
    PYTHONPATH="$ROOT/tools" "$ZITTER_PYTHON_BIN" - "$SRC" "$OUT" <<'PYTHON'
from pathlib import Path
import sys
from stage_content_embedder import preview_build

checkout = Path(sys.argv[1])
preview_build(checkout, checkout / sys.argv[2], checkout / "third_party/ninja/ninja")
PYTHON
    exit 0
  fi
  (cd "$SRC" && third_party/ninja/ninja -C "$OUT" -j "${ZITTER_BUILD_JOBS:-6}" zitter_content)
  if $build_only; then exit 0; fi
  "$ZITTER_PYTHON_BIN" - "$ROOT" "$SRC" "$SRC/$OUT" "$APP" \
    "$content_source_commit" "$content_source_dirty" "$content_graph_summary" "$archive" <<'PYTHON'
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
root, checkout, output, destination = map(Path, sys.argv[1:5])
commit, initially_dirty, graph_summary, archive = sys.argv[5:]
def git(directory, *args):
    return subprocess.check_output(["git", "-C", directory, *args], text=True).strip()

if git(root, "rev-parse", "HEAD") != commit:
    raise SystemExit("Zitter HEAD changed during the Content build")
metadata = {
    "backend": "content",
    "source_commit": commit,
    "source_dirty": initially_dirty == "true" or bool(
        git(root, "status", "--porcelain", "--untracked-files=normal")),
    "chromium_revision": git(checkout, "rev-parse", "HEAD"),
    "gn_args_sha256": hashlib.sha256((output / "args.gn").read_bytes()).hexdigest(),
    **json.loads(graph_summary),
}
sys.path.insert(0, str(root / "tools"))
from verify_content_bundle import (
    seal_local_copy, validate_payload, validate_runpaths, verify_signature,
)
destination.parent.mkdir(parents=True, exist_ok=True)
if destination.is_symlink():
    raise SystemExit("The Content lab bundle destination must not be a link")
with ExitStack() as stack:
    directory = Path(stack.enter_context(
        TemporaryDirectory(prefix=".content-package-", dir=destination.parent)))
    scratch = os.environ.get("ZITTER_CONTENT_PACKAGE_SCRATCH")
    signing_directory = directory
    if scratch:
        signing_directory = Path(stack.enter_context(
            TemporaryDirectory(prefix="zitter-content-signing-", dir=scratch)))
    staged = signing_directory / "Zitter Content Lab.app"
    copy = ["/usr/bin/ditto"] if scratch else ["/bin/cp", "-cR"]
    subprocess.run([*copy, output / staged.name, staged], check=True)
    audit = root / "build/license-audit/mac-content-local"
    subprocess.run([sys.executable, root / "tools/audit_licenses.py", "--app", staged,
                    "--out", output, "--target", "//zitter_content:zitter_content",
                    "--destination", audit], check=True)
    resources = staged / "Contents/Resources"
    resources.mkdir(exist_ok=True)
    for notice in ("LICENSE", "NOTICE"):
        shutil.copyfile(root / notice, resources / notice)
    shutil.copyfile(audit / "THIRD_PARTY_LICENSES.txt", resources / "THIRD_PARTY_LICENSES.txt")
    (resources / "zitter-build.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    validate_payload(staged)
    validate_runpaths(staged)
    try:
        seal_local_copy(staged)
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.output or str(error)) from error
    if archive == "true":
        candidate = directory / destination.name
        subprocess.run(["/usr/bin/ditto", "-c", "-k", "--sequesterRsrc",
                        "--keepParent", staged, candidate], check=True)
        unpacked = signing_directory / "unpacked"
        unpacked.mkdir()
        subprocess.run(["/usr/bin/ditto", "-x", "-k", candidate, unpacked], check=True)
        try:
            verify_signature(unpacked / staged.name)
        except subprocess.CalledProcessError as error:
            raise SystemExit(error.output or str(error)) from error
        staged = candidate
    elif scratch:
        candidate = directory / staged.name
        subprocess.run(["/usr/bin/ditto", staged, candidate], check=True)
        try:
            verify_signature(candidate)
        except subprocess.CalledProcessError as error:
            raise SystemExit(error.output or str(error)) from error
        staged = candidate
    backup = directory / "previous.app"
    if destination.exists():
        destination.rename(backup)
    try:
        staged.rename(destination)
    except BaseException:
        if backup.exists():
            backup.rename(destination)
        raise
print(f"Content lab package: {destination}")
PYTHON
  if $run; then
    exec "$ZITTER_PYTHON_BIN" - "$APP" "$@" <<'PYTHON'
from pathlib import Path
import secrets
import subprocess
import sys
from tempfile import TemporaryDirectory
app = Path(sys.argv[1])
with TemporaryDirectory(prefix="zitter-content-probe-") as directory:
    root = Path(directory).resolve()
    key = root / "probe-cookie-key"
    key.write_bytes(secrets.token_bytes(32))
    key.chmod(0o600)
    command = [app / "Contents/MacOS/Zitter Content Lab",
               "--zitter-content-workspace", f"--zitter-content-workspace-root={root}",
               *sys.argv[2:]]
    raise SystemExit(subprocess.run(command).returncode)
PYTHON
  fi
  exit 0
fi

"$ZITTER_PYTHON_BIN" - "$SRC/$OUT/args.gn" <<'EOF'
from pathlib import Path
import re, sys
args = Path(sys.argv[1]).read_text()
if not re.search(r"^is_debug\s*=\s*false\s*$", args, re.M):
    raise SystemExit("The local app requires an existing Release output (is_debug = false)")
if not all(re.search(pattern, args, re.M) for pattern in (
    r"^proprietary_codecs[ \t]*=[ \t]*true[ \t]*(?:#.*)?$",
    r'^ffmpeg_branding[ \t]*=[ \t]*"Chrome"[ \t]*(?:#.*)?$',
)):
    raise SystemExit(
        'The local app requires proprietary_codecs = true and ffmpeg_branding = "Chrome" in args.gn'
    )
EOF
"$ZITTER_PYTHON_BIN" "$ROOT/tools/install_chromium_overlay.py" "$SRC"
targets=(zitter)
if [[ ${ZITTER_BUILD_TESTS:-0} == 1 ]]; then targets+=(zitter_prototype); fi
# The standard target owns the adblock vendor visibility and full browser client.
(cd "$SRC" && buildtools/mac/gn gen "$OUT" --root-target=//zitter)
(cd "$SRC" && third_party/ninja/ninja -C "$OUT" -j "${ZITTER_BUILD_JOBS:-6}" $targets)

if $run; then
  # A second instance cannot share the isolated profile; quit the old one first.
  if pkill -TERM -f "$APP/Contents/MacOS/Chromium --user-data-dir"; then
    for _ in {1..20}; do pgrep -f "$APP/Contents/MacOS/" >/dev/null || break; sleep 0.5; done
  fi
fi

# Package once for the concrete binary audit, then include its fresh notices
# before the final signature. Keep the public release/notarization flow separate.
audit="$ROOT/build/license-audit/mac-local"
"$ZITTER_PYTHON_BIN" "$ROOT/tools/package_app.py" --out "$SRC/$OUT" --binary zitter --destination "$APP" --no-register
"$ZITTER_PYTHON_BIN" "$ROOT/tools/audit_licenses.py" --app "$APP" --out "$SRC/$OUT" \
  --target //zitter:zitter --destination "$audit"
"$ZITTER_PYTHON_BIN" "$ROOT/tools/package_app.py" --out "$SRC/$OUT" --binary zitter --destination "$APP" \
  --no-register --third-party-licenses "$audit/THIRD_PARTY_LICENSES.txt"

if $run; then
  exec "$ZITTER_PYTHON_BIN" "$ROOT/tools/run_metal_sidebar.py" --app "$APP" "$@"
fi
