#!/usr/bin/env bash
# Stage 02: SDL3 diagnostic GUI around the existing, known-working headless harness.
# Does not patch sdk/, overwrite scripts/portable-dev.sh, delete game assets or commit.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD="out/pgr1-preview"
GUI="$ROOT/$BUILD/pgr1_preview"
RUN="$ROOT/.local/run/gui"

usage() {
  cat <<'HELP'
Usage: bash scripts/portable-gui.sh [build|test|window|run|help]
  build    Build SDL3 window and TGA preview viewer
  test     Run unit tests for TGA parser (no game files required)
  window   Open SDL3 window without launching the game (test display system)
  run      Start the existing headless PGR1 exe + SDL3 preview/input bridge

Prerequisites: Linux desktop (Wayland/X11); SDK's SDL3 fetched source or system SDL3 dev package.
Reference captures are CPU-generated every PGR1_REF frames (default: 30), so this
is a VISUAL DEBUGGER, not a realtime renderer or final Linux port.
HELP
}

build() {
    command -v cmake >/dev/null || { echo 'Missing cmake' >&2; exit 2; }
    cmake -S portable/linux/preview -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
    cmake --build "$BUILD" --parallel "${JOBS:-4}"
}

need_gui() { [[ -x "$GUI" ]] || build; }

version_define() {
  [[ -f assets/default.xbe ]] || { echo 'Missing assets/default.xbe' >&2; exit 2; }
  local sha
  sha="$(sha1sum assets/default.xbe | awk '{print $1}')"
  case "$sha" in
    4878c3bdbd44663b64ce64986b313e8c6b7f8917) printf '0' ;;
    ba7763e6e946ab40aed6706f2c9bc1df3a990fa0) printf '1' ;;
    0d55fff29ceb34ab4dfcfc4820e823d967ca1469) printf '2' ;;
    *) echo "Unsupported XBE SHA-1: $sha" >&2; exit 2 ;;
  esac
}

ensure_harness() {
  local release="$1"
  [[ -f generated/game/x86_func_mapping.cpp ]] || {
    echo 'Missing generated/game. Run: bash scripts/portable-dev.sh recompile' >&2; exit 2;
  }
  if [[ ! -x .local/harness/game ]]; then
    echo 'Building PGR1 headless harness for release' "$release"
    HOST_FLAGS="-DPGR1_RELEASE=$release" JOBS="${JOBS:-4}" \
      bash sdk/harness/build.sh -r . -g generated/game -o .local/harness -n game
  fi
}

run_gui() {
  need_gui
  local release
  release="$(version_define)"
  ensure_harness "$release"
  mkdir -p "$RUN/cap" .local
  # Clean only stage-02's own reference capture files; never touch smoke data.
  find "$RUN/cap" -maxdepth 1 -type f -name 'ref_*.tga' -delete
  rm -f "$RUN/cmd" "$RUN/cmd.preview.tmp"

  echo 'Starting PGR1 Linux headless + SDL3 reference preview'
  echo "  Game release: $release"
  echo "  Capture interval: ${PGR1_REF:-30} frames"
  echo "  Game log: $RUN/logs/pgr1_001.log"
  echo "  Console log: $ROOT/.local/gui-game.out"
  echo '  ESC or window close will terminate the game process.'
  (
    cd "$RUN"
    exec env REF="${PGR1_REF:-30}" FPS="${PGR1_FPS:-60}" \
      "$ROOT/.local/harness/game" "$ROOT/assets"
  ) > "$ROOT/.local/gui-game.out" 2>&1 &
  game_pid=$!
  cleanup() {
    kill "$game_pid" 2>/dev/null || true
    wait "$game_pid" 2>/dev/null || true
  }
  trap cleanup EXIT INT TERM
  "$GUI" "$RUN"
}

case "${1:-help}" in
  help|-h|--help) usage ;;
  build) build ;;
  test) need_gui; ctest --test-dir "$BUILD" --output-on-failure ;;
  window) need_gui; mkdir -p "$RUN/cap"; "$GUI" "$RUN" ;;
  run) run_gui ;;
  *) usage >&2; exit 2 ;;
esac
