#include "vertex_register_expand.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
using pgr::gldirect::VertexRegisterExpander;
static VertexRegisterExpander::Attributes Reference(std::uint32_t mask,const float* input) {
    VertexRegisterExpander::Attributes attrs{};
    for(unsigned reg=0;reg<16;++reg)
        attrs[reg]=reg==3?std::array<float,4>{1,1,1,1}:
                          std::array<float,4>{0,0,0,1};
    unsigned offset=0;
    for(unsigned reg=0;reg<16;++reg)if(mask&(1u<<reg)){
        std::copy_n(input+offset,4,attrs[reg].data());offset+=4;
    }
    return attrs;
}
int main() {
    std::array<float,64> inputs{};
    for(unsigned i=0;i<inputs.size();++i)inputs[i]=float(i)*0.5f-10.0f;
    for(std::uint32_t mask=0;mask<=0xffff;mask+=257) {
        VertexRegisterExpander expander(mask);
        const auto fast=expander.Expand(inputs.data());
        const auto slow=Reference(mask,inputs.data());
        assert(std::memcmp(&fast,&slow,sizeof(fast))==0);
    }
    for(std::uint32_t mask:{0u,1u,8u,0x200u,0xFFFFu,0x209u}) {
        VertexRegisterExpander expander(mask);
        const auto fast=expander.Expand(inputs.data()),slow=Reference(mask,inputs.data());
        assert(std::memcmp(&fast,&slow,sizeof(fast))==0);
    }
    std::puts("Vertex expansion OK / Registradores validados");
}
