#!/usr/bin/env python3
"""Sample an owned benchmark browser after warmup; never mix with A/B timings."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

from benchmark_product_boundary import BACKENDS, ROOT, TESTS, describe_binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--test", choices=TESTS, default="interaction")
    parser.add_argument("--backend", choices=BACKENDS, default="compositor")
    parser.add_argument("--pins", type=int, default=1000)
    parser.add_argument("--iterations", type=int, default=180)
    args = parser.parse_args()
    binary = args.binary.resolve()
    folder = ROOT / "build/product-boundary-profiles" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    command = [str(binary), f"--gtest_filter=ZitterProfilesBrowserTest.{TESTS[args.test]}",
               "--single-process-tests", "--zitter-benchmark", f"--zitter-perf-iterations={args.iterations}",
               "--zitter-perf-tabs=20", f"--zitter-perf-pins={args.pins}",
               "--disable-component-update", *BACKENDS[args.backend]]
    log = folder / "benchmark.log"
    print(folder, flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(command, cwd=ROOT / ".chromium/src", stdout=stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 180
            while "ZITTER_PERF " not in log.read_text(errors="replace"):
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"Benchmark did not reach its measured phase: {log}")
                time.sleep(.1)
            sample_command = ["/usr/bin/sample", str(process.pid), "8", "1", "-file", str(folder / "sample.txt")]
            with (folder / "sample-command.log").open("w") as output:
                sample_status = subprocess.run(sample_command, stdout=output, stderr=subprocess.STDOUT, timeout=90).returncode
            status = process.wait(timeout=600)
            if status or sample_status:
                raise RuntimeError(f"Benchmark/sample failed ({status}/{sample_status}): {folder}")
            (folder / "metadata.json").write_text(json.dumps({
                "binary": describe_binary(binary), "command": command, "sample_command": sample_command,
                "test": args.test, "backend": args.backend,
                "scope": "Wall-clock stack samples of the owned browser, after benchmark warmup. Renderer/GPU processes remain separate. Sampled timings are excluded from latency comparisons.",
            }, indent=2) + "\n")
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=30)


if __name__ == "__main__":
    main()
