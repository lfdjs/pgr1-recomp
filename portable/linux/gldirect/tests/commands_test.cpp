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
    Submission cancelled;
    cancelled.completion=std::make_shared<std::promise<void>>();
    auto completion=cancelled.completion->get_future();
    Submit(std::move(cancelled)); Submit({});
    std::atomic<bool> entered=false;
    std::thread blocked([&] { entered=true; Submit({}); });
    while(!entered) std::this_thread::yield();
    Stop(); blocked.join(); assert(!Take(f)); Submit({}); assert(!Take(f));
    assert(completion.wait_for(std::chrono::seconds(0))==std::future_status::ready);
    bool cancelled_promise=false;
    try { completion.get(); } catch(const std::future_error&) { cancelled_promise=true; }
    assert(cancelled_promise); // A closing renderer cannot strand guest fences.
}
