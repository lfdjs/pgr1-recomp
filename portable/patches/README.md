# SDK patches / Patches do SDK

## PT-BR

Base pública: `97b1654736f744a3966b5dff3d49be6d61ac7244`.

- `sdk-97b1654-physical-write-audit.patch`: etapa histórica 04D.19D.
- `sdk-97b1654-performance-overhaul.patch`: cumulativo, inclui a etapa anterior,
  otimizações REP, correções SIMD/m64, eventos de alloc/free/reuse e montagem
  de geometria desta sessão. **Aplicar somente este patch em um checkout limpo.**

O checkout local do usuário já contém os patches; não reaplicar. Preserve o
gitlink no pin público enquanto o commit local do SDK não estiver publicado
no fork do usuário. Nenhum push ao upstream está autorizado por este arquivo.

```sh
git -C sdk status --short
git -C sdk rev-parse HEAD
# Somente em outra cópia limpa, no pin público:
git -C /path/to/clean-sdk apply --check /path/to/pgr1/portable/patches/sdk-97b1654-performance-overhaul.patch
git -C /path/to/clean-sdk apply /path/to/pgr1/portable/patches/sdk-97b1654-performance-overhaul.patch
```

Não aplicar o patch novo por cima do histórico. Para atualizar um checkout
parcialmente modificado, comparar as mudanças preservando o trabalho local.
Depois reconstruir `xbe_recomp`, regenerar o jogo e compilar o perfil escolhido.
Sem `XBE_PHYSICAL_WRITE_AUDIT`, as notificações diagnósticas são removidas;
as melhorias REP, SIMD/m64 e geometria continuam ativas.

Consulte [estado de memória/testes](../../docs/portable/31-memory-coherency-production.md)
e [geometria](../../docs/portable/33-geometry-capture-v2.md).

## EN

Both patches target the public pin above. The performance-overhaul patch is
cumulative and replaces the old audit patch when starting from a clean SDK.
Apply it once; never stack the two or overwrite an existing modified checkout.
The local SDK already contains the changes. Keep the public parent gitlink
until the local SDK commit is accessible from the user's fork. Rebuild the
recompiler, regenerate the game and build the selected profile. Diagnostic
notifications compile out without the audit flag; production optimizations
remain active. See the linked documents for tests and coverage limits.
