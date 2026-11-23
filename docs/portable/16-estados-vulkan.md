# 04D.14 — Reutilizacao de estados Vulkan / Vulkan draw-state reuse

## Portugues (Brasil)

O agrupamento de draws (04D.6) reduziu a quantidade de render passes.
Mesmo assim, o gravador Vulkan continuava emitindo chamadas redundantes
de `vkCmdBindPipeline`, `vkCmdSetViewport` e
`vkCmdSetBlendConstants` em desenhos consecutivos do mesmo grupo.

O novo controle reaproveita os estados **somente enquanto um grupo
continuo de draws estaticos permanece no mesmo render pass**.
Ao iniciar outro pass ou trocar pipeline, os estados sao reaplicados.
Se apenas a cor de blend mudar, somente ela e reenviada. Descritores,
buffers, indices, scissors, consultas de visibilidade e draws continuam
com as regras anteriores.

Os CSVs `frames.csv` e `submissions.csv` agora incluem
`vk_pipeline_bind_skips`, `vk_viewport_set_skips` e
`vk_blend_set_skips`. Os valores medem chamadas evitadas, **nao
equivalem automaticamente a FPS ganhos**.

O teste C++20 `vk_draw_state_reuse` verifica reaplicacao do estado
ao mudar pipeline, cor de blend e render pass. Nao altera texturas,
mipmaps, MSAA, audio ou ordem de renderizacao.

## English

Safe static draw batching removed many render-pass transitions but
still recorded redundant pipeline, viewport and blend constant bindings.
The Vulkan command recorder now skips identical bindings **only within
one uninterrupted static draw batch**. New passes and pipeline changes
conservatively rebind all dynamic state; blend-color changes still
update the correct constant. Descriptors, vertex/index buffers, scissors,
visibility queries and draw order are untouched.

`frames.csv` and `submissions.csv` include the optional counters
`vk_pipeline_bind_skips`, `vk_viewport_set_skips` and
`vk_blend_set_skips`. C++20 unit tests cover state invalidation.
A reduction in commands is not proof of FPS improvement until verified
on the same race section with identical settings.
