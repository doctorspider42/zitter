#!/bin/zsh
# Build the app and the test binaries the standard way (./build.sh: the //zitter
# overlay, `zitter` and `zitter_prototype`, the license audit and the package
# build/ZitterRustMetal.app), plus the Rust scene tests. --run also launches it.
#
#   build_app.sh                 build, audit and package build/ZitterRustMetal.app
#   build_app.sh --run           ...then launch it with an isolated profile
#   build_app.sh --run --raster metal   extra arguments go to tools/run_metal_sidebar.py
#
# The former zitter_ui_lab overlay is a frozen benchmark reference: never stage
# into it. Its targets cannot see the //zitter-only vendored crates.
set -euo pipefail

ROOT=$(git -C ${0:A:h} rev-parse --show-toplevel)
SRC=$ROOT/.chromium/src
OUT=out/ZitterOptimized
APP=$ROOT/build/ZitterRustMetal.app

run=false
if [[ ${1:-} == --run ]]; then run=true; shift; fi
if [[ $# -gt 0 && $run == false ]]; then
  echo "usage: build_app.sh [--run [run_metal_sidebar.py args...]]" >&2; exit 2
fi

if pgrep -fl "third_party/ninja/ninja|build.sh" | grep -v $$ >/dev/null; then
  echo "Another build is running in the shared output directory; wait for it." >&2; exit 1
fi

if $run; then
  # A second instance cannot share the isolated profile; quit the old one first.
  if pkill -TERM -f "$APP/Contents/MacOS/Chromium --user-data-dir"; then
    for _ in {1..20}; do pgrep -f "$APP/Contents/MacOS/" >/dev/null || break; sleep 0.5; done
  fi
fi

(cd $ROOT && ZITTER_BUILD_TESTS=1 ./build.sh)
# Chromium's own ninja; Homebrew ninja would wipe the build log.
(cd $SRC && third_party/ninja/ninja -C $OUT sidebar_scene_unittests)

if $run; then
  exec python3 $ROOT/tools/run_metal_sidebar.py --app $APP "$@"
fi
