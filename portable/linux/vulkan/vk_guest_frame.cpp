#include "vk_guest_frame.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pgr::vk {
namespace {
void Quad(Frame& frame,float x,float y,float w,float h,float z,const std::array<float,4>& color) {
    const auto base=std::uint32_t(frame.vertices.size());
    for(auto [px,py]:{std::pair{x,y},std::pair{x+w,y},std::pair{x+w,y+h},std::pair{x,y+h}})
        frame.vertices.push_back({px,py,z,1,color[0],color[1],color[2],color[3]});
    const auto first=std::uint32_t(frame.indices.size());
    for(auto index:{0u,1u,2u,0u,2u,3u}) frame.indices.push_back(base+index);
    frame.batches.push_back({first,6});
    ++frame.accepted_draws; ++frame.input_draws;
}
}
Frame Fixture(std::uint64_t i,std::uint32_t w,std::uint32_t h) {
    if(w<16 || h<16 || w>16384 || h>16384) throw std::invalid_argument("Invalid Vulkan fixture size");
    Frame frame;
    frame.clear={0.065f,0.095f,0.14f,1};
    Quad(frame,0,float(h)*.74f,float(w),float(h)*.26f,.8f,{.12f,.17f,.22f,1});
    const float x=float(w)*(.15f+.06f*std::sin(double(i)*.018));
    const float y=float(h)*.48f;
    Quad(frame,x,y,float(w)*.64f,float(h)*.21f,.4f,{.77f,.16f,.14f,1});
    Quad(frame,x+float(w)*.11f,y-float(h)*.11f,float(w)*.35f,float(h)*.12f,.35f,{.48f,.65f,.72f,1});
    Quad(frame,x+float(w)*.13f,y+float(h)*.14f,float(w)*.12f,float(h)*.13f,.32f,{.045f,.055f,.07f,1});
    Quad(frame,x+float(w)*.47f,y+float(h)*.14f,float(w)*.12f,float(h)*.13f,.32f,{.045f,.055f,.07f,1});
    return frame;
}
}
