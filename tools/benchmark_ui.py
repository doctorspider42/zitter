#!/usr/bin/env python3
"""Compare isolated Views, Rust/Skia and Rust/Metal sidebars in one app binary."""
import argparse
from datetime import datetime, timezone
import hashlib
import http.server
import itertools
import json
import math
import os
from pathlib import Path
import platform
import re
import signal
import statistics
import subprocess
import threading

ROOT = Path(__file__).resolve().parents[1]
BACKENDS = ("views", "rust-skia", "rust-metal")
METRICS = ("scene_ms", "submit_ms", "update_to_present_ms")


def records(output, marker):
    return [json.loads(match[1]) for match in re.finditer(re.escape(marker) + r" (\{[^\n]*\})", output)]


def parse_run(output, backend, iterations):
    status = records(output, "ZITTER_UI_STATUS")
    ready = records(output, "ZITTER_UI_READY")
    samples = records(output, "ZITTER_UI_SAMPLE")
    resources = records(output, "ZITTER_UI_RESOURCE")
    idle = records(output, "ZITTER_UI_IDLE")
    if status != [{"ok": True}] or ready != [{"backend": backend, "scene_parity": True,
                                               "page_surface": "native-webcontents"}]:
        raise ValueError("Missing successful status or matching scene/page readiness")
    if len(samples) != iterations or any(type(s["trial"]) is not int for s in samples) or [s["trial"] for s in samples] != list(range(iterations)):
        raise ValueError("Missing, duplicated or reordered UI samples")
    for sample in samples:
        for metric in METRICS:
            if type(sample[metric]) not in (int, float) or not math.isfinite(sample[metric]) or sample[metric] < 0:
                raise ValueError("Invalid UI timing")
        gpu = sample["metal_gpu_ms"]
        if type(gpu) not in (int, float) or not math.isfinite(gpu) or (gpu < 0 if backend == "rust-metal" else gpu != -1):
            raise ValueError("Invalid GPU timing")
        if type(sample["feedback_flags"]) is not int or sample["feedback_flags"] < 0 or sample["feedback_flags"] & 16:
            raise ValueError("Invalid presentation feedback")
    if len(resources) != 1:
        raise ValueError("Missing process resource accounting")
    resource = resources[0]
    for key in ("browser_cpu_ms", "wall_ms", "browser_footprint_bytes"):
        if type(resource[key]) not in (int, float) or not math.isfinite(resource[key]) or resource[key] <= 0:
            raise ValueError("Invalid process resources")
    if len(idle) != 1 or any(type(idle[0][key]) not in (int, float) or not math.isfinite(idle[0][key])
                             or idle[0][key] < 0 for key in ("browser_cpu_ms", "wall_ms")) or idle[0]["wall_ms"] < 1900:
        raise ValueError("Missing or invalid idle measurement")
    return {"samples": samples, "resources": resource, "idle": idle[0]}


def stats(values):
    ordered = sorted(values)
    return {"median": statistics.median(values), "p95": ordered[math.ceil(len(ordered) * .95) - 1]}


def aggregate(runs):
    result = {}
    for backend in BACKENDS:
        selected = [run for run in runs if run["backend"] == backend]
        if not selected:
            continue
        result[backend] = {}
        for metric in METRICS:
            # Each process is a repetition; retain its median to show run variance.
            medians = [statistics.median(s[metric] for s in run["samples"]) for run in selected]
            result[backend][metric] = {**stats([s[metric] for run in selected for s in run["samples"]]),
                                       "run_medians": medians}
        result[backend]["browser_cpu_ms_per_update"] = stats([
            run["resources"]["browser_cpu_ms"] / len(run["samples"]) for run in selected])
        result[backend]["browser_footprint_bytes"] = stats([
            run["resources"]["browser_footprint_bytes"] for run in selected])
        result[backend]["idle_browser_cpu_percent"] = stats([
            run["idle"]["browser_cpu_ms"] / run["idle"]["wall_ms"] * 100 for run in selected])
    return result


def launch(executable, directory, url, backend, scenario, iterations, log, interactive=False):
    directory.mkdir()
    (directory / ".zitter-ui-lab").write_text("isolated-ui-lab\n")
    command = [str(executable), "--user-data-dir=" + str(directory), "--no-first-run",
               "--no-default-browser-check", "--disable-background-networking", "--use-mock-keychain",
               "--enable-logging=stderr", "--disable-backgrounding-occluded-windows",
               "--zitter-ui-lab=" + backend, "--zitter-ui-scenario=" + scenario,
               "--zitter-performance-url=" + url, "--zitter-perf-iterations=" + str(iterations)]
    if interactive:
        command.append("--zitter-ui-interactive")
    with log.open("w") as stream, subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                                   start_new_session=True) as process:
        try:
            status = process.wait(timeout=None if interactive else max(90, iterations * .2))
        except BaseException:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise
    if interactive:
        if status or records(log.read_text(errors="replace"), "ZITTER_UI_STATUS") != [{"ok": True}]:
            raise ValueError(f"Interactive UI lab failed; see {log}")
        return None
    if status:
        raise ValueError(f"UI lab exited {status}; see {log}")
    try:
        return parse_run(log.read_text(errors="replace"), backend, iterations)
    except (ValueError, KeyError) as error:
        raise ValueError(f"{error}; see {log}") from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=120)
    parser.add_argument("--rounds", type=int, default=6)
    parser.add_argument("--scenario", choices=("selection", "resize", "both"), default="both")
    parser.add_argument("--interactive", choices=BACKENDS)
    parser.add_argument("--backends", nargs="+", choices=BACKENDS, default=list(BACKENDS))
    parser.add_argument("--results", type=Path, default=ROOT / "build/ui-performance")
    args = parser.parse_args()
    if not 1 <= args.iterations <= 1000 or not 1 <= args.rounds <= 30:
        parser.error("Use 1–1000 iterations and 1–30 rounds")
    app = args.app.resolve()
    marker = app / "Contents/Resources/zitter-development-bundle"
    if not marker.is_file():
        marker = app / "Contents/zitter-development-bundle"
    executable = app / "Contents/MacOS/Chromium"
    if not marker.is_file() or not executable.is_file():
        parser.error("Use an owned bundle from tools/package_app.py")
    out = Path(marker.read_text().strip())
    hasher = hashlib.sha256()
    with executable.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            hasher.update(chunk)
    digest = hasher.hexdigest()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    destination = args.results.resolve() / stamp
    destination.mkdir(parents=True)
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *values, **options):
            super().__init__(*values, directory=str(ROOT / "testdata"), **options)
        def log_message(self, *_):
            pass
    runs = []
    result = {"timestamp_utc": stamp, "platform": platform.platform(), "app": str(app),
              "cpu_model": subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip(),
              "physical_memory_bytes": int(subprocess.check_output(["sysctl", "-n", "hw.memsize"], text=True)),
              "executable_sha256": digest, "chromium_revision": json.loads((ROOT / "chromium/revision.json").read_text())["revision"],
              "gn_args": (out / "args.gn").read_text(), "iterations": args.iterations,
              "warmups_per_process": 10, "settling_seconds": 3, "rounds": args.rounds, "runs": runs,
              "scope": "Controlled opaque-rectangle sidebar with one visible native WebContents and a minimal shared Views window/page host. No production UI, text shaping, IME, accessibility, DnD, overlays or scrolling. Views retains primitive children; rust-skia paints a Rust display list through Chromium; rust-metal submits that list to a sibling CAMetalLayer, composed by macOS. No web-pixel readback. Sequential update-to-presentation feedback, not physical input-to-photon latency. Backend presentation APIs differ; feedback flags retained. CPU and footprint cover browser only, excluding GPU/renderer processes; no energy measurements. Samples in a process are correlated; compare run medians."}
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = f"http://127.0.0.1:{server.server_port}/performance_probe.html"
        try:
            scenarios = ("selection", "resize") if args.scenario == "both" else (args.scenario,)
            if args.interactive:
                print(f"Interactive {args.interactive}; close the lab window to finish. Logs: {destination}", flush=True)
                launch(executable, destination / "profile", url, args.interactive, scenarios[0],
                       args.iterations, destination / "interactive.log", interactive=True)
                return 0
            orders = list(itertools.permutations(dict.fromkeys(args.backends)))
            for scenario in scenarios:
                for repetition in range(args.rounds):
                    for backend in orders[repetition % len(orders)]:
                        name = f"{scenario}-{repetition}-{backend}"
                        print(f"Measuring {name}", flush=True)
                        measured = launch(executable, destination / (name + ".profile"), url,
                                          backend, scenario, args.iterations, destination / (name + ".log"))
                        runs.append({"backend": backend, "scenario": scenario, "round": repetition, **measured})
                        (destination / "partial.json").write_text(json.dumps(result, indent=2) + "\n")
            result["summary"] = {scenario: aggregate([r for r in runs if r["scenario"] == scenario])
                                 for scenario in scenarios}
            (destination / "results.json").write_text(json.dumps(result, indent=2) + "\n")
            for scenario, backends in result["summary"].items():
                for backend, metrics in backends.items():
                    print(f"{scenario} {backend}: present median {metrics['update_to_present_ms']['median']:.3f} ms, "
                          f"p95 {metrics['update_to_present_ms']['p95']:.3f} ms; "
                          f"submit {metrics['submit_ms']['median']:.3f} ms")
            print(f"Results: {destination / 'results.json'}")
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            print(str(error))
            return 1
        finally:
            server.shutdown()
            thread.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
