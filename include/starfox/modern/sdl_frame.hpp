#pragma once
#include "starfox/modern/sdl_source_scene.hpp"
#include "starfox/modern/texture_dump.hpp"
#include "starfox/vr/frame_layers.hpp"
#include <map>
#include <optional>
namespace starfox::modern {
// Background state between two ticks for display-rate rebuilds: the PPU
// with integer scroll registers/scanline tables (null when static or
// discontinuous), the fractional scroll remainders, and the Mode 2 horizon
// line fitted to each tick's VRAM offset table and interpolated.
struct BackgroundPresentation {
    std::shared_ptr<const simulation::SnesPpuState> ppu;
    std::optional<std::array<float,2>> bg2_fraction,bg3_fraction,mode2_horizon;
};
BackgroundPresentation interpolated_background(const vr::GameSceneSnapshot& previous,
    const vr::GameSceneSnapshot& current,double alpha);
// Least-squares line through the valid entries of the BG2 Mode 2 vertical
// offset table (VRAM 0x2fa0): {intercept, slope per column}, column 1 = x 0..7.
std::optional<std::array<double,2>> mode2_horizon_fit(const simulation::SnesPpuState&);
// A standard infinite perspective (VR's: clip z = -z - near) rewritten for
// reversed depth (clip z = near); other projections are returned unchanged.
vr::EyeCamera reversed_depth_camera(vr::EyeCamera) noexcept;
// MODERN's whole frame, drawn from the shared VR scene code in VR's layer
// order: surrounding stars, backgrounds, tunnel surround, world (compute
// route), first-person MHUD lines, circle wipe, sprites/HUD/dialogue, shutter. Only the camera and
// the target differ from the headset path.
class SdlFrameRenderer {
public:
    bool initialize(SDL_GPUDevice*,SDL_GPUTextureFormat colour,SDL_GPUTextureFormat depth);
    // Once per presented frame, before the render pass. `world` is
    // SourceModels::assemble_world_interpolated(previous,current,alpha,srgb,true).
    // Sprites/HUD rebuild per source tick; backgrounds also at display rate
    // while their scroll registers or scanline tables move.
    bool prepare(SDL_GPUCommandBuffer*,const vr::GameSceneSnapshot& previous,const vr::GameSceneSnapshot& current,
        double alpha,vr::SourceModelPackets world,const vr::FrameLayerInputs&);
    // Inside a render pass on targets of the initialized formats, with the
    // depth target cleared to clear_depth(). The frame draws with reversed
    // depth (reversed_depth_camera) for uniform precision at every distance:
    // coplanar source surfaces resolve by draw order, not depth rounding.
    bool record(SDL_GPUCommandBuffer*,SDL_GPURenderPass*,const vr::EyeCamera&);
    static constexpr float clear_depth() noexcept {return 0.F;}
    // Smooth (bilinear) filtering of sprites, HUD, flat backgrounds and model
    // textures; on by default (STARFOX_MODERN_FILTER=0 turns it off).
    void set_filter_2d(bool enabled) noexcept {filter_2d_=enabled;}
    // Replacement landscape artwork: <directory>/bg_XXXX_sky.png and
    // bg_XXXX_ground.png (XXXX = background id, hex), loaded once per level.
    void set_texture_directory(std::string directory) {texture_directory_=std::move(directory);}
    // Writes each planet background's art once (owned by the caller; null = off).
    void set_texture_dump(TextureDump* dump) noexcept {texture_dump_=dump;}
    // Target size and pixels per source pixel for source line thickness.
    void set_target(unsigned width,unsigned height,unsigned source_pixel) noexcept {
        pipelines_.set_line_brush(source_pixel,width,height);
        source_pixel_=float(height)/224.F;
    }
    const SdlSourceScene& world() const noexcept {return world_;}
    void release() noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    SDL_GPUDevice* device_{};
    SdlScenePipelines pipelines_;
    SdlUploadBatch uploads_;
    SdlDrawPackets stars_,backgrounds_,tunnel_,cockpit_,sprites_,reticle_,circle_,shutter_,textures_,colour_math_;
    std::vector<vr::DrawPacket> texture_packets_;
    std::optional<uint16_t> texture_background_;
    std::string texture_directory_;
    TextureDump* texture_dump_{};
    uint32_t texture_mapping_{};
    float source_pixel_{1};
    std::optional<float> level_horizon_;
    std::map<int,unsigned> horizon_votes_;
    uint16_t level_background_{};
    SdlSourceScene world_;
    std::vector<vr::DrawPacket> background_packets_;
    std::optional<uint64_t> layer_revision_;
    std::optional<SceneBlend> circle_blend_;
    bool colour_math_active_{},colour_math_over_hud_{};
    std::optional<SceneBlend> colour_math_blend_;
    bool circle_active_{},circle_over_hud_{},shutter_active_{},background_interpolated_{},cockpit_active_{},reticle_active_{},filter_2d_{true};
    std::string status_{"MODERN frame not initialized"};
};
}
