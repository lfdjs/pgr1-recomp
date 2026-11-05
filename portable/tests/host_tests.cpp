#include "pgr/portable/host.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

int main() {
    using namespace pgr::portable;
    auto host = CreateDesktopHost();
    assert(host);

    const Capabilities caps = host->GetCapabilities();
#if defined(_WIN32)
    assert(caps.platform == Platform::Windows);
#elif defined(__linux__)
    assert(caps.platform == Platform::Linux);
#endif
    assert(caps.pointer_bits == sizeof(void*) * 8);
    assert(caps.has_64bit_address_space == (sizeof(void*) >= 8));
    assert(caps.little_endian);

    const auto before = host->MonotonicNanoseconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto after = host->MonotonicNanoseconds();
    assert(after >= before);

    const auto data = host->UserDataDirectory("PGR1");
    assert(!data.empty());
    assert(data.filename() == std::filesystem::path("PGR1"));
    bool rejected = false;
    try { (void)host->UserDataDirectory("../unsafe"); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    std::cout << "platform=" << PlatformName(caps.platform)
              << " pointer_bits=" << caps.pointer_bits
              << " user_data=" << data.string() << '\n';
}
