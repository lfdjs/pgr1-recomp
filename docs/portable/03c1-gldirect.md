# PGR1 Portable — 03C.1: single-process SDL3/OpenGL bridge

**Historical stage description.** The implementation is now superseded by
[the FVF geometry increment](03c2-gpu-geometry.md), including its validation limits.
The instructions below describe the earlier clear-only prototype.

The previous 03A/03B preview uses a CPU reference rasterizer, sends sampled pixels
through mmap and displays them in a separate SDL3 process. This prototype instead
links SDL3 directly into **a new isolated harness executable**. It captures Xbox
`gpu::Backend::Clear` and `Present` calls on the game thread and displays the most
recent *clear colour* in OpenGL on the SDL **main thread**. `Draw` calls are counted,
**not rendered**. Guest depth, scissor, render targets, shaders and textures are
not implemented. The resulting display may be a solid colour — this is expected.
Do not use frame rate from this stage as a gameplay FPS measurement.

Its value is proving that the recompiled game's existing SDK runtime can start and
keep presenting in an SDL/OpenGL executable without a separate viewer, SHM files
or the reference CPU rasterizer.

## Requirements

- Stages 01 through 03B installed; PGR1 USA 1.0, EU 1.0, EU 1.1 supported.
- Original game in `assets/`, generated C++ present in `generated/game/`.
- Existing 03A SDL3 GUI build (`bash scripts/portable-gl.sh build`) succeeded.
- Clang, CMake, Ninja and standard Linux desktop video libraries.

## Install (VS Code terminal at the repository root)

```sh
mkdir -p /tmp/pgr1-stage03c1
unzip -oq ~/Downloads/PGR1_Portable_Etapa_03C1_GL_Direto.zip -d /tmp/pgr1-stage03c1
bash /tmp/pgr1-stage03c1/install.sh "$PWD"
```

The installer changes ONLY new `portable/linux/gldirect/` files and adds two
optional environment variables to `sdk/harness/build.sh`:

- `PGR1_EXTRA_INCLUDE_DIR` — a SINGLE path, safely quoted even with spaces.
- `PGR1_EXTRA_LINK_LIB` — an explicit SDL3 `.so` path, safely quoted.

When those variables are unset, **the old headless build uses its original link
line**, with the difference that an empty array is appended. A backup of the SDK
script is saved to `.local/sdk-harness-pre-gldirect.sh`.
The SDK submodule becomes locally modified; do not run `git submodule update --force`.
For reproducibility, the change must eventually be incorporated in a fork of the SDK.

## Test

```sh
bash scripts/portable-gldirect.sh test
JOBS=4 bash scripts/portable-gldirect.sh build-game
bash scripts/portable-gldirect.sh run
```

A window opens with the most recent guest clear colour. The title bar reports
`guest frames`, `draws (NOT rendered)` and `clears`. PS4/DualShock and keyboard
input are forwarded using the verified `cmd` protocol. Escape exits.

The executable and the run's logs are isolated in:

- `.local/harness_gldirect/game`
- `.local/run/gldirect/logs/pgr1_001.log`
- `.local/gldirect.out`

If the GUI crashes or guest frames remain zero, examine the two logs and use the
known-good `bash scripts/portable-perf.sh run` as a baseline. A guest boot failure
may be due to threading assumptions; this is a new host arrangement, not a tested
compatibility guarantee.

## Next: 03C.2 and beyond

Connect `gpu::Assemble` to upload vertex/index data into GL buffers and translate
primitive topology (start with XYZRHW/FVF 2D). Preserve title `State` rendering
semantics, render target identities, depth/stencil, Xbox textures and shader
translation in subsequent incremental milestones. **Never treat the colored
window from this stage as completed game graphics**.
