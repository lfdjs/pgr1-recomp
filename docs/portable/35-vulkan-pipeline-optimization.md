# 04D.21 — Cache de stages / Shader-stage cache

## Português (Brasil)

**IMPLEMENTADO:** cache de stages SPIR-V por processo antes da transformação de
HLSL e do acesso ao cache em disco. Um vertex shader idêntico usado por vários
pares vertex/pixel agora compartilha o mesmo resultado e execução em andamento.
Isso elimina regex de mapeamento da ABI, locks de arquivo e releituras repetidas.
Os shaders e flags enviados ao compilador permanecem iguais.

A chave contém texto HLSL completo, estágio e diretório absoluto do cache em
disco derivado do diretório de trabalho. Não depende somente de hash. O cache
retém até 1.024 entradas e 32 MiB de texto de origem mais nomes de diretório;
fontes novas além desse limite usam o caminho original sem cache em memória.
Resultados binários também são retidos por entrada; seu tamanho depende do
compilador. O limite não representa um teto absoluto da memória do processo.

Um mutex protege apenas o índice. O primeiro chamador compila fora do mutex;
os demais aguardam seu shared_future. Fontes diferentes continuam compilando
simultaneamente. O dono executa o compilador diretamente na thread que já
está processando o par; não enfileira outro job do qual os workers dependam.
Falhas propagam a mesma exceção e removem a entrada, permitindo nova tentativa.
O cache de disco continua verificando a fonte completa e a assinatura SPIR-V.

O teste `vk_shader_stage_single_flight` verifica 12 chamadores concorrentes da
mesma fonte, compilação simultânea de fontes distintas, separação por stage,
diretório e diferença textual, nova tentativa após falha e fallback nos limites
de entradas/bytes. O mesmo teste compila um vertex shader com glslang, remove
o cache de disco, verifica reuso em memória sem recriar arquivos e confere que
um novo diretório de trabalho cria um namespace independente. Os testes de
pixels também exercitam a integração. Os resultados finais de build, testes e desempenho estão
no [relatório de validação](36-road-to-60fps-validation.md); nenhuma meta de FPS
é deduzida de um cache hit isolado.

**TESTADO nesta sessão:** helper C++20 `-O2 -Wall -Wextra -Wpedantic -Werror`,
ASan/UBSan do helper e teste completo com compilador real sob ThreadSanitizer
(`-O1 -g -fsanitize=thread`, `TSAN_OPTIONS=halt_on_error=1`) passaram.
O log TSan local está em `.local/validation/04d21/shader-stage-tsan.log`;
não foram observados diagnósticos de corrida nessa execução.

## English

Implemented: a process-local exact-source, shader-stage and absolute cache-path
single-flight SPIR-V cache before ABI regex lowering and filesystem work.
Repeated vertex stages across different pairs reuse the actual binary future.
Compiler inputs, flags and disk-cache validation remain unchanged.

Owners compile directly outside the index mutex, independent sources can run
concurrently, and failed entries are removed for retry while existing waiters
receive their exception. No dependent work is queued to the same worker pool.
Retention is bounded to 1,024 entries / 32 MiB of source and namespace strings;
new sources above the limit use the original uncached path. Binary retention is
per entry and compiler-dependent, so this is not an absolute process-memory cap.
Tests cover concurrent coalescing, independent progress, exact identity,
namespace separation, failure retry and capacity overflow. An actual glslang
compile verifies reuse after deleting the disk cache and independent cache
creation after changing the working directory. See the validation
report for final execution results and measured performance.

Executed successfully this session: strict C++20 O2 helper, ASan/UBSan helper,
and the actual-compiler test with ThreadSanitizer O1/debug and
`TSAN_OPTIONS=halt_on_error=1`. No race diagnostics were reported in that run;
the local log is `.local/validation/04d21/shader-stage-tsan.log`.
