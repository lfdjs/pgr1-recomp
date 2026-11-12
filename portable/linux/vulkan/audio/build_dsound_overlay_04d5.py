#!/usr/bin/env python3
"""Native DirectSound overlay: XACT data and retained mono submix routing.

Build-only: reads pinned SDK source; first applies existing 04D.4 rolloff
and then adds XACT precomputed millibel blocks. Never modifies sdk/.
The XACT data layout is documented by the upstream SDK's experimental PR #9.
"""
import argparse
from pathlib import Path
from build_dsound_overlay import transform as base_transform


def once(s, old, new, name):
    count=s.count(old)
    if count != 1:
        raise ValueError(f'04D.5 {name}: expected exactly one SDK anchor; found {count}')
    return s.replace(old,new,1)


def upgrade(s):
    s=once(s,'#include "quality_state.h"\n',
        '#include "quality_state.h"\n#include "xact3d_gain.h"\n#include <cstdlib>\n', 'XACT helper include')
    s=once(s,'    bool is3d = false;\n',
'''    bool is3d = false;
    // XACT may create a voice with 0x400000 but without DSBCAPS_CTRL3D.
    // Its precomputed 3D distance is supplied by Set3DVoiceData, not SetPosition.
    uint32_t xbox_creation_flags=0;
    bool has_xact3d=false, use_xact3d=false;
    pgr::vulkan::audio3d::Data xact3d{};
    uint32_t xact3d_updates=0;
''','Voice XACT state')
    s=once(s,'float g_listener[3] = {}, g_front[3] = {0, 0, 1}, g_top[3] = {0, 1, 0};',
'''float g_listener[3] = {}, g_front[3] = {0, 0, 1}, g_top[3] = {0, 1, 0};
// A/B switch: only XACT 3D levels, never global sound output.
bool Xact3dEnabled() {
    static bool active=[] {
        const char* value=std::getenv("PGR1_VK_XACT_3D");
        return !value || *value!='0';
    }();
    return active;
}
void ReadXactData3d(Voice& v,uint32_t address);
''','mixer XACT switch and declaration')
    s=once(s,'    if (!v.is3d || v.mode == 2) return;',
'''    // XACT's 3D calculator already computed the distance/cone/direct loss.
    // For this path, do not also run the legacy positional distance formula.
    if (Xact3dEnabled() && v.has_xact3d && v.use_xact3d && v.xact3d.mask && v.mode!=2) {
        const unsigned spatial=pgr::vulkan::quality::spatial_percent.load(std::memory_order_relaxed);
        const float gain=pgr::vulkan::audio3d::Gain(v.xact3d,spatial);
        // The XACT data carries azimuth in degrees; keep side-to-side
        // positioning even when the voice lacks DSBCAPS_CTRL3D. This is a
        // stereo approximation, not a replacement for the Xbox HRTF.
        float pan=0;
        if((v.xact3d.mask&0x40u) && std::isfinite(v.xact3d.azimuth)) {
            pan=std::clamp(std::sin(v.xact3d.azimuth*0.017453292519943295f),-1.0f,1.0f);
        } else if(v.is3d) {
            float delta[3]={v.p[0],v.p[1],v.p[2]};
            if(v.mode!=1)for(unsigned axis=0;axis<3;axis++)delta[axis]-=g_listener[axis];
            const float len=std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
            const float right[3]={g_top[1]*g_front[2]-g_top[2]*g_front[1],
                g_top[2]*g_front[0]-g_top[0]*g_front[2],g_top[0]*g_front[1]-g_top[1]*g_front[0]};
            if(std::isfinite(len) && len>1e-3f)
                pan=std::clamp((v.mode==1?delta[0]:delta[0]*right[0]+delta[1]*right[1]+delta[2]*right[2])/len,-1.0f,1.0f);
        }
        for(uint32_t channel=0;channel<kMixChannels;channel++) {
            g[channel][0]*=gain*std::min(1.0f,1.0f-pan);
            g[channel][1]*=gain*std::min(1.0f,1.0f+pan);
        }
        return;
    }
    if (!v.is3d || v.mode == 2) return;''','XACT gain in mixer')
    f32='''float F32(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}'''
    s=once(s,f32,f32+'''
// The original Xbox XACT Set3DVoiceData layout is flags at +0, then the
// indicated millibel fields at +0x04..+0x24. This is not a DS3DBUFFER.
// Read only fields whose update bits are present; later partial writes retain
// the last complete value. The guest memory belongs to the call and is copied.
void ReadXactData3d(Voice& v,uint32_t address) {
    if(!address || !v.has_xact3d)return;
    const uint32_t changed=R32(address)&0xffu;
    pgr::vulkan::audio3d::Merge(v.xact3d,changed,
        [&](uint32_t offset){return R32(address+offset);});
    ++v.xact3d_updates;
}
''','copy XACT delta fields')
    creation='    v.is3d = flags & 0x10;  // DSBCAPS_CTRL3D'
    if s.count(creation)!=2:raise ValueError('04D.5 buffer/stream creation anchors changed')
    s=s.replace(creation,creation+'''
    v.xbox_creation_flags=flags;
    // The 0x400000 flag allocates an Xbox XACT 3D data block, even when the
    // normal DirectSound 3D bit is absent. This was previously ignored.
    v.has_xact3d=(flags&0x400010u)!=0;
    v.use_xact3d=v.has_xact3d;''')
    s=once(s,'BUFFER(Use3DVoiceData, 2) { return 0; }',
'''BUFFER(Use3DVoiceData, 2) { v.use_xact3d=v.has_xact3d && a[1]!=0; return 0; }''','buffer XACT voice toggle')
    s=once(s,'BUFFER(Set3DVoiceData, 2) { return 0; }',
'''BUFFER(Set3DVoiceData, 2) { ReadXactData3d(v,a[1]); return 0; }''','buffer XACT data')
    s=once(s,'HLE_STDCALL(IDirectSoundStream_Use3DVoiceData, 2) { return 0; }',
'''HLE_STDCALL(IDirectSoundStream_Use3DVoiceData, 2) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Voice* v=Find(a[0]);
    if(!v)return 0x80004005u;
    v->use_xact3d=v->has_xact3d && a[1]!=0;
    return 0;
}''','stream XACT voice toggle')
    s=once(s,'HLE_STDCALL(IDirectSoundStream_Set3DVoiceData, 2) { return 0; }',
'''HLE_STDCALL(IDirectSoundStream_Set3DVoiceData, 2) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Voice* v=Find(a[0]);
    if(!v)return 0x80004005u;
    ReadXactData3d(*v,a[1]);
    return 0;
}''','stream XACT data')
    # Extend the existing 5s voice report, without changing the audio callback
    # cadence, mixing algorithm or locking strategy. Log only active XACT voices.
    s=once(s,'        LOGR("dsound: %u voices playing, loud:%s", n, top.c_str());',
'''        LOGR("dsound: %u voices playing, loud:%s", n, top.c_str());
        if(const char* trace=std::getenv("PGR1_AUDIO_3D_TRACE");trace && *trace=='1') {
            uint32_t listed=0, with_blocks=0, updated=0, active=0;
            for(const auto& [id,v]:g_voices) {
                if(v.paused || v.sync || (!v.stream && !v.playing))continue;
                ++active;
                if(v.has_xact3d)++with_blocks;
                if(v.xact3d_updates)++updated;
                if(v.has_xact3d && listed++<16) {
                    const auto gain=pgr::vulkan::audio3d::Gain(v.xact3d,
                        pgr::vulkan::quality::spatial_percent.load(std::memory_order_relaxed));
                    LOGR("pgr1 audio3d: voice=%08X stream=%d flags=%08X xyz3d=%d xact=%d on=%d updates=%u mask=%02X dist_mb=%d cone_mb=%d direct_mb=%d gain=%.5f",
                        id,int(v.stream),v.xbox_creation_flags,int(v.is3d),int(v.has_xact3d),
                        int(v.use_xact3d),v.xact3d_updates,v.xact3d.mask,
                        v.xact3d.distance,v.xact3d.cone,v.xact3d.direct,gain);
                }
            }
            LOGR("pgr1 audio3d: active=%u xact_blocks=%u updated=%u xact_enabled=%d",
                active,with_blocks,updated,int(Xact3dEnabled()));
        }''','audio trace summary')
    return s


def diagnostics(s):
    s=once(s,'    uint32_t xact3d_updates=0;\n','    uint32_t xact3d_updates=0;\n    uint32_t creation_caller=0, position_updates=0, volume_updates=0;\n    int32_t requested_mb=0;\n', 'trace voice fields')
    s=once(s, 'bool DsoundLog() {', r'''bool AudioTrace() {
    static const bool on=[] { const char* e=std::getenv("PGR1_AUDIO_TRACE"); return e && *e && *e!='0'; }();
    return on;
}
// Bounded control-thread event log, never one line per sample or packet.
void TraceEvent(const Voice& v,const char* event,uint32_t arg,uint32_t caller) {
    if(!AudioTrace())return;
    static unsigned events=0;
    if(events>=1024)return;
    if(std::strcmp(event,"GetStatus")==0 || std::strcmp(event,"GetCurrentPosition")==0 ||
       std::strcmp(event,"Process")==0 || std::strcmp(event,"Unlock")==0 ||
       std::strcmp(event,"SetVelocity")==0 || std::strcmp(event,"SetConeOrientation")==0 ||
       std::strcmp(event,"SetFrequency")==0 || std::strcmp(event,"SetPosition")==0 ||
       std::strcmp(event,"SetVolume")==0)return;
    if(std::strcmp(event,"Pause")==0 && v.paused==(arg==1) && v.sync==(arg==2))return;
    ++events;
    LOGR("pgr1 audio event: voice=%08X event=%s arg=%08X caller=%08X refs=%d playing=%d paused=%d packets=%zu",
        v.obj,event,arg,caller,v.refs,int(v.playing),int(v.paused),v.packets.size());
}
bool DsoundLog() {''', 'trace helper')
    s=once(s,'    const bool log_voices = DsoundLog();','    const bool log_voices = DsoundLog() || AudioTrace();','trace independent of legacy logging')
    s=once(s,'    if (!DsoundLog()) return;\n    LOGI("dsound: %s', '    if (!DsoundLog() && !AudioTrace()) return;\n    LOGR("dsound: %s', 'trace creation')
    s=once(s,'    if (!DsoundLog() && !AudioTrace()) return;',r'''    if (!DsoundLog() && !AudioTrace()) return;
    static unsigned traced_creations=0;
    if(AudioTrace() && !DsoundLog() && traced_creations++>=1024)return;''','bounded creation trace')
    s=s.replace('    v.xbox_creation_flags=flags;', '    v.xbox_creation_flags=flags;\n    v.creation_caller=caller;')
    s=once(s,'        LOGR("dsound: %u voices playing, loud:%s", n, top.c_str());',r'''        LOGR("dsound: %u voices playing, loud:%s", n, top.c_str());
        if(AudioTrace()) {
            uint32_t shown=0;
            for(unsigned trace_pass=0;trace_pass<2;++trace_pass)
            for(auto& [id,v]:g_voices) {
                if(IsSubmix(v)!=(trace_pass==0))continue;
                if((v.stream?v.packets.empty():!v.playing) && !IsSubmix(v) && !v.position_updates)continue;
                float gain[kMixChannels][2]; Levels(v,gain);
                if(!IsSubmix(v) && gain[0][0]+gain[0][1]<1e-8f)continue;
                if(shown++>=64)break;
                const float dx=v.p[0]-(v.mode==1?0:g_listener[0]);
                const float dy=v.p[1]-(v.mode==1?0:g_listener[1]);
                const float dz=v.p[2]-(v.mode==1?0:g_listener[2]);
                char bins[192]={}; size_t at=0;
                for(unsigned i=0;i<v.nbins && at<sizeof(bins)-24;i++)
                    at+=std::snprintf(bins+at,sizeof(bins)-at," %u:%d",unsigned(v.bin[i]),v.bin_mb[v.bin[i]]);
                LOGR("pgr1 audio voice: id=%08X caller=%08X stream=%d flags=%08X tag=%X ch=%u data=%08X playing=%d paused=%d sync=%d packets=%zu xyz=%.3f,%.3f,%.3f listener=%.3f,%.3f,%.3f dist=%.3f min=%.3f max=%.3f mode=%u mb=%d head=%.4f gain=%.6f/%.6f positions=%u volumes=%u xact=%u bins=%s",
                     id,v.creation_caller,int(v.stream),v.xbox_creation_flags,v.tag,unsigned(v.channels),v.data,
                     int(v.playing),int(v.paused),int(v.sync),v.packets.size(),v.p[0],v.p[1],v.p[2],
                     g_listener[0],g_listener[1],g_listener[2],std::sqrt(dx*dx+dy*dy+dz*dz),v.min_dist,v.max_dist,v.mode,
                     v.requested_mb,v.headroom,gain[0][0],gain[0][1],v.position_updates,v.volume_updates,v.xact3d_updates,bins);
            }
        }''','trace full voice report')
    line='Voice* v = Find(a[0]);                              '
    s=s.replace(line+'\\'+'\n',line+'\\'+'\n        if(v)TraceEvent(*v,#name,nargs>1?a[1]:0,R32(ctx.esp)); '+'\\'+'\n')
    s=s.replace('    v.volume = Db(int32_t(a[1]));','    ++v.volume_updates; v.requested_mb=int32_t(a[1]);\n    v.volume = Db(int32_t(a[1]));')
    s=s.replace('    for (int i = 0; i < 3; i++) v.p[i] = F32(a[1 + i]);','    ++v.position_updates;\n    for (int i = 0; i < 3; i++) v.p[i] = F32(a[1 + i]);')
    return s


def submix(s):
    s=once(s,'    int32_t requested_mb=0;\n',r'''    int32_t requested_mb=0;
    uint32_t output=0; // SetOutputBuffer holds a reference to this mix-in voice.
    bool gain_valid=false;
    float previous_gain[6][2]={};
    // SDL pulls at most 1024 frames. Fixed storage avoids callback allocations.
    static constexpr uint32_t kInputFrames=1024;
    float input[kInputFrames]={};
    bool input_pending=false;
''','mix-in state')
    marker='// gain of voice channel c on host left and right'
    pos=s.index(marker)
    s=s[:pos]+r'''bool IsSubmix(const Voice& v) { return !v.stream && (v.xbox_creation_flags&0x2000u)!=0; }
Voice* MixDestination(const Voice& v) {
    const auto it=g_voices.find(v.output);
    return it!=g_voices.end() && IsSubmix(it->second)?&it->second:nullptr;
}
bool SubmixEnabled() {
    static bool on=[] {const char* e=std::getenv("PGR1_AUDIO_SUBMIX"); return !e || *e!='0';}();
    return on;
}
unsigned SubmixDepth(const Voice& v) {
    const Voice* at=&v; unsigned depth=0;
    while((at=MixDestination(*at)) && depth<8)++depth;
    return depth;
}
void StartSubmixes(const Voice& v) {
    Voice* to=MixDestination(v);
    for(unsigned hops=0;to && hops<8;to=MixDestination(*to),++hops)to->playing=to->looping=true;
}
void StopSubmixes(const Voice& v) {
    Voice* to=MixDestination(v);
    for(unsigned hops=0;to && hops<8;to=MixDestination(*to),++hops) {
        bool running=false;
        for(const auto& [obj,source]:g_voices)
            if(source.output==to->obj && (source.stream?source.active:source.playing)) {running=true;break;}
        if(running)return;
        to->playing=false;
    }
}

'''+s[pos:]
    # Extract the existing PCM cursor/decoder loop. Its behaviour is unchanged;
    # only its output can now be another voice's input instead of the speakers.
    begin=s.index('    for (auto& [obj, v] : g_voices) {\n        if (v.paused || v.sync || (!v.stream && !v.playing)) continue;',s.index('static const uint32_t solo_obj'))
    end=s.index('    for (uint32_t i = 0; i < frames * 2; i++) out[i] = std::clamp',begin)
    loop=s[begin:end]
    loop=loop.replace('        if (solo_tag >= 0 && long(v.tag) != solo_tag) continue;','        if (!IsSubmix(v) && solo_tag >= 0 && long(v.tag) != solo_tag) continue;')
    loop=loop.replace('        if (solo_obj && obj != solo_obj) continue;','        if (!IsSubmix(v) && solo_obj && obj != solo_obj) continue;')
    loop=loop.replace('        float g[kMixChannels][2];\n        Levels(v, g);',r'''        Voice* destination=SubmixEnabled()?MixDestination(v):nullptr;
        const bool mixin=SubmixEnabled() && IsSubmix(v);
        if(mixin && frames>Voice::kInputFrames)continue;
        float g[kMixChannels][2];
        Levels(v, g);
        // Xbox SetOutputBuffer replaces speaker bins with the multipass send,
        // at 1/channel. No dry stereo copy may bypass the positioned submix.
        if(destination) {
            const float gain=v.volume*v.headroom/float(std::max<uint16_t>(v.channels,1));
            for(unsigned c=0;c<kMixChannels;c++)g[c][0]=g[c][1]=gain;
        }''')
    loop=loop.replace('            if (v.stream) {','            if(mixin) {\n                from=next_from=0; at=next=0;\n            } else if (v.stream) {',1)
    loop=loop.replace('                float a = Sample(v, from, at, int(c)), a1 = Sample(v, next_from, next, int(c));','                float a = mixin?v.input[i]:Sample(v, from, at, int(c));\n                float a1 = mixin?a:Sample(v, next_from, next, int(c));')
    loop=loop.replace('            out[i * 2] += l;\n            out[i * 2 + 1] += r;',r'''            if(destination && i<Voice::kInputFrames) {destination->input[i]+=l;destination->input_pending=true;}
            else { out[i * 2] += l; out[i * 2 + 1] += r; }''')
    loop=loop.replace('            v.pos += step;','            if(!mixin)v.pos += step;')
    loop=loop.replace('        if (v.paused || v.sync || (!v.stream && !v.playing)) continue;',r'''        if(pass==9) {if(SubmixEnabled() && IsSubmix(v))continue;}
        else if(!SubmixEnabled() || !IsSubmix(v) || SubmixDepth(v)!=unsigned(pass))continue;
        if (v.paused || v.sync || (!v.stream && !v.playing && !(SubmixEnabled() && IsSubmix(v) && v.input_pending))) continue;''',1)
    pre=r'''    // Voices first, then deeper submixes before their destinations. Eight
    // bounded passes need no per-block vectors/sort/allocation.
    bool has_submix=false;
    for(auto& [obj,v]:g_voices)if(IsSubmix(v)) {has_submix=true;v.input_pending=false;std::fill_n(v.input,std::min(frames,Voice::kInputFrames),0.0f);}
    for(int pass=9;pass>=0;--pass) {
        if(pass<9 && !has_submix)break;
'''
    s=s[:begin]+pre+loop+'    }\n'+s[end:]
    # Reference/lifecycle semantics adapted narrowly from experimental PR #9.
    marker='BUFFER(Release, 1) {'
    pos=s.index(marker)
    finish=s.index('BUFFER(Unlock, 5)',pos)
    s=s[:pos]+r'''static uint32_t ReleaseBuffer(Voice& v);
static uint32_t SetOutput(Voice& v,uint32_t to) {
    if(to==v.output)return 0;
    Voice* dest=to?Find(to):nullptr;
    if(to && (!dest || dest->stream))return 0x88780032u;
    const Voice* at=dest;
    for(unsigned hops=0;at;at=MixDestination(*at),++hops)
        if(at==&v || hops>=8)return 0x88780032u;
    if(dest)++dest->refs;
    const uint32_t old=v.output;
    v.output=to;
    if(old) {
        if(auto* previous=Find(old)) {
            // old bus no longer has this source; keep its other sources alive.
            bool active=false;
            for(const auto& [id,source]:g_voices)
                if(source.output==old && (source.stream?source.active:source.playing))active=true;
            if(!active && IsSubmix(*previous))previous->playing=false;
            ReleaseBuffer(*previous);
        }
    }
    if(v.stream?v.active:v.playing)StartSubmixes(v);
    return 0;
}
static uint32_t ReleaseBuffer(Voice& v) {
    if(--v.refs>0)return uint32_t(v.refs);
    SetOutput(v,0);
    if(v.owned)mem::FreeContiguous(v.owned);
    const uint32_t obj=v.obj;
    g_voices.erase(obj);
    mem::FreeSystem(obj);
    return 0;
}
BUFFER(Release, 1) { return ReleaseBuffer(v); }
'''+s[finish:]
    s=once(s,'BUFFER(SetOutputBuffer, 2) { return 0; }  // the voice keeps its own mix bins',
        'BUFFER(SetOutputBuffer, 2) { return SetOutput(v,a[1]); }','buffer output route')
    # Optional stream export is absent in PGR1; implement if future config binds it.
    if 'STREAM(SetOutputBuffer, 2)' not in s:
        s+='\nSTREAM(SetOutputBuffer, 2) { return SetOutput(v,a[1]); }\n'
    s=s.replace('    v.sync = a[3] & 4;\n    return 0;', '    v.sync = a[3] & 4;\n    StartSubmixes(v);\n    return 0;')
    s=s.replace('    v.playing = false;\n    return 0;', '    v.playing = false;\n    StopSubmixes(v);\n    return 0;')
    s=once(s,'    v.active = true;\n    v.ending = false;','    v.active = true;\n    StartSubmixes(v);\n    v.ending = false;','start stream submix')
    s=once(s,'    Deactivate(v);\n}\n\nSTREAM(AddRef','    Deactivate(v);\n    StopSubmixes(v);\n}\n\nSTREAM(AddRef','flush stream submix')
    s=once(s,'    FlushStream(v);\n    uint32_t obj = v.obj;','    FlushStream(v);\n    SetOutput(v,0);\n    uint32_t obj = v.obj;','release stream route reference')
    s=s.replace('positions=%u volumes=%u xact=%u bins=%s', 'positions=%u volumes=%u xact=%u output=%08X mixin=%d bins=%s')
    s=s.replace('v.position_updates,v.volume_updates,v.xact3d_updates,bins);','v.position_updates,v.volume_updates,v.xact3d_updates,v.output,int(IsSubmix(v)),bins);')
    # Ramp changed levels over the output block (at most 21 ms at 48 kHz),
    # keeping new playback immediate and avoiding discontinuities on movement.
    anchor='        uint32_t base = v.data + v.play_start;'
    s=once(s,anchor,r'''        float gain_step[kMixChannels][2];
        for(unsigned c=0;c<kMixChannels;c++)for(unsigned ear=0;ear<2;ear++) {
            if(!v.gain_valid)v.previous_gain[c][ear]=g[c][ear];
            gain_step[c][ear]=(g[c][ear]-v.previous_gain[c][ear])/float(std::max(frames,1u));
        }
        v.gain_valid=true;
'''+anchor,'block gain ramp state')
    s=once(s,'            float t = float(v.pos - f), l = 0, r = 0;',r'''            for(unsigned c=0;c<kMixChannels;c++)for(unsigned ear=0;ear<2;ear++)
                g[c][ear]=(v.previous_gain[c][ear]+=gain_step[c][ear]);
            float t = float(v.pos - f), l = 0, r = 0;''','per-frame gain ramp')
    s=s.replace('    v.playing = true;\n    v.looping = a[3] & 1;', '    v.gain_valid=false;\n    v.playing = true;\n    v.looping = a[3] & 1;')
    # Stop auto-started buses when PCM ends naturally or a discontinuous stream
    # drains; explicit Stop/Flush and retained-reference release are covered too.
    s=once(s,'    if (v.active) v.active = v.paused = v.ending = false;','    if (v.active) v.active = v.paused = v.ending = false;\n    StopSubmixes(v);','drained stream bus')
    s=once(s,'                        v.playing = false;\n                        v.pos = 0;', '                        v.playing = false;\n                        StopSubmixes(v);\n                        v.pos = 0;', 'natural buffer end bus')
    marker='// a stream goes quiet: the synch hold ends'
    pos=s.index(marker)
    s=s[:pos]+r'''// Effective per-channel transfer through retained submix destinations. Used
// only by periodic diagnostics; Render performs the real bus sum below.
void EffectiveLevels(Voice& v,float gain[kMixChannels][2],unsigned depth=0) {
    Voice* to=SubmixEnabled()?MixDestination(v):nullptr;
    if(!to || depth>=8) {Levels(v,gain);return;}
    float bus[kMixChannels][2];EffectiveLevels(*to,bus,depth+1);
    const float scalar=v.volume*v.headroom/float(std::max<uint16_t>(v.channels,1));
    for(unsigned c=0;c<kMixChannels;c++) {
        gain[c][0]=scalar*bus[0][0];gain[c][1]=scalar*bus[0][1];
    }
}

'''+s[pos:]
    s=s.replace('float gain[kMixChannels][2]; Levels(v,gain);','float gain[kMixChannels][2]; EffectiveLevels(v,gain);')
    return s


def transform(s):
    return submix(diagnostics(upgrade(base_transform(s))))

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('source',type=Path)
    parser.add_argument('destination',type=Path)
    opts=parser.parse_args()
    product=transform(opts.source.read_text(encoding='utf-8'))
    opts.destination.parent.mkdir(parents=True,exist_ok=True)
    opts.destination.write_text(product,encoding='utf-8')
    print('PGR1 audio: XACT data, positional submix routing and bounded trace (SDK unchanged)')
