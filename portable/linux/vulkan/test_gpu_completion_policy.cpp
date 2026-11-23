#include "gpu_completion_policy.h"
#include <cassert>
#include <cstdio>
using namespace pgr::vulkan::performance;
static_assert(AsynchronousGpuCompletion(nullptr));
static_assert(AsynchronousGpuCompletion(""));
static_assert(AsynchronousGpuCompletion("auto"));
static_assert(AsynchronousGpuCompletion("1"));
static_assert(AsynchronousGpuCompletion("TRUE"));
static_assert(!AsynchronousGpuCompletion("0"));
static_assert(!AsynchronousGpuCompletion("OFF"));
static_assert(!AsynchronousGpuCompletion("False"));
static_assert(!AsynchronousGpuCompletion("no"));
static_assert(AsynchronousGpuCompletion("oops"));
int main() {
    assert(!AsynchronousGpuCompletion("off"));
    std::puts("GPU completion policy OK / Politica de conclusao da GPU aprovada");
}
