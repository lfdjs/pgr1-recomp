#include "cpu/x86_ops.h"
#include "kernel/memory.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif

// EN: Run the SDK helpers themselves against an independent element interpreter.
// PT-BR: Execute os helpers reais contra um interpretador independente por elemento.
static void Reference(X86Context& ctx, uint8_t* base, uint32_t width, bool copy) {
    const int32_t step = ctx.df ? -int32_t(width) : int32_t(width);
    while (ctx.ecx) {
        std::array<uint8_t,4> value{};
        std::memcpy(value.data(), copy ? base + ctx.esi : reinterpret_cast<uint8_t*>(&ctx.eax), width);
        std::memcpy(base + ctx.edi, value.data(), width);
        if (copy) ctx.esi += step;
        ctx.edi += step;
        --ctx.ecx;
    }
}

static void Cases() {
    unsigned cases = 0;
    for (bool reverse : {false, true}) for (uint32_t width : {1u,2u,4u})
    for (uint32_t count : {0u,1u,2u,15u,64u,257u}) for (uint32_t source : {1024u,1025u})
    for (int displacement : {-513,-8,-4,-3,-1,0,1,3,4,8,513}) {
        std::vector<uint8_t> expected(4096), actual(4096);
        for (size_t i = 0; i < actual.size(); ++i) actual[i] = uint8_t(i * 37u + 11u);
        expected = actual;
        X86Context reference{}, ctx{};
        ctx.esi = source + (reverse && count ? (count - 1) * width : 0);
        ctx.edi = uint32_t(int(source) + displacement) + (reverse && count ? (count - 1) * width : 0);
        ctx.ecx = count; ctx.df = reverse; reference = ctx;
        Reference(reference, expected.data(), width, true);
        x86_rep_movs(ctx, actual.data(), width);
        assert(actual == expected && ctx.esi == reference.esi && ctx.edi == reference.edi && !ctx.ecx);
        ++cases;
    }
    for (bool reverse : {false,true}) for (uint32_t width : {1u,2u,4u})
    for (uint32_t count : {0u,1u,2u,64u,257u})
    for (uint32_t value : {0u,0xFFFFFFFFu,0xABABABABu,0x12345678u,0x0080FFFFu}) {
        std::vector<uint8_t> expected(4096,0xEE), actual = expected;
        X86Context reference{}, ctx{};
        ctx.edi = 1025 + (reverse && count ? (count - 1) * width : 0);
        ctx.ecx = count; ctx.df = reverse; ctx.eax = value; reference = ctx;
        Reference(reference, expected.data(), width, false);
        x86_rep_stos(ctx, actual.data(), width);
        assert(actual == expected && ctx.edi == reference.edi && !ctx.ecx);
        ++cases;
    }
    uint32_t begin = 0; uint64_t bytes = 0;
    assert(!x86_rep_span(0,2,4,true,begin,bytes));
    assert(!x86_rep_span(UINT32_MAX,2,4,false,begin,bytes));
    assert(!x86_rep_span(16,UINT32_MAX,4,false,begin,bytes));
    assert(x86_rep_span(UINT32_MAX-3,1,4,false,begin,bytes) && bytes == 4);
    assert(x86_rep_cross_alias_overlap(mem::kContiguousBase,mem::kWriteCombineBase+4,8));
    assert(!x86_rep_cross_alias_overlap(mem::kContiguousBase,mem::kWriteCombineBase+8,8));
    std::printf("SDK REP runtime: %u cases passed / casos passaram\n", cases);
}

#ifdef __linux__
static void PhysicalAliases() {
    constexpr size_t ram_bytes = 16384;
    const auto size = size_t(1) << 32;
    auto* base = static_cast<uint8_t*>(mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));
    assert(base != MAP_FAILED);
    const int fd = memfd_create("pgr1-rep-alias-test",0);
    assert(fd >= 0 && ftruncate(fd,ram_bytes) == 0);
    for (uint32_t view : {mem::kContiguousBase,mem::kWriteCombineBase})
        assert(mmap(base+view,ram_bytes,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_SHARED,fd,0) == base+view);
    std::vector<uint8_t> expected(ram_bytes);
    for (bool reverse : {false,true}) for (uint32_t width : {1u,2u,4u})
    for (int displacement : {-8,-3,-1,0,1,3,8}) for (bool swap : {false,true}) {
        for (size_t i=0;i<ram_bytes;++i) expected[i] = uint8_t(i*31u+7u);
        std::memcpy(base+mem::kContiguousBase,expected.data(),ram_bytes);
        X86Context ctx{}, reference{};
        reference.esi = 4096 + (reverse ? 63*width : 0);
        reference.edi = uint32_t(4096+displacement) + (reverse ? 63*width : 0);
        reference.ecx = 64; reference.df = reverse; ctx = reference;
        ctx.esi += swap ? mem::kWriteCombineBase : mem::kContiguousBase;
        ctx.edi += swap ? mem::kContiguousBase : mem::kWriteCombineBase;
        Reference(reference,expected.data(),width,true);
        x86_rep_movs(ctx,base,width);
        assert(std::memcmp(expected.data(),base+mem::kContiguousBase,ram_bytes)==0);
#ifdef XBE_PHYSICAL_WRITE_AUDIT
        assert(mem::PhysicalWriteAudit::Read(mem::kContiguousBase+4096).source==mem::PhysicalWriteSource::String);
#endif
    }
    close(fd); assert(munmap(base,size)==0);
    std::puts("SDK REP real physical aliases: 84 cases passed / aliases físicos reais: 84 casos passaram");
}
#endif

int main() {
#ifdef XBE_PHYSICAL_WRITE_AUDIT
    mem::PhysicalWriteAudit::Enable(true);
#endif
    Cases();
#ifdef __linux__
    PhysicalAliases();
#endif
}
