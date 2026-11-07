# Etapa 03A — OpenGL bootstrap + reference frame IPC

This milestone is a **diagnostic bridge**, not a replacement for the Xbox GPU hardware pipeline.
It is deliberately additive: stage 02's `scripts/portable-gui.sh run`, original Linux headless harness,
Windows D3D11/D3D12 backends, game hooks, and the SDK submodule remain unchanged.

## What is accelerated

`bash scripts/portable-gl.sh gl-test` opens a REAL desktop OpenGL 3.3 core context through SDL3,
compiles and links GLSL shaders, and draws a triangle in GPU hardware (or driver software fallback,
if your platform uses Mesa llvmpipe). This validates context creation and the first GLSL pipeline.
It does **not** render Xbox geometry.

`bash scripts/portable-gl.sh run` renders selected PGR1 frames using the existing SDK CPU-reference
rasterizer but replaces disk TGA capture/reload with **BGRA8 frames in a shared mmap file**.
The SDL3 viewer displays frames via its renderer; an SDL GPU renderer may be selected automatically.
This removes per-frame image files but still does not remove the biggest performance bottleneck:
`reference::Draw` on the CPU. Expect substantial slowness and sparse image updates at `PGR1_REF=30`.

## Architecture

```
generated/game (XBE -> C++) + SDK runtime
  -> gpu::Backend (portable/linux/stream/backend.cpp)
      -> SDK reference::Draw (software, only on selected frames)
      -> gpu::DecodeLevel (BGRA8 Xbox surface)
      -> portable/linux/stream/frame_ipc.cpp  [mmap /dev/shm]
  -> SDL3 viewer (portable/linux/stream/viewer.cpp)
      -> SDL_Texture upload + presentation, PS4 gamepad via cmd file

OpenGL diagnostic executable: portable/linux/stream/gpu_probe.cpp (independent from game)
```

## Install

From your VS Code terminal at root of `pgr1-recomp`, extract ZIP into `/tmp/pgr1-stage03`
then call `bash /tmp/pgr1-stage03/install.sh "$PWD"`.

## Commands

```bash
bash scripts/portable-gl.sh build
bash scripts/portable-gl.sh test
bash scripts/portable-gl.sh gl-test  # GLSL triangle; Esc to close
JOBS=4 bash scripts/portable-gl.sh build-game  # links a separate .local/harness_stream/game
PGR1_REF=30 bash scripts/portable-gl.sh run
```

`build-game` automatically sets `PGR1_RELEASE` based on your disc SHA-1; all three supported versions
remain available. Requires the Stage 01 SDK harness path-with-spaces fix and your generated game code.

Use `PGR1_REF=60` to reduce CPU capture load. Use `PGR1_REF=15` to sample more often at substantially
higher CPU cost. `PGR1_FPS` controls frame pacing. The published frame counter and count appear in
`.local/run/glbridge/logs/pgr1_001.log`; game stdout/stderr is in `.local/glbridge-game.out`.

## Potential issues

- `frame surface is not BGRA8`: renderer must support the source Xbox render target format; this bridge
  currently accepts only surfaces decoded to BGRA8, without pretending all Xbox formats are portable.
- If the SDL window is blank, check the game log for `renderer: ... streamed` and `d3d: CreateDevice`.
- If the GL diagnostic window fails, check Mesa/graphics drivers and OpenGL 3.3 support.
- `/dev/shm` contains the temporary 8 MiB protocol buffer and is removed by the run script.
- `gpu_probe.cpp` confirms the GL pipeline only; a full `gpu::Backend` OpenGL renderer still requires Xbox
  vertex program translation, pixel combiners, texture formats, render state fidelity, render targets and readbacks.
- Original assets/ and generated/ files must remain uncommitted.

## Next step (03B)

Port shader/vertex-state semantics from `sdk/runtime/gpu/shaders.cpp`, `gpu/state.h`,
`gpu/vertex.cpp` and `gpu/texture.cpp` into GL backend modules, using the existing D3D outputs as
references. Start with geometry/clear/texture verification, preserve guest readback and fences,
then expand the state/shader test coverage. This is not a one-line switch from D3D to OpenGL.
