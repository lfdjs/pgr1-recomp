#!/usr/bin/env python3
"""Summarize PGR1 Vulkan batch telemetry and report truncated CSV records.

Usage: python3 portable/tools/report_batches.py .local/run/vulkan [--after-frame N] [--before-frame N]
"""
import argparse
import csv
from pathlib import Path
import statistics
import sys

REQUIRED = ("frame", "accepted", "render_passes", "batched_groups", "batched_draws", "passes_saved")


def load_csv(path, after=0, before=None):
    valid, errors = [], []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.reader(stream)
        header = next(reader, None)
        if not header:
            raise ValueError("CSV is empty")
        if len(set(header)) != len(header):
            raise ValueError("CSV has duplicate column names")
        missing = [key for key in REQUIRED if key not in header]
        if missing:
            raise ValueError("missing columns: " + ", ".join(missing))
        positions = {key: header.index(key) for key in REQUIRED}
        last = None
        for line_no, fields in enumerate(reader, start=2):
            if not fields or all(not cell.strip() for cell in fields):
                continue
            if len(fields) != len(header):
                errors.append(f"line {line_no}: expected {len(header)} columns, got {len(fields)}")
                continue
            try:
                values = {key: int(fields[index].strip()) for key, index in positions.items()}
                if any(value < 0 for value in values.values()):
                    raise ValueError("negative counter")
            except ValueError as exc:
                errors.append(f"line {line_no}: invalid integer/counter ({exc})")
                continue
            current = values["frame"]
            if last is not None and current <= last:
                errors.append(f"line {line_no}: frame {current} not newer than {last}")
                continue
            last = current
            if current <= after or (before is not None and current >= before):
                continue
            expected_saved = values["batched_draws"] - values["batched_groups"]
            if expected_saved < 0 or values["passes_saved"] != expected_saved:
                errors.append(f"line {line_no}: inconsistent saved passes")
                continue
            if values["batched_draws"] > values["accepted"]:
                errors.append(f"line {line_no}: batched draws exceed accepted")
                continue
            valid.append(values)
    return valid, errors


def describe(rows):
    sums = {key: sum(row[key] for row in rows) for key in REQUIRED if key != "frame"}
    n = len(rows)
    total = sums["accepted"]
    saved = sums["passes_saved"]
    print(f"Frames analyzed: {n} ({rows[0]['frame']}..{rows[-1]['frame']})")
    print(f"Accepted draws: {total:,}; render passes: {sums['render_passes']:,}")
    print(f"Batched groups: {sums['batched_groups']:,}; batched draws: {sums['batched_draws']:,}")
    print(f"Render passes saved: {saved:,} ({saved/(saved+sums['render_passes'])*100:.1f}% of estimated unbatched passes)" if saved+sums["render_passes"] else "Render passes saved: 0")
    print(f"Draws grouped: {sums['batched_draws']/total*100:.1f}% of accepted" if total else "Draws grouped: n/a")
    counts = [row["passes_saved"] for row in rows]
    print(f"Saved passes/frame: mean {statistics.mean(counts):.2f}, max {max(counts)}")
    print("Note: pass reductions do not by themselves demonstrate an FPS improvement.")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path, help="directory containing vk_batches.csv, or CSV path")
    p.add_argument("--after-frame", type=int, default=0, help="exclusive lower frame bound")
    p.add_argument("--before-frame", type=int, help="exclusive upper frame bound")
    p.add_argument("--window", type=int, default=0, help="also report the last N valid frames")
    args = p.parse_args(argv)
    if args.window < 0 or (args.before_frame is not None and args.before_frame <= args.after_frame):
        p.error("invalid frame window")
    path = args.directory if args.directory.is_file() else args.directory / "vk_batches.csv"
    try:
        rows, errors = load_csv(path, args.after_frame, args.before_frame)
    except (OSError, ValueError) as exc:
        p.error(str(exc))
    for error in errors[:10]:
        print("WARNING: " + error, file=sys.stderr)
    if len(errors) > 10:
        print(f"WARNING: {len(errors)-10} further malformed rows", file=sys.stderr)
    if errors:
        print(f"Discarded {len(errors)} malformed CSV rows", file=sys.stderr)
    if not rows:
        p.error("no valid rows found in the requested frame range")
    describe(rows)
    if args.window and len(rows) > args.window:
        print(f"\nLast {args.window} valid frames:")
        describe(rows[-args.window:])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
