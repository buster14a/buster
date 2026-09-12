// No device-library calls: the HSA profile needs no ROCm runtime or GPU.
__kernel void buster_gpu_smoke(__global unsigned *output, unsigned input)
{
    output[0] = input * 3u + 7u;
}
