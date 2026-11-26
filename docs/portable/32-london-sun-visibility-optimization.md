# 04D.21 — Visibilidade e readback / Visibility and readback

## Português (Brasil)

**IMPLEMENTADO:** pool persistente de consultas de oclusão, staging de readback
reutilizável e limitado, conclusão assíncrona de submissões apenas com consultas,
telemetria e propagação de falhas. **PENDENTE:** atribuir causalmente a queda com
o sol a uma operação específica; nenhuma dessas mudanças comprova 60 FPS.
Os resultados finais da sessão estão no [relatório de validação](36-road-to-60fps-validation.md).

O contrato do SDK em `runtime/gpu/backend.h` descreve GetReport como operação
ordenada que publica tempo/contagem/status após os draws. D3D12 enfileira um
report com a fence correspondente e o entrega posteriormente. O backend de
captura Vulkan chama Flush e aguarda `completed.get()` e os futures de consultas
durante GetReport. Isso serializa a produção de comandos com a GPU mesmo quando
a API aceita publicação posterior. É um fato do código; sua contribuição para
o trecho solar depende das contagens e esperas medidas naquela cena.

Essa publicação permanece síncrona nesta revisão. Mudar apenas para um atraso
fixo ou retornar contagem fictícia seria incorreto. Uma implementação futura
deve publicar a estrutura de 16 bytes por uma fronteira segura entre produtor,
consumidor e polling guest, preservar status pendente, ClearReport, cancelamento,
ordem e lifetime da memória. A contagem real usa consultas precisas com depth,
stencil e scissor, preservando a normalização por MSAA existente.

Antes, Stage criava um VkQueryPool em cada submissão contada e ReleaseFrame o
destruía. Agora a capacidade inicia em pelo menos 16 consultas, cresce
geometricamente quando necessário e só é substituída depois da fence anterior.
`vkCmdResetQueryPool` reinicia apenas o intervalo necessário fora de render pass;
os resultados são lidos depois da fence. O renderer conserva uma única
submissão em voo, portanto não precisa de ring adicional de pools. O vetor
host dos resultados também conserva sua capacidade.

Submit deixava a CPU esperando toda submissão com draw contado, antes mesmo de
GetReport consumir o resultado. No perfil assíncrono essa espera antecipada foi
removida. PollCompletion, a próxima submissão ou Finish observa a fence real e
conclui os futures; GetReport continua aguardando a conclusão ordenada. O perfil
`PGR1_VK_PIPELINED=0` conserva a conclusão síncrona e readbacks CPU continuam
esperando seus bytes. Nenhum resultado parcial ou falso é publicado.

O staging de readback mantém até oito slots mapeados coerentes e no máximo
64 MiB de capacidade solicitada retida (alocações do driver podem ter padding).
Leituras da mesma submissão têm slots diferentes. O reuso e o crescimento só
acontecem após a fence anterior e a conversão para vetores de pixels próprios.
Pedidos além do limite usam buffers temporários com o mesmo lifetime por fence.
Conversões D16, D24S8, D32S8 e RGB565 e o redimensionamento existente permanecem.

Falhas durante a conclusão sinalizam tanto as promessas do frame atual quanto
as da submissão anterior ainda retidas pelo renderer. O encerramento espera a
GPU e drena a última submissão; erros cancelam as promessas em vez de deixar
um consumidor esperando indefinidamente. Perda física de dispositivo não foi
injetada nos testes.

Os testes de pixels existentes foram ampliados com crescimento/reuso de pool,
20 consultas simultâneas, oclusão total/parcial, slots de readback independentes,
overflow dos oito slots, conclusão por polling e Finish, drenagem no encerramento
e cancelamento de consultas. Permanecem os testes de MSAA e depth/stencil.
Uma execução intermediária passou nos perfis assíncrono e síncrono em lavapipe;
consulte o relatório final para a revisão completa e validação na RX 580.

As novas métricas do renderer incluem consultas, criação/reuso de pools,
submissões de visibilidade com conclusão diferida, pedidos/bytes de readback,
criação/reuso de staging, conversão de readback, criação de pipelines e
submissões sem apresentação. Tempos de espera e conversão podem ser aninhados;
não os some indiscriminadamente para prever FPS. Cada submissão já produz seu
timestamp GPU; as amostras do mesmo frame guest precisam ser somadas mesmo
quando chegam posteriormente por polling ou captura diagnóstica.

## English

Implemented: persistent fence-retired precise occlusion pools, a retained host
query-result vector, bounded mapped readback staging, deferred completion for
query-only submissions, counters and cancellation of previous in-flight work.
No visibility value is fabricated. Existing depth/stencil/scissor precision,
MSAA normalization, pending status, readback conversion and command ordering
remain intact. CPU readbacks and the synchronous completion profile still wait.

SDK GetReport is an ordered asynchronous publication API; the Vulkan capture
backend currently blocks command production while awaiting its completion.
That serialization is confirmed in source, but its contribution to London's
sun-related slowdown requires scene-correlated measurements. This revision
does not change report publication or impose a fixed frame delay. A future
async implementation needs a safe guest-memory publication/lifetime boundary.

The single in-flight fence allows one geometrically growing query pool to be
reset and reused safely. Readback staging retains at most eight slots and
64 MiB of requested capacity; overflow uses ordinary transient buffers.
Actual driver allocations may include padding. Distinct slots protect multiple
ordered reads in the same submission. Shutdown drains outstanding real results;
errors notify current and previous promises. Device-loss injection remains
untested. Expanded GPU tests cover reuse, growth, simultaneous results,
readback-slot overflow, polling, Finish, shutdown and cancellation; final test
and hardware results are recorded in the validation report linked above.

Consulted primary references: [Khronos query reset](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdResetQueryPool.html)
and [query result availability and synchronization](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html).
