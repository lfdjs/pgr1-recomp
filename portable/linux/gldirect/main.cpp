// PGR1 03C.1: a single Linux process for the recompiled game and SDL3/OpenGL.
// SDL video + GL are owned by the MAIN thread, while the game boots on a worker.
// Owned Xbox draw commands are executed on this thread using OpenGL 4.0.
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
#include "renderer.h"
#include "sdl_audio.h"
#include "display.h"

namespace fs = std::filesystem;
namespace {
using GLenum = unsigned int;
constexpr GLenum kVendor = 0x1F00u, kRenderer = 0x1F01u, kVersion = 0x1F02u;

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
    const fs::path tmp = app::ExeDir() / "cmd.gldirect.tmp";
    const fs::path cmd = app::ExeDir() / "cmd";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << (tokens.empty() ? "KEYS" : tokens) << " 8\n";
        if (!f) return;
    }
    std::error_code ec;
    fs::rename(tmp, cmd, ec);
    if (ec) std::fprintf(stderr, "gldirect: controller cmd error: %s\n", ec.message().c_str());
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
            std::fprintf(stderr, "gldirect: cannot read %s\n", xbe_path.string().c_str());
            failed = true;
            return;
        }
        std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!kernel::Boot(std::move(image), IMAGE_XBE, game_dir.string(),
                          (app::ExeDir() / "hdd").string())) {
            std::fprintf(stderr, "gldirect: kernel::Boot failed (see logs/)\n");
            failed = true;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "gldirect: boot exception: %s\n", ex.what());
        failed = true;
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "Usage: %s <extracted-game-directory>\n", argv[0]); return 2; }
    const fs::path game = fs::absolute(argv[1]);
    if (!fs::is_regular_file(game / IMAGE_XBE)) {
        std::fprintf(stderr, "Missing %s\n", (game / IMAGE_XBE).string().c_str());
        return 2;
    }
    setenv("GAME_DIR", game.string().c_str(), 1);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    // SDL reports physical monitor pixels, while a high-density window's logical
    // size can differ. Publish pixels before the Xbox TV/projection hooks boot.
    const auto primary=SDL_GetPrimaryDisplay();
    const auto* desktop=SDL_GetDesktopDisplayMode(primary);
    unsigned width=desktop?unsigned(desktop->w):1280,height=desktop?unsigned(desktop->h):720;
    pgr::gldirect::SetDesktopPixels(width,height);
    const char* explicit_output=std::getenv("OUTPUT");
    if(explicit_output) {
        unsigned w=0,h=0;
        if(std::sscanf(explicit_output,"%ux%u",&w,&h)==2 && w && h) { width=w;height=h; }
    }
    pgr::gldirect::SetOutputPixels(width,height);
    const char* fullscreen_option=std::getenv("PGR1_FULLSCREEN");
    const bool fullscreen=fullscreen_option ? *fullscreen_option!='0' : !explicit_output;
    const SDL_WindowFlags flags=SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                (fullscreen?SDL_WINDOW_FULLSCREEN:0);
    SDL_Window* window = SDL_CreateWindow("PGR1 - Linux OpenGL (experimental)", width, height,flags);
    if (!window) { std::fprintf(stderr, "SDL window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { std::fprintf(stderr, "OpenGL context: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    auto glGetString = reinterpret_cast<const unsigned char* (*)(GLenum)>(SDL_GL_GetProcAddress("glGetString"));
    if (!glGetString) {
        std::fprintf(stderr, "gldirect: OpenGL glGetString unavailable\n");
        SDL_GL_DestroyContext(gl); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    std::printf("PGR1 OpenGL: GL vendor=%s; renderer=%s; version=%s\n",
                glGetString(kVendor), glGetString(kRenderer), glGetString(kVersion));
    const std::string driver=reinterpret_cast<const char*>(glGetString(kRenderer));
    if(std::getenv("PGR1_EXPECT_ZINK") && driver.find("zink")==std::string::npos) {
        std::fprintf(stderr,"Zink requested, but context renderer is %s\n",driver.c_str());
        SDL_GL_DestroyContext(gl); SDL_DestroyWindow(window); SDL_Quit(); return 2;
    }
    if((driver.find("llvmpipe")!=std::string::npos || driver.find("softpipe")!=std::string::npos || driver.find("lavapipe")!=std::string::npos) && !std::getenv("PGR1_ALLOW_SOFTWARE")) {
        std::fprintf(stderr,"Software GL renderer: %s. Set PGR1_ALLOW_SOFTWARE=1 for explicit diagnostics.\n",driver.c_str());
        SDL_GL_DestroyContext(gl); SDL_DestroyWindow(window); SDL_Quit(); return 2;
    }
    auto renderer = std::make_unique<pgr::gldirect::Renderer>([](const char* name) {
        return reinterpret_cast<pgr::gldirect::Renderer::Proc>(SDL_GL_GetProcAddress(name));
    });
    if (!renderer->Init()) { renderer.reset(); SDL_GL_DestroyContext(gl); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    int output_width=0,output_height=0;
    auto update_output=[&] {
        int w=0,h=0; SDL_GetWindowSizeInPixels(window,&w,&h);
        if(w>0 && h>0 && (w!=output_width || h!=output_height)) {
            output_width=w;output_height=h;
            pgr::gldirect::SetOutputPixels(unsigned(w),unsigned(h));
            renderer->SetAspect(app::FrameAspect());
            renderer->SetOutputSize(unsigned(w),unsigned(h));
            std::printf("SDL display %u: window pixels %dx%d, frame aspect %.4f\n",SDL_GetDisplayForWindow(window),w,h,app::FrameAspect());
        }
    };
    update_output();
    std::puts("Experimental GPU rendering: SDK shaders through HLSL/SPIR-V/GLSL; unsupported states are logged.");
    std::puts("DualShock 4 and keyboard controls are forwarded. Esc closes the process.");
    std::fflush(stdout);
    SDL_GL_SetSwapInterval(std::getenv("PGR1_VSYNC") ? std::atoi(std::getenv("PGR1_VSYNC")) : 1);
    SDL_Gamepad* pad = OpenFirstPad();

    std::atomic<bool> boot_failed{false};
    std::thread(BootGame, game, std::ref(boot_failed)).detach();

    const char* script_mode=std::getenv("PGR1_SCRIPT_ONLY");
    const bool script_only=script_mode && *script_mode && *script_mode!='0';
    if(script_only) std::puts("Automated benchmark: forwarding SCRIPT only; Escape/F11 remain active.");
    Keys keys;
    bool running = true, had_input = false;
    auto now = std::chrono::steady_clock::now();
    auto next_input = now, next_pad_scan = now, next_title = now;
    pgr::gldirect::Submission frame{}, pending{};
    double pending_execute_ms=0;
    std::ofstream timings("frames.csv"), gpu_times("gpu.csv");
    timings<<"frame,frame_ms,cpu_ms,capture_ms,max_draw_ms,decode_ms,texture_check_ms,assemble_ms,queue_wait_ms,execute_ms,present_ms,supported_draws,rejected_draws\n";
    gpu_times<<"frame,gpu_ms\n";
    const auto started=std::chrono::steady_clock::now(); auto last_frame=started;
    std::clock_t last_cpu=std::clock();
    const double seconds=std::getenv("PGR1_RUN_SECONDS") ? std::atof(std::getenv("PGR1_RUN_SECONDS")) : 0;
    auto ms=[](auto duration) { return std::chrono::duration<double,std::milli>(duration).count(); };
    while (running && !boot_failed.load()) {
        bool redraw=false;
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_WINDOW_EXPOSED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) redraw=true;
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
                else if(event.key.scancode == SDL_SCANCODE_F11) SDL_SetWindowFullscreen(window,!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN));
                else keys.down.insert(event.key.scancode);
            } else if (event.type == SDL_EVENT_KEY_UP) keys.down.erase(event.key.scancode);
        }
        now = std::chrono::steady_clock::now();
        if(seconds>0 && std::chrono::duration<double>(now-started).count()>=seconds) break;
        if(const auto error=pgr::gldirect::Failure(); !error.empty()) { std::fprintf(stderr,"gldirect: %s\n",error.c_str()); boot_failed=true; break; }
        if (now >= next_pad_scan) {
            if (pad && !SDL_GamepadConnected(pad)) { SDL_CloseGamepad(pad); pad = nullptr; }
            if (!pad) pad = OpenFirstPad();
            next_pad_scan = now + std::chrono::seconds(1);
        }
        if (now >= next_input && !script_only) {
            std::string tokens = InputTokens(keys, pad);
            if (!tokens.empty() || had_input) SendCommand(tokens);
            had_input = !tokens.empty();
            next_input = now + std::chrono::milliseconds(65);
        }
        update_output();
        pgr::gldirect::Submission next;
        bool took=pgr::gldirect::Take(next), presented=false; double execute_ms=0,frame_ms=0,cpu_ms=0;
        if (took) {
            const auto before=std::chrono::steady_clock::now();
            presented=next.present;
            if(presented) { frame_ms=ms(before-last_frame); last_frame=before; auto cpu=std::clock(); cpu_ms=1000.0*double(cpu-last_cpu)/CLOCKS_PER_SEC; last_cpu=cpu; }
            if (!renderer->Execute(next)) { boot_failed=true; break; }
            pending_execute_ms+=ms(std::chrono::steady_clock::now()-before);
            pending.draw_ns+=next.draw_ns; pending.decode_ns+=next.decode_ns;
            pending.texture_check_ns+=next.texture_check_ns; pending.assemble_ns+=next.assemble_ns;
            pending.max_draw_ns=std::max(pending.max_draw_ns,next.max_draw_ns);
            pending.queue_wait_ns+=next.queue_wait_ns; pending.supported+=next.supported; pending.rejected_draws+=next.rejected_draws;
            if(presented) {
                frame=std::move(next); frame.commands.clear();
                frame.draw_ns=pending.draw_ns; frame.decode_ns=pending.decode_ns; frame.max_draw_ns=pending.max_draw_ns;
                frame.texture_check_ns=pending.texture_check_ns; frame.assemble_ns=pending.assemble_ns;
                frame.queue_wait_ns=pending.queue_wait_ns; frame.supported=pending.supported; frame.rejected_draws=pending.rejected_draws;
                execute_ms=pending_execute_ms; pending={}; pending_execute_ms=0;
            }
        }
        int w=0, h=0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        const auto before_present=std::chrono::steady_clock::now();
        if ((presented || (!took && redraw)) && !renderer->Present(w,h)) { boot_failed=true; break; }
        if ((presented || (!took && redraw)) && !SDL_GL_SwapWindow(window)) {
            std::fprintf(stderr, "SDL_GL_SwapWindow failed: %s\n", SDL_GetError());
            boot_failed=true;
            break;
        }
        if(presented && !frame.reference_pixels.empty()) { renderer->Compare(frame); frame.reference_pixels.clear(); }
        if(presented) timings<<frame.frame<<','<<frame_ms<<','<<cpu_ms<<','<<frame.draw_ns/1e6<<','<<frame.max_draw_ns/1e6<<','<<frame.decode_ns/1e6<<','<<frame.texture_check_ns/1e6<<','<<frame.assemble_ns/1e6<<','<<frame.queue_wait_ns/1e6<<','<<execute_ms<<','<<ms(std::chrono::steady_clock::now()-before_present)<<','<<frame.supported<<','<<frame.rejected_draws<<'\n';
        for(auto sample:renderer->PollGpu()) gpu_times<<sample.frame<<','<<sample.nanoseconds/1e6<<'\n';
        if (now >= next_title) {
            timings.flush(); gpu_times.flush(); logx::Flush();
            if(fs::is_regular_file("snap")) {
                if(!renderer->Capture("snap.ppm")) std::fputs("gldirect: requested snapshot failed\n",stderr);
                std::error_code error; fs::remove("snap",error);
            }
            char title[240];
            std::snprintf(title, sizeof(title),
                "PGR1 OpenGL | frames %llu | draws %llu | unsupported operations %llu",
                static_cast<unsigned long long>(frame.frame),
                static_cast<unsigned long long>(frame.draws),
                static_cast<unsigned long long>(frame.unsupported));
            SDL_SetWindowTitle(window, title);
            next_title = now + std::chrono::milliseconds(500);
        }
        // Waiting for a guest frame must not consume another blocking VSync.
        // Swap only completed frames (or an expose), so a late submission does
        // not miss its refresh because an unchanged image was swapped first.
        if(!took) SDL_Delay(1);
    }
    if(const char* capture=std::getenv("PGR1_CAPTURE"); capture && !renderer->Capture(capture)) std::fprintf(stderr,"gldirect: capture failed\n");
    timings.flush(); gpu_times.flush(); logx::Flush();
    pgr::gldirect::Stop();
    pgr::gldirect::StopAudio();
    logx::Flush();
    if (had_input) SendCommand("");
    if (pad) SDL_CloseGamepad(pad);
    renderer.reset();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    // The Xbox worker and emulated kernel own long-running threads without a join API.
    // Shut down this experimental process rather than racing detached SDK threads at exit.
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(boot_failed ? 1 : 0);
}
