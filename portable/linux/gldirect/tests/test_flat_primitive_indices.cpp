#include "flat_primitive_indices.h"
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using pgr::gldirect::BuildFlatPrimitiveIndices;
using pgr::gldirect::FlatPrimitiveIndices;

// EN/PT-BR: Previous capture implementation / Implementação anterior da captura.
static FlatPrimitiveIndices Reference(std::uint32_t topology, std::uint32_t count,
                                     std::span<const std::uint32_t> source) {
    FlatPrimitiveIndices result{topology,{source.begin(),source.end()}};
    if(topology==0) return result;
    auto input=std::move(result.indices);
    if(input.empty()) for(std::uint32_t i=0;i<count;++i) input.push_back(i);
    result.indices.clear();
    if(topology==5) {
        for(std::size_t i=0;i+2<input.size();++i) {
            result.indices.push_back(input[i+2]);
            result.indices.push_back(input[i+(i&1)]);
            result.indices.push_back(input[i+1-(i&1)]);
        }
        result.topology=4;
    } else if(topology==3) {
        for(std::size_t i=0;i+1<input.size();++i) {
            result.indices.push_back(input[i+1]);
            result.indices.push_back(input[i]);
        }
        result.topology=1;
    } else {
        const std::size_t step=topology==4?3:2;
        for(std::size_t i=0;i+step<=input.size();i+=step) {
            result.indices.push_back(input[i+step-1]);
            for(std::size_t j=0;j+1<step;++j) result.indices.push_back(input[i+j]);
        }
    }
    return result;
}

static void Check(std::uint32_t topology, std::uint32_t count,
                  std::span<const std::uint32_t> source) {
    const auto expected=Reference(topology,count,source);
    const auto actual=BuildFlatPrimitiveIndices(topology,count,source);
    assert(actual.topology==expected.topology);
    assert(actual.indices==expected.indices);
}

static void Benchmark() {
    constexpr std::uint32_t count=8192;
    constexpr unsigned iterations=4000;
    std::vector<std::uint32_t> source(count);
    std::iota(source.begin(),source.end(),0);
    for(const bool indexed:{false,true}) {
        const auto input=indexed?std::span<const std::uint32_t>(source):std::span<const std::uint32_t>{};
        for(const auto topology:{1u,3u,4u,5u}) {
            std::uint64_t checksum=0;
            const auto run=[&](auto build) {
                const auto begin=std::chrono::steady_clock::now();
                for(unsigned iteration=0;iteration<iterations;++iteration) {
                    const auto converted=build(topology,count,input);
                    checksum+=converted.indices[(iteration*7)%converted.indices.size()];
                }
                return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            };
            const double previous_ms=run(Reference),direct_ms=run(BuildFlatPrimitiveIndices);
            std::printf("flat indices: indexed=%d topology=%u vertices=%u iterations=%u previous_ms=%.3f direct_ms=%.3f checksum=%llu\n",
                indexed,topology,count,iterations,previous_ms,direct_ms,static_cast<unsigned long long>(checksum));
        }
    }
}

int main(int argc,char** argv) {
    // EN: Last vertex first, with triangle winding and primitive order intact.
    // PT-BR: Último vértice primeiro, mantendo winding e ordem de primitivas.
    assert(BuildFlatPrimitiveIndices(4,7,{}).indices==
        std::vector<std::uint32_t>({2,0,1,5,3,4}));
    const auto strip=BuildFlatPrimitiveIndices(5,5,{});
    assert(strip.topology==4 && strip.indices==
        std::vector<std::uint32_t>({2,0,1,3,2,1,4,2,3}));
    const auto lines=BuildFlatPrimitiveIndices(3,4,{});
    assert(lines.topology==1 && lines.indices==
        std::vector<std::uint32_t>({1,0,2,1,3,2}));
    assert(BuildFlatPrimitiveIndices(1,5,{}).indices==
        std::vector<std::uint32_t>({1,0,3,2}));
    for(const auto topology:{0u,1u,3u,4u,5u}) {
        for(std::uint32_t count=0;count<=257;++count) {
            std::vector<std::uint32_t> source(count);
            // Repeated/out-of-order indices include degenerate strips.
            for(std::size_t i=0;i<source.size();++i) source[i]=std::uint32_t((i*19)%17);
            Check(topology,count,{});
            Check(topology,count+7,source);
        }
    }
    std::vector<std::uint32_t> source{9,5,1,1,7,4};
    const auto retained=BuildFlatPrimitiveIndices(5,0,source);
    const auto expected=retained.indices;
    source[0]=123;
    Check(5,0,source);
    assert(retained.indices==expected);
    // The SDK widens supported 16-bit indices before this boundary.
    const std::vector<std::uint32_t> widened{0,65535,32768,65534};
    Check(3,0,widened);
    bool rejected=false;
    try { static_cast<void>(BuildFlatPrimitiveIndices(2,0,{})); }
    catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    std::puts("Flat primitive index conversion OK / Conversão de índices flat validada");
    if(argc>1 && std::string_view(argv[1])=="--benchmark") Benchmark();
}
