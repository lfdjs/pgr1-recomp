#include "shader_stage_cache.h"
#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef PGR1_TEST_REAL_SHADER_COMPILER
#include "shader_compiler.h"
#include <filesystem>
#endif

int main() {
    using pgr::gldirect::ShaderStageCache;
    ShaderStageCache cache;
    std::atomic<unsigned> calls=0;
    std::barrier start(12);
    std::vector<std::thread> threads;
    for(unsigned i=0;i<12;i++)threads.emplace_back([&] {
        start.arrive_and_wait();
        assert(cache.Get("exact source",true,"directory",[&] {
            ++calls;std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return std::string("owned binary");
        })=="owned binary");
    });
    for(auto& thread:threads)thread.join();
    assert(calls==1);
    auto compile=[&] {++calls;return std::string("different stage");};
    assert(cache.Get("exact source",false,"directory",compile)=="different stage");
    assert(cache.Get("exact source",true,"other directory",compile)=="different stage");
    assert(cache.Get("exact source ",true,"directory",compile)=="different stage");
    assert(calls==4);

    // EN/PT-BR: Independent owners must run concurrently / Donos independentes executam em paralelo.
    std::barrier compilers(2);
    auto parallel=[&] {compilers.arrive_and_wait();return std::string("parallel");};
    std::thread a([&] {assert(cache.Get("a",true,"dir",parallel)=="parallel");});
    std::thread b([&] {assert(cache.Get("b",true,"dir",parallel)=="parallel");});
    a.join();b.join();

    unsigned attempts=0;
    bool failed=false;
    try {cache.Get("failure",true,"dir",[&]() -> std::string {++attempts;throw std::runtime_error("expected");});}
    catch(const std::runtime_error&) {failed=true;}
    assert(failed && attempts==1);
    assert(cache.Get("failure",true,"dir",[&] {++attempts;return std::string("retried");})=="retried");
    assert(attempts==2);

    ShaderStageCache bounded(1,16);
    unsigned bounded_calls=0;
    auto count=[&] {++bounded_calls;return std::string("bounded");};
    assert(bounded.Get("one",true,"dir",count)=="bounded");
    assert(bounded.Get("one",true,"dir",count)=="bounded");
    assert(bounded_calls==1);
    bounded.Get("two",true,"dir",count);bounded.Get("two",true,"dir",count);
    assert(bounded_calls==3);
    ShaderStageCache bytes(5,4);
    bytes.Get("oversized",true,"dir",count);bytes.Get("oversized",true,"dir",count);
    assert(bounded_calls==5);
#ifdef PGR1_TEST_REAL_SHADER_COMPILER
    // EN: Exercise the actual wrapper/glslang pipeline. Removing the disk
    // cache proves an exact process hit avoids remapping and file access;
    // changing cwd must build the independent cache namespace again.
    // PT-BR: Exercite wrapper/glslang real. Remover o cache em disco comprova
    // o hit em memoria sem remapeamento/arquivos; outro cwd cria novo namespace.
    namespace fs=std::filesystem;
    const auto original=fs::current_path();
    const auto temp=fs::temp_directory_path()/(
        "pgr1-stage-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temp/"a");fs::create_directories(temp/"b");
    struct Cleanup {
        fs::path original,temp;
        ~Cleanup() {fs::current_path(original);fs::remove_all(temp);}
    } cleanup{original,temp};
    const std::string source="float4 main(float4 position : POSITION) : SV_Position { return position; }";
    fs::current_path(temp/"a");
    const auto binary=pgr::gldirect::CompileHlslSpirv(source,true);
    assert(binary.size()>=20);
    fs::remove_all(temp/"a"/"shadercache-vk-v1");
    assert(pgr::gldirect::CompileHlslSpirv(source,true)==binary);
    assert(!fs::exists(temp/"a"/"shadercache-vk-v1"));
    fs::current_path(temp/"b");
    assert(pgr::gldirect::CompileHlslSpirv(source,true)==binary);
    assert(fs::exists(temp/"b"/"shadercache-vk-v1"));
#endif
}
