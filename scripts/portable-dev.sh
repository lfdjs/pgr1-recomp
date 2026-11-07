#!/usr/bin/env bash
# PGR1 Portable: predictable steps using the existing XboxXBERecomp scripts.
# No game files or generated code are uploaded. No automatic commit/push.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

usage() {
  cat <<'HELP'
PGR1 Portable — developer CLI
  bash scripts/portable-dev.sh doctor        Read-only environment check
  bash scripts/portable-dev.sh test          Compile/test portable host and SDK header contracts
  bash scripts/portable-dev.sh linux-tools   Build XBE compiler tools (requires SDK + assets)
  bash scripts/portable-dev.sh recompile     Translate your supported default.xbe to C++
  bash scripts/portable-dev.sh smoke [secs]  Run existing headless smoke test (default 240 s)
  bash scripts/portable-dev.sh all           Doctor + tests + tools + recompile + smoke

The smoke test requires your own legally obtained game files under assets/.
Your SDK's existing Windows build remains unchanged.
HELP
}

have_sdk_assets() {
  python3 portable/tools/doctor.py --require-linux --require-sdk --require-assets
}

run_tests() {
  local dir="out/portable-host"
  cmake -S . -B "$dir" -DPGR1_PORTABLE_HOST_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
  cmake --build "$dir" --parallel
  ctest --test-dir "$dir" --output-on-failure
}

command_name="${1:-help}"
shift || true
case "$command_name" in
  help|-h|--help) usage ;;
  doctor) python3 portable/tools/doctor.py "$@" ;;
  test) run_tests ;;
  linux-tools)
    have_sdk_assets
    bash scripts/linux-tools.sh
    ;;
  recompile)
    have_sdk_assets
    if [[ ! -f build/linux/build.ninja ]]; then
      echo "Build tools are missing: run 'bash scripts/portable-dev.sh linux-tools' first." >&2
      exit 2
    fi
    bash scripts/recompile.sh
    ;;
  smoke)
    have_sdk_assets
    if [[ ! -f generated/game/x86_func_mapping.cpp ]]; then
      echo "Generated game is missing: run 'bash scripts/portable-dev.sh recompile' first." >&2
      exit 2
    fi
    duration="${1:-240}"
    [[ "$duration" =~ ^[0-9]+$ && "$duration" -ge 30 ]] || { echo "Duration must be >=30 integer seconds" >&2; exit 2; }
    bash scripts/smoke.sh "$duration"
    ;;
  all)
    have_sdk_assets
    run_tests
    bash scripts/linux-tools.sh
    bash scripts/recompile.sh
    bash scripts/smoke.sh 240
    ;;
  *) echo "Unknown subcommand: $command_name" >&2; usage >&2; exit 2 ;;
esac
