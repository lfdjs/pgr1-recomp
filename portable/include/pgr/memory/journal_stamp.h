#pragma once
#include <cstdint>
#include <limits>
#include <optional>

namespace pgr::memory {
// EN: Advisory sum of monotonic page sequences; no hashing, no byte-access proof.
// PT-BR: Soma informativa de sequências monotônicas; sem hash nem prova de acesso aos bytes.
template<class ReadSequence>
std::optional<uint64_t> JournalStamp(uint32_t offset,uint32_t bytes,uint32_t page_bytes,
                                     uint32_t physical_bytes,ReadSequence read) {
    if(!bytes || !page_bytes || offset>=physical_bytes || bytes>physical_bytes-offset) return {};
    const uint64_t first=offset/page_bytes,last=(uint64_t(offset)+bytes-1)/page_bytes;
    constexpr uint64_t saturated=UINT64_MAX>>8;
    uint64_t total=0;
    for(uint64_t p=first;p<=last;++p) {
        const uint64_t seq=read(uint32_t(p*page_bytes));
        if(seq>=saturated || seq>UINT64_MAX-total)return {};
        total+=seq;
    }
    return total;
}
enum class JournalPrediction { Unknown, Clean, Dirty };
// EN: The cooperative tracker still supplies known invalidations; neither source proves coverage.
// PT-BR: O tracker cooperativo ainda fornece invalidações conhecidas; nenhum prova cobertura.
inline JournalPrediction PredictJournal(std::optional<uint64_t> prior,std::optional<uint64_t> now,
                                        bool registered,bool tracker_unchanged) {
    if(!prior || !now || !registered)return JournalPrediction::Unknown;
    return *prior==*now && tracker_unchanged ? JournalPrediction::Clean : JournalPrediction::Dirty;
}
inline std::optional<uint64_t> StableJournalStamp(std::optional<uint64_t> before,
                                                  std::optional<uint64_t> after) {
    return before==after?before:std::nullopt;
}
} // namespace pgr::memory
