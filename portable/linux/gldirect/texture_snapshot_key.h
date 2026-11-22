#pragma once
#include <cstdint>

namespace pgr::gldirect {
// EN: Guest metadata identifies an immutable decoded image for one synchronous Draw().
// PT-BR: Os metadados identificam uma imagem decodificada imutável em um mesmo Draw().
// EN: Unmarked direct guest writes may occur between draws; never skip that validation.
// PT-BR: O jogo pode alterar a memória entre draws sem marcar; não ignore essa validação.
struct TextureSnapshotKey {
    std::uint32_t address=0, format=0, width=0, height=0, depth=0, levels=0, pitch=0;
    bool cube=false, linear=false;
    constexpr bool operator==(const TextureSnapshotKey&) const = default;
};
} // namespace pgr::gldirect
