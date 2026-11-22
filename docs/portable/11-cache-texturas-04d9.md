# 04D.9 — Diagnóstico do cache / Texture cache diagnostics

## Português (Brasil)

### Evidências da RX 580

A sessão apresentou 3.109.670 comparações de texturas em 3.309
frames. Nos últimos 300 frames, a captura da CPU consumiu 52,206 ms
por frame, com aproximadamente 2.603 comparações e 54,79 snapshots
decodificados por frame. A GPU teve média de 3,437 ms na sessão completa.
O trecho final ainda precisa de identificação visual para ser tratado
como um benchmark representativo da pista de Londres.

**A etapa 04D.8 não reutilizou mipmaps nessa amostra**. Ainda não sabemos
se isso decorre de dados realmente alterados, descritores diferentes ou
substituições frequentes das entradas do cache.

### Instrumentação e limites

Na etapa 04D.9, o cache da CPU mantinha 512 entradas por padrão. Após a comparação A/B, a etapa 04D.10 passou a utilizar 1.024 entradas no Vulkan nativo (o OpenGL legado continua em 512). As novas colunas de
`frames.csv` e `submissions.csv` são:

- `texture_cache_evictions`: remoções por limite de entradas;
- `texture_cache_misses`: snapshots decodificados sem cache anterior;
- `texture_descriptor_redecodes`: decodificação por mudança de layout;
- `texture_guest_redecodes`: decodificação por mudança real dos bytes;
- `texture_cache_entries_peak`: máximo de entradas ocupadas, sem soma.

Os três motivos de decodificação devem somar `texture_decodes`.
Não se eliminou nenhuma verificação obrigatória entre draws; o jogo
pode escrever diretamente nos endereços de textura sem `MarkDirty()`.

Para investigar lotação, a variável `PGR1_VK_TEXTURE_CACHE_ENTRIES`
permite valores inteiros de 64 a 2048 (padrão atual: 1.024 no Vulkan; 512 no OpenGL legado). Valores inválidos
caem no padrão. O ajuste exige reiniciar o jogo. **1.024 entradas podem
aumentar significativamente a RAM**, sem garantia de ganho de FPS.

### Comparação A/B na mesma Quick Race

Teste A: `PGR1_VK_TEXTURE_CACHE_ENTRIES=512`

Teste B: `PGR1_VK_TEXTURE_CACHE_ENTRIES=1024`

Em ambos os casos mantenha `PGR1_VK_MSAA=0`,
`PGR1_VK_FXAA=1`, `PGR1_VK_INTERNAL_PERCENT=100`.
Preserve os `frames.csv` separados e execute:

```bash
python3 portable/tools/texture_report.py .local/benchmarks/cache512 --window 300
python3 portable/tools/texture_report.py .local/benchmarks/cache1024 --window 300
python3 portable/tools/frame_report.py .local/benchmarks/cache1024 --target-fps 30
```

Caso dominem as alterações reais de bytes, aumentar o cache
não elimina o gargalo. Caso dominem remoções e carregamentos
iniciais, a capacidade pode ser relevante. Áudio e Vulkan não foram
alterados por este diagnóstico.

## English

The RX 580 measured **3,109,670 texture comparisons** across 3,309
frames. Its last 300 frames averaged **52.206 ms of CPU capture**, about
2,603 comparisons and 54.79 newly decoded texture snapshots per frame.
The full-session GPU timestamp mean was 3.437 ms. Visual scene tagging
is required before treating the last 300 frames as a London-specific
benchmark. No mip levels were reused in the 04D.8 recording.

The new cache metrics distinguish **evictions**, **cold decodes**,
**descriptor-layout changes**, **actual guest byte changes** and **peak
cache entries**. The three decode reasons should sum to
`texture_decodes`. Guest bytes remain checked between draws to
detect writes that bypass `MarkDirty()`.

The 04D.9 experiment originally defaulted to **512 entries**. Since 04D.10, native Vulkan defaults to **1,024 entries**; the legacy OpenGL backend remains at 512. A bounded startup-only
`PGR1_VK_TEXTURE_CACHE_ENTRIES` override accepts 64..2048, using the backend's default for invalid values. Larger caches may consume substantially more
host RAM and are **not** guaranteed to increase FPS. Compare the
same London Quick Race with 512 vs 1024 entries while keeping
AA, resolution and scene identical. Audio routing is untouched.

## 04D.10 — Resultado A/B e novo padrão / A/B evidence and default

**Português (Brasil).** O teste posterior registrou 3.502 frames com cache
512 e 4.497 com cache 1.024 (sessões não equivalentes em duração/cena).
O cache menor realizou 52.763 expulsões e 53.275 carregamentos iniciais,
enquanto o maior apresentou **zero expulsões**, 1.013 carregamentos iniciais
e ocupação máxima de 1.013 entradas. Decodificações totais: 53.452
versus 1.166. Alterações reais de memória: 177 versus 153.

Os contadores comprovam o **cache thrashing** com 512 entradas; a diferença
de FPS entre as sessões não constitui benchmark controlado. O Vulkan nativo
agora usa 1.024 por padrão, preservando `PGR1_VK_TEXTURE_CACHE_ENTRIES=512`
para comparação ou economia de RAM. Ao trocar de pista, observe se há
expulsões com 1.024; para estudar outras cargas, há opções de até 2.048.

**English.** The subsequent A/B runs reported 3,502 frames at 512 entries
and 4,497 at 1,024 (not equal-length or scene-matched samples).
The smaller cache produced 52,763 evictions and 53,275 cold decodes.
The larger produced **zero evictions**, 1,013 cold decodes and a peak of
1,013 entries. Total decodes fell from 53,452 to 1,166; guest-byte
changes were 177 versus 153. These counts demonstrate cache thrashing
at 512, **not** a controlled FPS improvement. Native Vulkan now defaults
to 1,024 while legacy OpenGL remains at 512; the environment override
still supports lower RAM use or a larger capacity.
