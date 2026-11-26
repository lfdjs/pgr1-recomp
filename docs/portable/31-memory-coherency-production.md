# 04D.21 — Memory safety and runtime bulk operations / Segurança de memória e operações em bloco

## Português (Brasil)

**IMPLEMENTADO / TESTADO:** otimizações REP no runtime real, correções de acesso
SIMD desalinhado e da extensão m64, eventos diagnósticos de ciclo de vida e
testes de instruções efetivamente geradas pelo SDK. **PENDENTE:** cobertura
sincronizada dos escritores guest, fast hits seguros e eliminação de memcmp.
O título deste documento registra a frente de produção; não afirma que o
contrato completo de coerência esteja concluído.

O estado inicial é PGR1 `0eb9099`, SDK `97b1654736f744a3966b5dff3d49be6d61ac7244`
na branch `pgr1-memory-audit`, com o patch diagnóstico 04D.19D já aplicado
e sem commit. Todas essas mudanças foram preservadas. O novo patch consolidado
inclui essa base e a melhoria de montagem de geometria descrita no documento 33.
O gitlink do projeto permanece na revisão pública original.

### Operações REP e correção dos aliases

`x86_rep_movs` agora usa `memmove` também no sentido reverso quando o resultado
é comprovadamente equivalente à execução por elemento. Cópias que propagam
bytes por sobreposição permanecem por elemento; o valor é carregado para um
temporário antes do store, inclusive quando elementos individuais se sobrepõem.
O cálculo do intervalo usa 64 bits e rejeita wrap antes de autorizar a operação
em bloco. Os índices finais continuam obedecendo ao wrap de 32 bits do x86.

As views físicas `0x80000000` e `0xF0000000` podem apontar para bytes
sobrepostos mesmo com endereços virtuais muito distantes. Esse caso mantém
execução por elemento: o `memmove` da libc decide a direção pelo endereço
virtual e não comprova a relação dos aliases. O teste mapeia realmente as duas
views sobre o mesmo memfd; não se limita a simular endereços.

`x86_rep_stos` usa `memset` para palavras/dwords de bytes repetidos, como
zero e `0xFFFFFFFF`, nos dois sentidos. Outros padrões preservam o caminho
por elemento. A otimização de produção funciona com a auditoria desativada,
sem lock ou incremento atômico por store.

Quando o diário opt-in está ativo, os caminhos REP de destino contíguo
registram um evento por página de destino ao término, em vez de um por
elemento. Intervalos com wrap mantêm notificações individuais. O diário
continua posterior à escrita: sua sequência conta eventos conservadores,
não instruções/bytes; não fornece exclusão de leitores nem prova de cobertura.
Endereços virtuais/stack sem interseção física são rejeitados antes da leitura
atômica do estado de habilitação do diário.

### Geração SIMD

O gerador usava dereference de `X86Xmm*` para operandos MOVUPS/MOVDQU
desalinhados, contrariando o alinhamento C++ do tipo. As cargas/stores agora
usam `memcpy`, permitindo que o compilador selecione instruções adequadas sem
impor alinhamento ao guest. As conversões CVTPS2PI/CVTTPS2PI de memória
consumiam 16 bytes apesar de o operando ser m64; agora leem exatamente oito.
O teste de conversão coloca o último byte na borda de uma página mapeada,
seguida de `PROT_NONE`, e verifica o resultado numérico arredondado.
É obrigatório reconstruir `xbe_recomp` e regenerar o jogo antes do build final.

### Matriz de cobertura atualizada

| Família real | Evidência/teste desta revisão | Sincronização que permite reutilizar bytes |
| --- | --- | --- |
| MOV escalares 1/2/4 bytes | Instruções decodificadas, emitidas, compiladas e executadas; aliases e cruzamento de página | Unknown |
| Stores x87 32/80 bits | FSTP real gerado, valores e extensão conferidos | Unknown |
| Stores SSE/MMX | MOVDQU, MOVLPS, MOVHPS, MOVQ reais; acesso desalinhado | Unknown |
| Atômicas/bit strings | LOCK ADD e BTS reais; endereço efetivo deslocado por índice de bit | Unknown |
| Stack | PUSH EAX gerado e bytes conferidos | Unknown |
| REP MOVSD/STOSD | Instruções geradas e helpers reais, direções, propagação por overlap, aliases | Unknown |
| Helpers W16/W32/W64 | Chamadas reais, aliases e cruzamento de página | Unknown |
| Alocação/liberação/reuso | AllocContiguous/AllocHostContiguous/FreeContiguous reais; source 9 MemoryLifecycle | Unknown |
| Arquivos/RTL selecionados | Instrumentação prévia preservada; não certificada por esses fixtures | Unknown |
| G/HostPtr, rede, áudio, entrada, threads, hooks do jogo, ponteiros externos | Permanecem caminhos abertos; inventário nos documentos 24/25 | Unknown |
| Cópias/readback e autoridade GPU | Contrato existente do backend preservado; não equivale à cobertura dos demais escritores | Unknown |

Os eventos de ciclo de vida marcam alloc/free/reuse mesmo quando os bytes não
mudam. Não revogam automaticamente registros do tracker cooperativo: o SDK
continua em um domínio diagnóstico distinto. Os testes acima não certificam
todas as variantes do ISA, atomics, I/O ou concorrência do jogo.

**Nenhuma região guest foi promovida a Cooperative.** `exact` continua sendo
a referência, `audit` compara todos os bytes e `tracked` mantém fallback exato
integral nas regiões Unknown. Fast hits comprovados: zero. Redução efetiva de
memcmp por esta frente: zero. Um watch bitmap com checagem posterior ao store
não resolveria sozinho a corrida registro/escritor nem os ponteiros que escapam;
não foi adicionado um mecanismo que aparentasse satisfazer esse contrato.

### Testes e microbenchmark

Quatro novos CTests executam: REP de produção, REP com diário, alloc/free/reuse
e helpers reais, e 16 funções de instruções geradas. A geração usa a análise
e o CodeGen reais do SDK com Zydis 4.1.1 e bytes sintéticos públicos, sem assets.
Os testes cobrem 942 combinações REP por build, 84 cópias com aliases reais
por build e 64 casos gerados (16 funções × dois aliases × duas direções).
`PGR1_ZYDIS_TEST_SOURCE` permite apontar para uma cópia local de Zydis; se ela
não existir, o CMake anuncia a indisponibilidade do teste gerado.

Microbenchmark no host desta sessão, GCC `-O2`, 256 operações de 1 MiB, auditoria
desativada. Comparação contra o header da revisão SDK fixada; checksums iguais.

| Operação | Antes (ms) | Depois (ms) |
| --- | ---: | ---: |
| STOSD, frente, zero/FF alternado | 129,339 | 15,735 |
| STOSD, reverso, zero/FF alternado | 128,946 | 16,171 |
| MOVSD, frente, intervalos separados | 22,715 | 22,426 |
| MOVSD, reverso, intervalos separados | 131,666 | 21,904 |

São operações isoladas em memória quente, executadas enquanto outras tarefas
da sessão podiam estar ativas; não comprovam impacto no tempo guest da corrida
nem permitem extrapolar FPS. Arquivos locais: `.local/validation/04d21/memory/`.

Reprodução sem assets, após aplicar o patch consolidado em SDK limpo:

```sh
cmake -S portable/linux/vulkan -B out/pgr1-memory-tests -DCMAKE_BUILD_TYPE=Release
cmake --build out/pgr1-memory-tests --target pgr1_sdk_rep_runtime_test \
  pgr1_sdk_rep_audit_test pgr1_sdk_memory_lifecycle_test pgr1_sdk_generated_stores_test -j2
ctest --test-dir out/pgr1-memory-tests -R 'sdk_(rep|memory_lifecycle|actual_generated)' --output-on-failure
c++ -std=c++20 -O2 -DNDEBUG -Isdk/runtime \
  portable/tests/memory/benchmark_sdk_rep_runtime.cpp -o /tmp/pgr1-rep-benchmark
/tmp/pgr1-rep-benchmark
```

### Patch e instalação limpa

`portable/patches/sdk-97b1654-performance-overhaul.patch` é cumulativo contra
`97b1654`: substitui a aplicação do patch 04D.19D em checkout limpo. Não aplicar
os dois cumulativamente. A etapa verificou aplicação em árvore limpa extraída
via `git archive`, preservando o checkout modificado do usuário. O README de
patches contém os comandos e a ordem. O conteúdo gerado e os assets ficam fora
do Git. Nenhum remote de terceiros é modificado/publicado.

## English

Implemented and tested: checked forward/backward REP bulk paths; byte-pattern
STOS memset for word/dword zero/FF fills; physical-alias-aware overlap fallback
with staged element loads; batched diagnostic page events; fast rejection of
unrelated virtual/stack ranges; real allocation/free/reuse journal notifications;
portable unaligned SIMD memcpy accesses; exact eight-byte CVTPS2PI m64 loads.
The production REP improvements do not require instrumentation.

The four new CTests run real SDK helpers, allocators/HLE stores, and 16 real
analysis→CodeGen→compiled instruction functions (64 executions). REP differential
coverage is 942 combinations plus 84 actual shared-mapping alias copies per
build. The m64 conversion fixture ends at an unmapped page boundary. Synthetic
REP timings above compare the public pinned header with the new header and do
not establish gameplay FPS or frame-tail improvements.

The coverage table distinguishes observed post-store notifications from actual
synchronized byte access. Every guest region remains Unknown: exact reference,
full audit comparisons and tracked fallback are preserved. Proven guest fast
hits and comparisons removed by this work are both zero. Allocation notifications
do not revoke the separate cooperative tracker's coverage. Raw G/HostPtr, external
library pointers, remaining HLE/XDK/network/audio/game writers and concurrency
still prevent a complete production coherency claim. No bitmap, generation or
seqlock on non-atomic bytes is offered as a substitute for that missing contract.

The consolidated SDK patch includes the preexisting user audit modifications
and this session's runtime/geometry changes, applies once to the clean public
pin, and preserves the parent repository's public gitlink. Rebuild the compiler,
regenerate code, then build the final game. No proprietary assets, generated
game code or gameplay captures are included.
