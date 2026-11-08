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
    std::string input="// pgr1-cross-v1\n"+source;
    uint64_t hash=1469598103934665603ull;
    for(unsigned char c:input) { hash^=c; hash*=1099511628211ull; }
    std::ostringstream name; name<<std::hex<<hash<<(vertex?".vert":".frag");
    const auto dir=std::filesystem::current_path()/"shadercache-gl-v1";
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
    std::vector<std::string> args={PGR1_SPIRV_CROSS_EXECUTABLE,spv.string(),"--version","330","--no-420pack-extension","--output",glsl.string()+".tmp"};
    if(vertex) args.push_back("--fixup-clipspace");
    Run(std::move(args),log);
    auto output=Read(glsl.string()+".tmp");
    // GLSL 3.3 links varyings by name. HLSL's semantic locations are identical
    // but SPIRV-Cross gives entry outputs and inputs different names.
    output=std::regex_replace(output,std::regex("_entryPointOutput_"),"varying_");
    output=std::regex_replace(output,std::regex("\\bi_(d[01]|b[01]|tc[0-3]|fogamt|wlin|psize)\\b"),"varying_$1");
    if(output.empty()) throw std::runtime_error("Empty GLSL; inspect "+log.string());
    { std::ofstream out(glsl.string()+".ready"); out<<output; if(!out) throw std::runtime_error("Cannot cache GLSL"); }
    std::filesystem::rename(glsl.string()+".ready",glsl);
    return output;
}
}
