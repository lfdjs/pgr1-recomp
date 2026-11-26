# 04D.21 — Performance Overhaul II: handoff / Transferência de contexto

## Português (Brasil)

Data local da sessão: 25/11/2026 (America/Sao_Paulo).

**Resultado:** melhorias concretas de runtime, geometria e Vulkan; coerência de
memória guest ainda incompleta. Não considerar atingidas as metas de 30/60 FPS
estáveis nem de corrida completa. Os números finais e o trecho solar são
registrados abaixo após a validação em hardware.

### Estado inicial e reprodutibilidade

- Projeto `portable`, HEAD inicial `0eb9099545b1597c5f8688cbbcfe63dbdff14e5a`.
- Gitlink público SDK `97b1654736f744a3966b5dff3d49be6d61ac7244` preservado.
- SDK em `pgr1-memory-audit`, HEAD inicial no pin, com sete arquivos modificados
  e `physical_write_audit.h`/teste ainda sem commit. Essa base do usuário foi
  preservada, integrada e incluída no patch cumulativo.
- SDK final local: `068dc1008f58d8a5f2aabbd3ee926affe1d17f3d`,
  `perf(runtime): optimize bulk writes and guest vertex assembly`.
- Nenhum push e nenhuma alteração no upstream de terceiros. O SDK local ter
  HEAD diferente do gitlink é intencional; não fazer submodule update por cima.
- Remotes SDK anteriores preservados: origin hk21x, upstream TGP482.
- Assets, código do jogo gerado, HDDs, shaders derivados, imagens e logs locais
  não foram adicionados ao Git.

`portable/patches/sdk-97b1654-performance-overhaul.patch` é **cumulativo**.
A aplicação foi conferida em archive limpo do pin: `apply --check`, aplicação,
comparação byte a byte de todos os 11 arquivos e reverse check no SDK real.
Ele substitui o patch diagnóstico histórico quando se começa do pin limpo;
não se aplicam os dois em sequência. Ver [instruções](../../portable/patches/README.md).

A cópia `.local/validation/04d21/baseline-0eb9099-audit-build` preserva o executável
instrumentado existente antes das alterações, com diário desativado na execução
exact. A cópia `baseline-game` preserva um artefato native anterior, usado somente
na exploração dos menus e de San Francisco. Não confundir esses dois baselines.
O runner guarda SHA-256 dos artefatos e seed; a revisão Git no momento da execução
não comprova sozinha como um executável antigo foi compilado.

### Implementado e testado

| Frente | Mudança | Limite de aceitação |
| --- | --- | --- |
| Runtime CPU | REP MOVS reverso via memmove quando equivalente; STOS repetido via memset; sobreposição por elementos/aliases preservada | Microbenchmark não é ganho de FPS |
| Geração | SIMD desalinhado via memcpy; CVTPS2PI/CVTTPS2PI m64 lê oito bytes | Regeneração do jogo obrigatória |
| Diário | REP agrupado; rejeição barata de endereços não físicos; eventos de alloc/free/reuse | Posterior ao store, cobertura Unknown |
| Geometria | Evita sequência densa temporária e índices expandidos em listas/strips nativos | Sem cache de geometria mutável |
| Queries | Pool persistente, crescimento e reset após fence; resultados precisos | GetReport do produtor ainda síncrono |
| Readback | Staging reutilizável com oito slots/64 MiB e overflow temporário | Sem encurtar arbitrariamente a região lida |
| Lifetime | Drenagem/cancelamento de queries/readbacks/completion pendentes | Nenhum resultado falso ou atraso imposto |
| Shaders | Cache por stage/fonte exata/diretório, single-flight e retry | Compilação/flags/precisão preservadas |
| Medição | Motivos de prefixos, perfil opcional, ticks reais, spans GPU por origem | Tempos aninhados não são somáveis |

Documentos: [memória/runtime](31-memory-coherency-production.md),
[queries/readback](32-london-sun-visibility-optimization.md),
[geometria](33-geometry-capture-v2.md), [stages](35-vulkan-pipeline-optimization.md),
[comandos e cenas](36-road-to-60fps-validation.md).

O recompilador foi reconstruído; `recompile` executado novamente antes do build
final. A geração incremental preserva timestamps de chunks cujo conteúdo não
mudou, mas os chunks SIMD afetados foram atualizados. Perfis de CPU existentes
preservados, sem requisito incondicional de AVX2. Nenhuma mudança no clock,
limite de ticks, simulação, qualidade, sol, reflexos, áudio ou efeitos para medir
um FPS artificial. O novo contador de ticks observa updates já concluídos.

### Coerência definitiva: pendente

Exact é a referência, audit continua comparando bytes e tracked mantém fallback
exato para todas as regiões guest Unknown. Não há promoção baseada apenas em
zero divergências do diário. O tracker cooperativo e o diário do SDK continuam
contratos distintos; o diário não exclui leitores e não cobre todos os raw
pointers/HLE/I/O/threads/hooks. Eventos de lifecycle não revogam automaticamente
registros de outro domínio. Fast hits seguros guest e redução de memcmp por
cobertura permanecem **zero**. Reduzir comparações exige um contrato abrangente
de acessos, incluindo registro concorrente, aliases e ponteiros externos.

Estratégias descartadas nesta sessão: habilitar skip-exact em produção; promover
Unknown pelo histórico de uma corrida; atrasar obrigatoriamente um report por
um frame; retornar visibilidade fabricada; reutilizar geometria guest sem prova;
aumentar frames em voo sem resolver dependências. Não foram introduzidos esses
atalhos. A sincronização C++ de todos os escritores continua pendente; o fallback
anterior também não estabelece retroativamente exclusão sobre escritores raw.

### Londres e o sol

O roteiro anterior seleciona San Francisco no HDD de referência. A inspeção dos
menus confirmou London / Westminster / Big Ben após `1350 UP 1`. O novo roteiro
registra esse requisito do seed e não presume que outro save tem a mesma seleção.
Aceleração em linha reta pode terminar por timeout; isso **não** valida corrida
completa, domínio do percurso nem a pior região relatada pelo usuário.

No código, GetReport cria uma submissão e espera o resultado no produtor.
A interface SDK permite publicação posterior; o D3D12 original possui fila de
reports e o XDK pode consultar status pendente. Há serialização confirmada no
backend portátil, mas sua contribuição para a queda solar deve ser medida.
Mudar para publicação assíncrona exigiria resolver lifetime, escrita de report
e leitura guest concorrente; não foi implementado um callback com ponteiro guest
sem contrato. O novo perfil mede as consultas/stalls reais antes de decidir isso.

Referência real de 150 s: 5.104 apresentações, zero draws rejeitadas, média
34,03 FPS, P95 52,534 ms, P99 66,357 ms, 1% low 10,01 FPS. Inclui menus,
carregamento, gameplay e tela de resultado por timeout. Capture média 20,674 ms,
texture check 8,461 ms, assembly 2,767 ms, GPU span 2,266 ms (P99 4,120 ms).
Esses números indicam carga CPU relevante, mas não confirmam causalidade do sol.

### Validação final e resultados

Resultados finais: preencher com os relatórios locais após o ciclo de hardware.

Evidências locais em `.local/validation/04d21/`,
`.local/validation/04d21-geometry/` e `.local/benchmarks/04d21-*`.
Sanitizers de REP/aliases, geração real, lifecycle e geometria passaram com
ASan+UBSan; `detect_leaks=0`, portanto ausência de vazamentos não foi validada.
Os componentes de memória cooperativa existentes foram testados anteriormente
com TSan; distinguir isso de uma nova execução nesta sessão.
`perf stat` negado também fora do sandbox por `perf_event_paranoid=4`; nenhum
flamegraph/sampling foi obtido e a política do kernel não foi alterada.

Os microbenchmarks medem operações específicas. Geometria: 500 strips de 8.192
vértices, 131,104→108,379 ms sem índices e 133,633→119,050 ms com índices;
24.570→0 / 8.192 índices por draw. REP: 256×1 MiB, STOSD frente
129,339→15,735 ms, reverso 128,946→16,171 ms; MOVSD reverso
131,666→21,904 ms. Não extrapolar essas razões para gameplay.

### Reprodução e próximas etapas necessárias

1. Preservar o SDK local ou aplicar o patch cumulativo em uma cópia limpa.
2. Reconstruir xbe_recomp, regenerar e compilar; executar CTest completo.
3. Usar HDD de testes validado, mesmos gráficos e cache; executar runner sem builds
   concorrentes. Capturas ficam numa execução separada.
4. Selecionar trechos pelas imagens e ticks, com limites independentes; analisar
   sync_report, frame_report, texture_report e compare_capture.
5. Resolver cobertura/sincronização dos acessos guest para fast hits; medir custo
   total dos escritores. Priorizar o caminho crítico CPU remanescente comprovado.
6. Validar percurso completo, pior região solar, áudio e RTX 3060 Ti; ARM64,
   Android, Windows e demais destinos ainda não foram executados nesta sessão.

Comandos exatos e explicações dos CSVs estão no [documento 36](36-road-to-60fps-validation.md).
Para o seed local já preparado nesta sessão:

```bash
PGR1_VK_DEVICE='RX 580' PGR1_VK_SYNC_PROFILE=1 \
  bash scripts/benchmark-portable-london.sh \
  out/pgr1-vulkan-native-o2/pgr1_vk_game .local/benchmarks/04d21-seed \
  .local/benchmarks/my-next-london exact 150
python3 portable/tools/sync_report.py .local/benchmarks/my-next-london --json
```

## English

The initial PGR1 checkout was portable at 0eb9099, with the SDK pinned at
97b1654 and the user's uncommitted diagnostic writer patch already present.
All original changes were preserved and committed together with tested SDK
optimizations as 068dc1008f58d8a5f2aabbd3ee926affe1d17f3d. The parent public
gitlink stays unchanged; no remote publication or upstream modification occurred.
A cumulative patch was applied to a clean pinned archive and all 11 SDK files
matched exactly. Do not stack it on the old audit patch or overwrite local work.

Implemented changes remove redundant REP work, fix unaligned SIMD/m64 accesses,
preserve native converted topologies, reuse fence-safe query/readback resources,
propagate cancellation and coalesce exact shader stages before ABI/disk work.
Optional synchronization evidence records every submission and sums GPU samples
by source frame. Actual simulation ticks support scene correlation without
changing simulation or scheduling. Effects, visibility values and quality remain.

Writer coverage remains Unknown. Exact/audit reference bytes and tracked fallback
are preserved; proven guest fast hits and coherency-driven comparison reduction
remain zero. Post-store page events are not reader exclusion or a complete writer
contract. Mutable guest geometry was not cached without such proof.

The verified seed selects London/Westminster/Big Ben; previous scripted runs
selected San Francisco. Straight acceleration is a workload, not a complete race.
Source-level synchronous GetReport serialization is confirmed, but its causal
contribution to the reported solar slowdown requires scene-correlated evidence.
The 150-second baseline mixes menus, loading, gameplay and timeout results:
34.03 FPS mean, 52.534/66.357 ms P95/P99, 10.01 FPS 1% low, no rejected draws.
Final hardware outcomes and limitations are recorded in the Portuguese table.

Synthetic speedups do not establish gameplay FPS. ASan/UBSan used disabled leak
detection; perf sampling stayed blocked by kernel policy. No locked-30/60 or
completed-race acceptance is claimed. Remaining work is a complete synchronized
writer contract, source-scene CPU profiling, full London/sun/audio acceptance and
validation on the RTX 3060 Ti and other supported platforms.
