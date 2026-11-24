#include "../commands.h"
#include <cassert>
#include <chrono>
#include <future>
using namespace pgr::gldirect;
using namespace std::chrono_literals;
int main(){
    // EN: A queued submission wakes the consumer and preserves FIFO order.
    // PT-BR: Uma submissão acorda o consumidor e mantém a ordem FIFO.
    auto waiter=std::async(std::launch::async,[]{WaitForWork(2s);});
    Submission a;a.frame=1;Submit(std::move(a));
    assert(waiter.wait_for(1s)==std::future_status::ready);waiter.get();
    Submission b;b.frame=2;Submit(std::move(b));
    Submission out;
    assert(Take(out)&&out.frame==1);assert(Take(out)&&out.frame==2);
    assert(!Take(out));
    WaitForWork(1ms);assert(!Take(out));
    auto stopped=std::async(std::launch::async,[]{WaitForWork(2s);});
    Stop();assert(stopped.wait_for(1s)==std::future_status::ready);stopped.get();
}
