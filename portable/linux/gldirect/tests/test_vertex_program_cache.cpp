#include "../vertex_program_cache.h"
#include <cassert>
#include <array>
#include <cstdio>
using pgr::gldirect::VertexProgramCache;
int main(){
    VertexProgramCache cache;
    std::array<std::uint32_t,8> words{1,0,3,4,5,0,7,8};
    unsigned hashes=0;
    auto hash=[&](const void* data,std::size_t bytes){
        ++hashes;const auto* p=static_cast<const unsigned char*>(data);
        std::uint64_t h=0;for(std::size_t i=0;i<bytes;++i)h=h*131+p[i];return h;
    };
    auto first=cache.Get(&words,words,hash);
    assert(first.valid && hashes==1);
    assert(cache.Get(&words,words,hash).hash==first.hash && hashes==1);
    words[7]^=1;
    auto changed=cache.Get(&words,words,hash);
    assert(changed.valid && changed.hash!=first.hash && hashes==2);
    words[5]=14u<<21;
    auto invalid=cache.Get(&words,words,hash);
    assert(!invalid.valid && invalid.bad_instruction==1 && invalid.bad_opcode==14 && hashes==2);
    words[5]=0;
    assert(cache.Get(&words,words,hash).hash==changed.hash && hashes==2);
    assert(!cache.Get(&words,std::span<const std::uint32_t>(words).first(7),hash).valid);
    // EN/PT-BR: Address reuse cannot validate different bytes / Reuso de endereço não valida bytes diferentes.
    words[0]=99;
    assert(cache.Get(&words,words,hash).hash!=changed.hash && hashes==3);
    std::puts("Vertex program cache: OK / Cache de programas: OK");
}
