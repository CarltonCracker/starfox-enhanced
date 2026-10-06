#pragma once
#include <array>
#include <optional>
namespace starfox::vr {
// Column-major matrices multiplying column vectors. World/view coordinates
// are right-handed (+Y up, forward -Z), as in OpenXR LOCAL space. Projection
// uses Vulkan's downward framebuffer Y and [0,1] depth, with positive viewport
// height. Do not apply a second Y inversion in the renderer.
using Matrix4 = std::array<float,16>;
struct EyeCamera {
    Matrix4 view;
    Matrix4 projection;
    std::array<unsigned,4> effects{};
};
// Affine view stores three rows; projection remains a full column-major matrix.
// Leave 16 bytes for per-draw effects within Vulkan's guaranteed 128 bytes.
struct SceneConstants {
    std::array<float,12> view_rows;
    Matrix4 projection;
    std::array<unsigned,4> effects;
};
static_assert(sizeof(SceneConstants)==128);
inline SceneConstants scene_constants(const EyeCamera& camera) noexcept {
    SceneConstants result{};
    for(unsigned row=0;row<3;++row) for(unsigned col=0;col<4;++col)
        result.view_rows[row*4+col]=camera.view[col*4+row];
    result.projection=camera.projection;result.effects=camera.effects;
    return result;
}
// Compose only the small matrices on the CPU; all model vertex transforms
// remain in the GPU vertex shader. Model maps object-local to world space.
std::optional<EyeCamera> model_eye_camera(const EyeCamera&,const Matrix4& model) noexcept;
}
