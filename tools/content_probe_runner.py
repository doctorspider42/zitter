"""Run independent Content probes with bounded concurrency."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys


def positive_jobs(value):
    jobs = int(value)
    if jobs < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return jobs


def run_probes(probes, *, cwd, env, jobs, timeout=None, keep_going=False, visible=False):
    tools = Path(__file__).resolve().parent

    def run_probe(probe):
        script, arguments = probe
        if visible:
            arguments = (*arguments, "--visible")
        command = [sys.executable, tools / script, *arguments]
        try:
            return subprocess.run(
                command, cwd=cwd, env=env, capture_output=True, text=True,
                timeout=timeout, check=True,
            ).stdout
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            name = subprocess.list2cmdline([script, *map(str, arguments)])
            failure = ValueError(f"{name} failed: {error}\n"
                                 f"{error.stdout or ''}\n{error.stderr or ''}")
            if keep_going:
                return failure
            raise failure from error

    def report(results):
        failed = 0
        for result in results:
            if isinstance(result, ValueError):
                print(result, file=sys.stderr)
                failed += 1
            else:
                print(result, end="", flush=True)
        if failed:
            raise ValueError(f"{failed} runtime probes failed")

    if jobs == 1:
        report(map(run_probe, probes))
    else:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            report(pool.map(run_probe, probes))
