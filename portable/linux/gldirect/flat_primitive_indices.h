#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace pgr::gldirect {
struct FlatPrimitiveIndices {
    std::uint32_t topology=0;
    std::vector<std::uint32_t> indices;
};

// EN: Convert last-vertex Xbox colors to Vulkan's first-vertex convention.
// Read the producer's SDK indices synchronously; the result owns all indices.
// Implicit indices need no temporary array. Complete primitives retain their
// winding and order; incomplete list tails are discarded as in capture v1.
// PT-BR: Converta a cor do último vértice Xbox para o primeiro do Vulkan.
// Leia os índices SDK na captura síncrona; a saída possui todos os índices.
// Índices implícitos dispensam vetor temporário. Primitivas completas mantêm
// orientação e ordem; caudas incompletas de listas são descartadas como antes.
inline FlatPrimitiveIndices BuildFlatPrimitiveIndices(
    std::uint32_t topology, std::uint32_t vertex_count,
    std::span<const std::uint32_t> source) {
    FlatPrimitiveIndices result;
    result.topology=topology;
    if(topology==0) {
        result.indices.assign(source.begin(),source.end());
        return result;
    }
    if(topology!=1 && topology!=3 && topology!=4 && topology!=5)
        throw std::invalid_argument("unsupported flat primitive topology");
    const std::size_t count=source.empty()?vertex_count:source.size();
    const auto index=[&](std::size_t i) {
        return source.empty()?static_cast<std::uint32_t>(i):source[i];
    };
    const std::size_t step=topology==4 || topology==5?3:2;
    const bool strip=topology==3 || topology==5;
    const std::size_t primitives=strip?(count>=step?count-step+1:0):count/step;
    if(primitives>result.indices.max_size()/step)
        throw std::length_error("flat primitive index count overflow");
    result.indices.resize(primitives*step);
    if(strip) {
        result.topology=step==3?4:1;
        for(std::size_t i=0;i<primitives;++i) {
            auto* destination=result.indices.data()+i*step;
            destination[0]=index(i+step-1);
            if(step==3) {
                destination[1]=index(i+(i&1));
                destination[2]=index(i+1-(i&1));
            } else destination[1]=index(i);
        }
    } else {
        for(std::size_t i=0;i<primitives;++i) {
            auto* destination=result.indices.data()+i*step;
            const auto first=i*step;
            destination[0]=index(first+step-1);
            for(std::size_t j=0;j+1<step;++j) destination[j+1]=index(first+j);
        }
    }
    return result;
}
} // namespace pgr::gldirect
