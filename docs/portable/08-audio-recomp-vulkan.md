# Áudio, antialiasing e configurações RECOMP no Vulkan

Validação local em 11 de novembro de 2026. O trabalho partiu de `portable`,
HEAD `1d706e2`, com mudanças locais anteriores de resolução, cache de recursos,
pós-processamento e áudio. Essas mudanças foram preservadas e incorporadas.
O SDK continua em `97b1654736f744a3966b5dff3d49be6d61ac7244`; o SHA-1 de
`assets/default.xbe` foi conferido antes/depois. Nenhum arquivo do SDK ou do
jogo foi alterado. Capturas, perfis de teste e logs ficam em
`.local/validation/mission-audio-aa/`, fora do Git.

## Causa do áudio da Quick Race

O problema era a rota de saída ignorada, e não um multiplicador de distância.
O SDK fixado implementa `BUFFER(SetOutputBuffer)` como no-op em
`sdk/runtime/xdk/dsound.cpp`. Seu mixer envia as amostras de cada voz diretamente
ao estéreo. O PGR1, por sua vez, envia os motores a **buffers de submix 3D sem
PCM próprio**. Atenuar esses buffers silenciosos não atenuava os motores que
escapavam diretamente para os alto-falantes.

A Quick Race real foi executada na RX 580. O trace identificou:

| Recurso | Dados observados no jogo |
| --- | --- |
| Fontes dos motores | Buffers mono Xbox ADPCM, formato `0x69`, flags `0`, criados pelo caller `0xAD598` |
| Destinos posicionais | 19 buffers mono sem PCM, flags `0x2010` (`MIXIN | CTRL3D`), caller `0xB62F1` |
| Roteamento | `SetOutputBuffer` pelo caller `0xADAED`, também `0xAD73D` para clones |
| Jogador | Nove buses, começando em `B00B6CD0/B00B6D10` nessa execução |
| Cinco adversários | Pares `B00B7DF0/7E30`, `B00B88B0/88F0`, `B00B8F30/8F70`, `B00B9570/95B0`, `B00B9BF0/9C30` |
| Coordenadas | Atualizadas pelo jogo em modo 1, relativas ao listener; listener zero é correto nesse caminho |
| XACT 3D Voice Data | Nenhuma atualização; `Use3DVoiceData/Set3DVoiceData` não estão vinculados no executável recompilado |

Isso explica as tentativas anteriores: o rolloff de 04D.4 alterava o destino que
não recebia amostras; o caminho XACT de 04D.5 não era chamado por esse título.

O [overlay do mixer](../../portable/linux/vulkan/audio/build_dsound_overlay_04d5.py) gera uma
cópia modificada **na árvore de build**, sem mudar o submódulo. Agora retém a
referência ao destino, soma PCM no submix mono e só então aplica posição, volume,
headroom e bins do destino. Não existe cópia estéreo seca que contorne essa rota.
As ideias de roteamento e referências foram adaptadas seletivamente do
[PR experimental 9](https://github.com/TGP482/XboxXBERecomp/pull/9); o PR completo
não foi integrado.

O caminho observado é PGR1 → HLE DirectSound → fontes ADPCM → submixes mono
posicionais → mixer estéreo → `sdl_audio.cpp` → SDL3, float estéreo a 48 kHz.
As vozes dos motores identificadas são buffers, não streams. Rádio e interface
mantêm seus caminhos não posicionais.

Há armazenamento fixo por voz para os blocos SDL de até 1024 frames, sem novas
alocações no callback. Buses encadeados têm profundidade limitada, ciclos são
rejeitados e mudanças de ganho usam uma rampa dentro do bloco, até 21,3 ms a
48 kHz. Stop, flush, fim natural e release respeitam a rota, inclusive o último
bloco parcial. Música e outras vozes estéreo continuam no seu caminho.

Exemplo do **mesmo** bus de adversário, mantendo volume solicitado em -400 mB:

| Instante | Distância | Ganho esquerdo / direito |
| --- | ---: | ---: |
| 35,670 s | 39,035 | 0,048361 / 0,056495 |
| 85,802 s | 537,295 | 0,000462 / 0,004826 |

O bus principal do jogador permaneceu em distância 10,244, ganho aproximadamente
0,2348 / 0,2348. Os dados completos estão em `quickrace-audio-buses.csv` no
diretório local de validação. As posições já incorporam a câmera do jogo.

A pausa não criou outro conjunto de 19 buses. Ao sair da corrida para a seleção
de cidade, o jogo manteve algumas vozes em loop, mas solicitou -10000 mB: os
ganhos efetivos foram zero. Esse comportamento de retenção do título foi
preservado. A liberação, parada e drenagem também são cobertas pelo teste PCM.

A comparação integrada renderiza PCM pelo mixer de produção: no caminho antigo,
distância 20 mantém saída 0,500000, igual à próxima; no novo, passa a 0,025000,
razão 0,05. Isso é uma comparação de amostras, não uma aprovação auditiva.

A comparação também foi reproduzida **no jogo real**, habilitando
`PGR1_AUDIO_SUBMIX=0` em uma execução separada. A mesma fonte `B00BA170`, destinada
ao bus adversário `B00B9C30`, apresentou:

| Rota em torno de 95,8 s | Distância do bus | Volume da fonte (mB) | Ganho E / D | Soma normalizada pelo volume da fonte |
| --- | ---: | ---: | ---: | ---: |
| Antiga, bypass | 303,574 | -360 | 0,220231 / 0,220231 | 0,666666 |
| Corrigida, pelo submix | 294,625 | -384 | 0,001409 / 0,002831 | 0,006597 |

As trajetórias não são idênticas; a normalização remove a pequena diferença de
volume pedida pelo motor. O caminho antigo continua enviando um ganho alto ao
estéreo mesmo com o destino a mais de 300 unidades. Logs e observações estão
em `quick-audio-legacy/console.log` e `audio-route-comparison.json`.

**Ainda é necessária escuta humana em diferentes câmeras e em uma corrida
completa** para encerrar todos os critérios de áudio.

## Menu e recursos efetivos

| Opção RECOMP | Estado no Vulkan nativo |
| --- | --- |
| MSAA | Off/2×/4× conforme capacidades; imagens reais multisample e resolve de cor e depth/stencil; 8× indisponível |
| Smooth alpha | Alpha-to-coverage real quando MSAA está ativo |
| Edge smoothing | Off/FXAA; pass final com alternância imediata |
| SMAA | Explicitamente indisponível |
| Sharpening | Controle real de 0 a 100%; aplicado após AA, limitado pela faixa local para evitar halos |
| Render scale | Guest ou 25–200% da saída SDL; 100% preservado |
| Scaling filter | Bilinear ou sharpening leve no pass de saída; não promete um filtro inexistente |
| Texture filtering | Estado do jogo ou anisotropia até o limite consultado da GPU |
| Frame counter | Overlay sobre a imagem final, On/Off imediato e persistente |
| Frame rate cap | Aplicado em runtime; respeita o intervalo de apresentação solicitado pelo título |
| VSync | FIFO ligado; MAILBOX/IMMEDIATE suportado desligado; fallback refletido no menu |
| Low latency | Uma submissão em voo, fixo; não oferece uma opção sem implementação |
| Resolução / modo | Aplicação pelo thread SDL, incluindo janela, borderless e fullscreen exclusivo com modo suportado |
| Monitor | Display primário fixo |
| World textures / sharp text and HUD | Explicitamente indisponíveis; a captura nativa não usa as substituições de textura do SDK |
| Game frame rate | Identificado como opção que exige reinicialização |
| Pause in background | Indisponível; não há protocolo seguro de suspensão do jogo/áudio nesse host |

MSAA consulta máscaras de amostras e os formatos de attachments no dispositivo,
negocia Vulkan 1.2 e usa `SAMPLE_ZERO` no resolve de depth/stencil. Sem esses
recursos, o caminho Vulkan 1.0 de uma amostra continua disponível. Superfícies
auxiliares e cubemaps permanecem single-sample. Os caches de reflexos, staging,
descritores e framebuffers anteriores foram preservados. CopyRects reconstitui
a cor multisample por GPU antes dos draws seguintes.

A troca de MSAA aguarda um clear completo de depth/stencil, inclusive flags
`0x03` usadas pelo PGR1, preservando a cor anterior. Enquanto aguarda, o menu
identifica a solicitação como `pending`. A troca não adiciona device-idle por
frame. Há custo de banda: o renderizador existente abre um renderpass por draw,
portanto resolve cor/depth por draw. MSAA permanece desligado por padrão.

O contador usa relógio monotônico e apenas apresentações aceitas por
`vkQueuePresentKHR` (`VK_SUCCESS`/`VK_SUBOPTIMAL_KHR`). Submissões offscreen,
ticks e aquisições malsucedidas não contam. FPS e tempo médio por apresentação
atualizam a cada 0,5 s. Uma fonte bitmap é desenhada por attachment clears após
o pós-processamento, sem dependência externa. Os CSVs mantêm os diagnósticos e
agora incluem FPS, intervalo de apresentação, MSAA aplicado, FXAA e escala.

O teste no jogo abriu Pause → RECOMP → Performance → Frame counter. As capturas
`quick/fps-menu-off.png` e `quick/fps-menu-on.png` comprovam o desaparecimento e
retorno do overlay, e `quick/linux.conf` registrou a preferência.

Outra Quick Race verificou Pause → RECOMP → Picture: MSAA off → 2× → off e
FXAA on → off → on. Além das capturas, o CSV confirmou duas amostras aplicadas
e depois uma; o arquivo de configuração registrou MSAA e FXAA. Os cinco checks
de `quick-live-settings/live-menu-result.json` passaram. A corrida voltou da
pausa após as trocas. Isso valida a cadeia menu → preferências → backend no
jogo, além das fixtures.

`Capture()` antes lia somente o render target do jogo, anterior ao FXAA.
Agora executa o mesmo pipeline de saída e inclui AA e overlay. `ReadDisplay()`
permite comparar configurações sobre a mesma imagem resolvida, sem avanço da
simulação. `PGR1_CAPTURE_VARIANTS=1` gera `fxaa-off.ppm`, `fxaa-on.ppm` e
`fps-on.ppm`; o arquivo de comando `variants` pede as mesmas capturas durante
a execução. São operações diagnósticas, fora da medição de gameplay.

Também foi corrigido um abort preexistente ao sair: imagens do cache de cubemaps
eram destruídas depois de `vkDestroyDevice`. O cache agora é liberado com o
dispositivo ainda vivo.

## Persistência e precedência

`app::Load/Save/ApplyDisplay` agora têm implementação Linux. Preferências vão a
`$XDG_CONFIG_HOME/pgr1-recomp/linux.conf`, ou
`$HOME/.config/pgr1-recomp/linux.conf`. `PGR1_CONFIG_FILE` permite um arquivo de
teste isolado. A gravação usa arquivo temporário e rename atômico; números
inválidos e valores incompatíveis são normalizados. Opções próprias do PGR1,
como rádio, FOV, HUD e skip intro, também são persistidas.

A ordem é: **defaults → arquivo validado → ambiente no início → alterações ao
vivo pelo menu**. O ambiente não é reaplicado por frame ou por Save. O menu
publica atomics; threads de render/áudio não acessam `app::Settings` mutável.
Resolução e modo publicam uma revisão consumida pelo thread SDL.

Diagnósticos preservados: `FPS`, `OUTPUT`, `PGR1_FULLSCREEN`,
`PGR1_VK_INTERNAL_PERCENT`, `PGR1_VK_FXAA`, `PGR1_VK_SHARPEN`,
`PGR1_VK_ANISO`, `PGR1_VK_MSAA=0|2|4`, `PGR1_VK_VSYNC`, `PGR1_SHOW_FPS`,
`PGR1_VK_PRESENT_MODE=fifo|mailbox|immediate`, `PGR1_VK_REFLECTION_MIPS`,
`PGR1_VK_XACT_3D` e o multiplicador espacial antigo para diagnóstico.
O script só define `FPS` quando `PGR1_FPS` é explicitamente fornecido; não
sobrescreve mais a preferência salva com 60 em toda execução.

`PGR1_AUDIO_TRACE=1` habilita criação, caller, tipo, flags, formato, bins,
roteamento, reprodução, coordenadas, distância, ganho efetivo e ciclo de vida.
Eventos são limitados a 1024 e o estado de até 64 vozes é amostrado a cada cinco
segundos, priorizando os destinos 3D. `PGR1_AUDIO_SUBMIX=0` reproduz a rota antiga
para comparação. Logs não são emitidos por amostra.

## Testes e limitações

Comandos de compilação e teste usados:

```sh
JOBS=4 bash scripts/portable-vulkan.sh build-game
PGR1_VK_DEVICE='RX 580' bash scripts/portable-vulkan.sh test
```

Oito testes CTest passaram: contador de apresentações, relatório de apresentações,
modelo de geometria, ownership de
submissões, mixer PCM de produção, callbacks reais do menu e persistência,
pixels Vulkan e captura do backend SDK. As fixtures novas de áudio/menu usam
símbolos sintéticos e não precisam do XBE nem do código gerado do jogo.
O relatório Python também passou seus nove casos, incluindo aquisições que
falham e intervalos iniciais sem uma apresentação anterior. Nenhuma falha
permaneceu na suíte final; o abort observado na referência foi corrigido.

O teste de GPU mede uma diagonal: 0 pixels de cobertura intermediária sem MSAA,
560 em 2× e 600 em 4×. Também compara FXAA, sharpening e overlay sobre o mesmo
frame; verifica depth/stencil resolvido, depth retido, CopyRects, cobertura alpha
e trocas de MSAA sem perda da cor. São fixtures, distintas da corrida real.

GPU efetivamente usada: AMD Radeon RX 580 2048SP, RADV POLARIS10, Mesa 26.0.8,
Vulkan 1.4.335; caminho MSAA negociado em 1.2. NVIDIA RTX 3060 Ti **não estava
disponível** e permanece pendente. A camada Khronos de validação não está
instalada; não se declara execução livre de mensagens dessa camada.

### Desempenho observado

Execuções separadas na RX 580, saída 640×480, escala interna 100%, mesmo roteiro
Quick Race e MSAA desligado salvo indicação. A tabela usa 240 apresentações,
frames 2601–2840. O roteiro é repetido, mas cenas móveis e carga do host variam;
estes valores são amostras de diagnóstico, não um benchmark de pista completa.
As capturas foram inspecionadas para distinguir corrida, seleção e pausa.

| Quick Race | FPS médio | Intervalo médio (ms) | P95 (ms) | P99 (ms) | 1% low | GPU média (ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sem AA | 23,89 | 41,86 | 71,60 | 87,49 | 11,22 | 10,91 |
| FXAA | 23,80 | 42,02 | 66,89 | 74,71 | 12,05 | 11,39 |
| MSAA 2×, FXAA off | 8,91 | 112,17 | 182,43 | 198,98 | 4,61 | 61,22 |
| MSAA 4×, FXAA off | 6,67 | 150,03 | 249,31 | 284,00 | 3,49 | 95,29 |

O 1% low é o inverso da média dos intervalos do 1% mais lento. Tempos GPU são
timestamps concluídos, associados ao frame correto. Não são estimados pelo FPS.

| Médias (ms) | frame_ms | capture_ms | texture_check_ms | execute_ms | queue_wait_ms | fence_ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sem AA | 41,86 | 31,04 | 12,29 | 14,25 | <0,01 | 11,01 |
| FXAA | 42,05 | 30,50 | 12,20 | 12,89 | <0,01 | 11,49 |
| MSAA 2× | 112,24 | 32,08 | 11,31 | 35,16 | <0,01 | 61,39 |
| MSAA 4× | 150,09 | 32,92 | 11,37 | 39,71 | <0,01 | 95,47 |

Essas etapas se sobrepõem entre produtor, consumidor e GPU; não devem ser
somadas como um único caminho serial. O custo atual de MSAA é elevado e está
concentrado nos resolves por draw. O recurso foi verificado em cenas reais com
2×/4× aplicados e sem draws rejeitados; permanece desligado por padrão. Não há
promessa de desempenho aceitável com MSAA nessa arquitetura.

Na seleção de veículos, incluindo carregamento (frames 1501–1600), as médias
foram 41,32 FPS sem AA, 42,35 com FXAA, 20,33 em MSAA 2× e 14,44 em 4×. A amostra
Pause/RECOMP inspecionada (105–125 s da execução `quick`) teve 12,16 FPS, P95
121,47 ms, P99 142,70 ms, 1% low 6,87 FPS e GPU média 27,02 ms. Essa lentidão de
pausa também permanece aberta.

Na comparação Arcade anterior/depois, com o mesmo roteiro salvo, resolução e
FXAA, os mesmos frames 2601–2840 deram:

| Arcade | FPS médio | P95 (ms) | P99 (ms) | 1% low |
| --- | ---: | ---: | ---: | ---: |
| Checkout inicial, incluindo mudanças locais | 27,89 | 48,83 | 60,99 | 16,10 |
| Implementação atual | 28,53 | 52,08 | 63,34 | 15,53 |

A média não mostrou regressão relevante nessa amostra, mas a cauda variou e
não se afirma ganho de desempenho. A referência inicial abortou ao destruir o
cache Vulkan depois da captura; a execução atual encerrou normalmente.

Sobre o mesmo front buffer de uma corrida real, FXAA modificou 129877 dos
307200 pixels. A comparação não avança física ou câmera; confirma a execução
do shader de saída, separadamente das amostras de desempenho.

As medições locais completas (`timing-summary.json`, `frames.csv`, `gpu.csv`) e
capturas das configurações ficam no mesmo diretório de validação. Nenhuma
amostra curta substitui uma corrida completa:
a meta de 60 FPS estáveis não está comprovada. Validação auditiva, comparação
visual detalhada de reflexos/HUD em outras pistas e execução NVIDIA ainda
impedem considerar o port concluído.

Referências: [depth/stencil resolve da Khronos](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_depth_stencil_resolve.html),
[amostra MSAA da Khronos](https://docs.vulkan.org/samples/latest/samples/performance/msaa/README.html).

## Arquivos e versionamento

As mudanças locais anteriores e as novas foram incorporadas por área:

* Áudio: `portable/linux/vulkan/audio/` contém os dois geradores de overlay,
  `xact3d_gain.h`, a fixture de símbolos e `test_audio_mixer.cpp`.
* Configurações: `portable/linux/gldirect/app_linux.cpp`, `settings_store.h`,
  `test_linux_settings.cpp`, `tests/image_symbols.h`,
  `src/game/settings_entry.cpp`, `src/game/frame_rate.cpp` e
  `portable/linux/vulkan/quality_state.h`.
* Renderização: `portable/linux/vulkan/game_renderer.cpp/.h`,
  `vk_postfx.cpp/.h`, `shaders/postfx.vert/.frag`, `main.cpp`,
  `present_counter.h`, `vk_fps_overlay.cpp/.h`, `test_game_renderer.cpp` e
  `test_present_counter.cpp`. O ganho anterior de lookup em
  `portable/linux/gldirect/backend.cpp` foi preservado.
* Build e validação: `portable/linux/vulkan/CMakeLists.txt`,
  `scripts/portable-vulkan.sh`, `scripts/pad/portable-quick-race.txt`,
  `portable/tools/frame_report.py`, `portable/tests/frame_report_tests.py`,
  este relatório e seu link em `docs/portable/README.md`.

Commits de implementação: `4ec2aea` (submixes posicionais) e `661c142`
(controles gráficos, persistência e overlay). O commit de validação inclui este
relatório, o roteiro Quick Race e a correção do cálculo de FPS nos relatórios.
Os hashes completos e o resultado da publicação são informados na entrega.
