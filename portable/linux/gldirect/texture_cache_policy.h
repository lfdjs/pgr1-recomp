#pragma once
namespace pgr::gldirect {
// EN: Native Vulkan needs a larger working set; keep legacy OpenGL unchanged.
// PT-BR: Vulkan nativo exige mais entradas; mantenha o padrão do OpenGL legado.
constexpr unsigned TextureCacheDefault(bool native_vulkan) {
    return native_vulkan ? 1024u : 512u;
}

// EN: Bound host-memory growth; allow a controlled capacity override.
// PT-BR: Limite o crescimento da memória; permita ajustar o cache manualmente.
constexpr unsigned TextureCacheEntries(const char* value, unsigned fallback=512) {
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
