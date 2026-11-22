# PGR1 04D.10 — A/B do cache de texturas / Texture cache A/B

## Português (Brasil)

### Resultados observados na RX 580

| Indicador | Capacidade 512 | Capacidade 1.024 |
|---|---:|---:|
| Frames registrados | 3.502 | 4.497 |
| Expulsões do cache | 52.763 | **0** |
| Decodificações iniciais (misses) | 53.275 | 1.013 |
| Decodificações totais | 53.452 | 1.166 |
| Alterações reais de bytes | 177 | 153 |
| Pico de entradas ocupadas | 512 | 1.013 |
| Tempo médio de captura CPU | 21,093 ms | 18,117 ms |
| Tempo médio por frame | 37,653 ms | 31,271 ms |

No cache de 512 entradas, 53.275 carregamentos iniciais menos 52.763
expulsões equivale às 512 entradas iniciais. Portanto, praticamente todos
os carregamentos posteriores foram causados por **expulsões/reentradas**
(efeito de rotatividade do cache). Com capacidade de 1.024, nenhuma
expulsão ocorreu na amostra: havia no máximo 1.013 entradas residentes.

Os dois registros correspondem a **sessões diferentes**, com quantidades
de frames e cenas possivelmente distintas. A queda de decodificações é uma
evidência forte do fim da rotatividade, mas a diferença no tempo médio
por frame não estabelece isoladamente um ganho causal de FPS.

### Alteração 04D.10

- Vulkan nativo passa a usar **1.024 entradas** por padrão.
- OpenGL legado permanece com **512** por padrão.
- `PGR1_VK_TEXTURE_CACHE_ENTRIES=512` conserva o perfil anterior para
  comparação ou para sistemas com menos RAM.
- A faixa explícita de 64 a 2.048 entradas continua disponível.
- Comparações completas entre draws continuam necessárias para detectar
  escritas diretas do jogo em texturas do Xbox.
- Áudio, MSAA, reflexos e ordem de desenhos não são modificados.

O cache guarda snapshots da memória do jogo e pixels decodificados;
1.024 entradas podem consumir mais memória do sistema. Verifique
o uso real de RAM da aplicação nos próximos testes.

### Próxima validação

Use Londres em Quick Race com a mesma pista, carro, câmera, duração e
configurações, MSAA desligado, FXAA ligado e escala interna 100%.
Confira a mensagem de capacidade no `console.log`, os novos contadores
e os percentis de `frame_report.py`. Não misture menus, carregamento
e gameplay na mesma comparação.

## English

### RX 580 findings

The 512-entry run logged 3,502 frames, 52,763 evictions and 53,275 cold
decodes. The 1,024-entry run logged 4,497 frames, **zero evictions**,
1,013 cold decodes and a peak occupancy of 1,013. Total snapshot decodes
were 53,452 versus 1,166; guest byte changes were 177 versus 153.

For 512 entries, `53,275 - 52,763 = 512`. Nearly every subsequent
cold decode was caused by cache churn. A 1,024-entry capacity contained
the observed working set without evictions.

These are different-length runs with potentially different scenes, so
their average frame times do **not** establish a controlled FPS gain.
However, the cache counters directly identify avoidable re-decoding.

### New baseline

Native Vulkan now defaults to **1,024 entries** while legacy OpenGL
remains at **512**. The optional
`PGR1_VK_TEXTURE_CACHE_ENTRIES` override still accepts 64..2,048,
so users can trade off RAM use and re-decoding. The renderer retains
exact guest byte checks between draws and does not change audio,
anti-aliasing or texture correctness rules.

Further testing should isolate the same London Quick Race scene,
compare 30/60 FPS frame-time percentiles and inspect memory use and
evictions. Neither a never-below-30 guarantee nor stable 60 FPS is
established by this change.
