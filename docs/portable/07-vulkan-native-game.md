# Etapa 04C — comandos do PGR1 no Vulkan nativo

Esta etapa acrescenta um executável do jogo separado dos fixtures 04A/04B.
O caminho é o código recompilado do PGR1, runtime Xbox do SDK, captura de
`gpu::Backend`, fila de `Submission` próprias, Vulkan e swapchain SDL3.
A existência desse caminho e os testes sintéticos não comprovam, por si só,
uma corrida visualmente correta. A validação de jogo e as limitações devem ser
consultadas abaixo.

Base auditada: branch `portable`, commit
`13bd5f52135bc6ab08bba7ce98e314554ddbcde8`; SDK preservado em
`97b1654736f744a3966b5dff3d49be6d61ac7244`.

## Compilar e executar

Os arquivos pessoais extraídos do disco devem estar em `assets/`; o código
recompilado deve existir em `generated/game/`. O script identifica a revisão pelo
SHA-1 de `default.xbe`, seguindo a seleção de hooks já usada pelo port OpenGL.
Não há atualização do submódulo SDK nem exigência de revisão indisponível.

```sh
bash scripts/portable-vulkan.sh build
bash scripts/portable-vulkan.sh test
JOBS=4 bash scripts/portable-vulkan.sh build-game
PGR1_VK_DEVICE='RX 580' bash scripts/portable-vulkan.sh run-game
```

Na outra máquina:

```sh
JOBS=4 bash scripts/portable-vulkan.sh build-game
bash scripts/portable-vulkan.sh test
PGR1_VK_DEVICE='RTX 3060 Ti' bash scripts/portable-vulkan.sh run-game
```

`PGR1_VK_DEVICE` seleciona uma parte do nome sem distinguir maiúsculas de
minúsculas. Um nome ausente provoca erro, permitindo identificar a GPU realmente
testada. O log imprime o dispositivo escolhido. Os comandos `smoke`, `draw-test`
e `test` preservam suas interfaces. `draw-test` continua desenhando um fixture;
seus números não são desempenho de PGR1. `scripts/portable-linux.sh run-vulkan`
continua significando OpenGL sobre Zink.

## Captura, propriedade e shaders

O produtor usa o montador de vértices e primitivas do SDK. Os comandos carregam
vértices, índices, constantes, estados e snapshots de texturas com propriedade
própria. Endereços Xbox identificam recursos, sem transmitir ponteiros para
memória emulada à thread Vulkan. A fila limitada preserva ordem e contrapressão;
desenhos necessários não são descartados para reduzir esperas.

A captura e o empacotamento de constantes são compartilhados com o caminho
OpenGL. O executável Vulkan seleciona a tradução nativa por definição de
compilação. O gerador NV2A/fixed function do SDK produz HLSL, e `glslangValidator`
produz SPIR-V Vulkan 1.0 diretamente. A compilação ocorre em um worker; cada
programa possui uma future compartilhada. O consumidor espera o shader
necessário antes de executar aquele desenho e mede a espera. A primeira
utilização ainda pode bloquear; ela não é escondida por um shader substituto.

O cache `shadercache-vk-v1` identifica fontes e opções por conteúdo. O cache
OpenGL permanece separado. O caminho Vulkan não passa por SPIRV-Cross/GLSL.
O shader conserva a profundidade `[0,1]` do SDK, inverte Y para o viewport Vulkan
positivo e usa localizações estáveis para registradores de entrada e varyings.
As transformações de viewport já incorporadas ao shader usam um viewport físico
que cobre o target inteiro.

O layout no conjunto 0 é explícito:

| Binding | Conteúdo |
| --- | --- |
| 0 | VC: 192 registradores `float4` de constantes de vértice |
| 1 | DC: `gpu::DrawConstants`, mesmo empacotamento do SDK |
| 2–5 | Imagens `t0`–`t3` |
| 6–9 | Samplers `s0`–`s3` |

O contrato é definido em [shader_compiler.h](../../portable/linux/gldirect/shader_compiler.h)
e corresponde aos blocos de [shaders.h do SDK](../../sdk/runtime/gpu/shaders.h).

A inversão de textura de render target usada pelo OpenGL é desativada para
Vulkan. Readbacks devolvem linhas na orientação Xbox, antes da escrita pelo
produtor na memória emulada.

## Operações e requisitos do dispositivo

| Entrada `gpu::Backend` | Caminho implementado |
| --- | --- |
| `Clear` | Retângulos de cor/depth/stencil; clear parcial por canal de cor falha explicitamente |
| `Draw` | Montagem pelo SDK; vértices/índices próprios; `vkCmdDraw`/`vkCmdDrawIndexed`; estados de viewport/scissor/culling, blend e depth/stencil |
| `Copy` | Cópia de memória quando a fonte pertence à CPU; transferência Vulkan entre superfícies GPU |
| `Present` | Seleção do target final, transferência para a swapchain e `vkQueuePresentKHR` |
| `Sync` | Readback ordenado com fence antes da escrita na memória emulada |
| `SyncLate` | Readback enfileirado e entrega posterior, preservando a propriedade da promessa e dos buffers |
| `Fence`/`FencePending`/`WaitFence`, `Busy`/`WaitResource` | Tokens de conclusão da submission, sinalizados depois da fence Vulkan |
| `ClearReport`/`GetReport` | Queries de oclusão precisas por desenho contado, somadas e escritas no layout de 16 bytes do SDK |

Os targets têm formatos BGRA8, RGB565, D16 ou D24S8 verificados no dispositivo.
Se o dispositivo não oferecer D24S8 como attachment, a implementação consulta
D32 float com stencil como alternativa e registra a escolha. O readback
quantiza novamente a profundidade para D24 do Xbox; a precisão durante desenhos
pode diferir. Passes com comparação `EQUAL` precisam de verificação visual.
Texturas 2D com mipmaps, cubemaps de seis faces e faces atualizadas a partir de
render targets usam imagens Vulkan. Samplers e pipelines têm variantes por
estado. Bordas de textura arbitrárias, pontos sem lowering nativo de
`PointSize` e outros estados ausentes são registrados e rejeitados explicitamente.
As queries de visibilidade exigem a feature consultada `occlusionQueryPrecise`;
quando ausente, os desenhos contados falham explicitamente. Os resultados são
amostras aprovadas pela GPU, incluindo depth/scissor, sem fabricar contagens a
partir de frames ou chamadas de desenho.

O renderer usa o núcleo Vulkan 1.0. A apresentação requer as extensões de
instância informadas por SDL3 e `VK_KHR_swapchain`. Wireframe ausente provoca
rejeição; anisotropia é habilitada quando a feature consultada está disponível.
Os uploads usam memória `HOST_VISIBLE|HOST_COHERENT`; imagens usam
`DEVICE_LOCAL`. Falhas de seleção/alocação retornam diagnóstico.

A implementação serializa submissions por uma fence, mantendo buffers e
imagens vivos até a conclusão do dispositivo. Não usa `vkDeviceWaitIdle` em
cada frame; esse bloqueio fica na aposentadoria da swapchain e no encerramento.
Há um semáforo de conclusão de apresentação por imagem. `OUT_OF_DATE` e
`SUBOPTIMAL` provocam recriação da swapchain. Layout `GENERAL` e barreiras
conservadoras priorizam a correção nesta primeira versão; não representam
um pipeline já otimizado.
A organização dos semáforos por imagem segue a
[orientação oficial da Khronos sobre reutilização de semáforos da swapchain](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html):
uma fence do submit, isoladamente, não garante que a apresentação já terminou
de esperar o semáforo.

## Medições e comparação

O runtime grava `frames.csv` e `submissions.csv` no diretório de execução,
incluindo os lotes sem apresentação emitidos por sincronizações. As colunas
separam captura, montagem, verificação/decodificação de texturas, espera na fila,
shader, gravação/execução, fences, aquisição e apresentação. Os contadores
distinguem desenhos aceitos e rejeitados na captura e no Vulkan, além de
chamadas indexadas e não indexadas.

Tempo de GPU e tempo de CPU/espera têm significados diferentes. Esperas longas
na fila, aquisição ou apresentação não provam saturação da GPU. Os tempos
incluem o trabalho de primeira utilização; nenhum frame lento deve ser excluído
da análise. A captura de imagens serve para comparar o mesmo ponto do jogo com
os caminhos CPU e OpenGL; igualdade visual requer executar e alinhar esses
caminhos nas máquinas de destino.

`gpu.csv` mede as submissions recebidas do jogo, incluindo seus prefixos sem
apresentação. Transferências extras de diagnóstico feitas por `Compare`/`Capture`
podem usar frame 0 e não aparecem como amostras de jogo nesse arquivo. O tempo
de parede dos frames conserva o custo desses diagnósticos; os timestamps GPU
não devem ser interpretados como tempo total de todo o trabalho do processo.

Uma comparação CPU/Vulkan pode ser solicitada para um frame do jogo:

```sh
PGR1_COMPARE_FRAME=30 PGR1_RUN_SECONDS=60 PGR1_VK_DEVICE='RX 580' \
  bash scripts/portable-vulkan.sh run-game
```

O diretório de execução recebe `compare-reference.ppm`, `compare-vulkan.ppm` e
`compare.txt`, com erro RGB médio, máximo e quantidade de pixels diferentes.
O produtor inicializa a referência a partir do prefixo real da GPU, executa os
comandos do mesmo frame e entrega os pixels próprios ao consumidor. O cálculo
inclui sincronização e readback; seu custo permanece nas medições. Para comparar
também OpenGL, é necessário reproduzir o mesmo perfil, entrada, configuração e
frame no outro executável, sem confundir capturas de pontos diferentes do jogo.

Na validação AMD de 10/11/2026, o frame 30 produziu imagens CPU/Vulkan de
640×480 com erro RGB médio de 0,00769531 em escala de 0 a 255, erro máximo de
um nível por canal e zero pixels com erro acima de quatro entre 307.200 pixels.
Foram aceitos 50 desenhos reais nesse frame. Esse ensaio cobre uma tela de
inicialização; não demonstra paridade de veículos,
reflexos ou pista. Os arquivos foram mantidos localmente, sem inclusão no commit.

## Testes

O teste `vk_sdk_offscreen_pixels` instancia o renderer sem janela e executa
comandos sintéticos com shaders reais gerados pelo SDK. Verifica desenhos
indexados e não indexados, orientação/scissor, culling, retenção de targets,
depth/stencil, blend/alpha test, texturas 2D com três mipmaps, as seis faces de
cubemap, atualização de face por render target, amostragem de render target,
cópia, readbacks D16/D24S8 e queries de visibilidade exatas de 1.024, zero e 64
amostras para quad visível, quad ocluído e scissor 8×8. Também traduz
instruções NV2A MOV sintéticas e verifica rejeição de índices inválidos.
Esse teste valida o renderer com um ICD Vulkan disponível; não substitui
inicializar o PGR1 nem apresentar uma swapchain.

Os testes anteriores `vk_guest_geometry_model` e `vk_owned_submission_bridge`
continuam cobrindo apenas fixture/modelo 04B. Os testes OpenGL protegem os
componentes compartilhados e continuam disponíveis por
`bash scripts/portable-gldirect.sh test`.

Validação final em 10/11/2026:

- Quatro testes Vulkan passaram na AMD RX 580, em 0,18 s, incluindo pixels,
  queries precisas, mipmaps e clamp NV2A 5. Os quatro testes também passaram
  em lavapipe/llvmpipe.
- Cinco testes OpenGL compartilhados passaram com llvmpipe, incluindo pixels,
  constantes, fila e captura pelo SDK.
- Dois testes portable-host/contrato SDK passaram.
- 49 binários SPIR-V gerados durante a execução real passaram em
  `spirv-val --target-env vulkan1.0`.

O teste de pixels Vulkan na AMD é uma validação GPU adicional; seus comandos
continuam sintéticos. A evidência do PGR1 vem das execuções descritas a seguir.

## Inicialização real na AMD RX 580

Em 10/11/2026, o executável recompilado foi iniciado com os arquivos locais do
PGR1 USA 1.0, na AMD Radeon RX 580 2048SP, RADV POLARIS10, Vulkan 1.4.335.
A primeira tentativa encontrou a ausência de D24S8 como attachment e parou com
diagnóstico, sem apresentar desenhos. Isso motivou a alternativa D32S8 consultada
descrita acima.

Com a alternativa de profundidade, uma execução de inicialização de 30,053 s
apresentou 1.679 frames, aceitou 139.513 desenhos do jogo e rejeitou 38.082.
A rejeição observada era o modo de endereço NV2A 5. A captura mostrava
o texto de publicação pela Microsoft; esse resultado comprovou comandos do PGR1
no Vulkan e apresentação, mas também demonstrou conteúdo ausente. O modo 5
foi corrigido seguindo `Address()` de `sdk/runtime/gpu/d3d_common.cpp`: nesse
contrato NV2A, o valor 5 usa clamp-to-edge. O código não presume a enumeração
desktop D3D de mirror-once, nem requer uma extensão para esse modo. Um teste com
textura vermelha/verde e coordenada U negativa diferencia os comportamentos.

Medições dessa execução inicial, incluindo todos os frames e custos de primeira
utilização:

| Medida | Média (ms) | Máximo (ms) |
| --- | --- | --- |
| Frame apresentado | 17,873 | 800,747 |
| Captura do SDK | 3,168 | 10,889 |
| Espera na fila | 1,020 | 139,690 |
| Espera/compilação de shader e pipeline | 0,097 | 163,622 |
| Fence | 0,955 | 4,109 |
| Aquisição da swapchain | 11,553 | 32,355 |
| Apresentação | 0,075 | 0,861 |
| GPU por submission (1.698 amostras) | 0,883 | 1,496 |

Os 55,87 presents/s dessa tela de inicialização não são desempenho de uma
corrida. Os tempos de aquisição refletem também apresentação FIFO. O primeiro
frame lento foi mantido nas medições. Logs, CSVs e imagem da tentativa foram
preservados localmente fora dos arquivos versionados.

Uma tentativa posterior com perfil salvo chegou ao cenário 3D e revelou que
o jogo precisava de relatórios de visibilidade. Ela foi interrompida com erro
explícito enquanto o recurso ainda estava ausente. A implementação passou a
usar queries Vulkan precisas antes de uma nova validação. As imagens
intermediárias mostraram cenário excessivamente claro e prévia de carro escura;
esses defeitos permanecem sujeitos a comparação por frame na 04D.

Após corrigir o modo 5 conforme o SDK e implementar os relatórios precisos,
uma execução de 110,166 s com perfil salvo apresentou 2.126 frames consecutivos
e executou 1.293.993 desenhos indexados do PGR1, com zero desenhos rejeitados
na captura ou no renderer. Foram medidas 2.217 submissions e amostras de GPU,
incluindo 91 lotes sem apresentação. A imagem final mostrava a introdução da
pista National District. Isso comprova cenário 3D do jogo na GPU Vulkan;
não comprova condução, uma corrida concluída ou paridade dos reflexos.
O log registrou 64 pares distintos de shaders do SDK, com 30 identificadores
de vértice e 38 de pixel; esses são programas derivados dos comandos do PGR1,
sem substituir os materiais por shaders de cor genéricos.

Medições dessa execução, sem excluir frames lentos:

| Medida | Média (ms) | Máximo (ms) |
| --- | --- | --- |
| Frame apresentado | 51,788 | 459,938 |
| Captura do SDK | 12,694 | 130,408 |
| Espera na fila | 28,823 | 345,554 |
| Shader/pipeline | 0,239 | 311,801 |
| Fence | 10,969 | 47,756 |
| Aquisição da swapchain | 1,326 | 24,067 |
| Apresentação | 0,056 | 0,685 |
| GPU por submission | 10,424 | 47,687 |

A taxa total foi aproximadamente 19,3 presents/s nesse percurso de menus e
introdução 3D. Esse valor não é diretamente comparável ao benchmark OpenGL
anterior: o conteúdo e os frames não foram alinhados. Não houve apresentação
acima de 1 ms nessa execução, mas a fila, os shaders e as fences ainda exibem
custos relevantes. Os resultados permanecem arquivados localmente em
`.local/validation/vulkan04c/corrected110`, sem imagens ou dados do jogo no commit.

## Evidência final: condução real na AMD

A execução final, com o perfil Player copiado para o diretório isolado,
durou 135,073 s e apresentou 2.228 frames. Foram executados 1.417.252
`vkCmdDrawIndexed` originados dos comandos do PGR1, sem desenhos rejeitados
na captura ou no Vulkan. As 2.326 submissions, incluindo 98 prefixos sem
apresentação, têm amostras de GPU; não houve timeout de aquisição nem falha
de renderização. O log identificou 63 pares de shaders do SDK, com 30 IDs
de vértice e 37 de pixel.

A captura final mostra o MINI em terceira pessoa na pista, HUD ativo,
primeira marcha e 16 mph. Há, portanto, evidência de condução do jogo real
com geometria, texturas e shaders pelo Vulkan nativo. O veículo permanece
escuro, os reflexos requerem comparação e há uma faixa horizontal cinza
visível. A corrida não foi concluída nem validada como visualmente correta.
Zero desenhos rejeitados não equivale a paridade visual.

Medições completas dessa execução, com cache de shaders já aquecido e
comparação/captura do frame 30 habilitadas, sem remover frames lentos:

| Medida | Média (ms) | Máximo (ms) |
| --- | --- | --- |
| Frame apresentado | 60,604 | 362,697 |
| CPU do processo por frame | 65,149 | 556,389 |
| Captura do SDK | 13,983 | 122,501 |
| Espera na fila | 36,741 | 397,484 |
| Shader/pipeline | 0,073 | 89,856 |
| Fence | 11,953 | 47,754 |
| Aquisição da swapchain | 2,373 | 28,956 |
| Apresentação | 0,059 | 0,869 |
| Gravação/execução no consumidor | 24,545 | 185,836 |
| GPU por submission | 11,354 | 47,594 |

O p95 de frame foi 211,206 ms e o p95 de GPU foi 45,218 ms, pelo método de
posição mais próxima. A taxa total
foi aproximadamente 16,5 presents/s nesse percurso de inicialização, menus,
carregamento e início de condução. A CPU mede o processo com suas threads,
podendo superar o tempo de parede. Esse percurso e os diagnósticos diferem do
benchmark OpenGL anterior; não se atribui a diferença somente à API gráfica.
Os bloqueios ainda justificam investigação, mesmo sem os presents de vários
segundos observados anteriormente.

CSVs, console, imagens final/exata e comparação CPU foram preservados apenas
localmente em `.local/validation/vulkan04c/final135`, com resumo numérico em
`timing-summary.json`. A RTX 3060 Ti não estava disponível e permanece sem
validação física nesta entrega; os comandos para testá-la estão no início
do documento.

## Trabalho restante para 04D

Há evidência de inicialização do PGR1, desenhos originados no jogo aceitos pelo
Vulkan e apresentação na AMD RX 580. Uma corrida visualmente correta exige
ainda comparação dos veículos, reflexos,
texturas e superfícies com uma referência alinhada. A seleção de GPU e o uso de
Vulkan 1.0 não substituem o teste na NVIDIA RTX 3060 Ti; a evidência da AMD
não garante o mesmo resultado no driver NVIDIA.

Ainda falta importar/preservar de modo geral pixels escritos pela CPU em render
targets antes de desenhos parciais. Os readbacks GPU→CPU e os snapshots de
texturas CPU→GPU já existem, mas não comprovam essa direção de atualização de
targets retidos. Também permanecem a comparação da alternativa D32S8, correção
visual dos reflexos/veículos, gamma, clears parciais por canal, PointSize e bordas
arbitrárias. Otimizar barreiras, passes, alocações e compilação de pipelines
depende primeiro de validar essas semânticas com o mesmo frame de referência.
