#!/usr/bin/env python3
"""Overnight release build of one commit for Windows and macOS at once.

Turn on the build PC, the worker PCs and the Mac (logged in, on power), then
on the build PC (the preflight starts workers that are down):

  python tools/nightly/fire.py --check   preflight only (also asks the Mac's
                                         session for any permission it needs)
  python tools/nightly/fire.py           preflight, then start in the background
  python tools/nightly/fire.py --status  the last run's state

It builds the checked-out commit (clean tree, HEAD) in parallel:

- Windows on this PC: staging, Siso build with C++ compiles on the LAN
  NativeLink cluster (docs/remote-builds.md), license audit, portable ZIP and
  setup EXE, the package runtime suite.
- macOS on the Mac: ~/Projects/zitter-nightly is checked out at the same
  commit, then tools/nightly/mac_job.py runs there as a LaunchAgent job in the
  user's session: ./build.sh, Developer ID signing, Apple notarization and the
  DMG (docs/local-releases.md).

Everything ends up in build/nightly/<run id>/: release/ holds the files ready
for `gh release upload`, summary.md says what happened. Nothing is published:
publishing stays a separate, explicit step.
"""

import argparse
import ctypes
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / ".chromium" / "src"
OUT_REL = "out/ZitterWinContent"
OUT = SRC / OUT_REL
RUNS = ROOT / "build" / "nightly"
MAC = "zitter-mac"
MAC_REPO = "~/Projects/zitter"
MAC_WORKTREE = "~/Projects/zitter-nightly"
MAC_RUNS = "~/zitter-nightly/runs"
MAC_LABEL = "com.zitter.nightly"
REPO = "doctorspider42/zitter"
NO_WINDOW = 0x08000000 if os.name == "nt" else 0


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def sh(cmd, cwd=ROOT, timeout=120, **kw):
    """Runs a command for its output; never raises on a non-zero exit."""
    try:
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout,
                              encoding="utf-8", errors="replace", creationflags=NO_WINDOW, **kw)
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        return subprocess.CompletedProcess(cmd, 255, "", str(e))


def ssh(host, script, timeout=60):
    return sh(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", host, script],
              timeout=timeout)


def parse_version(header_text):
    m = re.search(r'#define ZITTER_VERSION_SHORT L"(\d+\.\d+\.\d+)"', header_text)
    if not m:
        raise ValueError("ZITTER_VERSION_SHORT not found in chromium/app/zitter_version.h")
    return m.group(1)


def run_id_for(commit, when=None):
    when = when or datetime.datetime.now()
    return f"{when:%Y%m%d-%H%M}-{commit[:8]}"


def windows_release_names(version):
    return {"setup": f"Zitter-v{version}-windows-x64-setup.exe",
            "zip": f"Zitter-v{version}-windows-x64.zip",
            "sums": "SHA256SUMS-windows-x64.txt",
            "build": f"Zitter-v{version}-windows-x64-build.json"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# --------------------------------------------------------------------------
# Preflight


class Preflight:
    def __init__(self, args):
        self.args = args
        self.errors, self.warnings, self.facts = [], [], {}

    def error(self, msg):
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)

    def run(self):
        self.source()
        self.build_pc()
        self.workers()
        if not self.args.skip_mac:
            self.mac()
        return self

    def source(self):
        dirty = sh(["git", "status", "--porcelain"]).stdout.strip()
        if dirty:
            self.error("the working tree has uncommitted changes; commit them first:\n" + dirty)
        commit = sh(["git", "rev-parse", "HEAD"]).stdout.strip()
        self.facts["commit"] = commit
        self.facts["version"] = parse_version(
            (ROOT / "chromium/app/zitter_version.h").read_text(encoding="utf-8"))
        revision = json.loads((ROOT / "chromium/revision.json").read_text())["revision"]
        self.facts["chromiumRevision"] = revision
        tag = f"v{self.facts['version']}"
        rel = sh(["gh", "release", "view", tag, "--repo", REPO, "--json", "tagName"], timeout=30)
        if rel.returncode == 0:
            self.warn(f"{tag} is already published: this run is a rehearsal, do not upload "
                      "its files to that release (bump chromium/app/zitter_version.h for a new one)")

    def build_pc(self):
        if self.args.skip_windows:
            return
        head = sh(["git", "rev-parse", "HEAD"], cwd=SRC).stdout.strip()
        if head != self.facts["chromiumRevision"]:
            self.error(f"PC Chromium checkout is at {head[:12]}, chromium/revision.json wants "
                       f"{self.facts['chromiumRevision'][:12]}: sync it first")
        if os.name == "nt":
            busy = sh(["tasklist", "/fo", "csv", "/nh"]).stdout.lower()
            for name in ("siso.exe", "ninja.exe"):
                if f'"{name}"' in busy:
                    self.error(f"{name} is running on the PC: another build is in progress")
        docker = sh(["docker", "ps", "--filter", "name=zitter-nativelink",
                     "--format", "{{.Status}}"])
        if not docker.stdout.startswith("Up"):
            self.error("the NativeLink server is not running: start Docker Desktop and "
                       "tools/remote_build/start_server.ps1")
        free = shutil.disk_usage(ROOT).free // 2**30
        if free < 30:
            self.error(f"only {free} GB free on the PC")

    def workers(self):
        if self.args.skip_windows:
            return
        # Starts the workers that are down (tools/remote_build/workers.py).
        sys.path.insert(0, str(ROOT / "tools" / "remote_build"))
        import workers
        states = workers.ensure()
        self.facts["workers"] = states
        for name, state in states.items():
            if state == "unreachable":
                self.warn(f"the {name} worker does not answer over SSH (off or asleep): "
                          "the Windows build runs without it and takes longer")
            elif state == "down":
                self.warn(f"the {name} worker did not start: the Windows build runs "
                          "without it and takes longer")
            elif state == "started":
                self.warn(f"the {name} worker was down and has been started")

    def mac(self):
        r = ssh(MAC, f"""
echo "gui=$(launchctl print gui/$(id -u) >/dev/null 2>&1 && echo yes || echo no)"
echo "ac=$(pmset -g ps | head -1 | grep -q 'AC Power' && echo yes || echo no)"
echo "free=$(df -g ~ | tail -1 | awk '{{print $4}}')"
echo "chromium=$(git -C {MAC_REPO}/.chromium/src rev-parse HEAD)"
echo "busy=$(pgrep -fl 'ninja|build\\.sh' | grep -v pgrep | wc -l | tr -d ' ')"
echo "identity=$(security find-identity -v -p codesigning | grep -c 'Developer ID Application' || true)"
""")
        if r.returncode != 0:
            self.error(f"the Mac ({MAC}) does not answer over SSH: {r.stderr.strip()}")
            return
        info = dict(line.split("=", 1) for line in r.stdout.split() if "=" in line)
        if info.get("gui") != "yes":
            self.error("nobody is logged in on the Mac: log in (signing and the DMG need "
                       "the user's session)")
        if info.get("ac") != "yes":
            self.warn("the Mac runs on battery: it can sleep during the build")
        if int(info.get("free", "0")) < 40:
            self.error(f"only {info.get('free')} GB free on the Mac")
        if info.get("chromium") != self.facts["chromiumRevision"]:
            self.error(f"Mac Chromium checkout is at {info.get('chromium', '?')[:12]}, "
                       "chromium/revision.json wants another revision: sync it first")
        if info.get("busy", "0") != "0":
            self.error("a build is running on the Mac")
        if not self.args.no_notarize and info.get("identity", "0") == "0":
            self.error("no Developer ID Application identity on the Mac")

    def report(self):
        f = self.facts
        print(f"Zitter {f.get('version')} at {f.get('commit', '')[:12]}")
        for w in self.warnings:
            print(f"  warning: {w}")
        for e in self.errors:
            print(f"  ERROR: {e}")
        if not self.errors:
            print("  preflight passed")
        return not self.errors


PROBE = r'''
import subprocess, sys
notarize = sys.argv[1] == "1"
lines = []
def check(name, cmd):
    ok = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    lines.append(name + "=" + ("ok" if ok else "FAILED"))
if notarize:
    check("notary", ["xcrun", "notarytool", "history", "--keychain-profile", "zitter-notary"])
    # The DMG layout drives Finder; macOS asks once per requesting program.
    check("finder", ["osascript", "-e", 'tell application "Finder" to get name of startup disk'])
lines.append("done=1")
open("/tmp/zitter-nightly-probe.log", "w").write(" ".join(lines) + "\n")
'''


def mac_session_probe(notarize, wait_s=300):
    """Runs a short job in the Mac user's session, through /usr/bin/python3 like
    mac_job.py: checks notarytool and the Keychain there, and lets macOS ask now
    for the Finder automation the DMG layout needs, while someone is at the Mac."""
    script = f"""
probe=/tmp/zitter-nightly-probe
rm -f $probe.log
cat > $probe.py <<'PROBE_EOF'
{PROBE}
PROBE_EOF
launchctl remove {MAC_LABEL}.probe 2>/dev/null || true
launchctl submit -l {MAC_LABEL}.probe -- /usr/bin/python3 $probe.py {1 if notarize else 0}
for i in $(seq 1 {wait_s // 2}); do grep -q done=1 $probe.log 2>/dev/null && break; sleep 2; done
launchctl remove {MAC_LABEL}.probe 2>/dev/null || true
cat $probe.log 2>/dev/null
"""
    print(f"Checking the Mac's session (up to {wait_s // 60} min): if the Mac asks to let "
          "python3 control Finder, click Allow there.", flush=True)
    r = ssh(MAC, script, timeout=wait_s + 60)
    info = dict(l.split("=", 1) for l in r.stdout.split() if "=" in l)
    problems = []
    if info.get("done") != "1":
        problems.append("the Mac session probe did not finish (a permission prompt may be "
                        "waiting on the Mac's screen: allow it and rerun --check)")
    if info.get("notary") == "FAILED":
        problems.append("notarytool cannot use the zitter-notary Keychain profile in the "
                        "Mac's session (docs/local-releases.md, one-time account setup)")
    if info.get("finder") == "FAILED":
        problems.append("the DMG step cannot control Finder: allow python3 on the Mac "
                        "(System Settings > Privacy & Security > Automation) and rerun --check")
    return problems


# --------------------------------------------------------------------------
# The run


class Run:
    def __init__(self, run_id, facts, args):
        self.id = run_id
        self.dir = RUNS / run_id
        self.args = args
        self.lock = threading.Lock()
        path = self.dir / "status.json"
        if path.exists():
            self.status = json.loads(path.read_text(encoding="utf-8"))
        else:
            self.dir.mkdir(parents=True, exist_ok=True)
            self.status = {
                "runId": run_id, "commit": facts["commit"], "version": facts["version"],
                "chromiumRevision": facts["chromiumRevision"], "started": now(),
                "finished": None, "state": "running",
                "windows": {"state": "skipped" if args.skip_windows else "pending", "steps": []},
                "mac": {"state": "skipped" if args.skip_mac else "pending", "steps": []},
                "options": {k: getattr(args, k) for k in
                            ("skip_mac", "skip_windows", "no_notarize", "no_verify", "smoke")},
            }
            self.save()

    def save(self):
        with self.lock:
            tmp = self.dir / "status.json.tmp"
            tmp.write_text(json.dumps(self.status, indent=2) + "\n", encoding="utf-8")
            tmp.replace(self.dir / "status.json")

    def log(self, msg):
        line = f"[{now()}] {msg}"
        if self.args.foreground:
            print(line, flush=True)
        with open(self.dir / "fire.log", "a", encoding="utf-8") as f:
            f.write(line + "\n")

    # ---- Windows

    def win_step(self, name, cmd, env=None):
        entry = {"name": name, "state": "running", "started": now(), "finished": None,
                 "log": f"windows/{name}.log"}
        self.status["windows"]["steps"].append(entry)
        self.save()
        self.log(f"windows: {name}")
        log = self.dir / "windows" / f"{name}.log"
        log.parent.mkdir(exist_ok=True)
        with open(log, "w", encoding="utf-8") as out:
            out.write("$ " + " ".join(str(c) for c in cmd) + "\n")
            out.flush()
            # CONDA_SHLVL keeps a conda cmd.exe AutoRun hook from prepending its
            # condabin to PATH, which breaks setup_toolchain.py's SetEnv.cmd check
            # in the license audit's gn runs.
            rc = subprocess.run([str(c) for c in cmd], cwd=ROOT, stdout=out,
                                stderr=subprocess.STDOUT, creationflags=NO_WINDOW,
                                env=dict(os.environ, PYTHONUTF8="1",
                                         CONDA_SHLVL=os.environ.get("CONDA_SHLVL", "0"),
                                         **(env or {}))).returncode
        entry.update(state="succeeded" if rc == 0 else "failed", finished=now(), exitCode=rc)
        self.save()
        if rc:
            raise RuntimeError(f"windows step {name} failed (exit {rc}), see {log}")

    def windows(self):
        win = self.status["windows"]
        win["state"] = "running"
        self.save()
        py = sys.executable
        version = self.status["version"]
        wdir = self.dir / "windows"
        try:
            self.win_step("stage", [py, "tools/stage_content_embedder.py", SRC])
            if self.args.smoke:
                self.win_step("build-dry-run", [py, "tools/remote_build/build.py", "-C",
                                                OUT_REL, "-n", "zitter_content"])
                win["state"] = "succeeded"
                return
            self.win_step("build", [py, "tools/remote_build/build.py", "-C", OUT_REL,
                                    "zitter_content"])
            first = wdir / "package-unaudited"
            self.win_step("package-unaudited", [py, "tools/package_win.py", "--out", OUT,
                                                "--setup", OUT / "zitter_setup.exe",
                                                "--destination", first])
            audit = wdir / "license-audit"
            self.win_step("license-audit", [py, "tools/audit_licenses.py", "--app",
                                            next(first.glob("*-portable.zip")), "--out", OUT,
                                            "--destination", audit])
            licenses = audit / "THIRD_PARTY_LICENSES.txt"
            pkg = wdir / "package"
            self.win_step("package", [py, "tools/package_win.py", "--out", OUT, "--setup",
                                      OUT / "zitter_setup.exe", "--destination", pkg,
                                      "--third-party-licenses", licenses])
            shutil.rmtree(first, ignore_errors=True)
            portable = next(pkg.glob("*-portable.zip"))
            setup = next(pkg.glob("*-setup.exe"))
            checks = []
            if not self.args.no_verify:
                self.win_step("verify-package", [
                    py, "tools/verify_content_windows_bundle.py", OUT,
                    "--third-party-licenses", licenses, "--input-archive", portable])
                checks.append("The portable package passed the Windows package runtime suite "
                              "(verify_content_windows_bundle.py).")
            self.windows_release(version, portable, setup, licenses, checks)
            win["state"] = "succeeded"
        except Exception as e:
            win["state"] = "failed"
            win["error"] = str(e)
            self.log(f"windows: FAILED: {e}")
        finally:
            self.save()

    def windows_release(self, version, portable, setup, licenses, checks):
        names = windows_release_names(version)
        rel = self.dir / "release"
        rel.mkdir(exist_ok=True)
        assets = []
        for src, name in ((setup, names["setup"]), (portable, names["zip"])):
            dest = rel / name
            shutil.copy2(src, dest)
            assets.append({"name": name, "bytes": dest.stat().st_size, "sha256": sha256(dest)})
        (rel / names["sums"]).write_text(
            "".join(f"{a['sha256']}  {a['name']}\n" for a in assets), encoding="utf-8")
        args_gn = (OUT / "args.gn").read_text(encoding="utf-8")
        sig = sh(["powershell", "-NoProfile", "-Command",
                  "(Get-AuthenticodeSignature '{}').Status; (Get-AuthenticodeSignature '{}').Status"
                  .format(rel / names["setup"], OUT / "zitter.exe")]).stdout.split()
        record = {
            "version": version, "platform": "windows", "architecture": "x64",
            "sourceCommit": self.status["commit"], "sourceDirty": False,
            "chromiumRevision": self.status["chromiumRevision"],
            "gnArgs": args_gn.splitlines(),
            "gnArgsSha256": hashlib.sha256(args_gn.encode()).hexdigest(),
            "buildMethod": "Overnight build (tools/nightly/fire.py): Release build with Siso "
                           "over the local NativeLink cluster, hermetic Windows toolchain",
            "authenticode": {"installer": sig[0] if sig else "Unknown",
                             "browser": sig[1] if len(sig) > 1 else "Unknown"},
            "assets": assets,
            "thirdPartyNoticesSha256": sha256(licenses),
            "verification": checks,
            "nightlyRun": self.id,
        }
        (rel / names["build"]).write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")

    # ---- macOS

    def mac(self):
        mac = self.status["mac"]
        mac["state"] = "running"
        self.save()
        try:
            self.mac_checkout()
            self.mac_start()
            self.mac_wait()
            if mac["state"] == "succeeded":
                self.mac_collect()
        except Exception as e:
            mac["state"] = "failed"
            mac["error"] = str(e)
            self.log(f"mac: FAILED: {e}")
        finally:
            self.save()

    def mac_checkout(self):
        commit = self.status["commit"]
        self.log(f"mac: checking out {commit[:12]} in {MAC_WORKTREE}")
        with tempfile.TemporaryDirectory() as tmp:
            bundle = Path(tmp) / "zitter.bundle"
            r = sh(["git", "bundle", "create", str(bundle), "HEAD"], timeout=300)
            if r.returncode:
                raise RuntimeError(f"git bundle failed: {r.stderr}")
            ssh(MAC, f"mkdir -p {MAC_RUNS}/{self.id}")
            r = sh(["scp", "-q", "-o", "BatchMode=yes", str(bundle),
                    f"{MAC}:zitter-nightly/runs/{self.id}/zitter.bundle"], timeout=600)
            if r.returncode:
                raise RuntimeError(f"copying the source to the Mac failed: {r.stderr}")
        r = ssh(MAC, f"""
set -euo pipefail
repo={MAC_REPO}; wt={MAC_WORKTREE}; bundle={MAC_RUNS}/{self.id}/zitter.bundle
git -C $repo fetch -q $bundle +HEAD:refs/nightly/head
test "$(git -C $repo rev-parse refs/nightly/head)" = {commit}
if [ ! -e $wt/.git ]; then
  git -C $repo worktree add -q --detach $wt {commit}
else
  git -C $wt checkout -q --detach --force {commit}
  git -C $wt clean -q -fd
fi
[ -L $wt/.chromium ] || ln -s $repo/.chromium $wt/.chromium
if [ -f $repo/.zitter-signing.json ]; then cp $repo/.zitter-signing.json $wt/; fi
rm -f $bundle
git -C $wt rev-parse HEAD
""", timeout=300)
        if r.returncode or r.stdout.strip().splitlines()[-1:] != [commit]:
            raise RuntimeError(f"Mac checkout failed: {r.stdout}{r.stderr}")

    def mac_start(self):
        flags = []
        if self.args.no_notarize:
            flags.append("--no-notarize")
        if self.args.no_verify:
            flags.append("--no-verify")
        if self.args.smoke:
            flags.append("--smoke")
        argv = "".join(f"<string>{a}</string>" for a in
                       ["/usr/bin/python3", "MACWT/tools/nightly/mac_job.py",
                        self.id, self.status["version"], *flags])
        r = ssh(MAC, f"""
set -euo pipefail
wt=$(cd {MAC_WORKTREE} && pwd); runs=$(cd {MAC_RUNS} && pwd)
plist=$HOME/Library/LaunchAgents/{MAC_LABEL}.plist
cat > $plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>{MAC_LABEL}</string>
  <key>ProgramArguments</key><array>{argv.replace("MACWT", "$wt")}</array>
  <key>WorkingDirectory</key><string>$wt</string>
  <key>EnvironmentVariables</key><dict>
    <key>PATH</key><string>/usr/local/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>
  </dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><false/>
  <key>StandardOutPath</key><string>$runs/{self.id}/job.log</string>
  <key>StandardErrorPath</key><string>$runs/{self.id}/job.log</string>
</dict></plist>
EOF
launchctl bootout gui/$(id -u)/{MAC_LABEL} 2>/dev/null || true
launchctl bootstrap gui/$(id -u) $plist
""")
        if r.returncode:
            raise RuntimeError(f"starting the Mac job failed: {r.stdout}{r.stderr}")
        self.log("mac: job started")

    def mac_wait(self):
        mac = self.status["mac"]
        failures, deadline = 0, time.time() + 14 * 3600
        seen = set()
        while time.time() < deadline:
            time.sleep(60)
            r = ssh(MAC, f"cat {MAC_RUNS}/{self.id}/status.json 2>/dev/null; "
                         f"launchctl print gui/$(id -u)/{MAC_LABEL} 2>/dev/null "
                         "| grep -E '^[[:space:]]+state = ' | head -1", timeout=60)
            if r.returncode and not r.stdout:
                failures += 1
                if failures >= 30:
                    raise RuntimeError("the Mac stopped answering for 30 minutes")
                continue
            failures = 0
            text, _, launchd = r.stdout.rpartition("}")
            try:
                remote = json.loads(text + "}")
            except ValueError:
                if "running" not in launchd:
                    raise RuntimeError("the Mac job exited before writing its status; "
                                       f"see {MAC_RUNS}/{self.id}/job.log on the Mac")
                continue
            mac["steps"] = remote.get("steps", [])
            for s in mac["steps"]:
                key = (s["name"], s["state"])
                if key not in seen and s["state"] != "running":
                    seen.add(key)
                    self.log(f"mac: {s['name']} {s['state']}")
            if remote.get("state") in ("succeeded", "failed"):
                mac["state"] = remote["state"]
                if remote.get("error"):
                    mac["error"] = remote["error"]
                self.save()
                return
            if "running" not in launchd:
                raise RuntimeError("the Mac job stopped while its status says running; "
                                   f"see {MAC_RUNS}/{self.id}/job.log on the Mac")
            self.save()
        raise RuntimeError("the Mac job did not finish in 14 hours")

    def mac_collect(self):
        rel = self.dir / "release"
        rel.mkdir(exist_ok=True)
        r = sh(["scp", "-q", "-o", "BatchMode=yes",
                f"{MAC}:zitter-nightly/runs/{self.id}/release/*", str(rel)], timeout=1800)
        if r.returncode:
            raise RuntimeError(f"copying the Mac files failed: {r.stderr}")
        self.log("mac: files copied")

    # ---- whole run

    def execute(self):
        if os.name == "nt":  # keep the PC awake while this process runs
            ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | 0x00000001)
        self.log(f"run {self.id}: Zitter {self.status['version']} at {self.status['commit'][:12]}")
        mac_thread = None
        if not self.args.skip_mac:
            mac_thread = threading.Thread(target=self.mac, daemon=True)
            mac_thread.start()
        if not self.args.skip_windows:
            self.windows()
        if mac_thread:
            mac_thread.join()
        states = {self.status[p]["state"] for p in ("windows", "mac")} - {"skipped"}
        self.status["state"] = ("succeeded" if states == {"succeeded"} else
                                "failed" if "succeeded" not in states else "partial")
        self.status["finished"] = now()
        self.save()
        self.write_summary()
        self.log(f"run {self.id}: {self.status['state']}")

    def write_summary(self):
        s = self.status
        lines = [f"# Overnight build {s['runId']}", "",
                 f"Zitter {s['version']}, commit `{s['commit']}`, Chromium `{s['chromiumRevision'][:12]}`.",
                 f"Started {s['started']}, finished {s['finished']}: **{s['state']}**.", ""]
        for platform, title in (("windows", "Windows"), ("mac", "macOS")):
            p = s[platform]
            lines.append(f"## {title}: {p['state']}")
            if p.get("error"):
                lines.append(f"\nError: {p['error']}")
            lines.append("")
            for step in p["steps"]:
                lines.append(f"- {step['name']}: {step['state']} ({step.get('started')} → "
                             f"{step.get('finished')})")
            lines.append("")
        rel = self.dir / "release"
        if rel.is_dir():
            lines.append("## Files in release/")
            lines += [f"- `{f.name}` ({f.stat().st_size // 2**20} MB)"
                      for f in sorted(rel.iterdir())]
            lines.append("")
        lines.append("Nothing was published. Upload with `gh release` as in docs/local-releases.md.")
        (self.dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def latest_run():
    runs = sorted(p for p in RUNS.glob("*/status.json")) if RUNS.is_dir() else []
    return runs[-1].parent if runs else None


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--check", action="store_true", help="preflight only")
    p.add_argument("--status", action="store_true", help="show the last run")
    p.add_argument("--skip-mac", action="store_true")
    p.add_argument("--skip-windows", action="store_true")
    p.add_argument("--no-notarize", action="store_true",
                   help="macOS: ad hoc signed ZIP instead of the notarized DMG")
    p.add_argument("--no-verify", action="store_true", help="skip the package test suites")
    p.add_argument("--foreground", action="store_true", help="run in this console")
    p.add_argument("--smoke", action="store_true",
                   help="check the pipeline without building: macOS runs ./build.sh --plan, "
                        "Windows stages and runs a Siso dry run")
    p.add_argument("--run-id", help=argparse.SUPPRESS)
    args = p.parse_args()
    for stream in (sys.stdout, sys.stderr):  # a cp1250 console must not stop a run
        stream.reconfigure(errors="replace")

    if args.status:
        run = latest_run()
        if not run:
            print("no runs yet")
            return 0
        summary = run / "summary.md"
        print(summary.read_text(encoding="utf-8") if summary.exists()
              else (run / "status.json").read_text(encoding="utf-8"))
        return 0

    if args.run_id:  # the detached child
        status = json.loads((RUNS / args.run_id / "status.json").read_text(encoding="utf-8"))
        Run(args.run_id, status, args).execute()
        return 0

    pre = Preflight(args).run()
    if not args.skip_mac and not pre.errors and args.check:
        for problem in mac_session_probe(not args.no_notarize):
            pre.error(problem)
    ok = pre.report()
    if args.check or not ok:
        return 0 if ok else 1

    run_id = run_id_for(pre.facts["commit"])
    run = Run(run_id, pre.facts, args)
    if args.foreground:
        run.execute()
        return 0 if run.status["state"] == "succeeded" else 1
    child = [sys.executable, str(Path(__file__).resolve()), "--run-id", run_id]
    child += [f"--{k.replace('_', '-')}" for k in
              ("skip_mac", "skip_windows", "no_notarize", "no_verify", "smoke")
              if getattr(args, k)]
    # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, and out of the caller's job
    # object when it allows that, so closing the terminal does not stop the run.
    flags = 0x00000008 | 0x00000200 if os.name == "nt" else 0
    with open(run.dir / "process.log", "a", encoding="utf-8") as log:
        for extra in ((0x01000000, 0) if os.name == "nt" else (0,)):
            try:
                subprocess.Popen(child, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, creationflags=flags | extra,
                                 start_new_session=os.name != "nt")
                break
            except OSError:
                if not extra:
                    raise
    print(f"Started run {run_id} in the background.")
    print(f"  progress: python tools/nightly/fire.py --status   (log: build/nightly/{run_id}/fire.log)")
    print(f"  files:    build/nightly/{run_id}/release/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
