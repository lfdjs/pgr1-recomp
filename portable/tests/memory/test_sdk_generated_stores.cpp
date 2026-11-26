#include "cpu/x86_ops.h"
#include "kernel/physical_write_audit.h"
#include "sdk_store_cases.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

[[noreturn]] void x86_trap(X86Context&, uint32_t, const char*) { std::abort(); }

int main() {
    constexpr size_t size = size_t(1)<<32, physical_bytes = 0x20000;
    auto* base = static_cast<uint8_t*>(mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));
    assert(base!=MAP_FAILED);
    const int fd = memfd_create("pgr1-generated-stores",0);
    assert(fd>=0 && ftruncate(fd,physical_bytes)==0);
    for(uint32_t view : {mem::kContiguousBase,mem::kWriteCombineBase})
        assert(mmap(base+view,physical_bytes,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_SHARED,fd,0)==base+view);
    mem::PhysicalWriteAudit::Enable(true);
    unsigned cases = 0;
    for(size_t i=0;i<kStoreCases.size();++i) for(uint32_t view : {mem::kContiguousBase,mem::kWriteCombineBase})
    for(bool reverse : {false,true}) {
        const auto& test = kStoreCases[i];
        std::vector<uint8_t> expected(physical_bytes);
        for(size_t p=0;p<physical_bytes;++p) expected[p]=uint8_t(p*13u+7u);
        std::memcpy(base+mem::kContiguousBase,expected.data(),physical_bytes);
        X86Context ctx{};
        const bool atomic = test.kind==StoreKind::Atomic || test.kind==StoreKind::Bit;
        uint32_t offset = atomic ? 8192-4 : 8192-1;
        ctx.edi = view + offset; ctx.esi = view+0x10000; ctx.esp = ctx.edi+4;
        ctx.eax = test.kind==StoreKind::Bit ? 35 : 0x12345678u;
        ctx.ecx=7; ctx.df=reverse; ctx.st[0]=12.5; ctx.fcw=0x37F;
        ctx.mm[0]=0xFEDCBA9876543210ull;
        ctx.xmm[0].q[0]=0x1020304050607080ull; ctx.xmm[0].q[1]=0x90A0B0C0D0E0F000ull;
        auto expected_copy = [&](const void* value,uint32_t bytes){std::memcpy(expected.data()+offset,value,bytes);};
        switch(test.kind) {
        case StoreKind::Byte: case StoreKind::Word: case StoreKind::Dword: case StoreKind::Stack:
            expected_copy(&ctx.eax,test.bytes); break;
        case StoreKind::Simd: case StoreKind::SimdLow: expected_copy(&ctx.xmm[0],test.bytes); break;
        case StoreKind::SimdHigh: expected_copy(&ctx.xmm[0].q[1],test.bytes); break;
        case StoreKind::Mmx: expected_copy(&ctx.mm[0],test.bytes); break;
        case StoreKind::Extended: {
            constexpr uint8_t value[10]={0,0,0,0,0,0,0,0xC8,0x02,0x40}; expected_copy(value,10); break;
        }
        case StoreKind::Float: { const float value=12.5f; expected_copy(&value,4); break; }
        case StoreKind::Atomic: {
            uint32_t value; std::memcpy(&value,expected.data()+offset,4); value+=ctx.eax; expected_copy(&value,4); break;
        }
        case StoreKind::Bit: {
            offset+=4; uint32_t value; std::memcpy(&value,expected.data()+offset,4); value|=8; expected_copy(&value,4); break;
        }
        case StoreKind::SimdLoad: expected_copy(expected.data()+0x10000,test.bytes); break;
        case StoreKind::Convert: {
            ctx.esi=view+physical_bytes-8;
            constexpr float input[2]={1.5f,-2.5f};
            std::memcpy(expected.data()+physical_bytes-8,input,8);
            std::memcpy(base+ctx.esi,input,8);
            constexpr int32_t result[2]={2,-2}; expected_copy(result,8); break;
        }
        case StoreKind::Movs: case StoreKind::Stos: {
            if(reverse) offset-=24;
            for(unsigned element=0;element<7;++element) {
                const auto to = uint32_t(ctx.edi-view) + (reverse ? 0u-element*4 : element*4);
                const auto from = uint32_t(ctx.esi-view) + (reverse ? 0u-element*4 : element*4);
                const void* value = test.kind==StoreKind::Movs ? static_cast<const void*>(expected.data()+from) : &ctx.eax;
                std::memcpy(expected.data()+to,value,4);
            }
            break;
        }
        }
        const uint32_t first=offset/mem::kPage,last=(offset+test.bytes-1)/mem::kPage;
        std::vector<uint64_t> prior;
        for(auto p=first;p<=last;++p) prior.push_back(mem::PhysicalWriteAudit::Read(view+p*mem::kPage).sequence);
        assert(X86FuncMappings[i].guest==0x10000+i*0x100);
        X86FuncMappings[i].host(ctx,base);
        assert(std::memcmp(expected.data(),base+view,physical_bytes)==0);
        for(auto p=first;p<=last;++p)
            assert(mem::PhysicalWriteAudit::Read(view+p*mem::kPage).sequence>prior[p-first]);
        ++cases;
    }
    close(fd); assert(munmap(base,size)==0);
    std::printf("Actual SDK generated stores: %u cases passed / stores gerados reais: %u casos passaram\n",cases,cases);
}
