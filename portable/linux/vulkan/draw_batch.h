#pragma once
// EN: Group consecutive draws with an identical target. External GPU sources
// are safe because no draw in the group writes them; prepare before BeginTarget.
// PT-BR: Agrupe draws consecutivos com alvo idêntico. Origens externas da GPU
// são seguras porque o grupo não as escreve; prepare antes de BeginTarget.
#include "../gldirect/commands.h"
#include <algorithm>
#include <cstddef>

namespace pgr::vulkan::batch {
using gldirect::Command;
using gldirect::Target;

inline bool Eligible(const Command& c,bool external=true) {
    if (c.kind != Command::Kind::Draw) return false;
    // EN: Do not move a read before a write to either attachment in this group.
    // PT-BR: Não antecipe uma leitura em relação à escrita em qualquer anexo do grupo.
    const auto safe=[&](std::uint32_t address) {
        return !address || (external && address!=c.target.color && address!=c.target.depth);
    };
    for (const auto& t:c.textures) {
        if (!safe(t.render_target)) return false;
        if (!std::all_of(t.face_targets.begin(),t.face_targets.end(),safe)) return false;
    }
    return true;
}
inline bool SameTarget(const Target& a,const Target& b) {
    return a.color==b.color && a.depth==b.depth && a.width==b.width &&
           a.height==b.height && a.opaque==b.opaque &&
           a.format==b.format && a.depth_format==b.depth_format;
}
inline std::size_t CandidateEnd(const std::vector<Command>& commands,
                                std::size_t begin,std::size_t maximum=128,
                                bool external=true) {
    if (begin>=commands.size() || !Eligible(commands[begin],external) || !maximum) return begin;
    std::size_t end=begin+1;
    while (end<commands.size() && end-begin<maximum &&
           Eligible(commands[end],external) && SameTarget(commands[begin].target,commands[end].target)) ++end;
    return end;
}
} // namespace pgr::vulkan::batch
