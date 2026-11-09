#include "vk_command_bridge.h"
#include "../gldirect/commands.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pgr::vk {
namespace {
float Byte(std::uint32_t color,unsigned shift) { return float((color>>shift)&255u)/255.0f; }
bool Finite(const std::array<float,4>& a) {
    return std::all_of(a.begin(),a.end(),[](float f){return std::isfinite(f);});
}
}
Frame FromSubmission(const gldirect::Submission& src, unsigned guest_width, unsigned guest_height) {
    Frame out;
    if(!guest_width || !guest_height || guest_width>16384 || guest_height>16384) return out;
    for(const auto& c:src.commands) {
        if(c.kind == gldirect::Command::Kind::Clear && c.target.color == src.front) {
            // Partial clears, masks and clears after existing Draws require command-ordered
            // render passes; this early adapter intentionally does not guess their effect.
            if(out.accepted_draws || (c.flags&0xf0)!=0xf0 ||
               c.rects.size()!=1 || c.rects[0].x!=0 || c.rects[0].y!=0 ||
               unsigned(c.rects[0].w)!=guest_width || unsigned(c.rects[0].h)!=guest_height) {
                ++out.rejected_state; continue;
            }
            out.clear={Byte(c.color,16),Byte(c.color,8),Byte(c.color,0),Byte(c.color,24)};
            continue;
        }
        if(c.kind!=gldirect::Command::Kind::Draw) continue;
        ++out.input_draws;
        if(c.target.color!=src.front || c.target.width!=guest_width || c.target.height!=guest_height ||
           c.pipeline.depth || c.pipeline.stencil || c.pipeline.blend || c.pipeline.alpha_test ||
           !c.pipeline.scissors.empty() || c.pipeline.fill!=0x1b02) { ++out.rejected_state; continue; }
        // A shader pair is not valid Vulkan until HLSL->SPIR-V and descriptor/state
        // lowering are performed. Never substitute the FVF fixture shader silently.
        if(c.shaders) { ++out.rejected_programs; continue; }
        if(c.topology!=4 || c.vertices.empty() || c.vertices.size()>200000 ||
           c.indices.empty() || c.indices.size()>600000 || c.indices.size()%3) {
            ++out.rejected_geometry; continue;
        }
        if(std::any_of(c.vertices.begin(),c.vertices.end(),[](const auto& v) {
            return !Finite(v.position) || !Finite(v.color) || v.position[3]<=0;
        })) { ++out.rejected_geometry; continue; }
        if(std::any_of(c.indices.begin(),c.indices.end(),[&](auto index) {
            return index>=c.vertices.size();
        })) { ++out.rejected_geometry;continue; }
        if(out.vertices.size()+c.vertices.size()>200000 || out.indices.size()+c.indices.size()>600000)
            { ++out.rejected_geometry; continue; }
        const auto base=std::uint32_t(out.vertices.size());
        const auto first=std::uint32_t(out.indices.size());
        for(const auto& v:c.vertices) {
            // The source has already normalized Xbox vertex formats through gpu::Assemble.
            out.vertices.push_back({v.position[0],v.position[1],v.position[2],v.position[3],
                                    v.color[0],v.color[1],v.color[2],v.color[3]});
        }
        for(const auto idx:c.indices) out.indices.push_back(base+idx);
        out.batches.push_back({first,std::uint32_t(c.indices.size())});
        ++out.accepted_draws;
    }
    return out;
}
}
