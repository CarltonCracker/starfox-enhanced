// MODERN: the VR scene shader under SDL_GPU's binding model.
// Vertex stage: storage buffers t#/space0, uniform buffers b#/space1.
// Fragment stage: storage buffers t#/space2, uniform buffers b#/space3.
// SPIR-V takes set = space, binding = register; DXIL uses the registers as-is.
// The camera arrives as a uniform block (SDL_PushGPU*UniformData, slot 0)
// instead of a Vulkan push constant. The caller flips projection Y because
// SDL_GPU presents a Y-up clip space on every backend.
// scene.hlsl declares those bindings for STARFOX_SDL_SCENE_VERTEX/FRAGMENT
// (shared with the calibrated DisplayXR renderer).
#if defined(STARFOX_SDL_FRAGMENT)
#define STARFOX_SDL_SCENE_FRAGMENT
#else
#define STARFOX_SDL_SCENE_VERTEX
#endif
#include "../../vr/shaders/scene.hlsl"

// SDL_GPU's D3D12 backend names every vertex attribute TEXCOORD<location>.
struct SdlVertex {
    [[vk::location(0)]] float3 position : TEXCOORD0;
    [[vk::location(1)]] float4 color : TEXCOORD1;
    [[vk::location(2)]] float4 odd_color : TEXCOORD2;
    [[vk::location(3)]] uint dither_scale : TEXCOORD3;
    [[vk::location(4)]] float3 visibility_a : TEXCOORD4;
    [[vk::location(5)]] float3 visibility_b : TEXCOORD5;
    [[vk::location(6)]] float3 visibility_c : TEXCOORD6;
    [[vk::location(7)]] uint visibility_enabled : TEXCOORD7;
    [[vk::location(8)]] float3 group_a : TEXCOORD8;
    [[vk::location(9)]] float3 group_b : TEXCOORD9;
    [[vk::location(10)]] float3 group_c : TEXCOORD10;
    [[vk::location(11)]] uint group_enabled : TEXCOORD11;
    [[vk::location(12)]] float2 uv : TEXCOORD12;
    [[vk::location(13)]] uint4 texture : TEXCOORD13;
    [[vk::location(14)]] float2 billboard : TEXCOORD14;
};
Vertex sdl_vertex(SdlVertex input) {
    Vertex output;
    output.position=input.position;output.color=input.color;output.odd_color=input.odd_color;
    output.dither_scale=input.dither_scale;
    output.visibility_a=input.visibility_a;output.visibility_b=input.visibility_b;output.visibility_c=input.visibility_c;
    output.visibility_enabled=input.visibility_enabled;
    output.group_a=input.group_a;output.group_b=input.group_b;output.group_c=input.group_c;
    output.group_enabled=input.group_enabled;
    output.uv=input.uv;output.texture=input.texture;output.billboard=input.billboard;
    return output;
}
Fragment sdl_vertex_textured_main(SdlVertex input) {return vertex_textured_main(sdl_vertex(input));}
