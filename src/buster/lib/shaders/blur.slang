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
    float weight_sum = 0.0;
    uint limit = blur_constants.radius;
    if (limit > BUSTER_BLUR_MAX_RADIUS)
    {
        limit = BUSTER_BLUR_MAX_RADIUS;
    }
    float sigma = max((float)limit * BUSTER_BLUR_SIGMA_SCALE, BUSTER_BLUR_MIN_SIGMA);
    for (int offset = -BUSTER_BLUR_MAX_RADIUS; offset <= BUSTER_BLUR_MAX_RADIUS; offset += 1)
    {
        if (offset < -(int)limit || offset > (int)limit)
        {
            continue;
        }
        float distance = (float)offset;
        float weight = pow(BUSTER_BLUR_EXP_BASE, -(distance * distance) / (2.0 * sigma * sigma));
        float2 delta = blur_constants.texel_step * (float)offset;
        float2 sample_uv = input.uv + (blur_constants.vertical != 0 ? float2(0.0, delta.y) : float2(delta.x, 0.0));
        sample_uv = clamp(sample_uv, float2(0.0, 0.0), float2(1.0, 1.0));
#if BUSTER_VULKAN
        sum += blur_source.Sample(sample_uv) * weight;
#else
        sum += blur_source.Sample(blur_sampler, sample_uv) * weight;
#endif
        weight_sum += weight;
    }

    float4 result = weight_sum > 0.0 ? sum / weight_sum : float4(0.0, 0.0, 0.0, 0.0);
    if (blur_constants.composite != 0)
    {
        float2 pixel_position = input.uv * blur_constants.target_size;
        float2 rect_min = blur_constants.mask_rect.xy;
        float2 rect_max = blur_constants.mask_rect.zw;
        float2 half_size = (rect_max - rect_min) * 0.5;
        float2 center = (rect_max + rect_min) * 0.5;
        float radius = pixel_position.x < center.x ? (pixel_position.y < center.y ? blur_constants.corner_radii.x : blur_constants.corner_radii.y)
                                                    : (pixel_position.y < center.y ? blur_constants.corner_radii.z : blur_constants.corner_radii.w);
        radius = min(max(radius, 0.0), min(half_size.x, half_size.y));
        float2 d2_no_r2 = abs(center - pixel_position) - half_size;
        float2 d2 = d2_no_r2 + float2(radius, radius);
        float negative_distance = min(max(d2.x, d2.y), 0.0);
        float positive_distance = length(max(d2, 0.0));
        float distance = negative_distance + positive_distance - radius;
        if (distance > 0.0)
        {
            discard;
        }
    }
    return result;
}

#if BUSTER_METAL
[shader("fragment")]
float4 blur_fs_metal(BlurVertexOut input) : SV_Target
{
    return blur_fs(input);
}
#endif
