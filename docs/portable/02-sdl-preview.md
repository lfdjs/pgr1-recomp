# Etapa 02 — janela SDL3 para o PGR1 recompilado no Linux

**Natureza:** prévia de diagnóstico, ainda **não é um renderizador em tempo real**.
Não substitui o `gpu::Backend` do SDK. Não altera `sdk/`, `src/game/`,
`generated/`, arquivos XBE, nem o executável headless validado anteriormente.

## Como funciona

1. `scripts/portable-gui.sh run` inicia `.local/harness/game` (o executável já compilado) numa pasta exclusiva: `.local/run/gui/`.
2. O backend `sdk/harness/null_backend.cpp` continua processando as draw calls e, com `REF=30`, usa o renderizador de referência da CPU em uma amostra periódica de frames.
3. O SDK grava as imagens TGA (`cap/ref_00030.tga`, etc.). O aplicativo SDL3 lê as novas imagens e mostra a mais recente numa janela redimensionável.
4. O SDL3 lê teclado/gamepad e envia comandos de controle por `.local/run/gui/cmd`, mecanismo **já implementado** pelo harness (`sdk/harness/pad_script.cpp`). A escrita é feita com rename atômico.
5. Fechar a janela ou apertar ESC encerra o processo do jogo iniciado pelo script, preservando os arquivos de log.

**Limites:** captura TGA + desenho por CPU não permite fluidez de 30–60 fps; exibe uma sequência de imagens amostradas. Controles são digitais (o eixo analógico acima de um limiar vira deflexão total) e podem ter atraso. Áudio permanece sem saída real. A prioridade é validar janela, entrada e fluxo gráfico sem regressões.

## Pré-requisitos

- Linux com sessão gráfica X11 ou Wayland.
- CMake, Clang/GCC, Ninja, submódulo SDK já presente.
- SDL3 3.2+ instalado ou código SDL3 já baixado em `build/linux/_deps/sdl3-src` (pelo `scripts/linux-tools.sh`).
- `assets/default.xbe` e os demais arquivos do seu disco legítimo.
- `.local/harness/game` compilado com a versão correta (`PGR1_RELEASE=2` para USA 1.0).

A compilação do preview é separada da compilação SDL3 **console/headless** do SDK, que desabilita X11/Wayland. Se o sistema ainda não tiver SDL3 dev, o CMake tenta compilar uma nova SDL3 GUI a partir dos fontes SDL3 já baixados pelo SDK. Isso não altera a configuração headless do SDK.

## Uso no terminal do VS Code (na raiz do projeto)

```sh
bash scripts/portable-gui.sh build
bash scripts/portable-gui.sh test
bash scripts/portable-gui.sh window
```

`window` abre uma janela de diagnóstico vazia, útil para validar SDL3 antes de iniciar o jogo. Feche com ESC.

Iniciar o jogo e exibir amostras visuais:

```sh
bash scripts/portable-gui.sh run
```

Modificar a frequência de captura: 1 a cada 60 frames (mais leve) ou 1 a cada 15 (mais amostras, CPU mais lenta):

```sh
PGR1_REF=60 bash scripts/portable-gui.sh run
PGR1_REF=15 bash scripts/portable-gui.sh run
```

## Controles (ponte para `pad_script.cpp`)

| Teclado | Xbox (script do harness) |
|---|---|
| Setas ou W/A/S/D | direcional |
| Enter | Start |
| Backspace | Back |
| Z/J | A |
| X/K | B |
| C | X |
| V | Y |
| ESC | fecha o preview e termina o jogo |

Gamepad SDL3: direcional, botões south/east/west/north, Start, Back, gatilhos e analógicos mapeados para tokens do script. Analógicos têm limiar (não há conversão proporcional nesta etapa).

## Diagnóstico

Segundo terminal do VS Code:

```sh
tail -f .local/gui-game.out
ls -lh .local/run/gui/cap/ref_*.tga | tail
find .local/run/gui/logs -type f -maxdepth 1
```

Se a janela abrir mas ficar só em "Waiting for reference frames":

- Verifique primeiro `tail -n 60 .local/gui-game.out` e o log de jogo em `.local/run/gui/logs/`.
- Confirme a criação das imagens em `.local/run/gui/cap/`.
- Se nenhuma imagem for produzida, o problema pode ser o boot, o tempo até a primeira apresentação do jogo ou um erro no renderizador de referência.
- Em caso de erro de CMake, cole o trecho do `cmake -S portable/linux/preview ...` que falhou.

Os dados do jogo permanecem em `assets/` e nunca devem ser enviados ao Git. `.local/` e `out/` já fazem parte de `.gitignore` do projeto.

## Próxima etapa

Trocar o trajeto `guest GPU -> referência CPU -> disco TGA -> SDL3` por
`guest GPU -> gpu::Backend portátil acelerado (Vulkan ou OpenGL) -> janela SDL3`.
A API `gpu::Backend` e os estados do SDK serão reaproveitados.
