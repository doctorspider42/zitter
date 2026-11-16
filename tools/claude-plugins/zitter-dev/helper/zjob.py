#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Host-side helper of the zitter-dev mod, the same on macOS and Windows.

The mod has no shell of its own, so every look at processes, files and disks
goes through one of these subcommands. Each prints plain text or JSON and
exits 0 unless it says otherwise.

  zjob.py info                      {"platform", "jobsDir"}
  zjob.py start <script> <log>      start a job detached in its own process group; print its pid
  zjob.py alive <pid>               exit 0 while the process runs, 1 once it is gone
  zjob.py kill <pid>                stop the job and everything it started
  zjob.py tail <file> <bytes>       the last bytes of a file, as text
  zjob.py grep <file> <regex>       matching lines as `<n>:<line>`
  zjob.py status <root>             {"busy", "appRunning", "appBuiltAt", "diskFree"}
  zjob.py quit-app <root>           quit the Content dev browser
  zjob.py launch <root>             quit it, then start the last Content build detached; print its pid
  zjob.py rm <file>...              delete files (missing ones are fine)
  zjob.py sleep <seconds>           wait
  zjob.py open <file>               open a file in the default text editor
"""
import json
import os
import re
import shutil
import signal
import subprocess
import sys
from pathlib import Path

WINDOWS = sys.platform == "win32"

# Commands that use the shared build directory.
BUSY = re.compile(r"third_party[\\/]ninja[\\/]ninja|[\\/]build\.sh\b|build_app(_win)?\.(sh|ps1)|\bautoninja\b|"
                  r"install_chromium_overlay|\bsiso(\.exe)?\b|\bninja\.exe\b", re.I)


def jobs_dir() -> Path:
    if WINDOWS:
        base = Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData/Local")
    else:
        base = Path.home() / "Library/Caches"
    return base / "zitter-dev" / "jobs"


def app_path(root: Path) -> Path:
    """The Content browser the dev build makes."""
    return root / (".chromium/src/out/ZitterWinContent/zitter.exe" if WINDOWS else "build/ZitterContentLab.app")


def app_binary(root: Path) -> Path:
    return app_path(root) if WINDOWS else app_path(root) / "Contents/MacOS/Zitter Content Lab"


def processes():
    """(pid, command line) of every process the user can see."""
    if WINDOWS:
        out = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "Get-CimInstance Win32_Process | ForEach-Object { \"$($_.ProcessId)`t$($_.CommandLine)\" }"],
            capture_output=True, text=True, errors="replace", creationflags=subprocess.CREATE_NO_WINDOW).stdout
        rows = (line.split("\t", 1) for line in out.splitlines() if "\t" in line)
    else:
        out = subprocess.run(["/bin/ps", "-axo", "pid=,command="], capture_output=True, text=True, errors="replace").stdout
        rows = (line.strip().split(" ", 1) for line in out.splitlines() if line.strip())
    me = os.getpid()
    for row in rows:
        if len(row) == 2 and row[0].strip().isdigit() and int(row[0]) != me:
            yield int(row[0]), row[1]


def app_processes(root: Path):
    needle = str(app_path(root) if WINDOWS else app_path(root) / "Contents/MacOS/").lower().replace("\\", "/")
    for pid, command in processes():
        if needle in command.lower().replace("\\", "/"):
            yield pid


def alive(pid: int) -> bool:
    if WINDOWS:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        return code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def kill_tree(pid: int):
    if WINDOWS:
        subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)
        return
    try:
        os.killpg(pid, signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass


def quit_app(root: Path):
    import time
    pids = list(app_processes(root))
    for pid in pids:
        kill_tree(pid) if WINDOWS else os.kill(pid, signal.SIGTERM)
    for _ in range(50):
        if not any(alive(pid) for pid in pids):
            return
        time.sleep(0.2)


def launch(root: Path) -> int:
    """Start the Content browser the way `./build.sh --content --run` does, but detached."""
    binary = app_binary(root)
    if not binary.exists():
        print(f"No Content build at {app_path(root)}; build it first.")
        return 1
    quit_app(root)
    if WINDOWS:
        # A copy started from an MSIX-packaged parent (the Claude app) sees a virtualized
        # AppData; Explorer starts it as a plain launch (docs/windows-port.md).
        subprocess.Popen(["explorer.exe", str(binary)])
        print(f"Started {binary}")
        return 0
    import secrets
    import tempfile
    workspace = Path(tempfile.mkdtemp(prefix="zitter-content-dev-")).resolve()
    key = workspace / "probe-cookie-key"
    key.write_bytes(secrets.token_bytes(32))
    key.chmod(0o600)
    proc = subprocess.Popen([str(binary), "--zitter-content-workspace", f"--zitter-content-workspace-root={workspace}"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL,
                            start_new_session=True)
    print(f"Started the Content browser (pid {proc.pid}) with a fresh workspace in {workspace}")
    return 0


def main(argv):
    command, args = argv[0], argv[1:]
    if command == "info":
        jobs_dir().mkdir(parents=True, exist_ok=True)
        print(json.dumps({"platform": "windows" if WINDOWS else "macos", "jobsDir": jobs_dir().as_posix()}))
    elif command == "start":
        script, log = args
        out = open(log, "wb")
        if WINDOWS:
            flags = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS | subprocess.CREATE_NO_WINDOW
            proc = subprocess.Popen(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script],
                                    stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, creationflags=flags)
        else:
            proc = subprocess.Popen(["/bin/zsh", script], stdout=out, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, start_new_session=True)
        print(proc.pid)
    elif command == "alive":
        return 0 if alive(int(args[0])) else 1
    elif command == "kill":
        kill_tree(int(args[0]))
    elif command == "tail":
        path, size = Path(args[0]), int(args[1])
        if path.is_file():
            with open(path, "rb") as f:
                f.seek(max(0, path.stat().st_size - size))
                sys.stdout.write(f.read().decode("utf-8", "replace"))
    elif command == "grep":
        path, pattern = Path(args[0]), re.compile(args[1])
        if path.is_file():
            with open(path, encoding="utf-8", errors="replace") as f:
                for n, line in enumerate(f, 1):
                    if pattern.search(line):
                        sys.stdout.write(f"{n}:{line.rstrip()}\n")
    elif command == "status":
        root = Path(args[0])
        busy = next((f"pid {pid}: {cmd[:90]}" for pid, cmd in processes() if BUSY.search(cmd)), None)
        app = app_path(root)
        print(json.dumps({
            "busy": busy,
            "appRunning": any(True for _ in app_processes(root)),
            "appBuiltAt": int(app.stat().st_mtime * 1000) if app.exists() else None,
            "diskFree": shutil.disk_usage(root / ".chromium" if (root / ".chromium").exists() else root).free,
        }))
    elif command == "quit-app":
        quit_app(Path(args[0]))
    elif command == "launch":
        return launch(Path(args[0]))
    elif command == "rm":
        for name in args:
            Path(name).unlink(missing_ok=True)
    elif command == "sleep":
        import time
        time.sleep(float(args[0]))
    elif command == "open":
        if WINDOWS:
            os.startfile(args[0])  # noqa: the default editor for the file's type
        else:
            subprocess.run(["/usr/bin/open", "-t", args[0]])
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
