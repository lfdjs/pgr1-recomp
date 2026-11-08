#include "commands.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
#include <chrono>
namespace pgr::gldirect {
namespace { std::mutex mutex; std::condition_variable available; std::deque<Submission> queue; bool stopped=false; std::string failure; }
void Submit(Submission frame) {
    const auto start=std::chrono::steady_clock::now();
    std::unique_lock lock(mutex);
    available.wait(lock, [] { return stopped || queue.size()<2; });
    frame.queue_wait_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    if (!stopped) queue.push_back(std::move(frame));
}
bool Take(Submission& frame) {
    std::lock_guard lock(mutex);
    if (queue.empty()) return false;
    frame=std::move(queue.front()); queue.pop_front(); available.notify_one(); return true;
}
void Fail(std::string reason) { std::lock_guard lock(mutex); failure=std::move(reason); }
std::string Failure() { std::lock_guard lock(mutex); return failure; }
void Stop() { std::lock_guard lock(mutex); stopped=true; queue.clear(); available.notify_all(); }
}
