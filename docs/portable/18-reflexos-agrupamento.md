# 04D.16 — Reflexos e agrupamento Vulkan / Reflection-aware batching

## Português (Brasil)

O relatório da RTX 3060 Ti registrou 36,13 apresentações/s na sessão
completa e média de 49,80 ms nos últimos 300 frames. Não é possível
identificar a cena exata apenas pelo CSV. O jogador relatou quedas quando
a luz solar incide na carroceria. A GPU teve média de 8,115 ms e P95 de
17,133 ms; a captura CPU teve média de 14,694 ms e P95 de 27,255 ms.

O agrupador anterior excluía qualquer textura de render target e qualquer
cubemap com faces dinâmicas. A nova regra permite agrupar draws consecutivos
com o mesmo destino quando as origens são externas aos anexos de cor e
profundidade desse destino. Limpezas, cópias, readbacks, alterações de alvo
e leituras do próprio destino interrompem o agrupamento. A preparação de
texturas e descritores ocorre antes do render pass. A ordem dos desenhos,
as consultas de visibilidade, os shaders, mipmaps e o áudio são preservados.
A regra mantém o modelo atual do backend de identificação por endereço.

O cache de reflexos mantém a validação das versões das faces. O teste
offscreen verifica cores distintas em duas metades do alvo, separadas
por uma atualização da face de origem. O teste do planejador verifica
origens externas, feedback de cor/profundidade e a reversão.

Para comparação ou regressão, inicie com
`PGR1_VK_BATCH_EXTERNAL=0`. O padrão é habilitado. Compare a mesma curva
e câmera na pista com e sem essa variável, mantendo MSAA, FXAA, resolução
e apresentação iguais. Nenhum ganho de FPS ou resolução definitiva da queda
solar foi demonstrado até a validação no jogo.

## English

The RTX 3060 Ti run averaged 36.13 presentations/s overall and 49.80 ms
per frame over the last 300 records; the exact scene cannot be identified
from the CSV alone. The player observed stalls when sunlight hits the car.
The previous batch planner excluded every GPU-backed texture and dynamic
cubemap. This revision permits consecutive draws with identical targets
to sample external GPU sources, provided none of those sources is the
current color or depth attachment. Clears, copies, readbacks, target
changes and destination feedback break groups. This retains the backend's
existing address-based resource identity model.

Textures/descriptors are prepared before the pass; reflection face version
checks, draw ordering, queries, shader features, mipmaps and audio remain
unchanged. The offscreen regression draws different colors on either
side of an intervening reflection-source update. The planner test covers
external sources, color/depth feedback and the fallback.

Set `PGR1_VK_BATCH_EXTERNAL=0` to restore the previous conservative grouping.
Compare the same sunny section with matching graphics settings. The change
is intended to reduce render-pass overhead; no FPS gain is claimed before
the hardware test.
