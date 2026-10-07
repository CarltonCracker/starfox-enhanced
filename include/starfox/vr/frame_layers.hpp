#pragma once
#include "starfox/vr/draw_packet.hpp"
#include "starfox/vr/game_scene.hpp"
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <vector>
namespace starfox::render {class ScaledTextRenderer;}
namespace starfox::vr {
class EnhancedLandscape;
// The source 2D layers around the world pass, as the PCVR application builds
// them (src/vr/application.cpp): backgrounds before the world, the tunnel
// surround, then sprites/meters/bitmap/dialogue/briefing after it. Every
// packet is placed on the native viewing rays (source_layer_matrix), so a
// flat source_panel_camera lands them on the original screen pixels.
// TODO(MODERN): switch application.cpp and steam_frame_application.cpp to
// these functions once they can be tested in a headset.
struct FrameLayerInputs {
    const assets::RomImage* rom{};
    const assets::SymbolMap* symbols{};
    render::ScaledTextRenderer* dialogue_layout{}; // Required when native dialogue is replaced.
    uint8_t language{};
    bool srgb{};
    // Starfox EX gameplay (live->game.experience()==starfox_ex) for orbital planets.
    bool ex_experience{};
    // Optional photographic sky (VR "enhanced sky"): both or neither.
    EnhancedLandscape* enhanced_landscape{};
    std::function<std::optional<DrawPacket>()> prepare_photograph;
    // Build only backgrounds and the tunnel surround (no sprites/HUD), e.g.
    // for display-rate rebuilds from an interpolated PPU state.
    bool backgrounds_only{};
    // Flat-screen landscapes: keep the native BG2 layer (with the cartridge's
    // scroll and offset tables) instead of VR's landscape sphere and 3D
    // ground receiver. The cartridge draws its ground band above the true 3D
    // horizon; only the native layer reproduces that against 3D models.
    bool flat_landscape{};
};
struct FrameLayers {
    std::vector<DrawPacket> backgrounds,tunnel_surround,sprites;
};
// Layers for one source snapshot (one 20 Hz tick).
FrameLayers assemble_frame_layers(const GameSceneSnapshot&,const FrameLayerInputs&);
// Colour the frame is cleared to before any layer: the source backdrop,
// menu panel or border ink (application.cpp eye clear).
std::array<float,4> frame_clear_colour(const GameSceneSnapshot&,bool srgb);
// Display-rate background transforms between two snapshots, in the order the
// PCVR application applies them. One matrix per nonempty background packet.
std::vector<Matrix4> frame_background_models(std::span<const DrawPacket> backgrounds,
    const GameSceneSnapshot& previous,const GameSceneSnapshot& current,double alpha,const FrameLayerInputs&);
}
