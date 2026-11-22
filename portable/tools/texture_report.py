#!/usr/bin/env python3
"""EN: Analyze CPU texture scanning/decoding from Vulkan frames.csv.
PT-BR: Analisa o custo de comparação/decodificação de texturas na CPU.

Example / Exemplo: python3 portable/tools/texture_report.py .local/run/vulkan --window 300
"""
import argparse
import csv
from pathlib import Path
import statistics
import sys

COUNTS = (
    "texture_static_binds", "texture_memcmp_checks", "texture_memcmp_bytes",
    "texture_same_draw_reuses", "texture_decodes",
)
TIMES = ("texture_check_ms", "decode_ms", "capture_ms", "frame_ms")


def read_frames(path: Path, after: int = 0, before: int | None = None):
    """EN: Return valid frames and malformed rows.
    PT-BR: Retorna frames válidos e registros incorretos, na ordem original.
    """
    records, errors = [], []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        fields = reader.fieldnames or []
        missing = [k for k in ("frame", *COUNTS, *TIMES) if k not in fields]
        if missing:
            raise ValueError("missing fields: " + ", ".join(missing))
        for lineno, row in enumerate(reader, 2):
            if None in row or any(row[k] is None for k in ("frame", *COUNTS, *TIMES)):
                errors.append(f"line {lineno}: incomplete CSV record")
                continue
            try:
                frame = int(row["frame"])
                counts = {key: int(row[key]) for key in COUNTS}
                times = {key: float(row[key]) for key in TIMES}
                if any(value < 0 for value in counts.values()) or any(value < 0 for value in times.values()):
                    raise ValueError("negative metric")
                if not all(value < float("inf") for value in times.values()):
                    raise ValueError("non-finite metric")
                if counts["texture_same_draw_reuses"] > counts["texture_static_binds"]:
                    raise ValueError("reuse exceeds static binds")
            except (TypeError, ValueError) as exc:
                errors.append(f"line {lineno}: {exc}")
                continue
            if frame > after and (before is None or frame < before):
                records.append({"frame": frame, **counts, **times})
    return records, errors


def percentile(values, pct: float):
    sample = sorted(values)
    if not sample:
        return 0.0
    return sample[max(0, min(len(sample)-1, int((len(sample)-1)*pct + 0.999999)))]


def report(rows, name="Selected frames"):
    if not rows:
        raise ValueError("no valid frames to summarize")
    total = {key: sum(row[key] for row in rows) for key in COUNTS}
    n = len(rows)
    print(f"{name}: {n} frames ({rows[0]['frame']}..{rows[-1]['frame']})")
    print(f"  Static binds: {total['texture_static_binds']:,}; reused within draw: {total['texture_same_draw_reuses']:,} "
          f"({100*total['texture_same_draw_reuses']/max(total['texture_static_binds'],1):.2f}%)")
    print(f"  Guest memcmp calls: {total['texture_memcmp_checks']:,} "
          f"({total['texture_memcmp_checks']/n:.1f}/frame)")
    print(f"  Requested memcmp scan: {total['texture_memcmp_bytes']/1048576:.1f} MiB total, "
          f"{total['texture_memcmp_bytes']/n/1048576:.1f} MiB/frame upper bound")
    print(f"  Decoded texture snapshots: {total['texture_decodes']:,} "
          f"({total['texture_decodes']/n:.2f}/frame)")
    for key in TIMES:
        nums = [row[key] for row in rows]
        print(f"  {key}: mean {statistics.mean(nums):.3f} ms; P95 {percentile(nums, 0.95):.3f} ms; "
              f"P99 {percentile(nums, 0.99):.3f} ms")
    print("  EN: Requested memcmp length is an upper bound (early exit possible).")
    print("  PT-BR: A extensão solicitada ao memcmp é um limite superior; a comparação pode parar antes.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="run directory or frames.csv path")
    parser.add_argument("--after-frame", type=int, default=0, help="exclusive lower frame bound")
    parser.add_argument("--before-frame", type=int, default=None, help="exclusive upper frame bound")
    parser.add_argument("--window", type=int, default=0, help="show additional summary for last N frames")
    args = parser.parse_args(argv)
    if args.window < 0 or (args.before_frame is not None and args.before_frame <= args.after_frame):
        parser.error("invalid frame range or window")
    path = args.directory if args.directory.is_file() else args.directory / "frames.csv"
    try:
        rows, errors = read_frames(path, args.after_frame, args.before_frame)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    for err in errors[:10]:
        print("WARNING: " + err, file=sys.stderr)
    if len(errors) > 10:
        print(f"WARNING: {len(errors)-10} additional malformed rows", file=sys.stderr)
    if not rows:
        parser.error("no valid records in selected frame range")
    report(rows)
    if args.window and len(rows) > args.window:
        print()
        report(rows[-args.window:], f"Last {args.window} frames")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
