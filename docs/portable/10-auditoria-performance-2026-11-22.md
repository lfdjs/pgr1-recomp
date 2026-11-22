# PGR1 04D.7 — Auditoria de desempenho / Performance audit (22-11-2026)

## Português (Brasil)

### Origem da medição

Relatórios `texture_report.py`, `report_batches.py` e `frame_report.py`
enviados após testar a branch `portable` na RX 580. O intervalo cobre
**8.064 registros de frame**, mas não identifica por si só quais pertencem
à corrida em Londres, aos menus ou ao carregamento. Evite interpretar os
resultados da sessão inteira como FPS de uma única pista.

### Resultados observados

| Métrica | Sessão completa | Últimos 300 frames |
| --- | ---: | ---: |
| `frame_ms`, média | 45,046 ms | 16,664 ms |
| `frame_ms`, P95 | 144,710 ms | 18,022 ms |
| `capture_ms`, média | 22,387 ms | 5,686 ms |
| `texture_check_ms`, média | 8,830 ms | 3,370 ms |
| `decode_ms`, média | 2,671 ms | 0,022 ms |
| Reutilizações de textura no mesmo draw | 0 | 0 |

No conjunto, foram **9.258.304 vínculos** de texturas estáticas,
**9.091.908 chamadas de `memcmp`** e **167.580 snapshots decodificados**.
A soma dos tamanhos solicitados ao `memcmp` chegou a **881.687 MiB**,
ou **109,3 MiB/frame** em média; isso é um limite superior e não demonstra
quantos bytes foram realmente lidos pelo processador.

O relatório de apresentações indicou **8.052 apresentações bem-sucedidas**,
FPS médio de **22,17**, e **46,0% dos intervalos acima de 33,33 ms**.
O tempo da GPU teve média **4,289 ms** e P95 de **14,156 ms**.
Esses dados sugerem pressão importante na CPU e na captura de comandos,
mas as etapas não devem ser somadas, pois podem se sobrepor.

### Defeito identificado no relatório de batching

A implementação Vulkan grava `vk_batches.csv` por **submissão**, não por
frame apresentado. Várias submissões legítimas recebem o mesmo identificador
de frame, especialmente prefixos offscreen e operações de sincronização.
O analisador anterior exigia IDs estritamente crescentes e descartou
**5.547 linhas**; portanto, os totais históricos de agrupamento calculados
dessa forma não são confiáveis como totais da execução.

A correção aceita identificadores iguais, soma todas as submissões de cada
frame e considera frames únicos em `--window`. Não altera a ordem dos draws
nem a renderização. Repita o relatório para obter a contagem correta.

### Decisões e próximos testes

- A reutilização intradraw da 04D.7 apresentou **0%** no teste observado:
  não se deve anunciar ganho de FPS a partir dessa mudança.
- O custo de verificações repetidas de memória e os picos de decodificação
  ainda precisam de investigação, sem ignorar escritas não notificadas do
  jogo na memória do Xbox.
- Separe **Londres em Quick Race**, a seleção de carros e Pause/RECOMP.
- Compare versões sobre a mesma pista/trecho e com escala interna 100%,
  MSAA desligado, FXAA ligado; depois meça o MSAA 2× separadamente.
- A meta de 30 FPS (33,33 ms) e, mais adiante, 60 FPS (16,67 ms) não foi
  comprovada para todas as cenas.

## English

### Measurement scope

The user supplied `texture_report.py`, `report_batches.py` and
`frame_report.py` results for the RX 580. The session contains **8,064
frame records**, but does not label which records belong to London gameplay,
menus, loading or pauses. Do not treat a whole-session average as a
single-track benchmark.

| Metric | Entire session | Last 300 frames |
| --- | ---: | ---: |
| Mean `frame_ms` | 45.046 ms | 16.664 ms |
| P95 `frame_ms` | 144.710 ms | 18.022 ms |
| Mean `capture_ms` | 22.387 ms | 5.686 ms |
| Mean `texture_check_ms` | 8.830 ms | 3.370 ms |
| Mean `decode_ms` | 2.671 ms | 0.022 ms |
| Intra-draw texture reuses | 0 | 0 |

The session recorded **9,258,304 static texture binds**, **9,091,908
`memcmp` calls**, **167,580 decoded snapshots**, and **881,687 MiB of
requested comparison lengths** (109.3 MiB/frame upper bound).
There were **8,052 successful presentations**, a **22.17 FPS** mean, and
**46.0% of intervals exceeded 33.33 ms**. Mean GPU timestamp span was
**4.289 ms**, P95 **14.156 ms**. These numbers suggest significant
CPU-side capture pressure; pipeline stages may overlap and must not be
summed as if serial.

### Batching report correction

`vk_batches.csv` is written **once per submission**. Multiple legitimate
submissions share one guest frame number. The previous report erroneously
discarded **5,547 rows** for duplicate frame IDs; those old aggregate
batching totals should not be used as accurate whole-session totals.
The corrected reader retains repeated IDs, aggregates them per unique
frame and computes windows over unique frames. Re-run the report.

### Follow-up validation

04D.7 recorded **zero intra-draw texture reuses**, so it has not proven an
FPS improvement on this workload. Investigate guest texture changes and
decoding spikes without assuming `DirtyEpoch` sees every direct write.
Benchmark London Quick Race, vehicle selection and Pause/RECOMP in separate
frame ranges, at matched internal scale and AA settings. Neither 30 FPS
minimum nor 60 FPS stability has been demonstrated across all scenes.
