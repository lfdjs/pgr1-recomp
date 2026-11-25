# PGR1 Portable

## Visão geral em português (Brasil)

A branch `portable` contém o port nativo Linux do **Project Gotham Racing 1**,
com o código original recompilado estaticamente, runtime Xbox preservado em
submódulo fixado, SDL3 e renderização Vulkan nativa. Há também etapas
históricas de experimentos headless, OpenGL e Zink; **Zink não é Vulkan nativo**.

Para o estado atual, comece pela [Etapa 04C — execução Vulkan do jogo](07-vulkan-native-game.md),
pelo [relatório de áudio, opções RECOMP e antialiasing](08-audio-recomp-vulkan.md),
pela [captura CPU de texturas](09-cpu-texture-capture.md) e pela
[auditoria recente da RX 580](10-auditoria-performance-2026-11-22.md).

**Situação:** o problema principal do áudio espacial dos adversários foi
corrigido; resolução dinâmica, FXAA e contador de FPS funcionam. O MSAA
ainda pode ser muito caro, e a estabilidade entre 30 e 60 FPS continua em
desenvolvimento. O port ainda precisa de testes comparáveis em Londres e
na RTX 3060 Ti. Informações históricas não devem ser tratadas como validação
do código mais recente.

[Otimizações recentes do pipeline Vulkan](14-sobreposicao-cpu-gpu.md):
o teste de GPU agora cobre tanto sobreposição assíncrona quanto fallback
síncrono; veja também o comparativo de apresentação FIFO/MAILBOX.

sobreposicao CPU/GPU, [compilacao paralela](15-compilacao-paralela-shaders.md)
e [reutilizacao de estados](16-estados-vulkan.md) e
[expansao de vertices na CPU](17-expansao-vertices.md).

[Seleção automática de GPU Vulkan (RX 580 / RTX 3060 Ti)](13-selecao-automatica-gpu.md):
basta executar `bash scripts/portable-vulkan.sh run-game` nas duas máquinas.
O Vulkan seleciona uma GPU dedicada compatível e registra o nome no log.

[Perfis de CPU x86-64 e compilação otimizada (04D.18A)](20-perfis-cpu-x86-64.md):
compatibilidade Intel/AMD, detecção automática e testes A/B no i7-2700K.

[Sobreposição de comparações exatas de texturas (04D.18B, experimental)](21-sobreposicao-texturas.md):
worker limitado, sem ignorar escritas; rastreamento de memória permanece pendente.

[Redução de trabalho repetido na captura (04D.18C)](22-captura-sem-repeticao.md):
cache exato de programas, buffers temporários e layout de texturas.

[Diagnóstico de teto e auditoria de coerência (04D.19A)](23-coerencia-guest.md):
modo inseguro opt-in, revisão fixada do SDK e prova de aliases físicos.

[Auditoria dos escritores (04D.19B)](24-writer-coverage.md),
[núcleo de coerência e captura auditada (04D.19B/C)](25-event-driven-texture-cache.md),
[conversão direta de índices (04D.20)](26-geometry-capture-v2.md),
[comandos de validação](27-performance-validation.md) e
[handoff da sessão](28-handoff.md): núcleo cooperativo testado, audit integrado
e tracked experimental com fallback exato integral no SDK fixado. A cobertura
de todos os escritores guest e as metas de 30/60 FPS continuam pendentes.

[Atribuição de escritores do SDK (04D.19D)](29-sdk-writer-attribution.md):
patch opt-in, metadados de falhas e instruções de validação; cobertura ainda desconhecida.

[Auditoria em sombra do diário (04D.19E)](30-journal-shadow-audit.md):
previsões por sequência de página e comparação exata preservada.

**Convenção para novos commits:** preservar nomes de APIs, variáveis e
símbolos em inglês; documentar decisões não triviais em comentários **EN/PT-BR**;
adicionar seções em português e inglês aos novos documentos técnicos.
Os registros históricos continuam disponíveis, com resumos bilíngues.

## English overview

The `portable` branch hosts a native Linux port of statically recompiled
PGR1, SDL3 audio, the pinned Xbox runtime and native Vulkan rendering.
Earlier headless/OpenGL/Zink milestones are historical; Zink is not native
Vulkan. Start with the [04C native runtime](07-vulkan-native-game.md), the
[audio/RECOMP/AA report](08-audio-recomp-vulkan.md), the
[CPU texture capture report](09-cpu-texture-capture.md), and the
[latest RX 580 performance audit](10-auditoria-performance-2026-11-22.md)
and the [texture cache experiment](11-cache-texturas-04d9.md).
Recent optimizations include [CPU/GPU overlap](14-sobreposicao-cpu-gpu.md),
[parallel SPIR-V shader compilation](15-compilacao-paralela-shaders.md) and
[Vulkan draw-state reuse](16-estados-vulkan.md) and
[CPU vertex expansion](17-expansao-vertices.md), and
[reflection-aware batching / agrupamento com reflexos](18-reflexos-agrupamento.md).
[Automatic Vulkan GPU selection](13-selecao-automatica-gpu.md) allows
`bash scripts/portable-vulkan.sh run-game` on either AMD or NVIDIA
without a vendor-specific environment variable.
[x86-64 CPU profiles and optimized builds (04D.18A)](20-perfis-cpu-x86-64.md)
provide CPU capability detection and controlled compiler experiments.
The engine-audio routing regression is resolved; the native Vulkan CPU texture cache now defaults to 1,024 entries after an A/B test exposed eviction thrashing. Full 30/60 FPS stability,
fast MSAA and RTX 3060 Ti validation remain open.

[Writer coverage audit](24-writer-coverage.md),
[portable memory tracker and audited capture](25-event-driven-texture-cache.md),
[direct flat index conversion](26-geometry-capture-v2.md),
[validation workflow](27-performance-validation.md) and
[session handoff](28-handoff.md) document 04D.19B/C and 04D.20.
The cooperative core is tested; guest coverage remains Unknown and experimental
tracked capture keeps complete exact fallback on the pinned SDK.

---


Current: native Linux GPU runtime with translated SDK shaders, SDL3 audio and
OpenGL/Zink race validation. See [native runtime](04-native-linux.md) and
[validation report](validation-2026-11-08.md). Performance and visual parity remain open.

The separate [04C native Vulkan runtime](07-vulkan-native-game.md) connects the
recompiled game to owned command submissions and SDK-generated SPIR-V. Its
validation limits are documented independently from the 04B geometry fixture
and the OpenGL/Zink race validation.

The [audio, RECOMP settings and Vulkan antialiasing report](08-audio-recomp-vulkan.md)
documents positional engine submixes, MSAA/FXAA, the FPS overlay, Linux preferences
and the hardware validation still pending.

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

CPU/SHM preview remains an explicit diagnostic path. `run-vulkan` selects Mesa
Zink and checks the actual renderer. The separate native Vulkan executable uses
`bash scripts/portable-vulkan.sh build-game` and `run-game`; see
[04C commands, architecture and acceptance limits](07-vulkan-native-game.md).
