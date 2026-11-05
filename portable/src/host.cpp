#include "pgr/portable/host.h"

#include <bit>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace pgr::portable {
namespace {

constexpr Platform DetectPlatform() noexcept {
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__linux__)
    return Platform::Linux;
#else
    return Platform::Unknown;
#endif
}

const char* NonEmptyEnv(const char* name) noexcept {
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
}

class DesktopHost final : public Host {
public:
    Capabilities GetCapabilities() const noexcept override {
        return {
            .platform = DetectPlatform(),
            .pointer_bits = static_cast<std::uint32_t>(sizeof(void*) * 8),
            .little_endian = std::endian::native == std::endian::little,
            .has_64bit_address_space = sizeof(void*) >= 8,
        };
    }

    std::uint64_t MonotonicNanoseconds() const noexcept override {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::filesystem::path UserDataDirectory(std::string_view app_id) const override {
        if (app_id.empty() || app_id.find('/') != app_id.npos || app_id.find('\\') != app_id.npos ||
            app_id == "." || app_id == "..") {
            throw std::invalid_argument("app_id must be one path component");
        }
        const std::filesystem::path suffix{std::string(app_id)};
#if defined(_WIN32)
        if (const char* appdata = NonEmptyEnv("APPDATA")) return std::filesystem::path(appdata) / suffix;
        if (const char* profile = NonEmptyEnv("USERPROFILE")) return std::filesystem::path(profile) / "AppData" / "Roaming" / suffix;
#elif defined(__linux__)
        if (const char* xdg = NonEmptyEnv("XDG_DATA_HOME")) {
            const std::filesystem::path base(xdg);
            if (base.is_absolute()) return base / suffix;
        }
        if (const char* home = NonEmptyEnv("HOME")) return std::filesystem::path(home) / ".local" / "share" / suffix;
#endif
        throw std::runtime_error("Cannot resolve the user data directory on this host");
    }
};

}  // namespace

const char* PlatformName(Platform platform) noexcept {
    switch (platform) {
        case Platform::Windows: return "Windows";
        case Platform::Linux: return "Linux";
        case Platform::NintendoSwitch: return "Nintendo Switch";
        case Platform::PlayStationVita: return "PlayStation Vita";
        default: return "Unknown";
    }
}

std::unique_ptr<Host> CreateDesktopHost() {
    return std::make_unique<DesktopHost>();
}

}  // namespace pgr::portable
