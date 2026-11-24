#include "commands.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
#include <chrono>
namespace pgr::gldirect {
namespace { std::mutex mutex; std::condition_variable available, work_ready; std::deque<Submission> queue; bool stopped=false; std::string failure; }
void Submit(Submission frame) {
    const auto start=std::chrono::steady_clock::now();
    std::unique_lock lock(mutex);
    available.wait(lock, [] { return stopped || queue.size()<2; });
    frame.queue_wait_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    if (!stopped) {
        queue.push_back(std::move(frame));
        work_ready.notify_one();
    }
}
bool Take(Submission& frame) {
    std::lock_guard lock(mutex);
    if (queue.empty()) return false;
    frame=std::move(queue.front()); queue.pop_front(); available.notify_one(); return true;
}
// EN: Separate producer-space and consumer-work notifications. Wake as soon
// as commands arrive, but bound the wait so the SDL thread can poll GPU fences.
// PT-BR: Separe os avisos de espaço e trabalho. Acorde ao chegar comandos,
// limitando a espera para que a thread SDL possa consultar fences da GPU.
void WaitForWork(std::chrono::microseconds timeout) {
    std::unique_lock lock(mutex);
    work_ready.wait_for(lock,timeout,[]{return stopped || !queue.empty() || !failure.empty();});
}
void Fail(std::string reason) { std::lock_guard lock(mutex); failure=std::move(reason); work_ready.notify_all(); }
std::string Failure() { std::lock_guard lock(mutex); return failure; }
void Stop() { std::lock_guard lock(mutex); stopped=true; queue.clear(); available.notify_all(); work_ready.notify_all(); }
}
