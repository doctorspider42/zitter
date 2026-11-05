#!/usr/bin/env python3
"""Matched before/after UI benchmarks, retaining raw trials and process pairing."""
import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TESTS = {"interaction": "PerformanceUiInteraction", "boundary": "PerformanceProductBoundary", "subtree": "PerformanceSidebarSubtree"}
BACKENDS = {"compositor": [], "metal": ["--zitter-rust-raster=metal"]}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def describe_binary(path):
    result = {"path": str(path), "sha256": digest(path)}
    source = path.parents[2] / ("zitter_" + path.name.removeprefix("zitter_prototype_"))
    if source.is_dir():
        files = [p for p in sorted(source.rglob("*")) if p.is_file() and not p.name.startswith(".zitter")]
        result["source"] = {"path": str(source),
                            "files": {str(p.relative_to(source)): digest(p) for p in files},
                            "newer_than_binary": [str(p.relative_to(source)) for p in files if p.stat().st_mtime > path.stat().st_mtime]}
    args = path.parent / "args.gn"
    if args.is_file():
        result["gn_args"] = args.read_text()
    return result


def summary(values):
    ordered = sorted(values)
    return {"n": len(values), "median": statistics.median(values),
            "p95": ordered[math.ceil(len(values) * .95) - 1], "mean": statistics.fmean(values)}


def paired_summary(before, after):
    # Process medians, not correlated iterations, are the resampling units.
    ratios = [(a / b - 1) * 100 for b, a in zip(before, after)]
    differences = [a - b for b, a in zip(before, after)]
    rng = random.Random(731)
    boot = sorted(statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(10000))
    observed = abs(statistics.fmean(differences))
    signs = list(itertools.product((-1, 1), repeat=len(differences)))
    p = sum(abs(statistics.fmean(d * s for d, s in zip(differences, sign))) >= observed
            for sign in signs) / len(signs)
    return {"paired_percent": ratios, "median_percent": statistics.median(ratios),
            "bootstrap_95_percent": [boot[249], boot[9749]], "sign_flip_p": p}


def run(binary, test, backend, iterations, tabs, pins, log):
    command = [str(binary), f"--gtest_filter=ZitterProfilesBrowserTest.{TESTS[test]}",
               "--zitter-benchmark", f"--zitter-perf-iterations={iterations}",
               f"--zitter-perf-tabs={tabs}", f"--zitter-perf-pins={pins}",
               "--test-launcher-jobs=1", "--test-launcher-retry-limit=0",
               "--disable-component-update", *BACKENDS[backend]]
    with log.open("w") as stream:
        status = subprocess.run(command, cwd=ROOT / ".chromium/src", stdout=stream,
                                stderr=subprocess.STDOUT, timeout=900).returncode
    text = log.read_text(errors="replace")
    if status:
        raise RuntimeError(f"Benchmark failed ({status}): {log}")
    samples = [json.loads(m) for m in re.findall(r"ZITTER_PERF (\{[^\n]*\})", text)]
    resources = [json.loads(m) for m in re.findall(r"ZITTER_UI_RESOURCE (\{[^\n]*\})", text)]
    grouped = {}
    for sample in samples:
        grouped.setdefault(sample["metric"], []).append(sample)
    expected = {"interaction": 9, "boundary": 7, "subtree": 8}[test]
    if len(grouped) != expected or len(resources) != 1:
        raise RuntimeError(f"Unexpected metric/resource count: {log}")
    for metric, values in grouped.items():
        if sorted(v["trial"] for v in values) != list(range(iterations)):
            raise RuntimeError(f"Missing/duplicated trials for {metric}: {log}")
        if any(not math.isfinite(v["ms"]) or v["ms"] < 0 for v in values):
            raise RuntimeError(f"Invalid timing: {log}")
    return {"command": command, "log": str(log), "samples": samples, "resources": resources[0],
            "operations": {k: summary([v["ms"] for v in values]) for k, values in grouped.items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--test", choices=TESTS, default="interaction")
    parser.add_argument("--iterations", type=int, default=40)
    parser.add_argument("--tabs", type=int, default=20)
    parser.add_argument("--pins", type=int, default=1000)
    parser.add_argument("--rounds", type=int, default=6)
    parser.add_argument("--backends", nargs="+", choices=BACKENDS, default=list(BACKENDS))
    parser.add_argument("--results", type=Path, default=ROOT / "build/product-boundary-performance")
    args = parser.parse_args()
    if not 1 <= args.rounds <= 8 or not 1 <= args.iterations <= 400:
        parser.error("Use 1–8 rounds and 1–400 iterations")
    folder = args.results / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    report = {"date": datetime.now(timezone.utc).isoformat(), "test": args.test,
              "iterations": args.iterations, "tabs": args.tabs, "pins": args.pins, "rounds": args.rounds,
              "binaries": {k: describe_binary(p) for k, p in binaries.items()},
              "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "runs": [], "limits": ["UI dispatch/drain latency, not monitor presentation or FPS.",
                "Independent fresh test processes with private fixtures; warm OS caches.",
                "Other desktop applications remain running; pairing balances order, not all ambient noise.",
                "Confidence intervals use paired process medians, not pooled correlated trials."]}
    report["system"] = {"os": subprocess.check_output(["sw_vers"], text=True),
                        "hardware": subprocess.check_output(["sysctl", "hw.model", "hw.ncpu", "hw.memsize"], text=True),
                        "thermal": subprocess.check_output(["pmset", "-g", "therm"], text=True)}
    (folder / "ambient-before.txt").write_text(subprocess.check_output(["ps", "-Ao", "pid,pcpu,comm"], text=True))
    print(folder, flush=True)
    for repetition in range(args.rounds):
        backends = args.backends if repetition % 2 == 0 else list(reversed(args.backends))
        for index, backend in enumerate(backends):
            order = ["before", "after"] if (repetition + index) % 2 == 0 else ["after", "before"]
            for variant in order:
                print(f"{args.test} round={repetition} {backend} {variant}", flush=True)
                result = run(binaries[variant], args.test, backend, args.iterations, args.tabs, args.pins,
                             folder / f"{repetition}-{backend}-{variant}.log")
                result.update(variant=variant, backend=backend, repetition=repetition)
                report["runs"].append(result)
                (folder / "raw.json").write_text(json.dumps(report, indent=2) + "\n")
    report["comparisons"] = {}
    for backend in args.backends:
        runs = {variant: sorted((r for r in report["runs"] if r["backend"] == backend and r["variant"] == variant),
                               key=lambda r: r["repetition"]) for variant in binaries}
        result = {}
        for metric in runs["before"][0]["operations"]:
            values = {v: [r["operations"][metric]["median"] for r in rows] for v, rows in runs.items()}
            result[metric] = {v: summary(ms) for v, ms in values.items()}
            result[metric]["change"] = paired_summary(values["before"], values["after"])
            result[metric]["pooled"] = {v: summary([s["ms"] for r in rows for s in r["samples"] if s["metric"] == metric])
                                         for v, rows in runs.items()}
        report["comparisons"][backend] = result
    (folder / "ambient-after.txt").write_text(subprocess.check_output(["ps", "-Ao", "pid,pcpu,comm"], text=True))
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(folder / "report.json", flush=True)


if __name__ == "__main__":
    main()
