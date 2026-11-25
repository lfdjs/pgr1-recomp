# 04D.19–21 — Validation workflow / Fluxo de validação

## Português (Brasil)

O SDK continua fixado em `97b1654736f744a3966b5dff3d49be6d61ac7244`.
`exact` é a referência inicial. `audit` renderiza o resultado exato e mede
previsões informativas. `tracked` existe para testar a política, mas no pin
atual usa **fallback exato integral**: cobertura e fast hits guest devem ser
zero. Nenhuma variável transforma cobertura desconhecida em autorização.
`PGR1_VK_TEXTURE_DIAGNOSTIC=skip-exact` continua inseguro e separado; remova-a
das medições de correção. Audit/tracked ignoram essa opção mesmo se presente.

### Atualização, compilação e testes

Os commits desta sessão podem estar somente no checkout local; `git pull`
não os publica. Execute a atualização quando estiverem disponíveis no remoto
ou depois de transferir os commits. Não sobrescreva mudanças locais.

```bash
git switch portable
git pull --ff-only origin portable
git submodule sync --recursive
git submodule update --init --recursive
git rev-parse HEAD
git -C sdk rev-parse HEAD
python3 portable/tools/probe_guest_aliases.py

PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 JOBS=4 \
  bash scripts/portable-vulkan.sh build-game
PGR1_VK_DEVICE='RX 580' bash scripts/portable-vulkan.sh test
```

`native` produz binário local à CPU de build. Para distribuir, preserve os
perfis `auto/generic/v2` existentes; não use `v3/v4` no Sandy Bridge.
`build-game` usa a recompilação incremental existente, sem editar manualmente
sources gerados, assets ou o SDK.

O núcleo também pode ser testado em Debug sem Vulkan/SDL/assets/SDK:

```bash
cmake -S . -B out/portable-host -DPGR1_PORTABLE_HOST_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build out/portable-host --parallel 4
ctest --test-dir out/portable-host --output-on-failure
```

Os comandos strict/sanitizers estão no [documento do núcleo](25-event-driven-texture-cache.md).
LSan não pôde executar sob ptrace; isso não é evidência de ausência de leaks.

### Corridas comparáveis no computador do usuário

Use o mesmo perfil/salvamento inicial, pista de **Londres**, MINI Cooper S
(ou outro carro escolhido, igual nas três execuções), mesma câmera e percurso,
640×480, resolução interna 100%, MSAA desligado, FXAA ligado e limite de 60.
Desligue a sobreposição experimental de texturas para isolar os modos.
Selecione a pista manualmente ou utilize um script de Londres previamente
verificado. O script existente `portable-quick-race.txt` foi documentado como
San Francisco e depende da seleção salva: **não estabelece a cena de Londres**.

Antes das execuções, copie um mesmo HDD de testes para diretórios novos.
Os exemplos não sobrescrevem um diretório existente. Ajuste `seed_run` para
um perfil próprio adequado e pareie a cena visualmente.

```bash
seed_run=.local/run/vulkan-race
for mode in exact audit tracked; do
  run=".local/benchmarks/04d19-${mode}-rx580"
  mkdir -p "$run"
  if [ ! -d "$run/hdd" ]; then
    cp -a --reflink=auto "$seed_run/hdd" "$run/hdd"
  fi
done

for mode in exact audit tracked; do
  run=".local/benchmarks/04d19-${mode}-rx580"
  env -u PGR1_VK_TEXTURE_DIAGNOSTIC -u SCRIPT \
    PGR1_VK_TEXTURE_COHERENCY="$mode" PGR1_VK_TEXTURE_OVERLAP=0 \
    PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 \
    PGR1_VK_DEVICE='RX 580' PGR1_VK_RUN_DIR="$run" \
    PGR1_CONFIG_FILE="$PWD/$run/linux.conf" \
    OUTPUT=640x480 PGR1_FULLSCREEN=0 PGR1_FPS=60 \
    PGR1_VK_INTERNAL_PERCENT=100 PGR1_VK_MSAA=0 PGR1_VK_FXAA=1 \
    PGR1_CAPTURE=final.ppm PGR1_CAPTURE_FRAME=3000 \
    PGR1_CAPTURE_FRAME_PATH=frame3000.ppm \
    bash scripts/portable-vulkan.sh run-game
done
```

Feche cada execução após o mesmo percurso. Uma captura no frame 3000 não
garante o mesmo instante de simulação/cena. Se usar SCRIPT, forneça o mesmo
arquivo absoluto e `PGR1_SCRIPT_ONLY=1` nas três execuções, confirme a cena e
registre o hash do arquivo. Use caches aquecidos de forma equivalente; menus,
carregamentos e compilação inicial de shaders não devem compor uma comparação
de gameplay sem identificação explícita.

### Relatórios e inspeção visual

```bash
for mode in exact audit tracked; do
  run=".local/benchmarks/04d19-${mode}-rx580"
  python3 portable/tools/frame_report.py "$run" --target-fps 60 --window 120
  python3 portable/tools/texture_report.py "$run" --window 300
done
```

Abra os PPMs correspondentes, confira pista/carro/câmera e examine HUD,
reflexos/cubemaps, iluminação, transparência, texturas dinâmicas e bordas.
Para distinguir o overlay de FPS em diferenças de imagem, inspecione também
a região do jogo fora do overlay. Não espere igualdade byte a byte entre
instantes diferentes de uma corrida com outras threads.

Registre os limites exclusivos `--after-frame` / `--before-frame` para cada
execução depois de identificar o trecho equivalente. Ambos os relatórios
existentes aceitam esses limites. Não trate as últimas 300 linhas ou números
de frame iguais como prova de cena pareada.

`compare_capture.py` acrescenta P50/P95/P99, 1% low, custos ponderados por
draw, contadores e deltas direita menos esquerda. Os limites de cada lado são
independentes; a ferramenta sempre avisa que não estabelece equivalência de
cena. O exemplo abaixo compara deliberadamente trechos distintos e só
demonstra a sintaxe; substitua os limites por trechos pareados visualmente:

```bash
python3 portable/tools/compare_capture.py \
  .local/benchmarks/04d19-exact-rx580 .local/benchmarks/04d19-audit-rx580 \
  --left-after 2400 --left-before 3000 --right-after 2600 --right-before 3200 \
  --target-fps 60 --scene-note 'Exemplo de sintaxe; equivalência ainda não validada'

python3 portable/tools/compare_capture.py \
  .local/benchmarks/04d19-exact-rx580 .local/benchmarks/04d19-audit-rx580 \
  --target-fps 60 --json > .local/benchmarks/04d19-comparison.json
```

A saída JSON conserva os caminhos, nota não verificada e estatísticas.
Guarde as informações de hardware/build/configuração junto desse arquivo.
CSVs inválidos/truncados ou métricas negativas/não finitas são rejeitados.

Colete apresentações bem-sucedidas, média/P95/P99/1% low, frames acima do
orçamento, capture/check/assembly e custo por draw, draws e binds/frame,
quantidade/extensão pedida ao memcmp, mudanças de bytes, decodes/evictions,
cobertura, divergências informativas, fast hits, custos leitor/escritor,
queue wait, GPU span e draws rejeitadas. `cpu_ms` mede CPU do processo;
tempos aninhados e assíncronos não devem ser somados como componentes exclusivos.
`write_ms` inclui espera e payload dos escritores conhecidos, não apenas
overhead líquido nem todos os escritores do SDK.

Grave a revisão, CPU/GPU/driver, configuração e hash do script junto à sessão:

```bash
git rev-parse HEAD
git submodule status
python3 portable/tools/cpu_info.py --profile native --guest-opt 2
lscpu
vulkaninfo --summary
```

Compare também na RTX 3060 Ti, com diretórios próprios. Não aumente workers
ou altere simulação/pacing para compensar o custo do audit. Sem cobertura
real, `tracked` não deve reduzir comparações; isso é comportamento esperado.

### Perfil e microbenchmarks

O `perf stat -e task-clock -- /bin/true` falhou dentro e fora do sandbox:
`perf_event_paranoid=4`. Nenhum flamegraph ou perfil de samples foi obtido.
Não foi alterada a política do kernel. Quando o ambiente permitir, execute
`perf record` separadamente dos benchmarks de tempo, usando o executável
real e o diretório isolado da corrida:

```bash
perf record -F 199 -g --call-graph dwarf -- \
  /caminho/absoluto/out/pgr1-vulkan-wsi/pgr1_vk_game /caminho/absoluto/assets
perf report --stdio

c++ -std=c++20 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -pthread \
  portable/tests/memory/benchmark_guest_memory_tracker.cpp -o /tmp/pgr1-tracker-benchmark
/tmp/pgr1-tracker-benchmark --iterations 100000 --write-iterations 20000

c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -UNDEBUG \
  -Iportable/linux/gldirect portable/linux/gldirect/tests/test_flat_primitive_indices.cpp \
  -o /tmp/pgr1-flat-indices
/tmp/pgr1-flat-indices --benchmark
```

Esses microbenchmarks não incluem escritores guest reais, GPU, decodificação
nem frequência de draws flat. Não justificam por si só expectativa de FPS.

## English

The SDK pin is unchanged. Default `exact` remains the correctness reference;
`audit` renders exact bytes and reports advisory predictions. Experimental
`tracked` keeps complete exact fallback for guest resources on this pin:
guest coverage and fast hits must remain zero. Unsafe `skip-exact` is separate
and is ignored in audit/tracked. No option claims unknown memory as covered.

The commands above update `portable` without rewriting history, restore the
pinned submodules, build with the existing CPU profiles and run tests. Local
session commits are not automatically published by `git pull`. A native binary
belongs to its build CPU; preserve portable profiles for distribution. The
host-only Debug build tests the memory core without graphics, assets or SDK.
Strict/sanitizer commands and their LSan limitation are documented separately.

For acceptance, use the same initial test save, London track, car, camera,
route, 640×480 output, 100% internal resolution, MSAA off, FXAA on, 60 FPS cap
and build profile. The existing Quick Race script is documented as San
Francisco and depends on saved selections; it does not establish London.
Copy the same HDD seed into new run directories and confirm the scene visually.
Keep script/hash and shader warm-up conditions identical. Matching frame IDs
or taking the last N rows does not establish equivalent scenes.

Use the frame and texture reports above with independently selected exclusive
frame bounds. Inspect corresponding PPM captures for HUD, reflections/cubes,
lighting, transparency and dynamic textures. Exclude the FPS overlay when
assessing game-region differences. Preserve all old CSV columns and use only
successful presentations for pacing metrics. Collect draw/resource workload,
scan/decode/cache costs, reader/writer instrumentation, advisory errors,
effective coverage, queue wait, GPU timestamps and rejected draws. Nested or
asynchronous timings are not additive exclusive frame components. Writer time
includes lock wait and guarded payload from known writers, not exhaustive SDK
write overhead. Record commit, SDK, hardware/driver, configuration and script.
`compare_capture.py` adds P50/P95/P99, 1% low, weighted costs per draw, workload
counts, right-minus-left deltas and JSON. Independent left/right frame bounds
never establish scene equivalence; the CLI always discloses that limitation.
The example bounds demonstrate syntax only and must be replaced after visual
scene selection. Malformed/non-finite/negative metrics are rejected.

Perf was installed but denied by kernel policy (`perf_event_paranoid=4`), both
inside and outside the sandbox; no sample profile/flamegraph was obtained and
no kernel policy was changed. The commands show how to profile in a permitted
environment separately from time benchmarks. Synthetic memory/index tools
measure specific CPU operations only and establish no gameplay FPS gain.
RTX 3060 Ti and matched London acceptance remain separate required validation.
