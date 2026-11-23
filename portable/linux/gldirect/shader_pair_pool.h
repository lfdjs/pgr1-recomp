#pragma once
#include <array>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace pgr::gldirect {
// EN: Shader jobs use immutable HLSL; only the work queue is shared.
// PT-BR: Jobs usam HLSL imutavel; somente a fila de trabalho e compartilhada.
class ShaderPairPool {
public:
    using Result=std::array<std::string,2>;
    using Task=std::packaged_task<Result()>;
    explicit ShaderPairPool(unsigned count) {
        if(count==0 || count>8)throw std::invalid_argument("shader worker count must be 1..8");
        threads_.reserve(count);
        try {
            for(unsigned i=0;i<count;++i)threads_.emplace_back([this]{Run();});
        } catch(...) { Stop(); throw; }
    }
    ~ShaderPairPool(){Stop();}
    ShaderPairPool(const ShaderPairPool&)=delete;
    ShaderPairPool& operator=(const ShaderPairPool&)=delete;
    unsigned ThreadCount()const{return unsigned(threads_.size());}
    std::shared_future<Result> Enqueue(Task task){
        auto future=task.get_future().share();
        {
            std::lock_guard lock(mutex_);
            if(stopping_)throw std::runtime_error("shader workers are stopping");
            queue_.push_back(std::move(task));
        }
        available_.notify_one();
        return future;
    }
private:
    void Run(){
        for(;;){
            Task task;
            {
                std::unique_lock lock(mutex_);
                available_.wait(lock,[this]{return stopping_ || !queue_.empty();});
                if(stopping_)return;
                task=std::move(queue_.front());queue_.pop_front();
            }
            task(); // EN/PT-BR: packaged_task propagates exceptions / propaga excecoes.
        }
    }
    void Stop(){
        {
            std::lock_guard lock(mutex_);
            stopping_=true;
            queue_.clear(); // EN/PT-BR: cancel pending jobs / cancela jobs pendentes.
        }
        available_.notify_all();
        for(auto& worker:threads_)if(worker.joinable())worker.join();
    }
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<Task> queue_;
    bool stopping_=false;
    std::vector<std::thread> threads_;
};
} // namespace pgr::gldirect
