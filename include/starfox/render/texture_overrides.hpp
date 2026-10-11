#pragma once
#include "starfox/assets/shape.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace starfox::render {
// Replacement artwork for a Super FX model texture, any size (RGBA words,
// red in the low byte). Alpha below one half is transparent, like index 0.
struct TextureOverrideImage {
    unsigned width{},height{};
    std::vector<std::uint32_t> rgba;
};
// A source texture's identity: its ROM address and nibble plane (the high
// nibble is a separate image sharing the same bytes).
inline std::uint32_t texture_override_key(const assets::TextureImage& texture) noexcept {
    return (texture.address&0xffffffU)|((texture.descriptor&0x2000U)!=0?0x1000000U:0U);
}
// File name stem shared by the texture dump and replacement loading:
// tex_AAAAAA_l / tex_AAAAAA_h (AAAAAA = SNES ROM address, hex).
inline std::string texture_override_name(std::uint32_t key) {
    char name[24];
    std::snprintf(name,sizeof name,"tex_%06x_%c",unsigned(key&0xffffffU),(key&0x1000000U)?'h':'l');
    return name;
}
using TextureOverrides=std::unordered_map<std::uint32_t,TextureOverrideImage>;
}
