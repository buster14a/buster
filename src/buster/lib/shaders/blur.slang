#ifndef BUSTER_BLUR_TEXTURE_SLOT_COUNT
#define BUSTER_BLUR_TEXTURE_SLOT_COUNT 1
#endif
#ifndef BUSTER_VULKAN
#define BUSTER_VULKAN 0
#endif
#ifndef BUSTER_METAL
#define BUSTER_METAL 0
#endif
#ifndef BUSTER_D3D12
#define BUSTER_D3D12 0
#endif

#include "blur_shared.h"

struct BlurVertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float2 blur_triangle[3] =
{
    float2(-1.0, -1.0),
    float2( 3.0, -1.0),
    float2(-1.0,  3.0),
};

#if BUSTER_VULKAN
[shader("vertex")]
BlurVertexOut blur_vs(uint vertex_id : SV_VulkanVertexID)
#else
[shader("vertex")]
BlurVertexOut blur_vs(uint vertex_id : SV_VertexID)
#endif
{
    BlurVertexOut output;
    float2 position = blur_triangle[vertex_id];
    output.position = float4(position, 0.0, 1.0);
    output.uv = position * 0.5 + float2(0.5, 0.5);
#if BUSTER_METAL || BUSTER_D3D12
    output.uv.y = 1.0 - output.uv.y;
#endif
    return output;
}

#if BUSTER_VULKAN
[[vk::binding(0, 0)]]
Sampler2D<float4> blur_source;
[[vk::push_constant]]
BlurConstants blur_constants;
#elif BUSTER_D3D12
Texture2D<float4> blur_source : register(t0);
SamplerState blur_sampler : register(s0);
ConstantBuffer<BlurConstants> blur_constants : register(b0);
#elif BUSTER_METAL
Texture2D<float4> blur_source : register(t0);
SamplerState blur_sampler : register(s0);
ConstantBuffer<BlurConstants> blur_constants : register(b0);
#endif

float4 blur_fs(BlurVertexOut input) : SV_Target
{
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    uint count = 0;
    uint limit = blur_constants.radius;
    if (limit > 32)
    {
        limit = 32;
    }
    for (uint sample_index = 0; sample_index <= 64; sample_index += 1)
    {
        if (sample_index > limit * 2)
        {
            break;
        }
        int offset = (int)sample_index - (int)limit;
        float2 delta = blur_constants.texel_step * (float)offset;
        float2 sample_uv = input.uv + (blur_constants.vertical != 0 ? float2(0.0, delta.y) : float2(delta.x, 0.0));
#if BUSTER_VULKAN
        sum += blur_source.Sample(sample_uv);
#else
        sum += blur_source.Sample(blur_sampler, sample_uv);
#endif
        count += 1;
    }
    return count != 0 ? sum / (float)count : float4(0.0, 0.0, 0.0, 0.0);
}

#if BUSTER_METAL
[shader("fragment")]
float4 blur_fs_metal(BlurVertexOut input) : SV_Target
{
    return blur_fs(input);
}
#endif
