#pragma once
#include "starfox/vr/draw_packet.hpp"
#include "starfox/vr/game_scene.hpp"
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <vector>
namespace starfox::render {class ScaledTextRenderer;}
namespace starfox::simulation {class TrigTables;}
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
    // Flat presentation between source ticks (BackgroundTileOptions): the
    // fractional parts of interpolated BG2/BG3 scroll and the interpolated
    // Mode 2 horizon. Applied to the plain native tile layers only.
    std::optional<std::array<float,2>> bg2_scroll_fraction,bg3_scroll_fraction,mode2_horizon;
    // Flat first-person view: draw MHUD's wing indicators (VR uses its own
    // cockpit instead). Required by renderers that draw them.
    const simulation::TrigTables* trigonometry{};
    // Widescreen HUD (desktop anchor_edge_hud): source pixels to move the
    // top/bottom HUD bands outward, left-half groups left and right-half
    // groups (and the boss meter) right. Zero keeps the native 256 layout.
    int hud_edge_shift{};
    // Chequered tunnels as a 3D corridor (vr::tunnel_corridor_packet) instead
    // of the native BG2 panel; position it with frame_background_models'
    // caller (tunnel_corridor_model). Falls back when the art is not a corridor.
    bool tunnel_corridor{};
    // The renderer draws the first-person reticle as geometry
    // (source_vector_reticle_packet) per frame: leave its objects out of the
    // sprite layer whenever that packet can be built.
    bool vector_reticle{};
    // Planet landscapes as a 3D sky dome and ground plane
    // (landscape_world_packet) instead of the native BG2 panel.
    bool landscape_world{};
    // With landscape_world: stream the classic ground's bands with the world.
    bool classic_ground_scroll{};
    // Space star/horizon spheres: drop the Mode 2 column slope (the
    // cartridge's roll shear) and keep only its height at the screen centre;
    // the renderer applies the real camera roll as geometry instead
    // (space_background_roll).
    bool space_roll_geometry{};
    // With landscape_world: draw the ground dots on the 3D ground plane
    // (anchored, anti-aliased, out to the horizon) instead of the native
    // lattice; the renderer drops the grid packet.
    bool landscape_ground_dots{};
    // The level's unpitched horizon art row (the renderer applies camera
    // pitch as geometry); empty = this tick's pitched art horizon.
    std::optional<float> landscape_horizon;
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
// Space backgrounds (star sphere, space horizon, unique planet) whose roll
// the renderer applies as geometry when FrameLayerInputs::space_roll_geometry.
[[nodiscard]] bool space_background_roll(const GameSceneSnapshot&) noexcept;
// The ground dots' colour (model palette 14 with the display fade), RGBA.
[[nodiscard]] uint32_t landscape_dot_colour(const GameSceneSnapshot&) noexcept;
std::vector<Matrix4> frame_background_models(std::span<const DrawPacket> backgrounds,
    const GameSceneSnapshot& previous,const GameSceneSnapshot& current,double alpha,const FrameLayerInputs&);
}
