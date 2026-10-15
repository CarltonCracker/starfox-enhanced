#pragma once
#include "starfox/vr/source_models.hpp"
#include <algorithm>
#include <cstdint>
#include <span>

namespace starfox::modern {
inline bool source_models_supported(std::span<const vr::SourceComputeModel> models) noexcept {
    return std::none_of(models.begin(),models.end(),[](const auto& model) {return model.warp || model.axis;});
}
constexpr bool flat_frame_supported(bool setup,bool options,std::uint8_t stereo_output) noexcept {
    // The desktop MODERN presenter currently submits one eye only. Preserve
    // all SBS/over-under/interlaced/anaglyph/Leia modes through GPU FAST.
    return !setup && !options && stereo_output==0;
}
}
