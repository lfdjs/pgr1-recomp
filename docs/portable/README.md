# PGR1 Portable

Current: native Linux GPU runtime with translated SDK shaders, SDL3 audio and
OpenGL/Zink race validation. See [native runtime](04-native-linux.md) and
[validation report](validation-2026-11-08.md). Performance and visual parity remain open.

The milestone 00 scaffold description below is historical.

## Current baseline

- Game fork: `lfdjs/pgr1-recomp`, branch `portable`.
- Xbox runtime: the `sdk` git submodule (keep its existing pinned commit until a dedicated SDK fork is ready).
- The existing Windows build and Linux headless harness are **not** changed by this milestone.
- The original game `assets/` and generated code `generated/` must remain uncommitted.
- Three supported disc revisions have their own configs: Europe 1.1, Europe/Australia 1.0, USA 1.0.

## First implementation

`portable/include/pgr/portable/host.h` introduces a small `Host` interface for:
1. Host architecture capability reporting (64-bit virtual address **width**, not guaranteed 4 GiB reservation).
2. Monotonic time.
3. Per-user persistent data directory resolution (Windows APPDATA; Linux XDG_DATA_HOME/HOME).

`portable/src/host.cpp` implements desktop host services. There is intentionally no GPU/window/audio adaptation yet.
Code in the original `src/game/` and `sdk/` still uses its existing runtime paths.

## Build without any copyrighted game files

```sh
cmake -S . -B out/portable-host -DPGR1_PORTABLE_HOST_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build out/portable-host
ctest --test-dir out/portable-host --output-on-failure
```

This special CMake mode returns before the Xbox SDK's normal build path; it does not compile the XBE or its game code.
Without that option the original CMake configuration remains unchanged.

## Next milestones

1. Fork/pin `XboxXBERecomp` on a dedicated SDK branch; preserve upstream and baseline hashes.
2. Run the current Linux headless smoke test using the user's own legal disc files. Record logs and frame checks before changes.
3. Connect host services to the SDK in small platform-isolated commits, keeping Windows builds intact.
4. Introduce SDL3 Linux window/input/audio and reuse `gpu::Backend` for a new hardware renderer.
5. Address ARM64 after Linux is playable. PS Vita (ARM32) requires a separate memory-model proof of concept.

## Guardrails

- Never commit `assets/`, `generated/`, cache, captures, or proprietary game bytes.
- Keep game-specific hooks in `src/game/`, platform implementations separate, reusable SDK fixes in the SDK fork.
- Do not replace `gpu::Backend` or rewrite the recompiler unless necessary.
- Do not claim hardware functionality until tested on target devices.

## Native Linux renderer

The scaffold description above is historical. The native SDL3/OpenGL runtime now
uses SDK-translated shaders, GPU geometry, textures and resource synchronization.
See [build, run, profiling and acceptance limits](04-native-linux.md).

```sh
bash scripts/portable-linux.sh doctor
bash scripts/portable-linux.sh build
bash scripts/portable-linux.sh run
```

CPU/SHM preview remains an explicit diagnostic path. Native Vulkan is not implemented;
`run-vulkan` selects Mesa Zink and checks the actual renderer.
