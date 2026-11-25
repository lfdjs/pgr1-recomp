# 04D.19E — Shadow journal prediction / Previsão em sombra do diário

## PT-BR

04D.19D reportou 3.645 frames, 4.259.571 memcmp, 155 alterações de
bytes e 155 falsos negativos informativos do tracker cooperativo.
Os 155 logs agrupam 122 em 839DD000 (640x480, fonte 4) e 33 em cinco
texturas 256x256 (fonte 5). A última família de página é indício,
não prova causal. Nos últimos 300 frames, 782.486 comparações e
nenhuma alteração detectada; check 16,294 ms, capture 39,042 ms,
frame 62,183 ms. Não há cobertura comprovada.

Esta etapa compara a previsão combinada do tracker e do diário SDK
com o resultado exato. Cada intervalo físico canônico recebe uma
soma checada das sequências monotônicas das páginas. Não é hash;
saturação, overflow, endereço fora de cobertura, stamp ausente ou
registro ausente tornam a previsão desconhecida. Uma mudança no
tracker ou nas sequências indica dirty. A segunda leitura após a
comparação/decodificação invalida a publicação do stamp se houve
mudança observável. Metadados de página não sincronizam bytes.
A notificação do SDK ainda ocorre após a escrita, não cobre todos
os escritores nem substitui exclusão de leitores. A soma de leituras
atômicas não é um snapshot atômico de todas as páginas. Por isso
**nenhuma comparação é omitida**, e as entradas continuam Unknown.

Ative com o SDK já instrumentado pela 04D.19D:
```sh
PGR1_VK_SDK_WRITE_AUDIT=ON PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 \
PGR1_VK_BUILD_DIR=out/pgr1-vulkan-write-audit JOBS=4 \
bash scripts/portable-vulkan.sh build-game

env -u PGR1_VK_TEXTURE_DIAGNOSTIC \
PGR1_VK_SDK_WRITE_AUDIT=ON PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 \
PGR1_VK_BUILD_DIR=out/pgr1-vulkan-write-audit \
PGR1_VK_TEXTURE_COHERENCY=audit PGR1_VK_TEXTURE_JOURNAL_AUDIT=1 \
PGR1_VK_TEXTURE_MISS_TRACE=1 PGR1_VK_TEXTURE_OVERLAP=0 \
PGR1_VK_RUN_DIR=.local/benchmarks/04d19e-journal-rx580 \
bash scripts/portable-vulkan.sh run-game

python3 portable/tools/texture_report.py .local/benchmarks/04d19e-journal-rx580 --window 300
```

Não reaplique o patch 04D.19D nem altere o pin. Novas colunas append-only:
clean, dirty, unknown, false negatives, false positives, unstable e
journal_ms. Candidatas limpas são previsões, nunca comparações evitadas
por prova. Instabilidade inclui amostras diferentes durante o processamento
e pode ocorrer também no primeiro bind. O tempo de consulta antes da
comparação está aninhado em texture_check; a segunda leitura ocorre
após a decodificação. O custo do escritor SDK não está incluso em
journal_ms. A configuração padrão não muda.

O helper e os testes isolados passaram GCC C++20 O2 com warnings estritos
e UBSan. Integração Vulkan, relatórios completos e jogo dependem de validação
no checkout real; sem alegação de ganho de FPS.

## EN

The journal shadow mode combines cooperative tracker invalidations and
checked sums of per-page monotonic SDK counters, with a second sample
after exact snapshot processing. Unknown ranges, missing stamps,
saturation and overflow are not candidates. Page counters are advisory:
post-store instrumentation, incomplete coverage and non-atomic multi-page
sampling cannot prove safe byte reuse. All exact comparisons remain.
Report candidate clean, dirty, unknown, advisory errors, unstable samples
and sampling time separately from proven hits. Default exact and SDK
pin are unchanged. Standalone helper strict O2 and UBSan tests passed;
full integrated build/gameplay validation remains pending.
