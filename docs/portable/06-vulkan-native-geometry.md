# Etapa 04B — pipeline gráfico Vulkan 1.0, FVF/XY... e seleção AMD/NVIDIA

## O que foi entregue (e o que NÃO foi)

04B continua a 04A: o executável `pgr1_vk_wsi` pode criar uma swapchain nativa e agora
**também pode desenhar geometria indexada real usando a GPU Vulkan**. O modo
`--geometry-test` compila dois shaders GLSL 450 para SPIR-V, cria shader modules,
pipeline gráfico, render pass, framebuffers por imagem, buffers vertex/index
mapeados em memória HOST_VISIBLE|HOST_COHERENT, grava `vkCmdDrawIndexed` e apresenta
na swapchain. A cena é um **fixture geométrico gerado no teste**, NÃO a corrida de PGR1.

Foi adicionada uma ponte `FromSubmission()` para o mesmo modelo de comandos próprios
`pgr::gldirect::Submission` já preenchido pelo backend do PGR1. O adaptador não
executa o jogo por si só; inicialmente só reconhece triângulos indexados,
pré-transformados, sem texturas, sem shader programável, sem blend/depth/stencil e
diretamente no front buffer. **Nenhuma chamada Xbox Draw está conectada à janela
Vulkan neste marco**. Chamadas incompatíveis retornam contadores de rejeição;
nunca são renderizadas como se fossem corretas. A atualização de runtime/command
loop, NV2A SPIR-V, descriptors/textures/cubemaps, offscreen render targets,
depth/stencil e readbacks são parte de 04C+.

O script `scripts/portable-linux.sh run-vulkan` segue sendo **OpenGL sobre Zink**.
Não o confunda com o executável 04B. Nenhum bug gráfico de carros foi declarado
corrigido nesta etapa.

## Portabilidade das GPUs

O código novo usa apenas o núcleo do Vulkan 1.0 e `VK_KHR_swapchain`, sem extensões
exclusivas da NVIDIA. O dispositivo escolhido é impresso com nome, vendor/device ID
e versão da API. Por padrão escolhe uma GPU discreta com queue graphics+present.
Se houver mais de uma, use a variável `PGR1_VK_DEVICE` para selecionar pelo nome.

Na máquina NVIDIA RTX 3060 Ti:

```sh
bash scripts/portable-vulkan.sh build
bash scripts/portable-vulkan.sh test
PGR1_VK_DEVICE='RTX 3060 Ti' bash scripts/portable-vulkan.sh draw-test --frames 180
```

Na máquina AMD RX 580 (Mesa RADV com Vulkan ICD configurado):

```sh
vulkaninfo --summary
PGR1_VK_DEVICE='RX 580' bash scripts/portable-vulkan.sh draw-test --frames 180
```

O valor é comparado sem distinguir maiúsculas/minúsculas. Se o driver anunciar
outra string, use parte do nome exibido em `vulkaninfo --summary`. Se o dispositivo
não existir ou não tiver suporte WSI, a aplicação falha de maneira explícita; não
recai silenciosamente no dispositivo errado. `PGR1_VK_DEVICE` é apenas seleção,
não altera drivers do sistema.

O buffer persistente de upload exige memória visível/coerente no dispositivo.
O dispositivo e o recurso são criados com verificação de erro. O orçamento neste
protótipo é de 16 MiB de vértices+índices; frames maiores são rejeitados antes do
`memcpy`, sem acesso além do buffer. O modo de apresentação é FIFO (vsync).

**Correção de sincronização WSI:** cada imagem da swapchain tem um semáforo
`renderFinished` próprio. A 04A reutilizava um único semáforo por quadro; o fim
do submit não significa que o present anterior já terminou de esperar o semáforo.
Usar semáforos por imagem evita essa forma de reutilização precoce, em especial
quando os drivers NVIDIA e AMD/Mesa variam em temporização.

## Instalar e compilar

Este patch pressupõe 04A previamente instalada. O instalador não modifica o SDK,
assets, código recompilado nem o backend OpenGL. Se encontrar uma versão inesperada
de um dos três arquivos 04A a substituir, interrompe antes de escrever.

```sh
bash /tmp/pgr1-vulkan04b/install.sh "$PWD"
JOBS=4 bash scripts/portable-vulkan.sh build
bash scripts/portable-vulkan.sh test
bash scripts/portable-vulkan.sh smoke --frames 180
bash scripts/portable-vulkan.sh draw-test --frames 180
```

Exige Vulkan headers/loader, SDL3, `glslangValidator`, CMake e compilador C++20.
O CMake foi corrigido para usar `find_package(Vulkan REQUIRED)` (o antigo
`COMPONENTS Headers` estava incorreto). `Vulkan::Vulkan` exporta headers e loader.

## Testes e avaliação honesta

- `vk_guest_geometry_model`: geometria e índices de um fixture gerado.
- `vk_owned_submission_bridge`: conversão de comandos próprios em dados compactos;
  rejeição explícita de shader não traduzido e índice inválido.
- `smoke`: teste original de clear/apresentação 04A.
- `draw-test`: teste Vulkan **GPU real** de render pass, shader modules, upload e
  `vkCmdDrawIndexed`, sem conteúdo do jogo.

Os dois testes CPU foram compilados e executados em ambiente isolado; a ponte
foi validada com um cabeçalho substituto compatível, não com o SDK completo. Esta
entrega ainda não foi compilada contra Vulkan/SDL3 reais nem executada em uma GPU
no ambiente em que foi produzida. A validação final cabe às máquinas NVIDIA e AMD.

Para testar a corrida real **enquanto Vulkan nativo ainda não está integrado**:

```sh
bash scripts/portable-linux.sh run  # OpenGL real (com travamentos ainda em investigação)
bash scripts/portable-linux.sh run-vulkan  # Zink, NÃO Vulkan nativo
```

## Próximo desenvolvimento obrigatório — 04C

1. Ligar o runtime recompilado ao `Submission` do Vulkan com eventos SDL e áudio.
2. Transformar o pipeline em variantes por estado; suportar RTs offscreen,
   depth/stencil e subpasses/cópias.
3. Consumir SPIR-V da tradução NV2A do SDK e montar descriptors + uniform buffers.
4. Implementar texturas 2D/cubo, reflexos dos carros, mipmaps e invalidação.
5. Comparar *o mesmo frame* CPU/OpenGL/Vulkan na RX 580 e RTX 3060 Ti.
6. Medir tempo real por frame e corrigir os bloqueios vistos em `queue_wait_ms` e
   `present_ms`; não inferir FPS do fixture.
7. Executar corrida completa, áudio, controle e saves; só depois declarar paridade.
