# 04D.18B — Sobreposição de comparações exatas / Exact comparison overlap

## Português (Brasil)

O teste native/O2 no i7-2700K + RX 580 registrou 28,26 FPS (sessão
anterior 26,56; cenas e durações não pareadas). P95 de captura subiu
de 43,322 para 47,359 ms. Nos últimos 300 frames foram 733.698
comparações, 183,4 MiB solicitados/frame, zero mudanças de bytes,
14,069 ms de verificação/frame e 34,974 ms de captura/frame.

A opção experimental `PGR1_VK_TEXTURE_OVERLAP=1` usa um worker
persistente para comparar **todos os bytes** de uma textura de cache
de pelo menos 256 KiB enquanto a thread de captura executa a montagem
SDK de um draw de pelo menos 64 elementos. Apenas uma comparação por
draw pode sobrepor a montagem. Texturas pequenas, misses e demais
comparações seguem o caminho original. Nenhum ponteiro emprestado
sobrevive à comparação; o worker não altera o cache, não executa
comandos Xbox e não modifica a ordem dos draws.

A montagem lê dados do guest, não os modifica. O experimento mantém
a premissa existente de captura síncrona pelo produtor; não fornece
uma garantia de instantâneo atômico contra escritas concorrentes
por outras threads guest. Por isso é opcional e exige validação visual
e testes SDK reais. `PGR1_VK_TEXTURE_OVERLAP=0` é o padrão e fallback.
O tempo de montagem não é cobrado duas vezes em texture_check_ms.
O contador texture_overlap_checks mostra a cobertura efetiva.

O teste isolado C++20 verifica igualdade, mutações no início/meio/fim,
reutilização e conclusão em caso de exceção. O ganho de FPS não é
estabelecido; acordar um worker também tem custo. Compare o mesmo
trecho de Londres, com os mesmos parâmetros native/O2, em 0 e 1.

**Rastreamento de páginas continua pendente.** O SDK mapeia o mesmo
memfd em 0x80000000 e 0xF0000000, e possui escritas diretas por
HostPtr/G/memcpy e operações de I/O. DirtyEpoch sozinho não prova
imutabilidade. Nenhum atalho de invalidação foi habilitado.

## English

The native/O2 Sandy Bridge run averaged 28.26 FPS versus 26.56
in a different-length, scene-unmatched previous run. The final
300 frames requested 733,698 exact comparisons, 183.4 MiB/frame,
with zero byte changes and 14.069 ms/frame of texture checking.

Experimental `PGR1_VK_TEXTURE_OVERLAP=1` overlaps one exact
cached comparison (at least 256 KiB) per draw with SDK assembly
(at least 64 elements) on the existing capture thread, using a
persistent worker. All bytes are still checked, small checks and
misses remain inline, and borrowed pointers never outlive the
comparison. The assembly reads guest data; this retains the
synchronous producer assumption, not an atomic snapshot guarantee
against concurrent guest writers. The default remains off.
The optional CSV counter records coverage; assembly time is not
double-counted in texture_check_ms.

Standalone C++20 tests verify equality, changed bytes and exception
safety. Real SDK rendering and performance validation are pending.
Page tracking remains open: the two shared physical RAM aliases
and direct guest/host writes must all be covered before dropping
exact comparisons.

## Resultado no i7-2700K / Sandy Bridge result

PT-BR: 107.638 de 3.408.276 comparações (3,16%) foram sobrepostas.
FPS médio 26,90 contra 28,26 na sessão O2 anterior; P95 de captura
64,588 contra 47,359 ms e P95 de verificação 34,817 contra 20,862 ms.
As sessões não são pareadas, mas não demonstram benefício. Mantenha
PGR1_VK_TEXTURE_OVERLAP=0 (padrão); não amplie a quantidade de workers.

EN: 107,638 of 3,408,276 checks overlapped (3.16%). Mean FPS was
26.90 vs 28.26, capture P95 64.588 vs 47.359 ms. Different sessions
are not a controlled comparison, but no benefit is demonstrated.
Keep PGR1_VK_TEXTURE_OVERLAP=0 (default); do not expand the worker pool.
