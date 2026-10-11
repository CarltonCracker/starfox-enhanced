// MODERN M1 harness (fork-local): renders the VR world packets (native draw-packet
// route) through SDL_GPU offscreen, with the same game frames and eye-0 camera as
// starfox_vr_scene_route_bench, so captures can be compared pixel for pixel.
// SDL_GPU_DRIVER=direct3d12|vulkan selects the backend.
//
// Usage: starfox_modern_scene_check ROM SYMBOLS LEVEL FRAMES WIDTH HEIGHT
// Captures: STARFOX_BENCH_CAPTURE_DIR + STARFOX_BENCH_CAPTURE_FRAMES=a,b,...
#include "starfox/modern/sdl_frame.hpp"
#include "starfox/render/scaled_text_renderer.hpp"
#include "starfox/vr/eye_camera.hpp"
#include "starfox/vr/game_frame_driver.hpp"
#include "starfox/vr/source_models.hpp"
#include "starfox/audio/spc700_audio.hpp"
#include "starfox/audio/stem_mixer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace starfox;
void require(bool ok,const std::string& what) {if(!ok) throw std::runtime_error(what+(SDL_GetError()[0]?std::string(": ")+SDL_GetError():""));}
}
int main(int argc,char** argv) try {
    if(argc<7) {std::cerr<<"Usage: "<<argv[0]<<" ROM SYMBOLS LEVEL FRAMES WIDTH HEIGHT\n";return 2;}
    const auto rom=assets::RomImage::load(argv[1]);
    const auto symbols=assets::SymbolMap::load(argv[2]);
    const std::string level=argv[3];
    const unsigned frames=unsigned(std::stoul(argv[4]));
    const uint32_t width=uint32_t(std::stoul(argv[5])),height=uint32_t(std::stoul(argv[6]));
    std::vector<unsigned> capture_frames;
    const char* capture_dir=std::getenv("STARFOX_BENCH_CAPTURE_DIR");
    if(const char* list=std::getenv("STARFOX_BENCH_CAPTURE_FRAMES")) {
        std::string text=list;std::size_t start=0;
        while(start<text.size()) {
            const auto end=text.find(',',start);
            capture_frames.push_back(unsigned(std::stoul(text.substr(start,end-start))));
            if(end==std::string::npos) break;start=end+1;
        }
    }
    simulation::GameSimulation game(rom,symbols,level,{},true);
    game.set_timing_mode(simulation::TimingMode::unlocked_20_fps);
    audio::Spc700Audio spc;audio::Msu1Audio msu;std::vector<int16_t> mixed;
    const auto checkpoint=symbols.find("MAPRESTART").at(0);
    unsigned warmup=0;
    while(!game.map().peek_ram_word(checkpoint).value() && warmup<3000) {
        const auto tick=game.tick({});
        audio::render_mixed_tick(spc,msu,tick.audio_port_writes,game.map().take_msu_register_writes(),
            game.music_volume(),game.sfx_volume(),mixed);
        game.synchronize_apu_output_ports(spc.output_ports());++warmup;
    }
    require(warmup<3000,"Live game checkpoint did not start");
    vr::GameSceneHistory history(game,rom,symbols);
    vr::GameFrameDriver driver(game,[&](auto apu,auto writes) {
        audio::render_mixed_tick(spc,msu,apu,writes,game.music_volume(),game.sfx_volume(),mixed);
        return spc.output_ports();
    },&history);
    // compute (default): solids through the span compute route, as VR ships;
    // raster: the native draw-packet route (compute_solids off).
    const bool compute_route=!std::getenv("STARFOX_MODERN_ROUTE") || std::string(std::getenv("STARFOX_MODERN_ROUTE"))!="raster";
    vr::SourceModels models(rom,symbols,true,compute_route);

    require(SDL_Init(SDL_INIT_VIDEO),"SDL_Init");
    auto* device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_DXIL,false,nullptr);
    require(device!=nullptr,"SDL_CreateGPUDevice");
    std::cout<<"SDL GPU driver: "<<SDL_GetGPUDeviceDriver(device)<<" | level "<<level<<" | "<<width<<'x'<<height<<'\n';
    constexpr auto colour_format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    const auto depth_format=SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)?SDL_GPU_TEXTUREFORMAT_D32_FLOAT:SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    SDL_GPUTextureCreateInfo colour_info{};
    colour_info.type=SDL_GPU_TEXTURETYPE_2D;colour_info.format=colour_format;
    colour_info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;colour_info.width=width;colour_info.height=height;
    colour_info.layer_count_or_depth=1;colour_info.num_levels=1;
    auto* colour=SDL_CreateGPUTexture(device,&colour_info);require(colour!=nullptr,"Create colour target");
    auto depth_info=colour_info;depth_info.format=depth_format;depth_info.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    auto* depth=SDL_CreateGPUTexture(device,&depth_info);require(depth!=nullptr,"Create depth target");
    SDL_GPUTransferBufferCreateInfo download_info{};
    download_info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;download_info.size=width*height*4;
    auto* download=SDL_CreateGPUTransferBuffer(device,&download_info);require(download!=nullptr,"Create download buffer");
    modern::SdlScenePipelines pipelines;require(pipelines.initialize(device,colour_format,depth_format),pipelines.status());
    modern::SdlDrawPackets scene;
    modern::SdlUploadBatch uploads;
    modern::SdlSourceScene source_scene;
    // Frame mode (STARFOX_MODERN_CAMERA=source): MODERN's whole frame, every
    // VR layer through the SNES camera.
    modern::SdlFrameRenderer frame_renderer;
    render::ScaledTextRenderer dialogue_layout(rom,symbols);
    vr::FrameLayerInputs layer_inputs;
    layer_inputs.rom=&rom;layer_inputs.symbols=&symbols;layer_inputs.dialogue_layout=&dialogue_layout;
    layer_inputs.srgb=true;layer_inputs.ex_experience=game.experience()==simulation::Experience::starfox_ex;
    std::size_t compute_models=0,unsupported_models=0;
    require(frame_renderer.initialize(device,colour_format,depth_format),frame_renderer.status());
    const auto cameras=vr::sbs_eye_cameras(1.75F,float(width)/float(height),.064F,4.F,.05F);
    require(cameras.has_value(),"Invalid stereo cameras");
    const bool source_camera=std::getenv("STARFOX_MODERN_CAMERA") && std::string(std::getenv("STARFOX_MODERN_CAMERA"))=="source";
    const auto eye_camera=(*cameras)[0]; // Same eye-0 camera as starfox_vr_scene_route_bench.
    std::vector<double> frame_ms,record_ms,wait_ms;std::size_t rejected=0,frames_drawn=0;
    for(unsigned frame=0;frame<frames;++frame) {
        driver.advance(XrTime(frame)*50'000'000,{},true);
        const auto previous=history.previous(),current=history.current();
        if(!source_camera && current->flow!=simulation::GameFlowState::gameplay) continue;
        auto packets=models.assemble_world_interpolated(*previous,*current,.5,true,true);
        require(packets.pending.empty(),"Model assembly pending");
        // source camera: the SNES-faithful flat camera (vr::source_panel_camera),
        // as MODERN's desktop view will use; no VR seat setback.
        if(!source_camera) for(std::size_t i=0;i<packets.packets.size();++i)  // Live gameplay setback (application.cpp).
            if((packets.handles[i]&0xffffU)!=0 && packets.handles[i]<0x20000U) packets.packets[i].model[14]-=.25F;
        const auto start=std::chrono::steady_clock::now();
        auto* command=SDL_AcquireGPUCommandBuffer(device);require(command!=nullptr,"Acquire command buffer");
        const bool initialized=source_camera?frame_renderer.prepare(command,*previous,*current,.5,packets,layer_inputs)
            :compute_route?source_scene.initialize(device,command,packets)
            :(scene.initialize(device,uploads,packets.packets,packets.handles) && uploads.flush(device,command));
        if(!initialized) {
            if(++rejected<=5) std::cout<<"rejected frame="<<frame<<": "<<(source_camera?frame_renderer.status():compute_route?source_scene.status():scene.status())<<'\n';
            SDL_CancelGPUCommandBuffer(command);continue;
        }
        if(source_camera) {
            compute_models+=frame_renderer.world().compute_models();unsupported_models+=frame_renderer.world().unsupported_models();
        } else if(compute_route) {
            require(source_scene.record_compute(command),"MODERN source compute: "+source_scene.status());
            compute_models+=source_scene.compute_models();unsupported_models+=source_scene.unsupported_models();
        }
        SDL_GPUColorTargetInfo target{};target.texture=colour;target.load_op=SDL_GPU_LOADOP_CLEAR;
        target.store_op=SDL_GPU_STOREOP_STORE;target.clear_color={0,0,0,1};
        if(source_camera) {
            const auto clear=vr::frame_clear_colour(*current,true);
            target.clear_color={clear[0],clear[1],clear[2],clear[3]};
        }
        SDL_GPUDepthStencilTargetInfo depth_target{};depth_target.texture=depth;depth_target.clear_depth=source_camera?modern::SdlFrameRenderer::clear_depth():1.F;
        depth_target.load_op=SDL_GPU_LOADOP_CLEAR;depth_target.store_op=SDL_GPU_STOREOP_DONT_CARE;
        depth_target.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth_target.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        auto* pass=SDL_BeginGPURenderPass(command,&target,1,&depth_target);require(pass!=nullptr,"Begin render pass");
        const auto camera=source_camera?vr::source_panel_camera(*current):eye_camera;
        const bool recorded=source_camera?frame_renderer.record(command,pass,camera)
            :compute_route?source_scene.record(command,pass,pipelines,camera):scene.record(command,pass,pipelines,camera);
        SDL_EndGPURenderPass(pass);
        require(recorded,"Record MODERN scene: "+(source_camera?frame_renderer.status():pipelines.status()));
        const bool capture=capture_dir && std::find(capture_frames.begin(),capture_frames.end(),frame)!=capture_frames.end();
        if(capture) {
            auto* copy=SDL_BeginGPUCopyPass(command);
            SDL_GPUTextureRegion region{};region.texture=colour;region.w=width;region.h=height;region.d=1;
            SDL_GPUTextureTransferInfo destination{download,0,width,height};
            SDL_DownloadFromGPUTexture(copy,&region,&destination);
            SDL_EndGPUCopyPass(copy);
        }
        const auto recorded_at=std::chrono::steady_clock::now();
        auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);require(fence!=nullptr,"Submit");
        require(SDL_WaitForGPUFences(device,true,&fence,1),"Wait for fence");
        SDL_ReleaseGPUFence(device,fence);
        const auto waited_at=std::chrono::steady_clock::now();
        frame_ms.push_back(std::chrono::duration<double,std::milli>(waited_at-start).count());
        record_ms.push_back(std::chrono::duration<double,std::milli>(recorded_at-start).count());
        wait_ms.push_back(std::chrono::duration<double,std::milli>(waited_at-recorded_at).count());
        ++frames_drawn;
        if(capture) {
            const auto* pixels=static_cast<const unsigned char*>(SDL_MapGPUTransferBuffer(device,download,false));
            require(pixels!=nullptr,"Map download");
            const std::string path=std::string(capture_dir)+"/"+level+"-"+std::to_string(frame)+"-sdl-"+(source_camera?"flat-":"")+(compute_route?"compute-":"raster-")+SDL_GetGPUDeviceDriver(device)+".bmp";
            std::ofstream out(path,std::ios::binary);require(bool(out),"Cannot write "+path);
            const uint32_t row=width*3,padded=(row+3)&~3U;
            unsigned char header[54]{'B','M'};
            const auto put=[&](unsigned offset,uint32_t value){for(unsigned i=0;i<4;++i) header[offset+i]=uint8_t(value>>(8*i));};
            put(2,54+padded*height);put(10,54);put(14,40);put(18,width);put(22,height);header[26]=1;header[28]=24;put(34,padded*height);
            out.write(reinterpret_cast<const char*>(header),54);
            std::vector<unsigned char> line(padded);
            for(uint32_t y=height;y-->0;) {
                for(uint32_t x=0;x<width;++x) {const auto* p=pixels+(std::size_t(y)*width+x)*4;line[x*3]=p[2];line[x*3+1]=p[1];line[x*3+2]=p[0];}
                out.write(reinterpret_cast<const char*>(line.data()),padded);
            }
            SDL_UnmapGPUTransferBuffer(device,download);
        }
    }
    std::sort(frame_ms.begin(),frame_ms.end());
    double sum=0;for(auto v:frame_ms) sum+=v;
    if(!frame_ms.empty()) std::cout<<"frames="<<frames_drawn<<" rejected="<<rejected<<" compute_models/frame="<<(frames_drawn?double(compute_models)/double(frames_drawn):0.)
        <<" unsupported(warp/axis) total="<<unsupported_models<<" upload+draw+wait ms: mean="<<sum/double(frame_ms.size())
        <<" p50="<<frame_ms[frame_ms.size()/2]<<" p95="<<frame_ms[std::min(frame_ms.size()-1,frame_ms.size()*95/100)]<<'\n';
    for(auto* list:{&record_ms,&wait_ms}) std::sort(list->begin(),list->end());
    const auto mean=[](const std::vector<double>& v){double t=0;for(auto x:v) t+=x;return v.empty()?0.:t/double(v.size());};
    if(!record_ms.empty()) std::cout<<"  cpu record (uploads+passes) ms: mean="<<mean(record_ms)<<" p50="<<record_ms[record_ms.size()/2]
        <<" | submit-to-fence ms: mean="<<mean(wait_ms)<<" p50="<<wait_ms[wait_ms.size()/2]<<'\n';
    frame_renderer.release();pipelines.release();scene.release();source_scene.release();
    SDL_ReleaseGPUTransferBuffer(device,download);SDL_ReleaseGPUTexture(device,depth);SDL_ReleaseGPUTexture(device,colour);
    SDL_DestroyGPUDevice(device);SDL_Quit();
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"modern scene check failed: "<<error.what()<<'\n';return 1;
}
