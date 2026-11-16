#!/bin/zsh
# Build the app and the Rust scene tests the standard way: ./build.sh stages
# the Content embedder, builds //zitter_content:zitter_content, runs the
# license audit and packages build/ZitterContentLab.app. --run also launches it
# through ui.py (workspace root $TMPDIR/zitter-content-probe-ui).
#
#   build_app.sh                         build, audit and package build/ZitterContentLab.app
#   build_app.sh --run                   ...then launch it
#   build_app.sh --run --open-panel space   extra arguments go to `ui.py launch`
set -euo pipefail

ROOT=$(git -C ${0:A:h} rev-parse --show-toplevel)
SRC=$ROOT/.chromium/src
OUT=${ZITTER_CONTENT_OUT:-out/ZitterContentBundle}

run=false
if [[ ${1:-} == --run ]]; then run=true; shift; fi
if [[ $# -gt 0 && $run == false ]]; then
  echo "usage: build_app.sh [--run [ui.py launch args...]]" >&2; exit 2
fi

if pgrep -fl "third_party/ninja/ninja|build.sh" | grep -v $$ >/dev/null; then
  echo "Another build is running in the shared checkout; wait for it." >&2; exit 1
fi

(cd $ROOT && ./build.sh)
# Chromium's own ninja; Homebrew ninja would wipe the build log.
(cd $SRC && third_party/ninja/ninja -C $OUT sidebar_scene_unittests)

if $run; then
  exec python3 ${0:A:h}/ui.py launch "$@"
fi
