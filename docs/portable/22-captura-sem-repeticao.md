# 04D.18C — Eliminar trabalho repetido / Remove repeated capture work

## Português (Brasil)

O teste experimental 04D.18B registrou 26,90 FPS contra 28,26 na
sessão native/O2 anterior. A captura P95 chegou a 64,588 ms, contra
47,359 ms. Nos últimos 300 frames foram 777.477 verificações exatas,
zero alterações de bytes e 45,406 ms de captura média. A GPU teve
máximo de 13,328 ms na sessão inteira. As cenas e durações não são
pareadas. A sobreposição permanece desativada por padrão.

Esta revisão reduz trabalho da CPU sem criar novos workers:

- Cache limitado a 64 slots de programas de vértices. A igualdade de
  todos os dwords prova o acerto, e não a identidade do objeto. Evita
  revalidar opcodes e recalcular o hash SDK quando o programa é igual.
  Mutações e opcodes não suportados continuam sendo detectados.
- O Assembled temporário do SDK reutiliza capacidade por thread de
  captura. Os comandos enviados continuam donos de seus vértices e
  índices. O SDK fixado reinicia os vetores convertidos em Assemble().
- Texturas com descritor idêntico reutilizam o tamanho exato do
  snapshot existente em vez de percorrer novamente os mipmaps.
  O passo entre faces de cubemap é calculado uma vez por vínculo.
  Toda comparação de bytes entre draws permanece obrigatória.

O teste C++20 isolado do cache de programas foi compilado e executado
com -O2 -Wall -Wextra -Wpedantic -Werror. O teste integrado de captura
foi ampliado para verificar constantes atualizadas e rejeitar mutação
do mesmo objeto de shader. Compilação integrada e desempenho continuam
pendentes. Não se afirma ganho de FPS antes dos testes.

O rastreamento seguro de escritas da RAM guest continua em aberto:
as duas views físicas compartilhadas e todos os escritores devem
ser cobertos antes de eliminar memcmp. Não usar DirtyEpoch, identidade
de ponteiro, hash amostral ou tempo de frame como prova de imutabilidade.

## English

04D.18B reported 26.90 FPS vs 28.26 in the previous native/O2 session,
capture P95 64.588 vs 47.359 ms, and no guest-byte changes over the
last 300 frames. The sessions are not scene-matched. Keep the overlap
experiment disabled by default.

This revision removes repeated CPU work without adding workers:
a bounded 64-slot vertex-program cache reuses validation and the SDK
hash only after exact word equality; converted SDK assembly scratch
retains capacity on its producer thread; texture sizes are reused
from exact matching descriptor snapshots, and cube face stride is
computed once per binding. Owned submissions and full guest byte
comparison are preserved.

The standalone C++20 cache test passed with strict warnings. The SDK
capture test additionally checks updated constants and same-object
shader mutation; integrated tests and game performance are pending.
Safe guest write tracking remains open and must cover both physical
RAM aliases and all writers before removing exact comparisons.
