#pragma once
// 04D.6: group only consecutive draws that cannot read a GPU-written target.
// Resource uploads and descriptor writes are performed before opening the group.
#include "../gldirect/commands.h"
#include <algorithm>
#include <cstddef>

namespace pgr::vulkan::batch {
using gldirect::Command;
using gldirect::Target;

inline bool Eligible(const Command& c) {
    if (c.kind != Command::Kind::Draw) return false;
    for (const auto& t:c.textures) {
        if (t.render_target) return false;
        if (std::any_of(t.face_targets.begin(),t.face_targets.end(),
                        [](std::uint32_t address) { return address!=0; })) return false;
    }
    return true;
}
inline bool SameTarget(const Target& a,const Target& b) {
    return a.color==b.color && a.depth==b.depth && a.width==b.width &&
           a.height==b.height && a.opaque==b.opaque &&
           a.format==b.format && a.depth_format==b.depth_format;
}
inline std::size_t CandidateEnd(const std::vector<Command>& commands,
                                std::size_t begin,std::size_t maximum=128) {
    if (begin>=commands.size() || !Eligible(commands[begin]) || !maximum) return begin;
    std::size_t end=begin+1;
    while (end<commands.size() && end-begin<maximum &&
           Eligible(commands[end]) && SameTarget(commands[begin].target,commands[end].target)) ++end;
    return end;
}
} // namespace pgr::vulkan::batch
