# 04D.18A — Perfis de CPU x86-64 / x86-64 CPU profiles

## Português (Brasil)

Referência confirmada: Intel Core i7-2700K (Sandy Bridge), 4 núcleos,
8 threads, 3,50–3,90 GHz, 8 MiB de L3, 30 GiB de RAM detectados e RX 580.
O executável já é ELF x86-64. O i7 suporta AVX, SSE4.2 e POPCNT,
mas não AVX2/FMA/BMI; seu nível é x86-64-v2. Trata-se de uma referência
de desempenho desejado, não de um requisito mínimo validado.

A sessão 04D.17 registrou 26,56 FPS, captura CPU média de 21,838 ms,
P95 de 43,322 ms e GPU média de 3,575 ms (P99 de 8,628 ms).
O intervalo 242..1921 teve janelas próximas de 60 FPS; 2162..5041
teve janelas de aproximadamente 15–25 FPS. Sem marcação visual não
é possível garantir que esses intervalos correspondam a cenas iguais.

O script identifica capacidades pelo Linux, considerando a interseção
dos flags das CPUs visíveis e reportando a topologia disponível à
afinidade do processo. A escolha é independente do fabricante.

| Perfil | Flags | Uso |
|---|---|---|
| auto (padrão) | x86-64-v2 + mtune=native | Compatibilidade v2, ajuste local |
| generic / v2 | x86-64-v2 + mtune=generic | Binário portável Intel/AMD |
| v3 / v4 | x86-64-v3 / v4 | Apenas CPUs compatíveis |
| native | march=native + mtune=native | Binário local, não redistribuir |

O código recompilado mantém -O1 como padrão conservador; -O2 e -O3
são experiências explícitas. O runtime continua em Release. Nenhum
fast-math foi introduzido; o ABI e os endereços Xbox de 32 bits
permanecem intactos. O script reconfigura o CMake ao mudar perfil.
A assinatura da CPU força a recompilação de objetos localmente ajustados
ao trocar de CPU. Não garante segurança de executar diretamente binários
native copiados para outro computador; use o script e diretórios separados.

### Comparação no i7-2700K

Identifique a CPU:
~~~bash
bash scripts/portable-vulkan.sh cpu-info
~~~

Referência original (pasta atual):
~~~bash
PGR1_VK_CPU_PROFILE=v2 PGR1_VK_GUEST_OPT=1 JOBS=4 bash scripts/portable-vulkan.sh build-game
~~~

Experimento O2 nativo (pasta separada):
~~~bash
export PGR1_VK_BUILD_DIR=out/pgr1-vulkan-native-o2
export PGR1_VK_CPU_PROFILE=native
export PGR1_VK_GUEST_OPT=2
JOBS=4 bash scripts/portable-vulkan.sh build-game
bash scripts/portable-vulkan.sh test
PGR1_FPS=60 PGR1_VK_DEVICE=auto PGR1_VK_VSYNC=0 PGR1_VK_PRESENT_MODE=mailbox PGR1_VK_MSAA=0 PGR1_VK_FXAA=1 PGR1_VK_INTERNAL_PERCENT=100 bash scripts/portable-vulkan.sh run-game
~~~
Preserve as variáveis durante build e run. Compare os mesmos trechos de
Londres com frames.csv, gpu.csv, submissions.csv, vk_batches.csv e
console.log separados. O2/nativo não foi validado em jogo real ainda;
caso haja regressão, volte a v2/O1 na pasta de referência.

### Rastreamento de memória — 04D.18B ainda pendente

O SDK fixado mapeia RAM física compartilhada em dois endereços virtuais
(kernel/memory.cpp). W32/W16/W64 usam memcpy; HostPtr permite escritas
diretas e o SDK também usa memset/memcpy. DirtyEpoch cobre apenas
marcações explícitas. Portanto, uma única faixa protegida, uma
amostragem de hash ou invalidar só por frame não garante fidelidade.
A próxima parte precisa tratar aliases, remapeamentos e todas as
origens de escrita. A 04D.18A mantém memcmp exato.

## English

Confirmed reference: i7-2700K (Sandy Bridge), 4 physical cores/8 logical
threads, AVX but no AVX2, RX 580, 30 GiB visible RAM. x86-64-v2 is
the existing baseline, not a proven 60 FPS minimum. 04D.17 logged
26.56 FPS, CPU capture P95 43.322 ms and GPU P99 8.628 ms.

The script detects Linux-visible ISA capabilities, using the intersection
of CPU flags and reporting affinity-visible topology. Auto uses the
portable v2 ISA with native tuning; generic/v2 uses generic tuning.
v3/v4 require the corresponding capabilities; native is a machine-local
build. A CPU signature invalidates local objects when changing CPUs
through the script. Directly copying and executing a native binary on
another host is not supported. CMake enforces a 64-bit x86 host build.

Generated guest code retains -O1 by default, with explicit -O2/-O3
experiments. Release runtime, guest ABI and exact texture comparisons
are preserved. Use separate build directories and identical race
sections for validation; see the commands above. No O2 gameplay or FPS
gain has yet been validated.

04D.18B remains open: the pinned SDK has aliased physical RAM mappings
and direct memcpy/HostPtr writes. DirtyEpoch alone is incomplete.
A safe tracking mechanism must account for aliases, mappings and all
writers before it can replace full byte validation.
