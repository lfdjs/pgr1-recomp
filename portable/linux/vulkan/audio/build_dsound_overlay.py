#!/usr/bin/env python3
"""Build-only DirectSound overlay for the pinned SDK: never modifies sdk/.

The native PGR1 port uses the SDK's mixer. Xbox listener rolloff calls were
ignored; optional adjustable attenuation applies only to 3D voices. Both are
implemented here in a generated C++ copy. Exact-anchor guarded.
"""
import argparse
from pathlib import Path

def replace_once(s, old, new, label):
    n=s.count(old)
    if n!=1: raise ValueError(f"{label}: incompatible SDK mixer ({n} matches)")
    return s.replace(old,new,1)

def transform(s):
    s=replace_once(s,'#include "app/app.h"\n',
        '#include "app/app.h"\n#include "quality_state.h"\n', 'include quality settings')
    s=replace_once(s, 'float g_listener[3] = {}, g_front[3] = {0, 0, 1}, g_top[3] = {0, 1, 0};',
'''float g_listener[3] = {}, g_front[3] = {0, 0, 1}, g_top[3] = {0, 1, 0};
// Rolloff factor supplied by Xbox DirectSound. The SDK previously ignored
// SetRolloffFactor, which could leave remote engines too loud.
float g_rolloff_factor=1.0f;''', 'listener rolloff state')
    s=replace_once(s, '''    if (dist > v.min_dist && v.curve.empty()) {
        att = v.min_dist / std::min(dist, v.max_dist);
    } else if (dist > v.min_dist && v.max_dist > v.min_dist) {''',
'''    if (dist > v.min_dist && v.curve.empty()) {
        // Default factor 1 reproduces original SDK 1/r attenuation. The
        // console's explicit rolloff factor is now honored (0 = no rolloff).
        const float clamped = std::min(dist,v.max_dist);
        const float rolloff = std::max(0.0f,g_rolloff_factor);
        att = v.min_dist / (v.min_dist + rolloff * (clamped-v.min_dist));
    } else if (dist > v.min_dist && v.max_dist > v.min_dist) {''', 'Xbox rolloff factor')
    s=replace_once(s, '''    float right[3] = {g_top[1] * g_front[2] - g_top[2] * g_front[1], g_top[2] * g_front[0] - g_top[0] * g_front[2],''',
'''    // User-controlled additional far-field attenuation for genuine 3D
    // voices only. Does not touch the player's dry/music/UI stereo streams.
    const unsigned spatial=std::clamp(pgr::vulkan::quality::spatial_percent.load(std::memory_order_relaxed),100u,400u);
    if(spatial>100 && dist>v.min_dist && std::isfinite(dist) && v.min_dist>0) {
        const float away=std::min(dist,std::max(v.min_dist,v.max_dist));
        att*=std::pow(v.min_dist/away,(float(spatial)-100.0f)/100.0f);
    }
    float right[3] = {g_top[1] * g_front[2] - g_top[2] * g_front[1], g_top[2] * g_front[0] - g_top[0] * g_front[2],''', 'optional spatial attenuation')
    s=replace_once(s,
        'HLE_STDCALL(IDirectSound_SetRolloffFactor, 3) { return 0; }',
'''HLE_STDCALL(IDirectSound_SetRolloffFactor, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    const float factor=F32(a[1]);
    if(std::isfinite(factor) && factor>=0 && factor<=100)g_rolloff_factor=factor;
    return 0;
}''', 'previously ignored Xbox SetRolloffFactor')
    return s

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('source',type=Path)
    p.add_argument('destination',type=Path)
    ns=p.parse_args()
    result=transform(ns.source.read_text(encoding='utf-8'))
    ns.destination.parent.mkdir(parents=True,exist_ok=True)
    ns.destination.write_text(result,encoding='utf-8')
    print('PGR1 Vulkan: generated compatible Xbox DirectSound attenuation overlay')
