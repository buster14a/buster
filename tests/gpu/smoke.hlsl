// Four independent writes; shared by the Vulkan and DXIL compute profiles.
RWStructuredBuffer<uint> output : register(u0);
[numthreads(4, 1, 1)]
void main(uint3 index : SV_DispatchThreadID)
{
    output[index.x] = index.x * 3u + 7u;
}
