#pragma once
#include <string_view>

namespace pgr::vulkan::performance {
// EN: Allow CPU capture to overlap a submitted GPU frame by default.
// PT-BR: Permita sobrepor captura na CPU e execucao do frame na GPU por padrao.
constexpr bool EqualsAsciiNoCase(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i) {
        const auto lower=[](char c) { return c>='A'&&c<='Z'?char(c-'A'+'a'):c; };
        if(lower(a[i])!=lower(b[i]))return false;
    }
    return true;
}
constexpr bool AsynchronousGpuCompletion(const char* env) {
    if(!env || !*env)return true;
    const std::string_view value(env);
    return !(value=="0" || EqualsAsciiNoCase(value,"false") ||
             EqualsAsciiNoCase(value,"off") || EqualsAsciiNoCase(value,"no"));
}
} // namespace pgr::vulkan::performance
