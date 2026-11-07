#!/usr/bin/env bash
# PGR1 Stage 03: SDL3 OpenGL bringup + zero-TGA reference framebuffer transport.
# Does not modify SDK, existing headless build or the stage 02 GUI.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD=out/pgr1-gl-bridge
BRIDGE="$ROOT/$BUILD/pgr1_stream_viewer"
GLPROBE="$ROOT/$BUILD/pgr1_gl_probe"
GAME_OUT=.local/harness_stream
RUN="$ROOT/.local/run/glbridge"

version_define() {
    [[ -f assets/default.xbe ]] || { echo 'Missing assets/default.xbe' >&2; exit 2; }
    case "$(sha1sum assets/default.xbe | awk '{print $1}')" in
      4878c3bdbd44663b64ce64986b313e8c6b7f8917) echo 0 ;;
      ba7763e6e946ab40aed6706f2c9bc1df3a990fa0) echo 1 ;;
      0d55fff29ceb34ab4dfcfc4820e823d967ca1469) echo 2 ;;
      *) echo 'Unsupported XBE region/hash; aborting to avoid wrong hooks.' >&2; exit 2 ;;
    esac
}

build() {
    cmake -S portable/linux/stream -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
    cmake --build "$BUILD" --parallel "${JOBS:-4}"
}

need_bridge() { [[ -x "$BRIDGE" && -x "$GLPROBE" ]] || build; }

build_game() {
    [[ -f generated/game/x86_func_mapping.cpp ]] || { echo 'Generated code missing; run scripts/portable-dev.sh recompile first.' >&2; exit 2; }
    # The SDK headless compiler is patched in Stage 01 to support spaces and -x/-e/-G.
    # Reuse generated x86 objects without modifying the known-good .local/harness/game.
    local release
    release="$(version_define)"
    echo "Building separate reference-IPC backend for region $release"
    mkdir -p .local/harness_stream
    HOST_FLAGS="-DPGR1_RELEASE=$release" JOBS="${JOBS:-4}" \
      bash sdk/harness/build.sh \
        -r . -g generated/game -o "$GAME_OUT" -n game \
        -G .local/harness/gen \
        -x '/harness/null_backend[.]cpp$' \
        -e portable/linux/stream/backend.cpp \
        -e portable/linux/stream/frame_ipc.cpp
}

run() {
    need_bridge
    [[ -x "$GAME_OUT/game" ]] || build_game
    mkdir -p "$RUN" .local
    local frame_file
    local frame_dir=/dev/shm
    if [[ ! -d "$frame_dir" || ! -w "$frame_dir" ]]; then
        frame_dir="$ROOT/.local"
        echo 'WARNING: /dev/shm unavailable; mmap file may be written back to disk.' >&2
    fi
    frame_file="$(mktemp "$frame_dir/pgr1-frame.XXXXXXXX")"
    echo "PGR1 diagnostic IPC viewer: $frame_file"
    echo "Game release: $(version_define)   Reference interval: ${PGR1_REF:-30} frames"
    echo "Game stderr/stdout: .local/glbridge-game.out"
    (
        cd "$RUN"
        exec env PGR1_REF="${PGR1_REF:-30}" PGR1_FRAME_FILE="$frame_file" \
            FPS="${PGR1_FPS:-60}" "$ROOT/$GAME_OUT/game" "$ROOT/assets"
    ) > "$ROOT/.local/glbridge-game.out" 2>&1 &
    game_pid=$!
    cleanup() {
        kill "$game_pid" 2>/dev/null || true
        wait "$game_pid" 2>/dev/null || true
        rm -f -- "$frame_file"
    }
    trap cleanup EXIT INT TERM
    "$BRIDGE" "$RUN" "$frame_file"
}

case "${1:-help}" in
  build) build ;;
  test) need_bridge; ctest --test-dir "$BUILD" --output-on-failure ;;
  gl-test) need_bridge; "$GLPROBE" ;;
  build-game) build_game ;;
  run) run ;;
  help|-h|--help)
    cat <<'HELP'
Usage: bash scripts/portable-gl.sh [build|test|gl-test|build-game|run]
  build       Build SDL3 memory viewer + standalone OpenGL GLSL test
  test        Test mmap frame protocol
  gl-test     Show GPU-rendered GLSL triangle (Escape closes)
  build-game  Build isolated PGR1 reference-IPC backend using existing SDK
  run         Run CPU-reference PGR1 with shared memory SDL3 preview + PS4 pad

PGR1_REF=30 chooses CPU captures every 30 game frames.
No per-frame TGA files are used. This is NOT yet a native OpenGL Xbox renderer.
HELP
    ;;
  *) echo 'Unknown command' >&2; exit 2 ;;
esac
