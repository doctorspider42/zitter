#!/usr/bin/env python3
"""Fresh Chromium checkout + full Zitter build with remote compiles.

The case remote builds are for: a new Chromium revision means rebuilding
everything. Steps, each timed in <log-dir>/fresh_build.log:

  checkout  move an existing .chromium/src aside (renamed, never deleted), write
            .gclient, `gclient sync` at chromium/revision.json, `gclient runhooks`
  overlay   content: tools/stage_content_embedder.py;
            chrome: tools/install_chromium_overlay.py
  siso      tools/remote_build/setup_siso.py (hermetic toolchain, Linux clang)
  gn        content: chromium/windows-release.args.gn (no ThinLTO or PGO, as the
            macOS Content build), root target //zitter_content:zitter_content;
            chrome: chromium/windows-official.args.gn;
            both with Siso and remote execution on
  build     tools/remote_build/build.py: zitter_content, or zitter zitter_setup

  python tools/remote_build/fresh_build.py              # Content, all steps
  python tools/remote_build/fresh_build.py --from gn    # resume at a step
  python tools/remote_build/fresh_build.py --product chrome

--keep-going keeps compiling past failed steps, for first builds where the
point is to measure the whole compile and collect every error at once.

Needs the NativeLink server running (start_server.ps1) and, for remote help,
the other PCs' workers. A full build takes hours and 10-20 GB in the output
directory.
"""

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
CHROMIUM = os.path.join(REPO, ".chromium")
SRC = os.path.join(CHROMIUM, "src")
DEPOT_TOOLS = os.path.join(CHROMIUM, "depot_tools")
VS2026 = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
STEPS = ["checkout", "overlay", "siso", "gn", "build"]

LOG = None


def log(msg):
    line = f"{datetime.datetime.now():%Y-%m-%d %H:%M:%S} {msg}"
    print(line, flush=True)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def env(hermetic):
    e = dict(os.environ)
    e["PATH"] = DEPOT_TOOLS + os.pathsep + e.get("PATH", "")
    e["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "1" if hermetic else "0"
    e["vs2026_install"] = VS2026
    # Chromium's scripts decode tool output (icacls) in the console code page.
    e.pop("PYTHONUTF8", None)
    # Chromium needs LF checkouts; override autocrlf for these commands only.
    e["GIT_CONFIG_COUNT"] = "1"
    e["GIT_CONFIG_KEY_0"] = "core.autocrlf"
    e["GIT_CONFIG_VALUE_0"] = "false"
    return e


def run(cmd, cwd, hermetic=False, logfile=None):
    log("run: " + " ".join(cmd))
    out = open(logfile, "a", encoding="utf-8") if logfile else None
    try:
        rc = subprocess.run(cmd, cwd=cwd, env=env(hermetic), shell=(os.name == "nt"
                            and cmd[0] in ("gclient", "gn", "git")),
                            stdout=out, stderr=subprocess.STDOUT if out else None).returncode
    finally:
        if out:
            out.close()
    if rc != 0:
        log(f"FAILED (exit {rc})" + (f", see {logfile}" if logfile else ""))
        sys.exit(rc)


def step_checkout(log_dir):
    if not os.path.isdir(DEPOT_TOOLS):
        run(["git", "clone", "https://chromium.googlesource.com/chromium/tools/depot_tools.git",
             DEPOT_TOOLS], CHROMIUM)
    if os.path.exists(SRC):
        aside = f"{SRC}.old-{datetime.datetime.now():%Y%m%d-%H%M%S}"
        os.rename(SRC, aside)
        log(f"moved the old checkout to {aside} (delete it with rmdir /s /q when done)")
    with open(os.path.join(REPO, "chromium", "revision.json"), encoding="utf-8") as f:
        revision = json.load(f)["revision"]
    with open(os.path.join(CHROMIUM, ".gclient"), "w", encoding="utf-8") as f:
        f.write(
            'solutions = [{\n'
            '  "name": "src",\n'
            '  "url": "https://chromium.googlesource.com/chromium/src.git",\n'
            '  "managed": False,\n'
            '  "custom_deps": {},\n'
            '  "custom_vars": {"checkout_pgo_profiles": True},\n'
            '}]\n'
            'target_os = ["win"]\n')
    glog = os.path.join(log_dir, "gclient.log")
    run(["gclient", "sync", "--no-history", "--nohooks", "-D", "--revision",
         f"src@{revision}"], CHROMIUM, logfile=glog)
    run(["gclient", "runhooks"], CHROMIUM, logfile=glog)


PRODUCTS = {
    "content": {
        "stage": "stage_content_embedder.py",
        "args": "windows-release.args.gn",
        "out": "out/ZitterWinContent",
        "gn_extra": ["--root-target=//zitter_content:zitter_content"],
        "targets": ["zitter_content"],
    },
    "chrome": {
        "stage": "install_chromium_overlay.py",
        "args": "windows-official.args.gn",
        "out": "out/ZitterWinOfficial",
        "gn_extra": [],
        "targets": ["zitter", "zitter_setup"],
    },
}


def step_overlay(log_dir, product):
    run([sys.executable, os.path.join(REPO, "tools", product["stage"]), SRC],
        REPO, logfile=os.path.join(log_dir, "overlay.log"))


def step_siso(log_dir):
    run([sys.executable, os.path.join(HERE, "setup_siso.py")], REPO,
        logfile=os.path.join(log_dir, "setup_siso.log"))


def step_gn(log_dir, out, product):
    with open(os.path.join(REPO, "chromium", product["args"]), encoding="utf-8") as f:
        lines = f.read().splitlines()
    args = []
    for line in lines:
        key = line.split("=")[0].strip()
        if key in ("use_siso", "use_remoteexec"):
            continue
        args.append(line)
    args += ["", "# Remote compiles (tools/remote_build/windows-remote.args.gn).",
             "use_siso = true", "use_remoteexec = true", ""]
    out_dir = os.path.join(SRC, out)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "args.gn"), "w", encoding="utf-8") as f:
        f.write("\n".join(args))
    run(["gn", "gen", out] + product["gn_extra"], SRC, hermetic=True,
        logfile=os.path.join(log_dir, "gn.log"))


def step_build(log_dir, out, product, remote_jobs, scheduler, keep_going):
    cmd = [sys.executable, os.path.join(HERE, "build.py"), "-C", out,
           f"--remote-jobs={remote_jobs}", f"--scheduler={scheduler}"]
    if keep_going:
        cmd.append("-k=0")
    run(cmd + product["targets"], REPO, hermetic=True,
        logfile=os.path.join(log_dir, "build.log"))


def main():
    global LOG
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--from", dest="start", choices=STEPS, default="checkout")
    p.add_argument("--to", dest="stop", choices=STEPS, default="build")
    p.add_argument("--product", choices=sorted(PRODUCTS), default="content")
    p.add_argument("--out", help="output dir (default per product)")
    p.add_argument("--remote-jobs", type=int, default=44)
    p.add_argument("--scheduler", default="127.0.0.1")
    p.add_argument("--keep-going", action="store_true")
    p.add_argument("--log-dir", default=os.path.join(REPO, "build", "fresh-build"))
    a = p.parse_args()
    product = PRODUCTS[a.product]
    a.out = a.out or product["out"]

    os.makedirs(a.log_dir, exist_ok=True)
    LOG = os.path.join(a.log_dir, "fresh_build.log")
    steps = STEPS[STEPS.index(a.start):STEPS.index(a.stop) + 1]
    log(f"=== fresh build: {a.product}, steps {', '.join(steps)}; out {a.out}")
    total = time.monotonic()
    for s in steps:
        t = time.monotonic()
        log(f"--- {s}")
        if s == "checkout":
            step_checkout(a.log_dir)
        elif s == "overlay":
            step_overlay(a.log_dir, product)
        elif s == "siso":
            step_siso(a.log_dir)
        elif s == "gn":
            step_gn(a.log_dir, a.out, product)
        elif s == "build":
            step_build(a.log_dir, a.out, product, a.remote_jobs, a.scheduler,
                       a.keep_going)
        log(f"--- {s} done in {(time.monotonic() - t) / 60:.1f} min")
    log(f"=== all done in {(time.monotonic() - total) / 3600:.2f} h")


if __name__ == "__main__":
    main()
