#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace pgr::memory {

struct PhysicalLayout {
    std::array<std::uint32_t,2> aliases;
    std::uint32_t bytes;
    std::uint32_t page_bytes;
};
struct PhysicalRange {
    std::uint32_t offset=0, bytes=0;
    constexpr bool operator==(const PhysicalRange&) const = default;
};

// EN: Only these two complete windows denote physical memory. Other guest VAs
//     need their own mapping authority; masking their high bits is incorrect.
// PT-BR: Só estas duas janelas completas denotam memória física. Outras VAs
//        exigem autoridade de mapeamento própria; mascarar bits altos é incorreto.
inline std::optional<PhysicalRange> Canonicalize(const PhysicalLayout& layout,
                                                std::uint32_t address, std::uint64_t bytes) {
    if(!bytes || bytes>layout.bytes) return {};
    for(auto base:layout.aliases) {
        if(address>=base) {
            const auto offset=std::uint64_t(address)-base;
            if(offset<layout.bytes && bytes<=layout.bytes-offset &&
               bytes<=(std::uint64_t(1)<<32)-address)
                return PhysicalRange{std::uint32_t(offset),std::uint32_t(bytes)};
        }
    }
    return {};
}

class GuestMemoryTracker {
public:
    using Handle=std::uint64_t;
    // EN: Cooperative is a caller contract: ALL byte access, aliases, I/O,
    //     mapping changes and external writers must share this domain's guards.
    //     A raw pointer or a lone notification never establishes this contract.
    // PT-BR: Cooperative é contrato do chamador: TODO acesso, aliases, I/O,
    //        remapeamento e escritores externos usam as guardas deste domínio.
    //        Um ponteiro bruto ou notificação isolada nunca prova o contrato.
    enum class Coverage { Unknown, Cooperative };
    struct Stamp { Handle handle=0; std::uint64_t generation=0, epoch=0, domain=0; };
    struct Prediction {
        bool registered=false, advisory_unchanged=false, covered=false;
        bool CanReuse() const { return registered && advisory_unchanged && covered; }
    };
    struct Stats {
        std::uint64_t monitored_writes=0, dirty_pages=0, tracked_bytes=0, write_ns=0;
    };

private:
    struct Entry {
        PhysicalRange range;
        Coverage coverage;
        std::uint64_t generation;
    };
    PhysicalLayout layout_;
    // EN: Stamps cannot migrate between domains, even if handles/epochs match.
    // PT-BR: Stamps não migram entre domínios, mesmo com handles/epochs iguais.
    inline static std::atomic<std::uint64_t> next_domain_{0};
    static std::uint64_t NewDomain() {
        auto previous=next_domain_.load(std::memory_order_relaxed);
        do {
            if(previous==std::numeric_limits<std::uint64_t>::max())return 0;
        } while(!next_domain_.compare_exchange_weak(previous,previous+1,std::memory_order_relaxed));
        return previous+1;
    }
    const std::uint64_t domain_=NewDomain();
    const std::size_t max_ranges_, max_page_links_;
    std::size_t page_links_=0;
    // EN: Active nodes, not retained vector capacities, obey the link budget.
    // PT-BR: Nós ativos, sem capacidades retidas de vetores, respeitam o limite.
    std::vector<std::list<Handle>> pages_;
    std::unordered_map<Handle,Entry> entries_;
    mutable std::shared_mutex mutex_;
    Handle next_handle_=0;
    std::uint64_t generation_=1, epoch_=1;
    bool exhausted_=false;
    Stats stats_;

    static bool Overlaps(PhysicalRange a,PhysicalRange b) {
        return std::uint64_t(a.offset)+a.bytes>b.offset &&
               std::uint64_t(b.offset)+b.bytes>a.offset;
    }
    std::size_t First(PhysicalRange r) const { return r.offset/layout_.page_bytes; }
    std::size_t Last(PhysicalRange r) const {
        return (std::uint64_t(r.offset)+r.bytes-1)/layout_.page_bytes;
    }
    void Advance() {
        if(generation_==std::numeric_limits<std::uint64_t>::max()) exhausted_=true;
        else ++generation_;
    }
    void Change(std::uint32_t address,std::uint64_t bytes,bool revoke) {
        if(entries_.empty() || !bytes) return;
        auto range=Canonicalize(layout_,address,bytes);
        if(!range) {
            // EN: Clip unsupported extents to both windows without arithmetic
            //     overflow; unrelated user/system writes do no resource work.
            // PT-BR: Recorte extensões desconhecidas nas duas janelas sem
            //        overflow; escritas user/system alheias não visitam recursos.
            const auto end=std::uint64_t(address)+std::min(bytes,(std::uint64_t(1)<<32)-address);
            for(auto base:layout_.aliases) {
                const auto begin=std::max(std::uint64_t(address),std::uint64_t(base));
                const auto clipped_end=std::min(end,std::uint64_t(base)+layout_.bytes);
                if(begin<clipped_end) Change(std::uint32_t(begin),clipped_end-begin,revoke);
            }
            return;
        }
        bool changed=false;
        for(auto page=First(*range);page<=Last(*range);++page) {
            bool dirty=false;
            for(auto id:pages_[page]) {
                auto& entry=entries_.at(id);
                if(!Overlaps(entry.range,*range)) continue;
                if(!changed) { Advance(); changed=true; }
                entry.generation=generation_;
                if(revoke) entry.coverage=Coverage::Unknown;
                dirty=true;
            }
            if(dirty && !revoke) ++stats_.dirty_pages;
        }
        if(changed && !revoke) ++stats_.monitored_writes;
    }
    Stamp Capture(Handle handle) const {
        const auto it=entries_.find(handle);
        return !exhausted_ && it!=entries_.end()?Stamp{handle,it->second.generation,epoch_,domain_}:Stamp{};
    }
    Prediction Query(Handle handle,Stamp stamp) const {
        const auto it=entries_.find(handle);
        if(exhausted_ || it==entries_.end()) return {};
        return {true,stamp.domain==domain_ && stamp.handle==handle && stamp.epoch==epoch_ &&
                     stamp.generation==it->second.generation,
                     it->second.coverage==Coverage::Cooperative};
    }
public:
    explicit GuestMemoryTracker(PhysicalLayout layout,std::size_t max_ranges=4096,
                                std::size_t max_page_links=262144)
        : layout_(layout),max_ranges_(max_ranges),max_page_links_(max_page_links) {
        exhausted_=domain_==0;
        if(!layout.bytes || !layout.page_bytes || (layout.page_bytes&(layout.page_bytes-1)) ||
           layout.bytes%layout.page_bytes ||
           std::uint64_t(layout.aliases[0])+layout.bytes>(std::uint64_t(1)<<32) ||
           std::uint64_t(layout.aliases[1])+layout.bytes>(std::uint64_t(1)<<32) ||
           (std::uint64_t(layout.aliases[0])<std::uint64_t(layout.aliases[1])+layout.bytes &&
            std::uint64_t(layout.aliases[1])<std::uint64_t(layout.aliases[0])+layout.bytes))
            throw std::invalid_argument("invalid guest physical layout");
    }
    GuestMemoryTracker(const GuestMemoryTracker&)=delete;
    GuestMemoryTracker& operator=(const GuestMemoryTracker&)=delete;

    Handle RegisterRange(std::uint32_t address,std::uint64_t bytes,
                         Coverage coverage=Coverage::Unknown) {
        std::unique_lock lock(mutex_);
        auto range=Canonicalize(layout_,address,bytes);
        if(exhausted_ || !range || entries_.size()>=max_ranges_ ||
           next_handle_==std::numeric_limits<Handle>::max()) return 0;
        const auto links=Last(*range)-First(*range)+1;
        if(links>max_page_links_-page_links_) return 0;
        if(pages_.empty()) pages_.resize(layout_.bytes/layout_.page_bytes);
        const auto id=++next_handle_;
        // EN: Stage allocations before publication; splice cannot throw.
        // PT-BR: Prepare alocações antes de publicar; splice não lança exceções.
        std::list<Handle> staged(links,id);
        entries_.emplace(id,Entry{*range,coverage,generation_});
        for(auto page=First(*range);page<=Last(*range);++page)
            pages_[page].splice(pages_[page].end(),staged,staged.begin());
        page_links_+=links; stats_.tracked_bytes+=range->bytes;
        return id;
    }
    void UnregisterRange(Handle handle) {
        std::unique_lock lock(mutex_);
        const auto it=entries_.find(handle);
        if(it==entries_.end()) return;
        auto range=it->second.range;
        for(auto page=First(range);page<=Last(range);++page) {
            auto& ids=pages_[page];
            ids.remove(handle); --page_links_;
        }
        stats_.tracked_bytes-=range.bytes; entries_.erase(it);
    }
    // EN: Revoke proof before free/remap/GPU authority changes. Re-registration
    //     requires a fresh caller contract; an old stamp cannot resurrect it.
    // PT-BR: Revogue a prova antes de liberar/remapear/trocar autoridade GPU.
    //        Novo registro exige novo contrato; stamps antigos não o recuperam.
    void InvalidateRange(std::uint32_t address,std::uint64_t bytes) {
        std::unique_lock lock(mutex_); Change(address,bytes,true);
    }
    void Reset() {
        std::unique_lock lock(mutex_);
        if(epoch_==std::numeric_limits<std::uint64_t>::max()) exhausted_=true;
        else ++epoch_;
        entries_.clear(); pages_.clear();
        page_links_=0; stats_={};
    }
    Stats GetStats() const { std::shared_lock lock(mutex_); return stats_; }

    class ReadGuard {
        // EN: Guards are non-reentrant and must not outlive their tracker.
        // PT-BR: Guardas não são reentrantes e não podem sobreviver ao tracker.
        const GuestMemoryTracker& tracker_;
        std::shared_lock<std::shared_mutex> lock_;
    public:
        explicit ReadGuard(const GuestMemoryTracker& tracker):tracker_(tracker),lock_(tracker.mutex_) {}
        ReadGuard(const ReadGuard&)=delete;
        Stamp CaptureGeneration(Handle handle) const { return tracker_.Capture(handle); }
        Prediction IsUnchanged(Handle handle,Stamp stamp) const { return tracker_.Query(handle,stamp); }
    };
    // EN: Invalidate before touching bytes; hold exclusion through completion,
    //     exceptions included. ReadGuard must cover comparison/copy/publication.
    //     Never retain a borrowed guest pointer after releasing either guard.
    // PT-BR: Invalide antes de tocar bytes; mantenha exclusão até concluir,
    //        inclusive em exceções. ReadGuard cobre comparação/cópia/publicação.
    //        Nunca retenha ponteiro guest emprestado após soltar a guarda.
    class WriteGuard {
        GuestMemoryTracker& tracker_;
        std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
        std::unique_lock<std::shared_mutex> lock_;
    public:
        WriteGuard(GuestMemoryTracker& tracker,std::uint32_t address,std::uint64_t bytes)
            :tracker_(tracker),lock_(tracker.mutex_) { tracker_.Change(address,bytes,false); }
        WriteGuard(const WriteGuard&)=delete;
        ~WriteGuard() {
            tracker_.stats_.write_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-start_).count();
        }
    };
    ReadGuard Read() const { return ReadGuard(*this); }
    WriteGuard Write(std::uint32_t address,std::uint64_t bytes) { return WriteGuard(*this,address,bytes); }
};
} // namespace pgr::memory
