// PGR1 04C: recompiled Xbox execution feeds owned submissions to native Vulkan.
// SDL video and the Vulkan device stay on the main thread; the guest boots on a worker.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "image_symbols.h"
#include "commands.h"
#include "game_renderer.h"
#include "sdl_audio.h"
#include "display.h"

namespace fs = std::filesystem;
namespace {

struct Keys {
    std::set<SDL_Scancode> down;
    bool Has(SDL_Scancode a, SDL_Scancode b = SDL_SCANCODE_UNKNOWN) const {
        return down.count(a) || (b != SDL_SCANCODE_UNKNOWN && down.count(b));
    }
};

void Add(std::string& dst, bool enabled, const char* token) {
    if (!enabled) return;
    if (!dst.empty()) dst += '+';
    dst += token;
}
std::string InputTokens(const Keys& keys, SDL_Gamepad* pad) {
    const auto button = [&](SDL_GamepadButton b) { return pad && SDL_GetGamepadButton(pad, b); };
    const auto axis = [&](SDL_GamepadAxis a) { return pad ? SDL_GetGamepadAxis(pad, a) : 0; };
    constexpr int dz = 10000;
    std::string out;
    Add(out, keys.Has(SDL_SCANCODE_UP, SDL_SCANCODE_W) || button(SDL_GAMEPAD_BUTTON_DPAD_UP), "UP");
    Add(out, keys.Has(SDL_SCANCODE_DOWN, SDL_SCANCODE_S) || button(SDL_GAMEPAD_BUTTON_DPAD_DOWN), "DOWN");
    Add(out, keys.Has(SDL_SCANCODE_LEFT, SDL_SCANCODE_A) || button(SDL_GAMEPAD_BUTTON_DPAD_LEFT), "LEFT");
    Add(out, keys.Has(SDL_SCANCODE_RIGHT, SDL_SCANCODE_D) || button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), "RIGHT");
    Add(out, keys.Has(SDL_SCANCODE_RETURN) || button(SDL_GAMEPAD_BUTTON_START), "START");
    Add(out, keys.Has(SDL_SCANCODE_BACKSPACE) || button(SDL_GAMEPAD_BUTTON_BACK), "BACK");
    Add(out, keys.Has(SDL_SCANCODE_Z, SDL_SCANCODE_J) || button(SDL_GAMEPAD_BUTTON_SOUTH), "A");
    Add(out, keys.Has(SDL_SCANCODE_X, SDL_SCANCODE_K) || button(SDL_GAMEPAD_BUTTON_EAST), "B");
    Add(out, keys.Has(SDL_SCANCODE_C) || button(SDL_GAMEPAD_BUTTON_WEST), "X");
    Add(out, keys.Has(SDL_SCANCODE_V) || button(SDL_GAMEPAD_BUTTON_NORTH), "Y");
    Add(out, button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), "BLACK");
    Add(out, button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), "WHITE");
    Add(out, axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > dz, "LT");
    Add(out, axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > dz, "RT");
    Add(out, axis(SDL_GAMEPAD_AXIS_LEFTY) < -dz, "LS");
    Add(out, axis(SDL_GAMEPAD_AXIS_LEFTY) > dz, "LSD");
    Add(out, axis(SDL_GAMEPAD_AXIS_LEFTX) < -dz, "LSL");
    Add(out, axis(SDL_GAMEPAD_AXIS_LEFTX) > dz, "LSR");
    Add(out, axis(SDL_GAMEPAD_AXIS_RIGHTY) < -dz, "RS");
    Add(out, axis(SDL_GAMEPAD_AXIS_RIGHTY) > dz, "RSD");
    Add(out, axis(SDL_GAMEPAD_AXIS_RIGHTX) < -dz, "RSL");
    Add(out, axis(SDL_GAMEPAD_AXIS_RIGHTX) > dz, "RSR");
    return out;
}
SDL_Gamepad* OpenFirstPad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_Gamepad* pad = ids && count > 0 ? SDL_OpenGamepad(ids[0]) : nullptr;
    SDL_free(ids);
    if(pad) std::printf("Gamepad connected: %s\n",SDL_GetGamepadName(pad));
    return pad;
}

// Reuses the known-good harness/pad_script.cpp and its atomic-renamed cmd protocol.
// Since both threads are in the same process, this also stays a useful regression check.
void SendCommand(const std::string& tokens) {
    const fs::path tmp = app::ExeDir() / "cmd.vulkan.tmp";
    const fs::path cmd = app::ExeDir() / "cmd";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << (tokens.empty() ? "KEYS" : tokens) << " 8\n";
        if (!f) return;
    }
    std::error_code ec;
    fs::rename(tmp, cmd, ec);
    if (ec) std::fprintf(stderr, "vulkan: controller cmd error: %s\n", ec.message().c_str());
}

void BootGame(fs::path game_dir, std::atomic<bool>& failed) {
    try {
        logx::Init((app::ExeDir() / "logs").string(), app::kGame.log);
        app::Load();
        if (app::kGame.init) app::kGame.init();
        if (app::kGame.window) app::kGame.window(nullptr);
        const fs::path xbe_path = game_dir / IMAGE_XBE;
        std::ifstream f(xbe_path, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "vulkan: cannot read %s\n", xbe_path.string().c_str());
            failed = true;
            return;
        }
        std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!kernel::Boot(std::move(image), IMAGE_XBE, game_dir.string(),
                          (app::ExeDir() / "hdd").string())) {
            std::fprintf(stderr, "vulkan: kernel::Boot failed (see logs/)\n");
            failed = true;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "vulkan: boot exception: %s\n", ex.what());
        failed = true;
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr,"Usage: %s <extracted-game-directory>\n",argv[0]); return 2; }
    const fs::path game=fs::absolute(argv[1]);
    if (!fs::is_regular_file(game/IMAGE_XBE)) {
        std::fprintf(stderr,"Missing %s\n",(game/IMAGE_XBE).string().c_str()); return 2;
    }
    std::uint64_t capture_frame=0;
    if (const char* selected=std::getenv("PGR1_CAPTURE_FRAME")) {
        char* end=nullptr;
        capture_frame=std::strtoull(selected,&end,10);
        if (!*selected || *selected=='-' || !end || *end || !capture_frame) {
            std::fputs("PGR1_CAPTURE_FRAME requires a positive guest frame number\n",stderr); return 2;
        }
    }
    const char* capture_frame_path=std::getenv("PGR1_CAPTURE_FRAME_PATH");
    if (!capture_frame_path || !*capture_frame_path) capture_frame_path="capture-frame.ppm";
    setenv("GAME_DIR",game.string().c_str(),1);
    if (!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD|SDL_INIT_AUDIO)) {
        std::fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1;
    }
    const auto* desktop=SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    unsigned width=desktop?unsigned(desktop->w):1280,height=desktop?unsigned(desktop->h):720;
    pgr::gldirect::SetDesktopPixels(width,height);
    const char* output=std::getenv("OUTPUT");
    if (output) {
        unsigned w=0,h=0;
        if (std::sscanf(output,"%ux%u",&w,&h)==2 && w && h) { width=w; height=h; }
    }
    pgr::gldirect::SetOutputPixels(width,height);
    const char* fullscreen_option=std::getenv("PGR1_FULLSCREEN");
    const bool fullscreen=fullscreen_option?*fullscreen_option!='0':!output;
    const SDL_WindowFlags flags=SDL_WINDOW_RESIZABLE|SDL_WINDOW_VULKAN|SDL_WINDOW_HIGH_PIXEL_DENSITY|
                               (fullscreen?SDL_WINDOW_FULLSCREEN:0);
    SDL_Window* window=SDL_CreateWindow("PGR1 - native Vulkan (04C experimental)",width,height,flags);
    if (!window) { std::fprintf(stderr,"SDL window: %s\n",SDL_GetError()); SDL_Quit(); return 1; }
    std::unique_ptr<pgr::vulkan::Renderer> renderer;
    try { renderer=std::make_unique<pgr::vulkan::Renderer>(window); }
    catch (const std::exception& ex) {
        std::fprintf(stderr,"Native Vulkan initialization failed: %s\n",ex.what());
        SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    std::puts("PGR1 real runtime -> owned Xbox submissions -> native Vulkan. Unsupported operations are logged.");
    std::puts("Keyboard and SDL gamepads are forwarded. Esc closes the process; F11 toggles fullscreen.");
    std::fflush(stdout);
    SDL_Gamepad* pad=OpenFirstPad();
    std::atomic<bool> boot_failed{false};
    std::thread(BootGame,game,std::ref(boot_failed)).detach();

    const char* script_mode=std::getenv("PGR1_SCRIPT_ONLY");
    const bool script_only=script_mode && *script_mode && *script_mode!='0';
    if (script_only) std::puts("Automated input: forwarding SCRIPT only; Escape/F11 remain active.");
    Keys keys;
    bool running=true,had_input=false;
    auto now=std::chrono::steady_clock::now();
    auto next_input=now,next_pad_scan=now,next_title=now;
    const auto started=now;
    auto last_frame=now;
    std::clock_t last_cpu=std::clock();
    const double seconds=std::getenv("PGR1_RUN_SECONDS")?std::atof(std::getenv("PGR1_RUN_SECONDS")):0;
    auto ms=[](auto duration) { return std::chrono::duration<double,std::milli>(duration).count(); };
    std::ofstream timings("frames.csv"),submissions("submissions.csv"),gpu_times("gpu.csv");
    timings<<"frame,frame_ms,cpu_ms,capture_ms,max_draw_ms,decode_ms,texture_check_ms,assemble_ms,queue_wait_ms,execute_ms,shader_ms,fence_ms,acquire_ms,present_ms,gpu_completed_ms,supported_draws,rejected_draws,vk_accepted,vk_rejected,indexed,nonindexed,presented,acquire_timeout,render_failed,cube_cache_creates,cube_cache_hits,cube_face_copies\n";
    submissions<<"frame,present_requested,presented,capture_ms,queue_wait_ms,execute_ms,shader_ms,fence_ms,acquire_ms,present_ms,gpu_completed_ms,gpu_sample_frame,vk_accepted,vk_rejected,indexed,nonindexed,acquire_timeout,render_failed,cube_cache_creates,cube_cache_hits,cube_face_copies\n";
    gpu_times<<"frame,gpu_ms\n";
    pgr::gldirect::Submission pending{};
    double execute_ms=0,shader_ms=0,fence_ms=0,acquire_ms=0,present_ms=0,gpu_ms=0;
    std::uint64_t accepted=0,rejected=0,indexed=0,nonindexed=0,total_frames=0,total_accepted=0,total_rejected=0,last_guest_frame=0;
    std::uint64_t cube_creates=0,cube_hits=0,cube_copies=0;
    int output_width=0,output_height=0;
    bool captured_frame=false;
    while (running && !boot_failed.load()) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type==SDL_EVENT_WINDOW_DISPLAY_CHANGED)
                renderer->Resize();
            if (event.type==SDL_EVENT_QUIT) running=false;
            else if (event.type==SDL_EVENT_KEY_DOWN) {
                if (event.key.scancode==SDL_SCANCODE_ESCAPE) running=false;
                else if (event.key.scancode==SDL_SCANCODE_F11)
                    SDL_SetWindowFullscreen(window,!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN));
                else keys.down.insert(event.key.scancode);
            } else if (event.type==SDL_EVENT_KEY_UP) keys.down.erase(event.key.scancode);
        }
        now=std::chrono::steady_clock::now();
        if (!running || (seconds>0 && std::chrono::duration<double>(now-started).count()>=seconds)) break;
        if (const auto error=pgr::gldirect::Failure(); !error.empty()) {
            std::fprintf(stderr,"vulkan runtime: %s\n",error.c_str()); boot_failed=true; break;
        }
        if (now>=next_pad_scan) {
            if (pad && !SDL_GamepadConnected(pad)) { SDL_CloseGamepad(pad); pad=nullptr; }
            if (!pad) pad=OpenFirstPad();
            next_pad_scan=now+std::chrono::seconds(1);
        }
        if (now>=next_input && !script_only) {
            const auto tokens=InputTokens(keys,pad);
            if (!tokens.empty() || had_input) SendCommand(tokens);
            had_input=!tokens.empty();
            next_input=now+std::chrono::milliseconds(65);
        }
        int w=0,h=0;
        SDL_GetWindowSizeInPixels(window,&w,&h);
        if (w>0 && h>0 && (w!=output_width || h!=output_height)) {
            output_width=w; output_height=h;
            pgr::gldirect::SetOutputPixels(unsigned(w),unsigned(h));
            renderer->Resize();
        }
        pgr::gldirect::Submission next;
        if (pgr::gldirect::Take(next)) {
            bool render_ok=false;
            try {
                renderer->SetAspect(app::FrameAspect());
                render_ok=renderer->Render(next);
            } catch (const std::exception& ex) {
                std::fprintf(stderr,"Native Vulkan submission failed: %s\n",ex.what());
                pgr::gldirect::Fail(ex.what());
            }
            const auto stats=renderer->LastStats();
            last_guest_frame=next.frame;
            if (render_ok) total_accepted+=stats.accepted;
            total_rejected+=stats.rejected;
            double completed_gpu_ms=0;
            for (const auto& sample:stats.gpu_samples) {
                gpu_times<<sample.frame<<','<<sample.milliseconds<<'\n';
                completed_gpu_ms+=sample.milliseconds;
            }
            submissions<<next.frame<<','<<next.present<<','<<stats.presented<<','<<next.draw_ns/1e6<<','
                <<next.queue_wait_ns/1e6<<','<<stats.execute_ms<<','<<stats.shader_ms<<','<<stats.fence_ms<<','
                <<stats.acquire_ms<<','<<stats.present_ms<<','<<completed_gpu_ms<<','<<stats.gpu_frame<<','<<stats.accepted<<','
                <<stats.rejected<<','<<stats.indexed<<','<<stats.nonindexed<<','<<stats.acquire_timeout<<','<<!render_ok<<','<<stats.cube_cache_creates<<','<<stats.cube_cache_hits<<','<<stats.cube_face_copies<<'\n';
            pending.draw_ns+=next.draw_ns; pending.decode_ns+=next.decode_ns;
            pending.texture_check_ns+=next.texture_check_ns; pending.assemble_ns+=next.assemble_ns;
            pending.max_draw_ns=std::max(pending.max_draw_ns,next.max_draw_ns);
            pending.queue_wait_ns+=next.queue_wait_ns; pending.supported+=next.supported;
            pending.rejected_draws+=next.rejected_draws;
            execute_ms+=stats.execute_ms; shader_ms+=stats.shader_ms; fence_ms+=stats.fence_ms;
            acquire_ms+=stats.acquire_ms; present_ms+=stats.present_ms; gpu_ms+=completed_gpu_ms;
            accepted+=stats.accepted; rejected+=stats.rejected; indexed+=stats.indexed; nonindexed+=stats.nonindexed;
            cube_creates+=stats.cube_cache_creates; cube_hits+=stats.cube_cache_hits; cube_copies+=stats.cube_face_copies;
            if (next.present) {
                const auto at=std::chrono::steady_clock::now();
                const auto cpu=std::clock();
                timings<<next.frame<<','<<ms(at-last_frame)<<','<<1000.0*double(cpu-last_cpu)/CLOCKS_PER_SEC<<','
                    <<pending.draw_ns/1e6<<','<<pending.max_draw_ns/1e6<<','<<pending.decode_ns/1e6<<','
                    <<pending.texture_check_ns/1e6<<','<<pending.assemble_ns/1e6<<','<<pending.queue_wait_ns/1e6<<','
                    <<execute_ms<<','<<shader_ms<<','<<fence_ms<<','<<acquire_ms<<','<<present_ms<<','<<gpu_ms<<','
                    <<pending.supported<<','<<pending.rejected_draws<<','<<accepted<<','<<rejected<<','<<indexed<<','
                    <<nonindexed<<','<<stats.presented<<','<<stats.acquire_timeout<<','<<!render_ok<<','<<cube_creates<<','<<cube_hits<<','<<cube_copies<<'\n';
                if (stats.presented) ++total_frames;
                last_frame=at; last_cpu=cpu; pending={};
                execute_ms=shader_ms=fence_ms=acquire_ms=present_ms=gpu_ms=0;
                accepted=rejected=indexed=nonindexed=0;
                cube_creates=cube_hits=cube_copies=0;
            }
            if (!render_ok) { boot_failed=true; break; }
            if (!next.reference_pixels.empty()) renderer->Compare(next);
            if (capture_frame && next.present && next.frame==capture_frame) {
                captured_frame=renderer->Capture(capture_frame_path);
                std::printf("Vulkan exact guest frame %llu capture %s: %s\n",
                    static_cast<unsigned long long>(capture_frame),capture_frame_path,captured_frame?"saved":"failed");
            }
        } else SDL_Delay(1);
        if (now>=next_title) {
            timings.flush(); submissions.flush(); gpu_times.flush(); logx::Flush();
            if (fs::is_regular_file("snap")) {
                if (!renderer->Capture("snap.ppm")) std::fputs("vulkan: requested snapshot failed\n",stderr);
                std::error_code error; fs::remove("snap",error);
            }
            char title[240];
            std::snprintf(title,sizeof(title),"PGR1 native Vulkan | guest %llu | presents %llu | draws %llu | rejected %llu",
                static_cast<unsigned long long>(last_guest_frame),static_cast<unsigned long long>(total_frames),
                static_cast<unsigned long long>(total_accepted),static_cast<unsigned long long>(total_rejected));
            SDL_SetWindowTitle(window,title);
            next_title=now+std::chrono::milliseconds(500);
        }
    }
    pgr::gldirect::Stop();
    if (capture_frame && !captured_frame)
        std::fprintf(stderr,"Vulkan requested guest frame %llu was not captured\n",static_cast<unsigned long long>(capture_frame));
    try {
        renderer->Finish();
        for (const auto& sample:renderer->LastStats().gpu_samples)
            gpu_times<<sample.frame<<','<<sample.milliseconds<<'\n';
    } catch (const std::exception& ex) {
        std::fprintf(stderr,"Native Vulkan final synchronization failed: %s\n",ex.what()); boot_failed=true;
    }
    if (const char* capture=std::getenv("PGR1_CAPTURE"); capture && !renderer->Capture(capture))
        std::fputs("vulkan: final capture failed\n",stderr);
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::printf("PGR1 NATIVE Vulkan game: guest_frame=%llu presented=%llu accepted=%llu rejected=%llu elapsed_s=%.3f\n",
        static_cast<unsigned long long>(last_guest_frame),static_cast<unsigned long long>(total_frames),
        static_cast<unsigned long long>(total_accepted),static_cast<unsigned long long>(total_rejected),elapsed);
    timings.flush(); submissions.flush(); gpu_times.flush(); logx::Flush();
    pgr::gldirect::StopAudio();
    if (had_input) SendCommand("");
    if (pad) SDL_CloseGamepad(pad);
    renderer.reset();
    SDL_DestroyWindow(window); SDL_Quit();
    // The SDK kernel has no joining API for its long-running worker threads.
    std::fflush(stdout); std::fflush(stderr);
    std::_Exit(boot_failed?1:0);
}
