#include "cpu/x86_ops.h"
#include <chrono>
#include <cstdio>
#include <vector>

[[gnu::noinline]] static void Execute(X86Context& ctx,uint8_t* base,bool copy) {
    if(copy) x86_rep_movs(ctx,base,4); else x86_rep_stos(ctx,base,4);
}
int main() {
    constexpr uint32_t bytes=1<<20, iterations=256;
    std::vector<uint8_t> memory(bytes*3,0x42);
    for(bool copy:{false,true}) for(bool reverse:{false,true}) {
        auto start=std::chrono::steady_clock::now();
        uint64_t checksum=0;
        for(unsigned i=0;i<iterations;++i) {
            X86Context ctx{};
            ctx.esi=reverse?bytes-4:0;
            ctx.edi=bytes+(reverse?bytes-4:0);
            ctx.ecx=bytes/4; ctx.df=reverse;
            // EN/PT-BR: Zero/FF fills are common; alternate to preserve work.
            //           Fills zero/FF são comuns; alterne para preservar o trabalho.
            ctx.eax=i&1?0xFFFFFFFFu:0u;
            Execute(ctx,memory.data(),copy);
            checksum+=memory[bytes+i]+ctx.edi;
        }
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::printf("%s,%s,%.3f,%.3f,%llu\n",copy?"movsd":"stosd",reverse?"backward":"forward",ms,
                    (double(bytes)*iterations/(1024.*1024.))/ms*1000.,static_cast<unsigned long long>(checksum));
    }
}
