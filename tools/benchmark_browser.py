#!/usr/bin/env python3
"""Run isolated warm-profile browser benchmarks and retain raw timing samples."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
METRICS = (
    "new_tab_dispatch_ms", "new_tab_first_visual_paint_ms",
    "split_dispatch_ms", "split_both_renderer_frames_ms",
)
SCENARIOS = {
    "navigation": ("PerformanceBaseline", METRICS),
    "restoration": ("PerformanceRestoration", ("restore_ready_ms", "restore_both_first_paints_ms")),
    "profiles": ("PerformanceProfileStartup", ("profiles_ready_ms", "profile_batch_ms", "active_profile_delay_ms")),
    "profile-page": ("PerformanceProfileStartup", ("profiles_ready_ms", "profiles_first_visual_paint_ms", "profile_batch_ms", "active_profile_delay_ms")),
    "preconnect": ("ZitterPreconnectBrowserTest.PerformancePreconnect", ("preconnect_first_visual_paint_ms", "preconnect_response_start_ms")),
}


def summarize(output, iterations, metric_names=METRICS):
    samples = {metric: {} for metric in metric_names}
    for line in output.splitlines():
        match = re.search(r"ZITTER_PERF (\{.*\})", line)
        if not match:
            continue
        value = json.loads(match[1])
        metric, trial, ms = value["metric"], value["trial"], value["ms"]
        if (metric not in samples or type(trial) is not int
                or not 0 <= trial < iterations or not math.isfinite(ms) or ms < 0):
            raise ValueError(f"Invalid benchmark sample: {value}")
        # The launcher can echo test output; only identical duplicates are safe.
        if trial in samples[metric] and samples[metric][trial] != ms:
            raise ValueError(f"Conflicting sample for {metric}, trial {trial}")
        samples[metric][trial] = ms
    result = {}
    for metric, trials in samples.items():
        if set(trials) != set(range(iterations)):
            raise ValueError(f"Incomplete {metric}: {len(trials)}/{iterations} trials")
        values = [trials[i] for i in range(iterations)]
        ordered = sorted(values)
        result[metric] = {
            "samples_ms": values, "median_ms": statistics.median(values),
            "p95_ms": ordered[math.ceil(0.95 * len(ordered)) - 1],
        }
    return result


def resources(output, iterations):
    samples = {}
    for line in output.splitlines():
        match = re.search(r"ZITTER_RESOURCE (\{.*\})", line)
        if not match:
            continue
        value = json.loads(match[1])
        trial = value["trial"]
        if (type(trial) is not int or not 0 <= trial < iterations
                or type(value["realized_pages"]) is not int
                or type(value["saved_tabs"]) is not int
                or not 0 <= value["realized_pages"] <= value["saved_tabs"]
                or type(value["browser_resident_bytes"]) is not int
                or value["browser_resident_bytes"] <= 0
                or type(value["browser_physical_footprint_bytes"]) is not int
                or value["browser_physical_footprint_bytes"] < 0):
            raise ValueError(f"Invalid resource sample: {value}")
        if trial in samples and samples[trial] != value:
            raise ValueError(f"Conflicting resource sample: {trial}")
        samples[trial] = value
    if set(samples) != set(range(iterations)):
        raise ValueError("Incomplete restoration resource samples")
    return [samples[i] for i in range(iterations)]


def process_tree_sample(output, root_pid):
    """Aggregate ps KiB RSS for this benchmark's current process tree only."""
    processes = {}
    for line in output.splitlines():
        columns = line.split(None, 3)
        if len(columns) != 4:
            raise ValueError("Invalid process-memory row")
        pid, parent, rss = map(int, columns[:3])
        if pid <= 0 or parent < 0 or rss < 0 or pid in processes:
            raise ValueError("Invalid process-memory value")
        processes[pid] = (parent, rss, "--type=renderer" in columns[3].split())
    owned = {root_pid} if root_pid in processes else set()
    while True:
        expanded = owned | {pid for pid, (parent, _, _) in processes.items() if parent in owned}
        if expanded == owned:
            break
        owned = expanded
    return {
        "process_count": len(owned),
        "renderer_count": sum(processes[pid][2] for pid in owned),
        "total_rss_bytes": sum(processes[pid][1] * 1024 for pid in owned),
        "renderer_rss_bytes": sum(processes[pid][1] * 1024 for pid in owned if processes[pid][2]),
    }


def renderer_samples(output, iterations, required=False):
    samples = {}
    for line in output.splitlines():
        match = re.search(r"ZITTER_RENDERER (\{.*\})", line)
        if not match:
            continue
        value = json.loads(match[1])
        trial = value["trial"]
        if (type(trial) is not int or not 0 <= trial < iterations
                or type(value["spares_before"]) is not int
                or type(value["ready_matching_spares_before"]) is not int
                or not 0 <= value["ready_matching_spares_before"] <= value["spares_before"]
                or type(value["used_spare"]) is not bool):
            raise ValueError("Invalid renderer diagnostic")
        if trial in samples and samples[trial] != value:
            raise ValueError("Conflicting renderer diagnostic")
        samples[trial] = value
    if (required or samples) and set(samples) != set(range(iterations)):
        raise ValueError("Incomplete renderer diagnostics; rebuild the benchmark binary")
    return [samples[i] for i in sorted(samples)]


def connection_samples(output, iterations, enabled):
    samples = {}
    for line in output.splitlines():
        match = re.search(r"ZITTER_CONNECTION (\{.*\})", line)
        if not match:
            continue
        value = json.loads(match[1])
        keys = ("trial", "connections_before_navigation", "accepted_connections", "http_requests",
                "http_requests_before_navigation", "simulated_rtt_ms", "typing_pause_ms")
        if (any(type(value[k]) is not int for k in keys)
                or not 0 <= value["trial"] < iterations
                or value["http_requests_before_navigation"] != 0
                or value["http_requests"] != 1 or value["accepted_connections"] != 1
                or value["connections_before_navigation"] != int(enabled)
                or value["simulated_rtt_ms"] != 120 or value["typing_pause_ms"] != 600):
            raise ValueError("Invalid connection experiment; check isolation, reuse and hint execution")
        trial = value["trial"]
        if trial in samples and samples[trial] != value:
            raise ValueError("Conflicting connection diagnostic")
        samples[trial] = value
    if set(samples) != set(range(iterations)):
        raise ValueError("Incomplete connection diagnostics")
    return [samples[i] for i in sorted(samples)]


def run_with_memory(command, cwd, stream):
    samples, error = [], None
    started = time.monotonic()
    with subprocess.Popen(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT) as process:
        while process.poll() is None:
            try:
                output = subprocess.check_output(
                    ["ps", "-axo", "pid=,ppid=,rss=,command="], text=True, timeout=5)
                sample = process_tree_sample(output, process.pid)
                if sample["process_count"]:
                    sample["elapsed_ms"] = (time.monotonic() - started) * 1000
                    samples.append(sample)
            except (OSError, subprocess.SubprocessError, ValueError) as failure:
                error = type(failure).__name__
                break
            try:
                process.wait(timeout=0.25)
            except subprocess.TimeoutExpired:
                pass
        status = process.wait()
    return status, {
        "complete": error is None and bool(samples), "error": error,
        "sampling_interval_ms": 250, "samples": samples,
        "scope": "Current benchmark launcher and descendants via ps. RSS sums can double-count shared pages; not private footprint or allocated-memory totals. Sampling adds overhead; compare only matching sampled runs.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / ".chromium/src/out/ZitterRelease")
    parser.add_argument("--iterations", type=int, default=30, choices=range(1, 201), metavar="1..200")
    parser.add_argument("--tabs", type=int, default=25, choices=range(2, 201), metavar="2..200")
    parser.add_argument("--profiles", type=int, default=4, choices=range(2, 17), metavar="2..16")
    parser.add_argument("--label", default="baseline")
    parser.add_argument("--scenario", choices=SCENARIOS, default="navigation")
    parser.add_argument("--spare-renderers", type=int, choices=range(1, 5), default=None,
                        help="Experiment: override Chromium MultipleSpareRPHs count (1..4)")
    parser.add_argument("--sample-process-memory", action="store_true",
                        help="Sample benchmark process-tree RSS every 250 ms (adds measurement overhead)")
    parser.add_argument("--preconnect", action="store_true",
                        help="Enable the experimental hint in --scenario preconnect")
    parser.add_argument("--binary", default="zitter_prototype",
                        help="Test binary name within --out (e.g. zitter_prototype_candidate)")
    parser.add_argument("--results", type=Path, default=ROOT / "build/performance")
    args = parser.parse_args()
    if args.preconnect and args.scenario != "preconnect":
        parser.error("--preconnect requires --scenario preconnect")
    test_name, metric_names = SCENARIOS[args.scenario]
    qualified_test = test_name if "." in test_name else "ZitterProfilesBrowserTest." + test_name
    profile_scenario = args.scenario in {"profiles", "profile-page"}
    out = args.out.resolve()
    executable = out / (args.binary + (".exe" if sys.platform == "win32" else ""))
    if not executable.is_file():
        parser.error(f"Build zitter_prototype first in {out}")
    gn_args = (out / "args.gn").read_text()
    digest = hashlib.sha256()
    with executable.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    args.results.mkdir(parents=True, exist_ok=True)
    prefix = args.results / stamp
    log = prefix.with_suffix(".log")
    command = [str(executable),
               f"--gtest_filter={qualified_test}",
               "--zitter-benchmark", f"--zitter-perf-iterations={args.iterations}",
               f"--zitter-perf-tabs={args.tabs}", "--test-launcher-jobs=1",
               f"--zitter-perf-profiles={args.profiles}", "--test-launcher-retry-limit=0",
               f"--test-launcher-timeout={max(300000, (args.iterations + 2) * 20000)}",
               "--enable-pixel-output-in-tests", "--enable-logging=stderr"]
    if args.scenario == "profile-page":
        command.append("--zitter-perf-profile-page")
    if args.preconnect:
        command.append("--zitter-perf-preconnect")
    browser_arguments = []
    if args.spare_renderers is not None:
        browser_arguments.append(f"--enable-features=MultipleSpareRPHs:count/{args.spare_renderers}")
    command.extend(browser_arguments)
    dataset = "one tab, local TLS, 120 ms modeled RTT" if args.scenario == "preconnect" else f"{args.profiles} fresh profiles" if profile_scenario else f"{args.tabs} tabs per workspace" if args.scenario == "restoration" else f"{args.tabs} tabs"
    print(f"Running {args.iterations} {args.scenario} trials with {dataset}; log: {log}", flush=True)
    memory_report = None
    with log.open("w") as stream:
        if args.sample_process_memory:
            status, memory_report = run_with_memory(command, ROOT / ".chromium/src", stream)
        else:
            status = subprocess.run(command, cwd=ROOT / ".chromium/src",
                                    stdout=stream, stderr=subprocess.STDOUT).returncode
    if status:
        print(f"Benchmark failed ({status}); see {log}", file=sys.stderr)
        return 1
    try:
        output = log.read_text()
        metrics = summarize(output, args.iterations, metric_names)
        resource_samples = resources(output, args.iterations) if args.scenario in {"restoration", "profiles", "profile-page"} else []
        renderer_diagnostics = renderer_samples(output, args.iterations,
                                               required=args.spare_renderers is not None) if args.scenario == "navigation" else []
        connections = connection_samples(output, args.iterations, args.preconnect) if args.scenario == "preconnect" else []
    except (ValueError, KeyError, TypeError) as error:
        print(f"Benchmark rejected: {error}; see {log}", file=sys.stderr)
        return 1
    if args.scenario == "preconnect":
        scope = "Local HTTPS with 120 ms socket-level RTT below TLS; one warm tab, fresh server origin per trial, fixed 600 ms typing pause in both variants. DNS/TCP accept are local. First visual paint is a renderer signal; responseStart uses Navigation Timing. Not representative internet latency."
    elif profile_scenario:
        scope = ("Fresh profiles with one restored local page; first visual paint is a renderer notification. "
                 if args.scenario == "profile-page" else "Fresh empty profiles. ")
        scope += "One browser-test process with warm UI globals; new temporary profile directories per trial. Not cold process startup."
    else:
        scope = "Warm isolated profiles; local HTTP page. "
        scope += "Window creation with fixed session/history fixtures. " if args.scenario == "restoration" else "New-tab creation and an already loaded split. "
        scope += "Not cold application/profile startup. Renderer paint/submission signals, not physical display latency."
    result = {
        "label": args.label, "timestamp_utc": stamp, "platform": platform.platform(),
        "build_directory": str(out), "gn_args": gn_args,
        "executable_sha256": digest.hexdigest(),
        "browser_arguments": browser_arguments,
        "iterations": args.iterations, "tabs": 1 if args.scenario == "preconnect" else args.tabs if not profile_scenario else None, "warmup_trials": 2,
        "scenario": args.scenario,
        "profile_count": args.profiles if profile_scenario else 2,
        "scope": scope,
        "metrics": metrics,
        "resource_samples": resource_samples,
        "process_memory": memory_report,
        "renderer_samples": renderer_diagnostics,
        "connection_samples": connections,
        "preconnect_enabled": args.preconnect,
    }
    destination = prefix.with_suffix(".json")
    destination.write_text(json.dumps(result, indent=2) + "\n")
    for metric, values in metrics.items():
        print(f"{metric}: median {values['median_ms']:.2f} ms, p95 {values['p95_ms']:.2f} ms")
    print(f"Results: {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
