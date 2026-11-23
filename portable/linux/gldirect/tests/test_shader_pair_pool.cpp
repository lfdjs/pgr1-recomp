#include "shader_pair_pool.h"
#include "shader_worker_policy.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
using namespace pgr::gldirect;
int main(){
    ShaderPairPool pool(2);
    assert(pool.ThreadCount()==2);
    std::atomic<unsigned> started{0};
    std::promise<void> open;
    auto gate=open.get_future().share();
    auto task=[&](const char* name){return ShaderPairPool::Task([&,name]{
        ++started;gate.wait();return ShaderPairPool::Result{name,"fragment"};
    });};
    auto first=pool.Enqueue(task("vs1"));
    auto second=pool.Enqueue(task("vs2"));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(started.load()<2 && std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    const bool concurrent=started.load()==2;
    open.set_value();
    assert(concurrent);
    assert(first.get()[0]=="vs1" && second.get()[0]=="vs2");
    auto broken=pool.Enqueue(ShaderPairPool::Task([]()->ShaderPairPool::Result{
        throw std::runtime_error("compile failed");
    }));
    bool propagated=false;
    try{broken.get();}catch(const std::runtime_error&){propagated=true;}
    assert(propagated);
    std::puts("Shader pool OK / Execucao paralela e propagacao aprovadas");
}
