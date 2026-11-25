#!/usr/bin/env python3
"""EN: Analyze CPU texture scanning/decoding from Vulkan frames.csv.
PT-BR: Analisa o custo de comparação/decodificação de texturas na CPU.

Example / Exemplo: python3 portable/tools/texture_report.py .local/run/vulkan --window 300
"""
import argparse
import csv
import math
from pathlib import Path
import statistics
import sys

COUNTS = (
    "texture_static_binds", "texture_memcmp_checks", "texture_memcmp_bytes",
    "texture_same_draw_reuses", "texture_decodes",
)
TIMES = ("texture_check_ms", "decode_ms", "capture_ms", "frame_ms")
# EN: Optional 04D.8 counters; old CSVs must still open.
# PT-BR: Contadores opcionais da 04D.8; CSVs antigos devem continuar válidos.
MIP_COUNTS = ("texture_mip_levels_decoded", "texture_mip_levels_reused")
# EN: Optional cache breakdown from 04D.9, backward-compatible with older CSV.
# PT-BR: Indicadores opcionais 04D.9; manter suporte aos CSVs anteriores.
CACHE_COUNTS = ("texture_cache_evictions", "texture_cache_misses",
                "texture_descriptor_redecodes", "texture_guest_redecodes",
                "texture_cache_entries_peak")
# EN: 04D.19 optional columns; predictions also include uncovered observations.
# PT-BR: Colunas opcionais 04D.19; previsões incluem observações sem cobertura.
TRACKER_COUNTS = (
    "texture_tracker_queries", "texture_tracker_fast_hits", "texture_tracker_invalidations",
    "texture_tracker_exact_fallbacks", "texture_tracker_uncovered_ranges",
    "texture_tracker_false_positives", "texture_tracker_false_negatives",
    "texture_tracker_monitored_writes", "texture_tracker_dirty_pages",
    "texture_tracker_tracked_bytes", "texture_tracker_covered_queries",
)
TRACKER_TIMES = ("texture_tracker_ms", "texture_tracker_write_ms")
JOURNAL_COUNTS = ("texture_journal_clean",
                  "texture_journal_dirty",
                  "texture_journal_unknown",
                  "texture_journal_false_negatives",
                  "texture_journal_false_positives",
                  "texture_journal_unstable",)
JOURNAL_TIMES = ("texture_journal_ms",)


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
                optional = {key: int(row[key]) for key in (*MIP_COUNTS, *CACHE_COUNTS, *TRACKER_COUNTS, *JOURNAL_COUNTS,
                            "texture_overlap_checks", "texture_diagnostic_skips") if key in fields}
                optional_times = {key: float(row[key]) for key in (*TRACKER_TIMES, *JOURNAL_TIMES) if key in fields}
                if any(not math.isfinite(value) for value in optional_times.values()):
                    raise ValueError("non-finite tracker time")
                if any(value < 0 for value in optional_times.values()):
                    raise ValueError("negative tracker time")
                for key in ("texture_tracker_covered_queries", "texture_tracker_fast_hits"):
                    if key in optional and "texture_tracker_queries" in optional and optional[key] > optional["texture_tracker_queries"]:
                        raise ValueError(f"{key} exceeds tracker queries")
                if all(key in optional for key in JOURNAL_COUNTS):
                    if optional["texture_journal_false_negatives"] > optional["texture_journal_clean"] or optional["texture_journal_false_positives"] > optional["texture_journal_dirty"]:
                        raise ValueError("journal errors exceed classified predictions")
                    if sum(optional["texture_journal_"+key] for key in ("clean","dirty","unknown")) > counts["texture_memcmp_checks"]:
                        raise ValueError("journal predictions exceed exact comparisons")
                if any(value < 0 for value in optional.values()):
                    raise ValueError("negative optional counter")
            except (TypeError, ValueError) as exc:
                errors.append(f"line {lineno}: {exc}")
                continue
            if frame > after and (before is None or frame < before):
                records.append({"frame": frame, **counts, **times, **optional, **optional_times})
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
    if "texture_overlap_checks" in rows[0]:
        overlap = sum(row["texture_overlap_checks"] for row in rows)
        print(f"  Exact comparisons overlapped / Comparações exatas sobrepostas: {overlap:,} "
              f"({overlap/n:.1f}/frame)")
    if "texture_diagnostic_skips" in rows[0]:
        skipped = sum(row["texture_diagnostic_skips"] for row in rows)
        print(f"  UNSAFE diagnostic skips / Omissões de diagnóstico inseguro: {skipped:,} "
              f"({skipped/n:.1f}/frame)")
        if skipped:
            print("  WARNING / AVISO: stale textures possible; NOT a correctness result.")
    print(f"  Requested memcmp scan: {total['texture_memcmp_bytes']/1048576:.1f} MiB total, "
          f"{total['texture_memcmp_bytes']/n/1048576:.1f} MiB/frame upper bound")
    print(f"  Decoded texture snapshots: {total['texture_decodes']:,} "
          f"({total['texture_decodes']/n:.2f}/frame)")
    if all(key in rows[0] for key in MIP_COUNTS):
        decoded = sum(row["texture_mip_levels_decoded"] for row in rows)
        reused = sum(row["texture_mip_levels_reused"] for row in rows)
        print(f"  Mipmap levels / Níveis de mipmap: decoded/decodificados={decoded:,}; "
              f"reused/reutilizados={reused:,} "
              f"({100*reused/max(decoded+reused,1):.2f}% of refreshed levels)")
    if all(key in rows[0] for key in CACHE_COUNTS):
        evictions = sum(row["texture_cache_evictions"] for row in rows)
        cold = sum(row["texture_cache_misses"] for row in rows)
        layout = sum(row["texture_descriptor_redecodes"] for row in rows)
        guest = sum(row["texture_guest_redecodes"] for row in rows)
        peak = max(row["texture_cache_entries_peak"] for row in rows)
        print(f"  Cache / Cache: evictions/remoções={evictions:,}; "
              f"cold decodes/carregamentos iniciais={cold:,}; "
              f"descriptor changes/mudanças de descritor={layout:,}; "
              f"guest byte changes/alterações de bytes={guest:,}; "
              f"peak entries/pico de entradas={peak:,}")
        if cold+layout+guest != total["texture_decodes"]:
            print("  WARNING / AVISO: decode reasons differ from texture_decodes")
    if all(key in rows[0] for key in TRACKER_COUNTS):
        tracker = {key: sum(row[key] for row in rows) for key in TRACKER_COUNTS}
        queries = tracker["texture_tracker_queries"]
        covered = tracker["texture_tracker_covered_queries"]
        hits = tracker["texture_tracker_fast_hits"]
        exact = total["texture_memcmp_checks"]
        print(f"  Tracker / Rastreador: queries/consultas={queries:,}; "
              f"covered/com cobertura={covered:,} ({100*covered/max(queries,1):.2f}%); "
              f"uncovered observations/observações sem cobertura={tracker['texture_tracker_uncovered_ranges']:,}")
        print(f"  Proven fast hits / Acertos comprovados={hits:,} "
              f"({100*hits/max(queries,1):.2f}% of queries/das consultas); "
              f"exact fallbacks/fallbacks exatos={tracker['texture_tracker_exact_fallbacks']:,}; "
              f"predicted invalidations/invalidações previstas={tracker['texture_tracker_invalidations']:,}")
        print(f"  Comparisons avoided by proof / Comparações evitadas por prova: {hits:,} "
              f"({100*hits/max(hits+exact,1):.2f}% of exact+proven checks/das verificações exatas+comprovadas)")
        print(f"  Advisory prediction errors / Erros de previsão consultiva: "
              f"false positives/falsos positivos={tracker['texture_tracker_false_positives']:,}; "
              f"false negatives/falsos negativos={tracker['texture_tracker_false_negatives']:,}")
        print("  EN/PT-BR: Errors include uncovered predictions; zero errors does not prove writer coverage / "
              "Erros incluem previsões sem cobertura; zero erros não comprova cobertura dos escritores.")
        peak = max(row["texture_tracker_tracked_bytes"] for row in rows)
        print(f"  Monitored writes / Escritas monitoradas={tracker['texture_tracker_monitored_writes']:,}; "
              f"dirty-page visits/visitas a páginas sujas={tracker['texture_tracker_dirty_pages']:,} "
              f"(upper bound, repeated pages/limite superior, páginas repetidas); "
              f"peak registered bytes/pico de bytes registrados={peak:,} ({peak/1048576:.2f} MiB)")
    if all(key in rows[0] for key in TRACKER_TIMES):
        combined = [sum(row[key] for key in TRACKER_TIMES) for row in rows]
        print(f"  Tracker reader+writer cost / Custo leitor+escritor: "
              f"mean/média {statistics.mean(combined):.3f} ms/frame; "
              f"total {sum(combined):.3f} ms")
        print("  EN/PT-BR: Tracker times may nest in capture/check times; do not add them again / "
              "Tempos do tracker podem estar na captura/verificação; não os some novamente.")
    if all(key in rows[0] for key in JOURNAL_COUNTS):
        journal = {key: sum(row["texture_journal_"+key] for row in rows)
                   for key in ("clean","dirty","unknown","false_negatives","false_positives","unstable")}
        queries = journal["clean"]+journal["dirty"]+journal["unknown"]
        if queries or journal["unstable"]:
            print(f"  Journal shadow audit / Auditoria em sombra: clean/limpas={journal['clean']:,}; dirty/sujas={journal['dirty']:,}; unknown/desconhecidas={journal['unknown']:,}")
            print(f"  Candidate clean / Candidatas limpas: {100*journal['clean']/max(queries,1):.3f}% of classified checks; NOT skipped / NÃO omitidas")
            print(f"  Journal errors / Erros do diário: false negatives/falsos negativos={journal['false_negatives']:,}; false positives/falsos positivos={journal['false_positives']:,}; unstable/instáveis={journal['unstable']:,}")
            print("  EN/PT-BR: Advisory only; exact comparisons retained / Apenas informativo; comparações exatas preservadas.")
    for key in (*TIMES, *(key for key in (*TRACKER_TIMES, *JOURNAL_TIMES) if key in rows[0])):
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
