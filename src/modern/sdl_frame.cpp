#include "starfox/modern/sdl_frame.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace starfox::modern {
namespace {
void require(bool ok,const std::string& what) {if(!ok) throw std::runtime_error(what);}
// PNG (or BMP) to RGBA words with red in the low byte; empty when absent.
std::optional<vr::ModernImage> load_rgba(const std::string& path) {
    SDL_Surface* loaded=SDL_LoadSurface(path.c_str());
    if(!loaded) return {};
    SDL_Surface* rgba=SDL_ConvertSurface(loaded,SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(loaded);
    if(!rgba) return {};
    vr::ModernImage image{unsigned(rgba->w),unsigned(rgba->h),{}};
    if(image.width==0 || image.height==0 || image.width>8192 || image.height>8192) {SDL_DestroySurface(rgba);return {};}
    image.pixels.resize(std::size_t(image.width)*image.height);
    for(unsigned y=0;y<image.height;++y)
        std::memcpy(image.pixels.data()+std::size_t(y)*image.width,static_cast<const uint8_t*>(rgba->pixels)+std::size_t(y)*rgba->pitch,image.width*4U);
    SDL_DestroySurface(rgba);
    return image;
}
SceneBlend sdl_blend(vr::SceneBlend blend) {
    switch(blend) {
    case vr::SceneBlend::opaque: return SceneBlend::opaque;
    case vr::SceneBlend::add: return SceneBlend::add;
    case vr::SceneBlend::subtract: return SceneBlend::subtract;
    case vr::SceneBlend::half_add: return SceneBlend::half_add;
    case vr::SceneBlend::half_subtract: return SceneBlend::half_subtract;
    case vr::SceneBlend::alpha: return SceneBlend::alpha;
    default: throw std::runtime_error("MODERN overlay blend has no SDL_GPU pipeline");
    }
}
}
void SdlFrameRenderer::set_texture_directory(std::string directory) {
    texture_directory_=std::move(directory);
    if(texture_directory_.empty()) {frame_.set_image_loader({});return;}
    frame_.set_image_loader([this](const std::string& name) {
        const auto path=texture_directory_+name;
        auto image=load_rgba(path);
        std::fprintf(stderr,"MODERN textures: %s %s\n",path.c_str(),image?"loaded":"(not present)");
        return image;
    });
}
void SdlFrameRenderer::set_texture_dump(TextureDump* dump) {
    texture_dump_=dump;
    if(!dump) {frame_.set_landscape_observer({});return;}
    frame_.set_landscape_observer([this](uint16_t background,const simulation::SnesPpuState& ppu,float horizon,std::span<const uint32_t> haze) {
        texture_dump_->landscape(background,ppu,horizon,haze);
    });
}
bool SdlFrameRenderer::initialize(SDL_GPUDevice* device,SDL_GPUTextureFormat colour,SDL_GPUTextureFormat depth) {
    release();
    if(!pipelines_.initialize(device,colour,depth,true)) {status_=pipelines_.status();return false;}
    device_=device;status_="MODERN frame ready";return true;
}
void SdlFrameRenderer::release() noexcept {
    for(auto* packets:{&stars_,&backgrounds_,&tunnel_,&cockpit_,&sprites_,&reticle_,&circle_,&shutter_,&textures_,&colour_math_}) packets->release();
    frame_.reset();world_.release();uploads_.release();pipelines_.release();device_=nullptr;
}
bool SdlFrameRenderer::prepare(SDL_GPUCommandBuffer* command,const vr::GameSceneSnapshot& previous,
    const vr::GameSceneSnapshot& current,double alpha,vr::SourceModelPackets world,const vr::FrameLayerInputs& inputs) {
    try {
        require(device_ && command,"MODERN frame not initialized");
        frame_.prepare(previous,current,alpha,std::move(world),inputs);
        const auto& f=frame_;
        require(stars_.initialize(device_,uploads_,f.stars,f.star_keys,false),stars_.status());
        if(f.layers_rebuilt) {
            require(tunnel_.initialize(device_,uploads_,f.tunnel_surround,{},false),tunnel_.status());
            require(sprites_.initialize(device_,uploads_,f.sprites,{},false),sprites_.status());
        }
        if(f.backgrounds_rebuilt) require(backgrounds_.initialize(device_,uploads_,f.backgrounds,{},false),backgrounds_.status());
        require(backgrounds_.update_models(f.background_models),"MODERN background motion update failed");
        if(f.textures_rebuilt) require(textures_.initialize(device_,uploads_,f.textures,{},false),textures_.status());
        if(!f.textures.empty()) require(textures_.update_models(f.texture_models),"MODERN landscape texture motion update failed");
        if(f.cockpit) require(cockpit_.initialize(device_,uploads_,std::span(&*f.cockpit,1),{},false),cockpit_.status());
        if(f.reticle) require(reticle_.initialize(device_,uploads_,std::span(&*f.reticle,1),{},false),reticle_.status());
        if(f.circle) require(circle_.initialize(device_,uploads_,std::span(&f.circle->packet,1),{},false,false,
            sdl_blend(f.circle->blend)),circle_.status());
        if(f.colour_math) require(colour_math_.initialize(device_,uploads_,std::span(&f.colour_math->packet,1),{},false,false,
            sdl_blend(f.colour_math->blend)),colour_math_.status());
        if(f.shutter) require(shutter_.initialize(device_,uploads_,std::span(&*f.shutter,1),{},false),shutter_.status());
        require(uploads_.flush(device_,command),uploads_.status());
        require(world_.initialize(device_,command,f.world,f.moving),world_.status());
        require(world_.record_compute(command),world_.status());
        status_="MODERN frame prepared";return true;
    } catch(const std::exception& error) {uploads_.clear();status_=error.what();return false;}
}
bool SdlFrameRenderer::record(SDL_GPUCommandBuffer* command,SDL_GPURenderPass* pass,const vr::EyeCamera& source_camera) {
    const auto& f=frame_;
    auto camera=reversed_depth_camera(source_camera);
    if(filter_2d_) camera.effects[3]|=1U;
    // Output pixels per source pixel, 4.4 fixed point (bits 8-19): ground dots.
    // A headset view measures it like the PCVR app: the native 1/256 tangent.
    const float source_pixel=f.settings().headset?target_height_*std::abs(camera.projection[5])/512.F:source_pixel_;
    camera.effects[3]|=uint32_t(std::clamp(source_pixel*16.F,16.F,4095.F))<<8;
    const auto fail=[&](const char* layer) {status_=std::string("MODERN ")+layer+" recording failed: "+pipelines_.status();return false;};
    if(!stars_.record(command,pass,pipelines_,camera)) return fail("star");
    {
        // A headset preview passes the source camera heading to the ground.
        auto background_camera=camera;
        if(f.settings().headset) background_camera.effects[2]=f.landscape_heading;
        else background_camera.effects[2]=f.sky_turn;
        if(!backgrounds_.record(command,pass,pipelines_,background_camera)) return fail("background");
    }
    if(!f.textures.empty()) {
        auto mapped=camera;mapped.effects[2]=f.texture_mapping;
        if(!textures_.record(command,pass,pipelines_,mapped)) return fail("landscape texture");
    }
    if(!tunnel_.record(command,pass,pipelines_,camera)) return fail("tunnel surround");
    if(!world_.record(command,pass,pipelines_,camera)) return fail("world");
    if(f.cockpit && !cockpit_.record(command,pass,pipelines_,camera)) return fail("cockpit HUD");
    if(f.circle && !f.circle->over_hud && !circle_.record(command,pass,pipelines_,camera)) return fail("circle");
    const vr::Matrix4 ndc{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    if(f.colour_math && !f.colour_math->over_hud && !colour_math_.record(command,pass,pipelines_,vr::EyeCamera{ndc,ndc}))
        return fail("colour math");
    if(!sprites_.record(command,pass,pipelines_,camera)) return fail("sprite");
    if(f.reticle && !reticle_.record(command,pass,pipelines_,camera)) return fail("reticle");
    if(f.circle && f.circle->over_hud && !circle_.record(command,pass,pipelines_,camera)) return fail("circle");
    if(f.colour_math && f.colour_math->over_hud && !colour_math_.record(command,pass,pipelines_,vr::EyeCamera{ndc,ndc}))
        return fail("colour math");
    // Head-locked normalized-device shutter: identity view and projection.
    if(f.shutter && !shutter_.record(command,pass,pipelines_,vr::EyeCamera{ndc,ndc})) return fail("shutter");
    return true;
}
}
