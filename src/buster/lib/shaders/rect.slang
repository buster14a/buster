#ifndef BUSTER_RECT_TEXTURE_SLOT_COUNT
#define BUSTER_RECT_TEXTURE_SLOT_COUNT 2
#endif
#ifndef BUSTER_VULKAN
#define BUSTER_VULKAN 0
#endif
#ifndef BUSTER_VULKAN_VERTEX_SSBO
#define BUSTER_VULKAN_VERTEX_SSBO 0
#endif
#ifndef BUSTER_METAL
#define BUSTER_METAL 0
#endif
#ifndef BUSTER_D3D12
#define BUSTER_D3D12 0
#endif

#include "rect_shared.h"

struct VertexOut
{
    float4 position : SV_Position;
    nointerpolation uint texture_index : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float2 pixel_position : TEXCOORD3;
    float2 center : TEXCOORD4;
    float2 half_size : TEXCOORD5;
    float corner_radius : TEXCOORD6;
    float softness : TEXCOORD7;
}

struct GPUDrawPushConstants
{
    RectVertex* vertex_buffer;
    float width;
    float height;
}

struct DrawConstants
{
    float width;
    float height;
}

static const float2 quad_vertices[4] =
{
    float2(-1.0, -1.0),
    float2( 1.0, -1.0),
    float2(-1.0,  1.0),
    float2( 1.0,  1.0),
};

VertexOut rect_vertex(uint vertex_id, RectVertex v, float width, float height)
{
    VertexOut output;
    uint quad_vertex_id = vertex_id % 4;
    float2 extent = v.extent;

    float2 p0 = v.p0;
    float2 p1 = p0 + extent;
    float2 center = (p1 + p0) * 0.5;
    float2 half_size = (p1 - p0) * 0.5;
    float2 pixel_position = quad_vertices[quad_vertex_id] * half_size + center;

#if BUSTER_METAL || BUSTER_D3D12
    output.position = float4(2.0 * pixel_position.x / width - 1.0, 1.0 - 2.0 * pixel_position.y / height, 0.0, 1.0);
#else
    output.position = float4(2.0 * pixel_position.x / width - 1.0, 2.0 * pixel_position.y / height - 1.0, 0.0, 1.0);
#endif

    float2 uv0 = v.uv0;
    float2 uv1 = uv0 + v.uv_extent;
    float2 texture_center = (uv1 + uv0) * 0.5;

    output.texture_index = v.texture_index;
    output.color = v.colors[quad_vertex_id];
    output.uv = quad_vertices[quad_vertex_id] * half_size + texture_center;
    output.pixel_position = pixel_position;
    output.center = center;
    output.half_size = half_size;
    output.corner_radius = v.corner_radius;
    output.softness = v.softness;
    return output;
}

float rounded_rect_sdf(float2 position, float2 center, float2 half_size, float radius)
{
    float2 r2 = float2(radius, radius);
    float2 d2_no_r2 = abs(center - position) - half_size;
    float2 d2 = d2_no_r2 + r2;
    float negative_distance = min(max(d2.x, d2.y), 0.0);
    float positive_distance = length(max(d2, 0.0));
    return negative_distance + positive_distance - radius;
}

#if BUSTER_VULKAN
#if BUSTER_VULKAN_VERTEX_SSBO
// Some Vulkan drivers (notably Qualcomm Adreno) crash compiling buffer_reference
// SPIR-V, so pull vertices from a storage-buffer descriptor instead of a raw
// GPU pointer. Vertices live at set 0, binding 1 (textures stay at binding 0).
[[vk::binding(1, 0)]]
StructuredBuffer<RectVertex> vertices_buffer;

[shader("vertex")]
VertexOut vs_main(uint vertex_id : SV_VulkanVertexID, uniform DrawConstants constants)
{
    return rect_vertex(vertex_id, vertices_buffer[vertex_id], constants.width, constants.height);
}
#else
[shader("vertex")]
VertexOut vs_main(uint vertex_id : SV_VulkanVertexID, uniform GPUDrawPushConstants constants)
{
    return rect_vertex(vertex_id, constants.vertex_buffer[vertex_id], constants.width, constants.height);
}
#endif

[[vk::binding(0, 0)]]
Sampler2D<float4> textures[BUSTER_RECT_TEXTURE_SLOT_COUNT];

[shader("fragment")]
float4 ps_main(VertexOut input) : SV_Target
{
    uint texture_width;
    uint texture_height;
    // Match GLSL's nonuniformEXT(texture_index) at each resource-array use.
    textures[NonUniformResourceIndex(input.texture_index)].GetDimensions(texture_width, texture_height);

    float2 uv = float2(input.uv.x / (float)texture_width, input.uv.y / (float)texture_height);
    float4 sampled = textures[NonUniformResourceIndex(input.texture_index)].Sample(uv);

    float softness = input.softness;
    float softness_padding_scalar = max(0.0, softness * 2.0 - 1.0);
    float distance = rounded_rect_sdf(input.pixel_position, input.center, input.half_size - float2(softness_padding_scalar, softness_padding_scalar), input.corner_radius);
    float sdf_factor = 1.0 - smoothstep(0.0, 2.0 * softness, distance);
    return input.color * sampled * sdf_factor;
}
#endif

#if BUSTER_D3D12
StructuredBuffer<RectVertex> vertices_buffer : register(t0);
ConstantBuffer<DrawConstants> draw_constants : register(b0);
Texture2D<float4> textures[BUSTER_RECT_TEXTURE_SLOT_COUNT] : register(t1);
SamplerState texture_sampler : register(s0);

[shader("vertex")]
VertexOut vs_main(uint vertex_id : SV_VertexID)
{
    return rect_vertex(vertex_id, vertices_buffer[vertex_id], draw_constants.width, draw_constants.height);
}

[shader("fragment")]
float4 ps_main(VertexOut input) : SV_Target
{
    uint texture_width;
    uint texture_height;
    uint texture_index = NonUniformResourceIndex(input.texture_index);
    textures[texture_index].GetDimensions(texture_width, texture_height);

    float2 uv = float2(input.uv.x / (float)texture_width, input.uv.y / (float)texture_height);
    float4 sampled = textures[texture_index].Sample(texture_sampler, uv);

    float softness = input.softness;
    float softness_padding_scalar = max(0.0, softness * 2.0 - 1.0);
    float distance = rounded_rect_sdf(input.pixel_position, input.center, input.half_size - float2(softness_padding_scalar, softness_padding_scalar), input.corner_radius);
    float sdf_factor = 1.0 - smoothstep(0.0, 2.0 * softness, distance);
    return input.color * sampled * sdf_factor;
}
#endif

#if BUSTER_METAL
StructuredBuffer<RectVertex> vertices_buffer : register(t0);
ConstantBuffer<DrawConstants> draw_constants : register(b1);
Texture2D<float4> textures[BUSTER_RECT_TEXTURE_SLOT_COUNT] : register(t0);
SamplerState texture_sampler : register(s0);

[shader("vertex")]
VertexOut rect_vs(uint vertex_id : SV_VertexID)
{
    return rect_vertex(vertex_id, vertices_buffer[vertex_id], draw_constants.width, draw_constants.height);
}

[shader("fragment")]
float4 rect_fs(VertexOut input) : SV_Target
{
    uint texture_width;
    uint texture_height;
    textures[input.texture_index].GetDimensions(texture_width, texture_height);

    float2 uv = float2(input.uv.x / (float)texture_width, input.uv.y / (float)texture_height);
    float4 sampled = textures[input.texture_index].Sample(texture_sampler, uv);

    float softness = input.softness;
    float softness_padding_scalar = max(0.0, softness * 2.0 - 1.0);
    float distance = rounded_rect_sdf(input.pixel_position, input.center, input.half_size - float2(softness_padding_scalar, softness_padding_scalar), input.corner_radius);
    float sdf_factor = 1.0 - smoothstep(0.0, 2.0 * softness, distance);
    return input.color * sampled * sdf_factor;
}
#endif
