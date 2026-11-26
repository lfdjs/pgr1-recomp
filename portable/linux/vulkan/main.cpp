// PGR1 04C: recompiled Xbox execution feeds owned submissions to native Vulkan.
// SDL video and the Vulkan device stay on the main thread; the guest boots on a worker.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
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
#include "quality_state.h"

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

// SDL video calls stay on the main thread. The menu publishes a revision only
// after its atomic display values are ready.
void ApplyWindow(SDL_Window* window) {
    namespace quality=pgr::vulkan::quality;
    unsigned mode=quality::display_mode.load();
    const auto desktop=app::Desktop();
    unsigned width=quality::output_width.load(),height=quality::output_height.load();
    if(!width || !height) { width=desktop.width; height=desktop.height; }
    bool ok=SDL_SetWindowFullscreen(window,false);
    if(mode==0) {
        SDL_DisplayMode closest{};
        const auto display=SDL_GetDisplayForWindow(window);
        if(SDL_GetClosestFullscreenDisplayMode(display,int(width),int(height),0,true,&closest)) {
            if(quality::output_width.load() && quality::output_height.load()) {
                quality::output_width.store(unsigned(closest.w));
                quality::output_height.store(unsigned(closest.h));
            }
            ok=SDL_SetWindowFullscreenMode(window,&closest) && ok;
        }
        else { mode=1; quality::display_mode.store(mode); }
    }
    if(mode==1)ok=SDL_SetWindowFullscreenMode(window,nullptr) && ok;
    if(mode==2) {
        ok=SDL_SetWindowSize(window,int(width),int(height)) && ok;
        ok=SDL_SetWindowBordered(window,true) && ok;
    } else ok=SDL_SetWindowFullscreen(window,true) && ok;
    if(!ok) {
        std::fprintf(stderr,"Vulkan display settings: %s; using a window\n",SDL_GetError());
        SDL_SetWindowFullscreen(window,false);
        SDL_SetWindowSize(window,int(width),int(height));
        SDL_SetWindowBordered(window,true);
        quality::display_mode.store(2);
    }
}
void CaptureVariants(pgr::vulkan::Renderer& renderer) {
    namespace quality=pgr::vulkan::quality;
    const bool fxaa=quality::fxaa.load(),overlay=quality::show_fps.load();
    const unsigned sharpen=quality::sharpen_percent.load();
    const bool sharp=quality::sharp_scaling.load();
    quality::show_fps.store(false); quality::sharpen_percent.store(0); quality::sharp_scaling.store(false);
    quality::fxaa.store(false); renderer.Capture("fxaa-off.ppm");
    quality::fxaa.store(true); renderer.Capture("fxaa-on.ppm");
    quality::show_fps.store(true); renderer.Capture("fps-on.ppm");
    quality::fxaa.store(fxaa); quality::show_fps.store(overlay);
    quality::sharpen_percent.store(sharpen); quality::sharp_scaling.store(sharp);
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
    app::Load();
    namespace quality=pgr::vulkan::quality;
    if(quality::output_width.load() && quality::output_height.load()) {
        width=quality::output_width.load(); height=quality::output_height.load();
    }
    pgr::gldirect::SetOutputPixels(width,height);
    const SDL_WindowFlags flags=SDL_WINDOW_RESIZABLE|SDL_WINDOW_VULKAN|SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window=SDL_CreateWindow("PGR1 - native Vulkan (04C experimental)",width,height,flags);
    if (!window) { std::fprintf(stderr,"SDL window: %s\n",SDL_GetError()); SDL_Quit(); return 1; }
    ApplyWindow(window);
    unsigned display_revision=quality::display_revision.load(std::memory_order_acquire);
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
    timings<<"frame,frame_ms,cpu_ms,capture_ms,max_draw_ms,decode_ms,texture_check_ms,assemble_ms,queue_wait_ms,execute_ms,shader_ms,fence_ms,acquire_ms,present_ms,gpu_completed_ms,supported_draws,rejected_draws,vk_accepted,vk_rejected,indexed,nonindexed,presented,acquire_timeout,render_failed,cube_cache_creates,cube_cache_hits,cube_face_copies,staging_allocations,staging_reuses,descriptor_creates,descriptor_resets,framebuffer_hits,framebuffer_creates,presented_fps,presented_frame_ms,msaa_samples,fxaa,render_percent,texture_static_binds,texture_memcmp_checks,texture_memcmp_bytes,texture_same_draw_reuses,texture_decodes,texture_mip_levels_decoded,texture_mip_levels_reused,texture_cache_evictions,texture_cache_misses,texture_descriptor_redecodes,texture_guest_redecodes,texture_cache_entries_peak,vk_pipeline_bind_skips,vk_viewport_set_skips,vk_blend_set_skips,texture_overlap_checks,texture_diagnostic_skips,texture_tracker_queries,texture_tracker_fast_hits,texture_tracker_invalidations,texture_tracker_exact_fallbacks,texture_tracker_uncovered_ranges,texture_tracker_false_positives,texture_tracker_false_negatives,texture_tracker_monitored_writes,texture_tracker_dirty_pages,texture_tracker_tracked_bytes,texture_tracker_covered_queries,texture_tracker_ms,texture_tracker_write_ms,texture_journal_clean,texture_journal_dirty,texture_journal_unknown,texture_journal_false_negatives,texture_journal_false_positives,texture_journal_unstable,texture_journal_ms,guest_vblank,submission_reason,geometry_vertices,geometry_indices,geometry_bytes,simulation_tick\n";
    submissions<<"frame,present_requested,presented,capture_ms,queue_wait_ms,execute_ms,shader_ms,fence_ms,acquire_ms,present_ms,gpu_completed_ms,gpu_sample_frame,vk_accepted,vk_rejected,indexed,nonindexed,acquire_timeout,render_failed,cube_cache_creates,cube_cache_hits,cube_face_copies,staging_allocations,staging_reuses,descriptor_creates,descriptor_resets,framebuffer_hits,framebuffer_creates,presented_fps,presented_frame_ms,msaa_samples,fxaa,render_percent,texture_static_binds,texture_memcmp_checks,texture_memcmp_bytes,texture_same_draw_reuses,texture_decodes,texture_mip_levels_decoded,texture_mip_levels_reused,texture_cache_evictions,texture_cache_misses,texture_descriptor_redecodes,texture_guest_redecodes,texture_cache_entries_peak,vk_pipeline_bind_skips,vk_viewport_set_skips,vk_blend_set_skips,texture_overlap_checks,texture_diagnostic_skips,texture_tracker_queries,texture_tracker_fast_hits,texture_tracker_invalidations,texture_tracker_exact_fallbacks,texture_tracker_uncovered_ranges,texture_tracker_false_positives,texture_tracker_false_negatives,texture_tracker_monitored_writes,texture_tracker_dirty_pages,texture_tracker_tracked_bytes,texture_tracker_covered_queries,texture_tracker_ms,texture_tracker_write_ms,texture_journal_clean,texture_journal_dirty,texture_journal_unknown,texture_journal_false_negatives,texture_journal_false_positives,texture_journal_unstable,texture_journal_ms,guest_vblank,submission_reason,geometry_vertices,geometry_indices,geometry_bytes,simulation_tick\n";
    gpu_times<<"frame,gpu_ms\n";
    // EN: Diagnostic readback can complete a pending game timestamp after its
    // render row was logged. Preserve new game samples, excluding frame 0
    // diagnostic submissions; never write the already-delivered samples twice.
    // PT-BR: Readback diagnóstico pode concluir timestamps após o registro do
    // render. Preserve só novas amostras do jogo, excluindo o frame diagnóstico 0.
    auto record_diagnostic_samples=[&](auto&& operation) {
        const auto before=renderer->LastStats().gpu_samples.size();
        operation();
        const auto& samples=renderer->LastStats().gpu_samples;
        for(std::size_t i=before;i<samples.size();++i) if(samples[i].frame)
            gpu_times<<samples[i].frame<<','<<samples[i].milliseconds<<'\n';
    };
    std::ofstream batch_log("vk_batches.csv");
    // EN: One guest frame may generate many submissions (offscreen/fences).
    // PT-BR: Um frame do Xbox pode gerar diversas submissões (offscreen/fences).
    // Analyze all rows; never discard duplicate frame IDs.
    // Analise todas as linhas; não descarte IDs de frame repetidos.
    batch_log<<"frame,accepted,render_passes,batched_groups,batched_draws,passes_saved\n";
    // EN: Optional counters follow every submission and sum until a real
    // presentation. GPU samples retain their source frame in gpu.csv.
    // PT-BR: Contadores opcionais somam todas as submissões até a apresentação.
    // Amostras GPU conservam seu frame de origem em gpu.csv.
    const char* sync_env=std::getenv("PGR1_VK_SYNC_PROFILE");
    const bool sync_profile=sync_env && std::string(sync_env)=="1";
    std::ofstream sync_log, sync_submissions;
    auto sync_header=[&](std::ostream& out) {
        out<<"frame,guest_vblank,presented,reason,"<<pgr::gldirect::SyncProfile::Columns()
           <<",visibility_queries,visibility_query_pool_creates,visibility_query_pool_reuses,visibility_deferred_submissions,readback_requests,readback_bytes,readback_buffer_creates,readback_buffer_reuses,readback_resolve_ms,submissions,nonpresent_submissions,render_passes,pipeline_creates,shader_ms\n";
    };
    auto sync_row=[&](std::ostream& out,const pgr::gldirect::Submission& frame,
                      const pgr::gldirect::SyncProfile& producer,const pgr::vulkan::Renderer::Stats& stats) {
        out<<frame.frame<<','<<frame.guest_vblank<<','<<stats.presented<<','
           <<pgr::gldirect::ReasonName(frame.reason)<<',';
        producer.Csv(out);
        out<<','<<stats.visibility_queries<<','<<stats.visibility_query_pool_creates<<','
           <<stats.visibility_query_pool_reuses<<','<<stats.visibility_deferred_submissions<<','
           <<stats.readback_requests<<','<<stats.readback_bytes<<','<<stats.readback_buffer_creates<<','
           <<stats.readback_buffer_reuses<<','<<stats.readback_resolve_ms<<','<<stats.submissions<<','
           <<stats.nonpresent_submissions<<','<<stats.render_passes<<','<<stats.pipeline_creates<<','<<stats.shader_ms<<'\n';
    };
    pgr::vulkan::Renderer::Stats sync_totals{};
    pgr::gldirect::SyncProfile sync_producer{};
    auto add_sync=[&](const pgr::vulkan::Renderer::Stats& stats) {
        sync_totals.visibility_queries+=stats.visibility_queries;
        sync_totals.visibility_query_pool_creates+=stats.visibility_query_pool_creates;
        sync_totals.visibility_query_pool_reuses+=stats.visibility_query_pool_reuses;
        sync_totals.visibility_deferred_submissions+=stats.visibility_deferred_submissions;
        sync_totals.readback_requests+=stats.readback_requests; sync_totals.readback_bytes+=stats.readback_bytes;
        sync_totals.readback_buffer_creates+=stats.readback_buffer_creates;
        sync_totals.readback_buffer_reuses+=stats.readback_buffer_reuses;
        sync_totals.readback_resolve_ms+=stats.readback_resolve_ms;
        sync_totals.submissions+=stats.submissions; sync_totals.nonpresent_submissions+=stats.nonpresent_submissions;
        sync_totals.render_passes+=stats.render_passes; sync_totals.pipeline_creates+=stats.pipeline_creates;
        sync_totals.shader_ms+=stats.shader_ms;
    };
    if(sync_profile) {
        sync_log.open("sync.csv"); sync_submissions.open("sync-submissions.csv");
        sync_header(sync_log); sync_header(sync_submissions);
    }
    // EN: Scene capture follows guest vblanks instead of host wall time or
    // presentation number. Capturing drains the GPU; use a separate scene run.
    // PT-BR: Captura por vblank guest, independente do tempo host e do número
    // da apresentação. Capturar drena a GPU; use uma execução visual separada.
    std::vector<std::uint64_t> scene_vblanks;
    if(const char* schedule=std::getenv("PGR1_VK_SCENE_VBLANKS")) {
        std::istringstream input(schedule); std::string token;
        while(std::getline(input,token,',')) {
            std::size_t used=0;
            try {
                auto value=std::stoull(token,&used);
                if(used!=token.size() || token.empty() || token.front()=='-') throw std::invalid_argument("vblank");
                scene_vblanks.push_back(value);
            } catch(const std::exception&) {
                std::fprintf(stderr,"Invalid scene vblank / Vblank de cena inválido: %s\n",token.c_str());
                pgr::gldirect::Stop(); return 2;
            }
        }
        std::sort(scene_vblanks.begin(),scene_vblanks.end());
        scene_vblanks.erase(std::unique(scene_vblanks.begin(),scene_vblanks.end()),scene_vblanks.end());
    }
    std::size_t scene_index=0;
    std::ofstream scene_log;
    if(!scene_vblanks.empty()) {
        scene_log.open("scene-markers.csv");
        scene_log<<"requested_vblank,guest_vblank,simulation_tick,frame,path,captured\n";
    }
    pgr::gldirect::Submission pending{};
    double execute_ms=0,shader_ms=0,fence_ms=0,acquire_ms=0,present_ms=0,gpu_ms=0;
    std::uint64_t accepted=0,rejected=0,indexed=0,nonindexed=0,total_frames=0,total_accepted=0,total_rejected=0,last_guest_frame=0;
    std::uint64_t staging_allocations=0,staging_reuses=0,descriptor_creates=0,
                  descriptor_resets=0,framebuffer_hits=0,framebuffer_creates=0;
    std::uint64_t cube_creates=0,cube_hits=0,cube_copies=0;
    std::uint64_t pipeline_skips=0,viewport_skips=0,blend_skips=0;
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
                else if (event.key.scancode==SDL_SCANCODE_F11) {
                    quality::display_mode.store((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)?2u:1u);
                    quality::display_revision.fetch_add(1,std::memory_order_release);
                }
                else keys.down.insert(event.key.scancode);
            } else if (event.type==SDL_EVENT_KEY_UP) keys.down.erase(event.key.scancode);
        }
        now=std::chrono::steady_clock::now();
        const unsigned revision=quality::display_revision.load(std::memory_order_acquire);
        if(revision!=display_revision) {
            ApplyWindow(window); renderer->Resize(); display_revision=revision;
        }
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
            // 04D.3: polling at ~60 Hz reduces pause-menu input latency;
            // it does not change guest game simulation/timestep.
            next_input=now+std::chrono::milliseconds(16);
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
            if(sync_profile) {
                sync_row(sync_submissions,next,next.sync,stats);
                sync_producer.Add(next.sync); add_sync(stats);
                // Failed presentation attempts remain in the next successful interval.
                // Tentativas sem apresentação entram no próximo intervalo concluído.
                if(stats.presented) {
                    sync_totals.presented=true;
                    sync_row(sync_log,next,sync_producer,sync_totals);
                    sync_producer={}; sync_totals={};
                }
            }
            batch_log<<next.frame<<','<<stats.accepted<<','<<stats.render_passes<<','
                     <<stats.batched_groups<<','<<stats.batched_draws<<','
                     <<(stats.batched_draws-stats.batched_groups)<<'\n';
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
                <<stats.rejected<<','<<stats.indexed<<','<<stats.nonindexed<<','<<stats.acquire_timeout<<','<<!render_ok<<','<<stats.cube_cache_creates<<','<<stats.cube_cache_hits<<','<<stats.cube_face_copies<<','
                <<stats.staging_allocations<<','<<stats.staging_reuses<<','<<stats.descriptor_creates<<','
                <<stats.descriptor_resets<<','<<stats.framebuffer_hits<<','<<stats.framebuffer_creates<<','
                <<stats.presented_fps<<','<<stats.presented_frame_ms<<','<<(1u<<quality::msaa_applied.load())<<','
                <<quality::fxaa.load()<<','<<quality::render_percent.load()<<','
                <<next.texture_static_binds<<','<<next.texture_memcmp_checks<<','<<next.texture_memcmp_bytes<<','
                <<next.texture_same_draw_reuses<<','<<next.texture_decodes<<','
                <<next.texture_mip_levels_decoded<<','<<next.texture_mip_levels_reused<<','
                <<next.texture_cache_evictions<<','<<next.texture_cache_misses<<','
                <<next.texture_descriptor_redecodes<<','<<next.texture_guest_redecodes<<','
                <<next.texture_cache_entries_peak<<','<<stats.pipeline_bind_skips<<','
                <<stats.viewport_set_skips<<','<<stats.blend_set_skips<<','<<next.texture_overlap_checks<<','<<next.texture_diagnostic_skips<<','
                    <<next.texture_tracker_queries<<','<<next.texture_tracker_fast_hits<<','
                    <<next.texture_tracker_invalidations<<','<<next.texture_tracker_exact_fallbacks<<','
                    <<next.texture_tracker_uncovered_ranges<<','<<next.texture_tracker_false_positives<<','
                    <<next.texture_tracker_false_negatives<<','<<next.texture_tracker_monitored_writes<<','
                    <<next.texture_tracker_dirty_pages<<','<<next.texture_tracker_tracked_bytes<<','
                    <<next.texture_tracker_covered_queries<<','
                    <<next.texture_tracker_ns/1e6<<','<<next.texture_tracker_write_ns/1e6<<','
                    <<next.texture_journal_clean<<','<<next.texture_journal_dirty<<','<<next.texture_journal_unknown<<','<<next.texture_journal_false_negatives<<','<<next.texture_journal_false_positives<<','<<next.texture_journal_unstable<<','
                    <<next.texture_journal_ns/1e6<<','<<next.guest_vblank<<','<<pgr::gldirect::ReasonName(next.reason)<<','<<next.geometry_vertices<<','<<next.geometry_indices<<','<<next.geometry_bytes<<','<<next.simulation_tick<<'\n';
            pending.geometry_vertices+=next.geometry_vertices;
            pending.geometry_indices+=next.geometry_indices; pending.geometry_bytes+=next.geometry_bytes;
            pending.draw_ns+=next.draw_ns; pending.decode_ns+=next.decode_ns;
            pending.texture_check_ns+=next.texture_check_ns; pending.assemble_ns+=next.assemble_ns;
            pending.texture_static_binds+=next.texture_static_binds;
            pending.texture_memcmp_checks+=next.texture_memcmp_checks;
            pending.texture_memcmp_bytes+=next.texture_memcmp_bytes;
            pending.texture_same_draw_reuses+=next.texture_same_draw_reuses;
            pending.texture_decodes+=next.texture_decodes;
            pending.texture_mip_levels_decoded+=next.texture_mip_levels_decoded;
            pending.texture_mip_levels_reused+=next.texture_mip_levels_reused;
            pending.texture_cache_evictions+=next.texture_cache_evictions;
            pending.texture_cache_misses+=next.texture_cache_misses;
            pending.texture_descriptor_redecodes+=next.texture_descriptor_redecodes;
            pending.texture_guest_redecodes+=next.texture_guest_redecodes;
            pending.texture_overlap_checks+=next.texture_overlap_checks;
            pending.texture_diagnostic_skips+=next.texture_diagnostic_skips;
            pending.texture_tracker_queries+=next.texture_tracker_queries;
            pending.texture_tracker_fast_hits+=next.texture_tracker_fast_hits;
            pending.texture_tracker_invalidations+=next.texture_tracker_invalidations;
            pending.texture_tracker_exact_fallbacks+=next.texture_tracker_exact_fallbacks;
            pending.texture_tracker_uncovered_ranges+=next.texture_tracker_uncovered_ranges;
            pending.texture_tracker_false_positives+=next.texture_tracker_false_positives;
            pending.texture_tracker_false_negatives+=next.texture_tracker_false_negatives;
            pending.texture_tracker_monitored_writes+=next.texture_tracker_monitored_writes;
            pending.texture_tracker_dirty_pages+=next.texture_tracker_dirty_pages;
            pending.texture_tracker_covered_queries+=next.texture_tracker_covered_queries;
            pending.texture_tracker_ns+=next.texture_tracker_ns;
            pending.texture_tracker_write_ns+=next.texture_tracker_write_ns;
            pending.texture_journal_clean+=next.texture_journal_clean;
            pending.texture_journal_dirty+=next.texture_journal_dirty;
            pending.texture_journal_unknown+=next.texture_journal_unknown;
            pending.texture_journal_false_negatives+=next.texture_journal_false_negatives;
            pending.texture_journal_false_positives+=next.texture_journal_false_positives;
            pending.texture_journal_unstable+=next.texture_journal_unstable;
            pending.texture_journal_ns+=next.texture_journal_ns;
            // EN/PT-BR: Residency gauge / Ocupação usa o pico.
            pending.texture_tracker_tracked_bytes=std::max(
                pending.texture_tracker_tracked_bytes,next.texture_tracker_tracked_bytes);
            // EN: Cache residency is a maximum, not an additive metric.
            // PT-BR: A ocupação do cache usa o máximo, nunca uma soma.
            pending.texture_cache_entries_peak=std::max(
                pending.texture_cache_entries_peak,next.texture_cache_entries_peak);
            pending.max_draw_ns=std::max(pending.max_draw_ns,next.max_draw_ns);
            pending.queue_wait_ns+=next.queue_wait_ns; pending.supported+=next.supported;
            pending.rejected_draws+=next.rejected_draws;
            pipeline_skips+=stats.pipeline_bind_skips;
            viewport_skips+=stats.viewport_set_skips;
            blend_skips+=stats.blend_set_skips;
            execute_ms+=stats.execute_ms; shader_ms+=stats.shader_ms; fence_ms+=stats.fence_ms;
            acquire_ms+=stats.acquire_ms; present_ms+=stats.present_ms; gpu_ms+=completed_gpu_ms;
            accepted+=stats.accepted; rejected+=stats.rejected; indexed+=stats.indexed; nonindexed+=stats.nonindexed;
            staging_allocations+=stats.staging_allocations; staging_reuses+=stats.staging_reuses;
            descriptor_creates+=stats.descriptor_creates; descriptor_resets+=stats.descriptor_resets;
            framebuffer_hits+=stats.framebuffer_hits; framebuffer_creates+=stats.framebuffer_creates;
            cube_creates+=stats.cube_cache_creates; cube_hits+=stats.cube_cache_hits; cube_copies+=stats.cube_face_copies;
            if (next.present) {
                const auto at=std::chrono::steady_clock::now();
                const auto cpu=std::clock();
                timings<<next.frame<<','<<ms(at-last_frame)<<','<<1000.0*double(cpu-last_cpu)/CLOCKS_PER_SEC<<','
                    <<pending.draw_ns/1e6<<','<<pending.max_draw_ns/1e6<<','<<pending.decode_ns/1e6<<','
                    <<pending.texture_check_ns/1e6<<','<<pending.assemble_ns/1e6<<','<<pending.queue_wait_ns/1e6<<','
                    <<execute_ms<<','<<shader_ms<<','<<fence_ms<<','<<acquire_ms<<','<<present_ms<<','<<gpu_ms<<','
                    <<pending.supported<<','<<pending.rejected_draws<<','<<accepted<<','<<rejected<<','<<indexed<<','
                    <<nonindexed<<','<<stats.presented<<','<<stats.acquire_timeout<<','<<!render_ok<<','<<cube_creates<<','<<cube_hits<<','<<cube_copies<<','
                    <<staging_allocations<<','<<staging_reuses<<','<<descriptor_creates<<','<<descriptor_resets<<','
                    <<framebuffer_hits<<','<<framebuffer_creates<<','<<stats.presented_fps<<','<<stats.presented_frame_ms<<','
                    <<(1u<<quality::msaa_applied.load())<<','<<quality::fxaa.load()<<','<<quality::render_percent.load()<<','
                    <<pending.texture_static_binds<<','<<pending.texture_memcmp_checks<<','<<pending.texture_memcmp_bytes<<','
                    <<pending.texture_same_draw_reuses<<','<<pending.texture_decodes<<','
                    <<pending.texture_mip_levels_decoded<<','<<pending.texture_mip_levels_reused<<','
                    <<pending.texture_cache_evictions<<','<<pending.texture_cache_misses<<','
                    <<pending.texture_descriptor_redecodes<<','<<pending.texture_guest_redecodes<<','
                    <<pending.texture_cache_entries_peak<<','<<pipeline_skips<<','
                    <<viewport_skips<<','<<blend_skips<<','<<pending.texture_overlap_checks<<','<<pending.texture_diagnostic_skips<<','
                    <<pending.texture_tracker_queries<<','<<pending.texture_tracker_fast_hits<<','
                    <<pending.texture_tracker_invalidations<<','<<pending.texture_tracker_exact_fallbacks<<','
                    <<pending.texture_tracker_uncovered_ranges<<','<<pending.texture_tracker_false_positives<<','
                    <<pending.texture_tracker_false_negatives<<','<<pending.texture_tracker_monitored_writes<<','
                    <<pending.texture_tracker_dirty_pages<<','<<pending.texture_tracker_tracked_bytes<<','
                    <<pending.texture_tracker_covered_queries<<','
                    <<pending.texture_tracker_ns/1e6<<','<<pending.texture_tracker_write_ns/1e6<<','
                    <<pending.texture_journal_clean<<','<<pending.texture_journal_dirty<<','<<pending.texture_journal_unknown<<','<<pending.texture_journal_false_negatives<<','<<pending.texture_journal_false_positives<<','<<pending.texture_journal_unstable<<','
                    <<pending.texture_journal_ns/1e6<<','<<next.guest_vblank<<','<<pgr::gldirect::ReasonName(next.reason)<<','<<pending.geometry_vertices<<','<<pending.geometry_indices<<','<<pending.geometry_bytes<<','<<next.simulation_tick<<'\n';
                if (stats.presented) ++total_frames;
                last_frame=at; last_cpu=cpu; pending={};
                execute_ms=shader_ms=fence_ms=acquire_ms=present_ms=gpu_ms=0;
                accepted=rejected=indexed=nonindexed=0;
                staging_allocations=staging_reuses=descriptor_creates=descriptor_resets=0;
                framebuffer_hits=framebuffer_creates=0;
                cube_creates=cube_hits=cube_copies=0;
                pipeline_skips=viewport_skips=blend_skips=0;
            }
            if (!render_ok) { boot_failed=true; break; }
            if(next.present && stats.presented && scene_index<scene_vblanks.size() &&
               next.guest_vblank>=scene_vblanks[scene_index]) {
                const auto requested=scene_vblanks[scene_index++];
                const auto path="scene-vblank-"+std::to_string(requested)+".ppm";
                bool captured=false;
                record_diagnostic_samples([&] { captured=renderer->Capture(path.c_str()); });
                scene_log<<requested<<','<<next.guest_vblank<<','<<next.simulation_tick<<','<<next.frame<<','<<path<<','<<captured<<'\n';
                scene_log.flush();
            }
            if (!next.reference_pixels.empty()) record_diagnostic_samples([&] { renderer->Compare(next); });
            if (capture_frame && next.present && next.frame==capture_frame) {
                record_diagnostic_samples([&] { captured_frame=renderer->Capture(capture_frame_path); });
                std::printf("Vulkan exact guest frame %llu capture %s: %s\n",
                    static_cast<unsigned long long>(capture_frame),capture_frame_path,captured_frame?"saved":"failed");
            }
        } else {
            // EN: The guest can be waiting for a fence without submitting more
            // work. Poll the pending GPU fence so that its resource token
            // completes without forcing a synchronous wait after every draw.
            // PT-BR: O jogo pode aguardar uma fence sem gerar novos comandos;
            // a sondagem conclui o token sem travar a thread a cada submissao.
            try {
                const auto previous_fence=renderer->LastStats().fence_ms;
                const auto previous_resolve=renderer->LastStats().readback_resolve_ms;
                if(renderer->PollCompletion()) {
                    const auto& completed=renderer->LastStats();
                    // EN: Poll retains prior counters; add only new wait costs.
                    // PT-BR: Poll conserva contadores; some só custos novos.
                    fence_ms+=completed.fence_ms-previous_fence;
                    if(sync_profile)sync_totals.readback_resolve_ms+=completed.readback_resolve_ms-previous_resolve;
                    for(const auto& sample:completed.gpu_samples) {
                        gpu_times<<sample.frame<<','<<sample.milliseconds<<'\n';
                        gpu_ms+=sample.milliseconds;
                    }
                }
            } catch(const std::exception& ex) {
                std::fprintf(stderr,"Vulkan GPU completion poll failed: %s\n",ex.what());
                pgr::gldirect::Fail(ex.what()); boot_failed=true; break;
            }
            // EN: Wake immediately on a submission; still poll fences regularly.
            // PT-BR: Acorde ao receber submissões; consulte fences periodicamente.
            pgr::gldirect::WaitForWork(std::chrono::milliseconds(1));
        }
        if (now>=next_title) {
            // EN: _Exit bypasses iostream destructors; flush every CSV, including
            // vk_batches.csv, before periodic snapshots and final shutdown.
            // PT-BR: _Exit ignora destruidores de streams; grave tambem
            // vk_batches.csv em disco antes de relatorios e do encerramento.
            timings.flush(); submissions.flush(); gpu_times.flush(); batch_log.flush(); sync_log.flush(); sync_submissions.flush(); logx::Flush();
            if (fs::is_regular_file("snap")) {
                record_diagnostic_samples([&] {
                    if (!renderer->Capture("snap.ppm")) std::fputs("vulkan: requested snapshot failed\n",stderr);
                });
                std::error_code error; fs::remove("snap",error);
            }
            if(fs::is_regular_file("variants")) {
                record_diagnostic_samples([&] { CaptureVariants(*renderer); });
                std::error_code error; fs::remove("variants",error);
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
    if (const char* capture=std::getenv("PGR1_CAPTURE"))
        record_diagnostic_samples([&] {
            if(!renderer->Capture(capture)) std::fputs("vulkan: final capture failed\n",stderr);
        });
    if(const char* variants=std::getenv("PGR1_CAPTURE_VARIANTS");variants && *variants=='1')
        record_diagnostic_samples([&] { CaptureVariants(*renderer); });
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::printf("PGR1 NATIVE Vulkan game: guest_frame=%llu presented=%llu accepted=%llu rejected=%llu elapsed_s=%.3f\n",
        static_cast<unsigned long long>(last_guest_frame),static_cast<unsigned long long>(total_frames),
        static_cast<unsigned long long>(total_accepted),static_cast<unsigned long long>(total_rejected),elapsed);
    // EN: _Exit bypasses iostream destructors; flush every CSV, including
            // vk_batches.csv, before periodic snapshots and final shutdown.
            // PT-BR: _Exit ignora destruidores de streams; grave tambem
            // vk_batches.csv em disco antes de relatorios e do encerramento.
            timings.flush(); submissions.flush(); gpu_times.flush(); batch_log.flush(); sync_log.flush(); sync_submissions.flush(); logx::Flush();
    pgr::gldirect::StopAudio();
    if (had_input) SendCommand("");
    if (pad) SDL_CloseGamepad(pad);
    renderer.reset();
    SDL_DestroyWindow(window); SDL_Quit();
    // The SDK kernel has no joining API for its long-running worker threads.
    std::fflush(stdout); std::fflush(stderr);
    std::_Exit(boot_failed?1:0);
}
