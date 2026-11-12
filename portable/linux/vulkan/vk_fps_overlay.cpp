#include "vk_fps_overlay.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace pgr::vulkan {
namespace {
std::array<std::uint8_t,7> Glyph(char c) {
    switch(c) {
    case '0':return {14,17,19,21,25,17,14};
    case '1':return {4,12,4,4,4,4,14};
    case '2':return {14,17,1,2,4,8,31};
    case '3':return {30,1,1,14,1,1,30};
    case '4':return {2,6,10,18,31,2,2};
    case '5':return {31,16,16,30,1,1,30};
    case '6':return {14,16,16,30,17,17,14};
    case '7':return {31,1,2,4,8,8,8};
    case '8':return {14,17,17,14,17,17,14};
    case '9':return {14,17,17,15,1,1,14};
    case '.':return {0,0,0,0,0,12,12};
    case 'F':return {31,16,16,30,16,16,16};
    case 'P':return {30,17,17,30,16,16,16};
    case 'S':return {15,16,16,14,1,1,30};
    case 'M':return {17,27,21,21,17,17,17};
    default:return {};
    }
}
}
void RecordFpsOverlay(VkCommandBuffer command,VkExtent2D extent,const char* text) {
    if(!text || !*text || extent.width<32 || extent.height<24)return;
    const unsigned scale=extent.height>=480?2:1;
    constexpr unsigned margin=8,pad=4,max_chars=48;
    const unsigned length=std::min<unsigned>(std::strlen(text),max_chars);
    VkClearAttachment clear{};
    clear.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    clear.colorAttachment=0;
    clear.clearValue.color={{.015f,.02f,.025f,1.f}};
    VkClearRect background{{{int(margin),int(margin)},
        {std::min(extent.width-margin,length*6*scale+2*pad),7*scale+2*pad}},0,1};
    vkCmdClearAttachments(command,1,&clear,1,&background);
    std::array<VkClearRect,max_chars*35> rectangles{};
    unsigned count=0;
    for(unsigned i=0;i<length;i++) {
        const auto glyph=Glyph(text[i]);
        for(unsigned y=0;y<7;y++)for(unsigned x=0;x<5;x++)if(glyph[y]&(1u<<(4-x))) {
            const unsigned px=margin+pad+(i*6+x)*scale,py=margin+pad+y*scale;
            if(px+scale<=extent.width && py+scale<=extent.height)
                rectangles[count++]={{{int(px),int(py)},{scale,scale}},0,1};
        }
    }
    clear.clearValue.color={{.92f,.96f,1.f,1.f}};
    if(count)vkCmdClearAttachments(command,1,&clear,count,rectangles.data());
}
}
