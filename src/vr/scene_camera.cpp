#include "starfox/vr/scene_camera.hpp"
#include <cmath>
namespace starfox::vr {
std::optional<EyeCamera> model_eye_camera(const EyeCamera& camera,const Matrix4& model) noexcept {
    for(const auto* matrix:{&camera.view,&camera.projection,&model})
        for(float value:*matrix) if(!std::isfinite(value)) return {};
    if(model[3]!=0 || model[7]!=0 || model[11]!=0 || model[15]!=1) return {};
    EyeCamera result{};result.projection=camera.projection;result.effects=camera.effects;
    for(unsigned column=0;column<4;++column) for(unsigned row=0;row<4;++row) {
        double value=0;
        for(unsigned k=0;k<4;++k) value+=double(camera.view[k*4+row])*model[column*4+k];
        result.view[column*4+row]=static_cast<float>(value);
        if(!std::isfinite(result.view[column*4+row])) return {};
    }
    return result;
}
}
