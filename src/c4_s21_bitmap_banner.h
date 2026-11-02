#pragma once
// C4-S21 dev-only banner: bitmap rectangles via the S17 render pass.
// No shader, texture, external font, additional hook, or per-frame allocation.
// ASCII uppercase text by design; the in-game message is PT-BR without accents.
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace embervale { namespace c4 { namespace devbanner21 {
struct Glyph { char ch; std::array<std::uint8_t,7> rows; };
inline const Glyph* glyph_for(char ch) noexcept {
    static constexpr Glyph font[] = {
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}},
    {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F}},
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}},
    {'J', {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
    {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
    {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    };
    for (const auto& glyph : font) if (glyph.ch==ch) return &glyph;
    return nullptr;
}
inline VkClearRect rect(std::int32_t x,std::int32_t y,std::uint32_t w,std::uint32_t h) noexcept {
    VkClearRect result{};
    result.rect.offset={x,y};result.rect.extent={w,h};
    result.baseArrayLayer=0;result.layerCount=1;
    return result;
}
inline void append_line(std::vector<VkClearRect>& out,std::string_view text,
                        std::int32_t x,std::int32_t y,std::uint32_t px) {
    for(char ch : text){
        const auto* glyph=glyph_for(ch);
        if(glyph){
            for(int row=0;row<7;++row){
                const auto mask=glyph->rows[static_cast<std::size_t>(row)];
                // Coalesce horizontal pixel runs to minimize clear rectangles.
                for(int col=0;col<5;){
                    if(!(mask&(1u<<(4-col)))){++col;continue;}
                    const auto start=col;
                    while(col<5 && (mask&(1u<<(4-col)))) ++col;
                    out.push_back(rect(x+static_cast<std::int32_t>(start*px),
                                       y+static_cast<std::int32_t>(row*px),
                                       static_cast<std::uint32_t>(col-start)*px,px));
                }
            }
        }
        x+=static_cast<std::int32_t>(6*px);
    }
}
inline const std::vector<VkClearRect>& letters() {
    static const std::vector<VkClearRect> shapes=[] {
        std::vector<VkClearRect> data;
        data.reserve(800);
        append_line(data,"EMBERVALE CORE CARREGADO",49,39,3);
        append_line(data,"API 1.1 - SHROUDTOPIA",50,76,2);
        return data;
    }();
    return shapes;
}
inline bool render(VkCommandBuffer cmd, PFN_vkCmdClearAttachments clear,
                   std::uint32_t width, std::uint32_t height) noexcept {
    if (!clear || width < 640 || height < 150) return false;
    VkClearAttachment bg{};bg.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    bg.colorAttachment=0;
    bg.clearValue.color.float32[0]=0.06f;
    bg.clearValue.color.float32[1]=0.085f;
    bg.clearValue.color.float32[2]=0.12f;
    bg.clearValue.color.float32[3]=1.0f;
    const auto panel=rect(24,24,580,98);
    clear(cmd,1,&bg,1,&panel);
    VkClearAttachment accent=bg;
    accent.clearValue.color.float32[0]=0.17f;
    accent.clearValue.color.float32[1]=0.72f;
    accent.clearValue.color.float32[2]=0.69f;
    const auto bar=rect(24,24,5,98);
    clear(cmd,1,&accent,1,&bar);
    VkClearAttachment fg=bg;
    fg.clearValue.color.float32[0]=0.94f;
    fg.clearValue.color.float32[1]=0.96f;
    fg.clearValue.color.float32[2]=0.98f;
    const auto& pixels=letters();
    if(pixels.empty()) return false;
    clear(cmd,1,&fg,static_cast<std::uint32_t>(pixels.size()),pixels.data());
    return true;
}
}}} // namespace embervale::c4::devbanner21