#!/bin/zsh
# Stage this checkout into the shared zitter_ui_lab overlay, build the app and the
# test binaries, and package build/ZitterRustMetal.app. --run also launches it.
#
#   build_app.sh                 stage, build and package build/ZitterRustMetal.app
#   build_app.sh --run           ...then launch it with an isolated profile
#   build_app.sh --run --raster metal   extra arguments go to tools/run_metal_sidebar.py
set -euo pipefail

ROOT=$(git -C ${0:A:h} rev-parse --show-toplevel)
SRC=/Users/papaj/Projects/zitter/.chromium/src
OVERLAY=$SRC/zitter_ui_lab
OUT=out/ZitterOptimized
APP=$ROOT/build/ZitterRustMetal.app

run=false
if [[ ${1:-} == --run ]]; then run=true; shift; fi
if [[ $# -gt 0 && $run == false ]]; then
  echo "usage: build_app.sh [--run [run_metal_sidebar.py args...]]" >&2; exit 2
fi

# Stage sources into the hand-staged zitter_ui_lab overlay. Unchanged files keep
# their mtimes, so ninja rebuilds only what changed.
stage() {
  local dest=$1 tmp=$1.tmp
  if cmp -s $tmp $dest 2>/dev/null; then rm $tmp; else mv $tmp $dest; echo "staged ${dest#$OVERLAY/}"; fi
}
for f in $ROOT/chromium/prototype/*; do
  dest=$OVERLAY/chromium/prototype/${f:t}
  if [[ $f == *_browsertest.cc ]]; then sed 's#zitter/testdata#zitter_ui_lab/testdata#g' $f > $dest.tmp; else cp $f $dest.tmp; fi
  stage $dest
done
for f in $OVERLAY/chromium/prototype/*; do
  [[ -e $ROOT/chromium/prototype/${f:t} ]] || { rm $f; echo "removed ${f:t}"; }
done
# The overlay renames the targets and leaves out the network policy targets.
python3 - $ROOT/chromium/BUILD.gn $OVERLAY/BUILD.gn.tmp <<'EOF'
import re, sys
s = open(sys.argv[1]).read()
s = re.sub(r'source_set\("network_policy"\) \{.*?\n\}\n\ntest\("network_policy_tests"\) \{.*?\n\}\n\n', '', s, flags=re.S)
s = s.replace('      ":network_policy",\n', '')
s = s.replace('test("zitter_prototype")', 'test("zitter_prototype_ui_lab")').replace('executable("zitter")', 'executable("zitter_ui_lab")')
open(sys.argv[2], 'w').write(s)
EOF
stage $OVERLAY/BUILD.gn

# Chromium's own ninja; Homebrew ninja would wipe the build log.
(cd $SRC && third_party/ninja/ninja -C $OUT zitter_ui_lab sidebar_scene_unittests zitter_prototype_ui_lab)

if $run; then
  # A second instance cannot share the isolated profile; quit the old one first.
  if pkill -TERM -f "$APP/Contents/MacOS/Chromium --user-data-dir"; then
    for _ in {1..20}; do pgrep -f "$APP/Contents/MacOS/" >/dev/null || break; sleep 0.5; done
  fi
fi

# codesign is verbose; show its output only when packaging fails.
if ! log=$(python3 $ROOT/tools/package_app.py --out $SRC/$OUT --binary zitter_ui_lab --destination $APP \
    --no-register --third-party-licenses /Users/papaj/Projects/zitter/build/license-audit/open-source/THIRD_PARTY_LICENSES.txt 2>&1); then
  print -r -- $log >&2; exit 1
fi
print -r -- ${log##*$'\n'}

if $run; then
  exec python3 $ROOT/tools/run_metal_sidebar.py --app $APP "$@"
fi
