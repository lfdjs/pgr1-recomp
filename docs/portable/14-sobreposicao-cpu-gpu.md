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
