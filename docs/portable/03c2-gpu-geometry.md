# PGR1 Portable — OpenGL FVF geometry increment

This supersedes the clear-only 03C.1 implementation. It is an experimental renderer,
not a claim that PGR1 scenes or gameplay are now correct.

## Implemented path

The guest thread calls the real SDK `gpu::Assemble` in converted-attribute mode.
It copies positions, diffuse colors, UVs and rebuilt 32-bit indices into API-neutral
commands. Xbox 16-bit indices, start/base vertex, fans, quads, loops and strips are
interpreted by the SDK, rather than a second Xbox assembler. No CPU rasterizer or
TGA capture participates in the new path.

A two-frame bounded FIFO transfers owned commands to SDL's main thread. It preserves
all clear/draw ordering and incremental frames; it never drops frames or retains
pointers into mutable guest memory. Closing the window releases a blocked producer.
The main thread alone owns GL objects and the GL context.

The GL 3.3 implementation uploads vertex/index buffers and executes GLSL FVF XYZRHW
draws, including reciprocal-W perspective interpolation, D3D pixel-center conversion
and viewport depth range. Per-color-address FBO attachments retain the previous
frame; depth/stencil attachments are shared by depth address. Supported target
formats are A8R8G8B8/X8R8G8B8 (linear or swizzled descriptor) and D24S8. XRGB uses
RGB8 storage so destination alpha remains one. Present blits the requested front
surface to the actual window; resizing the window changes the blit, not guest data.

Supported raster state includes viewport, a single scissor, channel write masks,
CW/CCW culling, Z test/write, stencil test/operations/masks, alpha comparison and
standard GL-compatible Xbox blending states. Clear honors its channel flags and
rectangles, defaulting to the guest viewport as the reference renderer does.

The first fixed-function texture stage supports SELECTARG1, SELECTARG2 and MODULATE
with texture/diffuse/current arguments, nearest or bilinear filtering, wrap/mirror/clamp
addressing and no mip filtering.
Texture data is decoded by the SDK `DecodeBgra` (including its swizzled/linear and
DXT paths); linear texel coordinates are normalized before upload. The GL upload
cache has one entry and compares dimensions and all decoded bytes. Address reuse
and direct writes invalidate it without relying solely on lock notifications.
Decoding/copying still occurs on each accepted draw; a larger cache is future work.

Unsupported operations are logged once per reason, counted cumulatively and shown
in the window title. Programmable shaders are rejected explicitly, not presented as
successful geometry. The unsupported count includes non-draw operations.

## Build and verification

No SDK files or original headless build scripts need patching. The integrated game
now has its own CMake target, using the same headless source substitutions. CMake
keeps source/include/library paths as separate arguments, including paths with spaces.
The original harness, shared-memory preview and controller token protocol are retained.

```sh
bash scripts/portable-gldirect.sh test
cmake -S . -B out/pgr1-host-tests -DPGR1_PORTABLE_HOST_TESTS=ON
cmake --build out/pgr1-host-tests --parallel 4
ctest --test-dir out/pgr1-host-tests --output-on-failure

# Requires a compatible populated SDK, own assets and generated/game:
bash scripts/portable-dev.sh recompile
bash scripts/portable-gl.sh build
JOBS=4 bash scripts/portable-gldirect.sh build-game
bash scripts/portable-gldirect.sh run
```

The wrapper identifies USA/EU discs by the existing SHA-1 mapping and passes the
matching `PGR1_RELEASE` (USA is 2). For an installed SDL3, direct CMake also works:

```sh
cmake -S portable/linux/gldirect -B .local/harness_gldirect \
  -DPGR1_RELEASE=2 -DCMAKE_BUILD_TYPE=Release
cmake --build .local/harness_gldirect --parallel 4
```

The EGL pixel test is built when `pkg-config egl` is available. It uses a surfaceless
OpenGL context and returns CTest skip code 77 if EGL cannot initialize. It checks
actual read-back pixels for indexed geometry/strip, masked clears, retained frames,
depth rejection, stencil, scissor, blending, texture replacement, alpha testing,
culling, opaque targets and multiple target identities. No game bytes are required.
The SDK capture test links the production backend with the actual SDK assembler and
texture decoder, using only a synthetic memory fixture in place of the kernel.
It verifies quad conversion, 16-bit indices, owned memory and texture decoding.

## Validation on 2026-11-08

The `portable` gitlink requests SDK commit `2ef2954ba50691f8a7734918ccfda26d7a67d4b3`.
`git submodule update --init sdk` failed because the configured hk21x remote does not
have that object. The gitlink was deliberately preserved. Tests used an **explicit
temporary SDK archive**, commit `77b18eb`, without changing the committed SDK version:

```sh
cmake -S portable/linux/gldirect/tests -B out/pgr1-gldirect-tests \
  -DPGR1_SDK_SOURCE_DIR=/path/to/compatible-sdk
cmake --build out/pgr1-gldirect-tests --parallel 4
ctest --test-dir out/pgr1-gldirect-tests --output-on-failure
```

All four graphics tests and the standalone host test passed. Existing SDK contract,
TGA decoder and shared-frame IPC tests also passed as standalone executables.
The graphics tests also passed in Release from a build directory containing spaces. The EGL context used
Mesa llvmpipe (software OpenGL), so physical GPU acceleration was not measured. Backend source also
passed a syntax compile with `PGR1_RELEASE=2`. No assets or generated game code were
present, so `build-game` stopped at its missing-generated-code check. The integrated
executable, actual scene output, PS4 behavior in a running game and gameplay speed
remain unvalidated. The pixel expectations use reference-renderer conventions and
analytic expected colors; no full CPU-renderer image comparison was possible.

## Vulkan and remaining work

There is no native Vulkan backend in this increment. The commands contain owned
vertices, pixels, target identities and pipeline values, not GL handles or SDK
objects; a Vulkan renderer can consume the same Xbox translation layer. It would
still need device/swapchain management, pipelines, descriptors, synchronization,
resource lifetimes and shader translation. Adding that now would duplicate an
unvalidated rendering path. Zink remains possible through Mesa's GL implementation;
no code forces a GL driver. A forced Zink EGL test could not initialize:
`ZINK: failed to choose pdev` (test exit 77), so Vulkan-backed presentation is unvalidated.

Highest priority: recover the intended SDK commit, generate the USA game, then
capture rejected shader/state frequencies in a real scene. Translate vertex programs
and Xbox register combiners into a shared IR before targeting GLSL/SPIR-V. SDK HLSL
output cannot be used as GLSL without translation and semantic validation.

Other limits: non-XYZRHW transforms, lighting, fog, flat shading, W-buffer, MSAA,
depth bias, multiple scissor rectangles/stages, other texture arguments and mip filtering,
cube/volume/palette textures, gamma, GPU CopyRects, GPU readback and visibility
reports. Sampling a GPU-written render target is diagnosed and rejected to avoid
using stale guest bytes. CopyRects retains the old guest-memory helper but cannot
copy GPU color results. Sync/SyncLate diagnose their absent readback; reports retain
the original zero-visibility convention. Frame commands accumulate until Present;
GPU surface storage has no guest resource-destruction notification or eviction yet.
No performance improvement or completed PGR1 scene rendering is claimed.
