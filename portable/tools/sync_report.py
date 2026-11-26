#!/usr/bin/env python3
"""EN: Attribute all GPU submissions to successful presentation intervals.
PT-BR: Atribui todas as submissões GPU aos intervalos de apresentação efetiva.
"""
import argparse
from collections import Counter
import csv
import json
import math
from pathlib import Path
import re
import statistics

COUNTS = (
    "visibility_reports", "visibility_clears", "sync_calls", "sync_late_calls",
    "flush_calls", "resource_wait_calls", "fence_wait_calls", "gpu_busy_poll_count",
    "readback_waits", "visibility_queries", "visibility_query_pool_creates",
    "visibility_query_pool_reuses", "visibility_deferred_submissions",
    "readback_requests", "readback_bytes", "readback_buffer_creates",
    "readback_buffer_reuses", "submissions", "nonpresent_submissions",
    "render_passes", "pipeline_creates", "descriptor_set_allocations",
    "descriptor_set_hits", "visibility_direct_waits",
)
TIMES = ("visibility_report_wait_ms", "readback_wait_ms", "resource_wait_ms",
         "fence_wait_ms", "readback_resolve_ms", "shader_ms")
NOTICE = (
    "Nested CPU waits and asynchronous GPU spans are not additive exclusive costs / "
    "Esperas CPU aninhadas e spans GPU assíncronos não são custos exclusivos aditivos.",
    "Guest tick/vblank/frame selection and scene markers do not prove identical scenes; confirm captures / "
    "Seleção por tick/vblank/frame guest e marcadores não comprovam cenas idênticas; confirme as capturas.",
    "Wait percentiles describe totals per successful presentation interval, not individual calls / "
    "Percentis de espera descrevem totais por intervalo de apresentação, não chamadas individuais.",
    "Submission reasons identify triggers; waits finishing after a flush can appear in the next submission / "
    "Motivos identificam os gatilhos; esperas após um flush podem aparecer na submissão seguinte.",
)


def read_csv(path, required, integers=(), times=(), optional=False):
    """EN/PT-BR: Reject incomplete evidence before selection / Valide antes do filtro."""
    if optional and not path.exists():
        return None
    records = []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, strict=True)
        fields = reader.fieldnames or []
        if any(not key for key in fields) or len(set(fields)) != len(fields):
            raise ValueError(f"{path}: blank or duplicate CSV columns")
        missing = set(required) - set(fields)
        if missing:
            raise ValueError(f"{path}: missing fields: {', '.join(sorted(missing))}")
        for line, raw in enumerate(reader, 2):
            if None in raw or any(value is None or value == "" for value in raw.values()):
                raise ValueError(f"{path}:{line}: truncated or extra CSV fields")
            row = dict(raw)
            try:
                for key in integers:
                    if key in raw:
                        value = int(raw[key])
                        if value < 0 or value > (1 << 64) - 1:
                            raise ValueError(f"{key}: outside uint64 range")
                        row[key] = value
                for key in times:
                    if key in raw:
                        value = float(raw[key])
                        if not math.isfinite(value) or value < 0:
                            raise ValueError(f"{key}: negative or non-finite metric")
                        row[key] = value
                if "presented" in row and row["presented"] not in (0, 1):
                    raise ValueError("invalid presentation status")
                if "reason" in row and not re.fullmatch(r"[a-z_]+", row["reason"]):
                    raise ValueError("invalid submission reason")
                if all(key in row for key in ("submissions", "nonpresent_submissions")):
                    if row["nonpresent_submissions"] > row["submissions"]:
                        raise ValueError("nonpresent submissions exceed submissions")
            except (TypeError, ValueError) as error:
                raise ValueError(f"{path}:{line}: {error}") from error
            records.append(row)
    return records


def distribution(values):
    if not values:
        return None
    sample = sorted(values)
    return {"count": len(sample), "mean": statistics.mean(sample),
            "p50": sample[max(0, math.ceil(.50 * len(sample)) - 1)],
            "p95": sample[max(0, math.ceil(.95 * len(sample)) - 1)],
            "p99": sample[max(0, math.ceil(.99 * len(sample)) - 1)], "max": sample[-1]}


def total(rows, key):
    if not rows or any(key not in row for row in rows):
        return None
    values = [row[key] for row in rows]
    value = math.fsum(values) if key.endswith("_ms") else sum(values)
    if not math.isfinite(value):
        raise ValueError(f"{key}: aggregate overflow")
    return value


def selected(row, after_frame, before_frame, after_vblank, before_vblank, after_tick, before_tick):
    return ((after_frame is None or row["frame"] > after_frame) and
            (before_frame is None or row["frame"] < before_frame) and
            (after_vblank is None or row["guest_vblank"] > after_vblank) and
            (before_vblank is None or row["guest_vblank"] < before_vblank) and
            (after_tick is None or row["simulation_tick"] > after_tick) and
            (before_tick is None or row["simulation_tick"] < before_tick))


def summarize(directory, after_frame=None, before_frame=None,
              after_vblank=None, before_vblank=None, after_tick=None, before_tick=None):
    directory = Path(directory)
    for lower, upper in ((after_frame, before_frame), (after_vblank, before_vblank), (after_tick, before_tick)):
        if any(value is not None and value < 0 for value in (lower, upper)):
            raise ValueError("selection bounds must be nonnegative")
        if lower is not None and upper is not None and upper <= lower:
            raise ValueError("exclusive upper bound must exceed lower bound")
    sync = read_csv(directory / "sync.csv", ("frame", "guest_vblank", "presented", "reason"),
                    ("frame", "guest_vblank", "presented", *COUNTS), TIMES, optional=True)
    result = {"available": sync is not None, "warnings": list(NOTICE)}
    if sync is None:
        result["warnings"].append(
            "Synchronization profile unavailable: sync.csv absent; enable PGR1_VK_SYNC_PROFILE=1 / "
            "Perfil de sincronização indisponível: sync.csv ausente; ative PGR1_VK_SYNC_PROFILE=1.")
        return result
    frames = read_csv(directory / "frames.csv", ("frame", "presented"),
                      ("frame", "guest_vblank", "simulation_tick", "presented"), ("presented_frame_ms", "frame_ms"))
    if (after_vblank is not None or before_vblank is not None) and any(
            "guest_vblank" not in row for row in frames):
        raise ValueError("frames.csv: guest_vblank unavailable for scene selection")
    if (after_tick is not None or before_tick is not None) and any(
            "simulation_tick" not in row for row in frames):
        raise ValueError("frames.csv: simulation_tick unavailable for scene selection")
    frame_map = {}
    for row in frames:
        if row["frame"] in frame_map:
            raise ValueError("frames.csv: duplicate presentation frame")
        frame_map[row["frame"]] = row
    sync_map = {}
    for row in sync:
        if row["presented"] != 1 or row["frame"] in sync_map:
            raise ValueError("sync.csv: expected unique successful presentation intervals")
        frame = frame_map.get(row["frame"])
        if frame is None or frame["presented"] != 1:
            raise ValueError("sync.csv: successful endpoint missing from frames.csv")
        if "guest_vblank" in frame and row["guest_vblank"] != frame["guest_vblank"]:
            raise ValueError("sync.csv: guest_vblank disagrees with frames.csv")
        sync_map[row["frame"]] = row
    picked = [row for row in frames if row["presented"] == 1 and selected(
        row, after_frame, before_frame, after_vblank, before_vblank, after_tick, before_tick)]
    if not picked:
        raise ValueError("no successful presentations in selected scene interval")
    if any(row["frame"] not in sync_map for row in picked):
        raise ValueError("sync.csv: missing selected presentation; evidence is incomplete")
    endpoints = {row["frame"] for row in picked}
    intervals = [sync_map[row["frame"]] for row in picked]
    result["selection"] = {"frames": [row["frame"] for row in picked],
        "guest_vblanks": [row.get("guest_vblank") for row in picked],
        "simulation_ticks": [row.get("simulation_tick") for row in picked],
        "after_frame": after_frame, "before_frame": before_frame,
        "after_vblank": after_vblank, "before_vblank": before_vblank,
        "after_tick": after_tick, "before_tick": before_tick}
    result["successful_presentations"] = len(intervals)
    result["totals"] = {key: total(intervals, key) for key in (*COUNTS, *TIMES)}
    result["interval_distributions"] = {key: distribution([row[key] for row in intervals])
        if all(key in row for row in intervals) else None for key in (*TIMES, "submissions")}
    submissions = result["totals"]["submissions"]
    result["submissions_per_present"] = submissions / len(intervals) if submissions is not None else None
    measured = [row["presented_frame_ms"] for row in picked
                if row.get("presented_frame_ms", 0) > 0]
    result["presentation_ms"] = distribution(measured)
    result["fps"] = 1000 / statistics.mean(measured) if measured else None
    worst = sorted(measured, reverse=True)[:max(1, math.ceil(len(measured) / 100))]
    result["one_percent_low_fps"] = 1000 / statistics.mean(worst) if worst else None
    source = read_csv(directory / "sync-submissions.csv", ("frame", "guest_vblank", "presented", "reason"),
                      ("frame", "guest_vblank", "presented", *COUNTS), TIMES, optional=True)
    groups, pending = {}, []
    if source is not None:
        for row in source:
            pending.append(row)
            if row["presented"]:
                if row["frame"] in groups or row["frame"] not in sync_map:
                    raise ValueError("sync-submissions.csv: invalid successful interval endpoint")
                groups[row["frame"]] = pending
                pending = []
        if any(frame not in groups for frame in endpoints):
            raise ValueError("sync-submissions.csv: missing selected presentation; evidence is incomplete")
        for endpoint, rows in groups.items():
            for key in COUNTS:
                subtotal = total(rows, key)
                if key in sync_map[endpoint] and subtotal is not None and subtotal != sync_map[endpoint][key]:
                    raise ValueError(f"sync-submissions.csv: {key} totals disagree with sync.csv")
    source_rows = [row for frame in result["selection"]["frames"] for row in groups.get(frame, [])]
    by_reason = {}
    for reason in sorted({row["reason"] for row in source_rows}):
        rows = [row for row in source_rows if row["reason"] == reason]
        by_reason[reason] = {"rows": len(rows), **{key: total(rows, key) for key in (*COUNTS, *TIMES)}}
    result["source"] = {"available": source is not None, "selected_rows": len(source_rows) if source is not None else None,
        "logged_rows": len(source) if source is not None else None,
        "other_completed_rows": len(source) - len(source_rows) - len(pending) if source is not None else None,
        "reason_totals": by_reason if source is not None else None,
        "trailing_unpresented_rows": len(pending) if source is not None else None,
        "trailing_reasons": dict(Counter(row["reason"] for row in pending)) if source is not None else None}
    if pending:
        result["warnings"].append(
            "Trailing submissions have no successful interval endpoint and are reported separately / "
            "Submissões finais sem apresentação efetiva são relatadas separadamente.")
    if source is None:
        result["warnings"].append(
            "sync-submissions.csv absent: reason breakdown and failed-frame GPU attribution unavailable / "
            "sync-submissions.csv ausente: motivos e atribuição GPU de tentativas falhas indisponíveis.")
    gpu = read_csv(directory / "gpu.csv", ("frame", "gpu_ms"), ("frame",), ("gpu_ms",), optional=True)
    result["gpu"] = {"available": gpu is not None}
    if gpu is not None:
        source_to_endpoint = {}
        for endpoint, rows in groups.items():
            for row in rows:
                frame = row["frame"]
                if frame in source_to_endpoint and source_to_endpoint[frame] != endpoint:
                    raise ValueError("sync-submissions.csv: source frame crosses successful intervals")
                source_to_endpoint[frame] = endpoint
        selected_sources = {row["frame"] for row in source_rows} if source is not None else endpoints
        expected = {}
        for row in source_rows:
            frame = row["frame"]
            if "submissions" not in row or expected.get(frame, 0) is None:
                expected[frame] = None
            else:
                expected[frame] = expected.get(frame, 0) + row["submissions"]
        samples = {}
        for row in gpu:
            if row["frame"] in selected_sources:
                samples.setdefault(row["frame"], []).append(row["gpu_ms"])
        sums = {frame: math.fsum(values) for frame, values in samples.items()}
        if any(not math.isfinite(value) for value in sums.values()):
            raise ValueError("gpu.csv: aggregate overflow")
        interval_gpu = {endpoint: [] for endpoint in endpoints}
        for frame, value in sums.items():
            interval_gpu[source_to_endpoint.get(frame, frame)].append(value)
        # EN: Never insert zero for missing GPU evidence or call a partial sum complete.
        # PT-BR: Nunca preencha amostras ausentes com zero nem declare soma parcial completa.
        complete_sources = {frame for frame, count in expected.items() if count is not None and
                            len(samples.get(frame, [])) == count}
        complete = {endpoint: math.fsum(interval_gpu[endpoint]) for endpoint in endpoints
                    if source is not None and {row["frame"] for row in groups[endpoint]} <= complete_sources}
        result["gpu"].update({"source_frame_sums_ms": sums,
            "source_samples": sum(map(len, samples.values())), "matched_source_frames": len(sums),
            "selected_source_frames": len(selected_sources), "source_ms": distribution(list(sums.values())),
            "expected_samples_by_source_frame": expected, "complete_source_frames": len(complete_sources),
            "complete_interval_sums_ms": complete, "complete_intervals": len(complete),
            "complete_interval_ms": distribution(list(complete.values()))})
    return result


def print_distribution(name, value):
    if value is None:
        print(f"{name}: unavailable / indisponível")
    else:
        print(f"{name}: n={value['count']} mean={value['mean']:.3f} P95={value['p95']:.3f} "
              f"P99={value['p99']:.3f} max={value['max']:.3f}")


def print_report(result):
    if not result["available"]:
        print("Synchronization data: unavailable / Dados de sincronização: indisponíveis")
    else:
        print(f"Successful presentations / Apresentações efetivas: {result['successful_presentations']}")
        selected_frames = result["selection"]["frames"]
        print(f"Selected frames / Frames selecionados: {selected_frames[0]}..{selected_frames[-1]}")
        print_distribution("Presentation / Apresentação (ms)", result["presentation_ms"])
        if result["fps"] is not None:
            print(f"FPS={result['fps']:.3f}; 1% low={result['one_percent_low_fps']:.3f}")
        ratio = result["submissions_per_present"]
        print("Submissions/successful present / Submissões/apresentação efetiva: " +
              (f"{ratio:.3f}" if ratio is not None else "unavailable / indisponível"))
        for key, value in result["totals"].items():
            print(f"{key}: total={value if value is not None else 'unavailable / indisponível'}")
        for key, value in result["interval_distributions"].items():
            print_distribution(f"Per successful interval / Por intervalo efetivo {key}", value)
        source = result["source"]
        if source["available"]:
            print(f"Source submissions / Submissões de origem: {source['selected_rows']}; "
                  f"logged / registradas={source['logged_rows']}; "
                  f"other completed intervals / outros intervalos completos={source['other_completed_rows']}; "
                  f"trailing without presentation / finais sem apresentação={source['trailing_unpresented_rows']}")
            for reason, metrics in source["reason_totals"].items():
                print(f"Reason / Motivo {reason}: rows={metrics['rows']} submissions={metrics['submissions']} "
                      f"queries={metrics['visibility_queries']} readbacks={metrics['readback_requests']} "
                      f"bytes={metrics['readback_bytes']}")
            for reason, count in source["trailing_reasons"].items():
                print(f"Trailing reason / Motivo final {reason}: rows={count}")
        else:
            print("Submission reasons: unavailable / Motivos das submissões: indisponíveis")
        gpu = result["gpu"]
        if gpu["available"]:
            print(f"GPU source samples / Amostras GPU: {gpu['source_samples']}; "
                  f"matched source frames / frames de origem={gpu['matched_source_frames']}/{gpu['selected_source_frames']}; "
                  f"complete intervals / intervalos completos={gpu['complete_intervals']}/{result['successful_presentations']}")
            print_distribution("GPU summed spans per source frame / Spans GPU somados por frame de origem (ms)", gpu["source_ms"])
            print_distribution("GPU summed complete interval / Intervalo GPU completo somado (ms)", gpu["complete_interval_ms"])
        else:
            print("GPU timestamp evidence: unavailable / Evidência de timestamps GPU: indisponível")
    for warning in result["warnings"]:
        print(warning)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    for key in ("after-frame", "before-frame", "after-vblank", "before-vblank", "after-tick", "before-tick"):
        parser.add_argument("--" + key, type=int, help="exclusive bound / limite exclusivo")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        result = summarize(args.directory, args.after_frame, args.before_frame, args.after_vblank,
                           args.before_vblank, args.after_tick, args.before_tick)
    except (OSError, ValueError, csv.Error, OverflowError) as error:
        parser.exit(2, f"Invalid synchronization evidence / Evidência de sincronização inválida: {error}\n")
    if args.json:
        print(json.dumps(result, indent=2, allow_nan=False))
    else:
        print_report(result)


if __name__ == "__main__":
    main()
