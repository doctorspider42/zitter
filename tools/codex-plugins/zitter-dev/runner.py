# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Persistent local queue and build log snapshots for Zitter Dev."""
from collections import deque
from contextlib import nullcontext
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from uuid import uuid4

WINDOWS = sys.platform == "win32"
OUTPUT = "out/ZitterWinContent" if WINDOWS else "out/ZitterContentBundle"
APP_BINARY = (f".chromium/src/{OUTPUT}/zitter.exe" if WINDOWS else
              "build/ZitterContentLab.app/Contents/MacOS/Zitter Content Lab")
HELPER = Path("tools/claude-plugins/zitter-dev/helper/zjob.py")
KINDS = ("build", "run", "launch", "test")
ACTIVE = {"queued", "running"}
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
NINJA = re.compile(r"\[(\d+)/(\d+)\][ \t]+([^\r\n]*)")
ERROR = re.compile(r"FAILED:|error:|error\[|\[ +FAILED +\]|Traceback", re.I)


def checkout(path=None):
    start = Path(path or Path.cwd()).expanduser()
    if not start.is_absolute():
        raise ValueError("Enter the absolute path to your Zitter checkout.")
    current = start.resolve(strict=True)
    for root in (current, *current.parents):
        if (root / ".git").exists():
            if all((root / name).is_file() for name in (HELPER, "build.sh", "tools/stage_content_embedder.py")):
                return root
            break
    raise ValueError("Select a Zitter checkout containing the Zitter Dev helper.")


def host_helper(root):
    spec = importlib.util.spec_from_file_location("zjob", root / HELPER)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    return helper


def cache_dir(root):
    base = (Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData/Local") if WINDOWS
            else Path.home() / "Library/Caches")
    key = hashlib.sha256(os.fsencode(os.path.normcase(str(root)))).hexdigest()
    return base / "zitter-dev" / "codex" / key


def write_json(path, data):
    temporary = path.parent / f".{uuid4().hex}.tmp"
    try:
        temporary.write_text(json.dumps(data), encoding="utf-8")
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def write_job(directory, job):
    write_json(directory / f"{job['id']}.json", job)


def jobs(directory):
    return sorted(
        (json.loads(path.read_text(encoding="utf-8")) for path in directory.glob("*.json")),
        key=lambda job: (job["queued"], job["id"]),
    )


def marker(directory, job):
    return directory / f"{job['id']}.stop"


def log_file(directory, job):
    return directory / f"{job['id']}.log"


def finish(directory, job, state, **fields):
    write_job(directory, {**job, **fields, "state": state, "ended": time.time()})
    marker(directory, job).unlink(missing_ok=True)


def require_output(root):
    output = root / ".chromium" / "src" / OUTPUT
    if not (output / "args.gn").is_file():
        raise ValueError(f"Set up .chromium/src/{OUTPUT} first. See docs/remote-builds.md.")
    return output


def enqueue(root, kind):
    if kind not in KINDS:
        raise ValueError("Choose build, run, launch or test.")
    if kind in ("build", "run"):
        require_output(root)
    directory = cache_dir(root)
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    job = {"id": uuid4().hex, "state": "queued", "kind": kind, "queued": time.time()}
    write_job(directory, job)
    flags = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS if WINDOWS else 0
    try:
        with (directory / "worker.log").open("ab") as log:
            subprocess.Popen(
                [sys.executable, str(Path(__file__).resolve()), str(root), job["id"]],
                cwd=root, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=not WINDOWS, creationflags=flags,
            )
    except OSError as error:
        finish(directory, job, "failed", errors=[str(error)])
        raise
    return job


def get_job(directory, job_id=None):
    if job_id is None:
        recent = jobs(directory)
        if recent:
            return recent[-1]
    elif re.fullmatch(r"[a-f0-9]{32}", job_id):
        path = directory / f"{job_id}.json"
        if path.is_file():
            return json.loads(path.read_text(encoding="utf-8"))
    raise ValueError("Job not found.")


def stop(directory, job_id):
    job = get_job(directory, job_id)
    if job["state"] not in ACTIVE:
        return {"id": job_id, "state": job["state"]}
    marker(directory, job).touch()
    # Completion may have raced with the stop request.
    if get_job(directory, job_id)["state"] not in ACTIVE:
        marker(directory, job).unlink(missing_ok=True)
    return {"id": job_id, "stopRequested": True}


def stop_child(child):
    if WINDOWS:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if child.poll() is None:
            child.kill()
    else:
        try:
            os.killpg(child.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass
        # A descendant can outlive the top-level shell.
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    child.wait()


def run_child(root, directory, job, command, log, env):
    if marker(directory, job).exists():
        return None
    child = subprocess.Popen(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                             stdin=subprocess.DEVNULL, start_new_session=not WINDOWS)
    while child.poll() is None:
        if marker(directory, job).exists():
            stop_child(child)
            return None
        time.sleep(0.2)
    return child.returncode


def execute(root, directory, job):
    kind = job["kind"]
    log_path = log_file(directory, job)
    job = {**job, "state": "running", "started": time.time(), "worker": os.getpid()}
    write_job(directory, job)
    env = os.environ.copy()
    with log_path.open("ab", buffering=0) as log:
        def run(*args):
            return run_child(root, directory, job, args, log, env)

        result = 0
        if kind in ("build", "run"):
            require_output(root)
            if WINDOWS:
                env["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"
                env["PATH"] = str(root / ".chromium/depot_tools") + os.pathsep + env.get("PATH", "")
                result = run(sys.executable, "tools/stage_content_embedder.py", ".chromium/src")
                if result == 0:
                    result = run("cmd.exe", "/d", "/c", "autoninja.bat", "-C", OUTPUT, "zitter_content")
            else:
                env["ZITTER_CONTENT_OUT"] = OUTPUT
                env["ZITTER_CONTENT_PACKAGE_DIR"] = str(root / "build")
                result = run("/bin/zsh", "./build.sh", "--content")
        if result == 0 and kind in ("run", "launch"):
            result = run(sys.executable, str(root / HELPER), "launch", str(root))
        if kind == "test":
            depot_python = root / ".chromium/depot_tools/python-bin/python3"
            python = str(depot_python) if not WINDOWS and depot_python.is_file() else sys.executable
            result = run(python, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py")

    if result is None:
        finish(directory, job, "stopped")
    elif result == 0:
        finish(directory, job, "ok", exitCode=result)
    else:
        with log_path.open(encoding="utf-8", errors="replace") as log:
            errors = deque((ANSI.sub("", line).strip() for line in log if ERROR.search(line)), maxlen=12)
        finish(directory, job, "failed", exitCode=result, errors=list(dict.fromkeys(errors)))


def drain(root, directory, helper):
    for job in jobs(directory):
        if job["state"] == "running":
            finish(directory, job, "failed", errors=["The build worker exited unexpectedly."])
    while True:
        job = next((item for item in jobs(directory) if item["state"] == "queued"), None)
        if not job:
            return
        try:
            lock = helper.chromium_lock(root)
            while not marker(directory, job).exists():
                with (nullcontext(True) if job["kind"] == "test" else helper.file_lock(lock)) as acquired:
                    if acquired:
                        execute(root, directory, job)
                        break
                time.sleep(0.25)
            else:
                finish(directory, job, "stopped")
        except (OSError, ValueError) as error:
            finish(directory, job, "failed", errors=[str(error)])


def worker(root, own_id):
    directory = cache_dir(root)
    helper = host_helper(root)
    while get_job(directory, own_id)["state"] in ACTIVE:
        with helper.file_lock(directory / "worker.lock") as acquired:
            if acquired:
                drain(root, directory, helper)
                return
        time.sleep(0.25)


def snapshot(directory, job, include_log=False):
    path = log_file(directory, job)
    public = {key: value for key, value in job.items() if key != "worker"}
    public["logPath"] = str(path)
    public["stopRequested"] = marker(directory, job).exists()
    if not path.is_file():
        return public
    with path.open("rb") as log:
        log.seek(max(0, os.fstat(log.fileno()).st_size - 16384))
        tail = ANSI.sub("", log.read().decode("utf-8", errors="replace"))
    if include_log:
        public["log"] = "\n".join(tail.splitlines()[-80:])
    if job["state"] == "running":
        last = deque(NINJA.finditer(tail), maxlen=1)
        if last:
            done, total, step = last[0].groups()
            if 0 < int(done) <= int(total):
                public["progress"] = {"done": int(done), "total": int(total), "step": step.strip()}
    return public


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    worker(checkout(sys.argv[1]), sys.argv[2])
