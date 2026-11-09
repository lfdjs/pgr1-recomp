#!/usr/bin/env python3
"""Analyze real PGR1 native GL frame CSV before migrating its command stream to Vulkan.

No invented GPU utilization / FPS and no assumption that benchmark entered a race.
The tool deliberately does not sum overlapping CPU/GL/queue timing dimensions.
"""
from __future__ import annotations
import argparse
import csv
import math
import re
import statistics
from pathlib import Path

NUMERIC = (
    'frame_ms', 'cpu_ms', 'capture_ms', 'max_draw_ms', 'decode_ms',
    'texture_check_ms', 'assemble_ms', 'queue_wait_ms', 'execute_ms', 'present_ms',
)

def q95(values: list[float]) -> float:
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * 0.95) - 1)]

def main() -> None:
    parser = argparse.ArgumentParser(description='PGR1 Linux frame/queue stall triage; does not benchmark Vulkan')
    parser.add_argument('run_dir', type=Path, nargs='?', default=Path('.local/run/linux'))
    parser.add_argument('--expected-seconds', type=float, default=180)
    parser.add_argument('--worst', type=int, default=8)
    args = parser.parse_args()
    path = args.run_dir / 'frames.csv'
    try:
        with path.open(newline='') as handle:
            data = list(csv.DictReader(handle))
    except OSError as exc:
        parser.error(f'cannot read {path}: {exc}')
    if not data:
        parser.error(f'{path} contains no frames')
    required = {'frame', *NUMERIC}
    missing = required - set(data[0])
    if missing:
        parser.error(f'missing CSV columns: {", ".join(sorted(missing))}')
    frames: list[dict] = []
    for index, item in enumerate(data, 2):
        try:
            values = {key: float(item[key]) for key in NUMERIC}
            frame_id = int(item['frame'])
        except (ValueError, TypeError, KeyError) as exc:
            parser.error(f'invalid row {index}: {exc}')
        if not all(math.isfinite(v) and v >= 0 for v in values.values()):
            parser.error(f'invalid nonfinite/negative metric in row {index}')
        if values['frame_ms'] <= 0:
            parser.error(f'nonpositive presentation interval in row {index}')
        values['frame'] = frame_id
        frames.append(values)
    elapsed = sum(frame['frame_ms'] for frame in frames) / 1000
    print(f'Run: {args.run_dir}')
    print(f'Frames: {len(frames)}; frame range: {frames[0]["frame"]}..{frames[-1]["frame"]}')
    print(f'Measured interval sum: {elapsed:.3f}s; average presented rate {len(frames)/elapsed:.2f} fps')
    if args.expected_seconds > 0 and elapsed < args.expected_seconds * 0.65:
        print(f'WARNING: measured frames cover much less than the requested {args.expected_seconds:g}s benchmark; inspect process exit, fatal logs and input script. FPS is NOT a race benchmark.')
    for key in NUMERIC:
        vals = [frame[key] for frame in frames]
        print(f'{key:18} mean={statistics.mean(vals):10.2f} ms p95={q95(vals):10.2f} ms max={max(vals):10.2f} ms')
    print('Largest presentation gaps (metrics overlap across guest/render threads; do not sum columns):')
    for frame in sorted(frames, key=lambda x: x['frame_ms'], reverse=True)[:max(0, args.worst)]:
        print(f'  frame={frame["frame"]:6} interval={frame["frame_ms"]:9.1f}ms '
              f'queue_wait={frame["queue_wait_ms"]:9.1f}ms execute={frame["execute_ms"]:8.2f}ms '
              f'present={frame["present_ms"]:9.1f}ms cpu={frame["cpu_ms"]:8.1f}ms')
    high_wait = sum(f['queue_wait_ms'] > 200 for f in frames)
    high_gpu = sum(f['execute_ms'] > 200 for f in frames)
    if high_wait and not high_gpu:
        print(f'OBSERVATION: {high_wait} frames have queue wait >200ms while no frame has execute >200ms. This suggests producer/consumer backpressure; compare presentation (SwapWindow) timing with GL execution and shader-translation logs.')
        present_block = sum(f['present_ms'] > 200 and f['execute_ms'] < 20 for f in frames)
        if present_block:
            print(f'PRESENT-BLOCK SIGNATURE: {present_block} frames had present_ms >200ms with execute_ms <20ms. Check SDL_GL_SwapWindow/VSync/compositor/driver first; Vulkan native may bypass that GL presentation path.')
    if (args.run_dir / 'gpu.csv').exists():
        with (args.run_dir / 'gpu.csv').open(newline='') as stream:
            gpu = list(csv.DictReader(stream))
        times = []
        for sample in gpu:
            try:
                v = float(sample['gpu_ms'])
            except (KeyError, ValueError):
                continue
            if math.isfinite(v) and v >= 0: times.append(v)
        if times:
            print(f'GPU timestamp spans: {len(times)} samples, mean={statistics.mean(times):.3f}ms, p95={q95(times):.3f}ms (NOT utilization)')
    cache = args.run_dir / 'shadercache-gl-v2'
    if cache.is_dir():
        print(f'Shader cache files: HLSL={len(list(cache.glob("*.hlsl")))} '
              f'SPIR-V={len(list(cache.glob("*.spv")))} GLSL={len(list(cache.glob("*.glsl")))}')
        failed = []
        for log in cache.glob('*.log'):
            try: text = log.read_text(errors='replace')
            except OSError: continue
            if re.search(r'(?im)^.*(?:ERROR:|compilation failed|link failed|fatal error).*$', text): failed.append(log)
        print(f'Possible shader compiler error logs: {len(failed)}')
        for file in failed[:6]: print(f'  {file}')
    console = args.run_dir / 'console.log'
    if console.exists():
        try: lines = console.read_text(errors='replace').splitlines()
        except OSError: lines = []
        selected = [l for l in lines if re.search(r'failed|error|fatal|exception|shutdown|quit asked|SIGTERM|translated shader', l, re.I)]
        print('Relevant console lines (last 15):')
        for line in selected[-15:]: print(f'  {line[:220]}')
    print('NEXT: compare identical runs with PGR1_VSYNC=1 vs PGR1_VSYNC=0 (and windowed OUTPUT=1280x720), collect a verified race, then compare matched CPU/GPU car frames. Vulkan-native will not automatically fix reflection/depth errors.')

if __name__ == '__main__':
    main()
