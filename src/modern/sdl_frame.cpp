#include "starfox/modern/sdl_frame.hpp"
#include "starfox/vr/game_model_pose.hpp"
#include "starfox/vr/source_sprites.hpp"
#include "starfox/timing/fixed_step.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
namespace starfox::modern {
namespace {
void require(bool ok,const std::string& what) {if(!ok) throw std::runtime_error(what);}
constexpr uint32_t surrounding_stars_key=0x20000U;
// make_sky_vertices (background_tiles.cpp): 64 x 32 cells, two triangles each.
constexpr std::size_t sky_sphere_vertex_count=64*32*6;
// Wrapped 16-bit source word lerp (the desktop's interpolate_source_word).
int16_t interpolate_word(int16_t previous,int16_t current,double alpha) noexcept {
    const auto difference=int16_t(uint16_t(current)-uint16_t(previous));
    return int16_t(uint16_t(int32_t(previous)+int32_t(std::lround(double(difference)*alpha))));
}
}
// The PPU background state between two ticks, or null when nothing moves or
// the scene is discontinuous (new background, flow, mode or a camera cut).
std::shared_ptr<const simulation::SnesPpuState> interpolated_background_ppu(const vr::GameSceneSnapshot& previous,
    const vr::GameSceneSnapshot& current,double alpha) {
    if(!previous.ppu || !current.ppu || !(alpha>0 && alpha<1)) return {};
    const auto& a=*previous.ppu;const auto& b=*current.ppu;
    if(previous.background_id!=current.background_id || previous.flow!=current.flow
        || a.background_mode!=b.background_mode || a.tunnel_scene!=b.tunnel_scene
        || timing::camera_transform_is_discontinuous(previous.camera,current.camera)) return {};
    const bool moving=a.bg1_scroll_x!=b.bg1_scroll_x || a.bg1_scroll_y!=b.bg1_scroll_y
        || a.bg2_scroll_x!=b.bg2_scroll_x || a.bg2_scroll_y!=b.bg2_scroll_y
        || a.bg3_scroll_x!=b.bg3_scroll_x || a.bg3_scroll_y!=b.bg3_scroll_y
        || a.bg2_horizontal_offsets!=b.bg2_horizontal_offsets || a.bg2_scanline_scroll_y!=b.bg2_scanline_scroll_y;
    if(!moving) return {};
    auto ppu=std::make_shared<simulation::SnesPpuState>(b);
    ppu->bg1_scroll_x=interpolate_word(a.bg1_scroll_x,b.bg1_scroll_x,alpha);
    ppu->bg1_scroll_y=interpolate_word(a.bg1_scroll_y,b.bg1_scroll_y,alpha);
    ppu->bg2_scroll_x=interpolate_word(a.bg2_scroll_x,b.bg2_scroll_x,alpha);
    // BG2's vertical phase wraps at 9 bits (CALCBGSCROLL).
    ppu->bg2_scroll_y=int16_t(timing::interpolate_wrapped_scroll(uint16_t(a.bg2_scroll_y),uint16_t(b.bg2_scroll_y),alpha,0x1ffU));
    ppu->bg3_scroll_x=interpolate_word(a.bg3_scroll_x,b.bg3_scroll_x,alpha);
    ppu->bg3_scroll_y=interpolate_word(a.bg3_scroll_y,b.bg3_scroll_y,alpha);
    for(std::size_t row=0;row<ppu->bg2_horizontal_offsets.size();++row) {
        ppu->bg2_horizontal_offsets[row]=interpolate_word(a.bg2_horizontal_offsets[row],b.bg2_horizontal_offsets[row],alpha);
        ppu->bg2_scanline_scroll_y[row]=interpolate_word(a.bg2_scanline_scroll_y[row],b.bg2_scanline_scroll_y[row],alpha);
    }
    return ppu;
}
namespace {
}
bool SdlFrameRenderer::initialize(SDL_GPUDevice* device,SDL_GPUTextureFormat colour,SDL_GPUTextureFormat depth) {
    release();
    if(!pipelines_.initialize(device,colour,depth)) {status_=pipelines_.status();return false;}
    device_=device;status_="MODERN frame ready";return true;
}
void SdlFrameRenderer::release() noexcept {
    for(auto* packets:{&stars_,&backgrounds_,&tunnel_,&sprites_,&circle_,&shutter_}) packets->release();
    world_.release();uploads_.release();pipelines_.release();
    background_packets_.clear();layer_revision_.reset();circle_blend_.reset();background_interpolated_=false;
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
        // Sprites, HUD and the tunnel surround change with the source tick (20 Hz).
        const bool new_tick=!layer_revision_ || *layer_revision_!=current.revision;
        if(new_tick) {
            auto layers=vr::assemble_frame_layers(current,inputs);
            require(tunnel_.initialize(device_,uploads_,layers.tunnel_surround,{},false),tunnel_.status());
            require(sprites_.initialize(device_,uploads_,layers.sprites,{},false),sprites_.status());
            layer_revision_=current.revision;
        }
        // Backgrounds follow the source's scroll registers and per-scanline
        // offset tables (how the cartridge fakes roll and turns). Interpolate
        // them at display rate, as the desktop renderer does, instead of
        // stepping at 20 Hz.
        const auto interpolated=interpolated_background_ppu(previous,current,alpha);
        if(new_tick || interpolated || background_interpolated_) {
            auto scene=current;
            if(interpolated) scene.ppu=interpolated;
            auto background_inputs=inputs;background_inputs.backgrounds_only=true;
            auto layers=vr::assemble_frame_layers(scene,background_inputs);
            require(backgrounds_.initialize(device_,uploads_,layers.backgrounds,{},false),backgrounds_.status());
            background_packets_=std::move(layers.backgrounds);
            background_interpolated_=bool(interpolated);
        }
        static const bool trace=std::getenv("STARFOX_TRACE_MODERN")!=nullptr;
        if(trace) std::fprintf(stderr,"modern-frame tick=%llu alpha=%.3f background=%s vp=%d,%d bg2=%d,%d vscroll=%d grid=%d atlas=%u\n",
            static_cast<unsigned long long>(current.revision),alpha,
            interpolated?"interpolated":new_tick?"tick":"retained",
            int(current.source_vanishing_point[0]),int(current.source_vanishing_point[1]),
            current.ppu?int(current.ppu->bg2_scroll_x):0,current.ppu?int(current.ppu->bg2_scroll_y):0,
            int(current.background_vertical_scroll),int(current.landscape_grid_height),unsigned(current.landscape_atlas_origin));
        // Display-rate background motion (landscape pitch, water height, ...).
        auto background_models=vr::frame_background_models(background_packets_,previous,current,alpha,inputs);
        // VR's sky spheres (and the landscape ground receiver built on the
        // same mesh) sample the source at 512 pixels per radian / unit
        // tangent, half the SNES camera's focal length of 256, so a flat view
        // showed mountains at half size over a mismatched ground. The shader
        // derives that mapping from model-space positions, so widening the
        // sphere 2x in X and Y (relative to Z) restores the SNES scale: exact
        // vertically at the centre column, close to it horizontally.
        std::size_t model_index=0;
        for(const auto& packet:background_packets_) {
            const auto vertices=packet.geometry.vertex_view();
            if(vertices.empty() && packet.geometry.line_view().empty()) continue;
            if(vertices.size()==sky_sphere_vertex_count && (vertices.front().texture[3]&8U)) {
                auto& model=background_models.at(model_index);
                for(unsigned row=0;row<4;++row) {model[0*4+row]*=2.F;model[1*4+row]*=2.F;}
            }
            ++model_index;
        }
        require(backgrounds_.update_models(background_models),"MODERN background motion update failed");
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
