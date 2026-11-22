#include "texture_snapshot_key.h"
#include <array>
#include <cassert>
#include <cstdio>
using pgr::gldirect::TextureSnapshotKey;
constexpr TextureSnapshotKey base{0x40000, 0x12, 128, 128, 1, 1, 512, false, true};
static_assert(base == base);
int main() {
    constexpr auto same=base;
    static_assert(base==same);
    constexpr std::array<TextureSnapshotKey,9> changed{{
        {base.address+1,base.format,base.width,base.height,base.depth,base.levels,base.pitch,base.cube,base.linear},
        {base.address,base.format+1,base.width,base.height,base.depth,base.levels,base.pitch,base.cube,base.linear},
        {base.address,base.format,base.width+1,base.height,base.depth,base.levels,base.pitch,base.cube,base.linear},
        {base.address,base.format,base.width,base.height+1,base.depth,base.levels,base.pitch,base.cube,base.linear},
        {base.address,base.format,base.width,base.height,base.depth+1,base.levels,base.pitch,base.cube,base.linear},
        {base.address,base.format,base.width,base.height,base.depth,base.levels+1,base.pitch,base.cube,base.linear},
        {base.address,base.format,base.width,base.height,base.depth,base.levels,base.pitch+1,base.cube,base.linear},
        {base.address,base.format,base.width,base.height,base.depth,base.levels,base.pitch,true,base.linear},
        {base.address,base.format,base.width,base.height,base.depth,base.levels,base.pitch,base.cube,false}
    }};
    for (const auto& key:changed) assert(!(base==key));
    std::puts("Texture snapshot key tests passed: exact metadata required.");
}
