#include "codegen.h"
#include "sdk_store_cases.h"
#include <algorithm>
#include <cassert>
#include <filesystem>

// EN: Decode synthetic public instruction bytes with the real SDK analysis and
//     emitter; no copyrighted image or hand-written replacement emitter is used.
// PT-BR: Decodifique bytes sintéticos com análise e gerador reais do SDK;
//        sem imagem proprietária ou gerador substituto escrito para o teste.
int main(int argc, char** argv) {
    assert(argc == 2);
    xbe::Image image;
    image.base = image.entry = 0x10000;
    image.file.resize(kStoreCases.size()*0x100,0xCC);
    image.image_size = uint32_t(image.file.size());
    image.sections.push_back({".text",xbe::kExecutable,image.base,image.image_size,0,image.image_size});
    Config config;
    config.code_sections = {".text"}; config.gap_fill = config.pointer_seeds = false;
    config.partitions = 1;
    for(size_t i=0;i<kStoreCases.size();++i) {
        const auto& bytes = kStoreCases[i].opcodes;
        std::copy(bytes.begin(),bytes.end(),image.file.begin()+i*0x100);
        image.file[i*0x100+bytes.size()] = 0xC3;
        config.functions[image.base+uint32_t(i)*0x100] = kStoreCases[i].name;
    }
    Analysis analysis(image,config); analysis.Run();
    assert(analysis.functions.size()==kStoreCases.size());
    std::filesystem::create_directories(argv[1]);
    assert(CodeGen(analysis,argv[1]).Run());
}
