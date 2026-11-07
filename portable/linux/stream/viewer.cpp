// PGR1 Linux stage 02: SDL3 window + captured frames + controller command bridge.
// Does NOT link the Xbox runtime, replace gpu::Backend or claim realtime graphics.
// Launches alongside the *unchanged* headless harness via scripts/portable-gui.sh.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "frame_ipc.h"

namespace fs = std::filesystem;
namespace {

struct Keys {
    std::set<SDL_Scancode> down;
    bool Has(SDL_Scancode a, SDL_Scancode b = SDL_SCANCODE_UNKNOWN) const {
        return down.count(a) || (b != SDL_SCANCODE_UNKNOWN && down.count(b));
    }
};

void Add(std::string& target, bool active, const char* token) {
    if (!active) return;
    if (!target.empty()) target += '+';
    target += token;
}

std::string InputTokens(const Keys& keys, SDL_Gamepad* pad) {
    const auto b = [&](SDL_GamepadButton button) { return pad && SDL_GetGamepadButton(pad, button); };
    const auto axis = [&](SDL_GamepadAxis a) { return pad ? SDL_GetGamepadAxis(pad, a) : 0; };
    constexpr int deadzone = 10000;
    std::string tokens;
    Add(tokens, keys.Has(SDL_SCANCODE_UP, SDL_SCANCODE_W) || b(SDL_GAMEPAD_BUTTON_DPAD_UP), "UP");
    Add(tokens, keys.Has(SDL_SCANCODE_DOWN, SDL_SCANCODE_S) || b(SDL_GAMEPAD_BUTTON_DPAD_DOWN), "DOWN");
    Add(tokens, keys.Has(SDL_SCANCODE_LEFT, SDL_SCANCODE_A) || b(SDL_GAMEPAD_BUTTON_DPAD_LEFT), "LEFT");
    Add(tokens, keys.Has(SDL_SCANCODE_RIGHT, SDL_SCANCODE_D) || b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), "RIGHT");
    Add(tokens, keys.Has(SDL_SCANCODE_RETURN) || b(SDL_GAMEPAD_BUTTON_START), "START");
    Add(tokens, keys.Has(SDL_SCANCODE_BACKSPACE) || b(SDL_GAMEPAD_BUTTON_BACK), "BACK");
    Add(tokens, keys.Has(SDL_SCANCODE_Z, SDL_SCANCODE_J) || b(SDL_GAMEPAD_BUTTON_SOUTH), "A");
    Add(tokens, keys.Has(SDL_SCANCODE_X, SDL_SCANCODE_K) || b(SDL_GAMEPAD_BUTTON_EAST), "B");
    Add(tokens, keys.Has(SDL_SCANCODE_C) || b(SDL_GAMEPAD_BUTTON_WEST), "X");
    Add(tokens, keys.Has(SDL_SCANCODE_V) || b(SDL_GAMEPAD_BUTTON_NORTH), "Y");
    Add(tokens, b(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), "BLACK");
    Add(tokens, b(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), "WHITE");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > deadzone, "LT");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > deadzone, "RT");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_LEFTY) < -deadzone, "LS");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_LEFTY) > deadzone, "LSD");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_LEFTX) < -deadzone, "LSL");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_LEFTX) > deadzone, "LSR");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_RIGHTY) < -deadzone, "RS");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_RIGHTY) > deadzone, "RSD");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_RIGHTX) < -deadzone, "RSL");
    Add(tokens, axis(SDL_GAMEPAD_AXIS_RIGHTX) > deadzone, "RSR");
    return tokens;
}

// The SDK's pad_script.cpp reads run_dir/cmd and unlinks it on the next poll.
// POSIX rename ensures the emulator never sees half a command.
void SendCommand(const fs::path& run, const std::string& tokens) {
    const fs::path tmp = run / "cmd.preview.tmp";
    const fs::path cmd = run / "cmd";
    {
        std::ofstream file(tmp, std::ios::trunc);
        if (!file) return;
        file << (tokens.empty() ? "KEYS" : tokens) << " 8\n";
        if (!file) return;
    }
    std::error_code ec;
    fs::rename(tmp, cmd, ec);
    if (ec) std::fprintf(stderr, "Cannot send pad input: %s\n", ec.message().c_str());
}

SDL_Gamepad* OpenFirstPad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_Gamepad* pad = ids && count ? SDL_OpenGamepad(ids[0]) : nullptr;
    SDL_free(ids);
    return pad;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <run-directory> <shared-frame-file>\n", argv[0]);
        return 2;
    }
    const fs::path run = fs::absolute(argv[1]);
    const fs::path frame_file = fs::absolute(argv[2]);
    std::error_code ec;
    fs::create_directories(run / "cap", ec);
    if (ec) { std::fprintf(stderr, "Cannot create %s: %s\n", run.string().c_str(), ec.message().c_str()); return 2; }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("PGR1 — Linux shared-memory preview", 1024, 768, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        std::fprintf(stderr, "Cannot create SDL3 window: %s\n", SDL_GetError());
        SDL_Quit(); return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(renderer, 1);
    SDL_Gamepad* pad = OpenFirstPad();
    SDL_Texture* texture = nullptr;
    pgr::stream::File shared;
    std::uint64_t current_frame = 0;
    Keys keys;
    bool running = true;
    bool had_buttons = false;
    const auto start = std::chrono::steady_clock::now();
    auto next_image_check = start;
    auto next_control = start;
    auto next_pad_check = start;

    std::puts("PGR1 SHM preview: ESC closes window; PS4 gamepad and keyboard supported.");
    std::puts("Frame transport is memory mapped; Xbox rendering remains on CPU in the harness.");
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
                else keys.down.insert(event.key.scancode);
            } else if (event.type == SDL_EVENT_KEY_UP) {
                keys.down.erase(event.key.scancode);
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= next_pad_check) {
            if (pad && !SDL_GamepadConnected(pad)) { SDL_CloseGamepad(pad); pad = nullptr; }
            if (!pad) pad = OpenFirstPad();
            next_pad_check = now + std::chrono::seconds(1);
        }
        if (now >= next_control) {
            std::string tokens = InputTokens(keys, pad);
            if (!tokens.empty() || had_buttons) SendCommand(run, tokens);
            had_buttons = !tokens.empty();
            next_control = now + std::chrono::milliseconds(65);
        }
        if (now >= next_image_check) {
            if (!shared.IsOpen()) shared.Open(frame_file, false);
            pgr::stream::Frame img;
            if (shared.Receive(img)) {
                if (!texture) texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
                                                         SDL_TEXTUREACCESS_STREAMING, img.width, img.height);
                else {
                    float w = 0, h = 0;
                    SDL_GetTextureSize(texture, &w, &h);
                    if (int(w) != int(img.width) || int(h) != int(img.height)) {
                        SDL_DestroyTexture(texture);
                        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
                                                    SDL_TEXTUREACCESS_STREAMING, img.width, img.height);
                    }
                }
                if (texture && SDL_UpdateTexture(texture, nullptr, img.bgra.data(), int(img.width) * 4)) {
                    current_frame = img.index;
                }
            }
            next_image_check = now + std::chrono::milliseconds(33);
        }
        SDL_SetRenderDrawColor(renderer, 13, 19, 31, 255);
        SDL_RenderClear(renderer);
        if (texture) {
            SDL_FRect target{0, 0, 640, 480};
            SDL_RenderTexture(renderer, texture, nullptr, &target);
        } else {
            SDL_SetRenderDrawColor(renderer, 220, 226, 235, 255);
            SDL_RenderDebugText(renderer, 16, 28, "PGR1 Linux / SDL3 / SHM frame preview");
            SDL_RenderDebugText(renderer, 16, 48, "Waiting for SHM frame from game harness...");
            SDL_RenderDebugText(renderer, 16, 68, "GPU presentation only; Xbox raster still CPU.");
        }
        if (texture) {
            SDL_SetRenderDrawColor(renderer, 250, 230, 160, 255);
            char label[96];
            std::snprintf(label, sizeof(label), "CPU FRAME %llu -> SHM -> SDL3 TEXTURE", (unsigned long long)current_frame);
            SDL_RenderDebugText(renderer, 12, 12, label);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    if (had_buttons) SendCommand(run, "");
    if (texture) SDL_DestroyTexture(texture);
    if (pad) SDL_CloseGamepad(pad);
    shared.Close();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
