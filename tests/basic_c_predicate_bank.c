// Multiple independent predicates feed a mask-only chain and a masked store.
// The integer return separately checks the 16-lane predicate boundary.
typedef unsigned char u8;
typedef unsigned long long u64;
typedef u8 Simd512 __attribute__((vector_size(64)));

void predicate_mask_chain(u8* output, u8 const* input)
{
    Simd512 bytes = __builtin_buster_simd_load(input);
    u64 a = __builtin_buster_simd_equal_byte(bytes, __builtin_buster_simd_splat_byte(3));
    u64 b = __builtin_buster_simd_equal_byte(bytes, __builtin_buster_simd_splat_byte(7));
    u64 c = __builtin_buster_simd_less_byte(bytes, __builtin_buster_simd_splat_byte(11));
    __builtin_buster_simd_store_masked(output, (a | b) & c, bytes);
}

u64 predicate_word_boundary(u8 const* input)
{
    Simd512 words = __builtin_buster_simd_load(input);
    return __builtin_buster_simd_equal_word(words, words);
}
