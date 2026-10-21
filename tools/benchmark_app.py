#!/usr/bin/env python3
"""Measure an owned regular Zitter .app with isolated profiles and normal helpers."""
import argparse
from datetime import datetime, timezone
import hashlib
import http.server
import json
import os
from pathlib import Path
import platform
import selectors
import signal
import subprocess
import threading
import time

from benchmark_browser import summarize

ROOT = Path(__file__).resolve().parents[1]
APP_METRICS = ("app_new_tab_dispatch_ms", "app_new_tab_first_visual_paint_ms",
               "app_split_dispatch_ms", "app_split_visual_state_ms")


def write_fixture(directory, url, tabs, startup):
    profiles = [{"id": name + "-profile", "name": name.title(), "directory": folder}
                for name, folder in (("work", "ZitterWork"), ("personal", "ZitterPersonal"))]
    catalogue = {"profiles": profiles, "workspaces": [
        {"id": name, "name": name.title(), "profile_id": name + "-profile"}
        for name in ("work", "personal")], "active": "personal" if startup else "work",
        "onboarding_complete": True}
    session = {name: {"profile_id": name + "-profile", "active": 0, "left": 0, "right": -1,
                      "ratio": 0.5, "tabs": [{"url": url if startup else "", "title": "Saved page"}
                                              for _ in range(tabs if startup else 1)]}
               for name in ("work", "personal")}
    directory.mkdir()
    for name, value in (("ZitterWorkspaces.json", catalogue), ("ZitterSession.json", session),
                        ("ZitterTimeMachine.json", {})):
        (directory / name).write_text(json.dumps(value))


def verify_restored_session(directory, tabs):
    """Check the session written by normal shutdown, outside the timed region."""
    catalogue = json.loads((directory / "ZitterWorkspaces.json").read_text())
    session = json.loads((directory / "ZitterSession.json").read_text())
    if (catalogue.get("active") != "personal" or set(session) != {"work", "personal"}
            or any(len(session[name].get("tabs", [])) != tabs
                   or session[name].get("profile_id") != name + "-profile"
                   for name in ("work", "personal"))):
        raise ValueError(f"Restored session workload or profile assignment changed: {directory}")


def launch(executable, directory, url, scenario, iterations, tabs, log):
    directory = directory.resolve()
    command = [str(executable), "--user-data-dir=" + str(directory), "--no-first-run",
               "--no-default-browser-check", "--disable-background-networking", "--use-mock-keychain",
               "--enable-logging=stderr", "--disable-backgrounding-occluded-windows",
               "--zitter-app-benchmark=" + scenario,
               "--zitter-performance-url=" + url, "--zitter-perf-iterations=" + str(iterations),
               "--zitter-perf-tabs=" + str(tabs)]
    started = time.monotonic()
    times = {}
    timed_out = threading.Event()
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          start_new_session=True) as process:
        def terminate_owned_group():
            timed_out.set()
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        watchdog = threading.Timer(max(300, (iterations + tabs + 2) * 10), terminate_owned_group)
        watchdog.start()
        try:
            with log.open("wb") as stream, selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                pending = b""
                while True:
                    events = selector.select(timeout=0.2)
                    for key, _ in events:
                        data = os.read(key.fd, 65536)
                        if not data:
                            selector.unregister(key.fileobj)
                            continue
                        stream.write(data)
                        stream.flush()
                        pending += data
                        while b"\n" in pending:
                            line, pending = pending.split(b"\n", 1)
                            elapsed = (time.monotonic() - started) * 1000
                            if b"Zitter ready;" in line and "app_process_ready_ms" not in times:
                                times.setdefault("app_process_ready_ms", elapsed)
                            if b"ZITTER_APP_FIRST_PAINT" in line:
                                times.setdefault("app_process_first_visual_paint_ms", elapsed)
                    # Helpers may retain stderr briefly after the browser exits.
                    # Drain available data without requiring all descendants' EOF.
                    if process.poll() is not None and (not events or not selector.get_map()):
                        break
            status = process.wait()
        except Exception:
            terminate_owned_group()
            raise
        finally:
            watchdog.cancel()
    output = log.read_text(errors="replace")
    if status or timed_out.is_set() or 'ZITTER_APP_STATUS {"ok":true}' not in output or 'ZITTER_APP_STATUS {"ok":false}' in output:
        raise ValueError(f"Application benchmark failed or incomplete; see {log}")
    return output, times


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--scenario", choices=("navigation", "startup"), default="navigation")
    parser.add_argument("--iterations", type=int, choices=range(1, 201), default=30)
    parser.add_argument("--tabs", type=int, choices=range(2, 201), default=25)
    parser.add_argument("--label", default="regular-app")
    parser.add_argument("--results", type=Path, default=ROOT / "build/performance")
    args = parser.parse_args()
    app = args.app.resolve()
    marker = app / "Contents/zitter-development-bundle"
    if not marker.is_file():
        parser.error("Use a marker-owned bundle produced by tools/package_app.py")
    executable = app / "Contents/MacOS/Chromium"
    if not executable.is_file():
        parser.error("Application executable is missing")
    out = Path(marker.read_text().strip())
    digest = hashlib.sha256()
    with executable.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    args.results.mkdir(parents=True, exist_ok=True)
    prefix = args.results / stamp
    # Preserve isolated fixture directories alongside logs for failure diagnosis.
    profiles = prefix.with_suffix(".profiles")
    profiles.mkdir()
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *values, **options):
            super().__init__(*values, directory=str(ROOT / "testdata"), **options)
        def log_message(self, *_):
            pass
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = f"http://127.0.0.1:{server.server_port}/performance_probe.html"
        try:
            if args.scenario == "navigation":
                directory = profiles / "navigation"
                write_fixture(directory, url, args.tabs, False)
                log = prefix.with_suffix(".log")
                print(f"Measuring regular app navigation; log: {log}", flush=True)
                output, times = launch(executable, directory, url, args.scenario, args.iterations, args.tabs, log)
                metrics = summarize(output, args.iterations, APP_METRICS)
            else:
                samples = {name: [] for name in ("app_process_ready_ms", "app_process_first_visual_paint_ms")}
                for trial in range(-2, args.iterations):
                    directory = profiles / str(trial + 2)
                    write_fixture(directory, url, args.tabs, True)
                    log = prefix.with_name(prefix.name + f"-trial-{trial + 2}").with_suffix(".log")
                    _, times = launch(executable, directory, url, args.scenario, 1, args.tabs, log)
                    verify_restored_session(directory, args.tabs)
                    if set(times) != set(samples):
                        raise ValueError(f"Missing ready/paint marker; see {log}")
                    if trial >= 0:
                        for name in samples:
                            samples[name].append(times[name])
                    print(f"Startup trial {trial}: ready {times['app_process_ready_ms']:.2f} ms, first paint {times['app_process_first_visual_paint_ms']:.2f} ms", flush=True)
                lines = "\n".join("ZITTER_PERF " + json.dumps({"metric": name, "trial": i, "ms": value})
                                  for name, values in samples.items() for i, value in enumerate(values))
                metrics = summarize(lines, args.iterations, tuple(samples))
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            print(str(error))
            return 1
        finally:
            server.shutdown()
            thread.join()
    result = {"label": args.label, "timestamp_utc": stamp, "platform": platform.platform(),
              "app": str(app), "executable_sha256": digest.hexdigest(), "gn_args": (out / "args.gn").read_text(),
              "scenario": args.scenario, "iterations": args.iterations, "tabs": args.tabs,
              "warmup_trials": 2, "metrics": metrics,
              "rendering_control": "--disable-backgrounding-occluded-windows, matching Chromium browser tests; compositor output is measured without OS occlusion throttling",
              "scope": "Regular packaged app with normal renderer helpers, isolated profiles, local HTTP. Navigation samples have warm profiles; startup samples use fresh processes/profiles with warm OS caches and restored tabs in two profiles (active last). Ready/first-paint startup times are observed via stderr receipt; renderer visual-state callbacks acknowledge submissions, not physical presentation. Not a browser-test executable."}
    prefix.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    for name, value in metrics.items():
        print(f"{name}: median {value['median_ms']:.2f} ms, p95 {value['p95_ms']:.2f} ms")
    print(f"Results: {prefix.with_suffix('.json')}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
