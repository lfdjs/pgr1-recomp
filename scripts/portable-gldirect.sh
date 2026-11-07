#!/usr/bin/env bash
# PGR1 Portable 03C.1: one-process diagnostic OpenGL backend.
# Builds in its own output directory. Existing 03A/03B and headless executables are not touched.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT=.local/harness_gldirect
RUN="$ROOT/.local/run/gldirect"
TEST=out/pgr1-gldirect-tests

region_define() {
  [[ -f assets/default.xbe ]] || { echo 'Missing assets/default.xbe' >&2; exit 2; }
  case "$(sha1sum assets/default.xbe | awk '{print $1}')" in
    4878c3bdbd44663b64ce64986b313e8c6b7f8917) echo 0 ;;
    ba7763e6e946ab40aed6706f2c9bc1df3a990fa0) echo 1 ;;
    0d55fff29ceb34ab4dfcfc4820e823d967ca1469) echo 2 ;;
    *) echo 'Unsupported XBE SHA-1: aborting to avoid incorrect hook addresses.' >&2; exit 2 ;;
  esac
}

find_sdl() {
  # The Stage 03A build has a SDL3 shared library, built from the SDK's already cached sources.
  SDL_SO="$(find -L out/pgr1-gl-bridge -type f -name 'libSDL3.so*' -print -quit 2>/dev/null || true)"
  if [[ -z $SDL_SO ]]; then
    echo 'SDL3 shared library missing; compiling Stage 03A GUI first...' >&2
    bash scripts/portable-gl.sh build
    SDL_SO="$(find -L out/pgr1-gl-bridge -type f -name 'libSDL3.so*' -print -quit 2>/dev/null || true)"
  fi
  [[ -n $SDL_SO && -f $SDL_SO ]] || { echo 'Cannot locate built libSDL3.so under out/pgr1-gl-bridge' >&2; exit 1; }
  SDL_SO="$(realpath "$SDL_SO")"
  SDL_LIBDIR="$(dirname -- "$SDL_SO")"
  if [[ -f /usr/include/SDL3/SDL.h ]]; then
    SDL_INCLUDE=/usr/include
  elif [[ -f build/linux/_deps/sdl3-src/include/SDL3/SDL.h ]]; then
    SDL_INCLUDE="$(realpath build/linux/_deps/sdl3-src/include)"
  else
    echo 'SDL3 headers not found (expected SDK cached source or system headers)' >&2; exit 1
  fi
}

build_game() {
  [[ -f generated/game/x86_func_mapping.cpp ]] || { echo 'Generate code first: scripts/portable-dev.sh recompile' >&2; exit 2; }
  grep -q 'PGR1_EXTRA_LINK_LIB' sdk/harness/build.sh || { echo 'SDK linker extension missing: run Etapa 03C installer.' >&2; exit 2; }
  find_sdl
  local region
  region="$(region_define)"
  echo "Building isolated 03C.1 GPU diagnostic executable (PGR1_RELEASE=$region)"
  echo "SDL3 library: $SDL_SO"
  mkdir -p "$OUT" .local/harness/gen
  HOST_FLAGS="-DPGR1_RELEASE=$region -O2 -g0" \
    PGR1_EXTRA_INCLUDE_DIR="$SDL_INCLUDE" \
    PGR1_EXTRA_LINK_LIB="$SDL_SO" \
    JOBS="${JOBS:-4}" bash sdk/harness/build.sh \
      -r . -g generated/game -o "$OUT" -n game \
      -G .local/harness/gen \
      -x '/harness/main_test[.]cpp$' \
      -x '/harness/null_backend[.]cpp$' \
      -e portable/linux/gldirect/main.cpp \
      -e portable/linux/gldirect/backend.cpp \
      -e portable/linux/gldirect/bridge.cpp
}

run_game() {
  [[ -x "$OUT/game" ]] || build_game
  find_sdl
  mkdir -p "$RUN/logs"
  echo 'Starting PGR1: GPU-clear diagnostic only (no guest triangles/textures yet).'
  echo 'DualShock 4 supported. SDL3 / OpenGL is now in the same process as the Xbox runtime.'
  echo 'Logs: .local/run/gldirect/logs and .local/gldirect.out'
  (
    cd "$RUN"
    LD_LIBRARY_PATH="$SDL_LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
      FPS="${PGR1_FPS:-60}" "$ROOT/$OUT/game" "$ROOT/assets"
  ) 2>&1 | tee .local/gldirect.out
}

tests() {
  cmake -S portable/linux/gldirect/tests -B "$TEST" -DCMAKE_BUILD_TYPE=Debug
  cmake --build "$TEST" --parallel "${JOBS:-4}"
  ctest --test-dir "$TEST" --output-on-failure
}
case "${1:-help}" in
  build-game) build_game ;;
  run) run_game ;;
  test) tests ;;
  help|-h|--help)
    cat <<'HELP'
PGR1 03C.1: direct single-process SDL3/OpenGL GPU diagnostic
  bash scripts/portable-gldirect.sh test        # atomic game->GL frame bridge test
  JOBS=4 bash scripts/portable-gldirect.sh build-game
  bash scripts/portable-gldirect.sh run         # solid colors, stats in title, PS4 input

IMPORTANT: guest Draw is counted, not rendered. This is intentionally not a playable
GPU backend. Preserve 03B reference preview as a way to see the actual game graphics.
HELP
    ;;
  *) echo "Unknown action: $1" >&2; exit 2 ;;
esac
