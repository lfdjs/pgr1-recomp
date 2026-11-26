#pragma once
#include <cstdint>
#include <vector>

enum class StoreKind { Byte, Word, Dword, Simd, SimdLow, SimdHigh, Mmx, Extended,
                       Float, Atomic, Bit, Movs, Stos, Stack, SimdLoad, Convert };
struct StoreCase { StoreKind kind; const char* name; std::vector<uint8_t> opcodes; uint32_t bytes; };
inline const std::vector<StoreCase> kStoreCases{
    {StoreKind::Byte,"mov byte",{0x88,0x07},1},
    {StoreKind::Word,"mov word",{0x66,0x89,0x07},2},
    {StoreKind::Dword,"mov dword",{0x89,0x07},4},
    {StoreKind::Simd,"movdqu",{0xF3,0x0F,0x7F,0x07},16},
    {StoreKind::SimdLow,"movlps",{0x0F,0x13,0x07},8},
    {StoreKind::SimdHigh,"movhps",{0x0F,0x17,0x07},8},
    {StoreKind::Mmx,"movq",{0x0F,0x7F,0x07},8},
    {StoreKind::Extended,"fstp 80",{0xDB,0x3F},10},
    {StoreKind::Float,"fstp 32",{0xD9,0x1F},4},
    {StoreKind::Atomic,"lock add",{0xF0,0x01,0x07},4},
    {StoreKind::Bit,"bts",{0x0F,0xAB,0x07},4},
    {StoreKind::Movs,"rep movsd",{0xF3,0xA5},28},
    {StoreKind::Stos,"rep stosd",{0xF3,0xAB},28},
    {StoreKind::Stack,"push eax",{0x50},4},
    {StoreKind::SimdLoad,"unaligned movups load/store",{0x0F,0x10,0x06,0x0F,0x11,0x07},16},
    {StoreKind::Convert,"cvtps2pi m64 boundary",{0x0F,0x2D,0x06,0x0F,0x7F,0x07},8},
};
