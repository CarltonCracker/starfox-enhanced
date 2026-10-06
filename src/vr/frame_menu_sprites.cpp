#include "starfox/vr/source_sprites.hpp"
#include "starfox/vr/frame_menu.hpp"
#include "starfox/vr/presentation.hpp"
#include <algorithm>
namespace starfox::vr {
std::vector<DrawPacket> layout_a_menu_packets(const assets::RomImage& rom,const assets::SymbolMap& symbols,
    const FrameMenu& menu,bool srgb) {
    std::array<uint16_t,256> palette{};palette[1]=0x7fff;palette[2]=0x03ff;
    std::vector<DrawPacket> rows;
    const unsigned visible_rows=std::min(6U,menu.row_count());
    const int footer=67+18*int(visible_rows);
    rows.push_back(layout_a_surface(true,menu.selection-menu.first_visible_row(),srgb,visible_rows));
    const auto add=[&](std::string_view text,int y,uint8_t ink) {
        auto row=source_ui_text_packet(rom,symbols,text,16,y,240,ink,palette,15,srgb,true);
        row.model=identity_matrix;rows.push_back(std::move(row));
    };
    const auto first=menu.first_visible_row();
    if(menu.language>=1 && menu.language<=4) {
        const auto add_unicode=[&](std::u32string_view text,int y,uint8_t ink) {
            auto row=source_unicode_ui_text_packet(rom,symbols,text,16,y,ink,palette,15,srgb);
            row.model=identity_matrix;rows.push_back(std::move(row));
        };
        const auto labels=menu.localized_labels();add_unicode(menu.translate(menu.title()),35,1);
        for(unsigned i=first;i<labels.size() && i<first+6;++i)
            add_unicode((i==menu.selection?U"> ":U"  ")+labels[i],67+int(i-first)*18,i==menu.selection?2:1);
        const auto help=menu.localized_help();add_unicode(help[0],footer+8,1);add_unicode(help[1],footer+26,1);
    } else {
        add(menu.title(),35,1);const auto labels=menu.labels();
        for(unsigned i=first;i<labels.size() && i<first+6;++i)
            add((i==menu.selection?"> ":"  ")+labels[i],67+int(i-first)*18,i==menu.selection?2:1);
        add("STICK: MOVE",footer+8,1);add("MENU: SELECT",footer+26,1);
    }
    return rows;
}
}
