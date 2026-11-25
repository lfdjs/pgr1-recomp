# 04D.20 — Flat primitive index capture / Captura de índices flat

## Português (Brasil)

Implementado: `flat_primitive_indices.h` elimina os vetores intermediários
da conversão de provoking vertex na captura Vulkan. O Xbox fornece a cor
flat pelo último vértice da primitiva; o Vulkan base pelo primeiro. A
conversão anterior copiava os índices montados pelo SDK, movia a cópia para
um vetor temporário e construía outro vetor com `push_back`. Quando o draw
não era indexado, materializava também a sequência `0..vertex_count-1`.

O helper lê diretamente um `span` dos índices **já convertidos e próprios
do SDK**, ou calcula índices implícitos. Calcula a quantidade exata de
primitivas e dimensiona um único vetor de saída. Não altera o SDK, os
vértices, as constantes, o layout/FVF, os draws não flat ou a ordem dos
draws. Triângulos de strip mantêm a alternância de winding; linhas mantêm
seus extremos e ordem. Caudas incompletas das listas continuam descartadas
como antes. Points preservam os índices de entrada.

Não há cache de geometria entre draws. Vértices dinâmicos e novas entradas
são lidos em cada captura. O resultado possui seus índices; nenhuma
referência à memória guest ou ao scratch SDK segue para o consumidor
assíncrono. Reutilização de geometria entre draws continua pendente de
cobertura real dos escritores e sincronização no tracker de memória.

### Verificação

`test_flat_primitive_indices.cpp` compara com a implementação anterior:
2.580 combinações de topologia, contagem e origem indexada/implícita,
incluindo listas incompletas, strips degenerados e índices repetidos ou
fora de ordem. Inclui valores de índices de 16 bits já expandidos para
32 bits pelo SDK, mutações de origem e preservação de saída já submetida.

Compilado e executado no Intel Core i7-2700K com C++20, `-O2 -Wall
-Wextra -Wpedantic -Werror -UNDEBUG`. Passou também em `-O1 -g
-fsanitize=address,undefined -fno-omit-frame-pointer`, usando
`ASAN_OPTIONS=detect_leaks=0`. O LeakSanitizer padrão falhou porque este
ambiente executa sob ptrace; detecção de vazamentos **não foi validada**.
Não foram desativados os checks de AddressSanitizer/UBSan.

O microbenchmark opcional usa 4.000 conversões de 8.192 índices/vértices
por caso. Nesta execução, as durações totais foram:

| Origem | Topologia de entrada | Anterior (ms) | Direto (ms) |
| --- | --- | ---: | ---: |
| Implícita | Lines | 87,418 | 16,812 |
| Implícita | Line strip | 183,109 | 34,862 |
| Implícita | Triangles | 78,516 | 13,631 |
| Implícita | Triangle strip | 394,858 | 61,959 |
| Indexada | Lines | 52,010 | 19,939 |
| Indexada | Line strip | 187,856 | 37,584 |
| Indexada | Triangles | 59,125 | 19,518 |
| Indexada | Triangle strip | 353,893 | 48,437 |

Essas são medições sintéticas de conversão e alocação, sem GPU, não uma
medição de FPS ou de captura de uma corrida. A frequência de draws flat
no gameplay e seu peso no frame precisam ser medidos antes de atribuir
impacto ao jogo. Não foi criado um worker nem requisito de AVX2.

Reprodução independente de assets:

```bash
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -UNDEBUG \
  -Iportable/linux/gldirect \
  portable/linux/gldirect/tests/test_flat_primitive_indices.cpp \
  -o /tmp/pgr1-flat-indices
/tmp/pgr1-flat-indices --benchmark
```

### Próximas medições

Comparar `capture_ms`, `assemble_ms`, draws/frame e custo de captura/draw
nas mesmas cenas de Londres. `assemble_ms` mede a montagem SDK e não
inclui esta conversão posterior; não se espera que ele caia só com esta
alteração. É necessário parear carro, câmera, percurso, resolução, perfil
de CPU, MSAA/FXAA e limitador. O benchmark sintético não substitui essa
validação. Cache de vértices/índices e alterações Vulkan adicionais não
foram implementados nesta etapa.

## English

`flat_primitive_indices.h` removes intermediate index arrays from Vulkan
flat-shading capture. Xbox selects the last primitive vertex's color;
base Vulkan selects the first. Previously capture copied SDK indices,
moved that copy into a temporary and built another vector with repeated
`push_back`; nonindexed draws also materialized sequential indices.

The helper reads SDK-owned converted indices synchronously, or computes
implicit indices. It sizes one owned output vector to the exact number
of complete primitives. Triangle strip winding, line order, incomplete
list tails and provoking colors match the previous implementation.
Vertices, declaration/FVF, constants, non-flat draws and draw ordering
are unchanged. No SDK or guest pointers escape in the result.

There is no inter-draw geometry cache: each conversion reads current
input, preserving dynamic geometry. Geometry caching remains dependent
on proven writer coverage and synchronization in the memory tracker.

The standalone test compares 2,580 combinations against the previous
algorithm, including incomplete lists, degenerate strips, repeated and
reordered indices, widened 16-bit indices, input mutation and independent
output lifetime. Strict C++20 Release compilation and ASan/UBSan passed.
LeakSanitizer could not run under the environment's ptrace; its detection
was disabled for the sanitizer run, so leaks were not validated.

On the i7-2700K, 4,000 synthetic conversions of 8,192 inputs took
52–395 ms with the old path and 14–62 ms with the direct path; see the
table for individual measurements. These include CPU conversion and
allocation only, with no GPU/gameplay. They establish no FPS gain.
The command above reproduces the asset-free test and microbenchmark.

Measure matched gameplay `capture_ms`, draws/frame and capture cost/draw.
`assemble_ms` covers the earlier SDK stage and does not include this
conversion. Keep car, camera, route, resolution, CPU profile, MSAA/FXAA
and frame limiter identical. Vertex/index caching and additional Vulkan
changes were not implemented in this stage.
