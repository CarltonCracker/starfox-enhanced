#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace starfox::render {
// Packed Global Enhancements submenu controls keep their old slot identities.
// Stage Weather and Camera Banking are retired, not reassigned.
inline constexpr std::uint8_t scene_enhancement_mask=63;
inline constexpr std::uint8_t camera_response_mask=15;
// Persisted two-bit slots are never renumbered. Retired Soft Focus (3) and
// Phosphor Mask (10) remain holes, so old settings cannot select another FX.
inline constexpr unsigned global_enhancement_count=13;
inline constexpr std::uint32_t global_enhancement_legacy_mask=(1U<<(global_enhancement_count*2))-1;
inline constexpr std::uint32_t global_enhancement_retired_mask=(3U<<6)|(3U<<20);
inline constexpr std::uint32_t global_enhancement_mask=global_enhancement_legacy_mask&~global_enhancement_retired_mask;
inline constexpr std::array<unsigned,11> global_enhancement_indices{0,1,2,4,5,6,7,8,9,11,12};
inline constexpr std::array<std::string_view,global_enhancement_count> global_enhancement_names{
    "LIGHT SHAFTS", "ANAMORPHIC FLARE", "HALATION", "", "RADIAL BLUR",
    "HEAT HAZE", "SHARPEN", "VIGNETTE", "FILM GRAIN", "CRT SCANLINES",
    "", "CRT CURVATURE", "LENS GHOSTS"};
}
