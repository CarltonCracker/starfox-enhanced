#include "starfox/modern/frame_policy.hpp"
#include "starfox/modern/sdl_render_state.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
}
int main() try {
    using namespace starfox::modern;
    for(unsigned stereo=0;stereo<=9;++stereo)
        for(bool setup:{false,true}) for(bool options:{false,true})
            require(flat_frame_supported(setup,options,std::uint8_t(stereo))
                ==(!setup && !options && stereo==0),"Flat presenter replaced a stereo/host frame");
    std::array<starfox::vr::SourceComputeModel,3> models;
    require(source_models_supported({}) && source_models_supported(models),"Ordinary scene was rejected");
    for(auto& model:models) {
        model.warp=std::make_shared<starfox::vr::SourceWarpInputs>();
        require(!source_models_supported(models),"Warp scene would lose a model instead of falling back");
        model.warp.reset();model.axis=std::make_shared<starfox::vr::SourceAxisInputs>();
        require(!source_models_supported(models),"Axis scene would lose a model instead of falling back");
        model.axis.reset();
    }
    auto* surface=SDL_CreateSurface(640,480,SDL_PIXELFORMAT_RGBA32);
    require(surface!=nullptr,"Create renderer test surface");
    auto* renderer=SDL_CreateSoftwareRenderer(surface);
    require(renderer!=nullptr,"Create renderer state fixture");
    for(bool custom:{false,true}) {
        require(SDL_SetRenderLogicalPresentation(renderer,320,224,SDL_LOGICAL_PRESENTATION_LETTERBOX),"Set logical canvas");
        require(SDL_SetRenderScale(renderer,1.25F,1.5F),"Set scale");
        const SDL_Rect viewport{7,9,300,200},clip{5,6,100,90};
        require(SDL_SetRenderViewport(renderer,custom?&viewport:nullptr),"Set viewport");
        require(SDL_SetRenderClipRect(renderer,custom?&clip:nullptr),"Set clip");
        for(bool failure:{false,true}) {
            try {
                const SdlRenderStateGuard guard{renderer};
                require(guard.logical_mode()==SDL_LOGICAL_PRESENTATION_LETTERBOX,"Captured wrong presentation");
                SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
                SDL_SetRenderScale(renderer,2,2);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderClipRect(renderer,nullptr);
                if(failure) throw 1;
            } catch(int) {}
            int w=0,h=0;SDL_RendererLogicalPresentation mode{};float sx=0,sy=0;
            require(SDL_GetRenderLogicalPresentation(renderer,&w,&h,&mode) && w==320 && h==224
                && mode==SDL_LOGICAL_PRESENTATION_LETTERBOX,"Fallback lost logical presentation");
            require(SDL_GetRenderScale(renderer,&sx,&sy) && sx==1.25F && sy==1.5F,"Fallback lost scale");
            require(SDL_RenderViewportSet(renderer)==custom && SDL_RenderClipEnabled(renderer)==custom,"Fallback lost state flags");
            if(custom) {
                SDL_Rect actual{};
                require(SDL_GetRenderViewport(renderer,&actual) && SDL_RectsEqual(&actual,&viewport),"Fallback lost viewport");
                require(SDL_GetRenderClipRect(renderer,&actual) && SDL_RectsEqual(&actual,&clip),"Fallback lost clip");
            }
        }
    }
    SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
    std::cout<<"MODERN complete-frame fallback and renderer-state recovery passed\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<": "<<SDL_GetError()<<'\n';return 1;}
