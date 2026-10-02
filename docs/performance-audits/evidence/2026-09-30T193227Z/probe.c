// #129 diagnostic: the compact lexer's sixteen-row writer through the
// existing SIMD vocabulary. Entry points: probe_rows_bytes (candidate),
// probe_rows_dwords (host reference), probe_rows_scalar (independent oracle).
// This is an isolated leaf experiment, not a production/self-host lexer port.
#include <buster/lib/simd.h>
#include <stdio.h>
#include <string.h>

enum
{
    PROBE_LANES = 64,
    PROBE_ROWS = 16,
    PROBE_ROW_WORDS = 3,
    PROBE_OUTPUT_WORDS = PROBE_ROWS * PROBE_ROW_WORDS,
};

#if BUSTER_SIMD_512
typedef u32 ProbeWords __attribute__((vector_size(64)));
#endif

static u32 const probe_word_indices[3][16] =
{
    {0, 0, 16, 1, 0, 17, 2, 0, 18, 3, 0, 19, 4, 0, 20, 5},
    {0, 21, 6, 0, 22, 7, 0, 23, 8, 0, 24, 9, 0, 25, 10, 0},
    {26, 11, 0, 27, 12, 0, 28, 13, 0, 29, 14, 0, 30, 15, 0, 31},
};
static u32 const probe_word_masks[3] = {0xdb6d, 0x6db6, 0xb6db};
static u8 probe_byte_indices[3][64];
static Mask64 probe_byte_masks[3];

// These controls are constructed once, outside the observed leaf. A complete
// compiler port must charge their representation/prewarm and caller transport.
static void probe_controls_build(void)
{
    for (u32 tile = 0; tile < 3; tile += 1)
    {
        for (u32 byte = 0; byte < PROBE_LANES; byte += 1)
        {
            u32 word = byte / 4u;
            probe_byte_indices[tile][byte] = (u8)(4u * probe_word_indices[tile][word] + byte % 4u);
            probe_byte_masks[tile] |= (Mask64)((probe_word_masks[tile] >> word) & 1u) << byte;
        }
    }
}

static Simd512 probe_add_words(Simd512 left, Simd512 right)
{
    Simd512 result;
#if BUSTER_SIMD_512
    result = (Simd512)((ProbeWords)left + (ProbeWords)right);
#else
    u8 left_bytes[PROBE_LANES];
    u8 right_bytes[PROBE_LANES];
    u8 result_bytes[PROBE_LANES];
    simd512_store(left_bytes, left);
    simd512_store(right_bytes, right);
    for (u32 word = 0; word < PROBE_ROWS; word += 1)
    {
        u32 a = 0;
        u32 b = 0;
        for (u32 byte = 0; byte < 4; byte += 1)
        {
            a |= (u32)left_bytes[word * 4u + byte] << (byte * 8u);
            b |= (u32)right_bytes[word * 4u + byte] << (byte * 8u);
        }
        u32 sum = a + b;
        for (u32 byte = 0; byte < 4; byte += 1)
        {
            result_bytes[word * 4u + byte] = (u8)(sum >> (byte * 8u));
        }
    }
    result = simd512_load(result_bytes);
#endif
    return result;
}

// Keep the leaf out of line to retain an inspectable emitted symbol.
BUSTER_GLOBAL_LOCAL __attribute__((noinline)) void probe_rows_bytes(u32* out, u8* shapes_out, Mask64 shape_mask, u8 const* starts, u8 const* lengths,
                                     u8 const* kinds, u8 const* punctuators, u8 const* shapes, u32 base)
{
    Simd512 start_wide = simd512_widen_byte(simd512_load(starts), 0);
    Simd512 length_wide = simd512_widen_byte(simd512_load(lengths), 0);
    Simd512 kind_wide = simd512_widen_byte(simd512_load(kinds), 0);
    Simd512 punctuator_wide = simd512_widen_byte(simd512_load(punctuators), 0);
    Simd512 offsets = probe_add_words(start_wide, simd512_splat_word(base));
    Simd512 metadata = simd512_or(length_wide, simd512_or(simd512_shift_left_word(kind_wide, 16),
                                                        simd512_shift_left_word(punctuator_wide, 24)));
    for (u32 tile = 0; tile < 3; tile += 1)
    {
        Simd512 indices = simd512_load(probe_byte_indices[tile]);
        Simd512 rows = simd512_permute2_byte(probe_byte_masks[tile], offsets, indices, metadata);
        simd512_store(out + tile * PROBE_ROWS, rows);
    }
    simd512_store_masked(shapes_out, shape_mask, simd512_load(shapes));
}

#if BUSTER_SIMD_512
BUSTER_GLOBAL_LOCAL __attribute__((noinline)) void probe_rows_dwords(u32* out, u8* shapes_out, Mask64 shape_mask, u8 const* starts, u8 const* lengths,
                                      u8 const* kinds, u8 const* punctuators, u8 const* shapes, u32 base)
{
    __m512i start_wide = _mm512_cvtepu8_epi32(_mm_loadu_si128((__m128i const*)starts));
    __m512i length_wide = _mm512_cvtepu8_epi32(_mm_loadu_si128((__m128i const*)lengths));
    __m512i kind_wide = _mm512_cvtepu8_epi32(_mm_loadu_si128((__m128i const*)kinds));
    __m512i punctuator_wide = _mm512_cvtepu8_epi32(_mm_loadu_si128((__m128i const*)punctuators));
    __m512i offsets = _mm512_add_epi32(start_wide, _mm512_set1_epi32((int)base));
    __m512i metadata = _mm512_or_si512(length_wide, _mm512_or_si512(_mm512_slli_epi32(kind_wide, 16),
                                                                 _mm512_slli_epi32(punctuator_wide, 24)));
    for (u32 tile = 0; tile < 3; tile += 1)
    {
        __m512i indices = _mm512_loadu_si512(probe_word_indices[tile]);
        _mm512_storeu_si512(out + tile * PROBE_ROWS,
                           _mm512_maskz_permutex2var_epi32((__mmask16)probe_word_masks[tile], offsets, indices, metadata));
    }
    _mm512_mask_storeu_epi8(shapes_out, (__mmask64)shape_mask, _mm512_loadu_si512(shapes));
}
#endif

static void probe_rows_scalar(u32* out, u8* shapes_out, Mask64 shape_mask, u8 const* starts, u8 const* lengths,
                              u8 const* kinds, u8 const* punctuators, u8 const* shapes, u32 base)
{
    for (u32 row = 0; row < PROBE_ROWS; row += 1)
    {
        out[row * 3u] = base + starts[row];
        out[row * 3u + 1u] = 0;
        out[row * 3u + 2u] = (u32)lengths[row] | ((u32)kinds[row] << 16) | ((u32)punctuators[row] << 24);
    }
    for (u32 byte = 0; byte < PROBE_LANES; byte += 1)
    {
        if ((shape_mask >> byte) & 1u)
        {
            shapes_out[byte] = shapes[byte];
        }
    }
}

int main(void)
{
    u32 const bases[] = {0, 1, 63, UINT32_C(0x7fffffff), UINT32_C(0xfffffff0), UINT32_MAX};
    u32 failures = 0;
    u32 comparisons = 0;
    probe_controls_build();
    // Every input byte value, nonuniform lanes, u32 wrapping, all 0..16-row
    // shape tails and arbitrary bit masks. Arrays are deliberately shifted
    // within backing allocations; inactive shape bytes must retain canaries.
    for (u32 seed = 0; seed < 256; seed += 1)
    {
        u8 inputs[5][PROBE_LANES + 2];
        for (u32 field = 0; field < 5; field += 1)
        {
            for (u32 byte = 0; byte < PROBE_LANES; byte += 1)
            {
                inputs[field][byte + 1u] = (u8)(seed + byte * (field * 2u + 1u));
            }
        }
        for (u32 base_index = 0; base_index < BUSTER_ARRAY_LENGTH(bases); base_index += 1)
        {
            for (u32 shape_case = 0; shape_case < 20; shape_case += 1)
            {
                Mask64 mask = shape_case <= 16 ? mask64_prefix(shape_case) :
                              shape_case == 17 ? UINT64_C(0xaaaaaaaaaaaaaaaa) :
                              shape_case == 18 ? UINT64_C(0x8000000000000001) : UINT64_MAX;
                u32 expected_rows[PROBE_OUTPUT_WORDS + 2];
                u32 actual_rows[PROBE_OUTPUT_WORDS + 2];
                u8 expected_shapes[PROBE_LANES + 2];
                u8 actual_shapes[PROBE_LANES + 2];
                memset(expected_rows, 0x5a, sizeof(expected_rows));
                memset(actual_rows, 0x5a, sizeof(actual_rows));
                memset(expected_shapes, 0xa5, sizeof(expected_shapes));
                memset(actual_shapes, 0xa5, sizeof(actual_shapes));
                probe_rows_scalar(expected_rows + 1, expected_shapes + 1, mask, inputs[0] + 1, inputs[1] + 1,
                                  inputs[2] + 1, inputs[3] + 1, inputs[4] + 1, bases[base_index]);
                probe_rows_bytes(actual_rows + 1, actual_shapes + 1, mask, inputs[0] + 1, inputs[1] + 1,
                                 inputs[2] + 1, inputs[3] + 1, inputs[4] + 1, bases[base_index]);
                failures += memcmp(expected_rows, actual_rows, sizeof(expected_rows)) != 0;
                failures += memcmp(expected_shapes, actual_shapes, sizeof(expected_shapes)) != 0;
                comparisons += 2;
#if BUSTER_SIMD_512
                memset(actual_rows, 0x5a, sizeof(actual_rows));
                memset(actual_shapes, 0xa5, sizeof(actual_shapes));
                probe_rows_dwords(actual_rows + 1, actual_shapes + 1, mask, inputs[0] + 1, inputs[1] + 1,
                                  inputs[2] + 1, inputs[3] + 1, inputs[4] + 1, bases[base_index]);
                failures += memcmp(expected_rows, actual_rows, sizeof(expected_rows)) != 0;
                failures += memcmp(expected_shapes, actual_shapes, sizeof(expected_shapes)) != 0;
                comparisons += 2;
#endif
            }
        }
    }
    printf("PROBE full=%u base=%u fixtures=30720 comparisons=%u failures=%u\n",
           BUSTER_SIMD_512, BUSTER_SIMD_512_BASE, comparisons, failures);
    return failures != 0;
}
