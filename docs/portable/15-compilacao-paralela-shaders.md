# 04D.13 — Compilacao paralela de shaders / Parallel shader compilation

## Portugues (Brasil)

O PGR1 recompilado traduz programas NV2A para HLSL e depois para SPIR-V.
Antes, os pares de shaders aguardavam **um unico worker**. Agora,
pares independentes podem ser compilados simultaneamente em um pool
persistente de threads. O padrao reserva parte do processador para
a simulacao: metade das threads de hardware, entre **1 e 4 workers**.

A variavel opcional `PGR1_VK_SHADER_WORKERS` aceita **1..8**; valores
invalidos usam o padrao. O cache em disco continua protegido por
`flock` por shader. O desenho so ocorre apos a compilacao correta
do shader: nenhum draw e descartado nem recebe shader substituto.

Teste comparativo, com a mesma pista e nivel de cache de shader:

```sh
PGR1_VK_SHADER_WORKERS=1 bash scripts/portable-vulkan.sh run-game
PGR1_VK_SHADER_WORKERS=4 bash scripts/portable-vulkan.sh run-game
```

Este ganho pode ocorrer principalmente na primeira visita a pistas
ou menus com shaders ainda nao compilados. Quando o cache esta
aquecido, o resultado pode ser semelhante. Nenhuma garantia de
60 FPS existe sem comparacao em gameplay real.

## English

The NV2A-to-HLSL-to-SPIR-V translation previously serialized shader pairs
on **one worker**. Stage 04D.13 compiles independent immutable pairs
concurrently using a persistent pool of 1..4 workers by default
(half the logical CPU count), with a bounded override of
`PGR1_VK_SHADER_WORKERS=1..8`.

The existing per-shader file lock preserves disk cache correctness.
The Vulkan consumer still waits for the exact compiled shader before
rendering; no draws are skipped. C++20 tests verify concurrent jobs,
error propagation and worker-count policy without Vulkan hardware.

Compare cold and warm caches separately. Shader parallelism targets
initial compilation stalls and does not eliminate CPU texture scanning.
