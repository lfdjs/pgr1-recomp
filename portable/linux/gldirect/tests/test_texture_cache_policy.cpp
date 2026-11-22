#include "texture_cache_policy.h"
#include <cassert>
#include <cstdio>
using pgr::gldirect::TextureCacheEntries;
static_assert(TextureCacheEntries(nullptr)==512);
static_assert(TextureCacheEntries("")==512);
static_assert(TextureCacheEntries("512")==512);
static_assert(TextureCacheEntries("1024")==1024);
static_assert(TextureCacheEntries("2048")==2048);
static_assert(TextureCacheEntries("64")==64);
static_assert(TextureCacheEntries("63")==512);
static_assert(TextureCacheEntries("2049")==512);
static_assert(TextureCacheEntries("99999999999999999999999999")==512);
static_assert(TextureCacheEntries("512oops")==512);
static_assert(TextureCacheEntries("-1")==512);
int main() { assert(TextureCacheEntries("768")==768);
    std::puts("Cache entry policy tests passed / Testes de capacidade aprovados"); }
