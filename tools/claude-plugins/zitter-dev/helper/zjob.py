#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Host-side helper of the zitter-dev mod, the same on macOS and Windows.

The mod has no shell of its own, so every look at processes, files and disks
goes through one of these subcommands. Each prints plain text or JSON and
exits 0 unless it says otherwise.

  zjob.py info                      {"platform", "jobsDir"}
  zjob.py start <script> <log> [root]  start a detached job, with the Chromium lock if root is given
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
from contextlib import contextmanager
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

WINDOWS = sys.platform == "win32"

# Commands that use the shared build directory.
BUSY = re.compile(r"third_party[\\/]ninja[\\/]ninja|[\\/]build\.sh\b|build_app(_win)?\.(sh|ps1)|\bautoninja\b|"
                  r"stage_content_embedder|\bsiso(\.exe)?\b|\bninja\.exe\b", re.I)


def jobs_dir() -> Path:
    if WINDOWS:
        base = Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData/Local")
    else:
        base = Path.home() / "Library/Caches"
    return base / "zitter-dev" / "jobs"


@contextmanager
def file_lock(path: Path):
    """Yield whether an exclusive, process-owned lock was acquired."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as handle:
        if WINDOWS:
            import msvcrt

            handle.seek(0)
            try:
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                if error.errno != 13:
                    raise
                yield False
                return
        else:
            import fcntl

            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                yield False
                return
        yield True


def chromium_lock(root: Path) -> Path:
    # Staging writes to the Chromium source tree even when the output dirs differ.
    source = os.path.normcase(str((root / ".chromium" / "src").resolve()))
    key = hashlib.sha256(os.fsencode(source)).hexdigest()
    return jobs_dir().parent / "locks" / f"{key}.lock"


def script_command(script):
    if WINDOWS:
        return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script]
    return ["/bin/zsh", script]


def app_path(root: Path) -> Path:
    """The Content browser the dev build makes."""
    return root / (".chromium/src/out/ZitterWinContent/zitter.exe" if WINDOWS else "build/ZitterContentLab.app")


def app_binary(root: Path) -> Path:
    return app_path(root) if WINDOWS else app_path(root) / "Contents/MacOS/Zitter Content Lab"


def processes():
    """(pid, command line) of every process the user can see, but this helper and its ancestors.

    The ancestors are the shell that ran the helper: its command line can name the
    very patterns callers look for.
    """
    if WINDOWS:
        out = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "Get-CimInstance Win32_Process | ForEach-Object { \"$($_.ProcessId)`t$($_.ParentProcessId)`t$($_.CommandLine)\" }"],
            capture_output=True, text=True, errors="replace", creationflags=subprocess.CREATE_NO_WINDOW).stdout
        rows = [line.split("\t", 2) for line in out.splitlines() if line.count("\t") >= 2]
    else:
        out = subprocess.run(["/bin/ps", "-axo", "pid=,ppid=,command="], capture_output=True, text=True,
                             errors="replace").stdout
        rows = [line.split(None, 2) for line in out.splitlines() if len(line.split(None, 2)) == 3]
    table = {}
    for row in rows:
        if row[0].strip().isdigit() and row[1].strip().isdigit():
            table[int(row[0])] = (int(row[1]), row[2])
    skip, pid = set(), os.getpid()
    while pid in table and pid not in skip:
        skip.add(pid)
        pid = table[pid][0]
    for pid, (_, command) in table.items():
        if pid not in skip:
            yield pid, command


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
        script, log, *root = args
        argv = [sys.executable, __file__, "locked", root[0], script] if root else script_command(script)
        options = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS}
                   if WINDOWS else {"start_new_session": True})
        with open(log, "wb") as out:
            proc = subprocess.Popen(argv, stdout=out, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, **options)
        print(proc.pid)
    elif command == "locked":
        root, script = args
        lock = chromium_lock(Path(root))
        while True:
            with file_lock(lock) as acquired:
                if acquired:
                    child = subprocess.Popen(script_command(script))
                    if not WINDOWS:
                        # Ctrl+Stop signals the whole group. Keep the lock until the script exits.
                        signal.signal(signal.SIGTERM, lambda *_: None)
                    return child.wait()
            time.sleep(0.25)
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
