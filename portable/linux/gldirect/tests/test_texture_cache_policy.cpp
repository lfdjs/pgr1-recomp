#include "texture_cache_policy.h"
#include <cassert>
#include <cstdio>
using pgr::gldirect::TextureCacheEntries;
using pgr::gldirect::TextureCacheDefault;
// EN: Both backends preserve independent defaults and honor an explicit override.
// PT-BR: Ambos os backends preservam padrões independentes e aceitam override.
static_assert(TextureCacheDefault(true)==1024);
static_assert(TextureCacheDefault(false)==512);
static_assert(TextureCacheEntries(nullptr,TextureCacheDefault(true))==1024);
static_assert(TextureCacheEntries("",TextureCacheDefault(true))==1024);
static_assert(TextureCacheEntries("512",TextureCacheDefault(true))==512);
static_assert(TextureCacheEntries("1024",TextureCacheDefault(false))==1024);
static_assert(TextureCacheEntries("2048",TextureCacheDefault(true))==2048);
static_assert(TextureCacheEntries("64",TextureCacheDefault(true))==64);
static_assert(TextureCacheEntries("63",TextureCacheDefault(true))==1024);
static_assert(TextureCacheEntries("2049",TextureCacheDefault(true))==1024);
static_assert(TextureCacheEntries("99999999999999999999999999",TextureCacheDefault(true))==1024);
static_assert(TextureCacheEntries("512oops",TextureCacheDefault(true))==1024);
static_assert(TextureCacheEntries("-1",TextureCacheDefault(true))==1024);
static_assert(TextureCacheEntries(nullptr)==512);
static_assert(TextureCacheEntries("",TextureCacheDefault(false))==512);
int main() {
    assert(TextureCacheEntries("768",TextureCacheDefault(true))==768);
    std::puts("Cache entry policy tests passed / Testes de capacidade aprovados");
}
