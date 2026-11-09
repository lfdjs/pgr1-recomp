#!/usr/bin/env bash
# PGR1 04B: native Vulkan WSI plus real GPU indexed geometry fixture.
# NOTE: NOT a native Vulkan PGR1 renderer. Never aliases run-vulkan (Zink).
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT="${PGR1_VK_BUILD_DIR:-$ROOT/out/pgr1-vulkan-wsi}"
case "${1:-help}" in
  doctor)
    for name in cmake c++ vulkaninfo; do
      if command -v "$name" >/dev/null; then echo "$name: OK"; else echo "$name: missing"; fi
    done
    if [[ -e /usr/include/vulkan/vulkan.h ]]; then echo 'Vulkan headers: system installed';
    else echo 'Vulkan headers: check CMake Vulkan::Headers detection'; fi
    command -v vulkaninfo >/dev/null && vulkaninfo --summary || true
    ;;
  build)
    cmake -S portable/linux/vulkan -B "$OUT" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$OUT" --parallel "${JOBS:-4}"
    ;;
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
PGR1 04B — NATIVE VULKAN WSI + INDEXED GEOMETRY TESTS (NOT GAME RENDERING)
  bash scripts/portable-vulkan.sh doctor
  bash scripts/portable-vulkan.sh build
  bash scripts/portable-vulkan.sh smoke --frames 180      # 04A clears only
  bash scripts/portable-vulkan.sh test                    # pure geometry model
  bash scripts/portable-vulkan.sh draw-test --frames 180  # native Vulkan GPU vertex/index draw
  bash scripts/portable-vulkan.sh triage .local/run/linux
Requires: SDL3 >=3.2, Vulkan loader/headers, glslangValidator, GPU Vulkan ICD.
PGR1_VK_DEVICE='RX 580' selects AMD RX 580, when available. Otherwise prefers discrete GPU.
IMPORTANT: draw-test displays a generated geometry FIXTURE, not an actual PGR1 race.
IMPORTANT: scripts/portable-linux.sh run-vulkan uses Zink (OpenGL over Vulkan).
Do not use this test to claim Xbox geometry/textures are supported in native Vulkan.
HELP
    ;;
  *) echo "Unknown action: $1" >&2; exit 2 ;;
esac
