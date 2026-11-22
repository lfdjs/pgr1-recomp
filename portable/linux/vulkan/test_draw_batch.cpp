#include "draw_batch.h"
#include <cassert>
#include <vector>
using namespace pgr::vulkan::batch;
int main() {
    using namespace pgr::gldirect;
    Command a; a.kind=Command::Kind::Draw; a.target.color=0x1000;
    Command b=a,c=a;
    std::vector<Command> v{a,b,c};
    assert(CandidateEnd(v,0)==3);
    assert(CandidateEnd(v,0,2)==2);
    assert(!Eligible(Command{}));
    v[1].textures[0].render_target=0x2000;
    assert(CandidateEnd(v,0)==1);
    v[1]=b;v[1].textures[2].face_targets[3]=0x3000;
    assert(CandidateEnd(v,0)==1);
    v[1]=b;v[2].target.width=800;
    assert(CandidateEnd(v,0)==2);
    v[2]=c;v[2].kind=Command::Kind::Clear;
    assert(CandidateEnd(v,0)==2);
    v[2]=c;v[2].target.depth=0xABC0;
    assert(CandidateEnd(v,0)==2);
    v[2]=c;v[1].kind=Command::Kind::Readback;
    assert(CandidateEnd(v,0)==1);
    assert(CandidateEnd(v,3)==3);
}
