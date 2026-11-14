#!/bin/bash
# Starts the NativeLink worker in WSL.
#
#   run_wsl_worker.sh SCHEDULER_HOST [SLOTS]               detached, logs to ~/nativelink/worker.log
#   run_wsl_worker.sh --foreground SCHEDULER_HOST [SLOTS]  in this terminal
#
# WSL stops a distro shortly after its last wsl.exe session ends, so a detached
# worker only lives while some wsl.exe stays open. Use --foreground from a
# window or a logon task that keeps wsl.exe running.
set -euo pipefail
foreground=0
if [[ "${1:-}" == "--foreground" ]]; then
  foreground=1
  shift
fi
host="${1:?usage: run_wsl_worker.sh [--foreground] SCHEDULER_HOST [SLOTS]}"
slots="${2:-14}"
dir="$HOME/nativelink"
pkill -f "$dir/nativelink " 2>/dev/null || true
sleep 1
export ZITTER_SCHEDULER_HOST="$host" ZITTER_WORKER_SLOTS="$slots"
if (( foreground )); then
  echo "NativeLink worker: $slots slots, scheduler $host (Ctrl+C stops it)"
  exec "$dir/nativelink" "$dir/nativelink_worker.json5" 2>&1 | tee "$dir/worker.log"
fi
nohup setsid "$dir/nativelink" "$dir/nativelink_worker.json5" \
  > "$dir/worker.log" 2>&1 < /dev/null &
sleep 3
if pgrep -f "$dir/nativelink " > /dev/null; then
  echo "worker running, $slots slots, scheduler $host"
else
  echo "worker exited:" >&2
  tail -30 "$dir/worker.log" >&2
  exit 1
fi
