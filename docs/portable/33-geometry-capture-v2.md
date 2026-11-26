# 04D.21 — Native converted primitives / Primitivas convertidas nativas

## Português (Brasil)

**IMPLEMENTADO e TESTADO:** o caminho convertido real de
`sdk/runtime/gpu/vertex.cpp` elimina a sequência temporária para entradas
densas e mantém primitivas nativas de pontos, linhas, triângulos e strips.
O caminho raw já utilizava essas topologias. Draws sem índices e gathers
esparsos possuem vértices na ordem necessária e dispensam a materialização
de índices sequenciais. Draws indexados densos possuem índices normalizados
de 32 bits; nenhum ponteiro guest segue para o consumidor assíncrono.
Quads, fans, polygons e line loops continuam usando a montagem anterior.

A sequência de gather existe somente para entradas esparsas. Seu vetor
thread-local retém capacidade e recebe todos os índices atuais a cada draw;
isso é scratch de trabalho, sem reutilização de geometria ou de bytes guest
entre draws. Não há novo requisito de CPU, worker, estado de cobertura ou
dependência de uma API gráfica específica.

O backend de captura já aceita strips e draws sem índices. O renderer Vulkan
já mapeia essas topologias diretamente e usa `vkCmdDraw` quando não há índices.
O conversor flat continua expandindo strips para preservar o último provoking
vertex do Xbox. Ordem, winding, degenerados e caudas incompletas mantêm o
resultado anterior. Não há cache entre draws: declarações/FVF, strides,
base/start, streams e dados dinâmicos continuam sendo lidos em cada montagem.
O contrato desconhecido de escritores não permite promover memória guest
mutável a geometria imutável.

### Evidência de correção

`sdk_vertex_assembly_differential` compila simultaneamente a implementação
atual e a implementação original do pin
`97b1654736f744a3966b5dff3d49be6d61ac7244`, com símbolos públicos distintos.
`build_vertex_reference.py` extrai somente código SDK via `git show` para
o diretório de build. Não extrai jogo, assets ou código recompilado.
O submódulo precisa preservar o objeto Git desse pin, como em uma instalação
limpa normal. Falha ao obter a referência interrompe o build do teste.

Foram aprovados 19.600 casos diferenciais: as dez topologias Xbox; contagens
0–34; FVF pretransformed e três configurações de declaração/streams, incluindo
`SetVertexShaderInput`, strides explícitos e derivados da declaração;
índices de 16 bits desalinhados, densos, esparsos, repetidos e reversos;
start/base vertex, inclusive base negativo válido; payload float4 convertido
completo; ordem/winding e saída flat. Há também alteração de dados no mesmo
frame e preservação de cópia própria anterior. A referência preserva o
decodificador original, incluindo SHORT normalizado desalinhado, D3DCOLOR,
FLOAT2H e NORMPACKED3.

O teste passou em CTest Release e em build separado com warnings estritos
`-Werror`, AddressSanitizer e UBSan. `ASAN_OPTIONS=detect_leaks=0` foi usado;
detecção de vazamentos não foi validada. Também passaram os três testes
reais de captura Vulkan nos modos exact, audit e tracked com fallback,
e os testes de captura SDK e montagem diferencial da suíte OpenGL legada.
Esses testes usam memória sintética e SDK real; a validação visual de uma
corrida completa permanece separada. Windows e outras arquiteturas não
foram executados nesta etapa.

### Microbenchmark local

Uma execução local de 500 montagens de 8.192 vértices por caso, com buffers
aquecidos, produziu a tabela abaixo. Tempos incluem montagem SDK, sem expansão
posterior dos 16 registradores, upload Vulkan, GPU ou gameplay. Há variação
de frequência/carga da máquina; os números não estabelecem ganho de FPS.

| Primitiva | Indexada | Original (ms) | Nova (ms) | Índices originais/draw | Índices novos/draw |
| --- | --- | ---: | ---: | ---: | ---: |
| Triangle list | Não | 118,425 | 106,386 | 8.190 | 0 |
| Triangle list | Sim | 125,173 | 116,425 | 8.190 | 8.192 |
| Triangle strip | Não | 131,104 | 108,379 | 24.570 | 0 |
| Triangle strip | Sim | 133,633 | 119,050 | 24.570 | 8.192 |
| Quad list | Não | 115,434 | 115,403 | 12.288 | 12.288 |
| Quad list | Sim | 121,518 | 118,032 | 12.288 | 12.288 |

As caudas incompletas de listas são descartadas pelo input assembler, sem
alterar primitivas completas. A redução de índices do strip indexado é
66,7%; a do strip sem índices é 100% antes de eventual conversão flat.
Os tempos da tabela e logs de sanitizers ficam em
`.local/validation/04d21-geometry/`, fora do Git.

Reprodução, sem assets:

```bash
cmake -S portable/linux/vulkan -B out/pgr1-vulkan-wsi
cmake --build out/pgr1-vulkan-wsi --target pgr1_sdk_vertex_assembly_test -j2
ctest --test-dir out/pgr1-vulkan-wsi -R sdk_vertex_assembly_differential --output-on-failure
out/pgr1-vulkan-wsi/pgr1_sdk_vertex_assembly_test --benchmark

c++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections -DXBE_RAW_VERTICES=0 \
  -Iportable/linux/gldirect -Isdk/runtime -Isdk/harness/compat \
  portable/linux/gldirect/tests/test_sdk_vertex_assembly.cpp \
  sdk/runtime/gpu/vertex.cpp out/pgr1-vulkan-wsi/sdk_vertex_reference.cpp \
  -Wl,--gc-sections -o /tmp/pgr1-sdk-assembly-sanitized
ASAN_OPTIONS=detect_leaks=0 /tmp/pgr1-sdk-assembly-sanitized
```

O mesmo teste está integrado à suíte OpenGL independente. O patch SDK
consolidado desta sessão inclui a mudança de montagem junto das alterações
de escritores. O gitlink permanece no pin; siga o handoff para aplicar o
patch somente em um checkout limpo, preservando mudanças locais existentes.

## English

**IMPLEMENTED and TESTED:** the actual SDK converted assembler avoids dense
input sequence allocation and preserves native point/line/triangle lists and
strips, as the raw path already does. Nonindexed and sparse-gather draws need
no sequential index array. Dense indexed draws own normalized 32-bit indices;
guest pointers never escape. Quads/fans/polygons/line loops retain their
existing list assembly. Sparse gather scratch retains capacity per thread,
but is overwritten from current input on every draw; this is not a geometry
cache or a writer-coverage claim.

The existing capture and Vulkan consumers support native strips and
nonindexed draws. Flat-shading conversion retains Xbox provoking colors,
winding, primitive order and incomplete-tail behavior. Every draw still reads
current layout/FVF, streams, stride, base/start and dynamic data. Unknown
writer coverage prevents safely caching mutable guest geometry across draws.

The differential CTest links current SDK assembly against its original pinned
implementation extracted into the build tree, with renamed public symbols.
Only SDK source is extracted; the test requires the pinned Git object and
fails explicitly if unavailable. All 19,600 cases passed: ten topologies,
counts 0–34, four layouts/stream configurations, unaligned 16-bit indices,
dense/sparse/repeated/reversed sequences, valid negative base offsets,
complete converted float4 bytes and expanded flat primitive order/winding.
Mutation and owned-copy lifetime are also checked. The original decoders
cover normalized unaligned SHORT, color, FLOAT2H and packed formats.

Release CTest, strict `-Werror` ASan/UBSan, three real Vulkan backend
capture modes and legacy OpenGL SDK capture/differential assembly passed.
Leak detection was disabled and remains unvalidated.
Synthetic guest memory is used; full-race visual validation, Windows and
other architectures remain separate. Commands above reproduce without assets.

The table records one warmed local microbenchmark, 500×8,192 vertices, with
original/current assembly in the same executable. Indexed strips materialize
66.7% fewer indices; nonindexed strips eliminate them completely before flat
conversion. Timings exclude later attribute expansion, GPU upload and gameplay,
vary with machine load and do not establish an FPS improvement. Local raw logs
are outside Git under `.local/validation/04d21-geometry/`. The session's
consolidated SDK patch carries this change; the submodule pin is unchanged.
