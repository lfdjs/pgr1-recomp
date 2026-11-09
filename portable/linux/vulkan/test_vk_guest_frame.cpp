#include "vk_guest_frame.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    using namespace pgr::vk;
    auto f=Fixture(0,640,480);
    assert(f.vertices.size()==20);
    assert(f.indices.size()==30);
    assert(f.batches.size()==5);
    assert(f.accepted_draws==5);
    for(const auto& batch:f.batches) {
        assert(batch.index_count==6);
        assert(std::size_t(batch.index_start)+batch.index_count<=f.indices.size());
    }
    for(auto index:f.indices) assert(index<f.vertices.size());
    for(const auto& vertex:f.vertices) assert(std::isfinite(vertex.x) && std::isfinite(vertex.y));
    try { (void)Fixture(0,0,480); assert(false); } catch(const std::invalid_argument&) {}
    std::cout<<"Vulkan guest geometry fixture OK\n";
}
