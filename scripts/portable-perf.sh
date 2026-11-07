#!/usr/bin/env bash
# PGR1 Portable Stage 03B: isolated -O3 host + -O2 generated recompilation benchmark.
# Requires Stage 03A and its SDL3 IPC viewer. Does not overwrite the known-good binaries.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
GAME_OUT=.local/harness_perf
GEN_OUT=.local/harness/gen_perf_o2
RUN="$ROOT/.local/run/perf"
VIEWER="$ROOT/out/pgr1-gl-bridge/pgr1_stream_viewer"

region_define() {
    [[ -f assets/default.xbe ]] || { echo 'Missing assets/default.xbe' >&2; exit 2; }
    case "$(sha1sum assets/default.xbe | awk '{print $1}')" in
      4878c3bdbd44663b64ce64986b313e8c6b7f8917) echo 0 ;;
      ba7763e6e946ab40aed6706f2c9bc1df3a990fa0) echo 1 ;;
      0d55fff29ceb34ab4dfcfc4820e823d967ca1469) echo 2 ;;
      *) echo 'Unrecognized XBE SHA-1. Not compiling with an unsafe hook map.' >&2; exit 2 ;;
    esac
}

build_game() {
    [[ -f generated/game/x86_func_mapping.cpp ]] || {
        echo 'Generated C++ game missing; run scripts/portable-dev.sh recompile' >&2; exit 2;
    }
    [[ -f portable/linux/stream/frame_ipc.cpp && -f portable/linux/perf/backend.cpp ]] || {
        echo 'Stage 03A/03B source missing.' >&2; exit 2;
    }
    local release
    release="$(region_define)"
    echo "Compiling isolated profiled PGR1 (PGR1_RELEASE=$release, host -O3, generated -O2)"
    echo 'This separate optimization build can take longer and use more RAM than the reference harness.'
    mkdir -p "$GAME_OUT" "$GEN_OUT"
    # The SDK baseline compiles host files with -O1 -g. Later -O3 -g0 override for THIS build only.
    HOST_FLAGS="-DPGR1_RELEASE=$release -O3 -g0" GEN_OPT="-O2" JOBS="${JOBS:-4}" \
      bash sdk/harness/build.sh -r . -g generated/game -o "$GAME_OUT" -n game \
      -G "$GEN_OUT" -x '/harness/null_backend[.]cpp$' \
      -e portable/linux/perf/backend.cpp \
      -e portable/linux/stream/frame_ipc.cpp
}

run() {
    [[ -x "$VIEWER" ]] || { echo "Stage 03A GUI missing: bash scripts/portable-gl.sh build" >&2; exit 2; }
    [[ -x "$GAME_OUT/game" ]] || build_game
    mkdir -p "$RUN" .local
    local tmpdir=/dev/shm
    [[ -d "$tmpdir" && -w "$tmpdir" ]] || tmpdir="$ROOT/.local"
    local frame_file
    frame_file="$(mktemp "$tmpdir/pgr1-perf-frame.XXXXXXXX")"
    echo "Running PGR1 performance preview: interval=${PGR1_REF:-60} budget=${PGR1_REF_BUDGET_MS:-8}ms/game-frame"
    echo "CSV: .local/run/perf/pgr1_perf.csv"
    echo "Game output: .local/perf-game.out"
    (
        cd "$RUN"
        exec env PGR1_REF="${PGR1_REF:-60}" \
            PGR1_REF_ADAPT="${PGR1_REF_ADAPT:-1}" \
            PGR1_REF_MIN="${PGR1_REF_MIN:-30}" PGR1_REF_MAX="${PGR1_REF_MAX:-600}" \
            PGR1_REF_BUDGET_MS="${PGR1_REF_BUDGET_MS:-8}" \
            PGR1_FRAME_FILE="$frame_file" FPS="${PGR1_FPS:-60}" \
            "$ROOT/$GAME_OUT/game" "$ROOT/assets"
    ) > "$ROOT/.local/perf-game.out" 2>&1 &
    local pid=$!
    cleanup() {
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        rm -f -- "$frame_file"
    }
    trap cleanup EXIT INT TERM
    "$VIEWER" "$RUN" "$frame_file"
}

report() {
    local csv="$RUN/pgr1_perf.csv"
    [[ -f "$csv" ]] || { echo "No benchmark available: $csv" >&2; exit 2; }
    python3 - "$csv" <<'PY'
import csv, statistics, sys
with open(sys.argv[1], newline='') as f:
    rows = list(csv.DictReader(f))
if not rows:
    raise SystemExit('CSV exists, but no sampled frame has completed. Run for longer or reduce PGR1_REF.')
metrics = ['reference_draw_ms', 'reference_clear_ms', 'reference_copy_ms', 'decode_ms', 'ipc_ms', 'sampled_wall_ms']
print(f'Captured CPU-reference frames: {len(rows)}')
print(f'First/last capture: {rows[0]["capture_frame"]} / {rows[-1]["capture_frame"]}')
for key in metrics:
    numbers = [float(r[key]) for r in rows]
    n = len(numbers)
    p95 = sorted(numbers)[max(0, (95*n+99)//100 - 1)]
    print(f'  {key:24} avg={statistics.mean(numbers):9.2f} ms  p95={p95:9.2f} ms  max={max(numbers):9.2f} ms')
print(f'Capture interval: {rows[0]["interval"]} -> {rows[-1]["next_interval"]} game frames')
print('Note: sampled_wall_ms includes the game CPU work as well as reference rendering; it is not GPU time.')
PY
}

case "${1:-help}" in
    build-game) build_game ;;
    run) run ;;
    report) report ;;
    help|-h|--help)
      cat <<'HELP'
PGR1 Stage 03B — Performance prototype (still a CPU rasterizer)
  bash scripts/portable-perf.sh build-game   # isolated optimized harness
  bash scripts/portable-perf.sh run          # interactive PS4/SDL3 preview + profiling
  bash scripts/portable-perf.sh report       # mean, p95, max time per reference capture
Options: JOBS=4, PGR1_REF=60, PGR1_REF_BUDGET_MS=8,
         PGR1_REF_ADAPT=0 (disable adaptive sampling), PGR1_REF_MIN=30, PGR1_REF_MAX=600
This does not replace the original headless or Stage 03A binaries.
HELP
      ;;
    *) echo "Unsupported command: $1" >&2; exit 2 ;;
esac
