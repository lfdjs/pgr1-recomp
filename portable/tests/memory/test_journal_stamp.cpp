#include "journal_stamp.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace pgr::memory;
int main() {
    constexpr uint32_t page=4096, size=page*4;
    std::array<uint64_t,4> seq{1,2,3,4};
    auto read=[&](uint32_t off){return seq.at(off/page);};
    auto stamp=[&](uint32_t off,uint32_t bytes){return JournalStamp(off,bytes,page,size,read);};
    assert(stamp(page-1,2)==3);
    auto prior=stamp(page,2*page);
    assert(prior==5);
    assert(PredictJournal(prior,stamp(page,2*page),true,true)==JournalPrediction::Clean);
    ++seq[0]; assert(stamp(page,2*page)==prior);
    ++seq[2]; auto now=stamp(page,2*page);
    assert(PredictJournal(prior,now,true,true)==JournalPrediction::Dirty);
    assert(PredictJournal(now,now,true,false)==JournalPrediction::Dirty);
    assert(PredictJournal(now,now,false,true)==JournalPrediction::Unknown);
    assert(!StableJournalStamp(prior,now)); assert(StableJournalStamp(now,now)==now);
    assert(!stamp(0,0) && !stamp(size,1) && !stamp(size-1,2));
    seq[1]=UINT64_MAX>>8; assert(!stamp(page,1));
    assert(PredictJournal(prior,stamp(page,1),true,true)==JournalPrediction::Unknown);
    // EN/PT-BR: A sum that would overflow must be unknown / soma com overflow é desconhecida.
    assert(!JournalStamp(0,512,1,512,[](uint32_t){return (UINT64_MAX>>8)-1;}));
    std::puts("Journal stamp tests passed / testes de stamp aprovados");
}
