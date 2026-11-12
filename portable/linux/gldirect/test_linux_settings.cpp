#include "settings_store.h"
#include "display.h"
#include "../vulkan/quality_state.h"
#include <cassert>
#include <iostream>
#include "../../../src/game/settings_entry.cpp"

namespace pgr1 {
bool skip_intro=false,hide_hud=false;
uint32_t frame_rate=60,fov_percent=100,radio_percent=150;
}
// UI hooks for unrelated wheel/quit rows are linked by their Row function
// pointers but never invoked by this test. The settings/menu code is real.
namespace input { void WheelSetup() {} }
namespace logx { void Write(Level,const char*,...) {} void Flush() {} }
namespace kernel { [[noreturn]] void Shutdown(int) { std::abort(); } }
const app::GameInfo app::kGame=[] {
    app::GameInfo game{};
    game.options={{"skip_intro",&pgr1::skip_intro,nullptr},{"frame_rate",nullptr,&pgr1::frame_rate}};
    return game;
}();

int main() {
    namespace ls=pgr::linux_settings;
    namespace q=pgr::vulkan::quality;
    const auto dir=std::filesystem::temp_directory_path()/("pgr1-settings-test-"+std::to_string(getpid()));
    const auto file=dir/"linux.conf";
    std::error_code error;
    std::filesystem::create_directories(dir);
    unsetenv("PGR1_CONFIG_FILE");
    setenv("XDG_CONFIG_HOME",dir.c_str(),1);
    assert(ls::Path()==dir/"pgr1-recomp"/"linux.conf");
    setenv("XDG_CONFIG_HOME","relative-path",1);
    assert(ls::Path().is_absolute()); // relative XDG paths are ignored
    setenv("PGR1_CONFIG_FILE",file.c_str(),1);
    for(const char* key:{"FPS","OUTPUT","PGR1_FULLSCREEN","PGR1_VK_INTERNAL_PERCENT","PGR1_VK_FXAA","PGR1_VK_SHARPEN","PGR1_VK_ANISO","PGR1_VK_MSAA","PGR1_VK_VSYNC","PGR1_SHOW_FPS","PGR1_VK_PRESENT_MODE"}) unsetenv(key);
    ls::Values initial{{"fxaa",0},{"show_fps",1},{"render_percent",75},{"msaa",1},{"skip_intro",1},{"frame_rate",144},{"future_setting",42}};
    assert(ls::Write(file,initial,error));
    app::Load();
    assert(!app::Get().fxaa && !q::fxaa && q::show_fps && q::render_percent==75 && q::msaa==1);
    assert(pgr1::skip_intro && pgr1::frame_rate==144 && q::display_revision>0);
    // Menu edits use app::Save: verify the real cross-thread publication and the disk roundtrip.
    q::msaa_supported_mask.store(1|2|4);
    app::Get().fxaa=true; app::Get().show_fps=false; app::Get().msaa=2;
    app::Get().fps_cap=30; app::Get().sharpen=25; app::Get().scale_filter=1;
    app::Get().display_mode=app::DisplayMode::Windowed;
    app::Get().width=1280; app::Get().height=720;
    app::ApplyDisplay(); app::Save();
    assert(q::fxaa && !q::show_fps && q::msaa==2 && q::fps_cap==30 && q::sharpen_percent==25 && q::sharp_scaling);
    assert(q::output_width==1280 && q::output_height==720 && q::display_mode==2);
    pgr::gldirect::SetOutputPixels(1280,720);
    app::Get().aspect=1; app::Save(); assert(!app::Widescreen() && app::FrameAspect()==4.0f/3);
    app::Get().aspect=2; app::Save(); assert(app::Widescreen() && app::FrameAspect()==16.0f/9);
    auto saved=ls::Read(file);
    assert(saved.at("future_setting")==42 && saved.at("msaa")==2 && saved.at("show_fps")==0);
    app::Get()={}; app::Load();
    assert(app::Get().fxaa && app::Get().width==1280 && app::Get().height==720 && app::Get().fps_cap==30);
    // SDL fallback/F11 is the source of truth, including when another menu row is saved.
    q::display_mode.store(2); app::Get().display_mode=app::DisplayMode::Fullscreen; app::Save();
    assert(app::Get().display_mode==app::DisplayMode::Windowed);
    // Hardware fallback never persists a mode the active GPU cannot use.
    q::msaa_supported_mask.store(1); app::Get().msaa=2; app::Save();
    assert(app::Get().msaa==0 && q::msaa==0);
    q::postfx_supported.store(false); q::aniso_supported_max.store(8); q::vsync.store(true);
    app::Get().fxaa=true; app::Get().sharpen=75; app::Get().scale_filter=1; app::Get().aniso=16; app::Get().msaa=UINT32_MAX; app::Get().vsync=false;
    app::Save();
    assert(!q::fxaa && q::sharpen_percent==0 && !q::sharp_scaling && q::aniso==8 && q::vsync && !q::msaa);
    q::postfx_supported.store(true); q::aniso_supported_max.store(16);
    // Startup env overrides file; subsequent menu edits are live even while env remains set.
    setenv("PGR1_VK_FXAA","0",1); setenv("PGR1_VK_INTERNAL_PERCENT","100",1);
    setenv("PGR1_VK_MSAA","4",1); setenv("OUTPUT","1920x1080",1); setenv("FPS","120",1);
    app::Load();
    assert(!q::fxaa && q::render_percent==100 && q::msaa==2 && q::fps_cap==120 && q::output_width==1920);
    app::Get().fxaa=true; app::Save(); assert(q::fxaa);
    setenv("OUTPUT","4294967936x480",1); app::Load();
    assert(q::output_width==1920 && q::output_height==1080); // no overflow wrap to640
    unsetenv("OUTPUT");
    setenv("PGR1_VK_VSYNC","0",1); setenv("PGR1_VK_PRESENT_MODE","fifo",1); app::Load(); assert(q::vsync);
    // Exercise the actual menu Row callbacks and Changed -> app::Save path.
    auto row=[](const auto& rows,const char* label)->const pgr1::Row& {
        for(const auto& entry:rows) if(std::string_view(entry.label)==label) return entry;
        std::abort();
    };
    const auto& counter=row(pgr1::kPerformance,"frame counter");
    pgr1::Changed(counter,1); assert(counter.get()==1 && q::show_fps);
    pgr1::Changed(counter,0); assert(counter.get()==0 && !q::show_fps);
    const auto& smoothing=row(pgr1::kPicture,"edge smoothing");
    pgr1::Changed(smoothing,0); assert(smoothing.get()==0 && !q::fxaa);
    pgr1::Changed(smoothing,1); assert(smoothing.get()==1 && q::fxaa);
    pgr1::Changed(row(pgr1::kPicture,"sharpening"),2); assert(q::sharpen_percent==50);
    pgr1::Changed(row(pgr1::kPerformance,"render scale"),0); assert(q::render_percent==0);
    pgr1::Changed(row(pgr1::kPerformance,"render scale"),4); assert(q::render_percent==100);
    pgr1::Changed(row(pgr1::kPerformance,"frame rate cap"),0); assert(q::fps_cap==30);
    pgr1::Changed(row(pgr1::kDisplay,"vertical sync"),0); assert(!q::vsync);
    q::msaa_supported_mask.store(1|4); q::msaa_applied.store(0);
    const auto& aa=row(pgr1::kPicture,"msaa");
    assert(aa.choices().size()==2);
    pgr1::Changed(aa,1); assert(q::msaa==2 && aa.get()==1 && aa.choices()[1]=="4x (pending)");
    q::msaa_applied.store(2); assert(aa.choices()[1]=="4x");
    q::postfx_supported.store(false); q::fxaa.store(false); q::sharpen_percent.store(0);
    assert(smoothing.choices().size()==1 && smoothing.choices()[0]=="unavailable");
    assert(row(pgr1::kPicture,"smaa").choices()[0]=="unavailable");
    // Invalid and oversized numeric input cannot wrap into runtime settings.
    uint32_t parsed=7;
    assert(!ls::Number("-1",parsed) && !ls::Number("4294967296",parsed) && !ls::Number("60junk",parsed));
    auto invalid=ls::Values{{"fps_cap",99999},{"render_percent",1},{"sharpen",150},{"msaa",3},{"width",0},{"height",720},{"display_mode",9},{"aniso",17},{"show_fps",2}};
    app::Settings clean; ls::Load(clean,invalid);
    assert(clean.fps_cap==60 && clean.render_percent==100 && clean.sharpen==100 && clean.msaa==0 && clean.width==0 && clean.height==0 && clean.aniso==0 && !clean.show_fps);
    assert(clean.display_mode==app::DisplayMode::Borderless);
    { std::ofstream malformed(file); malformed<<"fxaa=abc\nrender_percent=-1\nshow_fps=true\nunknown=13 # valid\nwidth=640junk\n"; }
    const auto malformed=ls::Read(file);
    assert(!malformed.contains("fxaa") && !malformed.contains("render_percent") && malformed.at("show_fps")==1 && malformed.at("unknown")==13 && !malformed.contains("width"));
    // A failed replacement leaves the previous complete preferences intact.
    assert(!ls::Write(dir,initial,error) && error);
    assert(ls::Read(file)==malformed);
    std::filesystem::remove_all(dir);
    std::cout<<"Linux settings: actual menu callbacks, publication, environment precedence, persistence, validation and fallback passed\n";
}
