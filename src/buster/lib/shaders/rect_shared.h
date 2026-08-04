#ifndef BUSTER_RECT_SHARED_H
#define BUSTER_RECT_SHARED_H

struct RectVertex
{
    float2 p0;
    float2 uv0;
    float2 extent;
    float corner_radius;
    float softness;
    float4 colors[4];
    uint texture_index;
    uint reserved[1];
    float2 uv_extent;
};

#endif /* BUSTER_RECT_SHARED_H */
