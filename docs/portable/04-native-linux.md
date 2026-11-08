# Native Linux runtime: SDK shaders and resource synchronization

This stage starts from `808d49a`, preserving the earlier `32daba9` geometry work and
the recovered SDK gitlink `97b1654736f744a3966b5dff3d49be6d61ac7244`. The initial fetch
found `origin/portable` at `25c262f` and two local commits ahead, with no divergence.
The earlier descriptions of an unavailable SDK and absent game files are historical.
No SDK source patches are required by this implementation.

The existing `.local/harness_stream/game` uses the CPU reference rasterizer and SHM.
It is preserved. `scripts/portable-linux.sh run` selects the native OpenGL runtime.
A context alone does not establish hardware acceleration: its vendor, renderer and
version are printed. llvmpipe/softpipe/lavapipe require explicit `PGR1_ALLOW_SOFTWARE=1`.

## Build and run

Requirements: Linux x86-64, CMake >= 3.25, Clang/Clang++, Ninja, Python 3, Git,
OpenGL development headers, SDL3 desktop/audio dependencies, `glslangValidator` and
`spirv-cross`. EGL development files enable the isolated pixel regression test.
On Debian/Ubuntu the shader tools are packaged as `glslang-tools` and `spirv-cross`.
An installed SDL3 >= 3.2 is used when available. Otherwise CMake uses the SDK's
cached SDL3 sources or downloads SDL `release-3.4.16`. Optional SDL XTEST is disabled.
The original disc must be extracted under `assets/`, including its data directories.
An isolated `default.xbe` cannot run the game.

```sh
bash scripts/portable-linux.sh doctor
JOBS=4 bash scripts/portable-linux.sh build
bash scripts/portable-linux.sh run
```

Build initializes the pinned SDK if absent, checks the XBE SHA-1, builds the SDK's
Linux compiler tools when necessary, runs its incremental recompile target and
builds the separate Release executable at `.local/harness_gldirect/game`. A local
SHA-1 manifest forces tool reconfiguration after a disc change. USA 1.0 maps to
`PGR1_RELEASE=2`; EU mappings are retained. Paths are quoted and CMake passes sources,
includes and libraries as separate arguments. The SDK's shell harness compiler,
which splits some paths on spaces, is not used for the native executable.

Run uses `.local/run/linux` by default and preserves its HDD between launches.
`PGR1_RUN_DIR=/absolute/path` selects another independent profile/cache directory.
`PGR1_BUILD_DIR` selects a separate build directory, including paths containing spaces.
A lock prevents two instances using that run directory. SDL3 keyboard and gamepad
inputs retain the existing atomic `cmd` protocol; F11 toggles fullscreen and Escape
exits. The existing analog-stick tokens retain their digital threshold behavior.
No physical DualShock 4 acceptance test is implied by retaining this code.

```sh
bash scripts/portable-linux.sh run-vulkan
```

This explicitly selects **Mesa Zink, OpenGL over Vulkan**. There is no native Vulkan
backend. Device/swapchain setup, descriptor/pipeline management and synchronization
for native Vulkan remain future work. Shader SPIR-V intermediates and API-neutral
owned commands can be reused; Xbox shader/state translation should not be duplicated.

## Rendering and shader path

`gpu::Assemble` decodes Xbox streams and 16-bit indices and rebuilds unsupported host
topologies. The backend snapshots all sixteen vertex attributes and shader constants.
It uses the SDK's `VertexProgramHlsl`, `FixedVertexHlsl`, `MakePixelKey` and `PixelHlsl`
for real NV2A vertex programs, fixed transforms/lighting, register combiners, texture
stages, fog, alpha tests and W-buffer fragment depth. Constant packing is a portable
extraction of the SDK's Direct3D helper routines, with attribution in its source.
The SDK itself is unchanged.

SDK HLSL -> glslang SPIR-V -> SPIRV-Cross GLSL 330 -> driver shader compile/link.
The compiler subprocesses use argument arrays and `posix_spawnp`, never shell command
interpolation. HLSL semantic interfaces are normalized for GLSL 3.3 name-based linkage;
clip depth is converted from Direct3D [0,1] to OpenGL [-1,1]. Flat colors use the first
provoking vertex, matching the reference. Generated source/SPIR-V/GLSL and failures
remain in the ignored run directory's `shadercache-gl-v1/`; none are published.
Source equality guards disk cache reuse, while shader pairs and linked GL programs
are cached in memory. Compiler failures stop the runtime visibly, rather than drawing
a substitute color. Unknown MAC opcodes and missing programs are diagnosed. This
inherits the SDK translator's coverage; it does not establish complete NV2A support.

Texture snapshots reuse the SDK's BGRA/DXT decoders for all mip levels and cube faces.
A byte hash plus descriptor comparison invalidates CPU decoding on content/address
reuse. Shared immutable snapshots avoid copying decoded pixels into every draw.
CPU and GL caches have entry bounds (512 and 256); these are entry counts, not byte
budgets. Sampling 2D GPU targets uses their GL attachments with coordinate orientation
conversion. Sampling GPU-written cube faces composes them into a real cubemap before
the draw, instead of incorrectly binding a 2D attachment to `GL_TEXTURE_CUBE_MAP`.
This fixes the driver error observed on the real car-selection scene. Eviction
reserves all four stages before binding a draw, preserving textures already bound
on its earlier units.

Color targets include ARGB/XRGB and RGB565, with 16/24-bit depth and D24S8 stencil.
The backend applies raster state, polygon offset, culling, viewport/scissor, blend,
alpha and depth/stencil operations. CopyRects between GPU color targets is an ordered
blit. Blocking Sync reads GPU results back and repacks linear/swizzled guest memory;
size zero is an **exact resource lookup**, as the SDK expects for each vertex stream.
Already-read targets do not stall again until written. NOOVERWRITE SyncLate retains a
pending result and delivers a finished copy without waiting on the guest thread.

Partial Flush/readback batches are not display frames and do not trigger VSync.
A completed Present is copied on the GPU into a separate display image. The next
frame's clears and partial drawing cannot overwrite the last completed picture.
This separation is essential when a game renders into memory reused as its front.

SDL audio replaces the harness's discarded mixer output: a bounded reusable buffer
feeds 48 kHz float stereo into an SDL audio stream. The device stream is stopped
before SDL teardown. SDK guest threads still use the SDK's process-exit convention;
this is not a new join/shutdown API. Saves retain the SDK's emulated HDD behavior.

## Profiling and visual validation

```sh
bash scripts/portable-linux.sh test
PGR1_RUN_SECONDS=180 bash scripts/portable-linux.sh benchmark
bash scripts/portable-linux.sh report
PGR1_RUN_SECONDS=180 bash scripts/portable-linux.sh benchmark-reference
```

GPU benchmark runs the existing `scripts/pad/race.txt`, including throttle after the
menu sequence; set `SCRIPT` to another script when testing a saved profile.
Benchmark uses the existing `PGR1_SKIP_INTRO=1` game option so the fixed menu script
starts from the title; set it to zero explicitly to include the intro. Manual run
keeps the game's normal intro behavior. Manual
play leaves SCRIPT unset. Benchmark defaults to `PGR1_SCRIPT_ONLY=1`, so live pad and
keyboard buttons cannot divert its menu script; Escape/F11 remain active. Set the
flag to zero explicitly to combine scripted and live inputs. A completed process is not proof that the script reached
a race: inspect the image and log. The reference benchmark is explicitly selected,
uses the same Release runtime/generated objects and records sampled CPU raster costs.
It retains the original reference renderer, instead of substituting GPU timings for
CPU measurements.

`frames.csv` records completed guest frames only: interval, process CPU time (all
threads, excluding compiler subprocesses), Draw capture/max cost, texture decode,
queue wait, GL execution wall time, presentation/VSync time and accepted/rejected
Draw counts. `gpu.csv` contains asynchronous timestamp spans, joined by guest frame;
these spans include submission gaps and are not a GPU engine-utilization percentage.
Release checks GL errors per batch; `PGR1_GL_DEBUG=1` enables synchronous driver
messages and per-command checks for diagnosis. Constant declarations are loaded by
the SDK on shader selection; later title writes in State::vsc remain authoritative. Vertex/index/uniform uploads are
combined into reusable staging buffers per batch; VAOs, FBO attachments and sampler
state are cached. The report prints mean/P95/P99/max. Intro, loading and cold shader compilation remain
visible outliers. A frame with no rejected calls still needs a visual correctness
check; its FPS does not establish a playable port.

For an explicit single-frame CPU/GPU comparison:

```sh
PGR1_COMPARE_FRAME=2500 PGR1_RUN_SECONDS=180 \
  PGR1_CAPTURE=last-completed.ppm bash scripts/portable-linux.sh benchmark
```

The GPU prefix initializes retained reference targets, then the chosen frame is also
rasterized by the SDK CPU renderer. `compare-reference.ppm`, `compare-gpu.ppm` and
`compare.txt` record RGB errors. This diagnostic deliberately stalls and changes the
performance sample; use another run with PGR1_COMPARE_FRAME unset for clean timing.
For a running instance, create an empty regular file `compare` in its run directory
to compare the next frame, or `snap` to capture `snap.ppm`. Requests are consumed.
The frame must actually be reached. Final capture always reads the last completed
GPU display image, not a partially cleared next frame. Captures are local game data
and must never be committed.

## Acceptance status and remaining limits

Validation results are recorded in [the dated report](validation-2026-11-08.md) and PROGRESS.md. A complete Linux
port is not claimed. Remaining areas include native Vulkan, depth-texture sampling,
paletted/volume textures, MSAA and point sprites, multiple scissor rectangles,
render-target feedback in a single draw, overlapping/self CopyRects, full CPU writes
back into previously GPU-owned targets, gamma presentation and GPU visibility queries
(the original zero-visibility report convention remains). Texture border colors and
some sampler controls still need parity checks. Cold external shader compilation
can stall; no background compilation or persistent driver-program cache is provided.
Resource destruction/address aliasing and cache byte budgets need further work.

Actual race screenshots, reference error numbers, native pause/restart and saved-profile
reload through Zink were checked. Physical DS4 input, subjective audio quality,
fullscreen behavior, longer races and remaining visual differences still need
acceptance before labeling the port stable or targeting 60 FPS. Local game
files and generated code, caches, executable binaries and captures are excluded from
commits and publication.
