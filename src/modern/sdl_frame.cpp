#include "starfox/modern/sdl_frame.hpp"
#include "starfox/vr/game_model_pose.hpp"
#include "starfox/vr/source_sprites.hpp"
#include <algorithm>
#include <stdexcept>
namespace starfox::modern {
namespace {
void require(bool ok,const std::string& what) {if(!ok) throw std::runtime_error(what);}
constexpr uint32_t surrounding_stars_key=0x20000U;
}
bool SdlFrameRenderer::initialize(SDL_GPUDevice* device,SDL_GPUTextureFormat colour,SDL_GPUTextureFormat depth) {
    release();
    if(!pipelines_.initialize(device,colour,depth)) {status_=pipelines_.status();return false;}
    device_=device;status_="MODERN frame ready";return true;
}
void SdlFrameRenderer::release() noexcept {
    for(auto* packets:{&stars_,&backgrounds_,&tunnel_,&sprites_,&circle_,&shutter_}) packets->release();
    world_.release();uploads_.release();pipelines_.release();
    background_packets_.clear();layer_revision_.reset();circle_blend_.reset();
    circle_active_=circle_over_hud_=shutter_active_=false;device_=nullptr;
}
bool SdlFrameRenderer::prepare(SDL_GPUCommandBuffer* command,const vr::GameSceneSnapshot& previous,
    const vr::GameSceneSnapshot& current,double alpha,vr::SourceModelPackets world,const vr::FrameLayerInputs& inputs) {
    try {
        require(device_ && command,"MODERN frame not initialized");
        // The surrounding starfield is the distant environment, drawn first
        // and outside the world's depth (application.cpp does the same).
        std::vector<vr::DrawPacket> stars;std::vector<uint32_t> star_keys;
        const auto star=std::find(world.handles.begin(),world.handles.end(),surrounding_stars_key);
        if(star!=world.handles.end()) {
            const auto index=std::size_t(star-world.handles.begin());
            stars.push_back(world.packets[index]);star_keys.push_back(*star);
            world.packets.erase(world.packets.begin()+std::ptrdiff_t(index));world.handles.erase(star);
            for(auto& compute:world.compute_models) {
                require(compute.packet_index!=index,"Star pass unexpectedly owns a compute model");
                if(compute.packet_index>index) --compute.packet_index;
            }
        }
        require(stars_.initialize(device_,uploads_,stars,star_keys,false),stars_.status());
        // 2D layers change only with the source snapshot (20 Hz).
        if(!layer_revision_ || *layer_revision_!=current.revision) {
            auto layers=vr::assemble_frame_layers(current,inputs);
            require(tunnel_.initialize(device_,uploads_,layers.tunnel_surround,{},false),tunnel_.status());
            require(backgrounds_.initialize(device_,uploads_,layers.backgrounds,{},false),backgrounds_.status());
            require(sprites_.initialize(device_,uploads_,layers.sprites,{},false),sprites_.status());
            background_packets_=std::move(layers.backgrounds);
            layer_revision_=current.revision;
        }
        // Display-rate background motion (landscape pitch, water height, ...).
        require(backgrounds_.update_models(vr::frame_background_models(background_packets_,previous,current,alpha,inputs)),
            "MODERN background motion update failed");
        auto circle=vr::source_circle_packet(previous.circle,current.circle,alpha,current.display_brightness,inputs.srgb);
        circle_active_=!circle.geometry.vertices.empty();
        if(circle_active_) {
            const auto flags=current.circle.affected_layers;
            circle_over_hud_=(flags&0x10)!=0;
            circle_blend_=(flags&0x80)?((flags&0x40)?SceneBlend::half_subtract:SceneBlend::subtract)
                :((flags&0x40)?SceneBlend::half_add:SceneBlend::add);
            circle.model=vr::source_layer_matrix(float(current.source_vanishing_point[0])+16.F,
                float(current.source_vanishing_point[1])+16.F).value();
            require(circle_.initialize(device_,uploads_,std::span(&circle,1),{},false,false,circle_blend_),circle_.status());
        }
        auto shutter=vr::source_shutter_packet(previous.wipe,current.wipe,alpha);
        shutter_active_=!shutter.geometry.vertices.empty();
        if(shutter_active_) require(shutter_.initialize(device_,uploads_,std::span(&shutter,1),{},false),shutter_.status());
        require(uploads_.flush(device_,command),uploads_.status());
        require(world_.initialize(device_,command,world),world_.status());
        require(world_.record_compute(command),world_.status());
        status_="MODERN frame prepared";return true;
    } catch(const std::exception& error) {uploads_.clear();status_=error.what();return false;}
}
bool SdlFrameRenderer::record(SDL_GPUCommandBuffer* command,SDL_GPURenderPass* pass,const vr::EyeCamera& camera) {
    const auto fail=[&](const char* layer) {status_=std::string("MODERN ")+layer+" recording failed: "+pipelines_.status();return false;};
    if(!stars_.record(command,pass,pipelines_,camera)) return fail("star");
    if(!backgrounds_.record(command,pass,pipelines_,camera)) return fail("background");
    if(!tunnel_.record(command,pass,pipelines_,camera)) return fail("tunnel surround");
    if(!world_.record(command,pass,pipelines_,camera)) return fail("world");
    if(circle_active_ && !circle_over_hud_ && !circle_.record(command,pass,pipelines_,camera)) return fail("circle");
    if(!sprites_.record(command,pass,pipelines_,camera)) return fail("sprite");
    if(circle_active_ && circle_over_hud_ && !circle_.record(command,pass,pipelines_,camera)) return fail("circle");
    if(shutter_active_) {
        // Head-locked normalized-device shutter: identity view and projection.
        const vr::Matrix4 identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        if(!shutter_.record(command,pass,pipelines_,vr::EyeCamera{identity,identity})) return fail("shutter");
    }
    return true;
}
}
