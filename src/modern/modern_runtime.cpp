#include "starfox/modern/modern_runtime.hpp"
#include "starfox/vr/source_models.hpp"
#include <SDL3/SDL.h>
#include <stdexcept>
namespace starfox::modern {
namespace {
void require(bool ok,const std::string& what) {if(!ok) throw std::runtime_error(what+(SDL_GetError()[0]?std::string(": ")+SDL_GetError():""));}
// SDL_PIXELFORMAT_ABGR8888 render targets are R8G8B8A8_UNORM on SDL_GPU.
constexpr auto target_format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}
vr::EyeCamera flat_scene_camera(const vr::GameSceneSnapshot& scene,float aspect) noexcept {
    auto camera=vr::source_panel_camera(scene);
    if(!(aspect>0)) return camera;
    // source_panel_camera maps the 256x224 canvas onto the target (m[0]=2 for
    // a 128-pixel half width). Keep 112 rows above/below centre and widen.
    const float half_width=112.F*aspect;
    camera.projection[0]=256.F/half_width;
    camera.projection[8]=(128.F-(float(scene.source_vanishing_point[0])+16.F))/half_width;
    return camera;
}
struct ModernRuntime::State {
    State(const simulation::GameSimulation& game,const assets::RomImage& rom,const assets::SymbolMap& symbols)
        :history(game,rom,symbols),models(rom,symbols,true,true),dialogue_layout(rom,symbols) {
        inputs.rom=&rom;inputs.symbols=&symbols;inputs.dialogue_layout=&dialogue_layout;
        inputs.srgb=false;inputs.ex_experience=game.experience()==simulation::Experience::starfox_ex;
    }
    vr::GameSceneHistory history;
    vr::SourceModels models;
    render::ScaledTextRenderer dialogue_layout;
    vr::FrameLayerInputs inputs;
    SdlFrameRenderer frame;
    SDL_GPUDevice* device{};
    SDL_Renderer* renderer{};
    SDL_Texture* target{};
    SDL_GPUTexture* depth{};
    SDL_GPUTextureFormat depth_format{};
    int width{},height{};
    bool captured{};
    void release_targets() noexcept {
        if(target) SDL_DestroyTexture(target);
        if(depth && device) SDL_ReleaseGPUTexture(device,depth);
        target=nullptr;depth=nullptr;width=height=0;
    }
    ~State() {release_targets();frame.release();}
};
ModernRuntime::ModernRuntime(const simulation::GameSimulation& game,const assets::RomImage& rom,const assets::SymbolMap& symbols)
    :state_(std::make_unique<State>(game,rom,symbols)) {}
ModernRuntime::~ModernRuntime()=default;
void ModernRuntime::capture_tick() {state_->history.capture();state_->captured=true;}
void ModernRuntime::reset_interpolation() noexcept {state_->history.reset_interpolation();}
std::size_t ModernRuntime::unsupported_models() const noexcept {return state_->frame.world().unsupported_models();}
bool ModernRuntime::draws(const simulation::GameSimulation& game) noexcept {
    const auto flow=game.flow_state();
    return (flow==simulation::GameFlowState::gameplay || flow==simulation::GameFlowState::training)
        && !game.paused() && !game.runtime_options_open();
}
bool ModernRuntime::present(SDL_Renderer* renderer,double alpha,int requested_width,int requested_height,const char* capture_path) {
    auto& s=*state_;
    try {
        require(renderer!=nullptr,"MODERN needs a renderer");
        if(!s.captured) {
            // Ticks run before the first gameplay tick (preroll, menus) were
            // not captured: start from the current state without interpolation.
            s.history.capture();s.history.reset_interpolation();s.captured=true;
        }
        auto* device=static_cast<SDL_GPUDevice*>(SDL_GetPointerProperty(
            SDL_GetRendererProperties(renderer),SDL_PROP_RENDERER_GPU_DEVICE_POINTER,nullptr));
        require(device!=nullptr,"MODERN needs SDL's GPU render driver");
        if(device!=s.device || renderer!=s.renderer) {
            s.release_targets();s.frame.release();
            s.device=device;s.renderer=renderer;
            s.depth_format=SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)?SDL_GPU_TEXTUREFORMAT_D32_FLOAT:SDL_GPU_TEXTUREFORMAT_D16_UNORM;
            require(s.frame.initialize(device,target_format,s.depth_format),s.frame.status());
        }
        int width=requested_width,height=requested_height;
        // Default: the window's output, not whatever intermediate target is bound.
        if(width<=0 || height<=0)
            require(SDL_GetRenderOutputSize(renderer,&width,&height) && width>0 && height>0,"Read MODERN output size");
        if(width!=s.width || height!=s.height) {
            if(std::getenv("STARFOX_TRACE_MODERN")) {
                int window_w=0,window_h=0,current_w=0,current_h=0;
                SDL_GetWindowSizeInPixels(SDL_GetRenderWindow(renderer),&window_w,&window_h);
                SDL_GetCurrentRenderOutputSize(renderer,&current_w,&current_h);
                std::fprintf(stderr,"modern-target: output=%dx%d window=%dx%d current=%dx%d\n",
                    width,height,window_w,window_h,current_w,current_h);
            }
            s.release_targets();
            s.target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_TARGET,width,height);
            require(s.target!=nullptr,"Create MODERN target");
            SDL_SetTextureBlendMode(s.target,SDL_BLENDMODE_NONE);
            SDL_GPUTextureCreateInfo depth{};
            depth.type=SDL_GPU_TEXTURETYPE_2D;depth.format=s.depth_format;depth.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            depth.width=uint32_t(width);depth.height=uint32_t(height);depth.layer_count_or_depth=1;depth.num_levels=1;
            s.depth=SDL_CreateGPUTexture(device,&depth);
            require(s.depth!=nullptr,"Create MODERN depth");
            s.width=width;s.height=height;
        }
        auto* colour=static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(
            SDL_GetTextureProperties(s.target),SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER,nullptr));
        require(colour!=nullptr,"MODERN target has no GPU texture");
        const auto previous=s.history.previous(),current=s.history.current();
        require(previous && current && current->ppu,"MODERN scene history incomplete");
        auto world=s.models.assemble_world_interpolated(*previous,*current,alpha,s.inputs.srgb,true);
        require(world.pending.empty(),"MODERN world assembly pending");
        // The renderer's own queued work must reach the GPU before ours.
        SDL_FlushRenderer(renderer);
        auto* command=SDL_AcquireGPUCommandBuffer(device);
        require(command!=nullptr,"Acquire MODERN command buffer");
        if(!s.frame.prepare(command,*previous,*current,alpha,std::move(world),s.inputs)) {
            SDL_CancelGPUCommandBuffer(command);throw std::runtime_error(s.frame.status());
        }
        const auto clear=vr::frame_clear_colour(*current,s.inputs.srgb);
        SDL_GPUColorTargetInfo target{};target.texture=colour;target.load_op=SDL_GPU_LOADOP_CLEAR;
        target.store_op=SDL_GPU_STOREOP_STORE;target.clear_color={clear[0],clear[1],clear[2],1};
        SDL_GPUDepthStencilTargetInfo depth_target{};depth_target.texture=s.depth;depth_target.clear_depth=1;
        depth_target.load_op=SDL_GPU_LOADOP_CLEAR;depth_target.store_op=SDL_GPU_STOREOP_DONT_CARE;
        depth_target.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth_target.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        auto* pass=SDL_BeginGPURenderPass(command,&target,1,&depth_target);
        if(!pass) {SDL_CancelGPUCommandBuffer(command);throw std::runtime_error("Begin MODERN render pass");}
        const bool recorded=s.frame.record(command,pass,flat_scene_camera(*current,float(width)/float(height)));
        SDL_EndGPURenderPass(pass);
        require(SDL_SubmitGPUCommandBuffer(command),"Submit MODERN frame");
        require(recorded,s.frame.status());
        // Blit to the window without the desktop's logical presentation.
        int logical_w=0,logical_h=0;SDL_RendererLogicalPresentation mode{};
        SDL_GetRenderLogicalPresentation(renderer,&logical_w,&logical_h,&mode);
        SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderTarget(renderer,nullptr);SDL_SetRenderViewport(renderer,nullptr);
        require(SDL_RenderTexture(renderer,s.target,nullptr,nullptr),"Draw MODERN frame");
        if(capture_path) {
            // Capture the GPU target that is blitted 1:1 to the window (never
            // a CPU buffer). SDL_RenderReadPixels returns nothing for hidden
            // test windows, so download the texture itself.
            SDL_GPUTransferBufferCreateInfo info{};
            info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=uint32_t(width)*uint32_t(height)*4U;
            if(auto* download=SDL_CreateGPUTransferBuffer(device,&info)) {
                auto* copy_command=SDL_AcquireGPUCommandBuffer(device);
                auto* copy=copy_command?SDL_BeginGPUCopyPass(copy_command):nullptr;
                if(copy) {
                    SDL_GPUTextureRegion region{};region.texture=colour;region.w=uint32_t(width);region.h=uint32_t(height);region.d=1;
                    SDL_GPUTextureTransferInfo destination{download,0,uint32_t(width),uint32_t(height)};
                    SDL_DownloadFromGPUTexture(copy,&region,&destination);SDL_EndGPUCopyPass(copy);
                    if(auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(copy_command)) {
                        SDL_WaitForGPUFences(device,true,&fence,1);SDL_ReleaseGPUFence(device,fence);
                        if(auto* pixels=SDL_MapGPUTransferBuffer(device,download,false)) {
                            if(auto* surface=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_ABGR8888,pixels,width*4)) {
                                SDL_SaveBMP(surface,capture_path);SDL_DestroySurface(surface);
                            }
                            SDL_UnmapGPUTransferBuffer(device,download);
                        }
                    }
                } else if(copy_command) SDL_CancelGPUCommandBuffer(copy_command);
                SDL_ReleaseGPUTransferBuffer(device,download);
            }
        }
        const bool presented=SDL_RenderPresent(renderer);
        SDL_SetRenderLogicalPresentation(renderer,logical_w,logical_h,mode);
        require(presented,"Present MODERN frame");
        status_.clear();return true;
    } catch(const std::exception& error) {status_=error.what();return false;}
}
}
