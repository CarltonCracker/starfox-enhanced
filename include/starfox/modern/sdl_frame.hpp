#pragma once
#include "starfox/modern/sdl_source_scene.hpp"
#include "starfox/vr/frame_layers.hpp"
#include <optional>
namespace starfox::modern {
// PPU background registers/scanline tables between two ticks (null when
// static or discontinuous), for display-rate background rebuilds.
std::shared_ptr<const simulation::SnesPpuState> interpolated_background_ppu(const vr::GameSceneSnapshot& previous,
    const vr::GameSceneSnapshot& current,double alpha);
// MODERN's whole frame, drawn from the shared VR scene code in VR's layer
// order: surrounding stars, backgrounds, tunnel surround, world (compute
// route), circle wipe, sprites/HUD/dialogue, shutter. Only the camera and
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
    // Inside a render pass on targets of the initialized formats.
    bool record(SDL_GPUCommandBuffer*,SDL_GPURenderPass*,const vr::EyeCamera&);
    const SdlSourceScene& world() const noexcept {return world_;}
    void release() noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    SDL_GPUDevice* device_{};
    SdlScenePipelines pipelines_;
    SdlUploadBatch uploads_;
    SdlDrawPackets stars_,backgrounds_,tunnel_,sprites_,circle_,shutter_;
    SdlSourceScene world_;
    std::vector<vr::DrawPacket> background_packets_;
    std::optional<uint64_t> layer_revision_;
    std::optional<SceneBlend> circle_blend_;
    bool circle_active_{},circle_over_hud_{},shutter_active_{},background_interpolated_{};
    std::string status_{"MODERN frame not initialized"};
};
}
