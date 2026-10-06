// The u32 vocabulary added for the C lexer's row writer (#129):
// simd512_add_u32 and simd512_permute2_u32, through the production header so
// the same body checks the scalar struct (baseline), the self-hosted F/BW
// builtins (skylake-avx512) and the full tier (znver5), against an oracle
// that does not use the header. Every failure returns a distinct code.
//
// Like tests/basic_c_simd_translate.c, this needs the header without a
// platform SDK: the base header's inline copies still need memcpy declared.
#define BUSTER_KERNEL 1
#include <stddef.h>
void* memcpy(void* restrict destination, void const* restrict source, size_t count);
#include <buster/lib/simd.h>

typedef union SimdU32Lanes SimdU32Lanes;
union SimdU32Lanes
{
    Simd512 vector;
    u32 u32_lanes[16];
};

// vpermt2d: the low five index bits pick one of 32 u32 lanes across low then
// high; lanes outside the low sixteen mask bits are zero.
BUSTER_GLOBAL_LOCAL void u32_permute2_oracle(u32* result, Mask64 mask, u32 const* low, u32 const* indices, u32 const* high)
{
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        u32 index = indices[lane] & 31;
        result[lane] = (mask >> lane) & 1 ? (index < 16 ? low[index] : high[index - 16]) : 0;
    }
}

int main(void)
{
    int failure = 0;
    SimdU32Lanes left;
    SimdU32Lanes right;
    SimdU32Lanes result;
    u32 values[] = {0, 1, 0x7fffffffU, 0x80000000U, 0xffffffffU};
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        left.u32_lanes[lane] = lane < 5 ? values[lane] : 0x9e3779b9U * lane;
        right.u32_lanes[lane] = values[(lane * 3 + 1) % 5] ^ (0x01020408U * lane);
    }

    // Addition wraps per u32 lane and never carries into a neighbor.
    result.vector = simd512_add_u32(left.vector, right.vector);
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        failure |= result.u32_lanes[lane] != left.u32_lanes[lane] + right.u32_lanes[lane] ? 1 : 0;
    }

    // Indices carry junk above bit 4, which the permute must ignore; mask
    // bits above lane 15 select nothing. The first three masks are the C
    // lexer's 12-byte row interleave.
    SimdU32Lanes indices;
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        indices.u32_lanes[lane] = (lane * 7 + 3) | (0x9e3779c0U * (lane + 1));
    }
    Mask64 masks[] = {0xDB6DULL, 0x6DB6ULL, 0xB6DBULL, 0xffffULL, 0, 0xffff8001ULL};
    for (u32 mask_index = 0; mask_index < 6; mask_index += 1)
    {
        u32 expected[16];
        u32_permute2_oracle(expected, masks[mask_index], left.u32_lanes, indices.u32_lanes, right.u32_lanes);
        result.vector = simd512_permute2_u32(masks[mask_index], left.vector, indices.vector, right.vector);
        for (u32 lane = 0; lane < 16; lane += 1)
        {
            failure |= result.u32_lanes[lane] != expected[lane] ? 2 : 0;
        }
    }
    return failure;
}
