#include <buster/tests/simd_test.h>
#if BUSTER_INCLUDE_TESTS

// <buster/lib/simd.h> against hand-computed results, under whichever of its
// three implementations this build selected. That is the point of running it
// here rather than only as a driver fixture: the host compiler picks the
// intrinsics on an AVX-512 x86-64 box and the scalar fallback on AArch64,
// MSVC, and pre-AVX-512 x86, so the same expectations are checked against both
// on the CI matrix and neither can drift from the other unnoticed. The
// builtins are covered separately by tests/basic_c_simd.c, which `ide cc`
// compiles.

typedef union SimdTestLanes SimdTestLanes;
union SimdTestLanes
{
    Simd512 vector;
    u8 bytes[64];
    u32 words[16];
};

UnitTestResult simd_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};

    // Masks. Same on every target, so these hold whichever implementation is
    // in play.
    BUSTER_TEST(arguments, mask64_prefix(0) == 0);
    BUSTER_TEST(arguments, mask64_prefix(1) == 1);
    BUSTER_TEST(arguments, mask64_prefix(64) == ~(Mask64)0);
    BUSTER_TEST(arguments, mask64_shift_left(1, 63) == ((Mask64)1 << 63));
    BUSTER_TEST(arguments, mask64_shift_right((Mask64)1 << 63, 63) == 1);
    BUSTER_TEST(arguments, mask64_and(0xF0, 0x3C) == 0x30);
    BUSTER_TEST(arguments, mask64_or(0xF0, 0x0F) == 0xFF);
    BUSTER_TEST(arguments, mask64_xor(0xFF, 0x0F) == 0xF0);
    BUSTER_TEST(arguments, mask64_and_not(0xFF, 0x0F) == 0xF0);
    BUSTER_TEST(arguments, mask64_not(0) == ~(Mask64)0);
    // 0b0111'0110: runs at lanes 1-2 and 4-6.
    BUSTER_TEST(arguments, mask64_run_starts(0x76) == 0x12);
    BUSTER_TEST(arguments, mask64_run_ends(0x76) == 0x44);
    BUSTER_TEST(arguments, mask64_count(0) == 0);
    BUSTER_TEST(arguments, mask64_count(~(Mask64)0) == 64);
    BUSTER_TEST(arguments, mask64_count(0xF0F0) == 8);
    BUSTER_TEST(arguments, mask64_first_set(1) == 0);
    BUSTER_TEST(arguments, mask64_first_set(0xF0) == 4);
    BUSTER_TEST(arguments, mask64_first_set((Mask64)1 << 63) == 63);
    BUSTER_TEST(arguments, mask64_leading_ones(0x0F) == 4);
    BUSTER_TEST(arguments, mask64_leading_ones(0) == 0);

    u8 source[64];
    u8 indices[64];
    u8 high[64];
    u8 signs[64];
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        source[lane] = (u8)lane;
        indices[lane] = (u8)(127 - lane);
        high[lane] = (u8)(0x80 + lane);
        signs[lane] = (u8)(lane & 1 ? 0x80 : 0x01);
    }

    Simd512 value = simd512_load(source);
    SimdTestLanes probe;
    bool lanes_match = true;

    probe.vector = value;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= probe.bytes[lane] == (u8)lane;
    }
    BUSTER_TEST(arguments, lanes_match);

    // Lanes outside a masked load read as zero and are never fetched, which is
    // what lets a chunked scan run off the end of a buffer.
    lanes_match = true;
    probe.vector = simd512_load_masked(source, 0x0F);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= probe.bytes[lane] == (lane < 4 ? (u8)lane : 0);
    }
    BUSTER_TEST(arguments, lanes_match);

    lanes_match = true;
    probe.vector = simd512_load_masked(source, 0);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= probe.bytes[lane] == 0;
    }
    BUSTER_TEST(arguments, lanes_match);

    lanes_match = true;
    probe.vector = simd512_splat(0xA5);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= probe.bytes[lane] == 0xA5;
    }
    BUSTER_TEST(arguments, lanes_match);

    lanes_match = true;
    probe.vector = simd512_zero();
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= probe.bytes[lane] == 0;
    }
    BUSTER_TEST(arguments, lanes_match);

    u8 written[64];
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        written[lane] = 0xCD;
    }
    simd512_store(written, value);
    lanes_match = true;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= written[lane] == (u8)lane;
    }
    BUSTER_TEST(arguments, lanes_match);

    for (u32 lane = 0; lane < 64; lane += 1)
    {
        written[lane] = 0xCD;
    }
    simd512_store_masked(written, 0x03, value);
    lanes_match = true;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= written[lane] == (lane < 2 ? (u8)lane : 0xCD);
    }
    BUSTER_TEST(arguments, lanes_match);

    // Comparisons. Byte `lane` equals 7 only at lane 7 and is below it in the
    // seven lanes under it.
    BUSTER_TEST(arguments, simd512_equal_byte(value, simd512_splat(7)) == ((Mask64)1 << 7));
    BUSTER_TEST(arguments, simd512_less_byte(value, simd512_splat(7)) == 0x7F);
    BUSTER_TEST(arguments, simd512_equal_byte(value, value) == ~(Mask64)0);
    BUSTER_TEST(arguments, simd512_less_byte(value, value) == 0);
    BUSTER_TEST(arguments, simd512_sign_byte(simd512_load(signs)) == UINT64_C(0xAAAAAAAAAAAAAAAA));
    BUSTER_TEST(arguments, simd512_sign_byte(value) == 0);
    BUSTER_TEST(arguments, simd512_sign_byte(simd512_load(high)) == ~(Mask64)0);
    // lane & 3 is non-zero for three lanes in every four.
    BUSTER_TEST(arguments, simd512_test_byte(value, simd512_splat(3)) == UINT64_C(0xEEEEEEEEEEEEEEEE));
    BUSTER_TEST(arguments, simd512_test_byte(value, simd512_zero()) == 0);

    // vpermt2b indexes a 128-byte table split across two vectors; the indices
    // count down from 127, so lane 0 selects the last byte of the high half.
    lanes_match = true;
    probe.vector = simd512_permute2_byte(~(Mask64)0, value, simd512_load(indices), simd512_load(high));
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        u32 index = 127 - lane;
        lanes_match &= probe.bytes[lane] == (index < 64 ? (u8)index : high[index - 64]);
    }
    BUSTER_TEST(arguments, lanes_match);

    lanes_match = true;
    probe.vector = simd512_permute2_byte(0x07, value, simd512_load(indices), simd512_load(high));
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= probe.bytes[lane] == (lane < 3 ? high[63 - lane] : 0);
    }
    BUSTER_TEST(arguments, lanes_match);

    // vpcompressb packs the selected lanes down and zeroes the rest.
    lanes_match = true;
    probe.vector = simd512_compress_byte(UINT64_C(0x5555555555555555), value);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= probe.bytes[lane] == (lane < 32 ? (u8)(lane * 2) : 0);
    }
    BUSTER_TEST(arguments, lanes_match);

    lanes_match = true;
    probe.vector = simd512_compress_byte(~(Mask64)0, value);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        lanes_match &= probe.bytes[lane] == (u8)lane;
    }
    BUSTER_TEST(arguments, lanes_match);

    // The compacting store writes exactly mask64_count(mask) bytes and must
    // not touch the byte after them.
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        written[lane] = 0xCD;
    }
    simd512_compress_store_byte(written, 0x55, value);
    lanes_match = true;
    for (u32 lane = 0; lane < 4; lane += 1)
    {
        lanes_match &= written[lane] == (u8)(lane * 2);
    }
    BUSTER_TEST(arguments, lanes_match);
    BUSTER_TEST(arguments, written[4] == 0xCD);

    // vpmovzxbd, one quarter at a time, and the high bytes must zero-extend
    // rather than sign-extend.
    lanes_match = true;
    probe.vector = simd512_widen_byte(value, 0);
    for (u32 word = 0; word < 16; word += 1)
    {
        lanes_match &= probe.words[word] == word;
    }
    BUSTER_TEST(arguments, lanes_match);

    lanes_match = true;
    probe.vector = simd512_widen_byte(value, 3);
    for (u32 word = 0; word < 16; word += 1)
    {
        lanes_match &= probe.words[word] == word + 48;
    }
    BUSTER_TEST(arguments, lanes_match);

    lanes_match = true;
    probe.vector = simd512_widen_byte(simd512_load(high), 3);
    for (u32 word = 0; word < 16; word += 1)
    {
        lanes_match &= probe.words[word] == (u32)high[48 + word];
    }
    BUSTER_TEST(arguments, lanes_match);

    lanes_match = true;
    probe.vector = simd512_shift_left_word(simd512_widen_byte(value, 1), 8);
    for (u32 word = 0; word < 16; word += 1)
    {
        lanes_match &= probe.words[word] == (word + 16) << 8;
    }
    BUSTER_TEST(arguments, lanes_match);

    // The token-stream shape the tokenizer builds: kind | (length << 8).
    lanes_match = true;
    probe.vector = simd512_ternary_word(simd512_widen_byte(value, 0), simd512_shift_left_word(simd512_widen_byte(value, 1), 8), simd512_zero(), 0xFE);
    for (u32 word = 0; word < 16; word += 1)
    {
        lanes_match &= probe.words[word] == (word | ((word + 16) << 8));
    }
    BUSTER_TEST(arguments, lanes_match);

    // vpternlogd truth tables: 0xFE is a|b|c, 0x80 is a&b&c, 0x96 is a^b^c,
    // and 0xF0/0xCC/0xAA select one input untouched, which catches an operand
    // order that happens to work for the symmetric tables.
    Simd512 a = simd512_splat(0xF0);
    Simd512 b = simd512_splat(0xCC);
    Simd512 c = simd512_splat(0xAA);
    probe.vector = simd512_ternary_word(a, b, c, 0xFE);
    BUSTER_TEST(arguments, probe.bytes[0] == (0xF0 | 0xCC | 0xAA));
    probe.vector = simd512_ternary_word(a, b, c, 0x80);
    BUSTER_TEST(arguments, probe.bytes[0] == (0xF0 & 0xCC & 0xAA));
    probe.vector = simd512_ternary_word(a, b, c, 0x96);
    BUSTER_TEST(arguments, probe.bytes[0] == (0xF0 ^ 0xCC ^ 0xAA));
    probe.vector = simd512_ternary_word(a, b, c, 0xF0);
    BUSTER_TEST(arguments, probe.bytes[0] == 0xF0);
    probe.vector = simd512_ternary_word(a, b, c, 0xCC);
    BUSTER_TEST(arguments, probe.bytes[0] == 0xCC);
    probe.vector = simd512_ternary_word(a, b, c, 0xAA);
    BUSTER_TEST(arguments, probe.bytes[0] == 0xAA);

    // Lanewise arithmetic and bitwise operations.
    probe.vector = simd512_and(a, b);
    BUSTER_TEST(arguments, probe.bytes[0] == (0xF0 & 0xCC));
    probe.vector = simd512_or(a, b);
    BUSTER_TEST(arguments, probe.bytes[0] == (0xF0 | 0xCC));
    probe.vector = simd512_xor(a, b);
    BUSTER_TEST(arguments, probe.bytes[0] == (0xF0 ^ 0xCC));
    probe.vector = simd512_add_byte(value, simd512_splat(1));
    BUSTER_TEST(arguments, probe.bytes[0] == 1 && probe.bytes[63] == 64);
    // Lane 0 is the wraparound case in the other direction: 0 - 1 is 0xFF.
    probe.vector = simd512_subtract_byte(value, simd512_splat(1));
    BUSTER_TEST(arguments, probe.bytes[0] == 0xFF && probe.bytes[63] == 62);
    probe.vector = simd512_add_byte(simd512_splat(0xFF), simd512_splat(2));
    BUSTER_TEST(arguments, probe.bytes[0] == 1);

    // One pass of the shape a chunked scan actually runs: classify, take the
    // run starts and ends, compact the positions out with an iota vector.
    u8 iota[64];
    u8 text[64];
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        iota[lane] = (u8)lane;
        text[lane] = (u8)(lane % 8 < 3 ? 'a' : ' ');
    }
    Mask64 letters = simd512_equal_byte(simd512_load(text), simd512_splat('a'));
    Mask64 starts = mask64_run_starts(letters);
    Mask64 ends = mask64_run_ends(letters);
    BUSTER_TEST(arguments, mask64_count(letters) == 24);
    BUSTER_TEST(arguments, mask64_count(starts) == 8);
    BUSTER_TEST(arguments, mask64_count(ends) == 8);

    SimdTestLanes start_positions;
    SimdTestLanes end_positions;
    start_positions.vector = simd512_compress_byte(starts, simd512_load(iota));
    end_positions.vector = simd512_compress_byte(ends, simd512_load(iota));
    lanes_match = true;
    for (u32 run = 0; run < 8; run += 1)
    {
        lanes_match &= start_positions.bytes[run] == (u8)(run * 8);
        lanes_match &= end_positions.bytes[run] == (u8)(run * 8 + 2);
        lanes_match &= (u8)(end_positions.bytes[run] - start_positions.bytes[run] + 1) == 3;
    }
    BUSTER_TEST(arguments, lanes_match);

    return result;
}
#endif
