# 04D.12 — Sobreposicao CPU/GPU / CPU-GPU overlap

## Portugues (Brasil)

O backend Vulkan aguardava a fence de **toda submissao** no final de
`Renderer::Render()`, mesmo quando nenhum readback ou consulta de
visibilidade exigia o resultado imediatamente. Isso serializava parte
do trabalho da GPU com a captura executada na thread do jogo.

Agora, o modo padrao deixa a submissao em andamento. A thread SDL/Vulkan
verifica a fence de forma nao bloqueante quando a fila esta ociosa,
liberando os tokens de sincronizacao pendentes; o proximo Render
aguarda a conclusao antes de reutilizar command buffers, staging e
descriptor pools. Readbacks, consultas de visibilidade, capturas
e encerramento continuam aguardando o resultado obrigatorio.

A opcao `PGR1_VK_PIPELINED=0` restaura a espera sincrona anterior
para comparacao A/B ou reversao de regressao. `0`, `off`, `false`,
e `no` sao equivalentes, sem diferenciar caixa. Esta alteracao
**nao descarta draws, nao altera os shaders e nao modifica o audio**.

### Comparacao

```bash
PGR1_VK_PIPELINED=0 PGR1_VK_MSAA=0 PGR1_VK_FXAA=1 \
 bash scripts/portable-vulkan.sh run-game
PGR1_VK_PIPELINED=1 PGR1_VK_MSAA=0 PGR1_VK_FXAA=1 \
 bash scripts/portable-vulkan.sh run-game
```

Salve os CSVs antes de repetir o teste, e compare trechos identicos
de Londres, menus e pausa. A sobreposicao pode reduzir a espera do
consumidor, mas a **captura CPU de texturas** ainda e um gargalo.
60 FPS nao estao garantidos antes de medir os dois computadores.

## English

The previous Vulkan renderer waited for the GPU fence at the end of
almost every submission, even without readbacks or visibility queries.
That serialized GPU execution with subsequent guest CPU capture.

The new default allows a submission to remain in flight. The SDL/Vulkan
thread polls fence status without blocking while the submission queue is
empty, fulfilling pending resource promises. The next Render still waits
before recycling command buffers, staging and descriptor pools. Required
readbacks, visibility queries, captures and final shutdown continue to
synchronize normally.

Set `PGR1_VK_PIPELINED=0` (or off/false/no, case-insensitive) to restore
the previous synchronous behavior for an A/B comparison or regression
rollback. No draws or shader features are removed. Measure matching
London Quick Race ranges and menu scenes on AMD and NVIDIA: this change
does not by itself guarantee stable 60 FPS.

### Atualização de testes — 23/11/2026 / Test update — Nov 23, 2026

**PT-BR.** O teste offscreen falhou na RTX 3060 Ti porque ainda esperava
que a promessa de conclusão fosse resolvida logo após `Render()`.
Na etapa 04D.12, a submissão pode retornar antes de a GPU terminar;
uma consulta explícita `PollCompletion()` na thread Vulkan libera o
token quando a fence realmente sinalizar. O teste de integração foi
atualizado para consultar a fence até o prazo de dois segundos,
sem transformar toda apresentação novamente em síncrona.

O CTest agora executa o mesmo conjunto tanto no modo padrão
`vk_sdk_offscreen_pixels` (`PGR1_VK_PIPELINED=1`) quanto em
`vk_sdk_offscreen_pixels_sync` (`PGR1_VK_PIPELINED=0`).

**EN.** The RTX 3060 Ti offscreen regression still assumed an immediate
completion token after `Render()`, which is no longer required under
pipelined GPU execution. The updated test polls `PollCompletion()` on
the owning Vulkan thread until the GPU signals the fence, with a
two-second bound. CTest now runs both the asynchronous and synchronous
modes to preserve compatibility.

### Investigação de apresentação / Presentation A/B

**PT-BR.** O relatório enviado após o commit 04D.15 registrou 6.554
apresentações bem-sucedidas, 32,71 FPS médios, 30,568 ms por intervalo
de apresentação, 15,775 ms de captura CPU em média e 9,162 ms de
tempo GPU médio. O P95 GPU chegou a 22,575 ms — acima do orçamento
de 16,667 ms para 60 FPS. A taxa de 84,4% acima desse orçamento
é da **sessão completa**, não necessariamente da mesma seção de Londres.

A captura exige 1.744 comparações de bytes por frame, em média,
mas o cache de 1.024 entradas registrou **zero expulsões**. O próximo
alvo é investigar o custo de validação da memória e os picos de GPU,
não ampliar indiscriminadamente o cache.

Para isolar a cadência do compositor, compare o mesmo percurso:

```bash
# Perfil A: VSync ativo, apresentacao FIFO / FIFO presentation
PGR1_VK_VSYNC=1 PGR1_VK_MSAA=0 PGR1_VK_FXAA=1 bash scripts/portable-vulkan.sh run-game
# Perfil B: VSync desligado, preferencia MAILBOX / MAILBOX if available
PGR1_VK_VSYNC=0 PGR1_VK_PRESENT_MODE=mailbox PGR1_VK_MSAA=0 PGR1_VK_FXAA=1 bash scripts/portable-vulkan.sh run-game
```

Confirme `mode=FIFO` ou `mode=MAILBOX` no log. Se MAILBOX não
estiver disponível, o runtime pode escolher IMMEDIATE ou continuar
em FIFO; portanto, valide o modo efetivamente aplicado. Preserve
arquivos CSV separados e não confunda intervalos de apresentação com
tempo de execução da GPU.

**EN.** The RTX run measured 6,554 successful presents, 32.71 average
presentations per second, 15.775 ms mean CPU capture and 9.162 ms
mean GPU work. GPU P95 reached 22.575 ms, exceeding the 60 FPS
16.667 ms budget; this is a whole-session measurement. The 1,024-entry
texture cache recorded no evictions, but guest bytes were compared
roughly 1,744 times per frame. Compare FIFO versus supported MAILBOX
on the same course and settings. Presentation timing, CPU capture
and GPU execution must be diagnosed separately; stable 60 FPS has not
been demonstrated.
