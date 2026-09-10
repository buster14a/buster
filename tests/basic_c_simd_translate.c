// Exercise the source translator's production vocabulary, including the real
// scalar header fallback. Unlike basic_c_simd.c, no target guards erase the
// test body. Keep this function externally visible for independent ISA checks.
#include <buster/lib/simd.h>

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
    return failure;
}
