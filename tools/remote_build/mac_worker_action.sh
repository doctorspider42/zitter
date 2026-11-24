#!/bin/bash
# NativeLink worker entrypoint on a Mac (installed as ~/nativelink/worker_action.sh
# by install_mac_worker.sh). Every remote action runs as
#   worker_action.sh <command> <args...>
#
# Remote compiles arrive as
#   worker_action.sh ../../buildtools/.../clang_remote_wrapper <clang-cl.exe> <args...>
# with the Linux clang as an input, which macOS cannot run. This script does what
# clang_remote_wrapper does, but with the Mac build of the same clang revision
# from ~/nativelink/clang/<package version>/. It picks that version by the
# SHA-256 of the Linux clang in the action (linux_clang.sha256 next to each Mac
# clang), so a clang roll on the build PC never runs a mismatched compiler.
#
# Handling this here, outside the action inputs, keeps clang_remote_wrapper (an
# input of every compile) and so the remote cache unchanged. Keep the clang
# command line in sync with clang_remote_wrapper.
set -euo pipefail

dir="$(cd "$(dirname "$0")" && pwd)"

if [[ "$(basename "${1:-}")" != "clang_remote_wrapper" ]]; then
  exec /bin/bash "$@"
fi
shift  # the wrapper; now $1 is the Windows clang-cl.exe path

clang_base_path="../../third_party/llvm-build/Release+Asserts"
linux_clang="../../third_party/llvm-build/Release+Asserts_linux/bin/clang"

# The CAS hardlinks inputs into the action directory, so device, inode and size
# name this Linux clang for as long as the worker keeps it cached: hash it once.
map="$dir/clang-map/$(stat -L -f '%d-%i-%z' "$linux_clang")"
if [[ ! -s "$map" ]]; then
  sum="$(shasum -a 256 "$linux_clang" | cut -d' ' -f1)"
  package=""
  for f in "$dir"/clang/*/linux_clang.sha256; do
    [[ -e "$f" ]] || continue
    if [[ "$(tr -d ' \r\n' < "$f")" == "$sum" ]]; then
      package="$(basename "$(dirname "$f")")"
    fi
  done
  if [[ -z "$package" ]]; then
    echo "mac_worker_action: no Mac clang in $dir/clang matches Linux clang $sum;" \
         "rerun install_mac_worker.sh for the new clang revision" >&2
    exit 1
  fi
  mkdir -p "$dir/clang-map"
  printf '%s\n' "$package" > "$map.$$"
  mv "$map.$$" "$map"
fi
clang="$dir/clang/$(cat "$map")/bin/clang"

# Use the headers sent with the action (the Windows clang's resource dir), not
# the ones next to the Mac binary. Keep the path relative: libc++ module files
# (.pcm) record header paths, and every action runs in its own directory.
resource_dir=""
for inc in "$clang_base_path"/lib/clang/*/include; do
  resource_dir="$(dirname "$inc")"
done
if [[ -z "$resource_dir" || ! -d "$resource_dir" ]]; then
  echo "mac_worker_action: no $clang_base_path/lib/clang/*/include in the action inputs" >&2
  exit 1
fi

# clang-cl takes its default target from the host: x86_64 on the Linux workers
# and the build PC, arm64 on a Mac. Name the x86_64 (or -m32: x86) target the
# others use, unless the command line names one.
target="--target=x86_64-pc-windows-msvc"
for arg in "${@:2}"; do
  case "$arg" in
    --target=*|-target|/clang:--target=*|-clang:--target=*) target=""; break ;;
    -m32) target="--target=i686-pc-windows-msvc" ;;
  esac
done

exec "$clang" --driver-mode=cl ${target:+"$target"} -resource-dir "$resource_dir" "${@:2}"
