// Transcribed from the PCVR frame loop (src/vr/application.cpp, sprite and
// background assembly plus background motion). Keep the two in step until
// the VR applications call these functions themselves.
#include "starfox/vr/frame_layers.hpp"
#include "starfox/vr/background_tiles.hpp"
#include "starfox/vr/enhanced_landscape.hpp"
#include "starfox/vr/game_model_pose.hpp"
#include "starfox/vr/scene_interpolation.hpp"
#include "starfox/vr/source_sprites.hpp"
#include "starfox/render/background_renderer.hpp"
#include "starfox/render/scaled_text_renderer.hpp"
#include "starfox/timing/fixed_step.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace starfox::vr {
FrameLayers assemble_frame_layers(const GameSceneSnapshot& scene,const FrameLayerInputs& in) {
    if(!scene.ppu) throw std::runtime_error("Frame layers need a PPU snapshot");
    const auto* snapshot=&scene;
    const bool srgb=in.srgb;
    const bool enhanced_sky=in.enhanced_landscape && in.prepare_photograph;
    FrameLayers out;
    auto packet=source_sprite_packet(*snapshot->ppu,snapshot->display_brightness,{},srgb,&snapshot->meters);
    const bool fixed_menu=snapshot->flow==simulation::GameFlowState::ex_pregame_menu;
    packet.model=source_ui_layer_matrix(
        float(snapshot->source_vanishing_point[0]),
        float(snapshot->source_vanishing_point[1]),fixed_menu).value();
    auto meters=source_meter_packet(snapshot->meters,snapshot->ppu->cgram,snapshot->display_brightness,srgb);
    // Meters use the inner 224x192 Super FX coordinates; OAM uses the
    // surrounding 256x224 PPU coordinates.
    meters.model=source_ui_layer_matrix(
        float(snapshot->source_vanishing_point[0]),
        float(snapshot->source_vanishing_point[1]),fixed_menu,true).value();
    BackgroundTileOptions background_options;
    background_options.colour_subtract=snapshot->background_colour_subtract;
    background_options.brightness=snapshot->display_brightness;
    background_options.scroll_override=snapshot->background_scroll_override;
    background_options.single_occurrence_top_rows=snapshot->background_unique_top_rows;
    background_options.ex_twin_planets=snapshot->background_ex_twin_planets;
    background_options.ex_face_planets=snapshot->background_ex_face_planets;
    background_options.ex_ocean_island=snapshot->background_ex_ocean_island;
    background_options.ex_volcanic_horizon=snapshot->background_ex_volcanic_horizon;
    background_options.ex_city_planets=snapshot->background_ex_city_planets;
    // Space black is the transparent sky between authored planet pixels.
    background_options.transparent_black=snapshot->background_unique_top_rows!=0;
    // Mode-2 terrain is a world layer: extend logical coordinates.
    if(snapshot->ppu->background_mode==2) {
        background_options.expanded_horizontal=true;
        background_options.horizontal_bounds={-384,640};
    }
    auto& next_backgrounds=out.backgrounds;
    if(snapshot->ppu->tunnel_scene) {
        const auto border=[&](unsigned y,unsigned x) {
            const auto ink=render::tunnel_border_index(*snapshot->ppu,y,x);
            return source_backdrop_colour(snapshot->ppu->cgram[ink],snapshot->display_brightness,srgb);
        };
        auto surround=tunnel_surround_packet(border(112,0),border(4,128),border(219,128));
        surround.model=source_layer_matrix(
            float(snapshot->source_vanishing_point[0])+16.F,
            float(snapshot->source_vanishing_point[1])+16.F).value();
        out.tunnel_surround.push_back(std::move(surround));
    }
    const bool intro_sky=!snapshot->ppu->tunnel_scene
        && snapshot->flow==simulation::GameFlowState::intro
        && !snapshot->meters.extended && snapshot->background_unique_top_rows==224;
    if(intro_sky)
        next_backgrounds.push_back(intro_star_sphere_packet(*snapshot->ppu,snapshot->display_brightness,srgb));
    if(!snapshot->ppu->tunnel_scene && snapshot->background_unique_space)
        next_backgrounds.push_back(intro_star_sphere_packet(*snapshot->ppu,snapshot->display_brightness,srgb,false,true));
    if(snapshot->ppu->background_mode>=1 && snapshot->ppu->background_mode<=2) {
        auto bg2=background_tile_packet(*snapshot->ppu,BackgroundLayer::bg2,background_options,srgb);
        if(intro_sky)
            bg2=intro_planet_packet(*snapshot->ppu,snapshot->display_brightness,srgb);
        else if(!snapshot->ppu->tunnel_scene && snapshot->background_unique_space)
            bg2=unique_planet_packet(*snapshot->ppu,background_options,snapshot->background_planet_rect,srgb);
        else if(!snapshot->ppu->tunnel_scene && snapshot->background_star_sphere) {
            bg2=intro_star_sphere_packet(*snapshot->ppu,snapshot->display_brightness,srgb,true,false,snapshot->background_retain_sky_scroll);
            if(snapshot->background_ex_face_planets && !bg2.geometry.texels.empty()) bg2.geometry.texels[7]|=256U;
        }
        else if(!snapshot->ppu->tunnel_scene && snapshot->background_space_horizon)
            bg2=snapshot->background_orbital_planet
                ?orbital_planet_sphere_packet(*snapshot->ppu,background_options,srgb,snapshot->background_orbital_thin,snapshot->background_orbital_entry,
                    snapshot->flow==simulation::GameFlowState::gameplay && in.ex_experience)
                :space_horizon_sphere_packet(*snapshot->ppu,background_options,srgb);
        else if(!snapshot->ppu->tunnel_scene && snapshot->background_landscape) {
            bg2=landscape_sphere_packet(*snapshot->ppu,background_options,
                112.F,srgb,snapshot->background_landscape_unique_half,snapshot->landscape_atlas_origin,
                snapshot->background_landscape_unique_right_half);
            if(snapshot->landscape_grid_height<0)
                place_landscape_ground(bg2,std::clamp(float(snapshot->landscape_grid_height)/256.F,-8.F,-.001F),true);
        }
        else if(snapshot->background_water_surround)
            bg2=water_surface_packet(*snapshot->ppu,background_options,
                std::clamp(std::abs(float(snapshot->camera.y-snapshot->shadow_height))/256.F,.01F,8.F),srgb);
        else bg2.model=packet.model;
        auto photograph=enhanced_sky?in.prepare_photograph():std::optional<DrawPacket>{};
        if(photograph) in.enhanced_landscape->retain_native_ground(bg2,*snapshot->ppu);
        if(!photograph && snapshot->flow==simulation::GameFlowState::game_over)
            next_backgrounds.push_back(game_over_star_sphere_packet(*snapshot->ppu,
                snapshot->display_brightness,snapshot->background_colour_subtract,srgb));
        if(photograph && snapshot->flow==simulation::GameFlowState::game_over) {
            next_backgrounds.push_back(std::move(*photograph));photograph.reset();
        }
        next_backgrounds.push_back(std::move(bg2));
        if(photograph) {
            next_backgrounds.push_back(std::move(*photograph));
            const auto& bodies=in.enhanced_landscape->bodies();
            next_backgrounds.insert(next_backgrounds.end(),bodies.begin(),bodies.end());
        }
        if(!photograph && !snapshot->ppu->tunnel_scene && snapshot->background_ex_city_planets) {
            auto planet_options=background_options;
            planet_options.scroll_override=std::array<int16_t,2>{0,248};
            next_backgrounds.push_back(unique_planet_packet(*snapshot->ppu,planet_options,{384,208,56,48},srgb));
        }
        if(!photograph && !snapshot->ppu->tunnel_scene && snapshot->background_orbital_entry) {
            auto planet_options=background_options;
            planet_options.scroll_override=std::array<int16_t,2>{0,312};
            next_backgrounds.push_back(unique_planet_packet(*snapshot->ppu,planet_options,{336,320,56,64},srgb));
        }
        if(snapshot->ppu->background_mode==1) {
            background_options.scroll_override.reset();
            auto bg3=background_tile_packet(*snapshot->ppu,BackgroundLayer::bg3,background_options,srgb);
            if(snapshot->background_water_surround)
                bg3=water_surround_packet(*snapshot->ppu,background_options,srgb);
            else bg3.model=packet.model;
            if(snapshot->background_water_surround)
                next_backgrounds.insert(next_backgrounds.end()-1,std::move(bg3));
            else next_backgrounds.push_back(std::move(bg3));
        }
    }
    DrawPacket bitmap;
    const bool replace_dialogue=replace_native_dialogue(*snapshot);
    if(snapshot->native_ex_bitmap) {
        BackgroundTileOptions options;
        options.brightness=snapshot->display_brightness;
        options.guard_inset=16;options.transparent_black=true;
        bitmap=background_tile_packet(*snapshot->ppu,BackgroundLayer::bg1,options,srgb);
        bitmap.model=packet.model;
        // Match desktop: replace active EX communications, retaining the
        // native pause and results bitmap.
        if(replace_dialogue) bitmap.geometry={};
    }
    auto& next_sprites=out.sprites;
    if(snapshot->flow==simulation::GameFlowState::title) {
        next_sprites=title_foreground_packets(*snapshot->ppu,
            snapshot->display_brightness,!snapshot->ex_title_logo_screen,
            snapshot->background_scroll_override,srgb);
        for(auto& foreground:next_sprites) foreground.model=packet.model;
    }
    const auto ppu_transform=packet.model;
    next_sprites.push_back(std::move(bitmap));
    next_sprites.push_back(std::move(packet));
    next_sprites.push_back(std::move(meters));
    if(snapshot->ppu->background_mode==3) {
        next_sprites=source_mode3_packets(*snapshot->ppu,
            snapshot->display_brightness,srgb,snapshot->background_scroll_override);
        for(auto& layer:next_sprites) layer.model=ppu_transform;
    }
    if(replace_dialogue) {
        if(!in.dialogue_layout || !in.rom || !in.symbols) throw std::runtime_error("Dialogue layers need ROM, symbols and layout");
        in.dialogue_layout->set_language(in.language);
        auto dialogue_packets=source_dialogue_packets(*in.rom,*in.symbols,
            snapshot->dialogue,*in.dialogue_layout,snapshot->ppu->cgram,snapshot->display_brightness,srgb);
        for(auto& layer:dialogue_packets) {
            layer.model=ppu_transform;next_sprites.push_back(std::move(layer));
        }
    }
    if(snapshot->briefing.active) {
        if(!in.rom || !in.symbols) throw std::runtime_error("Briefing layers need ROM and symbols");
        const auto append_text=[&](uint32_t address,int x,int y,int right,size_t count,uint8_t ink) {
            auto text=source_game_text_packet(*in.rom,*in.symbols,
                address,x,y,right,count,ink,snapshot->ppu->cgram,snapshot->display_brightness,srgb);
            text.model=ppu_transform;
            next_sprites.push_back(std::move(text));
        };
        const auto& brief=snapshot->briefing;
        append_text(brief.message_address,30,173,218,brief.visible_message_characters,101);
        append_text(brief.message_address,28,171,216,brief.visible_message_characters,109);
        append_text(brief.planet_name_address,30,41,224,brief.visible_planet_characters,97);
        append_text(brief.planet_name_address,28,39,224,brief.visible_planet_characters,100);
    }
    return out;
}
std::array<float,4> frame_clear_colour(const GameSceneSnapshot& scene,bool srgb) {
    const auto* snapshot=&scene;
    if(!snapshot->ppu) return {0,0,0,1};
    const bool controls=snapshot->flow==simulation::GameFlowState::controls_type
        || snapshot->flow==simulation::GameFlowState::controls_choice;
    return (controls || snapshot->flow==simulation::GameFlowState::continue_choice)
        ?source_menu_background_colour(*snapshot->ppu,snapshot->display_brightness,srgb)
        :snapshot->flow==simulation::GameFlowState::game_over || snapshot->ppu->tunnel_scene || snapshot->background_unique_top_rows!=0
            || snapshot->background_star_sphere || snapshot->background_space_horizon
        ?source_background_border_colour(snapshot->ppu->cgram,snapshot->display_brightness,srgb)
        :source_backdrop_colour(snapshot->ppu->cgram[0],snapshot->display_brightness,srgb);
}
std::vector<Matrix4> frame_background_models(std::span<const DrawPacket> uploaded_backgrounds,
    const GameSceneSnapshot& previous,const GameSceneSnapshot& current,double alpha,const FrameLayerInputs& in) {
    const auto* snapshot=&current;
    const auto* before=&previous;
    const bool enhanced_sky=in.enhanced_landscape && in.prepare_photograph;
    std::vector<Matrix4> model_updates;
    for(const auto& background:uploaded_backgrounds)
        if(!background.geometry.vertex_view().empty() || !background.geometry.line_view().empty())
            model_updates.push_back(background.model);
    if(!snapshot->ppu) return model_updates;
    // Each block below recomputes the full list from base models, so a later
    // matching block replaces an earlier one, exactly as the PCVR loop does.
    if(snapshot->background_orbital_planet && !snapshot->ppu->tunnel_scene && before->ppu) {
        const bool continuous=before->background_orbital_planet && before->flow==snapshot->flow
            && before->background_id==snapshot->background_id
            && !timing::camera_transform_is_discontinuous(before->camera,snapshot->camera);
        const auto scroll=[](const auto& state) {
            return state.background_scroll_override?float((*state.background_scroll_override)[1]):float(state.ppu->bg2_scroll_y);
        };
        const auto motion=orbital_horizon_motion(scroll(continuous?*before:*snapshot),scroll(*snapshot),
            continuous?alpha:1.,snapshot->background_orbital_thin,snapshot->background_orbital_entry,
            snapshot->flow==simulation::GameFlowState::gameplay && in.ex_experience);
        const auto photographic_motion=orbital_horizon_motion(scroll(continuous?*before:*snapshot),scroll(*snapshot),
            continuous?alpha:1.,snapshot->background_orbital_thin,snapshot->background_orbital_entry,false);
        model_updates.clear();
        for(std::size_t i=0;i<uploaded_backgrounds.size();++i) {
            const auto& background=uploaded_backgrounds[i];
            if(!background.geometry.vertex_view().empty() || !background.geometry.line_view().empty())
                model_updates.push_back(enhanced_sky && in.enhanced_landscape->orbital_body(background)
                    ?photographic_motion:i==0?motion:background.model);
        }
    }
    if(snapshot->background_water_surround) {
        const bool continuous=before->background_water_surround && before->flow==snapshot->flow
            && !timing::camera_transform_is_discontinuous(before->camera,snapshot->camera);
        const auto height=[](const auto& state) {
            return std::clamp(std::abs(float(state.camera.y-state.shadow_height))/256.F,.01F,8.F);
        };
        const auto motion=water_height_motion(height(continuous?*before:*snapshot),height(*snapshot),continuous?alpha:1.);
        model_updates.clear();
        for(const auto& background:uploaded_backgrounds) {
            if(background.geometry.vertex_view().empty() && background.geometry.line_view().empty()) continue;
            const auto payload=background.geometry.texel_view();
            const bool receiver=payload.size()>15 && (payload[15]&32U)!=0;
            model_updates.push_back(receiver?motion:background.model);
        }
    }
    if(snapshot->background_landscape) {
        const bool continuous=same_landscape_mapping(*before,*snapshot)
            && !timing::camera_transform_is_discontinuous(before->camera,snapshot->camera);
        const auto motion=landscape_camera_motion(*before,*snapshot,continuous?alpha:1.);
        model_updates.clear();
        for(std::size_t i=0;i<uploaded_backgrounds.size();++i) {
            const auto& background=uploaded_backgrounds[i];
            if(!background.geometry.vertex_view().empty() || !background.geometry.line_view().empty())
                model_updates.push_back((i==0 || snapshot->background_ex_city_planets
                    || (enhanced_sky && in.enhanced_landscape->landscape_body(background))
                    || (background.geometry.shared_texels && !background.geometry.vertex_view().empty()
                        && (background.geometry.vertex_view().front().texture[3]&backdrop_texture_flag)==backdrop_texture_flag))
                    ?motion:background.model);
        }
    }
    if(enhanced_sky && in.enhanced_landscape->scrolling_pattern()) {
        const auto motion=in.enhanced_landscape->pattern_motion(*before,*snapshot,alpha);
        model_updates.clear();
        for(const auto& background:uploaded_backgrounds) {
            if(background.geometry.vertex_view().empty() && background.geometry.line_view().empty()) continue;
            model_updates.push_back(in.enhanced_landscape->pattern_panorama(background)?motion:background.model);
        }
    }
    if(enhanced_sky && in.enhanced_landscape->moving_body()) {
        model_updates.clear();
        for(const auto& background:uploaded_backgrounds) {
            const auto vertices=background.geometry.vertex_view();
            if(vertices.empty() && background.geometry.line_view().empty()) continue;
            const auto body=in.enhanced_landscape->tracked_body(background);
            model_updates.push_back(body?in.enhanced_landscape->body_motion(*before,*snapshot,alpha,*body):background.model);
        }
    }
    if(snapshot->flow==simulation::GameFlowState::intro && !snapshot->meters.extended
        && snapshot->background_unique_top_rows==224) {
        const auto scroll=[](const auto& state) {
            return state.background_scroll_override?(*state.background_scroll_override)[1]:state.background_vertical_scroll;
        };
        const bool continuous=before->flow==snapshot->flow && before->background_unique_top_rows==224;
        const auto motion=intro_planet_motion(scroll(continuous?*before:*snapshot),scroll(*snapshot),
            continuous?alpha:1.,float(snapshot->source_vanishing_point[1])+16.F);
        model_updates.clear();
        for(std::size_t i=0;i<uploaded_backgrounds.size();++i) {
            const auto& background=uploaded_backgrounds[i];
            if(!background.geometry.vertex_view().empty() || !background.geometry.line_view().empty())
                model_updates.push_back(i==1?motion:background.model);
        }
    }
    return model_updates;
}
}
