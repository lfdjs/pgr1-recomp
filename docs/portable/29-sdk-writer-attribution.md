# 04D.19D — SDK writer attribution / Atribuição de escritores do SDK

PT-BR: O Codex entregou o tracker cooperativo e o modo audit, mas todas as
entradas guest continuam Unknown. O smoke final reportou 18 falsos negativos
informativos, zero fast hits e 4.006.982 memcmp. O fork do usuário
lfdjs/XboxXBERecomp contém o pin 97b1654736f744a3966b5dff3d49be6d61ac7244,
mas a integração GitHub recusou criação de branch com HTTP 403.
O patch neste repositório é uma alteração independente aplicável ao pin;
não altera o gitlink nem concede cobertura cooperativa.

A opção de compilação XBE_PHYSICAL_WRITE_AUDIT habilita um diário de
última família instrumentada por página física, com contador atômico
por página e aliases canonicalizados. Sem a definição as macros são no-op.
O diário não protege memória, não sincroniza writers/readers, não garante
causalidade e não autoriza pular memcmp. O contador inclui tentativas de
escrita, inclusive valores iguais e operações condicionais conservadoras.
Os caminhos instrumentados incluem CodeGen::Write, stack, atomic RMW
selecionados, bit strings, x87/SSE/MMX selecionados, REP e MOVS/STOS,
W16/32/64, transferências de arquivos e RTL de bloco selecionadas.
G, HostPtr que escapam, outros HLE, GPU authority, remap e hooks continuam
abertos. O patch foi gerado contra os arquivos exatos do pin; a build real
do recompilador e o teste integrado não foram executados nesta sessão.

EN: This is diagnostic instrumentation only. No production skip is
authorized. Last writer family is a hint, not proof of the writer of a
changed byte. A dedicated SDK branch should be created locally before
applying the patch; do not push to third-party upstream. Keep the main
project pin until the patched SDK is validated and published. Build and
regenerate the game using the patched recompiler; enable the build flag
for both runtime and generated objects. See the follow-up instructions.

SDK standalone test after applying the patch:

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -pthread \
  sdk/tests/physical_write_audit_test.cpp -o /tmp/pgr1-sdk-audit-test
/tmp/pgr1-sdk-audit-test
```

## Workflow / Fluxo

Start with a clean SDK checkout on the documented pin. / Comece com SDK
limpo no pin documentado. Do not overwrite local changes. / Não sobrescreva
alterações locais.

```sh
git -C sdk status --short
git -C sdk rev-parse HEAD
git -C sdk switch -c pgr1-memory-audit
git -C sdk apply --check ../portable/patches/sdk-97b1654-physical-write-audit.patch
git -C sdk apply ../portable/patches/sdk-97b1654-physical-write-audit.patch
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -pthread sdk/tests/physical_write_audit_test.cpp -o /tmp/pgr1-sdk-audit-test
/tmp/pgr1-sdk-audit-test
cmake --build build/linux --target xbe_recomp
bash scripts/portable-dev.sh recompile
PGR1_VK_SDK_WRITE_AUDIT=ON PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 JOBS=4 PGR1_VK_BUILD_DIR=out/pgr1-vulkan-write-audit bash scripts/portable-vulkan.sh build-game
env -u PGR1_VK_TEXTURE_DIAGNOSTIC PGR1_VK_SDK_WRITE_AUDIT=ON PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 PGR1_VK_BUILD_DIR=out/pgr1-vulkan-write-audit PGR1_VK_TEXTURE_COHERENCY=audit PGR1_VK_TEXTURE_MISS_TRACE=1 PGR1_VK_TEXTURE_OVERLAP=0 PGR1_VK_RUN_DIR=.local/benchmarks/04d19d-writers-rx580 bash scripts/portable-vulkan.sh run-game
grep 'texture-miss:' .local/benchmarks/04d19d-writers-rx580/console.log
python3 portable/tools/texture_report.py .local/benchmarks/04d19d-writers-rx580 --window 300
```

The first differing byte's offset/physical address, descriptor, frame/draw,
latest instrumented writer family and page sequence are logged for up to
256 observed advisory misses. No raw texture bytes are logged. A second
comparison may observe different bytes if an uncovered writer is concurrent.
Source 0 means no recorded family (or journal not compiled), not proof that
no write occurred. Source 1 scalar, 2 stack, 3 atomic/bit operations, 4 FP/SIMD,
5 string/REP, 6 W helpers, 7 file I/O, 8 selected RTL. Sequence counts
instrumented events per physical page since process start, not exact stores
or bytes changed; the last family may concern unrelated bytes on the same page.
Do not compare diagnostic FPS with production: atomic journal cost is extra.
The journal does not feed the existing tracker or establish coverage.
Run without the SDK patch using only PGR1_VK_TEXTURE_MISS_TRACE=1 and audit
to collect addresses (source/sequence 0). Default build and exact path are
unchanged; no unsafe fast path is enabled.

PT-BR: O log registra até 256 falhas informativas, com endereço/offset do
primeiro byte divergente e último evento instrumentado da página. Não há
dump de bytes do jogo. Fonte 0 não comprova ausência de escrita. A família
mais recente pode ser de outro byte da mesma página e não identifica
necessariamente a causa. O diário não alimenta o tracker nem autoriza
fast hits; a sobrecarga do diagnóstico deve ficar fora dos benchmarks.
Com SDK original, use apenas audit e PGR1_VK_TEXTURE_MISS_TRACE=1 para
obter endereços, com fonte/sequence zero. A configuração padrão segue exata.
