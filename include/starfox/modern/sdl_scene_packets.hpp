#pragma once
#include "starfox/vr/draw_packet.hpp"
#include "starfox/vr/scene_camera.hpp"
#include <SDL3/SDL_gpu.h>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace starfox::modern {
// Collects buffer writes for one frame and flushes them through a single
// persistent (cycled) transfer buffer and one copy pass. Per-write transfer
// buffers are committed resources on D3D12 and cost milliseconds per frame.
class SdlUploadBatch {
public:
    SdlUploadBatch()=default;
    ~SdlUploadBatch();
    SdlUploadBatch(const SdlUploadBatch&)=delete;
    SdlUploadBatch& operator=(const SdlUploadBatch&)=delete;
    // Bytes are copied immediately; the source may change afterwards.
    void add(SDL_GPUBuffer*,uint32_t offset,const void* bytes,uint32_t size);
    // One copy pass on `command` (no pass may be open). No-op when empty.
    bool flush(SDL_GPUDevice*,SDL_GPUCommandBuffer*);
    void clear() noexcept {writes_.clear();staging_.clear();}
    std::size_t bytes() const noexcept {return staging_.size();}
    void release() noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    struct Write {SDL_GPUBuffer* buffer{};uint32_t offset{},size{},staging{};};
    std::vector<Write> writes_;
    std::vector<unsigned char> staging_;
    SDL_GPUDevice* device_{};SDL_GPUTransferBuffer* transfer_{};uint32_t capacity_{};
    std::string status_;
};
// SDL_GPU equivalent of vr::SceneTopology/SceneBlend (vulkan_scene_pipeline.hpp).
enum class SceneTopology {triangles,lines};
enum class SceneBlend {opaque,alpha};
// Graphics pipelines for the shared scene shader, created lazily per state and
// retained for one device and target format pair. Release before the device.
class SdlScenePipelines {
public:
    SdlScenePipelines()=default;
    ~SdlScenePipelines();
    SdlScenePipelines(const SdlScenePipelines&)=delete;
    SdlScenePipelines& operator=(const SdlScenePipelines&)=delete;
    bool initialize(SDL_GPUDevice*,SDL_GPUTextureFormat colour,SDL_GPUTextureFormat depth);
    SDL_GPUGraphicsPipeline* get(SceneTopology,SceneBlend,bool depth_test,bool depth_write);
    void release() noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    SDL_GPUDevice* device_{};
    SDL_GPUShader* vertex_{};SDL_GPUShader* fragment_{};
    SDL_GPUTextureFormat colour_{},depth_{};
    std::map<unsigned,SDL_GPUGraphicsPipeline*> pipelines_;
    std::string status_{"Scene pipelines not initialized"};
};
// SDL_GPU equivalent of vr::VulkanDrawPackets: immutable per-packet geometry
// and texel buffers with replaceable model transforms. Keyed packets reuse
// unchanged geometry across frames, exactly as the Vulkan path does.
class SdlDrawPackets {
public:
    SdlDrawPackets();
    ~SdlDrawPackets();
    SdlDrawPackets(const SdlDrawPackets&)=delete;
    SdlDrawPackets& operator=(const SdlDrawPackets&)=delete;
    // Queues new geometry on `uploads`; flush it on the command buffer before
    // the render pass that draws these packets.
    bool initialize(SDL_GPUDevice*,SdlUploadBatch& uploads,std::span<const vr::DrawPacket>,
        std::span<const uint32_t> object_keys={},bool depth_test=true,bool depth_write=true);
    // Inside a render pass whose targets match `pipelines`' formats.
    bool record(SDL_GPUCommandBuffer*,SDL_GPURenderPass*,SdlScenePipelines&,const vr::EyeCamera&) const;
    // Contiguous range of uploaded (nonempty) items, so resident compute
    // models can interleave without changing painter/pass order.
    bool record_range(SDL_GPUCommandBuffer*,SDL_GPURenderPass*,SdlScenePipelines&,const vr::EyeCamera&,
        std::size_t first,std::size_t count) const;
    std::size_t size() const noexcept;
    std::size_t reused_packets() const noexcept;
    std::size_t rejected_packets() const noexcept;
    void release() noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    struct State;
    std::unique_ptr<State> state_;
    std::string status_{"Draw packets not initialized"};
};
// Camera constants for the SDL scene shader: vr::scene_constants with the
// projection's Y negated, since SDL_GPU uses a Y-up clip space everywhere.
vr::SceneConstants sdl_scene_constants(const vr::EyeCamera&) noexcept;
}
