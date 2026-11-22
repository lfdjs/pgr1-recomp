#pragma once
namespace pgr::gldirect {
// EN: Bound memory growth; allow a controlled cache-capacity experiment.
// PT-BR: Limite o crescimento da memória; permita testar a capacidade do cache.
constexpr unsigned TextureCacheEntries(const char* value) {
    constexpr unsigned fallback=512;
    if(!value || !*value)return fallback;
    unsigned parsed=0;
    for(const char* at=value;*at;++at) {
        if(*at<'0' || *at>'9')return fallback;
        parsed=parsed*10+unsigned(*at-'0');
        if(parsed>2048)return fallback;
    }
    return parsed>=64?parsed:fallback;
}
} // namespace pgr::gldirect
