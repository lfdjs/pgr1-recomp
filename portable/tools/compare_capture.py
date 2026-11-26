#!/usr/bin/env python3
"""EN: Compare independently selected Vulkan capture intervals; scene matching remains manual.
PT-BR: Compara intervalos independentes da captura Vulkan; pareamento de cenas é manual.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics

TIMES = ("cpu_ms", "capture_ms", "texture_check_ms", "assemble_ms", "decode_ms",
         "queue_wait_ms", "execute_ms", "texture_tracker_ms", "texture_tracker_write_ms")
COUNTS = ("supported_draws", "rejected_draws", "texture_static_binds", "texture_memcmp_checks",
          "texture_memcmp_bytes", "texture_decodes", "texture_cache_misses",
          "texture_overlap_checks", "texture_diagnostic_skips",
          "texture_guest_redecodes", "texture_descriptor_redecodes", "texture_cache_evictions",
          "texture_mip_levels_decoded", "texture_mip_levels_reused", "texture_tracker_queries",
          "texture_tracker_fast_hits", "texture_tracker_covered_queries", "texture_tracker_invalidations",
          "texture_tracker_exact_fallbacks", "texture_tracker_uncovered_ranges",
          "texture_tracker_false_positives", "texture_tracker_false_negatives",
          "texture_tracker_monitored_writes", "texture_tracker_dirty_pages",
          "descriptor_creates", "staging_allocations", "staging_reuses",
          "geometry_vertices", "geometry_indices", "geometry_bytes")
GAUGES = ("texture_cache_entries_peak", "texture_tracker_tracked_bytes")
STATUS = ("presented", "present_requested", "acquire_timeout", "render_failed")
SCENE_NOTICE = ("Scene equivalence NOT established by frame numbers / "
                "Números de frame NÃO comprovam equivalência de cena.")


def read_frames(path):
    """EN/PT-BR: Reject malformed evidence / Rejeita evidência malformada."""
    path = Path(path)
    if path.is_dir():
        path = path / "frames.csv"
    records = []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        fields = reader.fieldnames or []
        if any(not key for key in fields) or len(set(fields)) != len(fields):
            raise ValueError(f"{path}: blank or duplicate CSV columns")
        if "frame" not in fields or not any(k in fields for k in ("frame_ms", "presented_frame_ms")):
            raise ValueError(f"{path}: required fields: frame and frame_ms or presented_frame_ms")
        integer_fields = [k for k in ("frame", *COUNTS, *GAUGES, *STATUS) if k in fields]
        time_fields = [k for k in ("frame_ms", "presented_frame_ms", *TIMES) if k in fields]
        for line, raw in enumerate(reader, 2):
            if None in raw or any(value is None for value in raw.values()):
                raise ValueError(f"{path}:{line}: truncated or extra CSV fields")
            try:
                row = {key: int(raw[key]) for key in integer_fields}
                if any(value < 0 or value > (1 << 64) - 1 for value in row.values()):
                    raise ValueError("integer metric outside uint64 range")
                row.update({key: float(raw[key]) for key in time_fields})
                if any(not math.isfinite(value) or value < 0 for value in row.values()):
                    raise ValueError("negative or non-finite metric")
                if any(row[key] not in (0, 1) for key in STATUS if key in row):
                    raise ValueError("invalid presentation status")
                for key in ("texture_tracker_covered_queries", "texture_tracker_fast_hits"):
                    if key in row and "texture_tracker_queries" in row and row[key] > row["texture_tracker_queries"]:
                        raise ValueError(f"{key} exceeds tracker queries")
            except (TypeError, ValueError) as error:
                raise ValueError(f"{path}:{line}: {error}") from error
            records.append(row)
    return records


def distribution(values):
    sample = sorted(values)
    if not sample:
        raise ValueError("no samples")
    def percentile(p):
        return sample[max(0, math.ceil(p * len(sample)) - 1)]
    return {"mean": statistics.mean(sample), "p50": percentile(0.50),
            "p95": percentile(0.95), "p99": percentile(0.99)}


def summarize(records, after=0, before=None, target_fps=60):
    if not math.isfinite(target_fps) or target_fps <= 0:
        raise ValueError("target FPS must be finite and positive")
    if before is not None and before <= after:
        raise ValueError("before must exceed after")
    if not records:
        raise ValueError("no logged frames")
    selected = [row for row in records if row["frame"] > after and
                (before is None or row["frame"] < before)]
    if not selected:
        raise ValueError("no frames in selected range")
    has_status = "presented" in records[0]
    direct = "presented_frame_ms" in records[0]
    warnings = []
    if not has_status:
        warnings.append("Legacy CSV: no presented status; assume each logged row was presented.")
    if direct:
        source = "presented_frame_ms (successful-present counter)"
    elif has_status:
        source = "legacy frame_ms accumulated between recorded successful presentations"
        warnings.append("Legacy fallback: failed-attempt time is accumulated; first successful present has no known predecessor and is omitted.")
    else:
        source = "legacy frame_ms (presentation assumed)"
        warnings.append("Legacy fallback: frame_ms measures logged intervals, not a verified successful-present counter.")
    # EN: Build legacy intervals before filtering so failed attempts before the
    #     lower selection bound are retained in the next successful interval.
    # PT-BR: Monte intervalos legados antes do filtro para incluir tentativas
    #        falhas anteriores ao limite inferior na apresentação seguinte.
    samples = []
    elapsed = 0.0
    seen_success = False
    successful_selected = 0
    for row in records:
        in_range = row["frame"] > after and (before is None or row["frame"] < before)
        success = not has_status or row["presented"] == 1
        if success and in_range:
            successful_selected += 1
        if direct:
            interval = row["presented_frame_ms"] if success else 0
        elif has_status:
            elapsed += row["frame_ms"]
            interval = elapsed if success and seen_success else 0
            if success:
                seen_success = True
                elapsed = 0.0
        else:
            interval = row["frame_ms"]
        if in_range and success and interval > 0:
            samples.append((row, interval))
    if not samples:
        raise ValueError("no positive successful-presentation intervals in selected range")
    rows = [row for row, _ in samples]
    intervals = [interval for _, interval in samples]
    stats = distribution(intervals)
    worst = sorted(intervals, reverse=True)[:max(1, math.ceil(len(intervals) / 100))]
    budget = 1000 / target_fps
    above = sum(value > budget for value in intervals)
    metrics = {key: distribution([row[key] for row in rows]) for key in TIMES if key in rows[0]}
    counts = {key: {"total": sum(row[key] for row in rows),
                    "per_frame": statistics.mean(row[key] for row in rows)}
              for key in COUNTS if key in rows[0]}
    gauges = {key: max(row[key] for row in rows) for key in GAUGES if key in rows[0]}
    draws = counts.get("supported_draws", {}).get("total", 0)
    per_draw = {key: sum(row[key] for row in rows) / draws for key in metrics} if draws else {}
    def ratio(numerator, denominator):
        if numerator not in counts or denominator not in counts:
            return None
        total = counts[denominator]["total"]
        return counts[numerator]["total"] / total if total else None
    rates = {"covered_queries": ratio("texture_tracker_covered_queries", "texture_tracker_queries"),
             "proven_fast_hits": ratio("texture_tracker_fast_hits", "texture_tracker_queries")}
    if all(key in counts for key in ("texture_tracker_fast_hits", "texture_memcmp_checks")):
        hits = counts["texture_tracker_fast_hits"]["total"]
        checks = counts["texture_memcmp_checks"]["total"]
        rates["comparisons_avoided_by_proof"] = hits / (hits + checks) if hits + checks else None
    if counts.get("rejected_draws", {}).get("total", 0):
        warnings.append("Rejected draws present; timings do not establish complete rendering.")
    if counts.get("texture_diagnostic_skips", {}).get("total", 0):
        warnings.append("UNSAFE skip-exact diagnostic: stale textures are possible; not correctness evidence.")
    warnings.append("Only measured successful rows contribute capture metrics; timings may nest and must not be added as exclusive costs.")
    return {"selected_logged_rows": len(selected), "successful_presentations": successful_selected,
            "measured_intervals": len(samples), "measured_frame_first": rows[0]["frame"],
            "measured_frame_last": rows[-1]["frame"], "interval_source": source,
            "frame_ms": stats, "mean_fps": 1000 / stats["mean"],
            "one_percent_low_fps": 1000 / statistics.mean(worst),
            "one_percent_low_samples": len(worst), "budget_ms": budget,
            "above_budget_frames": above, "above_budget_fraction": above / len(intervals),
            "metrics_ms": metrics, "weighted_ms_per_supported_draw": per_draw,
            "counts": counts, "peak_gauges": gauges, "tracker_rates": rates, "warnings": warnings}


def differences(left, right):
    """EN/PT-BR: Right minus left, no causal claim / Direita menos esquerda, sem alegação causal."""
    values = {"frame_ms.mean": (left["frame_ms"]["mean"], right["frame_ms"]["mean"]),
              "mean_fps": (left["mean_fps"], right["mean_fps"])}
    for key in left["metrics_ms"].keys() & right["metrics_ms"].keys():
        values[f"{key}.mean"] = (left["metrics_ms"][key]["mean"], right["metrics_ms"][key]["mean"])
    for key in left["weighted_ms_per_supported_draw"].keys() & right["weighted_ms_per_supported_draw"].keys():
        values[f"{key}.per_supported_draw"] = (left["weighted_ms_per_supported_draw"][key],
                                               right["weighted_ms_per_supported_draw"][key])
    for key in left["counts"].keys() & right["counts"].keys():
        values[f"{key}.per_frame"] = (left["counts"][key]["per_frame"], right["counts"][key]["per_frame"])
    return {key: {"absolute": b - a, "percent": 100 * (b - a) / a if a else None}
            for key, (a, b) in sorted(values.items())}


def print_summary(label, summary):
    print(f"{label}: frames {summary['measured_frame_first']}..{summary['measured_frame_last']}; "
          f"measured={summary['measured_intervals']}; successful={summary['successful_presentations']}/"
          f"{summary['selected_logged_rows']}; source={summary['interval_source']}")
    stats = summary["frame_ms"]
    print(f"  Frame ms: mean={stats['mean']:.3f} P50={stats['p50']:.3f} "
          f"P95={stats['p95']:.3f} P99={stats['p99']:.3f}; FPS={summary['mean_fps']:.2f}; "
          f"1% low={summary['one_percent_low_fps']:.2f} "
          f"(inverse mean of slowest {summary['one_percent_low_samples']} intervals)")
    print(f"  Above {summary['budget_ms']:.3f} ms budget: {summary['above_budget_frames']}/"
          f"{summary['measured_intervals']} ({summary['above_budget_fraction']:.2%})")
    for key, stats in summary["metrics_ms"].items():
        per_draw = summary["weighted_ms_per_supported_draw"].get(key)
        cost = f"; weighted ms/supported draw={per_draw:.6f}" if per_draw is not None else ""
        print(f"  {key}: mean={stats['mean']:.3f} P50={stats['p50']:.3f} "
              f"P95={stats['p95']:.3f} P99={stats['p99']:.3f}{cost}")
    for key, values in summary["counts"].items():
        print(f"  {key}: total={values['total']:,}; per measured frame={values['per_frame']:.3f}")
    for key, value in summary["peak_gauges"].items():
        print(f"  {key}: peak={value:,}")
    for key, value in summary["tracker_rates"].items():
        print(f"  tracker {key}: {value:.2%}" if value is not None else f"  tracker {key}: unavailable")
    for warning in summary["warnings"]:
        print(f"  EN/PT-BR warning/aviso: {warning}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    for side in ("left", "right"):
        parser.add_argument(f"--{side}-after", type=int, default=0, help="exclusive lower frame bound")
        parser.add_argument(f"--{side}-before", type=int, help="exclusive upper frame bound")
    parser.add_argument("--target-fps", type=float, default=60)
    parser.add_argument("--scene-note", default="", help="user-supplied scene/configuration note, not verified")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON to stdout")
    args = parser.parse_args(argv)
    try:
        left = summarize(read_frames(args.left), args.left_after, args.left_before, args.target_fps)
        right = summarize(read_frames(args.right), args.right_after, args.right_before, args.target_fps)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    result = {"notice": SCENE_NOTICE, "scene_note_unverified": args.scene_note,
              "left_source": str(args.left), "right_source": str(args.right),
              "target_fps": args.target_fps, "left": left, "right": right,
              "delta_right_minus_left": differences(left, right)}
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))
    else:
        print(SCENE_NOTICE)
        if args.scene_note:
            print("User note, unverified / Nota do usuário, não verificada: " + args.scene_note)
        print_summary("LEFT / ESQUERDA", left)
        print_summary("RIGHT / DIREITA", right)
        print("Delta RIGHT minus LEFT / DIREITA menos ESQUERDA (no causal claim / sem alegação causal):")
        for key, delta in result["delta_right_minus_left"].items():
            percent = f"{delta['percent']:+.2f}%" if delta["percent"] is not None else "unavailable (zero baseline)"
            print(f"  {key}: {delta['absolute']:+.6f} ({percent})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
