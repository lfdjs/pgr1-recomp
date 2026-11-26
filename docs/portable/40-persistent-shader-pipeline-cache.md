# 04D.22C — Persistent shader/pipeline cache / Cache persistente

PT-BR: 04D.22B: 3774 apresentações, 4716 submissões, 2.319.248 hits de
descritores e 613.291 alocações (79,09% hits), 3.471.585 memcmp,
381 mudanças, zero draws rejeitadas. Região 2642..3774: 20,36 FPS.
Região 2762..3241: 22,32 FPS, capture 38,50 ms, texture 19,71 ms,
report 0,05 ms. Região 3242..3601: 16,33 FPS, capture 33,30 ms,
report 18,91 ms. Frame 2733: 680,772 ms, shader 626,717 ms,
report 639,642 ms, GPU 6,964 ms. Tempos aninhados, cena solar sem
confirmação visual. Shader stalls não explicam toda a lentidão.

Já existiam workers e cache SPIR-V por diretório de execução; o pipeline
cache Vulkan era vazio a cada processo. PGR1_VK_CACHE_DIR agora permite
compartilhar SPIR-V e persistir pipeline cache v1 por vendor/device/UUID
e layout de descritores. Cabeçalhos incompatíveis/truncados e arquivos
maiores que 64 MiB são descartados. Falha de importação tenta cache vazio.
Gravação no encerramento normal por temporário único e rename atômico.
Não há merge entre processos; a última gravação bem-sucedida prevalece.
Não há shader substituto, supressão de reflexos ou alteração de reports.
Logs de espera de future e criação de pipeline acima de 10 ms separam
as duas causas hoje agregadas em shader_ms. A opção é explícita para
preservar benchmarks cold/warm e testes antigos.

Referências: https://docs.vulkan.org/guide/latest/pipeline_cache.html
e https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheHeaderVersionOne.html

EN: Opt-in shared SPIR-V and driver pipeline cache, with device header
validation, bounded reads and atomic replacement on normal shutdown.
Separate slow future-wait and pipeline-creation logs. No approximate
shaders or visibility results. This addresses stutter, not sustained
capture costs. SDK unchanged. Integrated build and hardware validation
pending. Cold and warm runs must share the same explicit cache directory
and graphics settings; do not mix them with isolated cold baselines.

Test helper independently:
```sh
c++ -std=c++20 -Wall -Wextra -Werror portable/linux/vulkan/test_pipeline_cache_file.cpp -o /tmp/pgr1-cache-test
/tmp/pgr1-cache-test
```
