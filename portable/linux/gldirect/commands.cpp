#include "commands.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
namespace pgr::gldirect {
namespace { std::mutex mutex; std::condition_variable available; std::deque<Submission> queue; bool stopped=false; }
void Submit(Submission frame) {
    std::unique_lock lock(mutex);
    available.wait(lock, [] { return stopped || queue.size()<2; });
    if (!stopped) queue.push_back(std::move(frame));
}
bool Take(Submission& frame) {
    std::lock_guard lock(mutex);
    if (queue.empty()) return false;
    frame=std::move(queue.front()); queue.pop_front(); available.notify_one(); return true;
}
void Stop() { std::lock_guard lock(mutex); stopped=true; queue.clear(); available.notify_all(); }
}
