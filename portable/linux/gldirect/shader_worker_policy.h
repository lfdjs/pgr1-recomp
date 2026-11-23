#pragma once
#include <algorithm>
namespace pgr::gldirect {
// EN: Reserve CPU capacity for guest simulation and Vulkan presentation.
// PT-BR: Reserve nucleos para a simulacao e para a apresentacao Vulkan.
constexpr unsigned ShaderWorkerCount(unsigned hardware_threads,const char* override_value) {
    const unsigned fallback=std::clamp(hardware_threads/2u,1u,4u);
    if(!override_value || !*override_value)return fallback;
    unsigned parsed=0;
    for(const char* p=override_value;*p;++p) {
        if(*p<'0'||*p>'9')return fallback;
        parsed=parsed*10u+unsigned(*p-'0');
        if(parsed>8u)return fallback;
    }
    return parsed>=1u ? parsed : fallback;
}
} // namespace pgr::gldirect
