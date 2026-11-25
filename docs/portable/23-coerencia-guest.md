# 04D.19A — Diagnóstico de teto e auditoria / Ceiling test and writer audit

## Português (Brasil)

Baseline d75e50e, SDK hk21x/XboxXBERecomp fixado em
97b1654736f744a3966b5dff3d49be6d61ac7244. 04D.18C: 30,39 FPS,
P95 70,004 ms; nos últimos 300 frames, 763.376 comparações, zero
mudanças de bytes detectadas em entradas existentes, 15,856 ms de
verificação e 56,517 ms de frame. GPU span máximo 14,575 ms na sessão.
Cenas não pareadas. Eliminar 15,856 ms hipoteticamente ainda deixaria
40,661 ms se todo esse custo estivesse no caminho crítico.

### Auditoria preliminar do SDK fixado (não exaustiva)

| Caminho | Evidência no SDK | Cobertura |
| --- | --- | --- |
| Stores escalares | tools/recomp/codegen.cpp: CodeGen::Write, Mem | Aberta |
| RMW atômico | tools/recomp/codegen_int.cpp: EmitInteger, atomic compare exchange | Aberta |
| x87/MMX/SIMD | tools/recomp/codegen_fp.cpp; runtime/cpu/x86_ops.h: x87_store80 | Aberta |
| REP MOVS/STOS | runtime/cpu/x86_ops.h: memmove/memcpy/memset e direção | Aberta |
| HLE direta | runtime/kernel/kernel.h: G, W16/32/64, HostPtr | Aberta |
| I/O | runtime/kernel/files.cpp: Transfer, fread/MountedDisc Read em HostPtr | Aberta |
| RTL/atômicas | runtime/kernel/rtl.cpp: memmove, memset, Interlocked* | Aberta |
| Alocação/zeragem/boot | runtime/kernel/memory.cpp; boot.cpp | Aberta |
| Rede, áudio, entrada | runtime/xdk/xnet.cpp, dsound.cpp, xapi.cpp | Aberta |
| Superfícies | runtime/gpu/texture.cpp: CopySurfaceMemory; portable/linux/gldirect/backend.cpp: WritePixels/Sync/SyncLate | Aberta |
| Aliases | runtime/kernel/memory.cpp: Init, duas views do mesmo memfd | Aberta |
| Concorrência | runtime/kernel/threads.cpp: StartThread/StartHostThread | Aberta |

DirtyEpoch é indexado pelo endereço de recurso em runtime/gpu/texture.cpp,
não por todas as escritas de todas as páginas. Não habilitar um fast path
seguro com base nele. Ainda falta revisar os caminhos de dispatch,
instruções geradas e hooks do jogo, incluindo ponteiros que escapam.

A prova isolada `python3 portable/tools/probe_guest_aliases.py` foi
executada com sucesso em Linux: mprotect em A não protege B; readv
para A read-only retorna EFAULT. Não generalizar para userfaultfd.
Uma solução baseada em proteção deve cobrir ambas as views e I/O,
sincronizar escritores e capturas, e medir faults/rearm. Preferência
de projeto: instrumentação de stores e HLE no SDK dedicado, páginas
físicas canônicas e fallback exato para cobertura desconhecida.

### Experimento de teto (INSEGURO)

`PGR1_VK_TEXTURE_DIAGNOSTIC=skip-exact` pula somente a comparação
completa em hits de descritor no cache de texturas CPU do Vulkan.
Mantém cold decodes, mudanças de descritor, snapshots owned e
caminhos de render target existentes. Pode manter pixels obsoletos,
inclusive dentro de um frame; não é uma otimização de produção.
Não use para jogar normalmente, avaliar correção ou comparar imagens.
Desative a sobreposição para o teste (o modo diagnóstico também não
cria o worker). Sem variável, a comparação exata segue obrigatória.
A coluna texture_diagnostic_skips marca os frames/submissões afetados.
O teste SDK existente ainda verifica mudanças diretas no modo exato;
um teste adicional demonstra deliberadamente os pixels obsoletos.

### A/B com mesmo percurso

Use a mesma resolução, FXAA, MSAA, limite, perfil de CPU, build, carro,
câmera e trecho de Londres. Preferir script de entrada idêntico se
disponível; mesmo assim confirmar a cena visualmente. Sem salvamentos
importantes no experimento. Não misturar os CSVs com resultados válidos.
Manter perf separado das medições de tempo, se possível.

```sh
python3 portable/tools/probe_guest_aliases.py
PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 bash scripts/portable-vulkan.sh build-game
bash scripts/portable-vulkan.sh test
env -u PGR1_VK_TEXTURE_DIAGNOSTIC PGR1_VK_TEXTURE_OVERLAP=0 PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 PGR1_VK_RUN_DIR=.local/benchmarks/04d19a-exact-rx580 bash scripts/portable-vulkan.sh run-game
PGR1_VK_TEXTURE_DIAGNOSTIC=skip-exact PGR1_VK_TEXTURE_OVERLAP=0 PGR1_VK_CPU_PROFILE=native PGR1_VK_GUEST_OPT=2 PGR1_VK_RUN_DIR=.local/benchmarks/04d19a-UNSAFE-rx580 bash scripts/portable-vulkan.sh run-game
for d in .local/benchmarks/04d19a-{exact,UNSAFE}-rx580; do
  python3 portable/tools/frame_report.py "$d" --target-fps 60 --window 120
  python3 portable/tools/texture_report.py "$d" --window 300
done
```

O diretório de execução pode conter preferências locais diferentes;
confirme os parâmetros nos dois antes do percurso. Escolha intervalos
equivalentes via --after-frame/--before-frame (limites exclusivos no
texture_report); último N frames sozinho não garante cena equivalente.
Ao final, volte a executar sem a variável de diagnóstico.

Próximo: usar perfil perf do binário real para medir assembly, cópias,
estados, alocações e código guest. A implementação do tracker só poderá
autorizar cache hits após cobertura dos escritores, sincronização,
auditoria contra memcmp e testes de mutações. Não trocar o submódulo agora.

## English

The pinned SDK exposes raw generated stores, locked RMW, x87 and REP
helpers, untracked HLE pointer writes, direct file I/O, allocation,
surface copies/readbacks and guest threads. The writer table above is
preliminary, not evidence of complete coverage. Both physical aliases
must map to canonical pages. The Linux fixture shows that mprotect of
one alias does not protect the other and readv into a read-only mapping
returns EFAULT. It does not establish userfaultfd behavior.

The explicit skip-exact environment option is an intentionally unsafe
performance ceiling test. It skips exact texture cache-hit validation,
can display stale textures, disables the overlap worker, and must never
be used as visual correctness evidence or production mode. The default
remains exact. New CSV counters distinguish unsafe comparisons omitted.
The standalone Linux alias probe passed; integrated C++/SDK build and
real hardware A/B remain unvalidated. No SDK pin change or FPS claim.
