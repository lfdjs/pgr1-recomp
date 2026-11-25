#include "pgr/memory/guest_memory_tracker.h"
#include "texture_coherency.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

using pgr::memory::Canonicalize;
using pgr::memory::GuestMemoryTracker;
using pgr::memory::PhysicalLayout;
using pgr::memory::PhysicalRange;
using pgr::gldirect::AuditTexturePrediction;
using Coverage=GuestMemoryTracker::Coverage;
constexpr PhysicalLayout kLayout{{0x80000000u,0xf0000000u},16384,4096};
constexpr auto kA=kLayout.aliases[0],kB=kLayout.aliases[1];

// EN: Both aliases access one backing store. Guards cover actual bytes, not
//     merely generation notifications. Unknown/unmarked cases run serially.
// PT-BR: Os aliases acessam um armazenamento. Guardas cobrem os bytes reais,
//        não só gerações. Casos desconhecidos/sem notificação são seriais.
struct Fixture {
    GuestMemoryTracker tracker{kLayout};
    std::vector<std::uint32_t> words=std::vector<std::uint32_t>(kLayout.bytes/4);
    std::uint8_t* Pointer(std::uint32_t address,std::size_t bytes) {
        const auto range=Canonicalize(kLayout,address,bytes);
        assert(range);
        return reinterpret_cast<std::uint8_t*>(words.data())+range->offset;
    }
    void Fill(std::uint32_t address,std::size_t bytes,std::uint8_t value) {
        auto write=tracker.Write(address,bytes);
        std::memset(Pointer(address,bytes),value,bytes);
    }
    GuestMemoryTracker::Stamp Stamp(GuestMemoryTracker::Handle handle) {
        auto read=tracker.Read(); return read.CaptureGeneration(handle);
    }
    GuestMemoryTracker::Prediction Query(GuestMemoryTracker::Handle handle,
                                        GuestMemoryTracker::Stamp stamp) {
        auto read=tracker.Read(); return read.IsUnchanged(handle,stamp);
    }
};

static void CanonicalRanges() {
    for(const auto base:kLayout.aliases) {
        assert(Canonicalize(kLayout,base,1)==PhysicalRange({0,1}));
        assert(Canonicalize(kLayout,base+4095,2)==PhysicalRange({4095,2}));
        assert(Canonicalize(kLayout,base,kLayout.bytes)==PhysicalRange({0,kLayout.bytes}));
        assert(Canonicalize(kLayout,base+kLayout.bytes-1,1)==PhysicalRange({kLayout.bytes-1,1}));
        assert(!Canonicalize(kLayout,base,0));
        assert(!Canonicalize(kLayout,base-1,1));
        assert(!Canonicalize(kLayout,base+kLayout.bytes,1));
        assert(!Canonicalize(kLayout,base+kLayout.bytes-1,2));
        assert(!Canonicalize(kLayout,base,kLayout.bytes+1));
        assert(!Canonicalize(kLayout,base,std::numeric_limits<std::uint64_t>::max()));
    }
    assert(!Canonicalize(kLayout,0,1));
    assert(!Canonicalize(kLayout,0x1000,4096));
    assert(!Canonicalize(kLayout,0xffffffffu,2));
    // The endpoint 2^32 is valid when reached exactly; wrapping is not.
    constexpr PhysicalLayout boundary{{0x80000000u,0xfffff000u},4096,4096};
    assert(Canonicalize(boundary,0xffffffffu,1)==PhysicalRange({4095,1}));
    assert(!Canonicalize(boundary,0xffffffffu,2));
    for(const auto layout:{PhysicalLayout{{kA,kB},0,4096},
                          PhysicalLayout{{kA,kB},16384,0},
                          PhysicalLayout{{kA,kB},16384,3000},
                          PhysicalLayout{{kA,kB},16383,4096},
                          PhysicalLayout{{kA,kA+4096},16384,4096},
                          PhysicalLayout{{kA,0xfffff000u},16384,4096}}) {
        bool rejected=false;
        try { GuestMemoryTracker invalid(layout); }
        catch(const std::invalid_argument&) { rejected=true; }
        assert(rejected);
    }
}

static void ScalarAndBulkWrites() {
    Fixture fixture;
    const auto handle=fixture.tracker.RegisterRange(kA+4090,8192,Coverage::Cooperative);
    assert(handle);
    // Scalar stores include 80-bit x87 and 128-bit SIMD extents across a page.
    for(const auto bytes:{1u,2u,4u,8u,10u,16u}) {
        for(const auto alias:kLayout.aliases) {
            const auto stamp=fixture.Stamp(handle);
            assert(fixture.Query(handle,stamp).CanReuse());
            fixture.Fill(alias+4095,bytes,static_cast<std::uint8_t>(bytes));
            assert(!fixture.Query(handle,stamp).CanReuse());
            auto read=fixture.tracker.Read();
            assert(std::all_of(fixture.Pointer(kA+4095,bytes),fixture.Pointer(kA+4095,bytes)+bytes,
                [=](std::uint8_t value){return value==bytes;}));
            assert(std::memcmp(fixture.Pointer(kA+4095,bytes),fixture.Pointer(kB+4095,bytes),bytes)==0);
        }
    }
    auto stamp=fixture.Stamp(handle);
    fixture.Fill(kB+4090,8192,0x42); // memset / REP STOS-style batch guard
    assert(!fixture.Query(handle,stamp).CanReuse());
    stamp=fixture.Stamp(handle);
    {
        auto write=fixture.tracker.Write(kB+5000,2048);
        std::memmove(fixture.Pointer(kB+5000,2048),fixture.Pointer(kA+4090,2048),2048);
    }
    assert(!fixture.Query(handle,stamp).CanReuse());
    stamp=fixture.Stamp(handle);
    {
        // REP MOVS: one guard covers the entire destination, not each element.
        auto write=fixture.tracker.Write(kB+8000,1024);
        const auto* source=fixture.Pointer(kA+4090,1024);
        auto* destination=fixture.Pointer(kB+8000,1024);
        for(std::size_t i=0;i<1024;++i) destination[i]=source[i];
    }
    assert(!fixture.Query(handle,stamp).CanReuse());
    stamp=fixture.Stamp(handle);
    {
        auto write=fixture.tracker.Write(kB+10000,4);
        auto& word=fixture.words[10000/4];
        std::atomic_ref<std::uint32_t> atomic(word);
        const auto previous=atomic.fetch_add(1,std::memory_order_relaxed);
        assert(atomic.load(std::memory_order_relaxed)==previous+1);
    }
    assert(!fixture.Query(handle,stamp).CanReuse());
    // A writer that throws still invalidates and releases exclusion.
    stamp=fixture.Stamp(handle);
    try {
        auto write=fixture.tracker.Write(kA+10000,4);
        throw 7;
    } catch(int value) { assert(value==7); }
    assert(!fixture.Query(handle,stamp).CanReuse());
    const auto stats=fixture.tracker.GetStats();
    assert(stats.monitored_writes==17 && stats.dirty_pages>=stats.monitored_writes);
    assert(stats.tracked_bytes==8192);
}

static void OverlapAndLifecycle() {
    Fixture fixture;
    const auto first=fixture.tracker.RegisterRange(kA+4090,20,Coverage::Cooperative);
    const auto overlap=fixture.tracker.RegisterRange(kB+4100,40,Coverage::Cooperative);
    const auto separate=fixture.tracker.RegisterRange(kA+4200,10,Coverage::Cooperative);
    const auto first_stamp=fixture.Stamp(first),overlap_stamp=fixture.Stamp(overlap),separate_stamp=fixture.Stamp(separate);
    fixture.Fill(kB+4105,2,1);
    assert(!fixture.Query(first,first_stamp).CanReuse());
    assert(!fixture.Query(overlap,overlap_stamp).CanReuse());
    assert(fixture.Query(separate,separate_stamp).CanReuse()); // same page, no byte overlap
    auto stamp=fixture.Stamp(first);
    fixture.tracker.InvalidateRange(kB+4090,20); // free/remap/GPU authority revokes proof
    auto prediction=fixture.Query(first,stamp);
    assert(prediction.registered && !prediction.covered && !prediction.advisory_unchanged);
    stamp=fixture.Stamp(first);
    assert(!fixture.Query(first,stamp).CanReuse()); // a fresh stamp cannot restore coverage
    fixture.tracker.UnregisterRange(first);
    assert(!fixture.Query(first,stamp).registered);
    fixture.tracker.UnregisterRange(first); // idempotent release
    const auto reused=fixture.tracker.RegisterRange(kB+4090,20,Coverage::Cooperative);
    assert(reused && reused!=first);
    assert(!fixture.Query(reused,stamp).CanReuse());
    assert(fixture.Query(reused,fixture.Stamp(reused)).CanReuse());
    stamp=fixture.Stamp(reused);
    fixture.tracker.Reset();
    assert(!fixture.Query(reused,stamp).registered);
    const auto reset_reused=fixture.tracker.RegisterRange(kA+4090,20,Coverage::Cooperative);
    assert(reset_reused && reset_reused!=reused);
    assert(!fixture.Query(reset_reused,stamp).CanReuse());
    assert(fixture.tracker.GetStats().tracked_bytes==20);
    // Partly unknown ranges must still invalidate their physical intersection.
    stamp=fixture.Stamp(reset_reused);
    fixture.tracker.InvalidateRange(kA-1,4095);
    assert(!fixture.Query(reset_reused,stamp).CanReuse());
    fixture.tracker.Reset();
    const auto last=fixture.tracker.RegisterRange(kB+kLayout.bytes-1,1,Coverage::Cooperative);
    stamp=fixture.Stamp(last);
    fixture.tracker.InvalidateRange(kB+kLayout.bytes-1,std::numeric_limits<std::uint64_t>::max());
    assert(!fixture.Query(last,stamp).CanReuse());
}

static void BoundedCapacityAndUnknown() {
    {
        Fixture first_domain,second_domain;
        const auto first=first_domain.tracker.RegisterRange(kA,16,Coverage::Cooperative);
        const auto second=second_domain.tracker.RegisterRange(kA,16,Coverage::Cooperative);
        assert(first==second); // independent handle namespaces must not accept foreign stamps
        assert(!second_domain.Query(second,first_domain.Stamp(first)).CanReuse());
    }
    GuestMemoryTracker bounded(kLayout,2,2);
    const auto first=bounded.RegisterRange(kA+4095,2,Coverage::Cooperative);
    assert(first);
    assert(!bounded.RegisterRange(kA+9000,1)); // page-link cap, two links already held
    bounded.UnregisterRange(first);
    const auto second=bounded.RegisterRange(kA,1),third=bounded.RegisterRange(kB+4096,1);
    assert(second && third);
    assert(!bounded.RegisterRange(kA+8192,1)); // range cap
    bounded.UnregisterRange(second);
    assert(bounded.RegisterRange(kA+8192,1));
    assert(!bounded.RegisterRange(0x1000,1));
    assert(!bounded.RegisterRange(kA,0));
    bounded.Reset();
    assert(bounded.GetStats().tracked_bytes==0);
    GuestMemoryTracker disabled(kLayout,0,0);
    assert(!disabled.RegisterRange(kA,1,Coverage::Cooperative));
    auto read=disabled.Read();
    assert(!read.IsUnchanged(0,{}).CanReuse());
}

static void AuditNeverTrustsUnknown() {
    Fixture fixture;
    const auto handle=fixture.tracker.RegisterRange(kA+128,64); // deliberately Unknown
    auto stamp=fixture.Stamp(handle);
    std::vector<std::uint8_t> snapshot(64,0);
    auto prediction=fixture.Query(handle,stamp);
    assert(prediction.registered && prediction.advisory_unchanged && !prediction.covered);
    assert(!prediction.CanReuse());
    assert(!AuditTexturePrediction(prediction,false).false_negative);
    // A serial raw writer bypassing the contract demonstrates why Unknown
    // must keep exact fallback; it is not a permitted cooperative operation.
    fixture.Pointer(kB+128,64)[32]=9;
    {
        auto read=fixture.tracker.Read();
        prediction=read.IsUnchanged(handle,stamp);
        const bool changed=std::memcmp(snapshot.data(),fixture.Pointer(kA+128,64),64)!=0;
        const auto audit=AuditTexturePrediction(prediction,changed);
        assert(changed && audit.false_negative && !audit.false_positive);
        assert(!prediction.CanReuse());
        snapshot.assign(fixture.Pointer(kA+128,64),fixture.Pointer(kA+128,64)+64);
        stamp=read.CaptureGeneration(handle);
    }
    {
        // Coarse notification without changed bytes gives a false positive,
        // which costs exact fallback but cannot authorize stale texture reuse.
        auto write=fixture.tracker.Write(kB+128,64);
    }
    {
        auto read=fixture.tracker.Read();
        prediction=read.IsUnchanged(handle,stamp);
        const bool changed=std::memcmp(snapshot.data(),fixture.Pointer(kA+128,64),64)!=0;
        const auto audit=AuditTexturePrediction(prediction,changed);
        assert(!changed && audit.false_positive && !audit.false_negative);
    }
    assert(!AuditTexturePrediction({},true).false_negative);
    assert(pgr::gldirect::ParseTextureCoherency(nullptr)==pgr::gldirect::TextureCoherency::Exact);
    assert(pgr::gldirect::ParseTextureCoherency("invalid")==pgr::gldirect::TextureCoherency::Exact);
    assert(pgr::gldirect::ParseTextureCoherency("exact")==pgr::gldirect::TextureCoherency::Exact);
    assert(pgr::gldirect::ParseTextureCoherency("audit")==pgr::gldirect::TextureCoherency::Audit);
    assert(pgr::gldirect::ParseTextureCoherency("tracked")==pgr::gldirect::TextureCoherency::Tracked);
}

static void ConcurrentSnapshots() {
    Fixture fixture;
    constexpr std::size_t bytes=256;
    const auto handle=fixture.tracker.RegisterRange(kA+4090,bytes,Coverage::Cooperative);
    std::atomic<bool> start=false;
    std::atomic<unsigned> running=2;
    const auto writer=[&](std::uint32_t alias,unsigned seed) {
        while(!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for(unsigned i=0;i<512;++i) {
            fixture.Fill(alias+4090,bytes,static_cast<std::uint8_t>(seed+i));
            if(i%16==0) std::this_thread::yield();
        }
        running.fetch_sub(1,std::memory_order_release);
    };
    std::thread first(writer,kA,1),second(writer,kB,37);
    std::vector<std::vector<std::uint8_t>> immutable;
    std::vector<std::uint8_t> immutable_values;
    std::vector<std::uint8_t> current;
    GuestMemoryTracker::Stamp stamp{};
    start.store(true,std::memory_order_release);
    unsigned captures=0;
    do {
        {
            auto read=fixture.tracker.Read();
            const auto prediction=read.IsUnchanged(handle,stamp);
            const auto* guest=fixture.Pointer(kA+4090,bytes);
            if(prediction.CanReuse()) {
                assert(current.size()==bytes);
                assert(std::memcmp(current.data(),guest,bytes)==0);
            } else {
                current.assign(guest,guest+bytes);
                stamp=read.CaptureGeneration(handle);
                if(immutable.size()<64) {
                    immutable.push_back(current);
                    immutable_values.push_back(current.front());
                }
            }
            assert(current.size()==bytes);
            assert(std::all_of(current.begin(),current.end(),[&](std::uint8_t value){return value==current.front();}));
        }
        ++captures;
        std::this_thread::yield();
    } while(running.load(std::memory_order_acquire) || captures<128);
    first.join(); second.join();
    fixture.Fill(kB+4090,bytes,0xee);
    assert(!immutable.empty());
    for(std::size_t i=0;i<immutable.size();++i) {
        const auto& snapshot=immutable[i];
        assert(snapshot.size()==bytes);
        assert(std::all_of(snapshot.begin(),snapshot.end(),[&](std::uint8_t value){return value==immutable_values[i];}));
    }
    assert(fixture.tracker.GetStats().monitored_writes==1025);
}

int main() {
    CanonicalRanges();
    ScalarAndBulkWrites();
    OverlapAndLifecycle();
    BoundedCapacityAndUnknown();
    AuditNeverTrustsUnknown();
    ConcurrentSnapshots();
    std::puts("Guest memory coherency tests passed / Testes de coerência da memória guest passaram");
}
