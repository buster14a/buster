// Production SIMD-header conditional selection and stored lane results.
#define BUSTER_KERNEL 1
#include <stddef.h>
void* memcpy(void* restrict destination, void const* restrict source, size_t count);
#include <buster/lib/simd.h>

// Both calls have the same vector type even when their semantic rows differ.
int main(void)
{
    u8 input[65];
    u8 output[64];
    int failure = 0;
    for (u32 lane = 0; lane < 65; lane += 1)
    {
        input[lane] = (u8)(lane + 1);
    }
    for (u32 choose = 0; choose < 2; choose += 1)
    {
        Simd512 selected = choose ? simd512_load(input) : simd512_load(input + 1);
        simd512_store(output, selected);
        for (u32 lane = 0; lane < 64; lane += 1)
        {
            failure |= output[lane] != input[lane + (choose ? 0 : 1)];
        }
    }
    u32 counts[] = {0, 1, 31, 63, 64};
    for (u32 index = 0; index < sizeof(counts) / sizeof(counts[0]); index += 1)
    {
        u32 count = counts[index];
        Simd512 selected = count >= 64 ? simd512_load(input) : simd512_load_masked(input, mask64_prefix(count));
        simd512_store(output, selected);
        for (u32 lane = 0; lane < 64; lane += 1)
        {
            failure |= output[lane] != (lane < count ? input[lane] : 0);
        }
    }
    return failure;
}
