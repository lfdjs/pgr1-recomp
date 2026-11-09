// Constant semantics shared by native OpenGL and Zink/Vulkan.
#include "../draw_constants.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>

int main() {
    gpu::State state;
    state.viewport={0,0,640,480,0,1};
    for(auto& matrix:state.transforms) for(unsigned i=0;i<4;i++) matrix[i*4+i]=1;
    state.material={1,.5f,.25f,1, .1f,.2f,.3f,1, .4f,.5f,.6f,1, .7f,.8f,.9f,1,32};
    state.rs[gpu::kRsFogEnable]=1;
    state.rs[gpu::kRsFogTableMode]=0;
    state.rs[gpu::kRsFogColor]=0xff804020;
    state.simple[gpu::kMColorWriteEnable/4]=0x01010101;
    state.simple[gpu::kMAlphaTestEnable/4]=1;
    state.simple[gpu::kMAlphaRef/4]=127;
    state.simple[gpu::kMAlphaFunc/4]=0x204;
    state.rs[gpu::kRsZEnable]=2;
    state.zscale=65535;
    state.inv_wfar=.125f;

    auto fixed=pgr::gldirect::Constants(state,640,480,false);
    // Identity world/view/projection and a full viewport remain identity in FFP.
    for(unsigned r=0;r<4;r++) for(unsigned c=0;c<4;c++)
        assert(fixed.wvp[r*4+c]==(r==c?1:0));
    assert(fixed.mat[0][1]==.5f && fixed.mat[4][0]==32);
    assert(fixed.wdepth[2]==1.0f/65536.0f);
    assert(fixed.wdepth[3]==65535*.125f);
    // The largest D16 integer occupies its own quantization step below 1.0.
    assert(std::floor(65535.f)*fixed.wdepth[2]==1.f-1.f/65536.f);
    assert(fixed.hud[3]==1 && fixed.surf[0]==640 && fixed.surf[1]==480);

    auto program=pgr::gldirect::Constants(state,640,480,true);
    assert(program.wdepth[3]==1);
    assert(program.fog[0]==1 && program.fog[1]==0 && program.fog[3]==3);
    assert(program.misc[0]==127 && program.misc[1]==4);
    assert(program.fog_color[0]==128.f/255 && program.fog_color[1]==64.f/255);
    // Fixed-function state changes must not alter a programmable draw's DCs:
    // its vertex program takes transforms/light values from State::vsc instead.
    for(auto& matrix:state.transforms) matrix.fill(123);
    state.material.fill(456);
    state.light_enable=255;
    state.rs[gpu::kRsLighting]=1;
    auto changed=pgr::gldirect::Constants(state,640,480,true);
    assert(std::memcmp(&program,&changed,sizeof(program))==0);
    state.zscale=16777215;
    program=pgr::gldirect::Constants(state,640,480,true);
    assert(program.wdepth[2]==1.0f/16777216.0f);
    std::puts("Draw constants: fixed transforms/material, programmable state isolation, D16/D24 W-depth steps passed.");
}
