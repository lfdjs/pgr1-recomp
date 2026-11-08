#!/usr/bin/env bash
# PGR1 Portable: single-process experimental OpenGL backend.
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
  find_sdl
  local region
  region="$(region_define)"
  echo "Building isolated GPU executable (PGR1_RELEASE=$region)"
  cmake -S portable/linux/gldirect -B "$OUT" -DCMAKE_BUILD_TYPE=Release \
    -DPGR1_RELEASE="$region" -DPGR1_SDL_INCLUDE="$SDL_INCLUDE" -DPGR1_SDL_LIBRARY="$SDL_SO"
  cmake --build "$OUT" --parallel "${JOBS:-4}"

}

run_game() {
  [[ -x "$OUT/game" ]] || build_game
  find_sdl
  mkdir -p "$RUN/logs"
  echo 'Starting PGR1: experimental FVF XYZRHW GPU renderer.'
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
PGR1: direct single-process SDL3/OpenGL experimental renderer
  bash scripts/portable-gldirect.sh test        # bridge, owned commands, optional EGL pixel and SDK tests
  JOBS=4 bash scripts/portable-gldirect.sh build-game
  bash scripts/portable-gldirect.sh run         # experimental geometry/textures, PS4 input

FVF XYZRHW draws use the GPU. Programmable shaders remain unsupported and logged.
Preserve 03B reference preview for comparison.
HELP
    ;;
  *) echo "Unknown action: $1" >&2; exit 2 ;;
esac
