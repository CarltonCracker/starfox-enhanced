#pragma once
#include <SDL3/SDL_render.h>
#include <stdexcept>

namespace starfox::modern {
// MODERN temporarily bypasses SDL's logical presentation. Always restore the
// host's target-local state, including when a draw fails and GPU FAST takes over.
class SdlRenderStateGuard {
public:
    explicit SdlRenderStateGuard(SDL_Renderer* renderer):renderer_(renderer) {
        target_=SDL_GetRenderTarget(renderer_);
        viewport_set_=SDL_RenderViewportSet(renderer_);
        clip_enabled_=SDL_RenderClipEnabled(renderer_);
        if(!SDL_GetRenderLogicalPresentation(renderer_,&width_,&height_,&mode_)
            || !SDL_GetRenderViewport(renderer_,&viewport_)
            || !SDL_GetRenderClipRect(renderer_,&clip_)
            || !SDL_GetRenderScale(renderer_,&scale_x_,&scale_y_))
            throw std::runtime_error("Cannot preserve MODERN fallback renderer state");
    }
    ~SdlRenderStateGuard() {
        SDL_SetRenderTarget(renderer_,target_);
        SDL_SetRenderLogicalPresentation(renderer_,width_,height_,mode_);
        SDL_SetRenderScale(renderer_,scale_x_,scale_y_);
        SDL_SetRenderViewport(renderer_,viewport_set_?&viewport_:nullptr);
        SDL_SetRenderClipRect(renderer_,clip_enabled_?&clip_:nullptr);
    }
    SdlRenderStateGuard(const SdlRenderStateGuard&)=delete;
    SdlRenderStateGuard& operator=(const SdlRenderStateGuard&)=delete;
    SDL_RendererLogicalPresentation logical_mode() const noexcept {return mode_;}
private:
    SDL_Renderer* renderer_{};
    SDL_Texture* target_{};
    int width_{},height_{};
    SDL_RendererLogicalPresentation mode_{};
    SDL_Rect viewport_{},clip_{};
    float scale_x_{1},scale_y_{1};
    bool viewport_set_{},clip_enabled_{};
};
}
