#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>
#include <utility>

namespace pgr::preview {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// Decode only uncompressed true-color TGA (24/32 bit), the format emitted
// by sdk/harness/reference_renderer.cpp. No SDL dependency; easily unit-tested.
inline bool LoadTga(const std::filesystem::path& file, Image& image) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::uint8_t head[18] = {};
    if (!in.read(reinterpret_cast<char*>(head), sizeof head)) return false;
    if (head[1] != 0 || head[2] != 2 || (head[16] != 24 && head[16] != 32)) return false;
    const int w = head[12] | (head[13] << 8);
    const int h = head[14] | (head[15] << 8);
    if (w < 1 || h < 1 || w > 4096 || h > 4096) return false;
    const std::size_t bytes_per_pixel = head[16] / 8;
    const std::size_t count = std::size_t(w) * std::size_t(h);
    std::vector<std::uint8_t> raw(count * bytes_per_pixel);
    in.seekg(head[0], std::ios::cur); // optional image identifier
    if (!in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()))) return false;

    Image result;
    result.width = w;
    result.height = h;
    result.rgba.resize(count * 4);
    const bool top_origin = (head[17] & 0x20) != 0;
    const bool right_origin = (head[17] & 0x10) != 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int input_x = right_origin ? (w - 1 - x) : x;
            const int input_y = top_origin ? y : (h - 1 - y);
            const std::size_t src = (std::size_t(input_y) * w + input_x) * bytes_per_pixel;
            const std::size_t dst = (std::size_t(y) * w + x) * 4;
            result.rgba[dst + 0] = raw[src + 2];
            result.rgba[dst + 1] = raw[src + 1];
            result.rgba[dst + 2] = raw[src + 0];
            result.rgba[dst + 3] = bytes_per_pixel == 4 ? raw[src + 3] : 255;
        }
    }
    image = std::move(result);
    return true;
}

} // namespace pgr::preview
