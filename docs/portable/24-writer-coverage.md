# 04D.19B — Writer coverage audit / Auditoria de escritores

## Scope / Escopo

EN: This inventory was checked against PGR1 `bed1adf9b668668476ab946e1fedc132c14da154`
and SDK `97b1654736f744a3966b5dff3d49be6d61ac7244`. It identifies concrete
writer families; it does **not** establish complete instrumentation. Line
references below refer to that SDK baseline and the initial PGR1 tree. No SDK
source, generated game source, proprietary asset or submodule pin was changed
by this audit. SDK remotes are `origin=hk21x/XboxXBERecomp` and
`upstream=TGP482/XboxXBERecomp`; no user-controlled SDK fork is configured.

PT-BR: Inventário conferido contra as revisões acima. Identifica famílias reais
de escritores; **não** demonstra instrumentação completa. As linhas do SDK se
referem ao pin e as do PGR1 à árvore inicial. Esta auditoria não modificou SDK,
código gerado, assets proprietários ou gitlink. Não há fork do SDK controlado
pelo usuário configurado nos remotes locais; não publicar mudanças nos remotes
de terceiros.

## Canonical identity / Identidade canônica

| SDK evidence / Evidência | Consequence / Consequência |
| --- | --- |
| `sdk/runtime/kernel/memory.h:6-17` | Logical page 4096 bytes; console RAM 64 MiB plus 64 MiB host physical buffers; mapped physical size **128 MiB**. / Página lógica 4096 bytes; RAM console e buffers host totalizam **128 MiB**. |
| `sdk/runtime/kernel/memory.cpp:29-33,62-67` | Shared memfd at `[0x80000000,0x88000000)` and `[0xF0000000,0xF8000000)`. / Mesmo memfd nas duas janelas. |
| `sdk/runtime/kernel/memory.cpp:487-495` | Physical offsets below 128 MiB also resolve to physical views; unknown addresses must not be canonicalized with a mask. / Offsets físicos abaixo de 128 MiB resolvem para a view; não aplicar máscara a endereços desconhecidos. |
| `sdk/runtime/kernel/memory.cpp:81-115` | User/system/stack virtual commitments are distinct from physical aliases. / Commit virtual de user/system/stack é distinto dos aliases físicos. |
| `sdk/runtime/kernel/memory.cpp:389-455` | Allocation/free/reuse can change ownership independently of bytes; physical decommit is a no-op and host free zeros bytes. / Alocação/free/reuso muda propriedade; decommit físico não desmapeia e free host zera bytes. |

EN: Normalize the **entire** half-open interval using checked 64-bit arithmetic.
Reject zero/invalid extents, wraparound, intervals crossing the end of a view,
unknown virtual regions and offsets outside the physical backing. Physical
offsets are valid only when the caller explicitly supplies a physical address;
low guest virtual addresses are not automatically physical. An allocation ID
or coverage epoch must change on release, reuse and remapping even if bytes
happen to compare equal. Reset must retire every old registration/token.

PT-BR: Normalizar todo intervalo semiaberto com aritmética de 64 bits verificada.
Rejeitar extensão inválida/zero, overflow, travessia do fim de view e regiões
desconhecidas. Endereço físico baixo só é físico quando o chamador declara essa
semântica; endereço virtual baixo não deve ser convertido implicitamente.
Desalocação/reuso/remapeamento exigem nova identidade ou epoch de cobertura,
mesmo com bytes iguais. Reset invalida todos os registros/tokens anteriores.

## Writer inventory / Inventário de escritores

EN: Every row remains a coverage gap in the pinned SDK. Instrumenting only
`CodeGen::Write`, `W32` or `MarkDirty` leaves bypasses. A grep match alone is
not proof of a write: the table records inspected destinations and call paths.

PT-BR: Todas as linhas representam lacunas de cobertura no SDK fixado.
Instrumentar apenas `CodeGen::Write`, `W32` ou `MarkDirty` deixa caminhos de
desvio. A tabela registra destinos e chamadas inspecionados, não apenas matches.

| Family / Família | Concrete evidence / Evidência concreta | Required boundary / Fronteira necessária |
| --- | --- | --- |
| Generated scalar stores / Stores escalares | `sdk/tools/recomp/codegen.cpp:104-127`; `sdk/runtime/cpu/x86_ops.h:16-22` | Typed memory assignment; 1/2/4/8-byte stores, unaligned spans. / Atribuição tipada e intervalos desalinhados. |
| Implicit stack and string stores / Stores implícitos | `sdk/runtime/cpu/x86_ops.h:60-65,229-265`; `sdk/tools/recomp/codegen_int.cpp:421,513-535` | PUSH/call helpers, REP/non-REP MOVS/STOS, direction flag and overlap. / PUSH/call, REP/MOVS/STOS, direção e sobreposição. |
| Locked and RMW operations / Operações atômicas e RMW | `sdk/tools/recomp/codegen_int.cpp:68-75,123,318-345,374-379` | Builtins bypass `Write`; CMPXCHG and adjusted bit-test addresses need exact destinations. / Builtins desviam de `Write`; considerar CMPXCHG e endereço ajustado de bit-test. |
| x87/MMX/SSE | `sdk/tools/recomp/codegen_fp.cpp:104-106,121-123,205-226,290-299,369,443-446`; `sdk/runtime/cpu/x86_ops.h:146-163` | Direct 2/4/8/16-byte stores, 10-byte x87 pair and environment stores. / Stores diretos, par x87 de 10 bytes e ambiente. |
| HLE pointer/reference escapes / Ponteiros e referências HLE | `sdk/runtime/kernel/kernel.h:55-80` | `G<T>` returns mutable reference; `HostPtr` raw pointer escapes; cannot infer write duration at lookup. / Referência e ponteiro mutáveis não delimitam duração da escrita. |
| File and disc I/O / Arquivos e disco | `sdk/runtime/kernel/files.cpp:576,605-635,672,755,995`; `sdk/runtime/kernel/boot.cpp:53,107-114,142` | fread/disc Read write guest buffers, scatter pages and metadata; bracket external call through completion. / fread/Read, scatter e metadados exigem escopo até conclusão. |
| RTL and Interlocked | `sdk/runtime/kernel/rtl.cpp:31-37,55,100-112,288-326` | memcpy/memmove/memset equivalents, strings and direct atomic builtins; list operations touch multiple addresses. / Blocos, strings, atômicas e listas com vários destinos. |
| Allocation and image sections / Alocação e seções | `sdk/runtime/kernel/memory.cpp:81-115,389-455`; `sdk/runtime/kernel/xboxkrnl.cpp:448-450` | Track zeroing plus release/reuse/commit/reset; section load can occur after boot. / Rastrear zeragem e ciclo de vida; seção pode carregar após boot. |
| Kernel objects and callbacks / Objetos e callbacks kernel | `sdk/runtime/kernel/objects.cpp:63-64,139,334-343`; `sdk/runtime/kernel/xboxkrnl.cpp:138-179,492-585,767-808` | Object counters, DPC arguments, atomics and timer/event state; callbacks can re-enter guest execution. / Contadores, DPC, atômicas e reentrada guest. |
| Crypto through raw buffers / Criptografia | `sdk/runtime/kernel/xboxkrnl.cpp:1359-1387`; `sdk/runtime/kernel/crypto.cpp:53-92` | SHA/RC4/HMAC mutate state/output passed as raw pointer. / Estado e saída recebidos por ponteiro. |
| Network and kernel writes / Rede e escritas kernel | `sdk/runtime/xdk/xnet.cpp:257-275,600-601,611-612,736,788-795` | recv/getsockopt write directly to guest pointer; overlapped completion metadata can be deferred. / recv/getsockopt diretos e conclusão overlapped posterior. |
| Audio / Áudio | `sdk/runtime/xdk/dsound.cpp:307-308,532,603,644-647,970-973,1158-1159` | Completion/status output, host audio thread, locked guest buffer addresses retained in voice/packet state. / Conclusão/status, thread host e endereços retidos. |
| Input, TLS and fibers / Entrada, TLS e fibers | `sdk/runtime/xdk/xapi.cpp:118-124,141-147`; `sdk/runtime/xdk/fibers.cpp:131-142` | Scalar/block state output and switching TLS/exception state. / Saída de estado e troca de TLS/exceções. |
| D3D state/resource HLE | `sdk/runtime/xdk/d3d8.cpp:197,276-281,503-524,555-561,1173` | Resource headers/state arrays and copies, beyond texture Lock/Unlock. / Headers, arrays e cópias além de Lock/Unlock. |
| GPU copies/readbacks / Cópias e readbacks | `sdk/runtime/gpu/texture.cpp:885-929`; `portable/linux/gldirect/backend.cpp:127-145,646-679` | CopySurfaceMemory destinations, WritePixels linear/swizzled and delayed SyncLate. / Destinos de cópia, pixels e SyncLate posterior. |
| SDK capture/replay buffers / Captura e replay SDK | `sdk/runtime/gpu/render_thread.cpp:335`; `sdk/runtime/gpu/interp.cpp:256,542,556-558,592` | Host contiguous scratch and restored headers are guest physical writes too. / Scratch host físico e headers restaurados também escrevem guest. |
| PGR1 hooks / Hooks PGR1 | `src/game/hooks.cpp:66,82,326-335`; `src/game/settings_entry.cpp:64,557,645-695`; `src/game/frame_rate.cpp:83-88,142` | Matrix/camera/HUD, menu strings/entries, frame state and guest stack. / Matriz/câmera/HUD, menus, estado de frame e pilha. |

## Synchronization and coverage contract / Contrato de sincronização e cobertura

EN: Page generations are **metadata**, not a C++ memory-access synchronization
primitive. Reading ordinary guest bytes while a thread writes them remains a
data race even if an atomic sequence later changes and the read is discarded.
`StartThread` and `StartHostThread` (`sdk/runtime/kernel/threads.cpp:172-221`)
spawn host execution concurrently; audio and network add independent writers.
The existing exact comparison is a correctness reference for ordered writes,
not evidence that unsynchronized cross-thread writes are already safe.

PT-BR: Gerações são **metadados**, não sincronização de acesso C++ aos bytes.
Ler bytes comuns concorrentemente com escrita continua data race mesmo se a
sequência atômica mudar e a leitura for descartada. Threads guest/host, áudio e
rede são concorrentes. O caminho exact é referência para escritas ordenadas;
não prova segurança de escritas concorrentes sem sincronização.

EN: A future covered domain must bind all aliases, all writers and all mapping
changes to the same ownership protocol. Writer acquisition precedes any byte
mutation; release publishes completed mutation and invalidation. Snapshot
acquisition excludes byte writers for the complete copy/compare; submission
owns immutable host data. Multiple-page acquisition needs fixed order. Long
I/O must either hold an ownership scope through completion or write into a
temporary host buffer and publish a bounded copy under that scope. Retained
mutable pointers revoke coverage until their owner returns/quiesces. Callback
reentrancy and atomic instructions need explicit nesting/lock order rules.
Unknown ownership, GPU authority, unsupported range or failed registration
must use the existing exact path without issuing a coherence proof.

PT-BR: Domínio coberto futuro deve usar o mesmo protocolo de propriedade em
aliases, escritores e remapeamentos. Adquirir escrita antes de alterar bytes;
liberar publicando conclusão/invalidação. Snapshot exclui escritores por toda
cópia/comparação e submissão possui dados host imutáveis. Travar páginas em
ordem fixa. I/O longo mantém escopo até conclusão ou escreve em buffer host
temporário e publica cópia limitada sob escopo. Ponteiro mutável retido revoga
cobertura até retorno/quiescência. Reentrada e atômicas exigem regras explícitas
de nesting/ordem de locks. Propriedade desconhecida, autoridade GPU, intervalo
não suportado ou registro falho exigem fallback sem emitir prova de coerência.

EN: Selective portable instrumentation is the preferred direction, with a
small no-watch check before acquiring region ownership. Do not introduce a
global lock for every generated instruction. Merely patching a header cannot
cover the current lvalue macros and raw pointers. A complete SDK change needs
a dedicated branch/patch at the existing pin, regenerated output in the build
directory, writer fixtures, coverage review and end-to-end audit. This session
must not promote a partial writer set into a production tracked fast path.

PT-BR: Preferir instrumentação portátil seletiva e check curto de região sem
watch antes de adquirir propriedade. Não travar globalmente toda instrução.
Patch só em header não cobre lvalues e ponteiros existentes. Integração SDK
completa exige branch/patch dedicado no pin, regeneração fora do repositório,
fixtures, revisão de cobertura e audit integrado. Cobertura parcial não
autoriza fast path tracked de produção.

## GPU authority / Autoridade GPU

EN: `DirtyEpoch` (`sdk/runtime/gpu/texture.cpp:445-456`) is keyed by resource
data and explicit `MarkDirty`; it does not observe arbitrary physical stores.
The backend separately tracks `gpu_written_`, `unread_`, `gpu_epoch_`, targets
and late readback futures. CPU generation equality cannot replace that state.
Preserve target sampling/copy/Sync/SyncLate ordering. A readback invalidation
must cover physical bytes and overlapping resources, but must not claim that
all guest writers now participate in ownership. Cube faces, mips and compressed
blocks are part of the descriptor's complete byte extent, not only level zero.

PT-BR: DirtyEpoch depende de MarkDirty explícito e endereço de recurso, sem
observar stores físicos arbitrários. A autoridade GPU inclui targets, unread,
epoch e futures de readback; geração CPU não substitui isso. Preservar ordem
de amostragem/cópia/Sync/SyncLate. Readback invalida bytes e recursos sobrepostos
sem alegar cobertura dos demais escritores. Faces, mips e blocos comprimidos
integram a extensão completa, além do nível zero.

## Protection strategies and actual probe / Proteção e probe executado

EN: `python3 portable/tools/probe_guest_aliases.py` passed in this session:
writing alias B changes alias A protected read-only, including a page boundary;
readv into A returns EFAULT; restored mappings remain shared. mprotect of one
view cannot provide complete tracking. Protecting both views still needs a
kernel-I/O strategy and fault/rearm synchronization. No userfaultfd support,
permission, shmem-WP alias behavior, fault cost or kernel-write behavior was
established. No signal handler or mandatory Linux-only core was introduced.

PT-BR: Probe executado nesta sessão passou: alias B altera A read-only inclusive
na fronteira; readv em A retorna EFAULT; restauração mantém compartilhamento.
mprotect de uma view não cobre aliases; ambas ainda exigem estratégia para I/O
kernel e faults/rearm. Não foi demonstrado suporte/permissão de userfaultfd,
WP de shmem/aliases, custo de fault ou escritas kernel. Nenhum signal handler
ou núcleo obrigatório específico de Linux foi introduzido.

## External references actually consulted / Referências externas consultadas

EN/PT-BR: These are architectural observations from primary sources retrieved
during this session, not copied implementations. / Observações arquiteturais
de fontes primárias acessadas nesta sessão, sem copiar implementações.

- [Xemu Vulkan texture cache](https://raw.githubusercontent.com/xemu-project/xemu/master/hw/xbox/nv2a/pgraph/vk/texture.c): page dirty checks feed overlap invalidation; texture/surface authority is separate. Its content hash is **not** adopted as exact equality. / Dirty de páginas alimenta invalidação; autoridade de superfície é separada; hash não adotado como prova exata. Source header LGPL-2.0-or-later.
- [Xemu GL surface](https://raw.githubusercontent.com/xemu-project/xemu/master/hw/xbox/nv2a/pgraph/gl/surface.c): CPU access can require waiting for downloads; download marks memory dirty for clients. / Acesso CPU pode esperar download, que notifica dirty aos clientes. Header LGPL-2.0-or-later.
- [Xenia SharedMemory](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/shared_memory.h) and [TextureCache](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/texture_cache.h): range watches, global critical region, separate GPU-written state and base/mip invalidation. / Watches por intervalo, região crítica e estado GPU/base/mips separados. Headers state BSD.
- [RPCS3 texture cache](https://raw.githubusercontent.com/RPCS3/rpcs3/master/rpcs3/Emu/RSX/Common/texture_cache.h): overlapping locked sections, flush/unprotect/exclusion sets and invalidation causes require explicit handling. / Seções sobrepostas e causas de invalidação exigem conjuntos explícitos. No code copied; project license compatibility was not used to authorize reuse. / Sem cópia e sem presumir compatibilidade para reutilização.
- [Dolphin TextureCacheBase](https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/VideoCommon/TextureCacheBase.h) and [VertexManagerBase](https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/VideoCommon/VertexManagerBase.h): cache entries keep overlap relationships/content locks; vertex management has separate streaming state. / Relações de sobreposição e content locks; geometria tem estado próprio. Headers GPL-2.0-or-later; no code copied.
- [DXVK buffer declarations](https://raw.githubusercontent.com/doitsujin/dxvk/master/src/dxvk/dxvk_buffer.h): resource views carry invalidation-aware descriptor identity. / Views relacionam identidade de descritores e invalidação. The requested staging header failed to fetch and was not analyzed. / Header staging não acessível, não analisado.
- [QEMU memory API](https://www.qemu.org/docs/master/devel/memory.html): aliases model alternate views; external writes require explicit dirty marking; mapping ownership has a lifetime contract. / Aliases modelam views; escritas externas notificam dirty e mapeamentos têm lifetime.
- [Khronos descriptor management](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html): descriptor caching and buffer packing can reduce CPU work while retaining frame-in-flight copies. / Cache de descritores e organização de buffers podem reduzir CPU mantendo cópias em voo.
- [Khronos command buffer usage](https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html): pools need per-thread ownership and recycling; excessive small secondary buffers can cost performance. / Pools exigem propriedade por thread e reciclagem; muitos buffers pequenos podem custar desempenho.

EN: This audit did not inspect Xemu `blit.c`, the Xenia article, the other
recompilation repositories or complete implementations beyond the files listed.
Remote master sources are research references, not pinned dependencies.

PT-BR: Não foram inspecionados Xemu `blit.c`, artigo Xenia, demais repositórios
de recompilação ou implementações completas além dos arquivos listados.
Fontes master remotas são referências, não dependências fixadas.
