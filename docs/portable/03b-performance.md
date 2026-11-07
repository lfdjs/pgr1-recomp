# PGR1 Portable Stage 03B — CPU rendering baseline & profiling

This stage is an **isolated performance experiment**. It is **not** an OpenGL Xbox 3D renderer.
The user's OpenGL triangle test validates a graphics context, not the translation of Xbox
vertex shaders and pixel combiners. The PGR1 reference renderer still runs on the CPU.

## What changes

- Recompile a *separate* binary in `.local/harness_perf/game` with `-O3 -g0` for host code.
- Recompile generated guest code with `-O2` into `.local/harness/gen_perf_o2/` to avoid mixing
  optimization levels or overwriting the known-good `.local/harness/gen/` objects.
- Instrument the game backend at `portable/linux/perf/backend.cpp` to capture timings for
  `reference::Draw`, `reference::Clear`, `reference::Copy`, framebuffer decoding, and SHM publication.
- Use the **same SDL3 viewer, shared-memory frame protocol, and PS4 controller bridge** from Stage 03A.
- Optional adaptive CPU sampling: expensive images increase the number of game frames between captures.
  This reduces mean CPU cost, but **does not eliminate a stall when a sampled frame is rendered**.

## Installation

Unzip the package to a temporary folder. From the repository root on `portable`:

```sh
bash /tmp/pgr1-stage03b/install.sh "$PWD"
```

This only adds new paths under `portable/linux/perf`, `scripts/portable-perf.sh`, and `docs/portable`.
It does not rewrite SDK sources or change the existing Stage 03A binaries.

## Compilation

```sh
JOBS=4 bash scripts/portable-perf.sh build-game
```

**Requirements:** Original game `assets/default.xbe`, completed Stage 03A installation,
generated Xbox C++ code, and the previously patched SDK harness build script (spaces in paths).
The script automatically selects `PGR1_RELEASE` from SHA-1, including USA 1.0 release 2.

## Profiling run

```sh
PGR1_REF=60 PGR1_REF_BUDGET_MS=8 bash scripts/portable-perf.sh run
```

Esc closes the window. Play with the DualShock 4 as in Stage 03A. Try the race/menu and
let the preview run long enough for at least five captures.

Then:

```sh
bash scripts/portable-perf.sh report
```

`pgr1_perf.csv` includes `reference_draw_ms`, `reference_clear_ms`, `reference_copy_ms`,
`decode_ms`, `ipc_ms`, `sampled_wall_ms`, and sample spacing.

A useful alternative to compare the same capture interval with adaptive sampling disabled:

```sh
PGR1_REF=60 PGR1_REF_ADAPT=0 bash scripts/portable-perf.sh run
```

**Caveats:** `sampled_wall_ms` includes game CPU time, not just raster time. OpenGL
GPU utilization remains low until actual Xbox geometry/shader translation is connected.
`HOST_FLAGS=-O3` and `GEN_OPT=-O2` can change behavior; retain Stage 03A as a regression baseline.

## Engineering decision for Stage 03C

Do not attempt an all-at-once port of Direct3D12 code. Prototype GPU command recording
in `gpu::Backend` for a representative frame, then translate:

1. Clear, simple untextured triangles, and depth/stencil.
2. Vertex streams and FVF, then texture stages and Xbox texture swizzle.
3. Programmable Xbox vertex/pixel shaders and render-target synchronization.
4. Readback, W-buffer details, push buffers, visibility reports, fences, and performance.

The reference renderer should remain available for image-by-image comparison.
