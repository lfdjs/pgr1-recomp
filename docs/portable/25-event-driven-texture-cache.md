# 04D.19B/C — Guest memory tracker and audited texture capture / Tracker de memória guest e captura auditada

## Português (Brasil)

**Estado:** núcleo cooperativo implementado e testado; captura auditada
integrada; cobertura completa dos escritores do SDK **pendente**. No pin
`97b1654736f744a3966b5dff3d49be6d61ac7244`, toda textura guest é registrada
com cobertura `Unknown`. Portanto, o modo `tracked` continua experimental e
usa comparação exata para essas texturas. Não entrega ainda a eliminação
do `memcmp` no gameplay. Consulte o [inventário de escritores](24-writer-coverage.md).

### Identidade física e registro

`portable/include/pgr/memory/guest_memory_tracker.h` é independente de API
gráfica e de chamadas Linux. `PhysicalLayout` recebe os aliases, a extensão
física compartilhada e a página lógica. A integração usa as constantes
reais `mem::kContiguousBase`, `mem::kWriteCombineBase`,
`mem::kMappedPhysical` e `mem::kPage` do SDK, sem mascarar endereços guest
arbitrários. Somente um intervalo inteiramente contido em uma das duas
janelas é canonicalizado. Zero bytes, endereços desconhecidos, cruzamento
dos limites e overflow retornam ausência de identidade monitorável.

`RegisterRange(address, bytes, coverage)` retorna um handle monotônico.
O stamp também identifica o domínio da instância: dois trackers podem
atribuir o mesmo handle, geração e epoch sem aceitar o stamp um do outro.
O identificador de domínio é obtido uma vez por instância com CAS atômico
saturante, sem reutilização após overflow; se o espaço se esgotar, o
tracker deixa de autorizar registros/reutilização. O endereço do objeto
não é usado como identidade persistente.
Cobertura padrão é `Unknown`; passar `Cooperative` é um contrato explícito
do dono de **todos** os acessos aos bytes, não uma detecção automática do
tracker. Limites padrão: 4.096 regiões e 262.144 vínculos região/página.
Falhas de identidade ou capacidade retornam handle zero e exigem fallback.
As listas reversas por página são alocadas no primeiro registro. Nós
ativos obedecem ao orçamento e são liberados no unregister/reset; não há
capacidade de vetores de links acumulada após churn. Alocações de novos
links são preparadas antes da publicação do registro.

Gerações pertencem às regiões; páginas servem para encontrar rapidamente
registros candidatos às escritas. Uma escrita em outra faixa da mesma
página não invalida a região quando não existe sobreposição de bytes.
A consulta `ReadGuard::IsUnchanged(handle, stamp)` faz uma busca esperada
O(1) na tabela de registros, sem varrer páginas da textura por draw.
Uma escrita custa O(páginas tocadas + vínculos visitados nessas páginas).
Recursos sobrepostos podem receber a mesma geração, sem alterar esse
contrato. Não é uma solução de custo zero para escritores muito frequentes.

`UnregisterRange` remove links e invalida o handle. `Reset` remove registros
e altera o epoch; handles não voltam a identificar recursos anteriores.
`InvalidateRange` altera a geração e revoga cobertura antes de free,
remap, reuso de endereço ou mudança de autoridade GPU. Capturar um novo
stamp não restaura cobertura revogada: é necessário novo registro com
contrato válido. Uma invalidação parcialmente fora das janelas é recortada
para suas interseções físicas, usando aritmética sem overflow.

### Concorrência e snapshot

O domínio cooperativo usa `shared_mutex` por instância. `Write(address,
bytes)` adquire exclusão, invalida as regiões sobrepostas **antes** do
primeiro store e mantém a exclusão até o término, incluindo exceções.
`Read()` mantém lock compartilhado durante consulta, comparação, cópia e
publicação de snapshot/stamp. O consumidor recebe somente dados próprios
e imutáveis. Não se retêm ponteiros guest após liberar a guarda.

As guardas não são reentrantes. Uma thread que mantém `ReadGuard` ou
`WriteGuard` não pode chamar `RegisterRange`, `UnregisterRange`,
`InvalidateRange`, `Reset`, `GetStats` ou adquirir outra guarda no mesmo
tracker; essas operações tentariam adquirir novamente seu mutex.
Consultas/captura de stamp durante leitura usam os métodos da guarda já
existente. O tracker deve sobreviver a todas as suas guardas e operações;
nenhuma guarda pode escapar para além do lifetime da instância.

Não há seqlock em bytes não atômicos nem notificação isolada apresentada
como prova de sincronização. Os testes concorrentes fazem stores reais
por ambos os aliases sob a mesma guarda e cópias reais sob `ReadGuard`.
O lock não foi inserido em todas as instruções guest. A adoção real exige
redesenhar/instrumentar o conjunto de escritores e ponteiros externos
descrito na auditoria; apenas acrescentar algumas notificações HLE não
estabelece cobertura. Também não há proteção de páginas, signal handler
ou dependência de userfaultfd neste núcleo.

### Modos de captura

| Opção `PGR1_VK_TEXTURE_COHERENCY` | Decisão sobre os bytes |
| --- | --- |
| `exact` (padrão) | Mantém o caminho de comparação completa; não registra/consulta recursos no tracker. |
| `audit` | Prevê hit/invalidação, compara todos os bytes e usa o resultado exato para decodificar/renderizar. |
| `tracked` | Só omite comparação se `Prediction::CanReuse()` comprovar registro, domínio/geração/epoch iguais e cobertura cooperativa; caso contrário compara todos os bytes. |

Valores desconhecidos selecionam `exact`. A opção diagnóstica antiga
`PGR1_VK_TEXTURE_DIAGNOSTIC=skip-exact` continua separada e insegura;
é ignorada em `audit`/`tracked`. Para executar a referência exata, deixe
essa opção diagnóstica ausente.

Em `audit`, **falso negativo** significa previsão informativa de igualdade
apesar de bytes diferentes; **falso positivo** significa invalidação
prevista apesar de bytes iguais. Uma notificação conservadora ou store
que regrava o mesmo valor pode produzir falso positivo. Escritas guest que
não participam do domínio podem produzir falsos negativos informativos
em regiões `Unknown`; a imagem segue usando o resultado exato. Não se
deve interpretar taxa de divergência zero em uma corrida como prova de
cobertura. Regiões que não puderam ser registradas não geram uma previsão
válida e são contadas como descobertas.

O cache preserva o descritor completo, extensão real, mipmaps/cubemaps,
decodificadores e snapshots imutáveis existentes. Mudança de descritor
remove o registro anterior; eviction também remove seu handle. Em
audit/tracked, decodificação e publicação de stamp ficam dentro da guarda
de leitura.
No pin atual, `covered_queries` e `fast_hits` devem permanecer zero para
entradas guest, pois nenhuma é promovida a `Cooperative`.

### Autoridade CPU/GPU e cobertura parcial

Render targets e faces GPU continuam usando os caminhos próprios de
`gpu_written_`, cópias, `Sync` e `SyncLate`. Uma geração CPU estável não
autoriza reutilizar a versão CPU de um recurso cuja versão atual está
na GPU. Clear/draw/cópia GPU agrupam a invalidação das faixas afetadas
nas transições de autoridade: entrada em `gpu_written_` ou nova entrada
em `unread_`, incluindo o primeiro trabalho GPU após `Sync` limpar esse
estado. Os conjuntos e `gpu_epoch_` continuam atualizados em cada operação;
somente a notificação ao tracker foi agrupada. Draws GPU repetidos não
escrevem bytes CPU, portanto não exigem invalidação por draw enquanto a
autoridade GPU pendente permanece estabelecida. Esse agrupamento evita
percorrer os registros sobrepostos a cada draw.

Todo novo registro guest, inclusive sobreposto e criado durante autoridade
GPU já ativa, continua `Unknown`. Não se pode promover essas entradas a
`Cooperative` com esse agrupamento: a criação de registro ou mudança de
extensão de target durante autoridade ativa exigiria consultar/invalidar
explicitamente toda a autoridade GPU sobreposta. Os testes atuais não
autorizam tal promoção.

Os escritores conhecidos `WritePixels` (incluindo readback imediato ou
tardio) e `CopySurfaceMemory` na captura usam guardas de escrita, com
notificação conservadora da extensão destino. Isso torna essas mudanças
observáveis pelo audit, mas **não promove cobertura** de texturas guest.
O restante dos escritores do SDK não usa esse domínio; o fallback exato
é obrigatório em todas as texturas guest no pin atual. A nova guarda não
estabelece sincronização retroativa com esses escritores não cooperativos
nem amplia as garantias concorrentes do backend anterior.

### Telemetria

As colunas antigas dos CSVs são preservadas e novas colunas acrescentadas:
queries, fast hits, invalidations, exact fallbacks, uncovered ranges,
false positives/negatives, monitored writes, dirty pages, tracked bytes,
covered queries, tempo do tracker e tempo interno dos escritores.
`texture_report.py` aceita também os CSVs antigos.

`tracked_bytes` soma extensões registradas; sobreposições são contadas por
recurso e não representam RAM física única. `dirty_pages` conta visitas
a páginas com recursos efetivamente sobrepostos por evento de escrita,
não a quantidade de páginas distintas da sessão. Invalidações de
autoridade/mapeamento não são contadas como stores CPU. `write_ms` mede
a guarda, incluindo espera pelo lock, notificação e o trabalho do
escritor antes de liberar a guarda; o tempo externo completo pode ser
maior. Os tempos de tracker estão aninhados em custos de captura, não
devem ser somados indiscriminadamente ao frame.

### Testes executados

`portable/tests/memory/test_guest_memory_tracker.cpp` passou com C++20,
`-O2 -Wall -Wextra -Wpedantic -Werror -UNDEBUG -pthread`. Cobre:

- Ambos os aliases; limites, regiões desconhecidas, zero e overflow.
- Escritas reais de 1/2/4/8/10/16 bytes, inclusive atravessando páginas;
  operações em bloco, memmove, loops equivalentes a REP MOVS/STOS e
  `atomic_ref` em fixtures cooperativas. Isso testa o contrato do núcleo,
  **não instrumentação de opcodes gerados pelo SDK**.
- Sobreposições, faixas distintas na mesma página, capacidades,
  unregister, invalidação, reset, desalocação/reuso simulado e epochs.
- Rejeição de stamp de outra instância mesmo quando handle, geração e
  epoch locais coincidem.
- Escrita serial sem notificação em região desconhecida detectada pelo
  resultado exato como falso negativo; notificação sem mudança como
  falso positivo; fallback obrigatório em ambos os casos descobertos.
- Dois escritores concorrentes reais, snapshots consistentes e
  preservação dos valores de snapshots próprios após escritas posteriores.

Passou com AddressSanitizer+UBSan (`-O1 -g
-fsanitize=address,undefined -fno-omit-frame-pointer`) e com ThreadSanitizer
(`-fsanitize=thread`). ASan usa `ASAN_OPTIONS=detect_leaks=0` porque o
LeakSanitizer padrão falhou sob ptrace neste ambiente; vazamentos não
foram validados. Os três builds foram repetidos após a mudança das listas
reversas para nós com orçamento limitado.
Os três builds também foram repetidos após a inclusão do domínio único
saturante e do teste de stamp estrangeiro, todos com sucesso.

Reprodução independente de assets:

```bash
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -UNDEBUG -pthread \
  -Iportable/include -Iportable/linux/gldirect \
  portable/tests/memory/test_guest_memory_tracker.cpp \
  -o /tmp/pgr1-memory-test
/tmp/pgr1-memory-test

c++ -std=c++20 -O1 -g -fsanitize=thread -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror -UNDEBUG -pthread \
  -Iportable/include -Iportable/linux/gldirect \
  portable/tests/memory/test_guest_memory_tracker.cpp \
  -o /tmp/pgr1-memory-tsan
/tmp/pgr1-memory-tsan
```

O benchmark separado `portable/tests/memory/benchmark_guest_memory_tracker.cpp`
mede consultas, `memcmp` completo e stores com/sem guarda em fixtures.
É sintético: não inclui decodificação, todos os escritores reais, GPU ou
captura de corrida. Não comprova redução de 95% das comparações, texture
check inferior a 1 ms nem 30/60 FPS. Para aceitação de gameplay continuam
necessários cobertura dos escritores, configuração pareada e medição real.

Execução verificada no i7-2700K, GCC 15.2, sem AVX2, usando 1.024 regiões
de 256 KiB, 100.000 consultas e 20.000 stores por caso. Log local:
`.local/validation/04d19/tracker-benchmark.log`. As regiões sobrepostas
somam 256 MiB lógicos em uma janela física de 128 MiB.

| Operação sintética | ns/op |
| --- | ---: |
| `memcmp` completo, 256 KiB iguais | 15.838,956 |
| Consulta com aquisição/liberação de ReadGuard | 34,078 |
| Consulta sob uma única ReadGuard para todo o laço | 20,561 |
| Store de byte sem tracker, fixture separada | 10,034 |
| Store com WriteGuard monitorada | 93,129 |
| Store com WriteGuard fora dos recursos monitorados | 79,311 |

O tempo interno acumulado das guardas de escrita foi 1,962 ms, incluindo
os dois casos com guarda. A diferença do custo por store mostra por que
não basta transferir cegamente o trabalho para todas as instruções guest.
São necessárias medições da frequência de escritores reais e do custo
total; o pin atual não possui essa instrumentação completa.

```bash
c++ -std=c++20 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -pthread \
  portable/tests/memory/benchmark_guest_memory_tracker.cpp \
  -o /tmp/pgr1-tracker-benchmark
/tmp/pgr1-tracker-benchmark --iterations 100000 --write-iterations 20000
```

## English

**Status:** the cooperative core is implemented and tested, with audited
texture capture integrated. Complete SDK writer coverage remains pending.
Every guest texture on SDK pin `97b1654736f744a3966b5dff3d49be6d61ac7244`
registers as `Unknown`; experimental `tracked` therefore keeps full exact
fallback. It does not yet remove gameplay `memcmp` work. See the
[writer inventory](24-writer-coverage.md).

`GuestMemoryTracker` is graphics-API and Linux independent. The backend
passes real SDK physical alias/window/page constants. Canonicalization
requires a complete range inside one window and rejects unknown VAs,
zero length and overflow. It never masks arbitrary guest addresses.

Each registered range owns a monotonic handle, generation and coverage.
Stamps additionally identify their owning domain, rejecting foreign
instances even when handle/generation/epoch happen to match. A saturating
atomic CAS assigns each instance's identity once and never wraps; exhausted
domain space fails closed. Object addresses are not persistent identities.
Default budgets are 4,096 ranges and 262,144 reverse page links; failed
registration returns zero and requires fallback. Lazy page lists hold
only active nodes, released on unregister/reset. Allocations are staged
before publication. Exact range overlap prevents unrelated bytes on the
same page from invalidating a resource. Queries perform an expected O(1)
map lookup, without scanning every texture page per draw. Writes visit
touched pages and their candidate links. Reset changes the epoch;
unregister/free/reuse cannot resurrect stale handles. Invalidation changes
generation and revokes coverage, including clipped physical intersections
of partly unknown extents. A fresh stamp alone never restores coverage.

`Cooperative` is a caller contract covering **all** actual byte accesses,
aliases, external pointers, I/O and mapping changes. A per-domain shared
mutex excludes writers from readers. Write guards invalidate before the
first store and hold exclusion through completion, including exceptions.
Read guards cover queries, exact comparison, snapshot copying and stamp
publication. Consumers receive owned immutable data. No seqlock on
non-atomic bytes or isolated notification is presented as synchronization.
The SDK's uninstrumented writers do not satisfy this contract; no lock
was inserted globally into every guest instruction.

Guards are not reentrant. A thread holding either guard must not call
RegisterRange, UnregisterRange, InvalidateRange, Reset, GetStats or acquire
another guard on the same tracker, as these reacquire its mutex. Read-time
queries and stamp capture use the existing guard's methods. The tracker
must outlive every guard and in-flight operation.

`PGR1_VK_TEXTURE_COHERENCY=exact` is the default and keeps the original
full-byte path. `audit` records predictions while rendering the exact
result. Experimental `tracked` may skip bytes only if `CanReuse()` finds
a registered resource, equal domain/generation/epoch and cooperative coverage;
every other case uses exact fallback. Invalid values select exact.
The old unsafe `skip-exact` diagnostic remains separate and is ignored in
audit/tracked. Leave it unset for reference exact runs.

Advisory false negatives mean unchanged metadata despite changed bytes;
false positives mean invalidation despite equal bytes. Audit uses exact
bytes for rendering, including unknown regions with unreported writers.
Zero observed divergences do not prove writer coverage. Descriptor changes
and eviction unregister old resources; existing mipmap/cubemap decoding,
immutable snapshot ownership and GPU-authoritative paths are preserved.
Guest texture covered queries and fast hits remain zero on the current pin.

GPU clear/draw/copy destinations coalesce tracker invalidation at authority
transitions: insertion into `gpu_written_` or a new insertion into `unread_`,
including the first GPU work after `Sync` clears unread state. Both sets and
`gpu_epoch_` still update on every operation; only tracker notifications are
coalesced. Repeated GPU draws do not mutate CPU bytes, so they do not need
per-draw CPU-range invalidation while pending GPU authority remains established.
This avoids visiting overlapping registrations on every draw. Existing
render-target, `Sync`, `SyncLate` and GPU-written face handling remain
authoritative. Stable CPU generation is insufficient for GPU-owned data.
Every guest registration remains `Unknown`, including overlapping entries
created while GPU authority is already active. Such entries must not become
`Cooperative` under this policy: registration or target-extent changes during
active authority would need explicit checks/invalidation of all overlapping
GPU authority. Current tests do not authorize that promotion.
Known CPU copy/readback writers acquire write guards and report destination
extents conservatively, but never promote guest coverage. Guards do not
retroactively synchronize SDK writers outside the domain or expand the
old backend's concurrent byte-access guarantees.

CSV columns are appended compatibly; `texture_report.py` accepts older
files. Tracked bytes sum registered ranges, including overlap. Dirty-page
counts are visits per monitored write, not unique physical pages. Authority
invalidations are not counted as CPU stores. Internal writer timing includes
lock wait, invalidation and guarded work; wall cost can be higher. Nested
capture timings must not be added as independent frame components.

The asset-free memory test passed strict C++20 Release warnings, ASan/UBSan
and ThreadSanitizer. It covers aliases/boundaries/overflow, actual scalar
and block writes, 80-bit/128-bit extents, cross-page operations, guarded
REP-like loops/atomic operations, overlaps, limits, lifecycle, unknown
coverage, detected advisory false negatives/positives and consistent
snapshots with two real concurrent writers. These fixture operations do
not certify instrumentation of SDK-generated instructions. LeakSanitizer
could not run under ptrace and was disabled for the ASan run; leaks were
not validated. All three builds were rerun after the bounded-list revision.
They also passed after adding saturating domain identities and the
foreign-instance stamp regression test.
Commands above reproduce the tests independently of proprietary assets.

The separate memory benchmark measures synthetic metadata queries, full
equal-buffer scans and writer costs. It excludes real SDK writer coverage,
decode, GPU and gameplay capture. No 95% comparison reduction, sub-1 ms
texture check or 30/60 FPS acceptance is claimed by this stage.

The verified i7-2700K/GCC 15.2 run used 1,024 overlapping 256 KiB regions,
100,000 reads and 20,000 writes per case, with no AVX2. The table above
records full scan 15,838.956 ns/op; guarded query 34.078 ns/op; query under
one shared guard 20.561 ns/op; plain byte store 10.034 ns/op; monitored
guarded store 93.129 ns/op; unmonitored guarded store 79.311 ns/op.
Internal writer timing totaled 1.962 ms across the two guarded cases.
Raw local evidence is `.local/validation/04d19/tracker-benchmark.log`;
the command above reproduces it. The writer overhead must be evaluated
against real store frequency before selecting a production integration.
