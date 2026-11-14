#!/bin/bash
# Installs the NativeLink worker into ~/nativelink inside WSL from the folder
# this script was copied to (nativelink binary, worker config, entrypoint).
set -euo pipefail
from="$(cd "$(dirname "$0")" && pwd)"
dest="$HOME/nativelink"
mkdir -p "$dest"
for f in nativelink nativelink_worker.json5 worker_action.sh run_wsl_worker.sh; do
  cp "$from/$f" "$dest/$f"
done
sed -i 's/\r$//' "$dest/worker_action.sh" "$dest/nativelink_worker.json5" "$dest/run_wsl_worker.sh"
chmod 755 "$dest/nativelink" "$dest/worker_action.sh" "$dest/run_wsl_worker.sh"
echo "kernel: $(uname -r)"
echo "glibc: $(ldd --version | head -1)"
echo "cpus: $(nproc)"
free -g | head -2
"$dest/nativelink" --version
