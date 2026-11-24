#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace pgr::gldirect {
// EN: Cache only after exact word equality; the object address is just a slot hint.
// PT-BR: Reutilize apenas com igualdade exata; o endereço do objeto só escolhe o slot.
class VertexProgramCache {
public:
    struct Result {
        std::uint64_t hash=0;
        std::size_t bad_instruction=0;
        unsigned bad_opcode=0;
        bool valid=false;
    };
    template<class Hash>
    Result Get(const void* hint,std::span<const std::uint32_t> words,Hash&& hash) {
        if(words.size()%4)return {};
        auto& e=entries_[(reinterpret_cast<std::uintptr_t>(hint)>>4)%entries_.size()];
        if(e.valid && e.words.size()==words.size() &&
           (words.empty() || std::memcmp(e.words.data(),words.data(),words.size_bytes())==0))
            return {e.hash,0,0,true};
        for(std::size_t i=0;i<words.size();i+=4) {
            const unsigned op=(words[i+1]>>21)&15;
            if(op>13)return {0,i/4,op,false};
        }
        const auto value=hash(words.data(),words.size_bytes());
        // EN: Bound retained memory; large programs still use the exact old path.
        // PT-BR: Limite a memória retida; programas grandes seguem o caminho exato.
        if(words.size()<=4096) {
            e.words.assign(words.begin(),words.end());
            e.hash=value;e.valid=true;
        }
        return {value,0,0,true};
    }
private:
    struct Entry { std::vector<std::uint32_t> words; std::uint64_t hash=0; bool valid=false; };
    std::array<Entry,64> entries_{};
};
} // namespace pgr::gldirect
