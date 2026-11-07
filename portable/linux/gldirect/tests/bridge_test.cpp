#include "../bridge.h"
#include <atomic>
#include <cassert>
#include <thread>

int main() {
    using namespace pgr::gldirect;
    Frame initial;
    assert(!Latest(initial));
    constexpr std::uint64_t n = 20000;
    std::atomic<bool> ready{false};
    std::thread producer([&] {
        for (std::uint64_t i=1; i<=n; ++i) Publish({i, 3*i, 2*i, 640, 480, 0xFF102030u, true});
        ready = true;
    });
    Frame f;
    while (!ready.load()) {
        if (Latest(f)) {
            assert(f.total_draws == f.number * 3);
            assert(f.total_clears == f.number * 2);
            assert(f.last_color == 0xFF102030u);
        }
    }
    producer.join();
    assert(Latest(f));
    assert(f.number == n);
    assert(f.total_draws == n*3);
}
