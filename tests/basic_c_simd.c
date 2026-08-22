// The target-fixed 512-bit vocabulary, checked against hand-computed results.
//
// Self-contained on purpose. A driver fixture has to compile for every target
// `ide cc` supports, and including <buster/lib/simd.h> would pull <string.h>
// and <stdlib.h> in through base.h -- headers that exist for the host and for
// nobody else, so the fixture would build only where the sysroot happens to
// match. That is what every other fixture in this directory avoids by
// declaring what it needs; this one does the same and calls the builtins
// directly. The header's own three implementations are covered by
// simd_tests in src/buster/tests/simd_test.c, which the host compiler builds
// on every platform.
//
// The guard is the same condition <buster/lib/simd.h> uses, spelled out:
// `ide cc` predefines these from its target, so on a machine without the
// features the whole body compiles out and the fixture trivially succeeds.
//
// Every failure returns a distinct code so a driver run names the check.

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

#if defined(__x86_64__) && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VBMI__) && defined(__AVX512VBMI2__)
#define FIXTURE_SIMD_512 1
#else
#define FIXTURE_SIMD_512 0
#endif

#if FIXTURE_SIMD_512

typedef u8 Simd512 __attribute__((vector_size(64)));
typedef u64 Mask64;

#define simd512_load(address) __builtin_buster_simd_load(address)
#define simd512_load_masked(address, mask) __builtin_buster_simd_load_masked((address), (mask))
#define simd512_store(address, value) __builtin_buster_simd_store((address), (value))
#define simd512_store_masked(address, mask, value) __builtin_buster_simd_store_masked((address), (mask), (value))
#define simd512_splat(byte) __builtin_buster_simd_splat_byte(byte)
#define simd512_zero() simd512_splat(0)
#define simd512_equal_byte(left, right) __builtin_buster_simd_equal_byte((left), (right))
#define simd512_less_byte(left, right) __builtin_buster_simd_less_byte((left), (right))
#define simd512_sign_byte(value) __builtin_buster_simd_sign_byte(value)
#define simd512_test_byte(left, right) __builtin_buster_simd_test_byte((left), (right))
#define simd512_permute2_byte(mask, low, indices, high) __builtin_buster_simd_permute2_byte((mask), (low), (indices), (high))
#define simd512_compress_byte(mask, value) __builtin_buster_simd_compress_byte((mask), (value))
#define simd512_compress_store_byte(address, mask, value) __builtin_buster_simd_compress_store_byte((address), (mask), (value))
#define simd512_widen_byte(value, quarter) __builtin_buster_simd_widen_byte((value), (quarter))
#define simd512_shift_left_word(value, count) __builtin_buster_simd_shift_left_word((value), (count))
#define simd512_ternary_word(a, b, c, table) __builtin_buster_simd_ternary_word((a), (b), (c), (table))
#define simd512_equal_word(left, right) __builtin_buster_simd_equal_word((left), (right))
#define simd512_splat_word(value) __builtin_buster_simd_splat_word(value)
#define simd512_less_word(left, right) __builtin_buster_simd_less_word((left), (right))
#define simd512_add_byte(left, right) ((Simd512)((left) + (right)))

#define mask64_prefix(count) ((count) >= 64 ? ~(Mask64)0 : (((Mask64)1 << (count)) - 1))
#define mask64_shift_left(mask, count) ((Mask64)(mask) << (count))
#define mask64_shift_right(mask, count) ((Mask64)(mask) >> (count))
#define mask64_and(left, right) ((Mask64)(left) & (Mask64)(right))
#define mask64_and_not(left, right) ((Mask64)(left) & ~(Mask64)(right))
#define mask64_or(left, right) ((Mask64)(left) | (Mask64)(right))
#define mask64_xor(left, right) ((Mask64)(left) ^ (Mask64)(right))
#define mask64_not(mask) (~(Mask64)(mask))
#define mask64_run_starts(mask) ((Mask64)(mask) & ~((Mask64)(mask) << 1))
#define mask64_run_ends(mask) ((Mask64)(mask) & ~((Mask64)(mask) >> 1))
#define mask64_count(mask) ((u32)__builtin_popcountll(mask))
#define mask64_first_set(mask) ((u32)__builtin_ctzll(mask))
#define mask64_leading_ones(mask) mask64_first_set(~(Mask64)(mask))

typedef union Lanes Lanes;
union Lanes
{
    Simd512 vector;
    u8 bytes[64];
    u32 words[16];
};

// Crossing a call boundary by value. On the vector path a Simd512 is one
// 64-byte vector and travels in a vector register; on the fallback path it is
// a 64-byte struct and travels in memory. Both are exercised because the
// driver builds and runs this file twice, so neither convention can rot.
static Simd512 vector_identity(Simd512 value)
{
    return value;
}

// Nine of them: the first eight take the argument registers and the ninth has
// to be handed over on the stack.
static Simd512 vector_ninth(Simd512 a, Simd512 b, Simd512 c, Simd512 d, Simd512 e, Simd512 f, Simd512 g, Simd512 h, Simd512 i)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    return i;
}

// Interleaved with integers so both register files have to advance together.
static Simd512 vector_mixed(int first, Simd512 value, long long second, Simd512 other, int third)
{
    return simd512_add_byte(simd512_add_byte(value, other), simd512_splat((u8)(first + second + third)));
}

static u8 source_bytes[64];
static u8 index_bytes[64];
static u8 high_bytes[64];
static u8 sign_bytes[64];

static void fill(void)
{
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        source_bytes[lane] = (u8)lane;
        index_bytes[lane] = (u8)(127 - lane);
        high_bytes[lane] = (u8)(0x80 + lane);
        sign_bytes[lane] = (u8)(lane & 1 ? 0x80 : 0x01);
    }
}

int main(void)
{
    fill();

    // Masks first: everything below reads its expectations in terms of them.
    if (mask64_prefix(0) != 0)
    {
        return 1;
    }
    if (mask64_prefix(1) != 1)
    {
        return 2;
    }
    if (mask64_prefix(64) != ~(Mask64)0)
    {
        return 3;
    }
    if (mask64_shift_left(1, 63) != ((Mask64)1 << 63))
    {
        return 4;
    }
    if (mask64_shift_right((Mask64)1 << 63, 63) != 1)
    {
        return 5;
    }
    if (mask64_and(0xF0, 0x3C) != 0x30 || mask64_or(0xF0, 0x0F) != 0xFF || mask64_xor(0xFF, 0x0F) != 0xF0)
    {
        return 6;
    }
    if (mask64_and_not(0xFF, 0x0F) != 0xF0 || mask64_not(0) != ~(Mask64)0)
    {
        return 7;
    }
    // 0b0111'0110: runs at lanes 1-2 and 4-6.
    if (mask64_run_starts(0x76) != 0x12)
    {
        return 8;
    }
    if (mask64_run_ends(0x76) != 0x44)
    {
        return 9;
    }
    if (mask64_count(0) != 0 || mask64_count(~(Mask64)0) != 64 || mask64_count(0xF0F0) != 8)
    {
        return 10;
    }
    if (mask64_first_set(1) != 0 || mask64_first_set(0xF0) != 4 || mask64_first_set((Mask64)1 << 63) != 63)
    {
        return 11;
    }
    if (mask64_leading_ones(0x0F) != 4 || mask64_leading_ones(0) != 0)
    {
        return 12;
    }

    Simd512 value = simd512_load(source_bytes);
    Lanes probe;

    // A full-width load reproduces the buffer, and a zero mask reproduces
    // nothing at all.
    probe.vector = value;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != (u8)lane)
        {
            return 13;
        }
    }
    probe.vector = simd512_load_masked(source_bytes, 0);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != 0)
        {
            return 14;
        }
    }
    probe.vector = simd512_load_masked(source_bytes, 0x0F);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != (lane < 4 ? (u8)lane : 0))
        {
            return 15;
        }
    }
    probe.vector = simd512_zero();
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != 0)
        {
            return 16;
        }
    }
    probe.vector = simd512_splat(0xA5);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != 0xA5)
        {
            return 17;
        }
    }

    u8 written[64];
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        written[lane] = 0xCD;
    }
    simd512_store(written, value);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (written[lane] != (u8)lane)
        {
            return 18;
        }
    }
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        written[lane] = 0xCD;
    }
    simd512_store_masked(written, 0x03, value);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (written[lane] != (lane < 2 ? (u8)lane : 0xCD))
        {
            return 19;
        }
    }

    // Comparisons. Byte `lane` equals 7 only at lane 7, and is below 7 in the
    // seven lanes under it.
    if (simd512_equal_byte(value, simd512_splat(7)) != ((Mask64)1 << 7))
    {
        return 20;
    }
    if (simd512_less_byte(value, simd512_splat(7)) != 0x7F)
    {
        return 21;
    }
    if (simd512_equal_byte(value, value) != ~(Mask64)0)
    {
        return 22;
    }
    if (simd512_less_byte(value, value) != 0)
    {
        return 23;
    }
    // 128..255 are the high-bit bytes, and an unsigned compare has to agree.
    if (simd512_sign_byte(simd512_load(sign_bytes)) != 0xAAAAAAAAAAAAAAAAULL)
    {
        return 24;
    }
    if (simd512_sign_byte(value) != 0)
    {
        return 25;
    }
    if (simd512_sign_byte(simd512_load(high_bytes)) != ~(Mask64)0)
    {
        return 26;
    }
    // lane & 3 is non-zero for three lanes in every four.
    if (simd512_test_byte(value, simd512_splat(3)) != 0xEEEEEEEEEEEEEEEEULL)
    {
        return 27;
    }
    if (simd512_test_byte(value, simd512_zero()) != 0)
    {
        return 28;
    }

    // vpermt2b indexes a 128-byte table split across two vectors. index_bytes
    // counts down from 127, so lane 0 selects the last byte of the high half.
    probe.vector = simd512_permute2_byte(~(Mask64)0, value, simd512_load(index_bytes), simd512_load(high_bytes));
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        u32 index = 127 - lane;
        u8 expected = index < 64 ? (u8)index : high_bytes[index - 64];
        if (probe.bytes[lane] != expected)
        {
            return 29;
        }
    }
    probe.vector = simd512_permute2_byte(0x07, value, simd512_load(index_bytes), simd512_load(high_bytes));
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != (lane < 3 ? high_bytes[63 - lane] : 0))
        {
            return 30;
        }
    }

    // vpcompressb packs the selected lanes down and zeroes the rest.
    probe.vector = simd512_compress_byte(0x5555555555555555ULL, value);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != (lane < 32 ? (u8)(lane * 2) : 0))
        {
            return 31;
        }
    }
    probe.vector = simd512_compress_byte(0, value);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != 0)
        {
            return 32;
        }
    }
    probe.vector = simd512_compress_byte(~(Mask64)0, value);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != (u8)lane)
        {
            return 33;
        }
    }

    // The compacting store writes exactly mask64_count(mask) bytes and must
    // not touch the byte after them.
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        written[lane] = 0xCD;
    }
    simd512_compress_store_byte(written, 0x55, value);
    for (u32 lane = 0; lane < 4; lane += 1)
    {
        if (written[lane] != (u8)(lane * 2))
        {
            return 34;
        }
    }
    if (written[4] != 0xCD)
    {
        return 35;
    }
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        written[lane] = 0xCD;
    }
    simd512_compress_store_byte(written, 0, value);
    if (written[0] != 0xCD)
    {
        return 36;
    }

    // vpmovzxbd, one quarter at a time.
    probe.vector = simd512_widen_byte(value, 0);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (probe.words[word] != word)
        {
            return 37;
        }
    }
    probe.vector = simd512_widen_byte(value, 1);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (probe.words[word] != word + 16)
        {
            return 38;
        }
    }
    probe.vector = simd512_widen_byte(value, 2);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (probe.words[word] != word + 32)
        {
            return 39;
        }
    }
    probe.vector = simd512_widen_byte(value, 3);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (probe.words[word] != word + 48)
        {
            return 40;
        }
    }
    // The high bytes must zero-extend rather than sign-extend.
    probe.vector = simd512_widen_byte(simd512_load(high_bytes), 3);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (probe.words[word] != (u32)high_bytes[48 + word])
        {
            return 41;
        }
    }

    probe.vector = simd512_shift_left_word(simd512_widen_byte(value, 1), 8);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (probe.words[word] != (word + 16) << 8)
        {
            return 42;
        }
    }
    probe.vector = simd512_shift_left_word(simd512_widen_byte(value, 1), 0);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (probe.words[word] != word + 16)
        {
            return 43;
        }
    }
    probe.vector = simd512_shift_left_word(simd512_widen_byte(value, 0), 31);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (probe.words[word] != ((word & 1) ? 0x80000000u : 0u))
        {
            return 44;
        }
    }
    // The token-stream shape the tokenizer builds: kind | (length << 8).
    probe.vector = simd512_ternary_word(simd512_widen_byte(value, 0), simd512_shift_left_word(simd512_widen_byte(value, 1), 8), simd512_zero(), 0xFE);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (probe.words[word] != (word | ((word + 16) << 8)))
        {
            return 45;
        }
    }

    // vpternlogd truth tables: 0xFE is a|b|c, 0x80 is a&b&c, 0x96 is a^b^c and
    // 0x00 is the constant zero.
    Simd512 a = simd512_splat(0xF0);
    Simd512 b = simd512_splat(0xCC);
    Simd512 c = simd512_splat(0xAA);
    probe.vector = simd512_ternary_word(a, b, c, 0xFE);
    if (probe.bytes[0] != (0xF0 | 0xCC | 0xAA) || probe.bytes[63] != (0xF0 | 0xCC | 0xAA))
    {
        return 46;
    }
    probe.vector = simd512_ternary_word(a, b, c, 0x80);
    if (probe.bytes[0] != (0xF0 & 0xCC & 0xAA))
    {
        return 47;
    }
    probe.vector = simd512_ternary_word(a, b, c, 0x96);
    if (probe.bytes[0] != (0xF0 ^ 0xCC ^ 0xAA))
    {
        return 48;
    }
    probe.vector = simd512_ternary_word(a, b, c, 0x00);
    if (probe.bytes[0] != 0 || probe.bytes[63] != 0)
    {
        return 49;
    }
    // The identity table 0xF0 selects `a` untouched, which catches an operand
    // order that happens to work for the symmetric tables above.
    probe.vector = simd512_ternary_word(a, b, c, 0xF0);
    if (probe.bytes[0] != 0xF0)
    {
        return 50;
    }
    probe.vector = simd512_ternary_word(a, b, c, 0xCC);
    if (probe.bytes[0] != 0xCC)
    {
        return 51;
    }
    probe.vector = simd512_ternary_word(a, b, c, 0xAA);
    if (probe.bytes[0] != 0xAA)
    {
        return 52;
    }

    // One pass of the shape a chunked scan actually runs: classify, take the
    // run starts and ends, compact the lengths out with an iota vector.
    u8 iota[64];
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        iota[lane] = (u8)lane;
    }
    u8 text[64];
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        text[lane] = (u8)(lane % 8 < 3 ? 'a' : ' ');
    }
    Simd512 chunk = simd512_load(text);
    Mask64 letters = simd512_equal_byte(chunk, simd512_splat('a'));
    Mask64 starts = mask64_run_starts(letters);
    Mask64 ends = mask64_run_ends(letters);
    if (mask64_count(letters) != 24 || mask64_count(starts) != 8 || mask64_count(ends) != 8)
    {
        return 53;
    }
    Lanes start_positions;
    Lanes end_positions;
    start_positions.vector = simd512_compress_byte(starts, simd512_load(iota));
    end_positions.vector = simd512_compress_byte(ends, simd512_load(iota));
    for (u32 run = 0; run < 8; run += 1)
    {
        if (start_positions.bytes[run] != (u8)(run * 8))
        {
            return 54;
        }
        if (end_positions.bytes[run] != (u8)(run * 8 + 2))
        {
            return 55;
        }
        if ((u8)(end_positions.bytes[run] - start_positions.bytes[run] + 1) != 3)
        {
            return 56;
        }
    }
    // Vectors by value, in and out.
    probe.vector = vector_identity(value);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != (u8)lane)
        {
            return 58;
        }
    }
    probe.vector = vector_ninth(simd512_splat(1), simd512_splat(2), simd512_splat(3), simd512_splat(4), simd512_splat(5), simd512_splat(6), simd512_splat(7),
                                simd512_splat(8), value);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != (u8)lane)
        {
            return 59;
        }
    }
    probe.vector = vector_mixed(1, value, 2, simd512_splat(10), 3);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (probe.bytes[lane] != (u8)(lane + 10 + 6))
        {
            return 60;
        }
    }
    // A masked load past the end of the buffer must read zeros rather than the
    // bytes that happen to follow it.
    Mask64 tail = mask64_prefix(8);
    Lanes tail_probe;
    tail_probe.vector = simd512_load_masked(text, tail);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if (tail_probe.bytes[lane] != (lane < 8 ? text[lane] : 0))
        {
            return 57;
        }
    }
    // The dword compare answers one bit per u32 lane in the low sixteen and
    // zeroes the rest; a lane differing in one byte must not match, and the
    // all-ones sentinel is the byte splat because the pattern is
    // width-agnostic.
    if (simd512_equal_word(value, value) != 0xFFFF)
    {
        return 61;
    }
    Lanes word_probe;
    word_probe.vector = value;
    word_probe.bytes[4 * 5] ^= 1;
    if (simd512_equal_word(value, word_probe.vector) != (0xFFFF & ~((Mask64)1 << 5)))
    {
        return 62;
    }
    Lanes free_file;
    for (u32 word = 0; word < 16; word += 1)
    {
        free_file.words[word] = word & 1 ? 0xFFFFFFFFu : word;
    }
    if (simd512_equal_word(free_file.vector, simd512_splat(255)) != 0xAAAA)
    {
        return 63;
    }
    // The dword splat reaches all sixteen lanes with a value no byte splat
    // can spell, and the unsigned compare is strict and does not sign-extend:
    // 0x80000000 is above, not below, every small value.
    Lanes splat_probe;
    splat_probe.vector = simd512_splat_word(0x01020304u);
    for (u32 word = 0; word < 16; word += 1)
    {
        if (splat_probe.words[word] != 0x01020304u)
        {
            return 64;
        }
    }
    Lanes ascending;
    for (u32 word = 0; word < 16; word += 1)
    {
        ascending.words[word] = word;
    }
    if (simd512_less_word(ascending.vector, simd512_splat_word(5)) != 0x001F)
    {
        return 65;
    }
    if (simd512_less_word(ascending.vector, ascending.vector) != 0)
    {
        return 66;
    }
    Lanes high_bit;
    for (u32 word = 0; word < 16; word += 1)
    {
        high_bit.words[word] = 0x80000000u;
    }
    if (simd512_less_word(high_bit.vector, simd512_splat_word(1)) != 0)
    {
        return 67;
    }
    if (simd512_less_word(simd512_splat_word(1), high_bit.vector) != 0xFFFF)
    {
        return 68;
    }
    return 0;
}

#else

int main(void)
{
    return 0;
}

#endif
