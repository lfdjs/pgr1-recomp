#pragma once
#include <cstdint>

namespace pgr::gldirect {
// All guest metadata that determines the decoded image. Reuse is valid only
// within one synchronous Draw(): unmarked direct writes may occur between draws.
struct TextureSnapshotKey {
    std::uint32_t address=0, format=0, width=0, height=0, depth=0, levels=0, pitch=0;
    bool cube=false, linear=false;
    constexpr bool operator==(const TextureSnapshotKey&) const = default;
};
} // namespace pgr::gldirect
