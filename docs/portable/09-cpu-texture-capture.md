# 04D.7 — CPU texture capture: per-draw reuse and telemetry

> [English](#04d7--cpu-texture-capture-per-draw-reuse-and-telemetry) · [Português (Brasil)](#português-brasil--etapa-04d7)

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


---

## Português (Brasil) — Etapa 04D.7

### Motivação e segurança

O código recompilado do Xbox pode escrever diretamente em bytes de textura
sem chamar `MarkDirty()` do SDK. Por isso, o backend Vulkan precisa continuar
validando os bytes **entre chamadas diferentes de `Draw()`**. Apenas consultar
`DirtyEpoch()` não assegura a correção das texturas.

A etapa 04D.7 só permite reutilizar uma imagem decodificada quando dois
estágios de textura do **mesmo Draw síncrono** usam endereço, formato,
dimensões, mipmaps, pitch e layout idênticos. A configuração de sampler
permanece independente por estágio. Texturas produzidas pela GPU e faces
de cubemaps dinâmicos mantêm seus caminhos específicos.

### Contadores acrescentados

As colunas abaixo aparecem em `frames.csv` (agregado por apresentação)
e `submissions.csv` (por submissão):

| Coluna | Significado |
| --- | --- |
| `texture_static_binds` | Vínculos com texturas estáticas apoiadas pela memória do Xbox |
| `texture_same_draw_reuses` | Reutilizações entre estágios do mesmo draw |
| `texture_memcmp_checks` | Chamadas reais de comparação de bytes |
| `texture_memcmp_bytes` | Limite superior do tamanho solicitado às comparações; não é a quantidade efetivamente lida |
| `texture_decodes` | Novos snapshots de textura decodificados |

Os tempos `texture_check_ms` e `decode_ms` continuam medindo a captura
na CPU. Os testes de SDK verificam que alterações feitas sem marcação
entre dois draws diferentes continuam visíveis.

### Testar e comparar

```sh
JOBS=4 bash scripts/portable-vulkan.sh build-game
bash scripts/portable-vulkan.sh test
python3 portable/tools/texture_report.py .local/run/vulkan --window 300
python3 portable/tools/report_batches.py .local/run/vulkan --window 300
python3 portable/tools/frame_report.py .local/run/vulkan --target-fps 30
```

Execute a mesma parte da Quick Race de Londres em resolução interna 100%,
MSAA desligado e FXAA ligado. Compare intervalos equivalentes de gameplay;
menus e carregamentos devem ser medidos separadamente.

**Resultado observado em 22/11/2026:** zero reutilizações no mesmo draw,
apesar de 9,26 milhões de vínculos de texturas estáticas. Portanto, essa
otimização não apresentou benefício demonstrado naquele teste. Os novos
contadores revelaram volume elevado de comparações e picos de decodificação.
Consulte a [auditoria 04D.7](10-auditoria-performance-2026-11-22.md).

Nenhum código de áudio, pós-processamento, MSAA ou reflexão foi alterado
pela otimização original 04D.7.
