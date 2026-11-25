#pragma once
#include "../../include/pgr/memory/guest_memory_tracker.h"
#include <cstring>

namespace pgr::gldirect {
enum class TextureCoherency { Exact, Audit, Tracked };
inline TextureCoherency ParseTextureCoherency(const char* value) {
    if(value && std::strcmp(value,"audit")==0) return TextureCoherency::Audit;
    if(value && std::strcmp(value,"tracked")==0) return TextureCoherency::Tracked;
    return TextureCoherency::Exact;
}
struct TextureAuditResult { bool false_positive=false, false_negative=false; };
// EN: Prediction is advisory even for unknown coverage; only CanReuse() may
//     authorize skipping bytes. Audit always renders the exact-byte result.
// PT-BR: Previsão é informativa inclusive sem cobertura; só CanReuse() pode
//        autorizar omitir bytes. Audit sempre renderiza o resultado exato.
inline TextureAuditResult AuditTexturePrediction(memory::GuestMemoryTracker::Prediction prediction,
                                                 bool exact_changed) {
    return {prediction.registered && !prediction.advisory_unchanged && !exact_changed,
            prediction.registered && prediction.advisory_unchanged && exact_changed};
}
} // namespace pgr::gldirect
