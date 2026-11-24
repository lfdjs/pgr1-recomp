#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <thread>
namespace pgr::gldirect {
// EN: One persistent worker, one borrowed comparison at a time; never outlive Draw.
// PT-BR: Um worker persistente, uma comparação emprestada por vez; não ultrapasse Draw.
class TextureCompareWorker {
public:
    TextureCompareWorker():thread_([this]{Run();}){}
    ~TextureCompareWorker(){
        {std::lock_guard lock(mutex_);stop_=true;}
        ready_.notify_one();thread_.join();
    }
    TextureCompareWorker(const TextureCompareWorker&)=delete;
    TextureCompareWorker& operator=(const TextureCompareWorker&)=delete;
    template<class Work>
    bool CompareWhile(const void* guest,const void* snapshot,std::size_t bytes,Work&& work){
        {std::lock_guard lock(mutex_);guest_=guest;snapshot_=snapshot;bytes_=bytes;pending_=true;done_=false;}
        ready_.notify_one();
        try { work(); } catch(...) { Finish();throw; }
        return Finish();
    }
private:
    bool Finish(){
        std::unique_lock lock(mutex_);finished_.wait(lock,[this]{return done_;});return equal_;
    }
    void Run(){
        std::unique_lock lock(mutex_);
        for(;;){
            ready_.wait(lock,[this]{return pending_||stop_;});
            if(stop_)return;
            const auto* a=guest_;const auto* b=snapshot_;const auto n=bytes_;
            pending_=false;lock.unlock();
            const bool equal=std::memcmp(a,b,n)==0;
            lock.lock();equal_=equal;done_=true;finished_.notify_one();
        }
    }
    std::mutex mutex_;
    std::condition_variable ready_,finished_;
    const void* guest_=nullptr;const void* snapshot_=nullptr;
    std::size_t bytes_=0;
    bool pending_=false,done_=false,equal_=false,stop_=false;
    std::thread thread_;
};
} // namespace pgr::gldirect
