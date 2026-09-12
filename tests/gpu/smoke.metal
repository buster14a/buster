#include <metal_stdlib>
using namespace metal;
kernel void buster_gpu_smoke(device uint *output [[buffer(0)]], uint index [[thread_position_in_grid]])
{
    output[index] = index * 3u + 7u;
}
