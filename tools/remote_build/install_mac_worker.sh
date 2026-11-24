#!/bin/bash
# Installs and starts a NativeLink worker on a Mac (Apple Silicon), natively,
# without Docker. Run it on the Mac from the folder it was copied to, next to
# nativelink_worker.json5 and mac_worker_action.sh:
#
#   bash install_mac_worker.sh SCHEDULER_HOST CLANG_PACKAGE LINUX_CLANG_SHA256 [SLOTS]
#
# SCHEDULER_HOST  the build PC's LAN address (NativeLink server, ports 50051/50061)
# CLANG_PACKAGE   the checkout's clang package version, e.g.
#                 llvmorg-24-init-7747-g62397f8b-67 (CLANG_REVISION-CLANG_SUB_REVISION
#                 in tools/clang/scripts/update.py)
# LINUX_CLANG_SHA256  SHA-256 of the checkout's
#                 third_party/llvm-build/Release+Asserts_linux/bin/clang, which
#                 remote compiles carry; mac_worker_action.sh matches it to this clang
# SLOTS           parallel compiles, default: the number of CPU cores
#
# It installs into ~/nativelink:
#   nativelink                  the macOS build of the server's NativeLink version
#   clang/<CLANG_PACKAGE>/      Chromium's Mac clang of that revision, run
#                               instead of the Linux one
#   nativelink_worker.json5
#   worker_action.sh            from mac_worker_action.sh
# and a per-user LaunchAgent (no admin) that starts the worker at login, restarts
# it when it exits, and keeps the Mac awake with caffeinate while it runs.
# Rerun it after a clang roll: it adds the new clang next to the old ones.
set -euo pipefail

usage="usage: install_mac_worker.sh SCHEDULER_HOST CLANG_PACKAGE LINUX_CLANG_SHA256 [SLOTS]"
host="${1:?$usage}"
clang_package="${2:?$usage}"
linux_clang_sha256="${3:?$usage}"
slots="${4:-$(sysctl -n hw.ncpu)}"
nativelink_version="1.8.3"  # keep in sync with the Dockerfile
label="com.zitter.nativelink-worker"

[[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]] \
  || { echo "This installer is for Apple Silicon Macs." >&2; exit 1; }

from="$(cd "$(dirname "$0")" && pwd)"
dest="$HOME/nativelink"
mkdir -p "$dest/clang"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if ! "$dest/nativelink" --version 2>/dev/null | grep -qx "nativelink $nativelink_version"; then
  echo "Downloading NativeLink $nativelink_version"
  curl -fsSL -o "$tmp/nl.tgz" \
    "https://github.com/TraceMachina/nativelink/releases/download/v$nativelink_version/nativelink-$nativelink_version-aarch64-apple-darwin.tar.gz"
  mkdir "$tmp/nl"
  tar -xzf "$tmp/nl.tgz" -C "$tmp/nl"
  install -m 755 "$(find "$tmp/nl" -type f -name nativelink | head -1)" "$dest/nativelink"
fi

clang_dir="$dest/clang/$clang_package"
if [[ ! -x "$clang_dir/bin/clang" ]]; then
  echo "Downloading Mac clang $clang_package"
  curl -fsSL -o "$tmp/clang.tar.xz" \
    "https://commondatastorage.googleapis.com/chromium-browser-clang/Mac_arm64/clang-$clang_package.tar.xz"
  mkdir -p "$clang_dir.tmp"
  tar -xJf "$tmp/clang.tar.xz" -C "$clang_dir.tmp"
  rm -rf "$clang_dir"
  mv "$clang_dir.tmp" "$clang_dir"
fi
printf '%s\n' "$linux_clang_sha256" > "$clang_dir/linux_clang.sha256"
xattr -dr com.apple.quarantine "$dest" 2>/dev/null || true

launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
sleep 1

# Same config as the WSL workers, with the CAS and action directories on the
# case-sensitive image (see below).
tr -d '\r' < "$from/nativelink_worker.json5" \
  | sed -e 's#${HOME}/nativelink/cas#${HOME}/nativelink/vol/cas#g' \
        -e 's#${HOME}/nativelink/work"#${HOME}/nativelink/vol/work"#g' \
  > "$dest/nativelink_worker.json5"
grep -q 'nativelink/vol/work' "$dest/nativelink_worker.json5" \
  || { echo "nativelink_worker.json5 paths changed; update install_mac_worker.sh" >&2; exit 1; }
tr -d '\r' < "$from/mac_worker_action.sh" > "$dest/worker_action.sh"
chmod 755 "$dest/worker_action.sh"
rm -rf "$dest/clang-map"

# The hermetic Windows SDK has directories that differ only in case (um/gl and
# um/GL), and macOS volumes are case-insensitive by default. The worker's CAS
# and action directories live on a case-sensitive disk image instead (one
# volume, so the CAS can hardlink inputs into actions). It grows as needed;
# the CAS stays under ZITTER_CAS_BYTES (25 GB by default).
if [[ ! -d "$dest/data.sparsebundle" ]]; then
  echo "Creating the case-sensitive data image"
  hdiutil create -quiet -size 80g -type SPARSEBUNDLE -fs "Case-sensitive APFS" \
    -volname zitter-nativelink "$dest/data.sparsebundle"
fi
# Left by an install before the image.
rm -rf "$dest/cas" "$dest/cas-tmp" "$dest/work" "$dest/cas.exec" "$dest/cas.flush_sentinel"
cat > "$dest/run_mac_worker.sh" <<'EOF'
#!/bin/bash
# Started by the LaunchAgent: mounts the case-sensitive data image, then runs
# the worker under caffeinate so the Mac stays awake while it works.
set -euo pipefail
dest="$(cd "$(dirname "$0")" && pwd)"
vol="$dest/vol"
if ! mount | grep -q " on $vol ("; then
  mkdir -p "$vol"
  hdiutil attach -quiet -nobrowse -noautoopen -owners on -mountpoint "$vol" \
    "$dest/data.sparsebundle"
fi
mkdir -p "$vol/cas" "$vol/cas-tmp"
exec /usr/bin/caffeinate -i -s "$dest/nativelink" "$dest/nativelink_worker.json5"
EOF
chmod 755 "$dest/run_mac_worker.sh"

plist="$HOME/Library/LaunchAgents/$label.plist"
mkdir -p "$(dirname "$plist")"
cat > "$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$label</string>
  <key>ProgramArguments</key>
  <array>
    <string>/bin/bash</string><string>$dest/run_mac_worker.sh</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>HOME</key><string>$HOME</string>
    <key>ZITTER_SCHEDULER_HOST</key><string>$host</string>
    <key>ZITTER_WORKER_SLOTS</key><string>$slots</string>
    <key>ZITTER_WORKER_NAME</key><string>mac-$(scutil --get LocalHostName 2>/dev/null || hostname -s)</string>
  </dict>
  <key>SoftResourceLimits</key><dict><key>NumberOfFiles</key><integer>65536</integer></dict>
  <key>HardResourceLimits</key><dict><key>NumberOfFiles</key><integer>65536</integer></dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>30</integer>
  <key>ProcessType</key><string>Standard</string>
  <key>StandardOutPath</key><string>$dest/worker.log</string>
  <key>StandardErrorPath</key><string>$dest/worker.log</string>
</dict>
</plist>
EOF

: > "$dest/worker.log"
launchctl bootstrap "gui/$(id -u)" "$plist"
sleep 3
if pgrep -f "$dest/nativelink " > /dev/null; then
  echo "worker running: $slots slots, scheduler $host, clang $clang_package"
  echo "log: $dest/worker.log"
else
  echo "worker exited:" >&2
  tail -30 "$dest/worker.log" >&2
  exit 1
fi
