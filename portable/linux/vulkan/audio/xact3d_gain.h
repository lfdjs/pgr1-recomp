#pragma once
// PGR1 native Linux: consume the XACT-calculated 3D levels handed to Xbox
// DirectSound. The title supplies millibel gains for distance/cone/direct.
// This helper has no SDK/Vulkan dependencies; it is unit-tested separately.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace pgr::vulkan::audio3d {
struct Data {
    std::int32_t distance=0, cone=0, front=0, rear=0;
    std::int32_t crosstalk=0, centre=0, direct=0, room=0, doppler=0;
    float azimuth=0.0f, elevation=0.0f;
    std::uint32_t lowpass[2]={};
    std::uint32_t mask=0;
};

// Partial updates are common: a mask selects valid DWORD offsets, all
// other fields persist from the preceding call. The reader returns raw DWORDs.
template<class Read32>
inline void Merge(Data& d,std::uint32_t flags,Read32 read) {
    const auto mb=[&](std::uint32_t offset){ return std::bit_cast<std::int32_t>(read(offset)); };
    if(flags&0x01u)d.distance=mb(0x04);
    if(flags&0x02u)d.cone=mb(0x08);
    if(flags&0x04u){d.front=mb(0x0c);d.rear=mb(0x10);}
    if(flags&0x08u){d.crosstalk=mb(0x14);d.centre=mb(0x18);}
    if(flags&0x10u){d.direct=mb(0x1c);d.room=mb(0x20);}
    if(flags&0x20u)d.doppler=mb(0x24);
    if(flags&0x40u){d.azimuth=std::bit_cast<float>(read(0x28));d.elevation=std::bit_cast<float>(read(0x2c));}
    if(flags&0x80u){d.lowpass[0]=read(0x30)&0xffffu;d.lowpass[1]=read(0x34)&0xffffu;}
    d.mask|=flags&0xffu;
}

inline float Db(std::int32_t mb) {
    if(mb<=-10000)return 0.0f;
    return std::pow(10.0f,float(std::min(0,mb))/2000.0f);
}

// Xbox 3DVoiceData levels are millibels, not literal scalar volume.
// The optional percent amplifies only distance loss; 100 uses the title's
// authored level. No gain ever exceeds unity. Music/non-3D bypass this.
inline float Gain(const Data& d,unsigned percent) {
    const float distance=float(std::clamp(d.distance,-10000,0));
    const float cone=float(std::clamp(d.cone,-10000,0));
    const float direct=float(std::clamp(d.direct,-10000,0));
    const float strength=float(std::clamp(percent,100u,400u))/100.0f;
    const float mb=std::clamp(distance*strength+cone+direct,-10000.0f,0.0f);
    if(!std::isfinite(mb))return 0.0f;
    return mb<=-10000?0.0f:std::pow(10.0f,mb/2000.0f);
}
}
