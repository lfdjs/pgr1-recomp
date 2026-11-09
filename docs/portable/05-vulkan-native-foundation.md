# PGR1 Portable 04A — Native Vulkan foundation, not a gameplay renderer

Base reviewed: lfdjs/pgr1-recomp `portable` at `0f35b3a916deaf89f899fd47d25923a84d838ad3` (8 Nov 2026). Installed additively; does not change `scripts/portable-linux.sh`, its accelerated OpenGL game executable, the Zink mode, the SDK gitlink, game assets, or generated C++.

## Scope (strict)

`scripts/portable-vulkan.sh build` compiles an independent **native Vulkan** window and swapchain validation executable. It creates a real `VkInstance`, SDL3 Vulkan surface, `VkPhysicalDevice`, `VkDevice`, graphics+present queue, FIFO swapchain, command pool, semaphores and fence. Each presentation frame is cleared with `vkCmdClearColorImage`, transitions from `UNDEFINED` to `TRANSFER_DST_OPTIMAL` to `PRESENT_SRC_KHR`, and is submitted with `vkQueueSubmit`/`vkQueuePresentKHR`. No OpenGL, Zink, guest game commands, HLSL translation, or CPU rasterizer are used. Because the colors are generated, it is **not** a PGR1 scene and does not demonstrate car texture correctness or race FPS.

**Do not replace `scripts/portable-linux.sh run-vulkan` with this tool!** That old command deliberately selects Mesa Zink (GL over Vulkan), whereas this new command tests a real native Vulkan swapchain only. There is not yet a native Vulkan **game renderer**.

## Compile/run

Linux Debian/Ubuntu examples: `sudo apt install -y libvulkan-dev vulkan-tools mesa-vulkan-drivers` plus SDL3 >= 3.2 dev headers (`libsdl3-dev` when available), or the SDK's cached SDL3 source (`build/linux/_deps/sdl3-src`) with its other build dependencies. NVIDIA proprietary drivers need their matching Vulkan ICD; do not replace working vendor drivers merely to install Mesa. XTEST is disabled for the cached SDL3 build, but the other X11/Wayland development packages may still be necessary.

```
bash scripts/portable-vulkan.sh doctor
JOBS=4 bash scripts/portable-vulkan.sh build
bash scripts/portable-vulkan.sh smoke --frames 180
```

`smoke` prints the selected physical device, API version, queue family, swapchain dimensions/image count, and completed Vulkan presents. Window resizing and closing are handled; Esc closes. A color pulse simply makes presentation obvious. It does **not** boot the game.

The build target is `out/pgr1-vulkan-wsi/pgr1_vk_wsi` by default, overridable via `PGR1_VK_BUILD_DIR`, including paths with spaces. No XBE or SDK generated code is required. There is no attempt to build the final guest renderer in this limited first step.

## Analyze the supplied OpenGL benchmark

```
bash scripts/portable-vulkan.sh triage .local/run/linux --expected-seconds 180
```

The report in the conversation had only 93 frames and approximately 43 seconds of audio for a requested 180-second limit, mean 2.46 presentations/s, >5-second queue waits but extremely short GPU timestamp spans. **This does not measure a race**. The extremely large `present_ms` while GL execution and GPU timestamps are short is specifically consistent with **SDL_GL_SwapWindow/VSync/window-system/driver blocking**, which may also backpressure the bounded producer queue. Cold shader compilation is a separate possibility, but these particular aggregate numbers do not establish it; inspect per-frame overlaps. The process appears to have ended before the configured duration, so investigate why. Trace compilation and actual process exit before attributing latency to GPU shader execution. Queue wait and presentation timings overlap and must not be summed. Test warm-cache as well as cold-cache runs; use equivalent gameplay windows. A quick, non-destructive diagnostic is:

```
OUTPUT=1280x720 PGR1_VSYNC=0 PGR1_RUN_SECONDS=180 bash scripts/portable-linux.sh benchmark
bash scripts/portable-vulkan.sh triage .local/run/linux
```

This comparison isolates windowed/no-VSync behavior; it does not prove that native Vulkan will cure graphic bugs.

## Matched-frame visual comparisons for cars

While a **real game** is running with the existing native OpenGL executable, create a regular file `compare` in its run directory to request a CPU reference comparison of the next completed frame:

```
touch .local/run/linux/compare
```

Wait until the game logs that a comparison is complete. It writes `compare-reference.ppm`, `compare-gpu.ppm` and `compare.txt` into `.local/run/linux`. Use only images taken from the **same guest frame**. The ROI coordinates below are illustrative; adjust them to enclose the car in the 640x480 comparison.

```
python3 portable/tools/compare_car.py .local/run/linux --region 180 190 280 220 --heatmap .local/run/linux/car-diff.ppm
```

The tool computes per-channel mean absolute error, maximum error, pixels over a 4/255 threshold and a red heatmap. Without `--region`, it compares the full frame. Do not commit PPMs generated from proprietary game content.

## Next development milestones for a native Vulkan **game backend**

1. Extract API-neutral Xbox guest command production (`gpu::Backend`, `gpu::Assemble`, owned `Submission`) into a shared module. No `gl*` calls in it; preserve FVF/vertex shader semantics and copies of all guest buffers.
2. Expose **SPIR-V binary output** from SDK shader lowering without the SPIRV-Cross -> GLSL -> GL driver compile roundtrip. Reflect shader bindings, vertex input locations, push/uniform blocks, and sampling resources; do not assume the OpenGL GLSL interface names are also Vulkan descriptors.
3. Implement a `VkRenderer` consuming actual guest `Clear`, `Draw`, `Copy`, `Readback`, `Present`: VkPipeline cache keyed by full blend/depth/stencil/raster/format state; uniform/vertex/index staging; VkImage/VkImageView; VkSampler; descriptors; image layouts and synchronization; track guest resource identities/dirty epochs.
4. Retain offscreen 2D and cube render targets (car reflections), swizzled/DXT texture decoding, depth/stencil feedback, alpha test, mip LOD and gamma. Correct Xbox coordinate conventions and W-buffer behavior. Vulkan native will not automatically fix existing shader translator mismatches or reflection bugs.
5. Add real GPU timestamp query pools and time the **shader pipeline compilation** separately. Warm caches, budget memory by bytes and avoid synchronous GPU readbacks when unnecessary. Handle swapchain recreation without dropping/scrambling guest-owned frame state.
6. Produce matched-frame native Vulkan vs CPU reference images at **the same frame**, compare car ROI and full frame, and run sustained, scripted **verified races**, controller/audio/save tests, p95/p99 frame times. Never claim 60 FPS based on a clear-only Vulkan window.
7. Only after parity and multi-minute racing, add `scripts/portable-linux.sh run-native-vulkan` as a distinct option. Leave `run-vulkan` as Zink for backwards compatibility unless a documented migration is planned.

## Validation status

The Python triage/compare tests run with `python3 -m unittest discover -s portable/tools -p 'test_*.py'`. Bash scripts can be syntax-checked with `bash -n scripts/portable-vulkan.sh`. A full C++ compile and hardware Vulkan run **must occur on the user's Linux machine** with SDL3 and Vulkan development packages. This package is an integration foundation and no claim of game-level native Vulkan functionality is made.
