#include "../commands.h"
#include <cassert>
#include <thread>
#include <atomic>
using namespace pgr::gldirect;
int main() {
    Submission f; assert(!Take(f));
    std::thread producer([] {
        for(unsigned i=1;i<=1000;i++) {
            Submission s; s.frame=i; Command c; c.vertices.resize(1); c.vertices[0].position[0]=float(i);
            s.commands.push_back(std::move(c)); Submit(std::move(s));
        }
    });
    for(unsigned i=1;i<=1000;i++) {
        while(!Take(f)) std::this_thread::yield();
        assert(f.frame==i && f.commands[0].vertices[0].position[0]==float(i));
    }
    producer.join();
    Submit({}); Submit({});
    std::atomic<bool> entered=false;
    std::thread blocked([&] { entered=true; Submit({}); });
    while(!entered) std::this_thread::yield();
    Stop(); blocked.join(); assert(!Take(f)); Submit({}); assert(!Take(f));
}
