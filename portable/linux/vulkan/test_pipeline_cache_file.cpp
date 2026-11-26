#include "pipeline_cache_file.h"
#include <cassert>
#include <array>
#include <cstdio>
int main() {
    using namespace pgr::vulkan::performance;
    std::array<uint8_t,16> uuid{};uuid[15]=17;
    std::vector<unsigned char> bytes(40);
    bytes[0]=32;bytes[4]=1;bytes[8]=2;bytes[12]=3;bytes[31]=17;
    assert(ValidPipelineCache(bytes,2,3,uuid.data()));
    assert(!ValidPipelineCache(bytes,2,4,uuid.data()));
    bytes[31]=18;assert(!ValidPipelineCache(bytes,2,3,uuid.data()));bytes[31]=17;
    bytes[0]=41;assert(!ValidPipelineCache(bytes,2,3,uuid.data()));bytes[0]=32;
    bytes.resize(31);assert(!ValidPipelineCache(bytes,2,3,uuid.data()));
    std::puts("pipeline cache header validation PASS");
}
