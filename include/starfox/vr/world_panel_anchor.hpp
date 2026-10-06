#pragma once
#include "starfox/vr/eye_camera.hpp"
#include "starfox/vr/presentation.hpp"
#include <array>
#include <cmath>
#include <optional>
namespace starfox::vr {
// Capture in the real runtime LOCAL space, with yaw only: level text, stable
// world position. Both eyes see one retained pose, not per-eye head locking.
class WorldPanelAnchor {
public:
    void reset() noexcept {pose_.reset();}
    XrPosef pose(const std::array<XrView,2>& views,float distance=interface_panel_distance) noexcept {
        // A family transition must not retain the previous far/near anchor.
        if(distance!=distance_) {pose_.reset();distance_=distance;}
        if(!pose_) {
            const auto& q=views[0].pose.orientation;
            const float yaw=std::atan2(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x));
            XrPosef p{};p.orientation={0,std::sin(yaw*.5F),0,std::cos(yaw*.5F)};
            p.position={(views[0].pose.position.x+views[1].pose.position.x)*.5F-distance*std::sin(yaw),
                (views[0].pose.position.y+views[1].pose.position.y)*.5F,
                (views[0].pose.position.z+views[1].pose.position.z)*.5F-distance*std::cos(yaw)};
            pose_=p;
        }
        return *pose_;
    }
private:std::optional<XrPosef> pose_;float distance_{};
};
}
