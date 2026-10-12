#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <algorithm>
#include <cmath>
#include <vector>
#include "starfox/render/framebuffer.hpp"
#include "starfox/render/global_enhancement_options.hpp"

namespace starfox::render {
// Shared scalar kernel: GPU and software use identical bounded sampling.
template<class Sampler> inline float global_channel(Sampler sample,float x,float y,
    float w,float h,float step,float seconds,std::uint32_t packed,unsigned channel) {
    using uint=std::uint32_t;
    using std::sin; using std::floor; using std::sqrt; using std::abs;
#define G_MIN std::min
#define G_MAX std::max
#define G_SAMPLE(a,b) sample(a,b)
#include "global_enhancements.inc"
#undef G_SAMPLE
#undef G_MIN
#undef G_MAX
}

inline void apply_global_enhancements(std::uint32_t packed,const Framebuffer& frame,
    std::vector<std::uint8_t>& rgba,std::vector<std::uint8_t>& scratch,float seconds) {
    packed &= global_enhancement_mask;
    if(!packed || !frame.layer_tags_enabled() || rgba.size()!=frame.pixels().size()*4) return;
    scratch=rgba;
    const auto w=frame.stored_width(),h=frame.stored_height();
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
        const auto i=std::size_t(y)*w+x;
        if(frame.layer_tags()[i]==1) continue; // HUD, menu and portraits.
        for(unsigned channel=0;channel<3;++channel) {
            const auto sample=[&](float sx,float sy) {
                sx=std::clamp(sx,0.f,float(w-1));sy=std::clamp(sy,0.f,float(h-1));
                const unsigned ax=unsigned(sx),ay=unsigned(sy);
                const float tx=sx-ax,ty=sy-ay;float value=0;
                for(unsigned dy=0;dy<2;++dy) for(unsigned dx=0;dx<2;++dx) {
                    auto n=std::size_t(std::min(ay+dy,h-1))*w+std::min(ax+dx,w-1);
                    if(frame.layer_tags()[n]==1) n=i;
                    value+=scratch[n*4+channel]*(dx?tx:1-tx)*(dy?ty:1-ty);
                }
                return value/255.f;
            };
            rgba[i*4+channel]=std::uint8_t(std::clamp(global_channel(sample,float(x),float(y),
                float(w),float(h),float(frame.draw_scale()),seconds,packed,channel)*255.f+.5f,0.f,255.f));
        }
    }
}
}
