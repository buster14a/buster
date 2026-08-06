#ifndef BUSTER_BLUR_SHARED_H
#define BUSTER_BLUR_SHARED_H

// The Gaussian policy is intentionally explicit and mirrored by the CPU
// reference path: sigma is max(radius * 0.5, 1.0), and each sample weight is
// e^(-(offset * offset) / (2 * sigma * sigma)).
#define BUSTER_BLUR_MAX_RADIUS 32
#define BUSTER_BLUR_SIGMA_SCALE 0.5
#define BUSTER_BLUR_MIN_SIGMA 1.0
#define BUSTER_BLUR_EXP_BASE 2.7182818

struct BlurConstants
{
    float2 texel_step;
    uint radius;
    uint vertical;
    float4 mask_rect;
    float4 corner_radii;
    float2 target_size;
    uint composite;
    uint reserved;
};

#endif /* BUSTER_BLUR_SHARED_H */
