#pragma once
#include "starfox/vr/scene_camera.hpp"
#include <openxr/openxr.h>
#include <array>
#include <optional>
namespace starfox::vr {
// Capture one stereo midpoint, then retain it: never cancel subsequent head
// movement or collapse the eye separation. Compositor poses stay unmodified.
class PositionAnchor {
public:
    bool apply(std::array<XrView,2>& views,float translation_scale=1.F) noexcept;
    // Re-capture the centre at the next apply(). keep_height retains the
    // previous vertical origin, so a recentre moves yaw and horizontal position
    // only; the first capture (no previous origin) always takes the live height.
    void reset(bool recenter_heading=false,bool keep_height=false) noexcept {
        if(origin_) height_=origin_->y;
        keep_height_=keep_height && (origin_.has_value() || keep_height_);
        origin_.reset();capture_heading_|=recenter_heading;
    }
    XrPosef anchored(XrPosef pose) const noexcept;
private:
    std::optional<XrVector3f> origin_;
    XrVector3f translation_offset_{};
    float yaw_{},height_{};
    bool capture_heading_{},keep_height_{};
};
// Pose translation is in metres; units_per_metre converts it to the scene's
// units. Near/far are already in scene units. nullopt far selects an infinite
// far plane. Invalid/non-finite tracking or projection inputs are rejected.
std::optional<EyeCamera> eye_camera(const XrView&,float units_per_metre,
    float near_plane,std::optional<float> far_plane=std::nullopt) noexcept;
// Headset-free stereo cameras for SBS. All distances are scene units; aspect
// is the unsqueezed scene aspect, including when packing Half SBS.
std::optional<std::array<EyeCamera,2>> sbs_eye_cameras(float vertical_fov,
    float aspect,float separation,float convergence,float near_plane,
    std::optional<float> far_plane=std::nullopt) noexcept;
}
