#pragma once
#include <cstddef>
#include <string_view>

namespace pgr::vulkan::device {
// EN: An unset, empty or "auto" selector enables Vulkan GPU discovery.
// PT-BR: Seletor ausente, vazio ou "auto" ativa a descoberta Vulkan.
constexpr char LowerAscii(char ch) {
    return ch>='A' && ch<='Z' ? char(ch-'A'+'a') : ch;
}
constexpr bool Automatic(const char* selector) {
    if (!selector || !*selector) return true;
    constexpr char keyword[]="auto";
    for (std::size_t i=0;i<4;++i) {
        if (!selector[i] || LowerAscii(selector[i])!=keyword[i]) return false;
    }
    return selector[4]=='\0';
}
// EN: Manual selectors match case-insensitive substrings of Vulkan names.
// PT-BR: A seleção manual reconhece trechos do nome sem diferenciar caixa.
constexpr bool MatchesName(std::string_view name,const char* selector) {
    if (Automatic(selector)) return true;
    const std::string_view wanted(selector);
    if (wanted.size()>name.size()) return false;
    for (std::size_t start=0;start<=name.size()-wanted.size();++start) {
        bool equal=true;
        for (std::size_t i=0;i<wanted.size();++i) {
            if (LowerAscii(name[start+i])!=LowerAscii(wanted[i])) { equal=false; break; }
        }
        if (equal) return true;
    }
    return false;
}
} // namespace pgr::vulkan::device
