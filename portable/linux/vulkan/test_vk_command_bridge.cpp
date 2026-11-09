#include "vk_command_bridge.h"
#include "../gldirect/commands.h"
#include <cassert>
#include <cmath>
#include <memory>
#include <iostream>
int main() {
    namespace gl=pgr::gldirect;
    gl::Submission s{};
    s.front=1234;
    gl::Command clear{};
    clear.kind=gl::Command::Kind::Clear;
    clear.target.color=s.front;
    clear.flags=0xf0;
    clear.color=0xff4080ff;
    clear.rects.push_back({0,0,640,480});
    s.commands.push_back(clear);
    gl::Command draw{};
    draw.kind=gl::Command::Kind::Draw;
    draw.target.color=s.front;
    draw.target.width=640;draw.target.height=480;
    draw.topology=4;draw.pipeline.fill=0x1b02;
    for(int i=0;i<3;i++) {
        gl::Vertex v{};
        v.position={10.0f+float(i)*5.0f,30.0f,0.5f,1};
        v.color={1,0,0,1};
        draw.vertices.push_back(v);
    }
    draw.indices={0,1,2};
    s.commands.push_back(draw);
    auto f=pgr::vk::FromSubmission(s,640,480);
    assert(f.input_draws==1 && f.accepted_draws==1);
    assert(f.indices.size()==3 && f.vertices.size()==3);
    assert(f.batches.size()==1 && f.batches[0].index_count==3);
    assert(std::abs(f.clear[0]-64.0f/255.0f)<0.00001f);
    assert(std::abs(f.clear[1]-128.0f/255.0f)<0.00001f);
    assert(std::abs(f.clear[2]-1.0f)<0.00001f);
    s.commands.back().shaders=std::make_shared<gl::ShaderPair>();
    auto rejected=pgr::vk::FromSubmission(s,640,480);
    assert(rejected.input_draws==1 && rejected.accepted_draws==0 && rejected.rejected_programs==1);
    s.commands.back().shaders.reset();
    s.commands.back().indices={0,1,45};
    auto bad=pgr::vk::FromSubmission(s,640,480);
    assert(bad.rejected_geometry==1 && bad.accepted_draws==0);
    std::cout<<"Vulkan SDK-command adapter subset OK\n";
}
