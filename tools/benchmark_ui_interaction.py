#!/usr/bin/env python3
"""Compare Views and Rust/Metal on the real app's UI interaction workload.

Runs ZitterProfilesBrowserTest.PerformanceUiInteraction in alternating rounds
per backend and reports per-operation wall-time distributions plus browser
process CPU and memory. Timings end when posted UI work has drained; they do
not include WindowServer presentation, which needs a separate capture.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TEST = "ZitterProfilesBrowserTest.PerformanceUiInteraction"
BACKENDS = {"views": [], "rust-metal": ["--zitter-sidebar=rust-metal"]}


def run(binary, backend, iterations, tabs, log):
    command = [str(binary), f"--gtest_filter={TEST}", "--zitter-benchmark", f"--zitter-perf-iterations={iterations}",
               f"--zitter-perf-tabs={tabs}", "--test-launcher-jobs=1", "--test-launcher-retry-limit=0",
               "--disable-component-update", *BACKENDS[backend]]
    with log.open("w") as stream:
        status = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=900).returncode
    text = log.read_text(errors="replace")
    if status:
        raise SystemExit(f"{backend} run failed ({status}); see {log}")
    samples = [json.loads(m) for m in re.findall(r"ZITTER_PERF (\{[^\n]*\})", text)]
    resources = [json.loads(m) for m in re.findall(r"ZITTER_UI_RESOURCE (\{[^\n]*\})", text)]
    if len(resources) != 1 or len(samples) != 9 * iterations:
        raise SystemExit(f"Unexpected sample count in {log}")
    return samples, resources[0]


def summary(values):
    ordered = sorted(values)
    return {"n": len(ordered), "median": statistics.median(ordered), "p95": ordered[max(0, int(len(ordered) * 0.95) - 1)],
            "mean": statistics.fmean(ordered)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / ".chromium/src/out/ZitterOptimized/zitter_prototype_ui_lab")
    parser.add_argument("--iterations", type=int, default=40)
    parser.add_argument("--tabs", type=int, default=20)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--results", type=Path, default=ROOT / "build/ui-interaction-performance")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    folder = args.results / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    folder.mkdir(parents=True)
    data = {backend: {"samples": {}, "resources": []} for backend in BACKENDS}
    for round_index in range(args.rounds):
        order = list(BACKENDS) if round_index % 2 == 0 else list(reversed(BACKENDS))
        for backend in order:
            samples, resource = run(args.binary, backend, args.iterations, args.tabs, folder / f"{backend}-{round_index}.log")
            for sample in samples:
                data[backend]["samples"].setdefault(sample["metric"], []).append(sample["ms"])
            data[backend]["resources"].append(resource)
    report = {"date": datetime.now(timezone.utc).isoformat(), "test": TEST, "binary": str(args.binary),
              "iterations": args.iterations, "tabs": args.tabs, "rounds": args.rounds, "logs": str(folder),
              "limits": ["Wall time ends when posted UI work has drained; WindowServer presentation is not captured.",
                         "Single machine, warm process; no GPU counters."], "backends": {}}
    for backend, values in data.items():
        report["backends"][backend] = {
            "operations": {metric: summary(samples) for metric, samples in sorted(values["samples"].items())},
            "browser_cpu_ms_per_iteration": summary([r["browser_cpu_ms"] / r["iterations"] for r in values["resources"]]),
            "browser_footprint_mb": summary([r["browser_footprint_bytes"] / 2**20 for r in values["resources"]]),
        }
    output = json.dumps(report, indent=2)
    (folder / "report.json").write_text(output)
    if args.report:
        args.report.write_text(output)
    print(output)


if __name__ == "__main__":
    main()
