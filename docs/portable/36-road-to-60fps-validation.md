# 04D.21 — Validation and scene correlation / Validação e correlação de cenas

## Português (Brasil)

**IMPLEMENTADO:** roteiro London/Westminster/Big Ben, benchmark com HDD isolado,
metadados de binário/script/seed, contadores de sincronização, captura por vblank,
contagem de ticks reais e relatório de todas as submissões. Resultados de hardware
e limitações da sessão estão no [handoff](37-performance-handoff.md).

O roteiro anterior selecionou San Francisco no seed desta sessão. Uma execução
interrompida no menu confirmou `London / Westminster / Big Ben` após inserir
`1350 UP 1` entre `1260 A 1` e `1440 A 1`. O novo roteiro
`scripts/pad/portable-london-easy.txt` depende de um perfil Player já existente
cuja cidade inicial seja San Francisco. **Outro save exige confirmação visual.**
A aceleração em linha reta constitui uma carga repetível, não um piloto capaz
de completar a corrida; colisões e resultados por timeout devem ser identificados.

### Compilação e testes

O SDK local contém mudanças; não executar submodule update nem reaplicar patches.
Em uma instalação limpa, aplicar somente o patch cumulativo conforme
[ordem de aplicação](../../portable/patches/README.md).

```bash
cmake --build build/linux --target xbe_recomp -j4
bash scripts/portable-dev.sh recompile
PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 JOBS=4 \
  PGR1_VK_BUILD_DIR=out/pgr1-vulkan-native-o2 \
  bash scripts/portable-vulkan.sh build-game
PGR1_VK_DEVICE='RX 580' ctest --test-dir out/pgr1-vulkan-native-o2 --output-on-failure
```

Para audit do diário, usar build separado com `PGR1_VK_SDK_WRITE_AUDIT=ON`.
`native` é local à CPU; os perfis auto/generic/v2/v3/v4 permanecem disponíveis,
respeitando as capacidades do processador. O i7-2700K não requer AVX2.

### Execuções isoladas

O runner não compila durante a medição, não sobrescreve diretórios existentes e
copia HDD/cache do seed. Registra SHA-256 do executável, script e arquivos do HDD.
A revisão Git de execução não comprova sozinha a origem de um binário antigo:
registre a procedência do artefato de referência separadamente.

```bash
bash scripts/benchmark-portable-london.sh \
  out/pgr1-vulkan-native-o2/pgr1_vk_game \
  .local/benchmarks/04d21-seed .local/benchmarks/my-london-exact exact 150

PGR1_VK_SYNC_PROFILE=1 bash scripts/benchmark-portable-london.sh \
  out/pgr1-vulkan-native-o2/pgr1_vk_game \
  .local/benchmarks/04d21-seed .local/benchmarks/my-london-profile exact 150
```

O seed local não é distribuído no Git. Crie-o com um HDD legítimo de testes e
cache aquecido. Mesmos hashes de seed/script e configurações reduzem variáveis;
não garantem igualdade da simulação ou do percurso.

Uma execução visual separada pode usar:

```bash
PGR1_VK_SCENE_VBLANKS=1400,2500,3000,3600,4200,4800 \
PGR1_VK_SYNC_PROFILE=1 bash scripts/benchmark-portable-london.sh \
  out/pgr1-vulkan-native-o2/pgr1_vk_game \
  .local/benchmarks/04d21-seed .local/benchmarks/my-london-scenes exact 150
```

As capturas drenam a GPU e afetam pacing: não misturar essa execução com o
benchmark sem capturas. `scene-markers.csv` liga o vblank solicitado ao vblank,
frame e tick de atualização realmente concluído. O jogo pode executar vários
ticks por frame para recuperar tempo; vblank, tick e frame não são equivalentes.
Os ticks são incrementados após o update original, sem alterar o escalonamento.
Nem ticks iguais comprovam sozinhos câmera/posição iguais; inspecionar as imagens.

### Sincronização e GPU

`PGR1_VK_SYNC_PROFILE=1` habilita relógios opcionais no produtor e os CSVs:

- `sync-submissions.csv`: todos os prefixos, com motivo explícito.
- `sync.csv`: somas até cada apresentação bem-sucedida; tentativas falhas seguem
  para o próximo intervalo concluído.
- `submissions.csv` e `frames.csv`: preservam as colunas anteriores e acrescentam
  vblank, motivo, vértices/índices/bytes próprios e tick da simulação.

São medidos reports/clears, Sync/SyncLate, flush, polls de Busy, waits de
resource/fence/readback, queries/pools, staging de readback, passes, pipelines
criados e `shader_ms` (espera pelo future SPIR-V e criação de pipeline;
não mede todo o tempo CPU dos workers de compilação).
`visibility_report_wait_ms` é o tempo do wait obrigatório atual, incluindo a
espera de entrega/execução; não representa somente a GPU. Eventos contam antes
do flush e tempos de espera concluídos depois seguem na próxima submissão.

`gpu.csv` conserva **cada** timestamp com o frame de origem, inclusive prefixos
sem apresentação. Somar linhas repetidas por frame é obrigatório. A coluna
histórica `gpu_completed_ms` descreve entrega de amostras naquele ponto do loop,
não duração exclusiva do frame corrente. O relatório dedicado soma pela origem,
verifica quantidade de spans e informa cobertura incompleta. Capturas preservam
novas amostras do jogo sem incluir timestamps de submissões diagnósticas frame 0.

```bash
python3 portable/tools/frame_report.py <run> --target-fps 60 --window 120
python3 portable/tools/texture_report.py <run> --window 300
python3 portable/tools/sync_report.py <run> --after-tick 2000 --before-tick 3000
python3 portable/tools/sync_report.py <run> --json
python3 portable/tools/compare_capture.py <baseline> <new> --target-fps 60
```

Os limites são exclusivos e independentes. O relatório de sincronização aceita
limites de frame, vblank ou tick, recusa dados truncados/negativos/não finitos e
não inventa zero quando um CSV/campo está ausente. Seus testes cobrem múltiplas
submissões, apresentações falhas, amostras tardias/ausentes e filtros de cena.
Tempos aninhados/assíncronos não são parcelas somáveis. O perfil `perf` permaneceu
indisponível com `perf_event_paranoid=4`, dentro e fora do sandbox.

## English

The verified USA 1.0 Player seed reaches London/Westminster/Big Ben by selecting
UP at guest vblank 1350 while the city initially selects San Francisco. Other
saves require visual verification. Straight acceleration is a reproducible
workload, not a completed-race driver. The isolated runner copies HDD/cache,
refuses existing destinations, records binary/script/seed SHA-256 and never
builds during measurement. Keep shader warm-up and graphics settings matched.
Execution-time Git revisions alone cannot establish an old binary's provenance.

Optional sync profiling records every submission reason and sums through actual
successful presents. Producer wait clocks default off. Existing CSV columns stay
in place; source vblank, owned geometry counts and completed simulation ticks are
appended. Tick counting does not alter scheduling, pacing, effects or quality.
Scene captures record requested/actual vblank, tick and frame, but drain the GPU:
use separate visual runs. Even equal ticks do not establish scene equivalence.

GPU samples are summed by source frame, including repeated nonpresent prefixes,
and diagnostic frame-0 samples are excluded. The historical completion-time
column is not a source-frame duration. The dedicated report verifies sample
counts and handles failed presents, delayed/missing spans, legacy data and
exclusive independent scene filters. Nested/asynchronous costs are not additive.
Exact/audit/tracked keep their original coverage rules. No unsafe texture skip,
fake visibility result, delayed mandatory report or disabled effect is used.
