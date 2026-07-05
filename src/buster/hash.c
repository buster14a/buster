#include <buster/hash.h>
#ifndef USE_XXHASH
#define USE_XXHASH 0
#endif
#if USE_XXHASH
#define XXH_IMPLEMENTATION
#define XXH_STATIC_LINKING_ONLY
#define XXH_INLINE_ALL
#if defined(__TINYC__)
#define XXH_VECTOR 0
#endif
#include <xxhash/xxhash.h>
#else
BUSTER_GLOBAL_LOCAL BUSTER_INLINE u64 buster_hash_read_u64(u8* pointer)
{
    u64 result = 0;
    memcpy(&result, pointer, sizeof(result));
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 buster_hash_read_u32(u8* pointer)
{
    u32 result = 0;
    memcpy(&result, pointer, sizeof(result));
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u64 buster_hash_rotl64(u64 value, u8 amount)
{
    return (value << amount) | (value >> (64 - amount));
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u64 buster_hash_round(u64 acc, u64 input)
{
    acc += input * 14029467366897019727ULL;
    acc = buster_hash_rotl64(acc, 31);
    acc *= 11400714785074694791ULL;
    return acc;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u64 buster_hash_merge_round(u64 acc, u64 value)
{
    acc ^= buster_hash_round(0, value);
    acc = acc * 11400714785074694791ULL + 9650029242287828579ULL;
    return acc;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u64 buster_hash_avalanche(u64 hash)
{
    hash ^= hash >> 33;
    hash *= 14029467366897019727ULL;
    hash ^= hash >> 29;
    hash *= 1609587929392839161ULL;
    hash ^= hash >> 32;
    return hash;
}
#endif

u64 buster_hash_64(u8* pointer, u64 length)
{
#if USE_XXHASH
    return XXH3_64bits(pointer, length);
#else
    if (length == 0)
    {
        u64 empty_hash = buster_hash_avalanche(2870177450012600261ULL);
        return empty_hash ? empty_hash : 1;
    }

    u8* current = pointer;
    u8* end = pointer + length;
    u64 hash = 0;

    if (length >= 32)
    {
        u8* limit = end - 32;
        u64 v1 = 11400714785074694791ULL + 14029467366897019727ULL;
        u64 v2 = 14029467366897019727ULL;
        u64 v3 = 0;
        u64 v4 = 0 - 11400714785074694791ULL;

        do
        {
            v1 = buster_hash_round(v1, buster_hash_read_u64(current));
            current += 8;
            v2 = buster_hash_round(v2, buster_hash_read_u64(current));
            current += 8;
            v3 = buster_hash_round(v3, buster_hash_read_u64(current));
            current += 8;
            v4 = buster_hash_round(v4, buster_hash_read_u64(current));
            current += 8;
        } while (current <= limit);

        hash = buster_hash_rotl64(v1, 1) +
               buster_hash_rotl64(v2, 7) +
               buster_hash_rotl64(v3, 12) +
               buster_hash_rotl64(v4, 18);

        hash = buster_hash_merge_round(hash, v1);
        hash = buster_hash_merge_round(hash, v2);
        hash = buster_hash_merge_round(hash, v3);
        hash = buster_hash_merge_round(hash, v4);
    }
    else
    {
        hash = 2870177450012600261ULL;
    }

    hash += length;

    while ((u64)(end - current) >= 8)
    {
        u64 k1 = buster_hash_round(0, buster_hash_read_u64(current));
        hash ^= k1;
        hash = buster_hash_rotl64(hash, 27) * 11400714785074694791ULL + 9650029242287828579ULL;
        current += 8;
    }

    if ((u64)(end - current) >= 4)
    {
        hash ^= (u64)buster_hash_read_u32(current) * 11400714785074694791ULL;
        hash = buster_hash_rotl64(hash, 23) * 14029467366897019727ULL + 1609587929392839161ULL;
        current += 4;
    }

    while (current < end)
    {
        hash ^= (u64)*current * 2870177450012600261ULL;
        hash = buster_hash_rotl64(hash, 11) * 11400714785074694791ULL;
        current += 1;
    }

    hash = buster_hash_avalanche(hash);
    return hash ? hash : 1;
#endif
}
