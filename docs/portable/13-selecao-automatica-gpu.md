# 04D.11 — Seleção automática de GPU Vulkan / Automatic Vulkan GPU Selection

## Português (Brasil)

O backend Vulkan nativo **já preferia GPUs dedicadas** quando
`PGR1_VK_DEVICE` não estava definida. A 04D.11 torna a regra explícita
e unifica o reconhecimento de `PGR1_VK_DEVICE=auto` em dois executáveis:
o jogo `pgr1_vk_game` e os testes de Vulkan `pgr1_vk_wsi`.

Para usar tanto a Radeon RX 580 quanto a NVIDIA RTX 3060 Ti, o mesmo
comando é suficiente:

```bash
bash scripts/portable-vulkan.sh run-game
```

Se já existir uma variável `PGR1_VK_DEVICE` herdada do terminal e for
necessário ignorar essa escolha:

```bash
PGR1_VK_DEVICE=auto bash scripts/portable-vulkan.sh run-game
```

A seleção explícita ainda funciona:

```bash
PGR1_VK_DEVICE='RTX 3060 Ti' bash scripts/portable-vulkan.sh run-game
```

### Critérios da escolha

1. O próprio Vulkan enumera os dispositivos oferecidos pelo driver/ICD.
2. O dispositivo precisa oferecer fila gráfica e, para o jogo com janela,
   suporte à superfície e swapchain.
3. Dispositivos dedicados são priorizados em relação aos demais dispositivos
   compatíveis. Empates mantêm o primeiro candidato válido retornado pelo
   Vulkan; isto **não** é um benchmark automático de velocidade.
4. Um nome manual continua sendo pesquisado sem diferenciar maiúsculas e
   minúsculas. Se não houver dispositivo compatível com esse nome, ocorre erro.
5. O jogo registra no console o modo utilizado e o nome real da GPU escolhida.

O modo automático considera **somente dispositivos disponíveis ao processo
Vulkan**. Configurações externas como `VK_DRIVER_FILES`, `VK_ICD_FILENAMES`
ou filtros de camada podem limitar a lista de dispositivos visíveis; a
seleção automática não ignora essas restrições.

### Testes

```bash
git pull --ff-only origin portable
JOBS=4 bash scripts/portable-vulkan.sh build-game
bash scripts/portable-vulkan.sh test
bash scripts/portable-vulkan.sh run-game
grep -E 'GPU selection|Seleção da GPU|native Vulkan GAME backend' .local/run/vulkan/console.log
```

O teste de unidade `vk_auto_gpu_selection` valida nomes RX 580/RTX 3060 Ti,
comparação sem diferenciar caixa e comandos `auto`, `AUTO` ou variável vazia.
Não precisa de GPU, mas a confirmação de renderização cabe aos testes reais
nas duas máquinas.

## English

Native Vulkan already preferred compatible discrete GPUs when
`PGR1_VK_DEVICE` was unset. Stage 04D.11 explicitly supports case-insensitive
`PGR1_VK_DEVICE=auto` in both `pgr1_vk_game` and `pgr1_vk_wsi`.

On either machine, just run:

```bash
bash scripts/portable-vulkan.sh run-game
```

An inherited manual override can be bypassed with
`PGR1_VK_DEVICE=auto`. Partial case-insensitive device names continue
to work for explicit manual selection.

Vulkan enumerates the ICD-visible physical devices and checks for graphics
and presentation/swapchain capability before selecting a device. Discrete
GPUs are preferred, with the first compatible device breaking ties.
The output log reports the selected GPU; automatic selection is not a
performance benchmark and cannot bypass external ICD/layer restrictions.

The hardware-independent `vk_auto_gpu_selection` test covers both GPU
names and override semantics. Actual graphical operation still needs
testing on the RX 580 and RTX 3060 Ti.
