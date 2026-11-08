# Linux GPU validation — 2026-11-08

Initial HEAD: `808d49a`; preserved geometry commit `32daba9` and available SDK
`97b1654736f744a3966b5dff3d49be6d61ac7244`. Initial origin/portable: `25c262f`.
No SDK source changes. Disc: locally supplied USA 1.0, SHA-1
`0d55fff29ceb34ab4dfcfc4820e823d967ca1469`, `PGR1_RELEASE=2`.

Host: Linux x86-64, i7-9700K (8 cores), RTX 3060 Ti, NVIDIA 580.178.04,
Mesa 26.0.8, SDL3 3.4.16. Default GPU: NVIDIA OpenGL. Alternative renderer:
`zink Vulkan 1.4(NVIDIA GeForce RTX 3060 Ti (NVIDIA_PROPRIETARY))`.
Zink is OpenGL over Vulkan; a native Vulkan backend remains unimplemented.

## Acceptance evidence

A clean Release game and SDL3 source build succeeded in `.local/clean configure`
(a build path containing spaces). The pinned SDK and generated game sources were
used without local SDK patches. This was a clean build directory, not a fresh clone.

Six primary tests passed via `bash scripts/portable-linux.sh test`: host services,
SDK contract, atomic input snapshot, owned command FIFO, actual GL pixel tests and
SDK assembler/texture-decoder capture. Pixel tests include translated fixed/programmed
shaders, constants, mips/cubes, alpha/depth/stencil/blend, viewport clipping, cache
eviction, copies, odd-width D16 readback and retained display frames. GL debug mode
also passed. Separate existing TGA decoder and SHM IPC tests passed (eight total).
Original headless harness smoke completed 7860 frames without traps, with 1503
Draws/frame in the last 300 frames and final guest log at 178.345 seconds.

Native manual acceptance ran 240 seconds and exited zero. Created a driver, saved
it to isolated emulated HDD, selected arcade/easy/San Francisco/MINI Cooper S/Auto,
loaded the track and accelerated to 38 then 58 mph. Lap/time/checkpoint HUD advanced.
Pause, restart, confirmation, renewed acceleration to 54 mph, quit race and return
to menus worked. Pause darkening is intentional. The Zink process loaded that saved
Player profile, reached the same race, accelerated to 44 mph and paused/resumed.
The Zink race session ended with SIGTERM (status 143, about 192 seconds of recorded
frames), before its configured 240-second deadline; no driver error was logged.
Its full-duration/clean-exit acceptance is therefore incomplete. A separate 10-second
Zink deadline check exited zero, with 443,904 audio sample frames, peak0.172759,
RMS0.055207. This checks startup shutdown, not long-race stability.

SDL audio uses the real float48k stereo pull stream. Native run delivered 11,501,824
sample frames, peak 1.0, RMS 0.122855; startup Zink check also had nonzero audio.
This verifies generated/output audio, not subjective quality. Manual navigation used
the atomic controller command protocol. Xbox Series X was discovered physically;
physical DS4 buttons, keyboard gameplay and F11 fullscreen were not acceptance-tested.

## Measurements

These are entire sessions containing menus, loading, race, diagnostics and pauses.
They are **not continuous gameplay FPS**, and scene/cold-cache differences prevent
a controlled OpenGL-versus-Zink speedup claim. CPU time sums all process threads;
GPU values are asynchronous timestamp spans including submission gaps, not utilization.

| Whole-session metric | Native OpenGL | Zink/Vulkan |
| --- | ---: | ---: |
| Recorded frames | 8090 | 9500 |
| Mean frame interval | 29.666 ms | 20.196 ms |
| P95 / P99 | 93.035 / 110.152 ms | 39.002 / 64.698 ms |
| Maximum | 831.666 ms | 717.483 ms |
| Mean presentation rate | 33.71/s | 49.52/s |
| Mean process CPU | 43.746 ms | 21.943 ms |
| Mean Draw capture | 18.845 ms | 10.907 ms |
| Mean texture decode | 0.015 ms | 0.012 ms |
| Mean GL execution wall time | 20.627 ms | 5.258 ms |
| Mean GPU timestamp span | 15.606 ms | 3.406 ms |
| Mean presentation | 6.439 ms | 11.998 ms |
| Accepted / rejected Draws | 9,754,038 / 0 | 5,507,365 / 0 |

A short visually confirmed Zink driving interval, frames 8032–8266 (235 frames,
after the 44 mph snapshot, before the reference comparison), measured mean
23.470 ms, P95 38.510 ms, P99 46.922 ms: about 42.6 presented frames/s.
This is a short sample, not stable 60 FPS or a prolonged-race result.

Updated explicit CPU-reference profiling (119 samples, mixed scenes): raster mean
114.776 ms, P95 549.779, P99 582.687, maximum 584.548; clear 16.258 ms, texture
decode 0.708 ms, IPC 0.094 ms. Nine samples with >1000 Draws had mean raster
369.059 ms and P95 538.980 ms. Rasterization dominates IPC. Different scenes and
sampling prevent a like-for-like speedup ratio against native GPU sessions.

Active-race CPU/GPU comparisons at 640x480: native frame 5164 mean RGB error
5.55946/255, maximum channel error 251, 84641/307200 pixels above 4; Zink frame8268
mean 3.91369/255, maximum253, 73773/307200 pixels above4. The frames differ, so these
numbers do not rank backends. Differences remain visible around shading, vegetation,
reflections and edges; zero rejected Draws does not imply visual parity. Comparisons
stall the pipeline and are included in whole-session outliers.

## Reproduction and remaining work

```sh
bash scripts/portable-linux.sh doctor
JOBS=4 bash scripts/portable-linux.sh build
bash scripts/portable-linux.sh run
bash scripts/portable-linux.sh run-vulkan
bash scripts/portable-linux.sh test
```

See [runtime documentation](04-native-linux.md) for dependencies, isolated profile
paths, reference benchmarking, CSV reports and live `snap`/`compare` requests.
The fixed menu input script may enter profile menus or an attract demo with existing
saves; inspect captures before treating any benchmark as racing.

This is a substantial functional increment, not a complete Linux release. Priorities:
resolve CPU/GPU visual differences using matched frames; profile capture/texture
hashing and remaining synchronization; investigate the interrupted Zink long run;
repeat longer controlled races; test physical DS4/keyboard, audio quality and
fullscreen. Native Vulkan, depth textures, palette/volume formats, MSAA, GPU visibility,
CPU writes into GPU-owned resources and resource/cache lifetime parity remain open.
No game files, generated code, executables, caches or screenshots are published.
