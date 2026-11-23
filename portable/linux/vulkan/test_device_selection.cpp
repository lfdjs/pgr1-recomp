#include "device_selection.h"
#include <cassert>
#include <cstdio>
using namespace pgr::vulkan::device;
static_assert(Automatic(nullptr));
static_assert(Automatic(""));
static_assert(Automatic("auto"));
static_assert(Automatic("AUTO"));
static_assert(Automatic("AuTo"));
static_assert(!Automatic("rx 580"));
static_assert(!Automatic("automatic"));
static_assert(!Automatic("auto "));
static_assert(MatchesName("NVIDIA GeForce RTX 3060 Ti",nullptr));
static_assert(MatchesName("NVIDIA GeForce RTX 3060 Ti","auto"));
static_assert(MatchesName("NVIDIA GeForce RTX 3060 Ti","rTx 3060 TI"));
static_assert(MatchesName("AMD Radeon RX 580 2048SP","RX 580"));
static_assert(!MatchesName("AMD Radeon RX 580 2048SP","RTX 3060 Ti"));
static_assert(!MatchesName("NVIDIA GeForce RTX 3060 Ti","RX 580"));
static_assert(!MatchesName("NVIDIA GeForce RTX 3060 Ti","nvidia geforce rtx 3080"));
int main() {
    assert(MatchesName("Radeon RX 580 2048SP","rX 580"));
    std::puts("GPU selection tests passed / Testes de selecao de GPU aprovados");
}
