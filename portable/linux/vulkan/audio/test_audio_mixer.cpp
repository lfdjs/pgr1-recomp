// Compile the exact generated mixer used by pgr1_vk_game. Unused HLE entry
// points are discarded by the linker; only guest memory/event allocation is
// stubbed here. PCM decoding, voice cursors, routing and mixing are production.
#include PGR1_DSOUND_OVERLAY
#include <cassert>
#include <array>
#include <bit>
#include <cstdio>

namespace app { const GameInfo kGame{}; }
namespace kernel {
std::array<uint8_t,16384> test_memory{};
uint8_t* g_base=test_memory.data();
ObjectRef FromHandle(uint32_t) { return {}; }
int32_t SetEvent(Object&) { return 0; }
}
namespace mem {
void FreeSystem(uint32_t) {}
void FreeContiguous(uint32_t) {}
}
namespace logx { void Write(Level,const char*,...) {} }

namespace {
constexpr uint32_t kPcm=1024,kStack=4096;
X86Context ctx{};
Args Arguments(std::initializer_list<uint32_t> values) {
    ctx.esp=kStack;
    unsigned i=0;
    for(auto value:values)W32(kStack+4+4*i++,value);
    return Args{ctx};
}
bool Near(float a,float b,float epsilon=0.00001f) { return std::fabs(a-b)<epsilon; }
float Output() {
    float samples[16];Render(samples,8);
    return samples[14]+samples[15];
}
Voice& Source(uint32_t id,bool stereo=false) {
    auto& v=g_voices[id];v=Voice{};v.obj=id;
    v.channels=stereo?2:1;v.align=stereo?4:2;
    v.data=kPcm;v.bytes=v.play_len=v.loop_len=32;
    v.playing=v.looping=true;
    return v;
}
Voice& Bus(uint32_t id) {
    auto& v=g_voices[id];v=Voice{};v.obj=id;
    v.is3d=true;v.xbox_creation_flags=0x2010;
    v.min_dist=1;v.max_dist=10000;v.p[2]=1;
    return v;
}
}

int main() {
    // Every PCM sample is +0.5: exact output comparisons also catch direct
    // copies accidentally escaping the destination's distance/volume controls.
    for(unsigned i=0;i<32;i++)W16(kPcm+2*i,16384);
    auto& engine=Source(1);auto& spatial=Bus(2);
    const float direct=Output();assert(Near(direct,0.5f));
    assert(SetOutput(engine,2)==0);assert(spatial.refs==2 && spatial.playing);
    const float close=Output();assert(Near(close,direct));
    spatial.p[2]=20;
    const float far=Output();
    std::printf("PCM route comparison: submix=%d near=%.6f distance20=%.6f ratio=%.6f\n",
                int(SubmixEnabled()),close,far,far/close);
    if(!SubmixEnabled()) {
        assert(Near(far,close)); // reproduce the original direct-output bypass
        return 0;
    }
    assert(Near(far,close/20));
    spatial.p[2]=1;Output();spatial.p[2]=20;
    float ramp[16];Render(ramp,8);
    assert(ramp[0]+ramp[1]>ramp[14]+ramp[15]);
    assert(Near(ramp[14]+ramp[15],far));
    hle_IDirectSound_SetPosition(ctx,Arguments({0,0,0,std::bit_cast<uint32_t>(19.0f),0}));
    assert(Near(Output(),close)); // listener movement uses the same authored distance
    hle_IDirectSound_SetPosition(ctx,Arguments({0,0,0,0,0}));
    buf_SetMinDistance(spatial,Arguments({2,std::bit_cast<uint32_t>(2.0f),0}));
    assert(Near(Output(),close/10));
    buf_SetMinDistance(spatial,Arguments({2,std::bit_cast<uint32_t>(1.0f),0}));
    spatial.p[2]=200;assert(Near(Output(),close/200));
    buf_SetPosition(spatial,Arguments({2,0,0,std::bit_cast<uint32_t>(1.0f),0}));
    buf_SetVolume(spatial,Arguments({2,uint32_t(-2000)}));
    assert(Near(Output(),close*0.1f));
    buf_SetVolume(spatial,Arguments({2,0}));
    spatial.p[0]=1;spatial.p[2]=0;
    float ears[16];Render(ears,8);
    assert(Near(ears[14],0) && ears[15]>0);
    spatial.p[0]=0;spatial.p[2]=1;
    buf_Pause(engine,Arguments({1,1}));assert(Near(Output(),0));
    buf_Pause(engine,Arguments({1,0}));assert(Near(Output(),close));
    buf_Pause(spatial,Arguments({2,1}));assert(Near(Output(),0));
    buf_Pause(spatial,Arguments({2,0}));assert(Near(Output(),close));
    buf_Stop(engine,Arguments({1}));
    assert(!spatial.playing && Near(Output(),0));
    buf_Play(engine,Arguments({1,0,0,1}));assert(Near(Output(),close));
    auto& second=Source(3);assert(SetOutput(second,2)==0);
    assert(Near(Output(),2*close));
    buf_Stop(engine,Arguments({1}));assert(spatial.playing);
    assert(Near(Output(),close));
    buf_Stop(second,Arguments({3}));assert(!spatial.playing);
    assert(Near(Output(),0));
    // Routing is a retained reference; a game releasing the submix's own
    // handle does not destroy it while source voices still use it.
    assert(ReleaseBuffer(spatial)==2);
    assert(SetOutput(engine,0)==0 && Find(2)->refs==1);
    assert(SetOutput(second,0)==0 && !Find(2));
    // A non-looping source naturally stopping also stops its empty parent.
    auto& short_bus=Bus(8);auto& short_voice=Source(9);
    short_voice.looping=false;short_voice.play_len=4;
    assert(SetOutput(short_voice,8)==0);
    float tail[16];Render(tail,8);
    assert(Near(tail[0]+tail[1],close) && Near(tail[2]+tail[3],close));
    assert(Near(tail[14]+tail[15],0)); // keep the final PCM before stopping the bus
    assert(!short_voice.playing && !short_bus.playing);
    assert(ReleaseBuffer(short_voice)==0);assert(short_bus.refs==1);
    assert(ReleaseBuffer(short_bus)==0);
    // A discontinuous stream drains and completes its packet exactly once,
    // then releases the auto-played parent. It cannot leave persistent sound.
    auto& stream_bus=Bus(10);auto& stream=Source(11);
    stream.stream=true;stream.playing=false;stream.active=stream.ending=true;
    stream.packets.push_back({kPcm,4,512,516,0});
    assert(SetOutput(stream,10)==0);Output();
    assert(stream.packets.empty() && !stream.active && !stream_bus.playing);
    assert(R32(512)==4 && R32(516)==0);assert(Near(Output(),0));
    assert(SetOutput(stream,0)==0);g_voices.erase(11);ReleaseBuffer(stream_bus);
    auto& outer=Bus(4);auto& inner=Bus(5);
    assert(SetOutput(inner,4)==0);
    assert(SetOutput(outer,5)==0x88780032u); // reject cycles without altering route
    assert(SetOutput(engine,5)==0);
    buf_Play(engine,Arguments({1,0,0,1}));
    outer.p[2]=10;assert(Near(Output(),close/10));
    outer.paused=true;assert(Near(Output(),0));outer.paused=false;
    // Unrelated stereo music retains separate channels and its ordinary gain.
    engine.playing=false;inner.playing=outer.playing=false;
    for(unsigned i=0;i<16;i++){W16(kPcm+4*i,16384);W16(kPcm+4*i+2,uint16_t(-8192));}
    auto& music=Source(6,true);const float music_before=Output();
    float stereo[16];Render(stereo,8);assert(stereo[14]>0 && stereo[15]<0);
    outer.p[2]=1000;outer.volume=0;
    assert(Near(Output(),music_before));
    music.playing=false;
    for(unsigned i=0;i<32;i++)W16(kPcm+2*i,16384);
    // Production XACT block copies partial updates and uses millibels. PGR1's
    // current symbol inventory does not bind those methods; this covers their
    // actual HLE/helper path without claiming they are Quick Race engines.
    auto& xact=Source(7);xact.has_xact3d=xact.use_xact3d=true;
    W32(512,0x11);W32(516,uint32_t(-2000));W32(512+0x1c,uint32_t(-600));
    buf_Set3DVoiceData(xact,Arguments({7,512}));
    assert(xact.xact3d.distance==-2000 && xact.xact3d.direct==-600);
    const float attenuated=Output();
    assert(Near(attenuated,direct*std::pow(10.0f,-2600.0f/2000.0f)));
    W32(512,0x02);W32(520,uint32_t(-1000));buf_Set3DVoiceData(xact,Arguments({7,512}));
    assert(xact.xact3d.distance==-2000 && xact.xact3d.cone==-1000);
    buf_Use3DVoiceData(xact,Arguments({7,0}));assert(Near(Output(),direct));
    std::printf("Production audio mixer: PCM submix routing, distance, pan, volume, pause/stop/release, nested buses, stereo isolation and XACT partial updates passed.\n");
}
