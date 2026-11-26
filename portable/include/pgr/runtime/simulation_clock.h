#pragma once
#include <atomic>
#include <cstdint>
namespace pgr::runtime {
// EN: Completed title update ticks, never presentation frames. Diagnostic
// scene correlation only; this counter does not change the scheduler or rate.
// PT-BR: Ticks de atualização concluídos, nunca frames apresentados. Apenas
// correlação de cenas; o contador não altera o escalonamento nem a velocidade.
inline std::atomic<std::uint64_t> simulation_ticks{0};
}
