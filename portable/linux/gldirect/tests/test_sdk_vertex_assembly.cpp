// EN: Compare real optimized SDK assembly with the original pinned assembler.
// PT-BR: Compare a montagem SDK otimizada real com a implementação fixada original.
#include "gpu/vertex.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "flat_primitive_indices.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>
#include <vector>

namespace { std::vector<uint8_t> memory(2u << 20); }
namespace kernel { uint8_t* g_base=memory.data(); }
namespace mem {
bool IsCommitted(uint32_t address) { return address < memory.size(); }
uint32_t VirtualFromPhysical(uint32_t physical) { return physical; }
}
namespace logx { void Write(Level,const char*,...) {} }
namespace gpu {
bool AssembleReference(const State&,uint32_t,uint32_t,uint32_t,const uint8_t*,Assembled&);
extern bool g_reference_raw_vertices;
}

namespace {
// EN: Expand the consumer's native topology to the original list convention.
//     Full float4 payloads, winding, order and last provoking vertex must match.
// PT-BR: Expanda a topologia nativa para a convenção original de listas.
//        Compare float4 completos, orientação, ordem e último provoking vertex.
std::vector<uint32_t> PrimitiveList(const gpu::Assembled& a) {
    std::vector<uint32_t> sequence=a.indices, result;
    if(sequence.empty()) {
        sequence.resize(a.draw_count);
        std::iota(sequence.begin(),sequence.end(),0u);
    }
    const std::size_t step=a.topology==gpu::Topology::Points?1:
        a.topology==gpu::Topology::Lines?2:3;
    if(a.strip) {
        for(std::size_t i=0;i+step<=sequence.size();++i) {
            if(step==3 && (i&1)) {
                result.push_back(sequence[i+1]); result.push_back(sequence[i]);
            } else {
                result.push_back(sequence[i]); result.push_back(sequence[i+1]);
            }
            if(step==3) result.push_back(sequence[i+2]);
        }
    } else {
        result.assign(sequence.begin(),sequence.begin()+sequence.size()/step*step);
    }
    return result;
}
uint32_t ConsumerTopology(const gpu::Assembled& a) {
    return a.topology==gpu::Topology::Points?0:
        a.topology==gpu::Topology::Lines?(a.strip?3:1):(a.strip?5:4);
}

void Compare(gpu::State& state,uint32_t primitive,uint32_t start,uint32_t count,
             const uint8_t* indices) {
    gpu::Assembled before, after;
    const bool old_ok=gpu::AssembleReference(state,primitive,start,count,indices,before);
    const bool new_ok=gpu::Assemble(state,primitive,start,count,indices,after);
    assert(old_ok==new_ok);
    if(!old_ok) return;
    assert(before.vertex_count==after.vertex_count && before.regs==after.regs);
    assert(before.pretransformed==after.pretransformed && before.topology==after.topology);
    assert(before.vertices.size()==after.vertices.size());
    assert(std::memcmp(before.vertices.data(),after.vertices.data(),before.vertices.size()*sizeof(float))==0);
    const auto old_list=PrimitiveList(before), new_list=PrimitiveList(after);
    assert(old_list==new_list);
    assert(after.index_src==nullptr || after.index_src==after.indices.data());
    assert(!after.index16 && after.base_vertex==0);
    if(primitive==1 || primitive==2 || primitive==4 || primitive==5 || primitive==6) {
        if(!indices || after.vertex_count==count) {
            // Sparse indexed draws may also be represented without indices.
            if(!indices) assert(after.indices.empty());
        }
        if(primitive==6 && indices) assert(after.indices.size()<=count);
    }
    // EN/PT-BR: Validate flat conversion against the original primitive list.
    if(after.topology!=gpu::Topology::Points) {
        const auto old_flat=pgr::gldirect::BuildFlatPrimitiveIndices(
            ConsumerTopology(before),before.vertex_count,before.indices);
        const auto new_flat=pgr::gldirect::BuildFlatPrimitiveIndices(
            ConsumerTopology(after),after.vertex_count,after.indices);
        assert(old_flat.topology==new_flat.topology && old_flat.indices==new_flat.indices);
    }
}

gpu::State MakeState(unsigned layout) {
    gpu::State state;
    state.vs_handle=layout==0?0x144:1; // XYZRHW + DIFFUSE + TEX1
    kernel::W32(0x1004,0x10000); kernel::W32(0x1104,0x100000);
    state.streams[0]={0x1000,layout==0?28u:48u};
    state.streams[1]={0x1100,24};
    if(layout) {
        auto shader=std::make_shared<gpu::VertexShader>();
        shader->inputs[0]={0,0,0x32};
        shader->inputs[2]={1,1,0x31}; // unaligned normalized SHORT3
        shader->inputs[3]={0,16,0x40};
        shader->inputs[9]={0,20,0x72}; // FLOAT2H
        shader->inputs[12]={1,8,0x16}; // packed signed 11:11:10
        shader->stream_stride[0]=48; shader->stream_stride[1]=24;
        state.vs=shader;
        if(layout==2) {
            state.input_active=true;
            state.input_streams=state.streams;
            state.input_layout=shader->inputs;
            state.input_layout[9].offset=24;
        } else if(layout==3) {
            state.streams[0].stride=0;
            state.streams[1].stride=0;
        }
    }
    return state;
}

void RunCases() {
    std::size_t cases=0;
    for(unsigned layout=0;layout<4;++layout) {
        auto state=MakeState(layout);
        for(uint32_t primitive=1;primitive<=10;++primitive) {
            for(uint32_t count=0;count<=34;++count) {
                for(uint32_t start:{0u,7u}) { Compare(state,primitive,start,count,nullptr); ++cases; }
                for(unsigned pattern=0;pattern<4;++pattern) {
                    std::vector<uint16_t> indices(count+1);
                    for(uint32_t i=0;i<count;++i) indices[i]=uint16_t(
                        pattern==0?i+7:pattern==1?count+7-i:pattern==2?(i%4)+7:(i&1?2047:7));
                    for(uint32_t base:{0u,11u,uint32_t(-3)}) {
                        state.base_vertex=base;
                        // EN/PT-BR: Deliberately unaligned guest index source.
                        std::vector<uint8_t> unaligned(count*2+1);
                        std::memcpy(unaligned.data()+1,indices.data(),count*2);
                        Compare(state,primitive,29,count,unaligned.data()+1); ++cases;
                    }
                    state.base_vertex=0;
                }
            }
        }
    }
    auto state=MakeState(0);
    gpu::Assembled output;
    assert(gpu::Assemble(state,6,0,128,nullptr,output));
    const auto retained_vertices=output.vertices;
    const auto retained_indices=output.indices;
    kernel::W32(0x10000,0x3f123456);
    assert(gpu::Assemble(state,6,0,128,nullptr,output));
    assert(output.vertices!=retained_vertices && retained_indices.empty());
    Compare(state,6,0,128,nullptr);
    std::printf("SDK assembly differential cases: %zu / Casos diferenciais SDK: %zu\n",cases,cases);
}

void Benchmark() {
    auto state=MakeState(0);
    constexpr unsigned count=8192, iterations=500;
    for(uint32_t primitive:{5u,6u,8u}) {
        for(bool indexed:{false,true}) {
            std::vector<uint16_t> indices(count);
            std::iota(indices.begin(),indices.end(),0);
            const auto* source=indexed?reinterpret_cast<const uint8_t*>(indices.data()):nullptr;
            std::size_t old_indices=0,new_indices=0;
            auto measure=[&](bool original) {
                gpu::Assembled assembled;
                const auto begin=std::chrono::steady_clock::now();
                for(unsigned i=0;i<iterations;++i) {
                    bool ok=original?gpu::AssembleReference(state,primitive,0,count,source,assembled):
                        gpu::Assemble(state,primitive,0,count,source,assembled);
                    assert(ok);
                }
                (original?old_indices:new_indices)=assembled.indices.size();
                return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            };
            // EN/PT-BR: Warm both implementations / Aqueça ambas implementações.
            measure(true); measure(false);
            const double old_ms=measure(true),new_ms=measure(false);
            std::printf("primitive=%u indexed=%u old_ms=%.3f new_ms=%.3f old_indices=%zu new_indices=%zu\n",
                primitive,unsigned(indexed),old_ms,new_ms,old_indices,new_indices);
        }
    }
}
}
int main(int argc,char** argv) {
    gpu::g_raw_vertices=false; gpu::g_reference_raw_vertices=false;
    for(std::size_t i=0;i<memory.size()/4;++i) {
        const float value=float(i%97)*0.125f-6;
        std::memcpy(memory.data()+i*4,&value,4);
    }
    RunCases();
    if(argc==2 && std::strcmp(argv[1],"--benchmark")==0) Benchmark();
}
