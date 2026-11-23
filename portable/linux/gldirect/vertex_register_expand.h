#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace pgr::gldirect {
// EN: Resolve a draw's active Xbox vertex registers once, not per vertex.
// PT-BR: Determine os registradores do Xbox uma vez por draw, nao por vertice.
class VertexRegisterExpander {
public:
    using Attributes=std::array<std::array<float,4>,16>;
    explicit VertexRegisterExpander(std::uint32_t mask) {
        for(unsigned reg=0;reg<16;++reg)if(mask&(1u<<reg))registers_[count_++]=reg;
    }
    Attributes Expand(const float* packed)const {
        Attributes result=kDefaults;
        for(unsigned i=0;i<count_;++i)
            std::memcpy(result[registers_[i]].data(),packed+4*i,4*sizeof(float));
        return result;
    }
private:
    static constexpr Attributes kDefaults=[] {
        Attributes defaults{};
        for(unsigned reg=0;reg<16;++reg)
            defaults[reg]=reg==3?std::array<float,4>{1,1,1,1}:
                                 std::array<float,4>{0,0,0,1};
        return defaults;
    }();
    std::array<unsigned,16> registers_{};
    unsigned count_=0;
};
} // namespace pgr::gldirect
