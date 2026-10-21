#!/usr/bin/env python3
"""Capture one hardware GPU trace per Rust/Skia variant, without capture markers.

Run after benchmarks, with no simultaneous build. Requires macOS Instruments.
This is a localization experiment, not an end-to-end presentation ranking.
"""
import argparse
from datetime import datetime, timezone
import functools
import hashlib
import http.server
import json
from pathlib import Path
import subprocess
import signal
import threading
import time
from benchmark_ui import ROOT, launch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    args = parser.parse_args()
    app = args.app.resolve()
    if not (app / 'Contents/Resources/zitter-development-bundle').is_file():
        parser.error('Use an owned app bundle')
    dest = ROOT / 'build/ui-gpu-optimized' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    dest.mkdir(parents=True)
    hasher = hashlib.sha256()
    with (app / 'Contents/MacOS/Chromium').open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            hasher.update(chunk)
    metadata = {'app': str(app), 'executable_sha256': hasher.hexdigest(), 'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'variants': {}, 'iterations': 120, 'backend': 'rust-skia', 'scenario': 'selection',
                'scope': 'Unmarked sidebar selection, no ScreenCaptureKit observer. Metal System Trace all processes, analysis must filter app bundle processes. One process per variant; GPU work localization, not statistical latency ranking.'}
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(ROOT / 'testdata'))
    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler) as server:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            for variant in ('baseline', 'optimized'):
                directory = dest / variant
                directory.mkdir()
                print(variant, directory, flush=True)
                with (directory / 'record.log').open('w') as log:
                    recorder = subprocess.Popen(['xcrun', 'xctrace', 'record', '--template', 'Metal System Trace',
                                                 '--all-processes', '--time-limit', '90s', '--no-prompt',
                                                 '--output', str(directory / 'gpu-skia.trace')], stdout=log, stderr=subprocess.STDOUT)
                    try:
                        time.sleep(3)
                        run = launch(app / 'Contents/MacOS/Chromium', directory / 'profile',
                                     f'http://127.0.0.1:{server.server_port}/performance_probe.html',
                                     'rust-skia', 'selection', 120, directory / 'gpu-skia.log',
                                     extra_args=['--zitter-ui-variant=' + variant])
                        # Stop only after the owned app has finished its active and idle phases.
                        # A fixed short recording can otherwise truncate a slow-starting process.
                        recorder.send_signal(signal.SIGINT)
                        status = recorder.wait(timeout=240)
                        if status:
                            raise RuntimeError('Instruments capture failed: ' + str(status))
                    finally:
                        if recorder.poll() is None:
                            recorder.terminate()
                            recorder.wait(timeout=30)
                for option, filename in [('toc', 'gpu-toc.xml'), ('intervals', 'gpu-intervals.xml')]:
                    command = ['xcrun', 'xctrace', 'export', '--input', str(directory / 'gpu-skia.trace'),
                               '--output', str(directory / filename)]
                    command += ['--toc'] if option == 'toc' else ['--xpath', '/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-intervals"]']
                    subprocess.run(command, check=True)
                subprocess.run(['python3', str(ROOT / 'tools/analyze_ui_instruments.py'), str(directory),
                                '--kind', 'gpu', '--iterations', '120', '--bundle', app.name], check=True)
                run['gpu'] = json.loads((directory / 'gpu-analysis.json').read_text())
                metadata['variants'][variant] = run
                (dest / 'results.json').write_text(json.dumps(metadata, indent=2) + '\n')
        finally:
            server.shutdown()
    print(dest, flush=True)


if __name__ == '__main__':
    main()
