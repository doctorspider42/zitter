#!/usr/bin/env python3
"""macOS half of an overnight release build (started by tools/nightly/fire.py).

Runs on the Mac as a LaunchAgent job in the logged-in user's session, where the
login Keychain is unlocked: Developer ID signing, notarytool and the Finder
layout of the DMG need that session, and an SSH session gets a locked Keychain.

  mac_job.py RUN_ID VERSION [--no-notarize] [--no-verify] [--jobs N]

It works in the checkout it lives in (fire.py prepares ~/Projects/zitter-nightly
at the requested commit) and writes ~/zitter-nightly/runs/RUN_ID/:
  status.json   state of every step, polled by fire.py over SSH
  *.log         one log per step
  release/      the files to publish: the notarized DMG, its SHA256SUMS and
                build record (or, with --no-notarize, an ad hoc signed ZIP)

Keep it Python 3.9 compatible: it runs with the Mac's /usr/bin/python3.
"""

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNS = Path.home() / "zitter-nightly" / "runs"


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class Job:
    def __init__(self, run_id, version):
        self.dir = RUNS / run_id
        self.release = self.dir / "release"
        self.release.mkdir(parents=True, exist_ok=True)
        self.status = {"runId": run_id, "version": version, "state": "running",
                       "started": now(), "finished": None, "steps": []}
        self.save()

    def save(self):
        tmp = self.dir / "status.json.tmp"
        tmp.write_text(json.dumps(self.status, indent=2) + "\n")
        tmp.replace(self.dir / "status.json")

    def step(self, name, cmd, env=None, ok_codes=(0,), retry_codes=(), retries=0):
        """Runs one step into NAME.log; retries while it exits with a retry code."""
        entry = {"name": name, "state": "running", "started": now(), "finished": None,
                 "log": f"{name}.log", "exitCode": None}
        self.status["steps"].append(entry)
        self.save()
        log = self.dir / f"{name}.log"
        attempt = 0
        with open(log, "a") as out:
            while True:
                attempt += 1
                out.write(f"\n$ {' '.join(str(c) for c in cmd)}  (attempt {attempt}, {now()})\n")
                out.flush()
                rc = subprocess.run([str(c) for c in cmd], cwd=ROOT, stdout=out,
                                    stderr=subprocess.STDOUT,
                                    env=dict(os.environ, **(env or {}))).returncode
                if rc in retry_codes and attempt <= retries:
                    time.sleep(60)
                    continue
                break
        entry.update(state="succeeded" if rc in ok_codes else "failed",
                     finished=now(), exitCode=rc)
        self.save()
        if rc not in ok_codes:
            raise SystemExit(f"{name} failed with exit code {rc}; see {log}")

    def finish(self, state, error=None):
        self.status.update(state=state, finished=now())
        if error:
            self.status["error"] = error
        self.save()


def other_build_running():
    mine = {os.getpid(), os.getppid()}
    out = subprocess.run(["pgrep", "-fl", r"ninja|build\.sh|siso"], capture_output=True,
                         text=True).stdout
    return [line for line in out.splitlines()
            if line.split()[0].isdigit() and int(line.split()[0]) not in mine]


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("run_id")
    p.add_argument("version")
    p.add_argument("--no-notarize", action="store_true")
    p.add_argument("--no-verify", action="store_true")
    p.add_argument("--jobs", type=int, default=10, help="Ninja jobs (ZITTER_BUILD_JOBS)")
    p.add_argument("--smoke", action="store_true",
                   help="check the pipeline: ./build.sh --plan instead of building")
    args = p.parse_args()

    job = Job(args.run_id, args.version)
    try:
        busy = other_build_running()
        if busy:
            raise SystemExit("another build is running on the Mac:\n" + "\n".join(busy))
        py = sys.executable
        version = args.version
        app = ROOT / "build/ZitterContentLab.app"
        licenses = ROOT / "build/license-audit/mac-content-local/THIRD_PARTY_LICENSES.txt"

        if args.smoke:
            job.step("plan", ["./build.sh", "--plan"])
            (job.release / "smoke.txt").write_text(f"smoke run {args.run_id}\n")
            job.finish("succeeded")
            return
        job.step("build", ["./build.sh"], env={"ZITTER_BUILD_JOBS": str(args.jobs)})

        if args.no_notarize:
            archive = job.release / f"Zitter-v{version}-macos-content-arm64-adhoc.zip"
            job.step("archive", ["ditto", "-c", "-k", "--keepParent", app, archive])
            job.finish("succeeded")
            return

        notarization = ROOT / f"build/notarization/nightly-{args.run_id}"
        dmg_dir = ROOT / f"build/releases/nightly-{args.run_id}-dmg"
        job.step("notarize-prepare", [
            py, "tools/notarize_app.py", "--app", app, "--destination", notarization,
            "--content-version", version, "--content-release", "--prepare-only",
            "--third-party-licenses", licenses])
        prepared = notarization / "work/Zitter.app"
        if not args.no_verify:
            job.step("verify-app", [py, "tools/verify_content_bundle.py", "--content-release",
                                    "--preserve-signature", prepared])
        # Exit code 2: Apple is still processing; rerunning resumes the submission.
        job.step("notarize-submit", [
            py, "tools/notarize_app.py", "--app", app, "--destination", notarization,
            "--content-release", "--submit-content-candidate",
            "--keychain-profile", "zitter-notary"], retry_codes=(2,), retries=9)
        job.step("dmg", [
            py, "tools/package_dmg.py", "--content-release", "--app", prepared,
            "--destination", dmg_dir, "--keychain-profile", "zitter-notary"],
            retry_codes=(2,), retries=9)
        dmg = dmg_dir / f"Zitter-v{version}-macos-content-arm64.dmg"
        if not args.no_verify:
            job.step("verify-dmg", [py, "tools/verify_content_bundle.py", "--content-release",
                                    "--preserve-signature", dmg])
        for path in [dmg, *dmg_dir.glob("SHA256SUMS-*-dmg.txt"), *dmg_dir.glob("*-dmg-build.json")]:
            shutil.copy2(path, job.release / path.name)
        job.finish("succeeded")
    except SystemExit as e:
        job.finish("failed", str(e))
        raise
    except Exception as e:  # keep status.json truthful for fire.py
        job.finish("failed", repr(e))
        raise


if __name__ == "__main__":
    main()
