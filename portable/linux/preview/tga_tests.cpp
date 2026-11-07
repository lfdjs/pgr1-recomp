#include "tga.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const auto path = std::filesystem::temp_directory_path() / "pgr1_preview_tga_test.tga";
    // Two pixels: red then green, BGR, top left origin.
    const unsigned char header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 24, 0x20};
    const unsigned char pixels[6] = {0, 0, 255, 0, 255, 0};
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(header), sizeof(header));
        f.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    }
    pgr::preview::Image image;
    assert(pgr::preview::LoadTga(path, image));
    assert(image.width == 2 && image.height == 1);
    assert(image.rgba.size() == 8);
    assert(image.rgba[0] == 255 && image.rgba[1] == 0 && image.rgba[2] == 0);
    assert(image.rgba[4] == 0 && image.rgba[5] == 255 && image.rgba[6] == 0);
    std::filesystem::remove(path);
    assert(!pgr::preview::LoadTga(path, image));
    std::cout << "TGA decoder OK\n";
}
