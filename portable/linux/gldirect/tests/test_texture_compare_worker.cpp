#include "../texture_compare_worker.h"
#include <cassert>
#include <stdexcept>
#include <vector>
using pgr::gldirect::TextureCompareWorker;
int main(){
    // EN/PT-BR: Byte changes, exception safety and reuse / Mutações, exceções e reutilização.
    TextureCompareWorker worker;
    std::vector<unsigned char> a(512*1024,17),b=a;
    unsigned calls=0;
    auto work=[&]{++calls;};
    assert(worker.CompareWhile(a.data(),b.data(),a.size(),work));
    for(auto offset:{std::size_t(0),a.size()/2,a.size()-1}){
        b[offset]^=1;
        assert(!worker.CompareWhile(a.data(),b.data(),a.size(),work));
        b[offset]^=1;
    }
    assert(calls==4);
    bool threw=false;
    try{worker.CompareWhile(a.data(),b.data(),a.size(),[]{throw std::runtime_error("assembly");});}
    catch(const std::runtime_error&){threw=true;}
    assert(threw);
    for(int i=0;i<100;++i)assert(worker.CompareWhile(a.data(),b.data(),a.size(),work));
}
