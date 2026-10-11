// Exercise the source translator's production vocabulary, including the real
// scalar header fallback. Unlike basic_c_simd.c, no target guards erase the
// test body. Keep this function externally visible for independent ISA checks.
// This cross-target fixture needs the production SIMD vocabulary without a
// platform SDK. The base header's inline copies still need the C declaration;
// native execution links the ordinary runtime implementation.
#define BUSTER_KERNEL 1
#include <stddef.h>
void* memcpy(void* restrict destination, void const* restrict source, size_t count);
#include <buster/lib/simd.h>

_Static_assert(sizeof(UINT64_C(1)) == 8, "mask constants need 64-bit arithmetic");
_Static_assert(UINT64_C(1) << 63 == 0x8000000000000000ULL, "the last lane remains unsigned");
#if defined(__x86_64__) && defined(__AVX512F__) && defined(__AVX512BW__) && (!defined(__AVX512VBMI__) || !defined(__AVX512VBMI2__))
_Static_assert(BUSTER_SIMD_512_BASE == 1, "F/BW selects the base SIMD tier");
_Static_assert(BUSTER_SIMD_512 == 0, "VBMI/VBMI2 remain excluded from the base SIMD tier");
#endif

typedef union SimdTranslateWordLanes SimdTranslateWordLanes;
union SimdTranslateWordLanes
{
    Simd512 vector;
    u32 words[16];
};

BUSTER_GLOBAL_LOCAL Mask64 word_less_oracle(u32 const* left, u32 const* right)
{
    Mask64 result = 0;
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        result |= left[lane] < right[lane] ? (Mask64)1 << lane : 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL Mask64 word_equal_oracle(u32 const* left, u32 const* right)
{
    Mask64 result = 0;
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        result |= left[lane] == right[lane] ? (Mask64)1 << lane : 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void word_compress_oracle(u32* result, Mask64 mask, u32 const* source)
{
    u32 count = 0;
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        if ((mask >> lane) & 1)
        {
            result[count] = source[lane];
            count += 1;
        }
    }
    for (u32 lane = count; lane < 16; lane += 1)
    {
        result[lane] = 0;
    }
}

u64 buster_simd_translate_block(u8* destination, u8 const* source, u64* newlines)
{
    Simd512 chunk = simd512_load(source);
    Mask64 carriage = simd512_equal_byte(chunk, simd512_splat('\r'));
    Mask64 line_feed = simd512_equal_byte(chunk, simd512_splat('\n'));
    Mask64 backslash = simd512_equal_byte(chunk, simd512_splat('\\'));
    *newlines = line_feed;
    simd512_store(destination, chunk);
    return carriage | (backslash & ((line_feed | carriage) >> 1)) | (backslash & (UINT64_C(1) << 63));
}

static int check_block(u8 const* source)
{
    u8 destination[66];
    destination[0] = 0xA5;
    destination[65] = 0x5A;
    u64 expected_stops = 0;
    u64 expected_newlines = 0;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        u8 byte = source[lane];
        bool splice = false;
        if (byte == '\\')
        {
            splice = lane == 63;
            if (lane < 63)
            {
                splice = source[lane + 1] == '\n' || source[lane + 1] == '\r';
            }
        }
        if (byte == '\r' || splice)
        {
            expected_stops |= UINT64_C(1) << lane;
        }
        if (byte == '\n')
        {
            expected_newlines |= UINT64_C(1) << lane;
        }
    }
    u64 observed_newlines = 0;
    u64 observed_stops = buster_simd_translate_block(destination + 1, source, &observed_newlines);
    int failure = observed_stops != expected_stops || observed_newlines != expected_newlines;
    failure |= destination[0] != 0xA5 || destination[65] != 0x5A;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        failure |= destination[lane + 1] != source[lane];
    }
    return failure;
}

int main(void)
{
    u8 storage[65];
    u8* source = storage + 1;
    int failure = 0;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        source[lane] = 'a';
    }
    // Every byte in every lane, plus adjacent newline/splice stops. Input and
    // output are unaligned and no byte following the 64-byte input is read.
    for (u32 byte = 0; byte < 256; byte += 1)
    {
        for (u32 lane = 0; lane < 64; lane += 1)
        {
            source[lane] = (u8)byte;
            failure |= check_block(source);
            source[lane] = 'a';
        }
    }
    for (u32 lane = 0; lane < 63; lane += 1)
    {
        source[lane] = '\\';
        source[lane + 1] = '\n';
        failure |= check_block(source);
        source[lane + 1] = '\r';
        failure |= check_block(source);
        source[lane] = 'a';
        source[lane + 1] = 'a';
    }
    // On F/BW-only targets the vector representation must also work in the
    // existing fallback for the two operations requiring VBMI/VBMI2.
    u8 packed[64];
    Simd512 indices = simd512_splat(0);
    Simd512 letters = simd512_load(source);
    Simd512 selected = simd512_permute2_byte(1, letters, indices, letters);
    Simd512 compacted = simd512_compress_byte(1, selected);
    simd512_store(packed, compacted);
    failure |= packed[0] != 'a';
    for (u32 lane = 1; lane < 64; lane += 1)
    {
        failure |= packed[lane] != 0;
    }

    // This body never preprocesses away: baseline exercises the scalar
    // struct, skylake-avx512 the F/BW exact builtins, and znver5 the complete
    // vocabulary. Keep the expectations independent of the production header.
    SimdTranslateWordLanes word_left;
    SimdTranslateWordLanes word_right;
    SimdTranslateWordLanes word_result;
    u32 word_values[] = {0, 1, 0x7fffffffU, 0x80000000U, 0xffffffffU};
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        word_left.words[lane] = lane < 5 ? word_values[lane] : 0x9e3779b9U * lane;
        word_right.words[lane] = word_values[(lane * 3 + 1) % 5] ^ (0x01020408U * lane);
    }
    Mask64 observed = simd512_less_word(word_left.vector, word_right.vector);
    failure |= observed != word_less_oracle(word_left.words, word_right.words);
    failure |= (observed & ~0xffffULL) != 0;
    SimdTranslateWordLanes equal_right;
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        equal_right.words[lane] = word_left.words[lane];
        if (lane % 3 == 0)
        {
            equal_right.words[lane] ^= 0x80000001U;
        }
    }
    observed = simd512_equal_word(word_left.vector, equal_right.vector);
    failure |= observed != word_equal_oracle(word_left.words, equal_right.words);
    failure |= (observed & ~0xffffULL) != 0;
    for (u32 value = 0; value < 5; value += 1)
    {
        word_result.vector = simd512_splat_word(word_values[value]);
        for (u32 lane = 0; lane < 16; lane += 1)
        {
            failure |= word_result.words[lane] != word_values[value];
        }
    }
    Mask64 compact_mask = 0xfedcba987654a5c3ULL;
    u32 expected_compacted[16];
    word_compress_oracle(expected_compacted, compact_mask, word_left.words);
    word_result.vector = simd512_compress_word(compact_mask, word_left.vector);
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        failure |= word_result.words[lane] != expected_compacted[lane];
    }
    return failure;
}
