#include "starfox/modern/sdl_frame.hpp"
#include "starfox/vr/background_tiles.hpp"
#include "starfox/vr/game_model_pose.hpp"
#include "starfox/vr/source_sprites.hpp"
#include "starfox/timing/fixed_step.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <bit>
#include <numeric>
#include <stdexcept>
namespace starfox::modern {
namespace {
void require(bool ok,const std::string& what) {if(!ok) throw std::runtime_error(what);}
constexpr uint32_t surrounding_stars_key=0x20000U;
// make_sky_vertices (background_tiles.cpp): 64 x 32 cells, two triangles each.
constexpr std::size_t sky_sphere_vertex_count=64*32*6;
struct RgbaImage {unsigned width{},height{};std::vector<uint32_t> pixels;};
// PNG (or BMP) to RGBA words with red in the low byte; empty when absent.
std::optional<RgbaImage> load_rgba(const std::string& path) {
    SDL_Surface* loaded=SDL_LoadSurface(path.c_str());
    if(!loaded) return {};
    SDL_Surface* rgba=SDL_ConvertSurface(loaded,SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(loaded);
    if(!rgba) return {};
    RgbaImage image{unsigned(rgba->w),unsigned(rgba->h),{}};
    if(image.width==0 || image.height==0 || image.width>8192 || image.height>8192) {SDL_DestroySurface(rgba);return {};}
    image.pixels.resize(std::size_t(image.width)*image.height);
    for(unsigned y=0;y<image.height;++y)
        std::memcpy(image.pixels.data()+std::size_t(y)*image.width,static_cast<const uint8_t*>(rgba->pixels)+std::size_t(y)*rgba->pitch,image.width*4U);
    SDL_DestroySurface(rgba);
    return image;
}
// Source roll (65536 per turn) to screen rotation for landscape_world_model.
constexpr double roll_sign=-1;
// Source pitch to the landscape's geometric pitch.
constexpr double pitch_sign=-1;
// Wrapped 16-bit source word lerp (the desktop's interpolate_source_word).
int16_t interpolate_word(int16_t previous,int16_t current,double alpha) noexcept {
    const auto difference=int16_t(uint16_t(current)-uint16_t(previous));
    return int16_t(uint16_t(int32_t(previous)+int32_t(std::lround(double(difference)*alpha))));
}
}
std::optional<std::array<double,2>> mode2_horizon_fit(const simulation::SnesPpuState& ppu) {
    if(ppu.background_mode!=2 || !ppu.bg2_vertical_offsets_enabled) return {};
    // The shader's tile_horizon, in double precision.
    double count=0,sum_x=0,sum_y=0,sum_xx=0,sum_xy=0;
    int previous_raw=0,previous_unwrapped=0;
    for(int i=0;i<32;++i) {
        const auto address=std::size_t((0x2fa0U+unsigned(i))*2U)&0xffffU;
        const unsigned value=ppu.vram[address]|(unsigned(ppu.vram[(address+1)&0xffffU])<<8U);
        if(!(value&0x4000U)) continue;
        const int raw=int(value&8191U);
        int delta=(raw-previous_raw)&8191;if(delta>4095) delta-=8192;
        const int unwrapped=count==0?raw:previous_unwrapped+delta;
        previous_raw=raw;previous_unwrapped=unwrapped;
        const double x=i+1,y=unwrapped;
        count+=1;sum_x+=x;sum_y+=y;sum_xx+=x*x;sum_xy+=x*y;
    }
    if(count==0) return {};
    const double denominator=count*sum_xx-sum_x*sum_x;
    const double slope=count>1 && denominator!=0?(count*sum_xy-sum_x*sum_y)/denominator:0;
    return std::array<double,2>{(sum_y-slope*sum_x)/count,slope};
}
// The background between two ticks. Discontinuities (new background, flow,
// mode or a camera cut) present the current tick unchanged.
BackgroundPresentation interpolated_background(const vr::GameSceneSnapshot& previous,
    const vr::GameSceneSnapshot& current,double alpha) {
    BackgroundPresentation out;
    if(!current.ppu) return out;
    const auto& b=*current.ppu;
    const auto current_horizon=mode2_horizon_fit(b);
    if(current_horizon) out.mode2_horizon=std::array<float,2>{float((*current_horizon)[0]),float((*current_horizon)[1])};
    if(!previous.ppu || !(alpha>0 && alpha<1)) return out;
    const auto& a=*previous.ppu;
    if(previous.background_id!=current.background_id || previous.flow!=current.flow
        || a.background_mode!=b.background_mode || a.tunnel_scene!=b.tunnel_scene
        || timing::camera_transform_is_discontinuous(previous.camera,current.camera)) return out;
    // The cartridge's pitch/roll on Mode 2 ground lives in the VRAM offset
    // table, updated at 20 Hz: move its fitted line at display rate.
    if(const auto previous_horizon=mode2_horizon_fit(a); previous_horizon && current_horizon) {
        const double intercept=(*previous_horizon)[0]
            +std::remainder((*current_horizon)[0]-(*previous_horizon)[0],8192.)*alpha;
        const double slope=std::lerp((*previous_horizon)[1],(*current_horizon)[1],alpha);
        out.mode2_horizon=std::array<float,2>{float(intercept-8192.*std::floor(intercept/8192.)),float(slope)};
    }
    const bool moving=a.bg1_scroll_x!=b.bg1_scroll_x || a.bg1_scroll_y!=b.bg1_scroll_y
        || a.bg2_scroll_x!=b.bg2_scroll_x || a.bg2_scroll_y!=b.bg2_scroll_y
        || a.bg3_scroll_x!=b.bg3_scroll_x || a.bg3_scroll_y!=b.bg3_scroll_y
        || a.bg2_horizontal_offsets!=b.bg2_horizontal_offsets || a.bg2_scanline_scroll_y!=b.bg2_scanline_scroll_y;
    if(!moving) return out;
    auto ppu=std::make_shared<simulation::SnesPpuState>(b);
    // Integer registers for the payload plus the remainder the shader adds
    // between source pixels (a 10x raster otherwise steps 10 pixels at once).
    const auto split=[alpha](int16_t from,int16_t to,uint16_t mask,int16_t& word,float& fraction) {
        const double value=timing::interpolate_fractional_scroll(uint16_t(from),uint16_t(to),alpha,mask);
        const double whole=std::floor(value);
        word=int16_t(uint16_t(uint32_t(whole)&mask));fraction=float(value-whole);
    };
    ppu->bg1_scroll_x=interpolate_word(a.bg1_scroll_x,b.bg1_scroll_x,alpha);
    ppu->bg1_scroll_y=interpolate_word(a.bg1_scroll_y,b.bg1_scroll_y,alpha);
    std::array<float,2> bg2{},bg3{};
    split(a.bg2_scroll_x,b.bg2_scroll_x,0xffffU,ppu->bg2_scroll_x,bg2[0]);
    // BG2's vertical phase wraps at 9 bits (CALCBGSCROLL).
    split(a.bg2_scroll_y,b.bg2_scroll_y,0x1ffU,ppu->bg2_scroll_y,bg2[1]);
    split(a.bg3_scroll_x,b.bg3_scroll_x,0xffffU,ppu->bg3_scroll_x,bg3[0]);
    split(a.bg3_scroll_y,b.bg3_scroll_y,0xffffU,ppu->bg3_scroll_y,bg3[1]);
    out.bg2_fraction=bg2;out.bg3_fraction=bg3;
    for(std::size_t row=0;row<ppu->bg2_horizontal_offsets.size();++row) {
        ppu->bg2_horizontal_offsets[row]=interpolate_word(a.bg2_horizontal_offsets[row],b.bg2_horizontal_offsets[row],alpha);
        // Tunnel scanline values are CHEQUERED page selectors (24/280), not
        // positions: blending them samples rows between the two pages.
        if(!b.tunnel_scene)
            ppu->bg2_scanline_scroll_y[row]=interpolate_word(a.bg2_scanline_scroll_y[row],b.bg2_scanline_scroll_y[row],alpha);
    }
    out.ppu=std::move(ppu);
    return out;
}
bool SdlFrameRenderer::initialize(SDL_GPUDevice* device,SDL_GPUTextureFormat colour,SDL_GPUTextureFormat depth) {
    release();
    if(!pipelines_.initialize(device,colour,depth,true)) {status_=pipelines_.status();return false;}
    device_=device;status_="MODERN frame ready";return true;
}
void SdlFrameRenderer::release() noexcept {
    for(auto* packets:{&stars_,&backgrounds_,&tunnel_,&cockpit_,&sprites_,&reticle_,&circle_,&shutter_,&textures_}) packets->release();
    texture_packets_.clear();texture_background_.reset();
    world_.release();uploads_.release();pipelines_.release();
    background_packets_.clear();layer_revision_.reset();circle_blend_.reset();background_interpolated_=false;
    circle_active_=circle_over_hud_=shutter_active_=cockpit_active_=reticle_active_=false;device_=nullptr;
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
        // The cartridge's ground dots (grid packet 0x30000) tie objects to the
        // ground; keep them over the 3D landscape ground unless switched off.
        const bool landscape_world=inputs.landscape_world && current.background_landscape && current.ppu
            && !current.ppu->tunnel_scene;
        // With dots on, the landscape ground draws them itself (anchored to
        // the 3D ground); the native lattice is dropped either way.
        // STARFOX_MODERN_NATIVE_DOTS=1 keeps the native lattice too (alignment check).
        static const bool native_dots=std::getenv("STARFOX_MODERN_NATIVE_DOTS")!=nullptr;
        if(landscape_world && !current.grid_lines && !native_dots) {
            const auto grid=std::find(world.handles.begin(),world.handles.end(),0x30000U);
            if(grid!=world.handles.end()) {
                const auto index=std::size_t(grid-world.handles.begin());
                world.packets.erase(world.packets.begin()+std::ptrdiff_t(index));world.handles.erase(grid);
                for(auto& compute:world.compute_models) {
                    require(compute.packet_index!=index,"Grid pass unexpectedly owns a compute model");
                    if(compute.packet_index>index) --compute.packet_index;
                }
            }
        }
        // The cartridge paints objects in its own sorted list order (the native
        // draw list), so where two objects share a surface the later one
        // wins. With a depth buffer those surfaces z-fight. Scale each
        // placement about the eye (the flat camera's view is the identity) by
        // a tiny step per list position: screen positions are unchanged,
        // later objects move nearer by a relative step that exceeds
        // plane-reconstruction rounding and the shader's line/decal biases,
        // and stays far below visible interpenetration. Objects enclosing the
        // camera are drawn as a painter instead (SdlSourceScene::record).
        constexpr float draw_order_step=3.2e-5F;
        for(std::size_t i=0;i<world.packets.size();++i) {
            const float scale=1.F-draw_order_step*float(std::min<std::size_t>(i,1024));
            auto& model=world.packets[i].model;
            for(unsigned column=0;column<4;++column) for(unsigned row=0;row<3;++row) model[column*4+row]*=scale;
        }
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
        auto presentation=interpolated_background(previous,current,alpha);
        if(current.background_scroll_override) presentation.bg2_fraction.reset();
        const bool interpolated=presentation.ppu || presentation.mode2_horizon;
        if(new_tick || interpolated || background_interpolated_) {
            auto scene=current;
            if(presentation.ppu) scene.ppu=presentation.ppu;
            auto background_inputs=inputs;background_inputs.backgrounds_only=true;
            background_inputs.bg2_scroll_fraction=presentation.bg2_fraction;
            background_inputs.bg3_scroll_fraction=presentation.bg3_fraction;
            background_inputs.mode2_horizon=presentation.mode2_horizon;
            // Level horizon: the art row the cartridge puts on the true horizon,
            // i.e. its art horizon with the pitch shift (-tan(pitch) x 256 rows)
            // removed. Cinematics move the art without matching the camera
            // (Corneria's launch exit and clear), so take the most common value
            // over near-level ticks of this background rather than any one tick.
            if(inputs.landscape_world && scene.background_landscape && scene.ppu) {
                const float art=vr::landscape_art_horizon(*scene.ppu,float(scene.source_vanishing_point[1])+16.F);
                const double pitch=double(int16_t(scene.camera.pitch))*(2*3.14159265358979323846/65536.);
                if(level_background_!=scene.background_id || !level_horizon_) {
                    horizon_votes_.clear();level_background_=scene.background_id;
                    level_horizon_=float(art+std::tan(pitch)*256.);
                }
                if(new_tick && std::abs(int(int16_t(scene.camera.pitch)))<=1024) {
                    const int key=int(std::lround((art+std::tan(pitch)*256.)*4.));
                    const auto votes=++horizon_votes_[key];
                    if(votes>=horizon_votes_[int(std::lround(*level_horizon_*4.F))]) level_horizon_=float(key)/4.F;
                }
                background_inputs.landscape_horizon=level_horizon_;
                static const bool trace_horizon=std::getenv("STARFOX_TRACE_MODERN")!=nullptr;
                if(trace_horizon && new_tick) std::fprintf(stderr,"horizon-level tick=%llu level=%.2f art=%.2f implied=%.2f pitch=%d camy=%d shadow=%d vp=%d\n",
                    static_cast<unsigned long long>(scene.revision),*level_horizon_,art,float(art+std::tan(pitch)*256.),
                    int(int16_t(scene.camera.pitch)),int(scene.camera.y),int(scene.shadow_height),int(scene.source_vanishing_point[1]));
            }
            auto layers=vr::assemble_frame_layers(scene,background_inputs);
            require(backgrounds_.initialize(device_,uploads_,layers.backgrounds,{},false),backgrounds_.status());
            background_packets_=std::move(layers.backgrounds);
            background_interpolated_=interpolated;
        }
        static const bool trace=std::getenv("STARFOX_TRACE_MODERN")!=nullptr;
        if(trace) std::fprintf(stderr,"modern-frame tick=%llu alpha=%.3f background=%s vp=%d,%d bg2=%d,%d vscroll=%d grid=%d atlas=%u hudrot=%04x mode=%u flags=%s%s%s%s%s%s roll=%d\n",
            static_cast<unsigned long long>(current.revision),alpha,
            presentation.ppu?"interpolated":new_tick?"tick":"retained",
            int(current.source_vanishing_point[0]),int(current.source_vanishing_point[1]),
            current.ppu?int(current.ppu->bg2_scroll_x):0,current.ppu?int(current.ppu->bg2_scroll_y):0,
            int(current.background_vertical_scroll),int(current.landscape_grid_height),unsigned(current.landscape_atlas_origin),
            unsigned(current.cockpit_hud_roll),current.ppu?unsigned(current.ppu->background_mode):0U,
            current.background_star_sphere?"star_sphere ":"",current.background_space_horizon?"space_horizon ":"",
            current.background_unique_space?"unique_space ":"",current.background_landscape?"landscape ":"",
            current.ppu && current.ppu->tunnel_scene?"tunnel ":"",current.background_orbital_planet?"orbital ":"",
            int(int16_t(current.camera.roll)));
        if(trace && new_tick && current.ppu && current.background_landscape) {
            const auto fit=mode2_horizon_fit(*current.ppu);
            std::fprintf(stderr,"landscape world=%d vofs=%d hofs=%d flow=%d bg=%04x mode=%u shadow=%d dots=%d camera=%d,%d,%d pyr=%u,%u,%u scroll=%d,%d horizon=%.2f,%.4f grid=%d origin=%u\n",
                int(landscape_world),int(current.ppu->bg2_scanline_scroll_enabled),int(current.ppu->bg2_horizontal_offsets_enabled),
                int(current.flow),unsigned(current.background_id),unsigned(current.ppu->background_mode),int(current.shadow_height),int(current.dots_mode),
                int(current.camera.x),int(current.camera.y),int(current.camera.z),
                unsigned(current.camera.pitch),unsigned(current.camera.yaw),unsigned(current.camera.roll),
                int(current.ppu->bg2_scroll_x),int(current.ppu->bg2_scroll_y),fit?(*fit)[0]:0.,fit?(*fit)[1]:0.,
                int(current.landscape_grid_height),unsigned(current.landscape_atlas_origin));
        }
        if(trace && new_tick && current.ppu && current.ppu->tunnel_scene && current.ppu->bg2_scanline_scroll_enabled) {
            std::fprintf(stderr,"tunnel-table camz=%d camx=%d scrollx=%d hofs=%d:%d flips=",int(current.camera.z),int(current.camera.x),
                int(current.ppu->bg2_scroll_x),int(current.ppu->bg2_horizontal_offsets_enabled),int(current.ppu->bg2_horizontal_offsets[60]));
            for(std::size_t row=1;row<224;++row)
                if(current.ppu->bg2_scanline_scroll_y[row]!=current.ppu->bg2_scanline_scroll_y[row-1])
                    std::fprintf(stderr,"%zu:%d,",row,int(current.ppu->bg2_scanline_scroll_y[row]));
            std::fprintf(stderr,"\n");
        }
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
        // Landscape placement at display rate: roll as geometry, camera height
        // above the ground, and the camera's world X/Z for the ground plane.
        struct {double roll{},pitch{},height{1},x{},z{};vr::Matrix4 rotation{};} landscape;
        {
            const bool continuous=!timing::camera_transform_is_discontinuous(previous.camera,current.camera);
            const double a=continuous?alpha:1.;
            const double roll_delta=double(int16_t(uint16_t(current.camera.roll-previous.camera.roll)));
            landscape.roll=roll_sign*(double(previous.camera.roll)+roll_delta*a)*(2*3.14159265358979323846/65536.);
            const double pitch_delta=double(int16_t(uint16_t(current.camera.pitch-previous.camera.pitch)));
            double pitch=double(int16_t(previous.camera.pitch))+pitch_delta*a;
            pitch-=65536.*std::floor((pitch+32768.)/65536.);
            landscape.pitch=pitch_sign*pitch*(2*3.14159265358979323846/65536.);
            // The source view rotation the objects are drawn with (yaw
            // included), interpolated like the grid, in model axes.
            const auto view=vr::landscape_scene_view(continuous?previous:current,current,a);
            constexpr int sign[]{1,-1,-1};
            landscape.rotation[15]=1;
            for(unsigned column=0;column<3;++column) for(unsigned row=0;row<3;++row)
                landscape.rotation[column*4+row]=float(view[column*3+row])*float(sign[column]*sign[row])/32768.F;
            landscape.height=std::lerp(double(previous.shadow_height)-previous.camera.y,
                double(current.shadow_height)-current.camera.y,a);
            const auto blend=[&](int32_t before,int32_t now) {
                return double(before)+double(int16_t(uint16_t(now-before)))*a;
            };
            landscape.x=continuous?blend(previous.camera.x,current.camera.x):double(current.camera.x);
            landscape.z=continuous?blend(previous.camera.z,current.camera.z):double(current.camera.z);
        }
        // Space backgrounds: the real camera roll as geometry around the view
        // axis (the vanishing point), like the planet landscape, instead of
        // the cartridge's 20 Hz Mode 2 column shear.
        if(inputs.space_roll_geometry && vr::space_background_roll(current)) {
            const auto roll=vr::landscape_world_model(landscape.roll,0,256.);
            for(auto& model:background_models) model=vr::multiply_matrix(roll,model);
        }
        // The 3D tunnel corridor follows the display-rate camera Z.
        model_index=0;
        for(const auto& packet:background_packets_) {
            if(packet.geometry.vertex_view().empty() && packet.geometry.line_view().empty()) continue;
            if(vr::is_landscape_world(packet)) {
                background_models.at(model_index)=vr::landscape_dome_view_model(landscape.rotation,landscape.height);
                // Sky replacement mapping for this frame: horizon row and
                // centre column, 32x fixed point (camera.effects.z).
                const auto texels=packet.geometry.texel_view();
                const float horizon=std::bit_cast<float>(texels[272+16384]),centre=std::bit_cast<float>(texels[272+16384+1]);
                texture_mapping_=uint32_t(std::clamp(horizon*32.F,0.F,65535.F))
                    |(uint32_t(std::clamp(std::fmod(centre,1024.F)+1024.F,0.F,2047.F)*32.F)<<16);
                if(texture_dump_ && texels.size()>=272+16384+6+112)
                    texture_dump_->landscape(current.background_id,*current.ppu,horizon,texels.subspan(272+16384+6,112));
            }
            if(vr::is_landscape_ground(packet))
                background_models.at(model_index)=vr::landscape_ground_view_model(landscape.rotation,landscape.height,landscape.x,landscape.z);
            if(vr::is_tunnel_corridor(packet)) {
                const bool continuous=!timing::camera_transform_is_discontinuous(previous.camera,current.camera);
                const auto blend=[&](int32_t before,int32_t now) {
                    const double from=continuous?double(before):double(now);
                    return from+double(int16_t(uint16_t(now-int32_t(from))))*(continuous?alpha:1.);
                };
                background_models.at(model_index)=vr::tunnel_corridor_model(blend(previous.camera.z,current.camera.z),
                    blend(previous.camera.x,current.camera.x));
            }
            ++model_index;
        }
        require(backgrounds_.update_models(background_models),"MODERN background motion update failed");
        // Replacement landscape artwork, loaded once per background.
        if(landscape_world && !texture_directory_.empty() && texture_background_!=current.background_id) {
            texture_background_=current.background_id;
            texture_packets_.clear();
            const auto& ppu=*current.ppu;
            const float art_width=float(((ppu.bg2_screen_size&1U)?64U:32U)*(ppu.bg2_tile_size_16?16U:8U));
            char name[32];
            for(const auto kind:{vr::LandscapeTexture::sky,vr::LandscapeTexture::ground}) {
                std::snprintf(name,sizeof name,"bg_%04x_%s.png",unsigned(current.background_id),
                    kind==vr::LandscapeTexture::sky?"sky":"ground");
                const auto path=texture_directory_+name;
                auto image=load_rgba(path);
                std::fprintf(stderr,"MODERN textures: %s %s\n",path.c_str(),image?"loaded":"(not present)");
                if(image) texture_packets_.push_back(vr::landscape_texture_packet(kind,image->pixels,image->width,image->height,
                    art_width,112.F,2048.F,inputs.srgb));
            }
            require(textures_.initialize(device_,uploads_,texture_packets_,{},false),textures_.status());
        }
        if(!landscape_world && texture_background_) {
            texture_background_.reset();texture_packets_.clear();
            require(textures_.initialize(device_,uploads_,texture_packets_,{},false),textures_.status());
        }
        if(!texture_packets_.empty()) {
            std::vector<vr::Matrix4> models;
            for(const auto& packet:texture_packets_)
                models.push_back(vr::is_landscape_texture(packet,vr::LandscapeTexture::ground)
                    ?vr::landscape_ground_view_model(landscape.rotation,landscape.height,landscape.x,landscape.z)
                    :vr::landscape_dome_view_model(landscape.rotation,landscape.height));
            require(textures_.update_models(models),"MODERN landscape texture motion update failed");
        }
        // MHUD.MC (INIT_STRATS enables it only inside the cockpit): the desktop
        // draws it per frame with interpolated roll, above world models and
        // below the SNES OBJ HUD.
        const bool playing=current.flow==simulation::GameFlowState::gameplay || current.flow==simulation::GameFlowState::training;
        cockpit_active_=playing && inputs.trigonometry && (current.cockpit_hud_roll&0x8000U)!=0;
        if(cockpit_active_) {
            auto hud=vr::source_cockpit_hud_packet(*inputs.trigonometry,
                timing::interpolate_cockpit_roll(previous.cockpit_hud_roll,current.cockpit_hud_roll,alpha),
                current.cockpit_hud_colour,current.cockpit_hud_flags,current.ppu->cgram,current.display_brightness,inputs.srgb);
            hud.model=vr::source_ui_layer_matrix(float(current.source_vanishing_point[0]),
                float(current.source_vanishing_point[1]),false,true).value();
            require(cockpit_.initialize(device_,uploads_,std::span(&hud,1),{},false),cockpit_.status());
        }
        // The first-person reticle as geometry, moving at display rate.
        reticle_active_=false;
        if(inputs.vector_reticle && previous.ppu) {
            auto reticle=vr::source_vector_reticle_packet(*previous.ppu,*current.ppu,alpha,current.display_brightness,inputs.srgb);
            reticle_active_=!reticle.geometry.vertex_view().empty();
            if(reticle_active_) {
                reticle.model=vr::source_ui_layer_matrix(float(current.source_vanishing_point[0]),
                    float(current.source_vanishing_point[1]),false).value();
                require(reticle_.initialize(device_,uploads_,std::span(&reticle,1),{},false),reticle_.status());
            }
        }
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
        // Objects that moved in the world since the previous tick.
        std::vector<uint16_t> moving;
        for(const auto& object:current.objects) {
            const auto before=std::find_if(previous.objects.begin(),previous.objects.end(),
                [&](const auto& candidate){return candidate.handle==object.handle;});
            if(before!=previous.objects.end() && (before->object.world_x!=object.object.world_x
                || before->object.world_y!=object.object.world_y || before->object.world_z!=object.object.world_z))
                moving.push_back(uint16_t(object.handle));
        }
        require(world_.initialize(device_,command,world,moving),world_.status());
        require(world_.record_compute(command),world_.status());
        status_="MODERN frame prepared";return true;
    } catch(const std::exception& error) {uploads_.clear();status_=error.what();return false;}
}
vr::EyeCamera reversed_depth_camera(vr::EyeCamera camera) noexcept {
    auto& m=camera.projection;
    // Infinite perspective (clip w = -z): write clip z = near instead of
    // -z - near, so normalized depth is near/distance (1 at the near plane).
    if(m[11]==-1 && m[15]==0 && m[10]==-1) {m[10]=0;m[14]=-m[14];}
    return camera;
}
bool SdlFrameRenderer::record(SDL_GPUCommandBuffer* command,SDL_GPURenderPass* pass,const vr::EyeCamera& source_camera) {
    auto camera=reversed_depth_camera(source_camera);
    if(filter_2d_) camera.effects[3]|=1U;
    // Output pixels per source pixel, 4.4 fixed point (bits 8-19): ground dots.
    camera.effects[3]|=uint32_t(std::clamp(source_pixel_*16.F,16.F,4095.F))<<8;
    const auto fail=[&](const char* layer) {status_=std::string("MODERN ")+layer+" recording failed: "+pipelines_.status();return false;};
    if(!stars_.record(command,pass,pipelines_,camera)) return fail("star");
    if(!backgrounds_.record(command,pass,pipelines_,camera)) return fail("background");
    if(!texture_packets_.empty()) {
        auto mapped=camera;mapped.effects[2]=texture_mapping_;
        if(!textures_.record(command,pass,pipelines_,mapped)) return fail("landscape texture");
    }
    if(!tunnel_.record(command,pass,pipelines_,camera)) return fail("tunnel surround");
    if(!world_.record(command,pass,pipelines_,camera)) return fail("world");
    if(cockpit_active_ && !cockpit_.record(command,pass,pipelines_,camera)) return fail("cockpit HUD");
    if(circle_active_ && !circle_over_hud_ && !circle_.record(command,pass,pipelines_,camera)) return fail("circle");
    if(!sprites_.record(command,pass,pipelines_,camera)) return fail("sprite");
    if(reticle_active_ && !reticle_.record(command,pass,pipelines_,camera)) return fail("reticle");
    if(circle_active_ && circle_over_hud_ && !circle_.record(command,pass,pipelines_,camera)) return fail("circle");
    if(shutter_active_) {
        // Head-locked normalized-device shutter: identity view and projection.
        const vr::Matrix4 identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        if(!shutter_.record(command,pass,pipelines_,vr::EyeCamera{identity,identity})) return fail("shutter");
    }
    return true;
}
}
