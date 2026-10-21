#!/usr/bin/env python3
"""Summarize xctrace XML exports for processes belonging to the profiled bundle.

Requires {cpu,gpu}-toc.xml, cpu-samples.xml or gpu-intervals.xml, and the
corresponding {cpu,gpu}-skia.log. Export the time-profile / metal-gpu-intervals
tables with xctrace export --xpath. Phase boundaries use approximate log time.
"""
import argparse
import collections
from datetime import datetime
import json
import math
from pathlib import Path
import re
import statistics
import xml.etree.ElementTree as ET


def phase_window(log, toc, iterations):
    text = log.read_text()
    times = []
    for trial in (0, iterations - 1):
        pattern = r'/(\d{2})(\d{2})(\d{2}\.\d+):INFO:[^\n]+ZITTER_UI_SAMPLE \{"trial":' + str(trial) + ','
        match = re.search(pattern, text)
        if not match:
            raise ValueError(f'Missing phase boundary trial {trial} in {log}')
        times.append(int(match[1]) * 3600 + int(match[2]) * 60 + float(match[3]))
    start = datetime.fromisoformat(toc.findtext('.//start-date'))
    offset = start.hour * 3600 + start.minute * 60 + start.second + start.microsecond / 1e6
    # Profiles crossing midnight retain a positive relative time.
    window = [(time - offset) % 86400 for time in times]
    # New logs retain callback latency, allowing the GPU window to begin at
    # update start rather than after the first frame has already rendered.
    first = re.search(r'ZITTER_UI_SAMPLE (\{[^\n]+\})', text)
    if first:
        try:
            sample = json.loads(first[1])
            if sample.get('trial') == 0 and sample.get('callback_ms', -1) >= 0:
                window[0] -= sample['callback_ms'] / 1000
        except (ValueError, TypeError):
            pass
    return tuple(window)


def summarize(directory, kind, iterations=240, bundle='ZitterUiProfile.app'):
    filename = 'cpu-samples.xml' if kind == 'cpu' else 'gpu-intervals.xml'
    tree = ET.parse(directory / filename)
    ids = {e.get('id'): e for e in tree.iter() if e.get('id')}

    def resolve(element):
        return ids[element.get('ref')] if element is not None and element.get('ref') else element

    toc = ET.parse(directory / (kind + '-toc.xml'))
    owned = {e.get('pid') for e in toc.findall('.//processes/process')
             if '/' + bundle + '/' in e.get('path', '')}
    if not owned:
        raise ValueError('No processes belonging to the requested bundle')
    lo, hi = phase_window(directory / (kind + '-skia.log'), toc, iterations)
    if hi <= lo:
        raise ValueError('Invalid measurement window')
    recording_duration = toc.findtext('.//summary/duration')
    if recording_duration is not None and (lo < 0 or hi > float(recording_duration)):
        raise ValueError('Recording does not cover the complete measurement window')
    processes, threads, leaf, stacks, channels = [collections.Counter() for _ in range(5)]
    missing, intervals, packets, latency, raw_gpu = 0, [], [], [], []
    for row in tree.findall('.//row'):
        process = resolve(row.find('process'))
        if process is None:
            continue
        pid = resolve(process.find('pid')).text
        if pid not in owned:
            continue
        timestamp = resolve(row.find('sample-time' if kind == 'cpu' else 'start-time'))
        if not lo <= int(timestamp.text) / 1e9 <= hi:
            continue
        if kind == 'cpu':
            weight = int(resolve(row.find('weight')).text) / 1e6
            processes[pid] += weight
            threads[resolve(row.find('thread')).get('fmt')] += weight
            backtrace = resolve(row.find('tagged-backtrace'))
            if backtrace is None:
                missing += 1
                continue
            names = [resolve(frame).get('name', '?') for frame in backtrace.findall('frame')]
            if names:
                leaf[names[0]] += weight
            for name in set(names):
                stacks[name] += weight
        else:
            # Deeper levels repeat the same execution at finer granularity.
            if resolve(row.find('metal-nesting-level')).text != '0':
                continue
            if resolve(row.find('gpu-state')).text != 'Active':
                continue
            durations = [resolve(e) for e in row if e.tag == 'duration']
            duration = int(durations[0].text) / 1e6
            start = int(timestamp.text) / 1e6
            packets.append(duration)
            intervals.append((start, start + duration))
            channel = resolve(row.find('gpu-channel-name')).text
            channels[channel] += 1
            raw_gpu.append({'start_ns': int(timestamp.text), 'duration_ns': int(durations[0].text), 'pid': pid, 'channel': channel})
            if len(durations) > 1:
                latency.append(int(durations[1].text) / 1e6)
    if kind == 'cpu':
        return {'recording_duration_seconds': float(recording_duration) if recording_duration is not None else None, 'window_seconds': [lo, hi], 'owned_pids': sorted(owned),
                'sampled_cpu_ms': sum(processes.values()), 'process_cpu_ms': dict(processes),
                'thread_cpu_ms': threads.most_common(20), 'leaf_cpu_ms': leaf.most_common(30),
                'inclusive_stack_cpu_ms': stacks.most_common(40), 'missing_backtraces': missing,
                'note': 'Time Profiler 1ms running-thread statistical samples, filtered to bundle-owned processes and measured update phase. Window uses log wall timestamps relative to Instruments recording start, approximate at phase boundaries. Inclusive stack entries overlap; do not sum them.'}
    if not packets or not latency:
        raise ValueError('No owned GPU execution / latency intervals in measurement window')
    merged = []
    for start, end in sorted(intervals):
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(end, merged[-1][1])
        else:
            merged.append([start, end])
    busy = sum(end - start for start, end in merged)

    def stats(values):
        return {'median': statistics.median(values),
                'p95': sorted(values)[math.ceil(.95 * len(values)) - 1], 'max': max(values)}

    return {'recording_duration_seconds': float(recording_duration) if recording_duration is not None else None, 'raw_gpu_intervals': raw_gpu, 'window_seconds': [lo, hi], 'gpu_packets': len(packets),
            'packet_duration_ms': stats(packets), 'cpu_to_gpu_latency_ms': stats(latency),
            'gpu_busy_union_ms': busy, 'gpu_busy_ms_per_update': busy / iterations,
            'gpu_busy_percent_of_window': busy / ((hi - lo) * 1000) * 100,
            'channels': dict(channels),
            'note': 'Metal System Trace hardware GPU intervals, depth 0, active packets owned by this bundle, measured Rust/Skia update phase. Union avoids vertex/fragment overlap. Includes page and UI, not isolated sidebar. Per-packet duration is not per-frame duration. Instrumentation changes timings; not backend ranking.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--kind', choices=('cpu', 'gpu'), required=True)
    parser.add_argument('--iterations', type=int, default=240)
    parser.add_argument('--bundle', default='ZitterUiProfile.app')
    args = parser.parse_args()
    if args.iterations < 2:
        parser.error('At least two iterations are required')
    result = summarize(args.directory, args.kind, args.iterations, args.bundle)
    text = json.dumps(result, indent=2) + '\n'
    (args.directory / (args.kind + '-analysis.json')).write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
