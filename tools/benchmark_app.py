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
import re
import selectors
import signal
import subprocess
import tempfile
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


STARTUP_STAGES = (
    "main_entry", "content_main", "pre_profile_init", "pre_browser_start", "pre_main_loop",
    "workspace_begin", "shell_built", "widget_initialized", "shell_attached", "widget_shown",
    "workspace_files_read", "timeline_loaded", "workspace_restored", "workspace_ready",
)


def window_timing(output, started_ns, window_ns):
    marks = re.findall(r"^ZITTER_STARTUP ([a-z_]+) ([0-9]+)$", output, re.MULTILINE)
    if tuple(stage for stage, _ in marks) != STARTUP_STAGES:
        raise ValueError("Missing, duplicate, or out-of-order startup timing markers")
    timings = {"app_process_window_onscreen_ms": (window_ns - started_ns) / 1e6}
    previous_ns = started_ns
    for stage, micros in marks:
        timestamp_ns = int(micros) * 1000
        if timestamp_ns < previous_ns:
            raise ValueError(f"Invalid monotonic timestamp for {stage}")
        timings[f"startup_to_{stage}_ms"] = (timestamp_ns - started_ns) / 1e6
        timings[f"startup_stage_ending_at_{stage}_ms"] = (timestamp_ns - previous_ns) / 1e6
        previous_ns = timestamp_ns
    show_requested_ns = int(dict(marks)["shell_attached"]) * 1000
    if window_ns < show_requested_ns:
        raise ValueError("WindowServer observed a window before Zitter requested Show()")
    timings["startup_show_to_window_onscreen_ms"] = (window_ns - show_requested_ns) / 1e6
    return timings


def launch(executable, directory, url, scenario, iterations, tabs, log, observer=None):
    directory = directory.resolve()
    command = [str(executable), "--user-data-dir=" + str(directory), "--no-first-run",
               "--no-default-browser-check", "--disable-background-networking", "--use-mock-keychain",
               "--enable-logging=stderr", "--disable-backgrounding-occluded-windows",
               "--zitter-app-benchmark=" + scenario,
               "--zitter-performance-url=" + url, "--zitter-perf-iterations=" + str(iterations),
               "--zitter-perf-tabs=" + str(tabs)]
    if observer:
        command.append("--zitter-startup-timing")
    def now_ns():
        # Match Chromium TimeTicks and the Swift observer on macOS.
        return time.clock_gettime_ns(time.CLOCK_UPTIME_RAW) if observer else time.monotonic_ns()

    started_ns = now_ns()
    times = {}
    timed_out = threading.Event()
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          start_new_session=True) as process:
        observer_process = None
        def terminate_owned_group():
            timed_out.set()
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        watchdog = threading.Timer(max(300, (iterations + tabs + 2) * 10), terminate_owned_group)
        watchdog.start()
        try:
            if observer:
                observer_process = subprocess.Popen(
                    [str(observer), str(process.pid)], stdout=subprocess.PIPE, text=True)
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
                            elapsed = (now_ns() - started_ns) / 1e6
                            if b"Zitter ready;" in line and "app_process_ready_ms" not in times:
                                times.setdefault("app_process_ready_ms", elapsed)
                            if b"ZITTER_APP_FIRST_PAINT" in line:
                                times.setdefault("app_process_first_visual_paint_ms", elapsed)
                    # Helpers may retain stderr briefly after the browser exits.
                    # Drain available data without requiring all descendants' EOF.
                    if process.poll() is not None and (not events or not selector.get_map()):
                        break
            status = process.wait()
            if observer_process:
                observed, _ = observer_process.communicate(timeout=5)
                if observer_process.returncode:
                    raise ValueError(f"WindowServer timing failed; see {log}")
        except Exception:
            terminate_owned_group()
            raise
        finally:
            watchdog.cancel()
            if observer_process and observer_process.poll() is None:
                observer_process.kill()
                observer_process.communicate()
    output = log.read_text(errors="replace")
    if status or timed_out.is_set() or 'ZITTER_APP_STATUS {"ok":true}' not in output or 'ZITTER_APP_STATUS {"ok":false}' in output:
        raise ValueError(f"Application benchmark failed or incomplete; see {log}")
    if observer:
        try:
            times.update(window_timing(output, started_ns, int(observed)))
        except ValueError as error:
            raise ValueError(f"{error}; see {log}") from error
    return output, times


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--scenario", choices=("navigation", "startup"), default="navigation")
    parser.add_argument("--iterations", type=int, choices=range(1, 201), default=30)
    parser.add_argument("--tabs", type=int, choices=range(2, 201), default=25)
    parser.add_argument("--label", default="regular-app")
    parser.add_argument("--results", type=Path, default=ROOT / "build/performance")
    parser.add_argument("--window-timing", action="store_true",
                        help="measure startup stages and the first on-screen window on macOS")
    args = parser.parse_args()
    if args.window_timing and (platform.system() != "Darwin" or args.scenario != "startup"):
        parser.error("--window-timing requires macOS and --scenario startup")
    with tempfile.TemporaryDirectory(prefix="zitter-window-observer-") as temporary:
        observer = None
        if args.window_timing:
            observer = Path(temporary) / "observe"
            try:
                subprocess.run(["xcrun", "swiftc", str(ROOT / "tools/observe_startup_window.swift"),
                                "-o", str(observer)], check=True)
            except (OSError, subprocess.CalledProcessError) as error:
                parser.error(f"Could not build the WindowServer observer: {error}")
        return benchmark(args, parser, observer)


def benchmark(args, parser, observer):
    app = args.app.resolve()
    marker = app / "Contents/Resources/zitter-development-bundle"
    if not marker.is_file():
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
                samples = {}
                for trial in range(-2, args.iterations):
                    directory = profiles / str(trial + 2)
                    write_fixture(directory, url, args.tabs, True)
                    log = prefix.with_name(prefix.name + f"-trial-{trial + 2}").with_suffix(".log")
                    _, times = launch(executable, directory, url, args.scenario, 1, args.tabs, log, observer)
                    verify_restored_session(directory, args.tabs)
                    if not samples:
                        samples = {name: [] for name in times}
                    if (set(times) != set(samples) or
                            not {"app_process_ready_ms", "app_process_first_visual_paint_ms"} <= times.keys()):
                        raise ValueError(f"Missing ready/paint marker; see {log}")
                    if trial >= 0:
                        for name in samples:
                            samples[name].append(times[name])
                    window = (f", on-screen {times['app_process_window_onscreen_ms']:.2f} ms"
                              if observer else "")
                    print(f"Startup trial {trial}: ready {times['app_process_ready_ms']:.2f} ms, first paint {times['app_process_first_visual_paint_ms']:.2f} ms{window}", flush=True)
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
    if observer:
        result["window_timing"] = ("Monotonic timestamps from just before executable spawn; the observer "
                                   "polls WindowServer metadata every 5 ms plus query time. On-screen "
                                   "means window presence, not pixel presentation.")
    prefix.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    for name, value in metrics.items():
        print(f"{name}: median {value['median_ms']:.2f} ms, p95 {value['p95_ms']:.2f} ms")
    print(f"Results: {prefix.with_suffix('.json')}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
