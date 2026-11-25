// EN: Synthetic CPU microbenchmark, not a gameplay or FPS acceptance result.
// PT-BR: Microbenchmark sintético de CPU, não é validação de gameplay ou FPS.
// Build / Compile: c++ -std=c++20 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -pthread
// Append / Acrescente: portable/tests/memory/benchmark_guest_memory_tracker.cpp -o /tmp/pgr1-tracker-benchmark
#include "../../include/pgr/memory/guest_memory_tracker.h"
#include <atomic>
#include <charconv>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {
using Tracker=pgr::memory::GuestMemoryTracker;
using Clock=std::chrono::steady_clock;
volatile std::uint64_t result_sink=0;

// EN: A compiler memory barrier inside every iteration prevents invariant
//     memcmp hoisting. Equal buffers also mutate together periodically.
// PT-BR: Uma barreira de compilação em cada iteração impede mover memcmp para
//        fora do laço. Buffers iguais também mudam juntos periodicamente.
inline void MemoryBarrier(const void* address) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r"(address) : "memory");
#else
    (void)address;
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}
struct Sample {
    double nanoseconds;
    std::uint64_t checksum;
};
template<class Work>
Sample Measure(std::uint64_t iterations,Work work) {
    std::uint64_t checksum=0;
    const auto start=Clock::now();
    for(std::uint64_t i=0;i<iterations;++i) checksum+=work(i);
    const auto elapsed=std::chrono::duration<double,std::nano>(Clock::now()-start).count();
    result_sink=checksum;
    return {elapsed,checksum};
}
void Print(std::string_view name,Sample sample,std::uint64_t iterations) {
    std::cout<<name<<": total_ms="<<sample.nanoseconds/1e6
             <<" ns/op="<<sample.nanoseconds/static_cast<double>(iterations)
             <<" checksum="<<sample.checksum<<'\n';
}
std::uint64_t Number(std::string_view text) {
    std::uint64_t value=0;
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size() || !value)
        throw std::invalid_argument("expected a positive integer / esperado inteiro positivo");
    return value;
}
} // namespace

int main(int argc,char** argv) try {
    std::uint64_t iterations=100000,write_iterations=20000,bytes=256u*1024u,resources=1024;
    for(int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        if(arg=="--help") {
            std::cout<<"Synthetic benchmark / Benchmark sintético\n"
                     <<"Options: --iterations N --write-iterations N --bytes N --resources N\n"
                     <<"Compare the same binary/configuration; no gameplay/FPS claim.\n"
                     <<"Compare o mesmo binário/configuração; nenhum resultado de gameplay/FPS.\n";
            return 0;
        }
        if(i+1>=argc) throw std::invalid_argument("missing option value / falta valor da opção");
        const auto value=Number(argv[++i]);
        if(arg=="--iterations") iterations=value;
        else if(arg=="--write-iterations") write_iterations=value;
        else if(arg=="--bytes") bytes=value;
        else if(arg=="--resources") resources=value;
        else throw std::invalid_argument("unknown option / opção desconhecida");
    }
    if(resources>4096 || bytes>1024u*1024u || iterations>1000000000 || write_iterations>1000000000)
        throw std::invalid_argument("limits: resources<=4096, bytes<=1MiB, iterations<=1e9");
    constexpr pgr::memory::PhysicalLayout layout{{0x80000000u,0xf0000000u},128u<<20,4096};
    // EN: Separate fixtures keep the untracked baseline outside the cooperative
    //     region contract. Only managed_guest is touched under writer guards.
    // PT-BR: Fixtures separadas mantêm a referência sem tracker fora do contrato
    //        cooperativo. Só managed_guest recebe escritas sob as guardas.
    std::vector<std::uint8_t> scan_guest(static_cast<std::size_t>(bytes),0x5a),snapshot=scan_guest;
    std::vector<std::uint8_t> managed_guest=scan_guest,plain_guest=scan_guest;
    Tracker tracker(layout);
    std::vector<Tracker::Handle> handles;
    std::vector<Tracker::Stamp> stamps;
    handles.reserve(static_cast<std::size_t>(resources));
    stamps.reserve(static_cast<std::size_t>(resources));
    // EN: Leave a final physical page deliberately unmonitored for the writer
    //     fast-path measurement. The data arrays themselves are owned fixtures.
    // PT-BR: Deixe a última página física sem monitoramento para medir escritores
    //        fora das regiões. Os arrays são fixtures de memória própria.
    const auto last_offset=layout.bytes-layout.page_bytes-bytes;
    for(std::uint64_t i=0;i<resources;++i) {
        const auto offset=resources>1?i*last_offset/(resources-1):0;
        const auto handle=tracker.RegisterRange(layout.aliases[0]+static_cast<std::uint32_t>(offset),bytes,
                                               Tracker::Coverage::Cooperative);
        if(!handle) throw std::runtime_error("registration limit / limite de registros; reduce resources or bytes");
        handles.push_back(handle);
    }
    {
        const auto read=tracker.Read();
        for(auto handle:handles) stamps.push_back(read.CaptureGeneration(handle));
    }
    std::cout<<std::fixed<<std::setprecision(3);
    std::cout<<"Synthetic tracker benchmark / Benchmark sintético do tracker\n";
#if defined(__VERSION__)
    std::cout<<"compiler="<<__VERSION__<<'\n';
#endif
#if defined(__aarch64__)
    std::cout<<"architecture=ARM64\n";
#elif defined(__x86_64__) || defined(_M_X64)
    std::cout<<"architecture=x86-64\n";
#else
    std::cout<<"architecture=other\n";
#endif
#if defined(NDEBUG)
    std::cout<<"assertions=disabled (NDEBUG)\n";
#else
    std::cout<<"assertions=enabled\n";
#endif
#if defined(__AVX2__)
    std::cout<<"compiled_AVX2=yes\n";
#else
    std::cout<<"compiled_AVX2=no\n";
#endif
    std::cout<<"iterations="<<iterations<<" write_iterations="<<write_iterations
             <<" bytes/comparison="<<bytes<<" resources="<<resources
             <<" logical_registered_bytes="<<tracker.GetStats().tracked_bytes<<'\n';
    // EN/PT-BR: Warm both arrays / Aqueça ambos os arrays.
    MemoryBarrier(scan_guest.data());
    if(std::memcmp(scan_guest.data(),snapshot.data(),scan_guest.size())!=0) return 2;
    const auto exact=Measure(iterations,[&](std::uint64_t i) {
        if(i%4096==0) {
            const auto offset=static_cast<std::size_t>((i/4096)%bytes);
            ++scan_guest[offset]; ++snapshot[offset];
        }
        MemoryBarrier(scan_guest.data()); MemoryBarrier(snapshot.data());
        return std::uint64_t(std::memcmp(scan_guest.data(),snapshot.data(),scan_guest.size())==0);
    });
    Print("exact_memcmp_equal",exact,iterations);
    if(exact.checksum!=iterations) return 2;
    const auto guarded=Measure(iterations,[&](std::uint64_t i) {
        MemoryBarrier(managed_guest.data());
        const auto index=static_cast<std::size_t>(i%resources);
        const auto read=tracker.Read();
        return std::uint64_t(read.IsUnchanged(handles[index],stamps[index]).CanReuse());
    });
    Print("tracker_query_with_read_guard",guarded,iterations);
    if(guarded.checksum!=iterations) return 2;
    Sample batched{};
    {
        const auto read=tracker.Read();
        batched=Measure(iterations,[&](std::uint64_t i) {
            MemoryBarrier(managed_guest.data());
            const auto index=static_cast<std::size_t>(i%resources);
            return std::uint64_t(read.IsUnchanged(handles[index],stamps[index]).CanReuse());
        });
    }
    Print("tracker_query_under_one_read_guard",batched,iterations);
    if(batched.checksum!=iterations) return 2;
    const auto plain=Measure(write_iterations,[&](std::uint64_t i) {
        MemoryBarrier(plain_guest.data());
        const auto offset=static_cast<std::size_t>(i%bytes);
        return std::uint64_t(++plain_guest[offset]);
    });
    Print("synthetic_byte_store_without_tracker",plain,write_iterations);
    const auto monitored=Measure(write_iterations,[&](std::uint64_t i) {
        const auto offset=static_cast<std::uint32_t>(i%bytes);
        const auto address=layout.aliases[i%2]+offset;
        const auto write=tracker.Write(address,1);
        return std::uint64_t(++managed_guest[offset]);
    });
    Print("synthetic_byte_store_with_monitored_write_guard",monitored,write_iterations);
    const auto after_monitored=tracker.GetStats();
    std::uint8_t unmonitored_byte=0;
    const auto unmonitored=Measure(write_iterations,[&](std::uint64_t i) {
        const auto write=tracker.Write(layout.aliases[i%2]+layout.bytes-layout.page_bytes,1);
        return std::uint64_t(++unmonitored_byte);
    });
    Print("synthetic_byte_store_with_unmonitored_write_guard",unmonitored,write_iterations);
    const auto after_unmonitored=tracker.GetStats();
    const auto read=tracker.Read();
    if(read.IsUnchanged(handles[0],stamps[0]).CanReuse() ||
       after_monitored.monitored_writes!=write_iterations ||
       after_unmonitored.monitored_writes!=after_monitored.monitored_writes) return 2;
    std::cout<<"monitored_writes="<<after_unmonitored.monitored_writes
             <<" dirty_page_visits="<<after_unmonitored.dirty_pages
             <<" writer_guard_internal_ms="<<after_unmonitored.write_ns/1e6<<'\n'
             <<"EN: Internal writer timing includes lock wait, notification and fixture store.\n"
             <<"PT-BR: Tempo interno do escritor inclui espera, notificação e store da fixture.\n"
             <<"EN: Hot equal-buffer scan versus metadata only; excludes decode, game writers, GPU and real capture.\n"
             <<"PT-BR: Compara buffers iguais aquecidos com metadados; exclui decode, escritores do jogo, GPU e captura real.\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
