// Shared hash algorithms: buster_hash_64 for table keys, sha256_* for
// streaming byte identities. Neither interface substitutes for the other.
#include <buster/lib/hash.h>
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
    // Callers may reserve 0 as an "empty" sentinel; the non-xxhash path below
    // never returns it, so keep both paths consistent.
    u64 xxhash = XXH3_64bits(pointer, length);
    return xxhash ? xxhash : 1;
#else
    u64 hash;
    if (length == 0)
    {
        hash = buster_hash_avalanche(2870177450012600261ULL);
    }
    else
    {
        // The cursor is a byte offset rather than a pointer: comparing derived
        // pointers (P + C1 < P + C2) trips gcc's -Wstrict-overflow=5 whenever the
        // length constant-propagates into a specialized clone.
        u64 i = 0;

        if (length >= 32)
        {
            u64 v1 = 11400714785074694791ULL + 14029467366897019727ULL;
            u64 v2 = 14029467366897019727ULL;
            u64 v3 = 0;
            u64 v4 = 0 - 11400714785074694791ULL;

            do
            {
                v1 = buster_hash_round(v1, buster_hash_read_u64(pointer + i));
                i += 8;
                v2 = buster_hash_round(v2, buster_hash_read_u64(pointer + i));
                i += 8;
                v3 = buster_hash_round(v3, buster_hash_read_u64(pointer + i));
                i += 8;
                v4 = buster_hash_round(v4, buster_hash_read_u64(pointer + i));
                i += 8;
            } while (length - i >= 32);

            hash = buster_hash_rotl64(v1, 1) + buster_hash_rotl64(v2, 7) + buster_hash_rotl64(v3, 12) + buster_hash_rotl64(v4, 18);

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

        while (length - i >= 8)
        {
            u64 k1 = buster_hash_round(0, buster_hash_read_u64(pointer + i));
            hash ^= k1;
            hash = buster_hash_rotl64(hash, 27) * 11400714785074694791ULL + 9650029242287828579ULL;
            i += 8;
        }

        if (length - i >= 4)
        {
            hash ^= (u64)buster_hash_read_u32(pointer + i) * 11400714785074694791ULL;
            hash = buster_hash_rotl64(hash, 23) * 14029467366897019727ULL + 1609587929392839161ULL;
            i += 4;
        }

        while (i < length)
        {
            hash ^= (u64)pointer[i] * 2870177450012600261ULL;
            hash = buster_hash_rotl64(hash, 11) * 11400714785074694791ULL;
            i += 1;
        }

        hash = buster_hash_avalanche(hash);
    }

    return hash ? hash : 1;
#endif
}

BUSTER_GLOBAL_LOCAL u32 sha256_ror(u32 x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

BUSTER_GLOBAL_LOCAL void sha256_block(Sha256* hash)
{
    BUSTER_GLOBAL_LOCAL u32 const constants[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    u32 w[64];
    for (unsigned i = 0; i < 16; ++i)
    {
        u8 const* p = hash->block + i * 4;
        w[i] = (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
    }
    for (unsigned i = 16; i < 64; ++i)
    {
        u32 x = w[i - 15], y = w[i - 2];
        w[i] = w[i - 16] + (sha256_ror(x, 7) ^ sha256_ror(x, 18) ^ (x >> 3)) + w[i - 7] +
               (sha256_ror(y, 17) ^ sha256_ror(y, 19) ^ (y >> 10));
    }
    u32 a = hash->h[0], b = hash->h[1], c = hash->h[2], d = hash->h[3];
    u32 e = hash->h[4], f = hash->h[5], g = hash->h[6], h = hash->h[7];
    for (unsigned i = 0; i < 64; ++i)
    {
        u32 t1 = h + (sha256_ror(e, 6) ^ sha256_ror(e, 11) ^ sha256_ror(e, 25)) + ((e & f) ^ (~e & g)) + constants[i] + w[i];
        u32 t2 = (sha256_ror(a, 2) ^ sha256_ror(a, 13) ^ sha256_ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    hash->h[0] += a; hash->h[1] += b; hash->h[2] += c; hash->h[3] += d;
    hash->h[4] += e; hash->h[5] += f; hash->h[6] += g; hash->h[7] += h;
}

void sha256_init(Sha256* hash)
{
    *hash = (Sha256){{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u},0,0,{0},0};
}

void sha256_add(Sha256* hash, void const* bytes, u64 size)
{
    u8 const* input = (u8 const*)bytes;
    hash->bytes += size;
    while (size)
    {
        u64 part = 64 - hash->used;
        if (part > size)
        {
            part = size;
        }
        memcpy(hash->block + hash->used, input, part);
        hash->used += (unsigned)part;
        input += part;
        size -= part;
        if (hash->used == 64)
        {
            sha256_block(hash);
            hash->used = 0;
        }
    }
}

void sha256_finish_hex(Sha256* hash, char8 result[SHA256_HEX_CAPACITY])
{
    u8 padding[128] = {0x80};
    u64 bits = hash->bytes * 8;
    unsigned count = hash->used < 56 ? 56 - hash->used : 120 - hash->used;
    for (unsigned i = 0; i < 8; ++i)
    {
        padding[count + i] = (u8)(bits >> (56 - i * 8));
    }
    sha256_add(hash, padding, count + 8);
    for (unsigned i = 0; i < 8; ++i)
    {
        for (u32 digit = 0; digit < 8; digit += 1)
        {
            result[i * 8 + digit] = "0123456789abcdef"[(hash->h[i] >> (28 - digit * 4)) & 15];
        }
    }
    result[64] = 0;
}
