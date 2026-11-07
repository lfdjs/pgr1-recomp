# Etapa 01 — Linux headless baseline e contratos do SDK

## O que está implementado

- CLI `scripts/portable-dev.sh` para diagnosticar dependências, compilar os testes do host e acionar as ferramentas
  de recompilação/harness **já existentes no SDK**.
- Validação de SHA-1 de `assets/default.xbe` para as três revisões de PGR1 documentadas pelo projeto.
- Um teste de contrato do SDK que compila as interfaces reais de `runtime/cpu/x86_context.h`,
  `runtime/kernel/memory.h`, `runtime/gpu/backend.h` e instancia um backend sem GPU para validar o contrato.
  Não modifica o SDK; esse teste só aparece quando o submódulo está disponível.
- O Windows continua usando o projeto original. O flag `PGR1_PORTABLE_HOST_TESTS` oferece uma compilação independente
  dos arquivos proprietários do jogo.

## Primeiras verificações (sem arquivos do jogo)

```bash
bash scripts/portable-dev.sh doctor
bash scripts/portable-dev.sh test
```

Se o SDK não estiver disponível:

```bash
git submodule update --init --recursive
bash scripts/portable-dev.sh test
```

O segundo teste do CTest (SDK contract tests) só será adicionado se `sdk/runtime/gpu/backend.h` existir no checkout.

## Pipeline headless completo (Linux, requer o jogo original)

Colocar `assets/default.xbe` e os demais arquivos do seu disco em `assets/`, sem versioná-los.
A ferramenta de recompilação exige uma das três versões reconhecidas.

```bash
bash scripts/portable-dev.sh linux-tools
bash scripts/portable-dev.sh recompile
bash scripts/portable-dev.sh smoke 240
```

Ou executar todas as operações numa só linha:

```bash
bash scripts/portable-dev.sh all
```

**Importante:** o SDK procura uma cópia completa dos arquivos do jogo na pasta ou um ISO válido para execução.
O `default.xbe` é usado para a recompilação. A primeira execução pode preparar cache e demorar mais.
O teste imprime `SMOKE OK` quando o jogo avançou de boot para uma corrida sem traps detectados.

## Próximo passo de implementação

Agora que os contratos de CPU, memória e renderer foram isolados em testes, o próximo desenvolvimento é migrar o
bootstrap Win32 para uma entrada Linux SDL3, com áudio SDL3 e implementação real de `gpu::Backend` para a GPU local.
A GPU ainda depende de HLSL/D3D; não há renderer Linux funcional neste marco.

## Restrições

- Não publique `assets/`, `generated/`, capturas ou artefatos derivados do XBE.
- Não altere `.gitmodules` nem avance o SHA do SDK sem fork e validação de smoke test.
- Nunca habilite `PGR1_PORTABLE_HOST_TESTS` na build normal do jogo. É um modo independente de testes.
