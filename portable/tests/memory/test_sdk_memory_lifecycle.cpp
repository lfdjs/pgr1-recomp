#include "kernel/kernel.h"
#include "kernel/physical_write_audit.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace kernel { uint8_t* g_base=nullptr; }

int main() {
    assert(mem::Init()); kernel::g_base=mem::Base();
    mem::PhysicalWriteAudit::Enable(true);
    for(bool host:{false,true}) {
        auto allocate=[&]{return host ? mem::AllocHostContiguous(8192,4096)
            : mem::AllocContiguous(8192,0,UINT32_MAX,4096);};
        const auto address=allocate(); assert(address);
        const auto alias=mem::kWriteCombineBase+(address-mem::kContiguousBase);
        auto event=mem::PhysicalWriteAudit::Read(alias);
        assert(event.sequence && event.source==mem::PhysicalWriteSource::MemoryLifecycle);
        assert(mem::PhysicalWriteAudit::Read(alias+4096).source==mem::PhysicalWriteSource::MemoryLifecycle);
        kernel::W16(address+4095,0xBEEF);
        assert(kernel::R16(alias+4095)==0xBEEF);
        assert(mem::PhysicalWriteAudit::Read(alias).source==mem::PhysicalWriteSource::KernelHelper);
        kernel::W32(alias+4094,0x12345678);
        assert(kernel::R32(address+4094)==0x12345678);
        kernel::W64(address+4093,0xFEDCBA9876543210ull);
        assert(kernel::R64(alias+4093)==0xFEDCBA9876543210ull);
        const auto before=mem::PhysicalWriteAudit::Read(alias).sequence;
        mem::FreeContiguous(address);
        event=mem::PhysicalWriteAudit::Read(alias);
        assert(event.sequence>before && event.source==mem::PhysicalWriteSource::MemoryLifecycle);
        const auto reused=allocate(); assert(reused==address);
        assert(mem::PhysicalWriteAudit::Read(alias).sequence>event.sequence);
        for(unsigned i=0;i<8192;++i) assert(kernel::g_base[alias+i]==0);
        mem::FreeContiguous(reused);
    }
    std::puts("Actual SDK alloc/free/reuse and W16/W32/W64 dual-alias writes: PASS / alocação/liberação/reuso e stores reais: OK");
}
