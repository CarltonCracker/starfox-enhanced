#pragma once
#include "starfox/assets/shape.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace starfox::render {
struct RenderPose;

// Optional 3D stand-ins for the cartridge's asteroid sprites, which read as
// flat cut-outs in stereo. SUPER FX STYLE draws low-poly models through the
// ordinary shape path, flat shaded and dithered with the level's palette.
enum class AsteroidModels : std::uint8_t {
    sprite,
    super_fx,
};
inline constexpr std::uint8_t asteroid_model_mode_count = 2U;
inline constexpr std::array<std::string_view, asteroid_model_mode_count>
    asteroid_model_names{"SPRITE", "SUPER FX STYLE"};

// When `pose` draws `source` as a recognised asteroid (a whole-object sprite,
// or a lone textured quad such as BIG_METEOR), returns the 3D model and
// rewrites `pose` to draw it at the sprite's size and orientation. Otherwise
// returns nullptr and leaves `pose` untouched. Models are matched by texel
// content, so Original and Star Fox EX share them.
const assets::Shape* substitute_asteroid_model(
    const assets::Shape& source, RenderPose& pose, AsteroidModels mode);

// Exposed for tests.
[[nodiscard]] std::uint32_t asteroid_texture_hash(const assets::TextureImage& texture) noexcept;
[[nodiscard]] const assets::Shape* asteroid_model_for_texture(const assets::TextureImage& texture);
[[nodiscard]] std::span<const assets::Shape> asteroid_model_shapes();
}
