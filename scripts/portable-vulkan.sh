#!/usr/bin/env bash
# PGR1 04C: native Vulkan game path and independent WSI/geometry fixtures.
# scripts/portable-linux.sh run-vulkan retains its existing Zink meaning.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT="${PGR1_VK_BUILD_DIR:-$ROOT/out/pgr1-vulkan-wsi}"
[[ "$OUT" = /* ]] || OUT="$ROOT/$OUT"
RUN="${PGR1_VK_RUN_DIR:-${PGR1_RUN_DIR:-$ROOT/.local/run/vulkan}}"
[[ "$RUN" = /* ]] || RUN="$ROOT/$RUN"

region() {
  [[ -f assets/default.xbe ]] || { echo 'Missing assets/default.xbe' >&2; return 2; }
  case "$(sha1sum assets/default.xbe | cut -d' ' -f1)" in
    4878c3bdbd44663b64ce64986b313e8c6b7f8917) echo 0 ;;
    ba7763e6e946ab40aed6706f2c9bc1df3a990fa0) echo 1 ;;
    0d55fff29ceb34ab4dfcfc4820e823d967ca1469) echo 2 ;;
    *) echo 'Unsupported XBE SHA-1: aborting to avoid incorrect hook addresses.' >&2; return 2 ;;
  esac
}

build_game() {
  [[ -f sdk/runtime/gpu/vertex.cpp ]] || { echo 'Restore the pinned sdk submodule before build-game.' >&2; return 2; }
  local release
  release="$(region)"
  for name in cmake c++ glslangValidator; do
    command -v "$name" >/dev/null || { echo "Missing dependency: $name" >&2; return 2; }
  done
  # Incremental regeneration ensures generated code matches this XBE and config.
  # The SDK checkout/revision is never changed by this command.
  [[ -f build/linux/build.ninja ]] || bash scripts/portable-dev.sh linux-tools
  bash scripts/portable-dev.sh recompile
  cmake -S portable/linux/vulkan -B "$OUT" -DCMAKE_BUILD_TYPE=Release \
    -DPGR1_VK_BUILD_GAME=ON -DPGR1_RELEASE="$release"
  cmake --build "$OUT" --parallel "${JOBS:-4}"
  sha1sum assets/default.xbe | cut -d' ' -f1 > "$OUT/game-xbe.sha1"
}

run_game() {
  local digest recorded='' gpu_choice="${PGR1_VK_DEVICE:-auto}"
  region >/dev/null
  digest="$(sha1sum assets/default.xbe | cut -d' ' -f1)"
  [[ ! -f "$OUT/game-xbe.sha1" ]] || recorded="$(cat "$OUT/game-xbe.sha1")"
  if [[ -x "$OUT/pgr1_vk_game" && "$digest" == "$recorded" ]]; then
    # Source/shader/CMake changes must not run a stale binary simply because
    # default.xbe is unchanged. Ninja/Make performs an incremental build.
    cmake --build "$OUT" --parallel "${JOBS:-4}" --target pgr1_vk_game
  else
    build_game
  fi
  mkdir -p "$RUN/logs"
  RUN="$(realpath "$RUN")"
  (
    flock -n 9 || { echo "A game already owns $RUN" >&2; exit 2; }
    cd "$RUN"
    echo 'Backend: native Vulkan (SDK NV2A -> HLSL -> SPIR-V, real recompiled PGR1)'
    echo "Run directory: $RUN"
    # EN: Display the selection mode; the Vulkan backend identifies the real
    # compatible GPU after checking device and presentation capabilities.
    # PT-BR: Informe o modo; o backend Vulkan confirma a GPU compatível
    # após verificar as capacidades de renderização e apresentação.
    if [[ "${gpu_choice,,}" == "auto" ]]; then
      echo "GPU Vulkan / Placa de vídeo: automática (preferir GPU dedicada compatível)"
    else
      echo "GPU Vulkan / Placa de vídeo: seleção manual: $gpu_choice"
    fi
    # Leave the saved menu cap effective unless an explicit diagnostic override
    # is requested. FPS itself is also accepted for existing harness workflows.
    if [[ -n "${PGR1_FPS+x}" ]]; then
      FPS="$PGR1_FPS" "$OUT/pgr1_vk_game" "$ROOT/assets"
    else
      "$OUT/pgr1_vk_game" "$ROOT/assets"
    fi
  ) 9>"$RUN/run.lock" 2>&1 | tee "$RUN/console.log"
}

case "${1:-help}" in
  doctor)
    for name in cmake c++ vulkaninfo; do
      if command -v "$name" >/dev/null; then echo "$name: OK"; else echo "$name: missing"; fi
    done
    if [[ -e /usr/include/vulkan/vulkan.h ]]; then echo 'Vulkan headers: system installed';
    else echo 'Vulkan headers: check CMake Vulkan::Headers detection'; fi
    # EN: Lists ICD-visible GPUs; actual compatibility is checked by Vulkan.
    # PT-BR: Lista GPUs visíveis ao ICD; o Vulkan valida a compatibilidade real.
    echo 'GPU mode / Modo de GPU: automatic by default; PGR1_VK_DEVICE=auto or manual substring'
    command -v vulkaninfo >/dev/null && vulkaninfo --summary || true
    ;;
  build)
    cmake -S portable/linux/vulkan -B "$OUT" -DCMAKE_BUILD_TYPE=Release -DPGR1_VK_BUILD_GAME=OFF
    cmake --build "$OUT" --parallel "${JOBS:-4}"
    ;;
  build-game) build_game ;;
  run-game) run_game ;;
  draw-test)
    [[ -x "$OUT/pgr1_vk_wsi" ]] || { echo 'Run build first' >&2; exit 2; }
    "$OUT/pgr1_vk_wsi" --geometry-test "${@:2}"
    ;;
  test)
    [[ -x "$OUT/pgr1_vk_guest_frame_test" ]] || { echo 'Run build first' >&2; exit 2; }
    ctest --test-dir "$OUT" --output-on-failure
    ;;
  smoke)
    [[ -x "$OUT/pgr1_vk_wsi" ]] || { echo 'Not built. First run: bash scripts/portable-vulkan.sh build' >&2; exit 2; }
    "$OUT/pgr1_vk_wsi" "${@:2}"
    ;;
  triage)
    python3 portable/tools/vulkan_triage.py "${2:-.local/run/linux}" "${@:3}"
    ;;
  help|-h|--help)
    cat <<'HELP'
PGR1 04C — NATIVE VULKAN GAME + INDEPENDENT FIXTURE TESTS
  bash scripts/portable-vulkan.sh doctor
  bash scripts/portable-vulkan.sh build
  bash scripts/portable-vulkan.sh smoke --frames 180      # 04A clears only
  bash scripts/portable-vulkan.sh test                    # geometry, capture and optional SDK/Vulkan pixel tests
  bash scripts/portable-vulkan.sh draw-test --frames 180  # native Vulkan GPU vertex/index draw
  bash scripts/portable-vulkan.sh build-game             # recompiled Xbox game + native Vulkan
  bash scripts/portable-vulkan.sh run-game               # automatic compatible GPU (default)
  PGR1_VK_DEVICE=auto bash scripts/portable-vulkan.sh run-game  # explicit automatic mode
  PGR1_VK_DEVICE='RX 580' bash scripts/portable-vulkan.sh run-game # optional manual GPU
  PGR1_VK_DEVICE='RTX 3060 Ti' bash scripts/portable-vulkan.sh run-game # optional manual GPU
  bash scripts/portable-vulkan.sh triage .local/run/linux
Requires: SDL3 >=3.2, Vulkan loader/headers, glslangValidator, GPU Vulkan ICD.
build-game additionally requires the pinned SDK, supported assets/default.xbe and compiler tools.
EN: Unset/empty PGR1_VK_DEVICE or 'auto' (case insensitive) chooses a compatible
    graphics+present GPU, prioritizing a discrete GPU (RTX/Radeon, etc.).
PT-BR: Sem PGR1_VK_DEVICE ou com 'auto', o Vulkan procura uma GPU compatível,
       preferindo placa dedicada; nomes explícitos continuam disponíveis.
Manual overrides match part of a Vulkan device name (case insensitive).
IMPORTANT: draw-test displays a generated geometry FIXTURE, not an actual PGR1 race.
IMPORTANT: scripts/portable-linux.sh run-vulkan uses Zink (OpenGL over Vulkan).
run-game executes the game; acceptance still requires reviewing its draw counters and images.
Run options: PGR1_RUN_SECONDS=60 OUTPUT=1280x720 PGR1_FULLSCREEN=0 PGR1_CAPTURE=frame.ppm
             SCRIPT=/absolute/input-script.txt PGR1_SCRIPT_ONLY=1 PGR1_VK_RUN_DIR=/path/to/run
             PGR1_CAPTURE_FRAME=300 PGR1_CAPTURE_FRAME_PATH=capture-frame.ppm
             PGR1_VK_PIPELINED=0 # diagnostic: restore per-submission GPU fence wait
             PGR1_VK_SHADER_WORKERS=1 # diagnostic: single shader worker
             PGR1_VK_BATCH_EXTERNAL=0 # EN/PT-BR: disable external-source batching / desativar agrupamento de origens externas
             # PT-BR: 0 restaura a espera sincrona em cada submissao
             PGR1_COMPARE_FRAME=300 # optional CPU reference and Vulkan images/error report
Every submission and requested frame is logged to submissions.csv and frames.csv, including rejected draws.
HELP
    ;;
  *) echo "Unknown action: $1" >&2; exit 2 ;;
esac
