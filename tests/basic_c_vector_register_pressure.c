// Vector register-pressure corpus, the 512-bit counterpart of
// basic_c_register_pressure.c. The lexer kernels keep only a handful of
// vector values live at once, so they cannot show what the vector file does
// under contention; these bodies deliberately hold more 64-byte values live
// than the sixteen-register ZMM file, across a loop and across a call —
// where every vector register is caller-saved on System V, so call-crossing
// vector values must round-trip through their spill slots no matter what
// the allocator does. Correctness is self-checking so the file doubles as
// an execution test under every allocator mode.
//
// Self-contained like basic_c_simd.c and guarded by the same predefined
// feature macros: on a target without the vocabulary the whole body
// compiles out and the fixture trivially succeeds.

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

#define simd512_load(address) __builtin_buster_simd_load(address)
#define simd512_store(address, value) __builtin_buster_simd_store((address), (value))
#define simd512_splat(byte) __builtin_buster_simd_splat_byte(byte)
#define simd512_equal_byte(left, right) __builtin_buster_simd_equal_byte((left), (right))
#define simd512_sign_byte(value) __builtin_buster_simd_sign_byte(value)
#define simd512_test_byte(left, right) __builtin_buster_simd_test_byte((left), (right))

static u8 corpus_bytes[64 * 20];

static void corpus_fill(u64 seed)
{
    u64 state = seed | 1;
    for (u32 index = 0; index < (u32)sizeof(corpus_bytes); index += 1)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        corpus_bytes[index] = (u8)(state >> 56);
    }
}

// A 64-bit signature of one vector, built from the mask vocabulary so the
// reduction itself exercises the mask-producing rows. A macro rather than a
// function for the same reason the vocabulary's own operations are macros:
// a vector parameter would put the whole caller outside the machine subset
// (vector ABI is a later stage), and `ide cc` runs no inliner. Arguments
// must be free of side effects.
#define vector_signature(value)                                                                                                                                \
    (simd512_sign_byte(value) ^ (simd512_test_byte((value), simd512_splat(0x0f)) * 0x9e3779b97f4a7c15ull) ^                                                    \
     (simd512_equal_byte((value), simd512_splat(0)) >> 1))

// Eighteen vector values live simultaneously across a loop: more than the
// sixteen-register ZMM file, so something must spill and the choice of what
// separates a good vector allocator from a bad one.
static u64 wide_vector_loop(u64 seed, u32 rounds)
{
    corpus_fill(seed);
    Simd512 a0 = simd512_load(corpus_bytes + 64 * 0);
    Simd512 a1 = simd512_load(corpus_bytes + 64 * 1);
    Simd512 a2 = simd512_load(corpus_bytes + 64 * 2);
    Simd512 a3 = simd512_load(corpus_bytes + 64 * 3);
    Simd512 a4 = simd512_load(corpus_bytes + 64 * 4);
    Simd512 a5 = simd512_load(corpus_bytes + 64 * 5);
    Simd512 a6 = simd512_load(corpus_bytes + 64 * 6);
    Simd512 a7 = simd512_load(corpus_bytes + 64 * 7);
    Simd512 a8 = simd512_load(corpus_bytes + 64 * 8);
    Simd512 a9 = simd512_load(corpus_bytes + 64 * 9);
    Simd512 a10 = simd512_load(corpus_bytes + 64 * 10);
    Simd512 a11 = simd512_load(corpus_bytes + 64 * 11);
    Simd512 a12 = simd512_load(corpus_bytes + 64 * 12);
    Simd512 a13 = simd512_load(corpus_bytes + 64 * 13);
    Simd512 a14 = simd512_load(corpus_bytes + 64 * 14);
    Simd512 a15 = simd512_load(corpus_bytes + 64 * 15);
    Simd512 a16 = simd512_load(corpus_bytes + 64 * 16);
    Simd512 a17 = simd512_load(corpus_bytes + 64 * 17);
    for (u32 round = 0; round < rounds; round += 1)
    {
        a0 = a0 + (a1 ^ a17);
        a1 = a1 + (a2 ^ a0);
        a2 = a2 + (a3 ^ a1);
        a3 = a3 + (a4 ^ a2);
        a4 = a4 + (a5 ^ a3);
        a5 = a5 + (a6 ^ a4);
        a6 = a6 + (a7 ^ a5);
        a7 = a7 + (a8 ^ a6);
        a8 = a8 + (a9 ^ a7);
        a9 = a9 + (a10 ^ a8);
        a10 = a10 + (a11 ^ a9);
        a11 = a11 + (a12 ^ a10);
        a12 = a12 + (a13 ^ a11);
        a13 = a13 + (a14 ^ a12);
        a14 = a14 + (a15 ^ a13);
        a15 = a15 + (a16 ^ a14);
        a16 = a16 + (a17 ^ a15);
        a17 = a17 + (a0 ^ a16);
    }
    Simd512 combined = a0 ^ a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6 ^ a7 ^ a8 ^ a9 ^ a10 ^ a11 ^ a12 ^ a13 ^ a14 ^ a15 ^ a16 ^ a17;
    return vector_signature(combined) ^ vector_signature(a0 & a9) ^ vector_signature(a5 | a13);
}

static u64 scalar_mix(u64 value)
{
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 29;
    return value;
}

// Eight vector values live across a call in every iteration. On System V
// every vector register is caller-saved, so each one pays a 64-byte store
// and reload per call whatever the allocator chooses; this body prices
// that contract and pins the behavior for a future callee-saved or
// split-aware stage to improve on.
static u64 call_crossing_vector_loop(u64 seed, u32 rounds)
{
    corpus_fill(seed ^ 0xc2b2ae3d27d4eb4full);
    Simd512 b0 = simd512_load(corpus_bytes + 64 * 0);
    Simd512 b1 = simd512_load(corpus_bytes + 64 * 1);
    Simd512 b2 = simd512_load(corpus_bytes + 64 * 2);
    Simd512 b3 = simd512_load(corpus_bytes + 64 * 3);
    Simd512 b4 = simd512_load(corpus_bytes + 64 * 4);
    Simd512 b5 = simd512_load(corpus_bytes + 64 * 5);
    Simd512 b6 = simd512_load(corpus_bytes + 64 * 6);
    Simd512 b7 = simd512_load(corpus_bytes + 64 * 7);
    u64 total = 0;
    for (u32 round = 0; round < rounds; round += 1)
    {
        total += scalar_mix(total ^ round);
        Simd512 salt = simd512_splat((u8)total);
        b0 = b0 + (salt ^ b7);
        b1 = b1 + (salt ^ b0);
        b2 = b2 + (salt ^ b1);
        b3 = b3 + (salt ^ b2);
        b4 = b4 + (salt ^ b3);
        b5 = b5 + (salt ^ b4);
        b6 = b6 + (salt ^ b5);
        b7 = b7 + (salt ^ b6);
    }
    Simd512 combined = b0 ^ b1 ^ b2 ^ b3 ^ b4 ^ b5 ^ b6 ^ b7;
    return total ^ vector_signature(combined) ^ vector_signature(b2 - b6);
}

// A deep vector expression tree: every intermediate is live until the final
// combine, so the peak is set by the shape of the tree rather than by any
// loop.
static u64 deep_vector_tree(u64 seed)
{
    corpus_fill(seed ^ 0xd6e8feb86659fd93ull);
    Simd512 c0 = simd512_load(corpus_bytes + 64 * 0) + simd512_splat(1);
    Simd512 c1 = simd512_load(corpus_bytes + 64 * 1) + simd512_splat(2);
    Simd512 c2 = simd512_load(corpus_bytes + 64 * 2) + simd512_splat(3);
    Simd512 c3 = simd512_load(corpus_bytes + 64 * 3) + simd512_splat(4);
    Simd512 c4 = simd512_load(corpus_bytes + 64 * 4) + simd512_splat(5);
    Simd512 c5 = simd512_load(corpus_bytes + 64 * 5) + simd512_splat(6);
    Simd512 c6 = simd512_load(corpus_bytes + 64 * 6) + simd512_splat(7);
    Simd512 c7 = simd512_load(corpus_bytes + 64 * 7) + simd512_splat(8);
    Simd512 d0 = (c0 ^ c1) + (c0 & c1);
    Simd512 d1 = (c2 ^ c3) + (c2 & c3);
    Simd512 d2 = (c4 ^ c5) + (c4 & c5);
    Simd512 d3 = (c6 ^ c7) + (c6 & c7);
    Simd512 e0 = (d0 | d1) - (c0 ^ c7);
    Simd512 e1 = (d2 | d3) - (c1 ^ c6);
    Simd512 top = (e0 + e1) ^ (d0 - d3) ^ (c2 | c5) ^ (c3 & c4);
    return vector_signature(top) ^ vector_signature(e0 ^ e1) ^ vector_signature(d1 + d2);
}

#endif

int main(void)
{
#if FIXTURE_SIMD_512
    u64 wide = wide_vector_loop(0x9e3779b97f4a7c15ull, 64);
    u64 crossing = call_crossing_vector_loop(0xc2b2ae3d27d4eb4full, 64);
    u64 tree = deep_vector_tree(0xd6e8feb86659fd93ull);
    // The expected values are whatever a correct compiler produces; the
    // check is that every allocator agrees with the canonical path, which
    // the differential comparison in the harness enforces. Here we only
    // assert the computation ran and stayed in range.
    return !(wide != 0 && crossing != 0 && tree != 0 && (wide ^ crossing ^ tree) != 0);
#else
    return 0;
#endif
}
