#pragma once
#include <cstdint>

namespace pgr::vulkan::performance {
// EN: Reuse pipeline/viewport/blend only inside one uninterrupted static batch.
// PT-BR: Reaproveite pipeline/viewport/blend apenas no mesmo grupo de draws.
template<class PipelineHandle>
class BatchBindingCache {
public:
    struct Changes { bool pipeline=false,viewport=false,blend=false; };
    Changes Next(bool continuation,PipelineHandle pipeline,std::uint32_t blend_color) {
        // EN: Pipeline changes conservatively rebind all dynamic state.
        // PT-BR: Ao trocar o pipeline, reaplique todos os estados dinamicos.
        const bool new_pipeline=!continuation || !valid_ || pipeline!=previous_pipeline_;
        const bool new_blend=new_pipeline || blend_color!=previous_blend_;
        previous_pipeline_=pipeline;
        previous_blend_=blend_color;
        valid_=true;
        return {new_pipeline,new_pipeline,new_blend};
    }
private:
    PipelineHandle previous_pipeline_{};
    std::uint32_t previous_blend_=0;
    bool valid_=false;
};
} // namespace pgr::vulkan::performance
