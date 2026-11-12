#pragma once
// Small versioned Linux preferences file. No dependency on the SDK's Windows
// TOML/registry services, and unknown keys survive a save for forward compatibility.
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include "app/app.h"

namespace pgr::linux_settings {
using Values = std::map<std::string, uint32_t>;
inline std::string_view Trim(std::string_view s) {
    const auto first=s.find_first_not_of(" \t\r\n");
    if(first==s.npos) return {};
    return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
}
inline bool Number(std::string_view text,uint32_t& value) {
    text=Trim(text);
    if(text=="true") { value=1; return true; }
    if(text=="false") { value=0; return true; }
    if(text.empty()) return false;
    const auto [p,ec]=std::from_chars(text.data(),text.data()+text.size(),value);
    return ec==std::errc{} && p==text.data()+text.size();
}
inline Values Read(const std::filesystem::path& path) {
    std::ifstream input(path);
    Values out;
    for(std::string line;std::getline(input,line);) {
        auto view=Trim(line);
        view=view.substr(0,view.find('#'));
        const auto equals=view.find('=');
        if(equals==view.npos) continue;
        const auto key=Trim(view.substr(0,equals));
        uint32_t value=0;
        if(!key.empty() && Number(view.substr(equals+1),value)) out[std::string(key)]=value;
    }
    return out;
}
inline bool Write(const std::filesystem::path& path,const Values& values,std::error_code& error) {
    error.clear();
    if(path.empty()) { error=std::make_error_code(std::errc::invalid_argument); return false; }
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(),error);
    if(error) return false;
    auto temp=path;
    temp += ".tmp."+std::to_string(getpid());
    {
        std::ofstream output(temp,std::ios::trunc);
        output << "# PGR1 native Linux preferences, version 1\n";
        for(const auto& [key,value]:values) output << key << '=' << value << '\n';
        output.close();
        if(!output) { error=std::make_error_code(std::errc::io_error); std::error_code ignored; std::filesystem::remove(temp,ignored); return false; }
    }
    std::filesystem::rename(temp,path,error);
    if(error) { std::error_code ignored; std::filesystem::remove(temp,ignored); }
    return !error;
}
inline std::filesystem::path Path() {
    if(const char* p=std::getenv("PGR1_CONFIG_FILE");p && *p) return p;
    if(const char* p=std::getenv("XDG_CONFIG_HOME");p && *p && std::filesystem::path(p).is_absolute())
        return std::filesystem::path(p)/"pgr1-recomp"/"linux.conf";
    if(const char* p=std::getenv("HOME");p && *p)
        return std::filesystem::path(p)/".config"/"pgr1-recomp"/"linux.conf";
    return {}; // an account without a config directory runs without persistence
}
inline void Validate(app::Settings& s) {
    if(s.render_percent && (s.render_percent<25 || s.render_percent>200)) s.render_percent=100;
    if(s.fps_cap>1000) s.fps_cap=60;
    if(s.sharpen>100) s.sharpen=100;
    if(s.aniso!=0 && s.aniso!=2 && s.aniso!=4 && s.aniso!=8 && s.aniso!=16) s.aniso=0;
#ifdef PGR1_NATIVE_VULKAN
    if(s.msaa>2) s.msaa=0; // 8x intentionally unavailable in the native renderer
#else
    if(s.msaa>3) s.msaa=0;
#endif
    if(s.scale_filter>1) s.scale_filter=0;
    if(uint32_t(s.display_mode)>2) s.display_mode=app::DisplayMode::Borderless;
    if((s.width==0)!=(s.height==0) || (s.width && (s.width<320 || s.height<240 || s.width>16384 || s.height>16384))) s.width=s.height=0;
    if(s.aspect>3) s.aspect=0;
#ifdef PGR1_NATIVE_VULKAN
    s.monitor=0; // SDL primary display is the only implemented selection
    s.smaa=false;
    s.world_textures=false; s.sharp_2d=false; // native capture does not use SDK replacement textures
    s.low_latency=false; // the Vulkan renderer already has one submission in flight
    s.pause_unfocused=false; // no game/audio suspension protocol exists in this host
#endif
    s.mouse_sensitivity=std::clamp(s.mouse_sensitivity,25u,800u);
    s.wheel_range=std::clamp(s.wheel_range,180u,2880u);
    s.wheel_lock=std::clamp(s.wheel_lock,180u,900u);
    s.wheel_deadzone=std::min(s.wheel_deadzone,20u);
    s.ffb_strength=std::min(s.ffb_strength,100u); s.ffb_road=std::min(s.ffb_road,100u);
}
inline void Load(app::Settings& s,const Values& v) {
    auto number=[&](const char* key,uint32_t& dest) { if(auto it=v.find(key);it!=v.end()) dest=it->second; };
    auto flag=[&](const char* key,bool& dest) { if(auto it=v.find(key);it!=v.end() && it->second<=1) dest=it->second!=0; };
    uint32_t mode=uint32_t(s.display_mode); number("display_mode",mode); s.display_mode=app::DisplayMode(mode);
    number("width",s.width); number("height",s.height); number("render_percent",s.render_percent);
    number("fps_cap",s.fps_cap); number("aniso",s.aniso); number("msaa",s.msaa); number("sharpen",s.sharpen);
    number("scale_filter",s.scale_filter); number("aspect",s.aspect); number("language",s.language);
    number("mouse_sensitivity",s.mouse_sensitivity); number("wheel_range",s.wheel_range); number("wheel_lock",s.wheel_lock);
    number("wheel_deadzone",s.wheel_deadzone); number("ffb_strength",s.ffb_strength); number("ffb_road",s.ffb_road);
    flag("vsync",s.vsync); flag("show_fps",s.show_fps); flag("fxaa",s.fxaa); flag("alpha_coverage",s.alpha_coverage);
    flag("keyboard",s.keyboard); flag("mouse_look",s.mouse_look); flag("mouse_invert_y",s.mouse_invert_y);
    flag("invert_steering",s.wheel_steer.invert);
    Validate(s);
}
inline void Save(const app::Settings& s,Values& v) {
    v["version"]=1;
    v["display_mode"]=uint32_t(s.display_mode); v["width"]=s.width; v["height"]=s.height;
    v["render_percent"]=s.render_percent; v["fps_cap"]=s.fps_cap; v["aniso"]=s.aniso; v["msaa"]=s.msaa;
    v["sharpen"]=s.sharpen; v["scale_filter"]=s.scale_filter; v["aspect"]=s.aspect; v["language"]=s.language;
    v["vsync"]=s.vsync; v["show_fps"]=s.show_fps; v["fxaa"]=s.fxaa; v["alpha_coverage"]=s.alpha_coverage;
    v["keyboard"]=s.keyboard; v["mouse_look"]=s.mouse_look; v["mouse_invert_y"]=s.mouse_invert_y;
    v["mouse_sensitivity"]=s.mouse_sensitivity; v["wheel_range"]=s.wheel_range; v["wheel_lock"]=s.wheel_lock;
    v["wheel_deadzone"]=s.wheel_deadzone; v["ffb_strength"]=s.ffb_strength; v["ffb_road"]=s.ffb_road;
    v["invert_steering"]=s.wheel_steer.invert;
}
} // namespace pgr::linux_settings
