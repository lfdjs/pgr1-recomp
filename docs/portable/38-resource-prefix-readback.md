# 04D.22A — Resource prefixes and readback / Prefixos de recursos e readback

## PT-BR

Base 724e96e. O Codex publicou o patch cumulativo do SDK e alterações no
runtime, geometria, queries e shaders. O gitlink acidentalmente apontava
para 068dc1008f58d8a5f2aabbd3ee926affe1d17f3d, não encontrado no fork
lfdjs nem no upstream hk21x. Restauramos o pin público 97b1654; o checkout
local já modificado e o patch cumulativo devem ser preservados. Não execute
submodule update sobre o SDK local. Instalações limpas aplicam somente o
patch cumulativo (portable/patches/README.md).

Mudanças Vulkan/captura:
- Sync(data,0) usa find em vez de percorrer todos os targets; Sync com
  tamanho mantém o percurso ordenado e teste de sobreposição original.
- Busy/WaitResource submetem o prefixo atual somente se o último uso do
  recurso pertencer a ele. Um token de geração anterior já foi entregue à
  FIFO; observar sua conclusão não precisa fragmentar draws alheios.
  Fence, Flush, GetReport e readback mantêm sua ordem e seus waits.
- GetReport mantém a soma saturada de resultados já resolvidos até
  ClearReport e descarta os futures consumidos. Continua aguardando a
  conclusão ordenada e resultados reais, inclusive reports vazios.
- Readback Vulkan linear de 32 bits copia linhas com memcpy, preservando
  pitch/padding, orientação, guarda do tracker e notificação diagnóstica.
  Formatos 16 bits, swizzled e OpenGL conservam o caminho por pixel.
- Testes de regressão cobrem polling de recurso anterior com draw alheio,
  report cumulativo, readback com pitch e range não nulo.

A queda no trecho solar ainda não está atribuída causalmente. O perfil
sync_report da etapa anterior é necessário para medir o ganho destas
mudanças. Nenhum fast hit de textura foi habilitado; o diário segue
informativo. Não há alegação de ganho de FPS nem de testes integrados
executados nesta intervenção remota.

## EN

Restore the publicly accessible SDK gitlink without modifying local SDK
work; the cumulative patch remains the reproducible source. Exact resource
Sync uses a map lookup; range Sync retains ordered overlap handling.
Resource busy/wait only flushes when the resource's latest use belongs to
the current unsubmitted prefix. Previously submitted tokens already own
their completion. GetReport retains a saturated cumulative total until
ClearReport, discards resolved futures, and still waits for ordered real
results. Linear 32-bit Vulkan readback uses pitch-preserving row copies
under the existing tracker guard and reports advisory writes. Other formats
and legacy GL retain pixel conversion. Regression tests added; integrated
build and hardware benchmarks are pending. No unsafe texture reuse.
