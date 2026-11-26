#pragma once
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

namespace pgr::gldirect {
// EN: Exact process-local stage identity avoids repeating ABI regex lowering,
// disk locks and reads across shader pairs. Owners compile outside the mutex;
// waiters share the actual result/exception, never a queued dependent worker.
// PT-BR: Identidade exata evita repetir regex da ABI, locks e leituras entre
// pares. O dono compila fora do mutex; aguardantes compartilham resultado/erro
// real, sem enfileirar uma tarefa dependente de outro worker.
class ShaderStageCache {
    using Key=std::tuple<std::string,std::string,bool>;
    struct Entry {
        std::promise<std::string> promise;
        std::shared_future<std::string> future=promise.get_future().share();
        std::size_t bytes=0;
    };
    std::mutex mutex_;
    std::map<Key,std::shared_ptr<Entry>> entries_;
    const std::size_t max_entries_,max_source_bytes_;
    std::size_t source_bytes_=0;
public:
    explicit ShaderStageCache(std::size_t max_entries=1024,std::size_t max_source_bytes=32*1024*1024)
        :max_entries_(max_entries),max_source_bytes_(max_source_bytes){}

    template<class Compile>
    std::string Get(const std::string& source,bool vertex,const std::string& directory,Compile&& compile) {
        const Key key{directory,source,vertex};
        std::shared_ptr<Entry> entry;
        bool owner=false;
        {
            std::lock_guard lock(mutex_);
            const auto found=entries_.find(key);
            if(found!=entries_.end())entry=found->second;
            else if(entries_.size()<max_entries_ && directory.size()<=max_source_bytes_ &&
                    source.size()<=max_source_bytes_-directory.size() &&
                    source.size()+directory.size()<=max_source_bytes_-source_bytes_) {
                entry=std::make_shared<Entry>();entry->bytes=source.size()+directory.size();
                entries_.emplace(key,entry);source_bytes_+=entry->bytes;owner=true;
            }
        }
        // EN/PT-BR: Bounded overflow uses the original compiler / Excesso usa o compilador original.
        if(!entry)return compile();
        if(!owner)return entry->future.get();
        try {
            auto result=compile();entry->promise.set_value(result);return result;
        } catch(...) {
            const auto error=std::current_exception();
            {
                std::lock_guard lock(mutex_);
                const auto found=entries_.find(key);
                if(found!=entries_.end() && found->second==entry) {
                    source_bytes_-=entry->bytes;entries_.erase(found);
                }
            }
            // EN/PT-BR: Failed stages can be retried / Stages com falha permitem nova tentativa.
            entry->promise.set_exception(error);std::rethrow_exception(error);
        }
    }
};
}
