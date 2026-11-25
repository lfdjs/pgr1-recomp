# Architectural performance overhaul — Handoff / Transferência de contexto

## Português (Brasil)

### Estado inicial e resultado

Checkout inicial limpo, branch `portable`, projeto em
`bed1adf9b668668476ab946e1fedc132c14da154`; SDK em
`97b1654736f744a3966b5dff3d49be6d61ac7244`. Não havia commits posteriores
nem modificações preexistentes a preservar. O gitlink permanece nesse pin;
os remotes SDK são hk21x (origin) e TGP482 (upstream), sem fork dedicado do
usuário configurado. Nenhum código SDK, asset ou source gerado foi editado
manualmente ou incluído nos commits. A build executou a recompilação incremental
já existente, com o mesmo SDK/configuração.

Antes: cada bind estático validava um snapshot owned por comparação completa;
o diagnóstico `skip-exact` media um teto inseguro. `DirtyEpoch` e identidade
do descritor não cobriam stores diretos. A montagem reutilizava scratch SDK,
mas a conversão flat ainda construía vetores intermediários.

Agora: há um núcleo portátil de coerência, registros de regiões, gerações,
identidade física canônica, domínio/epoch/lifetime, listas reversas limitadas,
guardas cooperativas de acesso real aos bytes, modos audit/tracked com fallback,
telemetria e testes. A conversão flat gera diretamente um único vetor owned.
**Isso não estabelece cobertura dos escritores guest e não elimina ainda as
comparações em gameplay.** A investigação confirmou bypasses no gerador, REP,
x87/SIMD, HLE/I/O, hooks, buffers externos e threads reais; não foi preparado
um patch SDK parcial que pudesse aparentar integração completa.

### Estado das etapas

| Etapa | Implementado/testado | Pendência de aceitação |
| --- | --- | --- |
| 04D.19B | Núcleo portátil; aliases reais; limites/overflow; guardas sincronizadas; overlap/lifecycle; fixtures concorrentes e sanitizers. | Instrumentar e demonstrar cobertura de todos os escritores SDK/PGR1, ownership de ponteiros/I/O e lifecycle real. |
| 04D.19C | Exact padrão; audit integrado; política tracked experimental; fallback obrigatório; telemetria/relatórios; backend real testado com aliases/readback. | Fast path guest seguro permanece indisponível: todos os registros são Unknown; metas >95% menos comparações e <1 ms não atingidas. |
| 04D.20 | Conversão flat direta com menos cópias/alocações, preservando ordem/winding/provoking vertex; testes e microbenchmark. | Perfil CPU por samples, impacto em captura/draw real, cache de geometria com cobertura comprovada. |
| 04D.21 | Consolidação de testes, documentação e workflow de comparação/medição. | O pedido não especifica uma implementação própria para 04D.21; não atribuir a ela otimizações Vulkan adicionais nem aceitação de 30/60 FPS. |

### Arquitetura e decisões

O núcleo reside em `portable/include/pgr/memory/guest_memory_tracker.h`, sem
API gráfica/Linux obrigatórios. A integração obtém as constantes reais do SDK:
duas views compartilhadas de 128 MiB, páginas de 4 KiB. VAs user/system/stack
não são tratadas como físicas por máscara. Registros inválidos/sem capacidade
retornam handle zero. IDs de domínio e handles monotônicos, epoch de reset,
gerações e revogação de cobertura impedem ressuscitar stamps antigos ou de
outra instância. Overflow de identidades fecha o caminho de reutilização.

A consulta usa busca esperada O(1) por recurso, sem percorrer páginas por
bind. Escritas visitam páginas/links candidatos e filtram sobreposição exata.
Limites: 4.096 registros, 262.144 links ativos. Listas alocadas de forma lazy
liberam seus nós no unregister/reset; alocações precedem publicação do registro.

`Cooperative` só é legítimo se **todos** os acessos aos bytes, aliases,
ponteiros externos e lifecycle compartilham o domínio. A guarda exclusiva
começa antes do store, invalida e termina após a escrita. A guarda de leitura
protege comparação/cópia/publicação do snapshot owned. Não se usa seqlock em
bytes não atômicos. Guardas não são reentrantes nem podem sobreviver ao tracker.
Não se inseriu lock global em toda instrução guest.

O backend registra guest apenas como `Unknown`. `audit` faz previsão
informativa mas renderiza pelo memcmp; `tracked` só poderia pular comparação
com `CanReuse()` comprovado e, portanto, usa fallback integral no pin atual.
Escritas sem notificação podem aparecer como falsos negativos **informativos**,
inclusive nos testes deliberados; nunca autorizam pixels obsoletos. Zero
divergências em um trecho não comprova cobertura.

Readback/CopySurfaceMemory conhecidos participam de guardas, sem promover
cobertura. A autoridade GPU existente permanece separada: sampling de targets,
cubemaps, Copy, Sync/SyncLate e futures seguem seus caminhos anteriores.
Clear/draw/copy revogam prova CPU quando a região entra em autoridade GPU e
quando recebe novas escritas após readback. Repetir invalidação para todos os
draws de um target já GPU-owned foi removido após medir seu custo no audit.
Isso não altera os comandos nem a ordem dos draws; novos registros guest
continuam Unknown mesmo se criados após a transição de autoridade.

O modo exact não registra recursos nem calcula extensões para o tracker.
Audit/tracked ignoram o diagnóstico unsafe caso as opções sejam combinadas.
Desativação imediata: remova o diagnóstico e use
`PGR1_VK_TEXTURE_COHERENCY=exact`; a sobreposição permanece uma opção separada.

### Arquivos

Criados:

- `portable/include/pgr/memory/guest_memory_tracker.h`.
- `portable/linux/gldirect/texture_coherency.h`.
- `portable/linux/gldirect/flat_primitive_indices.h`.
- `portable/linux/gldirect/tests/test_flat_primitive_indices.cpp`.
- `portable/tests/memory/test_guest_memory_tracker.cpp`.
- `portable/tests/memory/benchmark_guest_memory_tracker.cpp`.
- `portable/tools/compare_capture.py`, `portable/tests/compare_capture_tests.py`.
- Documentos 24–28 em `docs/portable/`.

Modificados:

- `portable/CMakeLists.txt`: núcleo no build host sem assets/SDK.
- `portable/linux/gldirect/backend.cpp`: modos, registros, readbacks/cópias,
  transições GPU e conversão de índices.
- `portable/linux/gldirect/commands.h`, `portable/linux/vulkan/main.cpp`:
  métricas append-only e agregação soma/pico.
- `portable/linux/gldirect/tests/backend_test.cpp`: aliases físicos reais,
  mutações sem notificação, readback sobreposto e snapshots imutáveis.
- `portable/linux/vulkan/CMakeLists.txt`: testes e modos.
- `portable/tools/texture_report.py`, `portable/tests/texture_report_tests.py`:
  schema opcional compatível com CSVs antigos, cobertura e custos.
- `docs/portable/README.md`: índice atualizado.
- `scripts/portable-vulkan.sh`: ajuda das novas opções de coerência.

### Testes e evidências

Hardware efetivamente acessado: Intel Core i7-2700K, AMD Radeon RX 580 2048SP
(RADV POLARIS10), Mesa 26.0.8-1ubuntu0.3, Vulkan 1.4.335 no device; compilador
GCC 15.2.0. Perf instalado, mas a política `perf_event_paranoid=4` negou
`perf stat -e task-clock -- /bin/true` também fora do sandbox. Nenhum sample
profile/flamegraph foi obtido; a política do kernel não foi alterada.

- `python3 portable/tools/probe_guest_aliases.py`: PASS nos três checks:
  outro alias modifica bytes protegidos, cruzamento de página e readv EFAULT.
- Build Release real: `PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 JOBS=4
  bash scripts/portable-vulkan.sh build-game`: PASS após corrigir include
  faltante do novo alvo de teste. Build final incremental também PASS.
- Suíte Vulkan final: 29/29 PASS na RX 580, incluindo pixels offscreen assíncronos
  e síncronos, áudio mixer, captura real SDK exact/audit/tracked e novos testes.
  Execução inicial no sandbox falhou por acesso Vulkan; os dois testes de
  audit também expuseram uma expectativa antiga do diagnóstico unsafe, que
  foi isolada no modo exact. Falhas foram corrigidas e a suíte repetida.
- Debug host independente: 3/3 PASS, incluindo o novo contrato de memória.
- Backend OpenGL legado: compilado e `sdk_backend_capture` PASS.
- Memória: strict O2 `-Wall -Wextra -Wpedantic -Werror`, ASan+UBSan e TSan PASS,
  incluindo stores concorrentes reais e foreign stamps. Fixtures de tamanhos
  x87/REP/atômicas não certificam instrumentação dos opcodes SDK.
- Geometria: 2.580 combinações + goldens, strict O2 e ASan/UBSan PASS.
- Relatórios Python: 11 testes de texturas e 10 de comparação PASS;
  comparação de CSV consigo mesmo gerou deltas zero em 3.567 intervalos.
- LeakSanitizer padrão falhou sob ptrace; ASan foi executado com
  `detect_leaks=0`. Vazamentos não foram validados.
- `git diff --check`: PASS.

Logs e artefatos locais, ignorados pelo Git:
`.local/validation/04d19/` e `.local/validation/04d20-flat-indices/`.
Os diretórios `.local/benchmarks/04d19-*-smoke-rx580` guardam CSVs, logs,
HDDs isolados e capturas dos smokes de 100 segundos. São mistura de menus,
loading e corrida, usando `portable-quick-race.txt` e um mesmo seed de HDD.
Não são aceitação pareada de Londres. As capturas finais mostram o MINI em
corrida, com HUD/reflexos/iluminação presentes; inspeção de poucos frames não
é uma validação gráfica exaustiva, de áudio ou de corrida completa.

O primeiro audit mediu cerca de 9,49 ms/frame no tracker nos últimos 300
frames, sem fast hits: notificações repetidas por draw tinham custo alto.
Essa observação motivou agrupar revogações por transição de autoridade GPU.
Não comparar o FPS desses smokes como A/B: cenas finais e quantidade de
draws diferiram. O exact inicial também antecedeu essa limpeza de metadados.
Relatórios completos permanecem nos artefatos locais.

O audit final, com notificações agrupadas, apresentou 3.580 frames e
3.554.844 draws suportadas em 100 segundos, zero rejeitadas. Registrou
4.007.735 consultas, **18 falsos negativos informativos**, um falso positivo,
zero consultas cobertas e zero fast hits. Fez 4.006.982 comparações completas.
O custo instrumentado do tracker foi 0,242 ms/frame na sessão inteira;
guardas dos escritores conhecidos acumularam 140,138 ms (incluem payload).
Nos últimos 300 frames, o leitor mediu 0,289 ms/frame; a sessão chegou à tela
de resultados após timeout, portanto esse trecho não é só gameplay.

| Smoke de 100 s | Frames | Draws suportadas | Rejeitadas | Falsos negativos informativos |
| --- | ---: | ---: | ---: | ---: |
| Exact inicial | 3.568 | 3.638.092 | 0 | Não consulta tracker |
| Audit antes de agrupar | 3.180 | 2.938.717 | 0 | 18 |
| Tracked com fallback, antes de agrupar | 3.217 | 2.992.665 | 0 | 18 |
| Audit final agrupado | 3.580 | 3.554.844 | 0 | 18 |

Esses contadores comprovam execução e gaps observados; não demonstram ganho
A/B de desempenho. Todas as execuções audit/tracked tiveram zero cobertura e
fast hits. Os 18 misses reais reforçam que a aceitação de cobertura/zero
falsos negativos ainda não foi atingida. Capturas finais dos três primeiros
smokes foram inspecionadas em corrida; a última mostrou a tela de resultados.
Resumo reproduzível: `.local/validation/04d19/smoke-summary.json`.

Microbenchmarks sintéticos, não gameplay: 256 KiB iguais por memcmp ~15,839 µs,
consulta com guarda ~34 ns, escrita monitorada ~93 ns, na revisão de listas
antes da comparação adicional de ID de domínio. Conversão flat: 4.000×8.192
entradas levou 52–395 ms antes e 14–62 ms depois. Ambos excluem o conjunto
real de escritores/cenas; não extrapolar esses números para FPS.

### Limitações, próximos passos e reprodução

Não há SDK modificado nem gitlink novo. Para integrar writers: criar fork/branch
dedicado do usuário ou patch reproduzível contra o pin; instrumentar também
stack/RMW/SIMD/REP e HLE/raw pointers/I/O/threads/hooks; delimitar ownership
até conclusão real. Rever nesting/callbacks e lifecycle antes de atribuir
`Cooperative` a qualquer faixa guest. O inventário não é prova de cobertura
exaustiva. Mprotect de um alias é insuficiente; userfaultfd não foi validado.

O fallback exato preserva as garantias anteriores para escritas ordenadas,
mas não corrige retroativamente data races de escritores não cooperativos.
Tracked não deve ser anunciado como otimização de produção neste pin. Metas
de redução de memcmp, <1 ms de texture check e 30/60 FPS estáveis permanecem
pendentes. Windows, ARM64/Android/Switch/Vita e RTX 3060 Ti não foram validados.

Próximo trabalho de CPU: obter perfil permitido de corrida, medir frequência
de flat shading e custos por draw, priorizar conversão/estado/cópias que
apareçam no perfil. Não introduzir cache de geometria mutável até provar
coerência; não aumentar workers nem reordenar draws sem evidência.

Consulte [workflow e comandos completos](27-performance-validation.md),
[arquitetura/coerência](25-event-driven-texture-cache.md),
[cobertura dos escritores e referências consultadas](24-writer-coverage.md)
e [geometria](26-geometry-capture-v2.md). Referências externas efetivamente
abertas e limites da consulta constam no inventário; nenhum código desses
projetos foi copiado, e suas licenças não foram usadas para autorizar cópia.

Commits locais:

- `322bb3e` — `feat(memory): introduce cooperative guest memory coherency tracker`.
- `80d2aa6` — `feat(capture): integrate audited texture coherency and direct flat indices`.
- O commit `docs(portable): document coherency coverage and validation handoff`
  consolida os documentos 25–28 e o índice. Seu hash pode ser consultado com
  `git log --oneline -- docs/portable/28-handoff.md`.

Nenhum push foi solicitado ou realizado.

## English

The initial clean checkout was `portable` at
`bed1adf9b668668476ab946e1fedc132c14da154`, with SDK pin
`97b1654736f744a3966b5dff3d49be6d61ac7244`. The pin and SDK sources remain
unchanged; no dedicated user SDK fork was configured. Proprietary assets and
generated sources were not manually edited or committed. The existing
incremental build/recompile workflow was used.

Implemented: a graphics-independent portable memory domain with checked real
physical aliases, bounded reverse page lists, resource generations, domain
identity/epochs and synchronized cooperative byte access; audited texture
capture, experimental tracked policy with full exact fallback, compatible
telemetry/reports and tests. Direct flat index conversion removes temporary
arrays while retaining owned asynchronous submissions and primitive semantics.

Not implemented: complete SDK/guest writer coverage, production texture fast
hits, geometry caching or further Vulkan submission changes. Every guest
registration remains Unknown. Advisory audit misses are detected by exact
bytes and never authorize stale pixels. Known CPU copies/readbacks participate
without claiming global coverage. GPU authority remains separate; revocation
is grouped on authority/unread transitions instead of repeated for every draw.
Default exact performs no resource registration or extent calculation for
tracking. Unsafe diagnostics are ignored in audit/tracked.

The stage table and file inventory above distinguish implemented, tested and
pending acceptance. 04D.21 has no separate implementation specification in the
request and is recorded as validation/documentation consolidation only.
Writer inventory/reference research is in document 24; core/locking/coverage
contracts in 25; geometry semantics and synthetic timings in 26; full update,
build, run and matched London acceptance commands in 27.

Validation used the actual i7-2700K/RX 580 with RADV Mesa 26.0.8. Release game
build, 29 Vulkan CTests including real GPU pixels, three host Debug tests,
legacy OpenGL backend capture, strict memory/geometry tests and memory
ASan/UBSan/TSan passed. Initial missing include and old unsafe-test expectation
were fixed; restricted Vulkan tests were rerun outside the sandbox. LSan could
not run under ptrace, and perf sampling remained blocked by kernel policy.
Local logs/captures are retained under `.local/validation/04d19` and the
separate smoke benchmark directories. Smoke scenes mix menus/loading/racing,
are not matched London acceptance, and must not be compared as controlled A/B.
The first audit exposed high repeated-invalidation cost and motivated batching.
Final grouped audit ran 3,580 presented frames and 3,554,844 supported draws,
with zero rejected draws. Its 4,007,735 tracker queries found 18 advisory misses
and one false positive; guest coverage/fast hits remained zero and 4,006,982
exact comparisons were retained. Instrumented reader cost averaged 0.242 ms
per logged frame; known writer guards accumulated 140.138 ms including payload.
The session ended on a timeout results screen; this is not pure gameplay or
matched A/B. Observed writer gaps mean coverage acceptance remains unmet.
Synthetic microbenchmarks are operation-specific and establish no FPS gain.

Pending: a dedicated SDK fork/patch covering every generated/HLE/raw/I/O/thread
writer and lifecycle, demonstrable ownership, real CPU profiling, matched London
and NVIDIA acceptance, exhaustive visual/audio/full-race checks, and other
platforms. Existing exact fallback does not retroactively synchronize raw
writers outside the domain. No >95% comparison reduction, sub-1 ms texture
check or stable 30/60 FPS claim is made. No third-party upstream was changed
and no push was performed. Implementation commits are `322bb3e` and `80d2aa6`;
the documentation commit is identified by the title/path above.
