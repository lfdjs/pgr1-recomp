#!/usr/bin/env python3
"""Report successful presentations when recorded; state assumptions for legacy CSVs."""
import argparse
import csv
import math
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


def pacing(intervals, target_fps):
    """Report measured intervals, with jitter tolerance stated rather than hidden."""
    budget = 1000 / target_fps
    worst = sorted(intervals, reverse=True)[:max(1, math.ceil(len(intervals) / 100))]
    exact = sum(t > budget for t in intervals)
    late = sum(t > budget + 1 for t in intervals)
    double = sum(t > 2 * budget for t in intervals)
    streak = longest = 0
    for interval in intervals:
        streak = streak + 1 if interval > budget + 1 else 0
        longest = max(longest, streak)
    print(f"Target={target_fps:g} frames/s ({budget:.3f} ms/frame): "
          f"over budget={exact}/{len(intervals)} ({exact/len(intervals):.1%}); "
          f"over budget+1 ms={late}/{len(intervals)} ({late/len(intervals):.1%}); "
          f"over two budgets={double}")
    print(f"1% low presentation rate={1000/statistics.mean(worst):.2f} frames/s "
          f"(inverse mean of slowest {len(worst)} intervals); "
          f"longest consecutive budget+1 ms overruns={longest}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", action="store_true")
    parser.add_argument("--after-frame", type=int, default=0,
                        help="exclude this frame and all earlier frames")
    parser.add_argument("--before-frame", type=int,
                        help="exclude this frame and all later frames")
    parser.add_argument("--target-fps", type=float, default=60,
                        help="frametime budget to evaluate (default: 60)")
    args = parser.parse_args()
    if not math.isfinite(args.target_fps) or args.target_fps <= 0:
        parser.error("--target-fps must be a finite positive number")
    if args.before_frame is not None and args.before_frame <= args.after_frame:
        parser.error("--before-frame must be greater than --after-frame")
    file = args.directory / ("pgr1_perf.csv" if args.reference else "frames.csv")
    try:
        with file.open(newline="") as handle:
            rows = list(csv.DictReader(handle))
    except OSError as error:
        raise SystemExit(f"Cannot read timing data: {error}") from error
    frame_key = "capture_frame" if args.reference else "frame"
    rows = [r for r in rows if int(r[frame_key]) > args.after_frame and
            (args.before_frame is None or int(r[frame_key]) < args.before_frame)]
    if args.reference:
        if not rows:
            raise SystemExit("No reference captures in selected frame range; benchmark unavailable")
        print(f"Sampled CPU reference captures: {len(rows)} (not GPU frame time)")
        for key in ("reference_draw_ms", "reference_clear_ms", "decode_ms", "ipc_ms"):
            stats(key, [float(r[key]) for r in rows])
        return
    if not rows:
        raise SystemExit("No submitted game frames; benchmark unavailable")
    # Modern runtime CSVs include unsuccessful acquire/present attempts. Those
    # rows and offscreen work must never count as displayed frames. Prefer the
    # monotonic successful-present counter's intervals: frame_ms measures the
    # time between title present requests, which may have failed in between.
    requested_rows = rows
    has_status = "presented" in rows[0]
    direct_intervals = "presented_frame_ms" in rows[0]
    if has_status:
        try:
            rows = [r for r in rows if int(r["presented"]) == 1]
        except (TypeError, ValueError) as error:
            raise SystemExit("Invalid presentation status; timing data is incomplete") from error
        if not rows:
            raise SystemExit("No successful presented game frames; benchmark unavailable")
        print(f"Successful presentations={len(rows)}/{len(requested_rows)} logged rows; "
              f"excluded unsuccessful/offscreen rows={len(requested_rows)-len(rows)}")
    else:
        print("Legacy CSV: no presentation status; rates assume each logged frame was presented.")
    try:
        if direct_intervals:
            measured = [float(r["presented_frame_ms"]) for r in rows]
            if any(not math.isfinite(t) or t < 0 for t in measured):
                raise ValueError("invalid successful-present interval")
            # The first successful present establishes the clock and has no
            # previous successful presentation to measure; it reports zero.
            intervals = [t for t in measured if t > 0]
            print(f"Interval source=successful-present counter; measured intervals={len(intervals)}; "
                  f"uninitialized intervals omitted={len(measured)-len(intervals)}")
        elif has_status:
            # Older native CSVs have status but no direct counter. Account for
            # time spent in failed attempts between successful presentations.
            intervals = []
            elapsed = 0.0
            for row in requested_rows:
                interval = float(row["frame_ms"])
                if not math.isfinite(interval) or interval < 0:
                    raise ValueError("invalid logged frame interval")
                elapsed += interval
                if int(row["presented"]) == 1:
                    if elapsed > 0:
                        intervals.append(elapsed)
                    elapsed = 0.0
            print("Interval source=accumulated logged intervals between successful presentations")
        else:
            intervals = [float(r["frame_ms"]) for r in rows]
            if any(not math.isfinite(t) or t <= 0 for t in intervals):
                raise ValueError("invalid legacy frame interval")
    except (KeyError, TypeError, ValueError) as error:
        raise SystemExit("Nonpositive or nonfinite frame interval; timing data is incomplete") from error
    if not intervals:
        raise SystemExit("No measured positive presentation intervals; benchmark unavailable")
    print(f"Selected frame range={rows[0]['frame']}..{rows[-1]['frame']} "
          "(scene selection requires visual confirmation)")
    stats("End-to-end frame", intervals)
    pacing(intervals, args.target_fps)
    for key in ("cpu_ms", "capture_ms", "max_draw_ms", "texture_check_ms", "assemble_ms",
                "decode_ms", "queue_wait_ms", "execute_ms", "fence_ms", "acquire_ms", "present_ms"):
        stats(key, [float(r[key]) for r in rows if key in r])
    supported = sum(int(r['supported_draws']) for r in rows)
    rejected = sum(int(r['rejected_draws']) for r in rows)
    gpu = args.directory / 'gpu.csv'
    if gpu.is_file():
        selected_frames = {int(r['frame']) for r in rows}
        with gpu.open(newline='') as handle:
            by_frame = {}
            for row in csv.DictReader(handle):
                frame = int(row['frame'])
                if frame in selected_frames:
                    by_frame[frame] = by_frame.get(frame, 0) + float(row['gpu_ms'])
            times = list(by_frame.values())
        stats('GPU timestamp span (asynchronous)', times)
        print(f"GPU timestamps matched={len(by_frame)}/{len(selected_frames)} selected frames")
    print(f"Frames={len(rows)} supported Draws={supported} rejected Draws={rejected}")
    if rejected or not supported:
        print('Incomplete rendering: these timings do not establish playable-game FPS.')
    else:
        elapsed = sum(intervals) / 1000
        print(f'Mean presentation rate={len(intervals)/elapsed:.2f} frames/s; scene correctness still requires visual review.')


if __name__ == '__main__':
    main()
