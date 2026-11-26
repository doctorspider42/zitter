#!/usr/bin/env python3
"""Checks the remote NativeLink workers and starts the ones that are down.

  python tools/remote_build/workers.py            # check, start what is down
  python tools/remote_build/workers.py --no-start # only check

build.py runs this before every build and fire.py --check runs it in the
preflight, so a worker that a reboot, sleep or logoff took down is noticed
before a long build runs without it. The build PC's own worker runs in Docker
with the server and is checked by fire.py.

Each worker is an SSH alias (docs/remote-builds.md). WSL workers start through
their scheduled task (install_logon_task.ps1), the Mac through its LaunchAgent.
A worker that does not answer over SSH is off or asleep: turn it on.
"""

import argparse
import concurrent.futures
import subprocess
import time

NO_WINDOW = 0x08000000 if hasattr(subprocess, "CREATE_NO_WINDOW") else 0
TASK = "Zitter NativeLink worker"
MAC_AGENT = "com.zitter.nativelink-worker"

# name, SSH alias, kind
WORKERS = [
    ("laptop", "zitter-node", "wsl"),
    ("old PC", "zitter-node2", "wsl"),
    ("Mac", "zitter-mac", "mac"),
]

CHECK = {
    "wsl": "wsl -d Ubuntu -- pgrep -f nativelink/nativelink",
    "mac": "pgrep -f nativelink/nativelink",
}
START = {
    # Works from both cmd.exe and PowerShell as the remote shell.
    "wsl": f'schtasks /run /tn "{TASK}"',
    "mac": (f"launchctl print gui/$(id -u)/{MAC_AGENT} >/dev/null 2>&1 || "
            f"launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/{MAC_AGENT}.plist; "
            f"launchctl kickstart gui/$(id -u)/{MAC_AGENT}"),
}


def ssh(host, command, timeout=40):
    try:
        return subprocess.run(
            ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", host, command],
            capture_output=True, text=True, timeout=timeout, encoding="utf-8",
            errors="replace", creationflags=NO_WINDOW)
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        return subprocess.CompletedProcess(command, 255, "", str(e))


def state(host, kind):
    """'up', 'down' (answers, worker not running) or 'unreachable'."""
    r = ssh(host, CHECK[kind])
    if r.returncode == 255:
        return "unreachable"
    return "up" if r.returncode == 0 and r.stdout.strip() else "down"


def ensure(start=True, wait=60, workers=WORKERS):
    """Returns {name: state}; starts down workers and waits for them when start."""
    with concurrent.futures.ThreadPoolExecutor(len(workers)) as pool:
        states = dict(zip([w[0] for w in workers],
                          pool.map(lambda w: state(w[1], w[2]), workers)))
    down = [w for w in workers if states[w[0]] == "down"]
    if start and down:
        for name, host, kind in down:
            ssh(host, START[kind])
        deadline = time.monotonic() + wait
        while down and time.monotonic() < deadline:
            time.sleep(5)
            for w in list(down):
                if state(w[1], w[2]) == "up":
                    states[w[0]] = "started"
                    down.remove(w)
    return states


def describe(states):
    hosts = {w[0]: w[1] for w in WORKERS}
    hint = {"unreachable": "off or asleep, turn it on",
            "down": "not running; if starting fails, see its worker.log",
            "started": "was down, started now"}
    lines = []
    for name, s in states.items():
        extra = f" ({hint[s]})" if s in hint else ""
        lines.append(f"  {name:<7} {hosts.get(name, ''):<13} {s}{extra}")
    return "\n".join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--no-start", action="store_true", help="only report")
    args = p.parse_args()
    states = ensure(start=not args.no_start)
    print("remote workers:")
    print(describe(states))
    raise SystemExit(0 if all(s in ("up", "started") for s in states.values()) else 1)


if __name__ == "__main__":
    main()
