#include "sdl_audio.h"
#include "audio/audio.h"
#include "core/log.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <mutex>
#include <cmath>
#include <vector>
namespace {
struct Output {
    std::function<void(float*,uint32_t)> render;
    std::vector<float> samples;
    SDL_AudioStream* stream=nullptr;
    uint64_t frames=0; double squares=0; float peak=0;
};
std::mutex mutex;
Output* output=nullptr; // SDK audio callback outlives main; destroyed only after stream is stopped.
void Pull(void* userdata,SDL_AudioStream* stream,int additional,int) {
    auto& out=*static_cast<Output*>(userdata);
    while(additional>0) {
        // A bounded reusable buffer keeps callback allocations out of the steady path.
        unsigned frames=std::min(1024,(additional+7)/8);
        out.render(out.samples.data(),frames);
        out.frames+=frames;
        for(unsigned i=0;i<frames*2;i++) { const float v=out.samples[i]; out.peak=std::max(out.peak,std::abs(v)); out.squares+=double(v)*v; }
        if(!SDL_PutAudioStreamData(stream,out.samples.data(),int(frames*8))) return;
        additional-=int(frames*8);
    }
}
}
namespace audio {
void StartOutput(std::function<void(float*,uint32_t)> render) {
    std::lock_guard lock(mutex);
    if(output) return;
    auto* out=new Output{std::move(render),std::vector<float>(2048)};
    SDL_AudioSpec spec{SDL_AUDIO_F32,2,int(kRate)};
    out->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,Pull,out);
    if(!out->stream) { LOGE("SDL audio open failed: %s",SDL_GetError()); delete out; return; }
    output=out;
    if(!SDL_ResumeAudioStreamDevice(out->stream)) LOGE("SDL audio resume failed: %s",SDL_GetError());
    else LOGR("SDL audio: 48000 Hz float stereo output active (%s)",SDL_GetCurrentAudioDriver());
}
}
namespace pgr::gldirect {
void StopAudio() {
    std::lock_guard lock(mutex);
    if(!output) return;
    SDL_DestroyAudioStream(output->stream);
    LOGR("SDL audio: %llu frames rendered, peak %.6f RMS %.6f",(unsigned long long)output->frames,
         output->peak,output->frames?std::sqrt(output->squares/(output->frames*2)):0);
    delete output; output=nullptr;
}
}
