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

O cache da CPU mantém 512 entradas por padrão. As novas colunas de
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
permite valores inteiros de 64 a 2048 (padrão 512). Valores inválidos
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

The default cache remains at **512 entries**. A bounded startup-only
`PGR1_VK_TEXTURE_CACHE_ENTRIES` override accepts 64..2048, using
512 for invalid values. Larger caches may consume substantially more
host RAM and are **not** guaranteed to increase FPS. Compare the
same London Quick Race with 512 vs 1024 entries while keeping
AA, resolution and scene identical. Audio routing is untouched.
