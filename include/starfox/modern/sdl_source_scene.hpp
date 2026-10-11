#pragma once
#include "starfox/modern/sdl_scene_packets.hpp"
#include "starfox/vr/source_models.hpp"
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace starfox::modern {
// The five ordinary source compute stages (vr::SourceComputeStage 0..4:
// spans, continuous clip, continuous projection, continuous visibility, BSP)
// as SDL_GPU compute pipelines from the shared portable shaders. Lazy; retained
// per device. Release before the device.
class SdlSourceStages {
public:
    SdlSourceStages()=default;
    ~SdlSourceStages();
    SdlSourceStages(const SdlSourceStages&)=delete;
    SdlSourceStages& operator=(const SdlSourceStages&)=delete;
    SDL_GPUComputePipeline* get(SDL_GPUDevice*,unsigned stage);
    void release() noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    SDL_GPUDevice* device_{};
    std::array<SDL_GPUComputePipeline*,5> pipelines_{};
    std::string status_{"Source stages not initialized"};
};
// SDL_GPU counterpart of vr::VulkanSourceModel for ordinary and span models.
// SDL_GPU binds whole buffers, so every arena region a stage touches is its
// own buffer; after the stages run, their outputs are copied into one arena
// buffer laid out exactly like VR's, which the graphics pass reads as texels.
// Warp and axis models are not supported yet (initialize() rejects them).
class SdlSourceModel {
public:
    SdlSourceModel();
    ~SdlSourceModel();
    SdlSourceModel(const SdlSourceModel&)=delete;
    SdlSourceModel& operator=(const SdlSourceModel&)=delete;
    // Queues uploads on `uploads`; flush before record_compute.
    bool initialize(SDL_GPUDevice*,SdlUploadBatch& uploads,const vr::SourceSpanModel&,float units);
    // Same-shape update: queues only changed inputs. False if incompatible.
    bool update(SdlUploadBatch& uploads,const vr::SourceSpanModel&);
    // Compute passes; skipped when no input changed. Returns whether the
    // arena needs publish_outputs() this frame.
    bool record_compute(SDL_GPUCommandBuffer*,SdlSourceStages&,bool& publish);
    // Inside a copy pass: copy this frame's stage outputs into the arena.
    void publish_outputs(SDL_GPUCopyPass*) const;
    bool record_graphics(SDL_GPUCommandBuffer*,SDL_GPURenderPass*,SdlScenePipelines&,const vr::EyeCamera&) const;
    float units() const noexcept;
    bool unclipped() const noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    struct State;
    std::unique_ptr<State> state_;
    std::string status_{"Source model not initialized"};
};
// SDL_GPU counterpart of vr::VulkanSourceScene: one ordered world pass where
// compute models (SourceModelPackets::compute_models) interleave with the
// remaining draw packets in painter order.
class SdlSourceScene {
public:
    SdlSourceScene();
    ~SdlSourceScene();
    SdlSourceScene(const SdlSourceScene&)=delete;
    SdlSourceScene& operator=(const SdlSourceScene&)=delete;
    // Uploads on `command`; then call record_compute on the same command
    // before the render pass that calls record().
    // `moving`: source object handles whose world position changed since the
    // previous tick (ships, shots). They always draw depth-tested, even when
    // large, so a ship leaving a hangar stays behind its walls.
    bool initialize(SDL_GPUDevice*,SDL_GPUCommandBuffer*,const vr::SourceModelPackets&,
        std::span<const uint16_t> moving={});
    bool record_compute(SDL_GPUCommandBuffer*);
    bool record(SDL_GPUCommandBuffer*,SDL_GPURenderPass*,SdlScenePipelines&,const vr::EyeCamera&) const;
    // Compute models this frame that the SDL path cannot draw yet (warp/axis).
    std::size_t unsupported_models() const noexcept {return unsupported_;}
    std::size_t compute_models() const noexcept {return compute_models_;}
    void release() noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    struct State;
    std::unique_ptr<State> state_;
    SdlSourceStages stages_;
    SdlUploadBatch uploads_;
    std::size_t unsupported_{},compute_models_{};
    std::string status_{"Source scene not initialized"};
};
}
