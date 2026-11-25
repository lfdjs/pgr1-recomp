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
