# 04D.7 — CPU texture capture: per-draw reuse and telemetry

The Vulkan producer must scan guest texture bytes at **every distinct Draw()**
because the recompiled Xbox title can modify textures without SDK MarkDirty.
DirtyEpoch alone is not sufficient to skip comparisons between draws.

This change reuses a decoded immutable texture snapshot only when two stages
inside the **same synchronous Draw()** refer to exactly the same guest address,
format, size, mip chain, pitch and layout. Sampler state is still independent.
GPU-backed targets and reflection cube faces retain their existing paths.

New columns in both `frames.csv` (aggregated per presentation prefix) and
`submissions.csv` (per submission):

- `texture_static_binds`: static texture bindings encountered.
- `texture_same_draw_reuses`: stages that shared a snapshot within one draw.
- `texture_memcmp_checks`: actual comparisons performed.
- `texture_memcmp_bytes`: the summed *requested comparison lengths*, an upper
  bound since libc memcmp may exit on the first difference.
- `texture_decodes`: newly decoded immutable texture snapshots.

`texture_check_ms` and `decode_ms` still measure capture CPU time.
The standalone identity test covers all fields. The SDK capture test verifies
that two identical stages share pixels and the existing test checks that
unmarked guest writes between separate draws are still visible.

Validation:
```sh
JOBS=4 bash scripts/portable-vulkan.sh build-game
bash scripts/portable-vulkan.sh test
python3 portable/tools/frame_report.py .local/run/vulkan --target-fps 30
python3 portable/tools/report_batches.py .local/run/vulkan --window 300
```

Benchmark the same London Quick Race section with resolution 100%, MSAA off
and FXAA on before assessing gain. This is a bounded CPU optimization, **not**
a claim that the 30/60 FPS target has been reached. Audio, postfx, MSAA and
cubemap renderer paths are unchanged.
