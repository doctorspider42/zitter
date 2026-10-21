#!/usr/bin/env python3
"""Capture isolated UI presentation stages and a cross-process Perfetto trace."""
import argparse
from datetime import datetime, timezone
import functools
import http.server
import json
from pathlib import Path
import hashlib
import subprocess
import threading
from benchmark_ui import BACKENDS, ROOT, launch, records, stats


def summarize_frames(frames, iterations):
    frames = [f for f in frames if f['trial'] >= 0]
    if sorted(f['trial'] for f in frames) != list(range(iterations)):
        raise ValueError('Missing or duplicated compositor timing details')
    stages = [('start_us', 'received_us'), ('received_us', 'draw_start_us'),
              ('draw_start_us', 'swap_start_us'), ('swap_start_us', 'swap_end_us'),
              ('swap_end_us', 'presentation_us')]
    result = {}
    for a, b in stages:
        values = [(f[b] - f[a]) / 1000 for f in frames]
        if any(f[a] <= 0 or f[b] < f[a] for f in frames):
            raise ValueError(f'Invalid or nonmonotonic timing stage: {a} -> {b}')
        result[a.removesuffix('_us') + '_to_' + b.removesuffix('_us') + '_ms'] = stats(values)
    result['presentation_to_callback_ms'] = stats([(f['callback_us'] - f['presentation_us']) / 1000 for f in frames])
    result['vsync_interval_ms'] = stats([f['interval_us'] / 1000 for f in frames])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--iterations', type=int, default=120)
    parser.add_argument('--rounds', type=int, default=1)
    parser.add_argument('--trace', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.iterations <= 1000 or not 1 <= args.rounds <= 6:
        parser.error('Use 1–1000 iterations and 1–6 rounds')
    app = args.app.resolve()
    if not (app / 'Contents/Resources/zitter-development-bundle').is_file():
        parser.error('Use a bundle from tools/package_app.py')
    dest = ROOT / 'build/ui-profile' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    dest.mkdir(parents=True)
    executable = app / 'Contents/MacOS/Chromium'
    hasher = hashlib.sha256()
    with executable.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            hasher.update(chunk)
    output = {'executable_sha256': hasher.hexdigest(),
              'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_files_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT / 'chromium/app').glob('ui_lab*') if p.is_file()},
              'app': str(app), 'instrumented': args.trace, 'iterations': args.iterations,
              'rounds': args.rounds, 'runs': []}
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(ROOT / 'testdata'))
    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler) as server:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            for scenario in ('selection', 'resize'):
                for repetition in range(args.rounds):
                    for backend in BACKENDS[repetition % 3:] + BACKENDS[:repetition % 3]:
                        name = f'{scenario}-{repetition}-{backend}'
                        flags = ['--zitter-ui-profile']
                        if args.trace:
                            flags += ['--enable-tracing=benchmark,toplevel,cc,viz,gpu,views,skia,disabled-by-default-gpu.service,disabled-by-default-skia.gpu',
                                      '--enable-tracing-format=proto', '--trace-startup-record-mode=record-continuously',
                                      '--default-trace-buffer-size-limit-in-kb=131072',
                                      '--enable-tracing-output=' + str(dest / (name + '.pftrace'))]
                        print(name, flush=True)
                        log = dest / (name + '.log')
                        run = launch(app / 'Contents/MacOS/Chromium', dest / (name + '-profile'),
                                     f'http://127.0.0.1:{server.server_port}/performance_probe.html', backend,
                                     scenario, args.iterations, log, extra_args=flags)
                        frames = [f for f in records(log.read_text(), 'ZITTER_UI_FRAME') if f['trial'] >= 0]
                        run.update(backend=backend, scenario=scenario, repetition=repetition, frames=frames)
                        if backend != 'rust-metal':
                            run['stages'] = summarize_frames(frames, args.iterations)
                        output['runs'].append(run)
                        (dest / 'results.json').write_text(json.dumps(output, indent=2) + '\n')
        finally:
            server.shutdown()
    print(dest, flush=True)


if __name__ == '__main__':
    main()
