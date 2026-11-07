#!/bin/zsh
# Build the latest Zitter (Rust UI) app bundle; with --run, also launch it.
#
#   ./build.sh                 stage, build and package build/ZitterRustMetal.app
#   ./build.sh --run           ...then launch it with an isolated profile
#   ./build.sh --run --raster metal   extra arguments go to tools/run_metal_sidebar.py
set -euo pipefail

ROOT=${0:A:h}
SRC=$ROOT/.chromium/src
OUT=out/ZitterOptimized
APP=$ROOT/build/ZitterRustMetal.app

run=false
if [[ ${1:-} == --run ]]; then run=true; shift; fi
if [[ $# -gt 0 && $run == false ]]; then
  echo "usage: $0 [--run [run_metal_sidebar.py args...]]" >&2; exit 2
fi

# Install the complete browser overlay, including bootstrap, renderer hooks and
# vendored ad-blocking crates. The installer refuses modified/unowned sources.
ZITTER_PYTHON_BIN=${ZITTER_PYTHON:-$ROOT/.chromium/depot_tools/python-bin/python3}
if [[ ! -x $ZITTER_PYTHON_BIN ]]; then ZITTER_PYTHON_BIN=$(command -v python3); fi
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
