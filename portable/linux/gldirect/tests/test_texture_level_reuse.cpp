#include "texture_level_reuse.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

using pgr::gldirect::TextureLevelUnchanged;
int main() {
    // EN/PT-BR: Equal levels, mutations, boundary checks / níveis iguais, alterações, limites.
    std::array<std::uint8_t,80> live{},previous{};
    for(std::size_t i=0;i<live.size();++i) live[i]=previous[i]=std::uint8_t(i);
    assert(TextureLevelUnchanged(live.data(),previous.data(),80,0,64));
    assert(TextureLevelUnchanged(live.data(),previous.data(),80,64,16));
    live[3]^=0xff;
    assert(!TextureLevelUnchanged(live.data(),previous.data(),80,0,64));
    assert(TextureLevelUnchanged(live.data(),previous.data(),80,64,16));
    live[77]^=0xff;
    assert(!TextureLevelUnchanged(live.data(),previous.data(),80,64,16));
    assert(!TextureLevelUnchanged(live.data(),previous.data(),80,80,1));
    assert(!TextureLevelUnchanged(live.data(),previous.data(),80,75,6));
    assert(!TextureLevelUnchanged(live.data(),previous.data(),80,0,0));
    assert(!TextureLevelUnchanged(nullptr,previous.data(),80,0,8));
    assert(!TextureLevelUnchanged(live.data(),nullptr,80,0,8));
    std::puts("OK: identical, changed and bounds-checked mip levels");
}
