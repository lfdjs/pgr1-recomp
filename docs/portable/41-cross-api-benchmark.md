# 04D.22D — Cross-API benchmark / Comparação de APIs

## PT-BR

O cache Vulkan 04D.22C carregou 86.148 bytes na corrida warm, salvou
86.144 e registrou uma espera SPIR-V de 17,233 ms acima do limiar.
Isso não é uma medição do ganho sustentado de FPS.

O renderer OpenGL antigo publicava zero em GetReport, não comparável ao
Vulkan preciso nas cenas solares. PGR1_GL_REAL_VISIBILITY=1 usa contagens
GL_SAMPLES_PASSED por draw, resultados pendentes limitados a 4096,
marcador ordenado e glFinish quando o guest exige report. Não é uma
otimização assíncrona definitiva. Exige alvos contados sem escala e pelo
menos 32 bits de contador; aborta o caminho em caso contrário. O modo
histórico continua por padrão, não vale como prova de paridade.
Tests: fixture EGL cobre contagens 1024/0/64 em prefixo separado.
Não executado neste ambiente remoto.

O OpenGL agora registra intervalos entre swaps bem-sucedidos e contadores
de textura/geometria e reports no frames.csv (PGR1_GL_SYNC_PROFILE=1).
Perfis CPU e -O guest reutilizam o helper Vulkan, com defaults anteriores
generic/O1. Use native/O2 em ambas as APIs; verifique compilador e SDK.
O backend GL não tem toda a paridade de áudio/FXAA/MSAA/pipeline do Vulkan.
Use 640x480, janela sem escala HiDPI, MSAA e FXAA desligados nas referências,
mesmo carro/pista/trecho, cache aquecido, limite de 60, textura cache 1024.
Não comparar menus ou afirmar corrida completa via script curto.
Não misturar diretórios HDD ativos; copie um seed limpo para diretórios
novos, preservando os saves originais. Verifique a GPU e cenas visualmente.

```sh
PGR1_GL_CPU_PROFILE=native PGR1_GL_GUEST_OPT=2 JOBS=4 \
PGR1_BUILD_DIR=out/pgr1-gl-native-o2 bash scripts/portable-linux.sh build
bash scripts/portable-gldirect.sh test
env -u SCRIPT -u MESA_LOADER_DRIVER_OVERRIDE -u GALLIUM_DRIVER -u PGR1_EXPECT_ZINK \
PGR1_GL_CPU_PROFILE=native PGR1_GL_GUEST_OPT=2 PGR1_BUILD_DIR=out/pgr1-gl-native-o2 \
PGR1_GL_REAL_VISIBILITY=1 PGR1_GL_SYNC_PROFILE=1 PGR1_VK_TEXTURE_CACHE_ENTRIES=1024 \
OUTPUT=640x480 PGR1_FULLSCREEN=0 PGR1_SCRIPT_ONLY=0 PGR1_FPS=60 \
PGR1_RUN_DIR=.local/benchmarks/04d22d-gl-london bash scripts/portable-linux.sh run
```

Para Zink, altere o diretório e use run-vulkan no script acima. Verifique
radeonsi para GL nativo e zink/RADV para Zink. PGR1_GL_REAL_VISIBILITY=0
é somente controle histórico de visibilidade sem paridade.

Windows: use uma instalação Windows já preparada legalmente (PGR1.exe,
PGR1.dll e dependências/arquivos originais). Adicione o exe à Steam como
jogo não Steam, selecione Proton compatível e use F1 > Graphics > Renderer
(D3D11/D3D12; reinicie se solicitado). DXVK e VKD3D-Proton são traduções
para Vulkan, não backends nativos. Use diretório de jogo e HDD separados,
não altere o checkout/SDK atual; registre versão do Proton e driver.
MangoHud pode gravar telemetria externa; XBE_PERF=1 oferece logs do runtime
Windows. WineD3D (PROTON_USE_WINED3D=1) é comparação opcional D3D11->GL,
não se aplica ao D3D12. Se a versão Windows não iniciar, registre o erro;
não assuma compatibilidade. Não há validação Proton neste commit.

## EN

Warm Vulkan cache I/O was confirmed, not a sustained FPS gain. Legacy
OpenGL returned zero visibility; the opt-in real GL query path queues
bounded GL_SAMPLES_PASSED results and resolves an ordered report marker
with glFinish. Only unscaled counted targets are accepted. This is a
correctness-oriented comparison, not a claim of asynchronous guest
visibility or visual parity. The GL CSV now includes successful swap
intervals and shared capture/visibility counters. CPU profile helper is
shared with Vulkan; generic/O1 remains the GL default. Use matched native/O2,
1024 texture entries and disabled post-AA for comparison. Verify identical
scene, guest resolution, hardware, warmed caches and real query mode.
The Windows executable under Proton provides a separate architecture
reference (DXVK D3D11, VKD3D-Proton D3D12); it does not replace the portable
runtime. Integrated build, EGL tests and hardware/Proton validation pending.
