#include "frame_ipc.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <vector>

#define REQUIRE(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

int main() {
    char pattern[] = "/tmp/pgr1-frames-XXXXXX";
    const int fd = mkstemp(pattern);
    REQUIRE(fd >= 0);
    close(fd);
    const std::filesystem::path path{pattern};
    pgr::stream::File publisher, viewer;
    REQUIRE(publisher.Open(path, true));
    REQUIRE(viewer.Open(path, false));
    pgr::stream::Frame initial;
    REQUIRE(!viewer.Receive(initial)); // no frame published yet
    constexpr std::uint32_t w = 8, h = 4;
    std::vector<std::uint8_t> data(w * h * 4);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = std::uint8_t(i % 251);
    REQUIRE(publisher.Publish(w, h, 42, data.data(), w * 4));
    pgr::stream::Frame image;
    REQUIRE(viewer.Receive(image));
    REQUIRE(image.index == 42 && image.width == w && image.height == h);
    REQUIRE(image.bgra == data);
    REQUIRE(!viewer.Receive(image));
    data[0] = 67;
    REQUIRE(publisher.Publish(w, h, 43, data.data(), w * 4));
    REQUIRE(viewer.Receive(image) && image.index == 43 && image.bgra[0] == 67);
    REQUIRE(!publisher.Publish(pgr::stream::kMaxWidth + 1, 1, 1, data.data(), 10000));
    viewer.Close(); publisher.Close();
    REQUIRE(std::filesystem::remove(path));
    std::cout << "Shared-frame protocol: PASS\n";
}
