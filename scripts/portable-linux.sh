#!/usr/bin/env bash
# Native Linux entry point. OpenGL is the default; CPU reference is explicit.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD="${PGR1_BUILD_DIR:-$ROOT/.local/harness_gldirect}"
[[ "$BUILD" = /* ]] || BUILD="$ROOT/$BUILD"
RUN="${PGR1_RUN_DIR:-$ROOT/.local/run/linux}"
region() {
    [[ -f assets/default.xbe ]] || { echo 'Missing assets/default.xbe' >&2; return 2; }
    case "$(sha1sum assets/default.xbe | cut -d' ' -f1)" in
        4878c3bdbd44663b64ce64986b313e8c6b7f8917) echo 0;;
        ba7763e6e946ab40aed6706f2c9bc1df3a990fa0) echo 1;;
        0d55fff29ceb34ab4dfcfc4820e823d967ca1469) echo 2;;
        *) echo 'Unsupported XBE: no safe regional hook map' >&2; return 2;;
    esac
}
build() {
    [[ -f sdk/runtime/gpu/vertex.cpp ]] || git submodule update --init sdk
    for tool in cmake clang clang++ glslangValidator spirv-cross; do
        command -v "$tool" >/dev/null || { echo "Missing dependency: $tool" >&2; return 2; }
    done
    local release; release="$(region)"
    local digest recorded=""
    digest="$(sha1sum assets/default.xbe | cut -d' ' -f1)"
    [[ ! -f .local/generated-xbe.sha1 ]] || recorded="$(cat .local/generated-xbe.sha1)"
    if [[ ! -f build/linux/build.ninja || "$digest" != "$recorded" ]]; then
        bash scripts/portable-dev.sh linux-tools
    fi
    # Re-run the incremental SDK target even when generated sources already exist.
    # This tracks config/SDK edits and prevents silently combining another disc's code with USA hooks.
    bash scripts/portable-dev.sh recompile
    mkdir -p .local
    echo "$digest" > .local/generated-xbe.sha1
    local compiler=()
    [[ -f "$BUILD/CMakeCache.txt" ]] || compiler=(-DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}")
    cmake -S portable/linux/gldirect -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DPGR1_RELEASE="$release" "${compiler[@]}"
    cmake --build "$BUILD" --parallel "${JOBS:-4}" --target "${1:-game}"
}
run() {
    [[ -x "$BUILD/game" ]] || build
    mkdir -p "$RUN/logs"
    RUN="$(realpath "$RUN")"
    (
        flock -n 9 || { echo "A game already owns $RUN" >&2; exit 2; }
        cd "$RUN"
        echo "Backend: OpenGL (SDK HLSL -> SPIR-V -> GLSL)"
        [[ -z "${PGR1_COMPARE_FRAME:-}" ]] || echo "Diagnostic CPU comparison: frame $PGR1_COMPARE_FRAME"
        echo "Run directory: $RUN"
        FPS="${PGR1_FPS:-60}" "$BUILD/game" "$ROOT/assets"
    ) 9>"$RUN/run.lock" 2>&1 | tee "$RUN/console.log"
}
case "${1:-help}" in
    doctor)
        python3 portable/tools/doctor.py --require-linux --require-sdk --require-assets
        for tool in glslangValidator spirv-cross; do command -v "$tool" || exit 2; done
        [[ -f generated/game/x86_func_mapping.cpp ]] && echo 'Generated game: present' || echo 'Generated game: build will generate it'
        echo 'Default: OpenGL. Driver identity is printed when a context opens; software rendering is not hardware acceleration.'
        ;;
    build) build;;
    run) run;;
    run-vulkan)
        echo 'Backend: Mesa Zink (OpenGL over Vulkan), not native Vulkan'
        export MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink PGR1_EXPECT_ZINK=1
        export __GLX_VENDOR_LIBRARY_NAME=mesa
        if [[ -f /usr/share/glvnd/egl_vendor.d/50_mesa.json ]]; then
            export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
        fi
        run;;
    benchmark)
        export PGR1_SCRIPT_ONLY="${PGR1_SCRIPT_ONLY:-1}"
        export PGR1_SKIP_INTRO="${PGR1_SKIP_INTRO:-1}"
        export PGR1_RUN_SECONDS="${PGR1_RUN_SECONDS:-180}"
        export SCRIPT="${SCRIPT:-$ROOT/scripts/pad/race.txt}"
        run
        python3 portable/tools/frame_report.py "$RUN";;
    benchmark-reference)
        export PGR1_SKIP_INTRO="${PGR1_SKIP_INTRO:-1}"
        build reference_benchmark
        mkdir -p "$RUN/reference"
        RUN="$(realpath "$RUN")"
        frame_file="$(mktemp "$RUN/reference/frame.XXXXXXXX")"
        duration="${PGR1_RUN_SECONDS:-180}"
        [[ "$duration" =~ ^[1-9][0-9]*$ ]] || { echo 'PGR1_RUN_SECONDS must be a positive integer' >&2; exit 2; }
        set +e
        (cd "$RUN/reference" && PGR1_FRAME_FILE="$frame_file" PGR1_REF="${PGR1_REF:-60}" PGR1_REF_ADAPT=0 \
            SCRIPT="${SCRIPT:-$ROOT/scripts/pad/race.txt}" FPS="${PGR1_FPS:-60}" \
            timeout --kill-after=5 "$duration" "$BUILD/reference_benchmark" "$ROOT/assets") >"$RUN/reference/console.log" 2>&1
        status=$?; set -e
        [[ $status == 0 || $status == 124 ]] || exit "$status"
        python3 portable/tools/frame_report.py "$RUN/reference" --reference;;
    report) python3 portable/tools/frame_report.py "$RUN";;
    test) bash scripts/portable-dev.sh test; bash scripts/portable-gldirect.sh test;;
    *) cat <<'HELP'
PGR1 native Linux (experimental, acceptance incomplete)
  bash scripts/portable-linux.sh doctor
  bash scripts/portable-linux.sh build
  bash scripts/portable-linux.sh run
  bash scripts/portable-linux.sh run-vulkan          # Mesa Zink, not native Vulkan
  bash scripts/portable-linux.sh benchmark           # GPU path, race input script, CSV
  bash scripts/portable-linux.sh benchmark-reference # explicit sampled CPU reference
  bash scripts/portable-linux.sh report
  bash scripts/portable-linux.sh test
Options: JOBS=4, PGR1_RUN_DIR=..., PGR1_RUN_SECONDS=180, SCRIPT=..., PGR1_VSYNC=0|1,
         PGR1_CAPTURE=local.ppm. Benchmark defaults to SCRIPT-only input; PGR1_SCRIPT_ONLY=0 enables live input.
Unset SCRIPT for manual play. F11 toggles fullscreen.
HELP
    ;;
esac
