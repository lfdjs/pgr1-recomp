// Linux host app services, adapted from the MIT SDK harness (TGP482/hk21x).
// Desktop pixel dimensions are supplied by the SDL thread; game threads never call SDL video.
#include <chrono>
#include <atomic>
#include <algorithm>
#include "display.h"
#include "settings_store.h"
#ifdef PGR1_NATIVE_VULKAN
#include "../vulkan/quality_state.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "harness.h"

namespace app {

namespace {
std::mutex settings_io;
pgr::linux_settings::Values saved_values;
// Settings are owned by the game/menu thread. Other native threads consume
// atomics; publishing never hands them a reference to the mutable Settings.
void Publish() {
#ifdef PGR1_NATIVE_VULKAN
    const auto& s=Get();
    using namespace pgr::vulkan::quality;
    render_percent.store(s.render_percent,std::memory_order_relaxed);
    fxaa.store(s.fxaa,std::memory_order_relaxed);
    sharpen_percent.store(s.sharpen,std::memory_order_relaxed);
    sharp_scaling.store(s.scale_filter==1,std::memory_order_relaxed);
    aniso.store(s.aniso,std::memory_order_relaxed);
    msaa.store(s.msaa,std::memory_order_relaxed);
    alpha_coverage.store(s.alpha_coverage,std::memory_order_relaxed);
    show_fps.store(s.show_fps,std::memory_order_relaxed);
    vsync.store(s.vsync,std::memory_order_relaxed);
    fps_cap.store(s.fps_cap,std::memory_order_relaxed);
    aspect.store(s.aspect,std::memory_order_relaxed);
#endif
}
void ReadEnvironment(Settings& s) {
    auto number=[](const char* key,uint32_t& value) {
        const char* env=std::getenv(key);
        if(!env) return;
        uint32_t parsed=0;
        if(pgr::linux_settings::Number(env,parsed)) value=parsed;
        else std::fprintf(stderr,"linux settings: ignored invalid %s\n",key);
    };
    number("FPS",s.fps_cap);
#ifdef PGR1_NATIVE_VULKAN
    number("PGR1_VK_INTERNAL_PERCENT",s.render_percent);
    number("PGR1_VK_SHARPEN",s.sharpen);
    number("PGR1_VK_ANISO",s.aniso);
    uint32_t samples=0;
    if(std::getenv("PGR1_VK_MSAA")) {
        number("PGR1_VK_MSAA",samples);
        s.msaa=samples==2?1:samples==4?2:0;
    }
    auto flag=[&](const char* key,bool& value) {
        if(!std::getenv(key)) return;
        uint32_t parsed=uint32_t(value);
        number(key,parsed);
        if(parsed<=1) value=parsed!=0;
    };
    flag("PGR1_VK_FXAA",s.fxaa); flag("PGR1_VK_VSYNC",s.vsync); flag("PGR1_SHOW_FPS",s.show_fps);
    if(const char* mode=std::getenv("PGR1_VK_PRESENT_MODE")) {
        const std::string_view selected=mode;
        if(selected=="fifo") s.vsync=true;
        else if(selected=="mailbox" || selected=="immediate") s.vsync=false;
        else std::fprintf(stderr,"linux settings: ignored invalid PGR1_VK_PRESENT_MODE\n");
    }
    unsigned spatial=100; number("PGR1_VK_SPATIAL_PERCENT",spatial);
    pgr::vulkan::quality::spatial_percent.store(std::clamp(spatial,100u,400u));
    bool mips=true; flag("PGR1_VK_REFLECTION_MIPS",mips);
    pgr::vulkan::quality::reflection_mips.store(mips);
#endif
    if(const char* output=std::getenv("OUTPUT")) {
        const std::string_view resolution=output;
        const auto separator=resolution.find('x');
        uint32_t w=0,h=0;
        if(separator!=resolution.npos && pgr::linux_settings::Number(resolution.substr(0,separator),w) &&
           pgr::linux_settings::Number(resolution.substr(separator+1),h)) {
            s.width=w; s.height=h; s.display_mode=DisplayMode::Windowed;
        } else std::fprintf(stderr,"linux settings: ignored invalid OUTPUT\n");
    }
    if(const char* full=std::getenv("PGR1_FULLSCREEN")) {
        uint32_t enabled=0;
        if(pgr::linux_settings::Number(full,enabled) && enabled<=1)
            s.display_mode=enabled?DisplayMode::Borderless:DisplayMode::Windowed;
    }
}
}
Settings& Get() { static Settings s; return s; }
void Load() {
    std::lock_guard lock(settings_io);
#ifdef PGR1_NATIVE_VULKAN
    Get().fxaa=true; // preserve the current native port's default
#endif
    saved_values=pgr::linux_settings::Read(pgr::linux_settings::Path());
    pgr::linux_settings::Load(Get(),saved_values);
    for(const Option& option:kGame.options) {
        if(auto it=saved_values.find(option.key);it!=saved_values.end()) {
            if(option.flag && it->second<=1) *option.flag=it->second!=0;
            if(option.value) *option.value=it->second;
        }
    }
    // Title-owned numeric preferences have finite ranges too.
    for(const Option& option:kGame.options) if(option.value) {
        const std::string_view key=option.key;
        if(key=="fov_percent") *option.value=std::clamp(*option.value,50u,200u);
        else if(key=="radio_percent") *option.value=std::min(*option.value,400u);
        else if(key=="frame_rate" && *option.value!=60 && *option.value!=90 && *option.value!=120 && *option.value!=144) *option.value=60;
    }
    ReadEnvironment(Get());
    pgr::linux_settings::Validate(Get());
    Publish();
    ApplyDisplay();
    std::fprintf(stderr,"linux settings: %s (environment overrides at startup; menu changes then apply live)\n",pgr::linux_settings::Path().string().c_str());
}
void Save() {
    std::lock_guard lock(settings_io);
#ifdef PGR1_NATIVE_VULKAN
    // The SDL thread may have fallen back to windowed mode or handled F11.
    Get().display_mode=DisplayMode(pgr::vulkan::quality::display_mode.load());
    Get().width=pgr::vulkan::quality::output_width.load();
    Get().height=pgr::vulkan::quality::output_height.load();
    Get().vsync=pgr::vulkan::quality::vsync.load();
    if(!pgr::vulkan::quality::postfx_supported.load()) {
        Get().fxaa=false; Get().sharpen=0; Get().scale_filter=0;
    }
    const unsigned max_aniso=pgr::vulkan::quality::aniso_supported_max.load();
    if(Get().aniso>max_aniso) {
        Get().aniso=0;
        for(unsigned value:{2u,4u,8u,16u}) if(value<=max_aniso) Get().aniso=value;
    }
    const unsigned mask=pgr::vulkan::quality::msaa_supported_mask.load();
    if(Get().msaa && (Get().msaa>2 || !(mask&(1u<<Get().msaa)))) Get().msaa=0;
#endif
    pgr::linux_settings::Validate(Get());
    Publish();
    pgr::linux_settings::Save(Get(),saved_values);
    for(const Option& option:kGame.options) {
        if(option.flag) saved_values[option.key]=*option.flag;
        if(option.value) saved_values[option.key]=*option.value;
    }
    std::error_code error;
    if(!pgr::linux_settings::Write(pgr::linux_settings::Path(),saved_values,error))
        std::fprintf(stderr,"linux settings: save failed: %s\n",error.message().c_str());
}
void ApplyDisplay() {
#ifdef PGR1_NATIVE_VULKAN
    const auto& s=Get();
    using namespace pgr::vulkan::quality;
    output_width.store(s.width,std::memory_order_relaxed);
    output_height.store(s.height,std::memory_order_relaxed);
    display_mode.store(uint32_t(s.display_mode),std::memory_order_relaxed);
    display_revision.fetch_add(1,std::memory_order_release);
#endif
}
std::array<uint16_t, 2> DefaultKeys(int) { return {0, 0}; }
namespace {
std::atomic<uint64_t> output_pixels{uint64_t(1280)<<32 | 720};
std::atomic<uint64_t> desktop_pixels{uint64_t(1280)<<32 | 720};
Mode Unpack(uint64_t value) { return {uint32_t(value>>32),uint32_t(value)}; }
}
std::vector<std::string> Monitors() { return {}; }
std::vector<Mode> ListModes() { return {Desktop(),{1920,1080},{1280,720}}; }
Mode Desktop() { return Unpack(desktop_pixels.load(std::memory_order_relaxed)); }
Mode Output() { return Unpack(output_pixels.load(std::memory_order_relaxed)); }
float RenderScale(uint32_t guest_height) {
    return guest_height ? std::max(.1f,float(Output().height)/guest_height) : 1.0f;
}
bool Widescreen() {
#ifdef PGR1_NATIVE_VULKAN
    const auto selected=pgr::vulkan::quality::aspect.load(std::memory_order_relaxed);
    if(selected==1) return false;
    if(selected==2 || selected==3) return true;
#endif
    const auto mode=Output();
    return mode.width*2 >= mode.height*3;
}
float FrameAspect() {
    static const float forced=float(harness::Env("FRAME_ASPECT_X100"))/100;
    if(forced>0) return forced;
#ifdef PGR1_NATIVE_VULKAN
    const unsigned selected=pgr::vulkan::quality::aspect.load(std::memory_order_relaxed);
    if(selected==1) return 4.0f/3;
    if(selected==2) return 16.0f/9;
    if(selected==3) {
        const auto mode=Output();
        return mode.height?std::max(16.0f/9,float(mode.width)/mode.height):16.0f/9;
    }
#endif
    if(!Widescreen()) return 4.0f/3;
    if(!kGame.auto_fills_screen) return 16.0f/9;
    const auto mode=Output();
    return mode.height ? std::max(16.0f/9,float(mode.width)/mode.height) : 16.0f/9;
}

std::filesystem::path ExeDir() {
    static const std::filesystem::path dir = std::filesystem::current_path();
    return dir;
}
int Instance() {
    static const int n = [] {
        const char* e = std::getenv("INSTANCE");
        return e ? std::atoi(e) : 0;
    }();
    return n;
}
void* Window() { return nullptr; }
void Quit() {
    LOGR("quit asked for");
    logx::Flush();
    kernel::Shutdown(0);
}

bool OpenSplash() { return false; }
void SplashFirstFrame() {}
void CloseSplash() {}
void OpenGameWindow(bool) {}
bool StartHandover() { return false; }
bool HandingOver() { return false; }
void FinishHandover() {}

// no settings window without a window: never open, opening it does nothing
bool OverlayOpen() { return false; }
void ToggleOverlay() { LOGI("settings window: none headless"); }

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

// The menu's current cap applies immediately. With no cap, honor the title's
// presentation interval; an interval of zero and cap zero is unpaced. This does
// not change the game's simulation clock or discard submissions.
void Pace(uint32_t vblanks) {
    using clock = std::chrono::steady_clock;
#ifdef PGR1_NATIVE_VULKAN
    const long cap=long(pgr::vulkan::quality::fps_cap.load(std::memory_order_relaxed));
#else
    static const long cap=[] {
        if(const char* env=std::getenv("FPS");env && *env) return std::atol(env);
        return Get().fps_cap?long(Get().fps_cap):60L;
    }();
    if(cap<=0) return;
#endif
    static clock::time_point next = clock::now();
    harness::g_vblanks += vblanks ? vblanks : 1;
    long fps = PaceRate(uint32_t(cap), vblanks);
    if (fps <= 0) return;
    auto period = std::chrono::nanoseconds(1000000000LL / fps);
    auto now = clock::now();
    next += period;
    if (next < now - period) next = now;
    std::this_thread::sleep_until(next);
}

}  // namespace app

namespace pgr::gldirect {
void SetDesktopPixels(unsigned width,unsigned height) {
    if(width && height) app::desktop_pixels.store(uint64_t(width)<<32 | height,std::memory_order_relaxed);
}
void SetOutputPixels(unsigned width,unsigned height) {
    if(width && height) app::output_pixels.store(uint64_t(width)<<32 | height,std::memory_order_relaxed);
}
}
