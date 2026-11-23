#include "shader_worker_policy.h"
#include <cassert>
#include <cstdio>
using pgr::gldirect::ShaderWorkerCount;
static_assert(ShaderWorkerCount(0,nullptr)==1);
static_assert(ShaderWorkerCount(1,nullptr)==1);
static_assert(ShaderWorkerCount(2,nullptr)==1);
static_assert(ShaderWorkerCount(4,nullptr)==2);
static_assert(ShaderWorkerCount(8,nullptr)==4);
static_assert(ShaderWorkerCount(32,nullptr)==4);
static_assert(ShaderWorkerCount(8,"1")==1);
static_assert(ShaderWorkerCount(8,"8")==8);
static_assert(ShaderWorkerCount(8,"9")==4);
static_assert(ShaderWorkerCount(8,"0")==4);
static_assert(ShaderWorkerCount(8,"abc")==4);
static_assert(ShaderWorkerCount(8,"99999999")==4);
int main(){assert(ShaderWorkerCount(12,"3")==3);
    std::puts("Shader worker policy OK / Politica aprovada");}
