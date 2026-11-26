#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>
namespace pgr::vulkan::performance {
inline constexpr size_t kMaxPipelineCacheBytes=64u*1024u*1024u;
// EN: Vulkan header v1 is little-endian even on big-endian hosts.
// PT-BR: O cabeçalho Vulkan v1 usa little-endian em qualquer host.
inline bool ValidPipelineCache(const std::vector<unsigned char>& data,uint32_t vendor,uint32_t device,const uint8_t* uuid) {
    if(data.size()<32 || data.size()>kMaxPipelineCacheBytes)return false;
    auto word=[&](size_t i){return uint32_t(data[i])|uint32_t(data[i+1])<<8|uint32_t(data[i+2])<<16|uint32_t(data[i+3])<<24;};
    const auto header=word(0);
    return header>=32 && header<=data.size() && word(4)==1 &&
        word(8)==vendor && word(12)==device && std::equal(data.begin()+16,data.begin()+32,uuid);
}
inline std::vector<unsigned char> ReadPipelineCacheFile(const std::filesystem::path& path,uint32_t vendor,uint32_t device,const uint8_t* uuid) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream)return {};
    const auto end=stream.tellg();
    if(end<32 || end>std::streamoff(kMaxPipelineCacheBytes))return {};
    std::vector<unsigned char> bytes(static_cast<size_t>(end));
    stream.seekg(0);
    if(!stream.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))return {};
    if(!ValidPipelineCache(bytes,vendor,device,uuid))return {};
    return bytes;
}
}
