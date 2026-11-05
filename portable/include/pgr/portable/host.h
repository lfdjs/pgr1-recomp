#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace pgr::portable {

enum class Platform : std::uint8_t {
    Windows,
    Linux,
    NintendoSwitch,
    PlayStationVita,
    Unknown,
};

struct Capabilities {
    Platform platform = Platform::Unknown;
    std::uint32_t pointer_bits = 0;
    bool little_endian = false;
    // Address width alone does not guarantee that a 4 GiB reservation will succeed.
    bool has_64bit_address_space = false;
};

const char* PlatformName(Platform platform) noexcept;

// Small host-services boundary. The Xbox kernel, renderer and audio mixer remain
// in the SDK; backends for those subsystems will be introduced separately.
class Host {
public:
    virtual ~Host() = default;
    virtual Capabilities GetCapabilities() const noexcept = 0;
    virtual std::uint64_t MonotonicNanoseconds() const noexcept = 0;
    virtual std::filesystem::path UserDataDirectory(std::string_view app_id) const = 0;
};

// Desktop implementation: Windows and Linux. Console backends come later.
std::unique_ptr<Host> CreateDesktopHost();

}  // namespace pgr::portable
