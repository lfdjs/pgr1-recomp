// PGR1 03C.1: a single Linux process for the recompiled game and SDL3/OpenGL.
// SDL video + GL are owned by the MAIN thread, while the game boots on a worker.
// This deliberately presents ONLY GPU-clear diagnostics, NOT guest geometry.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <chrono>
#include <cstdint>
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
#include "bridge.h"

namespace fs = std::filesystem;
namespace {
using GLenum = unsigned int;
constexpr GLenum kColorBufferBit = 0x00004000u;
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
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("PGR1 - Linux OpenGL direct (DIAGNOSTIC)", 960, 720,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (!window) { std::fprintf(stderr, "SDL window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { std::fprintf(stderr, "OpenGL context: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    auto glClearColor = reinterpret_cast<void (*)(float,float,float,float)>(SDL_GL_GetProcAddress("glClearColor"));
    auto glClear = reinterpret_cast<void (*)(GLenum)>(SDL_GL_GetProcAddress("glClear"));
    auto glViewport = reinterpret_cast<void (*)(int,int,int,int)>(SDL_GL_GetProcAddress("glViewport"));
    auto glGetString = reinterpret_cast<const unsigned char* (*)(GLenum)>(SDL_GL_GetProcAddress("glGetString"));
    if (!glClearColor || !glClear || !glViewport || !glGetString) {
        std::fprintf(stderr, "gldirect: OpenGL 3.3 clear/present functions unavailable\n");
        SDL_GL_DestroyContext(gl); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    std::printf("PGR1 03C.1 diagnostic: GL vendor=%s; renderer=%s; version=%s\n",
                glGetString(kVendor), glGetString(kRenderer), glGetString(kVersion));
    std::puts("DIAGNOSTIC ONLY: the GPU shows guest clear colors; game meshes/textures are NOT drawn yet.");
    std::puts("DualShock 4 and keyboard controls are forwarded. Esc closes the process.");
    std::fflush(stdout);
    SDL_GL_SetSwapInterval(1);
    SDL_Gamepad* pad = OpenFirstPad();

    std::atomic<bool> boot_failed{false};
    std::thread(BootGame, game, std::ref(boot_failed)).detach();

    Keys keys;
    bool running = true, had_input = false;
    auto now = std::chrono::steady_clock::now();
    auto next_input = now, next_pad_scan = now, next_title = now;
    pgr::gldirect::Frame frame{};
    while (running && !boot_failed.load()) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
                else keys.down.insert(event.key.scancode);
            } else if (event.type == SDL_EVENT_KEY_UP) keys.down.erase(event.key.scancode);
        }
        now = std::chrono::steady_clock::now();
        if (now >= next_pad_scan) {
            if (pad && !SDL_GamepadConnected(pad)) { SDL_CloseGamepad(pad); pad = nullptr; }
            if (!pad) pad = OpenFirstPad();
            next_pad_scan = now + std::chrono::seconds(1);
        }
        if (now >= next_input) {
            std::string tokens = InputTokens(keys, pad);
            if (!tokens.empty() || had_input) SendCommand(tokens);
            had_input = !tokens.empty();
            next_input = now + std::chrono::milliseconds(65);
        }
        pgr::gldirect::Latest(frame);
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        const auto color = frame.had_color_clear ? frame.last_color : 0xFF101929u;
        const auto channel = [&](unsigned shift) { return float((color >> shift) & 0xFFu) / 255.0f; };
        glClearColor(channel(16), channel(8), channel(0), 1.0f);
        glClear(kColorBufferBit);
        if (!SDL_GL_SwapWindow(window)) {
            std::fprintf(stderr, "SDL_GL_SwapWindow failed: %s\n", SDL_GetError());
            break;
        }
        if (now >= next_title) {
            char title[240];
            std::snprintf(title, sizeof(title),
                "PGR1 GPU DIAGNOSTIC | guest frames %llu | draws %llu (NOT rendered) | clears %llu",
                static_cast<unsigned long long>(frame.number),
                static_cast<unsigned long long>(frame.total_draws),
                static_cast<unsigned long long>(frame.total_clears));
            SDL_SetWindowTitle(window, title);
            next_title = now + std::chrono::milliseconds(500);
        }
        SDL_Delay(4);
    }
    if (had_input) SendCommand("");
    if (pad) SDL_CloseGamepad(pad);
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    // The Xbox worker and emulated kernel own long-running threads without a join API.
    // Shut down this experimental process rather than racing detached SDK threads at exit.
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(boot_failed ? 1 : 0);
}
