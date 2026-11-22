#!/usr/bin/env python3
"""EN: Summarize Vulkan batch submissions, preserving multiple submissions per frame.
PT-BR: Resume as submissões Vulkan, incluindo várias submissões por frame.

Usage / Uso:
python3 portable/tools/report_batches.py .local/run/vulkan [--window 300]
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
            # EN: One game frame can produce several Vulkan submissions. Equal
            # frame IDs are expected and MUST be included in the totals.
            # PT-BR: Um frame pode gerar diversas submissões Vulkan. IDs iguais
            # são válidos e DEVEM participar das estatísticas.
            if last is not None and current < last:
                errors.append(f"line {line_no}: frame {current} went backwards from {last}")
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


def aggregate_frames(rows):
    """EN: Group sequential submissions sharing a frame ID.
    PT-BR: Agrupa submissões consecutivas do mesmo frame sem perder dados.
    """
    frames = []
    for row in rows:
        if frames and frames[-1]["frame"] == row["frame"]:
            for field in REQUIRED[1:]:
                frames[-1][field] += row[field]
            frames[-1]["submissions"] += 1
        else:
            frames.append({**row, "submissions": 1})
    return frames


def describe(rows):
    frames = aggregate_frames(rows)
    sums = {key: sum(frame[key] for frame in frames) for key in REQUIRED if key != "frame"}
    total = sums["accepted"]
    saved = sums["passes_saved"]
    submissions = sum(frame["submissions"] for frame in frames)
    print(f"Frames analyzed / Frames analisados: {len(frames)} "
          f"({frames[0]['frame']}..{frames[-1]['frame']}); "
          f"submissions / submissões: {submissions}")
    print(f"Accepted draws / Desenhos aceitos: {total:,}; "
          f"render passes / passes: {sums['render_passes']:,}")
    print(f"Batched groups / Grupos: {sums['batched_groups']:,}; "
          f"batched draws / draws agrupados: {sums['batched_draws']:,}")
    baseline = saved + sums["render_passes"]
    print(f"Render passes saved / Passes evitados: {saved:,} "
          f"({saved/baseline*100:.1f}% of estimated unbatched passes)" if baseline
          else "Render passes saved / Passes evitados: 0")
    print(f"Draws grouped / Draws agrupados: {sums['batched_draws']/total*100:.1f}%"
          if total else "Draws grouped / Draws agrupados: n/a")
    counts = [frame["passes_saved"] for frame in frames]
    print(f"Saved passes/frame / Passes evitados por frame: "
          f"mean/média {statistics.mean(counts):.2f}, max/máximo {max(counts)}")
    print("EN: Fewer passes alone do not prove higher FPS. "
          "PT-BR: Menos passes não comprovam ganho de FPS.")


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
    grouped = aggregate_frames(rows)
    if args.window and len(grouped) > args.window:
        first = grouped[-args.window]["frame"]
        print(f"\nLast {args.window} frames / Últimos {args.window} frames:")
        describe([row for row in rows if row["frame"] >= first])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
