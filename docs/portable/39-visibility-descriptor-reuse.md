# 04D.22B — Visibility prefix cost / Custo dos prefixos de visibilidade

## PT-BR

04D.22A: 3427 apresentações, 33,30 FPS global, 1% low 5,912; usuário
relata ~20 FPS em corrida e ~10 FPS no sol. 723 reports causaram 723 das
903 submissões intermediárias (80,1%). Espera de report acumulada 7803 ms,
P95/P99 por intervalo 25,006/34,162 ms; máximo 54,850 ms. GPU spans somados
com cobertura completa: média 2,500, máximo 14,428 ms. Capture média
16,925 ms, texture check 8,438 ms. Tempos não são parcelas aditivas.
Não há associação per-frame com imagens do sol neste relatório.

O SDK 97b1654 documenta stall semelhante em PGR2 no D3D_SetFence; o
consumidor D3DDevice_GetVisibilityTestResult não está identificado no
config USA do PGR1. Não publicar resultados em outra thread nem atrasar
obrigatoriamente um frame sem contrato de leitura/escrita do report.
Esta etapa mantém o GetReport síncrono e reduz custo antes de sua conclusão.

- PGR1_VK_DESCRIPTOR_REUSE=1: UBO dinâmico, cache exato de descritores por
  submissão, chave com buffer/base/range, views e samplers efetivos e layout.
  Offsets dinâmicos são fornecidos a cada draw; acima de uint32_t a base
  estática conserva o endereço completo. Texturas e cubemaps continuam
  preparados em ordem e imagens retidas até fence. Cache limpo antes de
  reutilizar o pool após a fence. Sem novo contrato guest ou memcmp omitido.
- PGR1_VK_REPORT_DIRECT_WAIT=1: quando o produtor explicitamente aguarda
  GetReport, o consumidor espera a fence real logo após a submissão
  intermediária em vez de depender do loop ocioso de 1 ms. Não altera
  contagem, status, ordenação, nem força espera nos demais prefixos.
  O ganho esperado desta parte é limitado à latência de entrega/polling;
  não elimina a dependência GPU nem o custo da preparação CPU.

Ambas opções desligadas por padrão até teste A/B. Novas colunas opcionais
em sync.csv e sync-submissions.csv: descriptor_set_allocations,
descriptor_set_hits, visibility_direct_waits. Teste offscreen adicional
para reuso, pixels e contagem real. Compile/execute CTest no hardware.
Não foi executada build integrada nesta intervenção remota.

## EN

The 04D.22A run confirms 723/903 intermediate prefixes caused by visibility,
with report wait P95 25.006 ms and P99 34.162 ms per presentation interval.
Full GPU span coverage gives a mean of 2.500 ms; these timings are not
exclusive additive costs. Solar causality requires scene correlation.
The SDK documents a similar PGR2 stall, but the PGR1 USA configuration does
not identify the visibility result consumer. Keep synchronous guest publication.

Opt-in dynamic uniform descriptors reuse exact host bindings within the
fence-retired descriptor pool. Actual images are prepared and retained;
uniform offsets remain per draw, with no truncation for large offsets.
Opt-in direct report fence wait avoids idle polling when the producer is
already waiting. Neither option fabricates visibility nor changes guest
texture validation. Both default off for controlled A/B. Added counters
and offscreen regression test. Integrated compile/game validation pending.
