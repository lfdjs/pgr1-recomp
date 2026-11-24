# 04D.17 — Cadência de submissões / Submission pacing

## Português (Brasil)

A RTX 3060 Ti registrou 36,31 FPS médios após 04D.16, com P95 de GPU
13,251 ms e P99 de 15,662 ms. Ainda houve 80,6% de intervalos acima
de 16,667 ms e variação observada de 20 a 45 FPS na corrida.
O último grupo de 300 frames teve captura média de 19,787 ms.
Os CSVs não identificam cenas automaticamente.

A thread Vulkan agora aguarda trabalho com uma variável de condição
por até 1 ms quando ociosa, sendo notificada imediatamente ao chegar
uma submissão. A espera continua limitada para que fences, SDL e
eventos sejam processados. A fila FIFO e seu limite permanecem intactos.
O produtor registra cada recurso uma vez por geração do prefixo,
preservando a mesma promessa de conclusão, sem alterar a semântica
de Busy/WaitResource ou a validação de bytes de textura.

Para analisar toda a corrida, use intervalos visualmente identificados
e a nova opção de relatório por janelas:

```bash
python3 portable/tools/frame_report.py .local/run/vulkan --target-fps 60 --window 120
```

Cada janela utiliza apresentações efetivas, não ticks do jogo, e
informa FPS, P95, máximo e porcentagem acima do orçamento +1 ms.
Não é uma garantia de ganho de FPS. Valide também os testes Vulkan
assíncrono e síncrono, pixels e áudio.

## English

The RTX 3060 Ti run averaged 36.31 FPS, GPU P95 13.251 ms and
P99 15.662 ms, yet 80.6% of presentation intervals exceeded 16.667 ms.
Observed race FPS fluctuated between 20 and 45. Scene ranges require
visual confirmation.

The idle Vulkan consumer now uses a bounded condition-variable wait
(up to 1 ms), waking on incoming work while retaining periodic fence
polling. FIFO order and capacity are unchanged. The producer records
a resource completion token once per submission generation, avoiding
repeated shared-future assignment within that prefix. Busy/WaitResource,
texture byte equality, audio and graphics semantics are preserved.

Use frame_report.py with --window 120 to locate slow presentation
windows, then compare matching race sections. The hardware tests and
performance impact remain to be validated.
