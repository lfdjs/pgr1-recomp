// Linux host app services, adapted from the MIT SDK harness (TGP482/hk21x).
// Desktop pixel dimensions are supplied by the SDL thread; game threads never call SDL video.
#include <chrono>
#include <atomic>
#include <algorithm>
#include "display.h"
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

Settings& Get() {
    static Settings s;
    // FPS=N is the cap too, for what reads it (gpu/interp.cpp)
    static const bool capped = [] {
        if (const char* e = std::getenv("FPS"); e && *e) s.fps_cap = uint32_t(std::atol(e));
        return true;
    }();
    (void)capped;
    return s;
}
void Load() {}
void Save() {}
void ApplyDisplay() {}
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
    const auto mode=Output();
    return mode.width*2 >= mode.height*3;
}
float FrameAspect() {
    static const float forced=float(harness::Env("FRAME_ASPECT_X100"))/100;
    if(forced>0) return forced;
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

// FPS=N presents at most N frames a second, 0 unpaced; without it the settings' fps_cap as on Windows (a game's
// init may set it), 60 when that is 0. Within that, the title's present interval as on Windows. Titles that time
// their menus by the milliseconds between frames stand still when thousands of frames take no time at all.
void Pace(uint32_t vblanks) {
    using clock = std::chrono::steady_clock;
    static const long cap = [] {
        const char* e = std::getenv("FPS");
        if (e && *e) return std::atol(e);
        return Get().fps_cap ? long(Get().fps_cap) : 60L;
    }();
    static clock::time_point next = clock::now();
    harness::g_vblanks += vblanks ? vblanks : 1;
    if (cap <= 0) return;
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
