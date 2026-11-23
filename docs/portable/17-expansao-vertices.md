# 04D.15 — Montagem de vertices na CPU / CPU vertex expansion

## Portugues (Brasil)

O backend de captura percorria os 16 registradores NV2A de entrada
para **cada vertice** da geometria capturada, mesmo que os atributos
ativos nao mudassem dentro de um draw. O novo
`VertexRegisterExpander` calcula essa mascara **uma vez por draw**
e a aplica aos vertices subsequentes, preservando integralmente os
valores padrao de cada atributo, a ordem dos registros e os dados
originalmente decodificados pelo SDK do Xbox.

A melhoria e compartilhada pelos backends de captura Vulkan/OpenGL
e nao altera os dados do jogo, a simulacao ou o audio. O teste C++20
`vk_vertex_register_expansion` compara a nova implementacao com a
rotina anterior para centenas de mascaras, incluindo registros vazios
e todos os 16 atributos.

Esta alteracao visa reduzir `assemble_ms` e o custo de captura CPU.
O impacto real de FPS depende da quantidade de vertices da cena;
nao e uma garantia de 60 FPS.

## English

The capture backend previously walked all 16 NV2A vertex registers for
**every captured vertex** even though the register mask is unchanged
within a draw. `VertexRegisterExpander` now computes the active register
indices once per draw and expands the vertex array using the same
defaults and SDK-decoded attribute ordering.

The shared Vulkan/OpenGL capture path preserves render semantics,
guest simulation and audio. The standalone C++20 test
`vk_vertex_register_expansion` compares the optimized code to the
original implementation over hundreds of register masks.
This targets `assemble_ms` and guest CPU capture cost; measure
matching game scenes before claiming an FPS improvement.
