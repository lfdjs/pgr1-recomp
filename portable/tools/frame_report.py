#!/usr/bin/env python3
"""Report actual submitted game frames. Reject empty/reference-incomplete FPS claims."""
import argparse
import csv
import statistics
from pathlib import Path


def stats(name, values):
    if not values:
        print(f"{name}: unavailable")
        return
    values = sorted(values)
    def percentile(p):
        return values[max(0, (p * len(values) + 99) // 100 - 1)]
    print(f"{name}: mean={statistics.mean(values):.3f} ms "
          f"P95={percentile(95):.3f} P99={percentile(99):.3f} max={max(values):.3f}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", action="store_true")
    parser.add_argument("--after-frame", type=int, default=0)
    args = parser.parse_args()
    file = args.directory / ("pgr1_perf.csv" if args.reference else "frames.csv")
    with file.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    if args.reference:
        print(f"Sampled CPU reference captures: {len(rows)} (not GPU frame time)")
        for key in ("reference_draw_ms", "reference_clear_ms", "decode_ms", "ipc_ms"):
            stats(key, [float(r[key]) for r in rows])
        return
    rows = [r for r in rows if int(r['frame']) > args.after_frame]
    if not rows:
        raise SystemExit("No submitted game frames; benchmark unavailable")
    stats("End-to-end frame", [float(r['frame_ms']) for r in rows])
    for key in ("cpu_ms", "capture_ms", "max_draw_ms", "decode_ms", "queue_wait_ms", "execute_ms", "present_ms"):
        stats(key, [float(r[key]) for r in rows if key in r])
    supported = sum(int(r['supported_draws']) for r in rows)
    rejected = sum(int(r['rejected_draws']) for r in rows)
    gpu = args.directory / 'gpu.csv'
    if gpu.is_file():
        with gpu.open(newline='') as handle:
            by_frame = {}
            for row in csv.DictReader(handle):
                frame = int(row['frame'])
                if frame > args.after_frame:
                    by_frame[frame] = by_frame.get(frame, 0) + float(row['gpu_ms'])
            times = list(by_frame.values())
        stats('GPU timestamp span (asynchronous)', times)
    print(f"Frames={len(rows)} supported Draws={supported} rejected Draws={rejected}")
    if rejected or not supported:
        print('Incomplete rendering: these timings do not establish playable-game FPS.')
    else:
        elapsed = sum(float(r['frame_ms']) for r in rows) / 1000
        print(f'Mean presentation rate={len(rows)/elapsed:.2f} frames/s; scene correctness still requires visual review.')


if __name__ == '__main__':
    main()
