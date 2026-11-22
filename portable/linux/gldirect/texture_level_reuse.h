#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pgr::gldirect {
// EN: Reuse a decoded mip only if its complete guest byte span is identical.
// PT-BR: Reutilize um mip apenas se todos os bytes na memória forem iguais.
inline bool TextureLevelUnchanged(const std::uint8_t* live,
                                  const std::uint8_t* snapshot,
                                  std::size_t total_bytes,
                                  std::size_t offset,
                                  std::size_t count) {
    return live && snapshot && count!=0 && offset<=total_bytes &&
           count<=total_bytes-offset &&
           std::memcmp(live+offset,snapshot+offset,count)==0;
}
} // namespace pgr::gldirect
