#include "shader_compiler.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <regex>
#include <stdexcept>
#include <vector>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
extern char** environ;
#ifndef PGR1_GLSLANG_EXECUTABLE
#define PGR1_GLSLANG_EXECUTABLE "glslangValidator"
#endif
#ifndef PGR1_SPIRV_CROSS_EXECUTABLE
#define PGR1_SPIRV_CROSS_EXECUTABLE "spirv-cross"
#endif
namespace pgr::gldirect {
namespace {
std::string Read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void Run(std::vector<std::string> args, const std::filesystem::path& log) {
    std::vector<char*> argv;
    for(auto& a:args) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY|O_CREAT|O_APPEND, 0600);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    pid_t pid=0;
    int error=posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if(error) throw std::runtime_error(args[0]+": "+std::strerror(error));
    int status=0;
    while(waitpid(pid,&status,0)<0) { if(errno!=EINTR) throw std::runtime_error("waitpid shader compiler"); }
    if(!WIFEXITED(status) || WEXITSTATUS(status)!=0)
        throw std::runtime_error("Shader translation failed; inspect "+log.string());
}
}
std::string CompileHlsl(const std::string& source, bool vertex) {
    // Version salt covers lowering flags and interface naming, not guest addresses.
    std::string input="// pgr1-cross-v2-glsl400-precise\n"+source;
    uint64_t hash=1469598103934665603ull;
    for(unsigned char c:input) { hash^=c; hash*=1099511628211ull; }
    std::ostringstream name; name<<std::hex<<hash<<(vertex?".vert":".frag");
    const auto dir=std::filesystem::current_path()/"shadercache-gl-v2";
    std::filesystem::create_directories(dir);
    const auto base=dir/name.str();
    auto hlsl=base; hlsl+=".hlsl"; auto spv=base; spv+=".spv";
    auto glsl=base; glsl+=".glsl"; auto log=base; log+=".log";
    const int descriptor=open((base.string()+".lock").c_str(),O_CREAT|O_RDWR,0600);
    if(descriptor<0) throw std::runtime_error("Cannot lock shader cache");
    struct CacheLock { int descriptor; ~CacheLock() { close(descriptor); } } lock{descriptor};
    while(flock(descriptor,LOCK_EX)<0) if(errno!=EINTR) throw std::runtime_error("Cannot lock shader cache");
    if(Read(hlsl)==input) { auto cached=Read(glsl); if(!cached.empty()) return cached; }
    std::filesystem::remove(glsl);
    { std::ofstream out(hlsl); out<<input; if(!out) throw std::runtime_error("Cannot write "+hlsl.string()); }
    Run({PGR1_GLSLANG_EXECUTABLE,"-D","-V","-S",vertex?"vert":"frag","-e","main","--hlsl-iomap",
         "--auto-map-bindings","--auto-map-locations",hlsl.string(),"-o",spv.string()},log);
    // GLSL 3.30 lowering silently drops SPIR-V NoContraction decorations, even
    // with ARB_gpu_shader5 requested. The SDK marks position arithmetic precise
    // because multipass EQUAL depth needs the same rounded results. GLSL 4.00
    // preserves that precision contract on both native GL and Zink.
    std::vector<std::string> args={PGR1_SPIRV_CROSS_EXECUTABLE,spv.string(),"--version","400","--no-420pack-extension","--output",glsl.string()+".tmp"};
    if(vertex) args.push_back("--fixup-clipspace");
    Run(std::move(args),log);
    auto output=Read(glsl.string()+".tmp");
    // GLSL 4.0 links these varyings by name. HLSL's semantic locations are identical
    // but SPIRV-Cross gives entry outputs and inputs different names.
    output=std::regex_replace(output,std::regex("_entryPointOutput_"),"varying_");
    output=std::regex_replace(output,std::regex("\\bi_(d[01]|b[01]|tc[0-3]|fogamt|wlin|psize)\\b"),"varying_$1");
    if(output.empty()) throw std::runtime_error("Empty GLSL; inspect "+log.string());
    { std::ofstream out(glsl.string()+".ready"); out<<output; if(!out) throw std::runtime_error("Cannot cache GLSL"); }
    std::filesystem::rename(glsl.string()+".ready",glsl);
    return output;
}

std::string CompileHlslSpirv(const std::string& source, bool vertex) {
    const auto start=std::chrono::steady_clock::now();
    std::string mapped=source;
    // glslang's automatic input mapping compacts unused NV2A registers, while
    // the shared owned vertex array always has sixteen float4 attributes.
    for(unsigned reg=0;reg<16;reg++) {
        const auto n=std::to_string(reg);
        const std::regex attribute("(float4 v"+n+" : ATTR"+n+";)");
        mapped=std::regex_replace(mapped,attribute,"[[vk::location("+n+")]] $1");
    }
    // Pin the same inter-stage ABI even when one stage's optimizer removes
    // unused varyings. SV_Position remains a built-in, not a user location.
    const char* fields[]={"d0","d1","b0","b1","tc0","tc1","tc2","tc3","fogamt","wlin","psize"};
    for(unsigned loc=0;loc<std::size(fields);loc++) {
        const std::regex varying("((?:nointerpolation\\s+)?(?:float4|float) "+std::string(fields[loc])+" : (?:COLOR|TEXCOORD)[0-9]+;)");
        mapped=std::regex_replace(mapped,varying,"[[vk::location("+std::to_string(loc)+")]] $1");
    }
    // The register classes are separate in HLSL and share Vulkan's descriptor
    // binding namespace. Shifts below preserve register indices within a class.
    const std::string input="// pgr1-native-vk-v1-explicit-abi-invert-y-vulkan10\n"+mapped;
    uint64_t hash=1469598103934665603ull;
    for(unsigned char c:input) { hash^=c; hash*=1099511628211ull; }
    std::ostringstream name; name<<std::hex<<hash<<(vertex?".vert":".frag");
    const auto dir=std::filesystem::current_path()/"shadercache-vk-v1";
    std::filesystem::create_directories(dir);
    const auto base=dir/name.str();
    auto hlsl=base; hlsl+=".hlsl"; auto spv=base; spv+=".spv";
    auto log=base; log+=".log";
    const int descriptor=open((base.string()+".lock").c_str(),O_CREAT|O_RDWR,0600);
    if(descriptor<0) throw std::runtime_error("Cannot lock native shader cache");
    struct CacheLock { int descriptor; ~CacheLock() { close(descriptor); } } lock{descriptor};
    while(flock(descriptor,LOCK_EX)<0) if(errno!=EINTR) throw std::runtime_error("Cannot lock native shader cache");
    auto valid=[](const std::string& binary) {
        uint32_t magic=0;
        if(binary.size()>=20) std::memcpy(&magic,binary.data(),sizeof(magic));
        return binary.size()%4==0 && magic==0x07230203;
    };
    std::string binary;
    const bool hit=Read(hlsl)==input && valid(binary=Read(spv));
    if(!hit) {
        { std::ofstream out(hlsl); out<<input; if(!out) throw std::runtime_error("Cannot write "+hlsl.string()); }
        std::vector<std::string> args={PGR1_GLSLANG_EXECUTABLE,"-D","-V","--target-env","vulkan1.0",
            "-S",vertex?"vert":"frag","-e","main","--hlsl-iomap","--auto-map-locations",
            "--shift-texture-binding","2","--shift-sampler-binding","6",hlsl.string(),"-o",spv.string()+".tmp"};
        if(vertex) args.push_back("--invert-y");
        Run(std::move(args),log);
        binary=Read(spv.string()+".tmp");
        if(!valid(binary)) throw std::runtime_error("Invalid native SPIR-V; inspect "+log.string());
        std::filesystem::rename(spv.string()+".tmp",spv);
    }
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::fprintf(stderr,"vulkan shader: %s %s %.3f ms %zu bytes %s\n",vertex?"VS":"PS",hit?"cache":"compile",elapsed,binary.size(),spv.filename().c_str());
    return binary;
}

std::shared_future<std::array<std::string,2>> CompileHlslPairSpirvAsync(std::string vertex, std::string fragment) {
    // Generation stays on the SDK thread; only immutable HLSL crosses to this
    // worker. No process launch/compiler wait occurs on the guest draw path.
    struct Worker {
        using Task=std::packaged_task<std::array<std::string,2>()>;
        std::mutex mutex;
        std::condition_variable available;
        std::deque<Task> tasks;
        bool stopping=false;
        std::thread thread;
        Worker():thread([this] {
            for(;;) {
                Task task;
                {
                    std::unique_lock lock(mutex);
                    available.wait(lock,[this] { return stopping || !tasks.empty(); });
                    if(stopping) return;
                    task=std::move(tasks.front()); tasks.pop_front();
                }
                task(); // packaged_task preserves diagnostic exceptions for consumer.
            }
        }) {}
        ~Worker() {
            { std::lock_guard lock(mutex); stopping=true; tasks.clear(); }
            available.notify_all(); thread.join();
        }
    };
    static Worker worker;
    Worker::Task task([v=std::move(vertex),p=std::move(fragment)] {
        return std::array<std::string,2>{CompileHlslSpirv(v,true),CompileHlslSpirv(p,false)};
    });
    auto future=task.get_future().share();
    { std::lock_guard lock(worker.mutex); worker.tasks.push_back(std::move(task)); }
    worker.available.notify_one();
    return future;
}
}
