#include "draw_bind_state.h"
#include <cassert>
#include <cstdio>
using pgr::vulkan::performance::BatchBindingCache;
int main() {
    BatchBindingCache<unsigned> cache;
    auto a=cache.Next(false,7u,0x11111111u);
    assert(a.pipeline && a.viewport && a.blend);
    auto b=cache.Next(true,7u,0x11111111u);
    assert(!b.pipeline && !b.viewport && !b.blend);
    auto color=cache.Next(true,7u,0x22222222u);
    assert(!color.pipeline && !color.viewport && color.blend);
    auto pipeline=cache.Next(true,8u,0x22222222u);
    assert(pipeline.pipeline && pipeline.viewport && pipeline.blend);
    auto restart=cache.Next(false,8u,0x22222222u);
    assert(restart.pipeline && restart.viewport && restart.blend);
    std::puts("Draw bind-state cache OK / Reutilizacao dos estados validada");
}
