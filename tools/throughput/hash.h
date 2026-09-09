/* Streaming SHA-256 for immutable workload, compiler, and artifact identity.
 * Entry points: tp_hash_init, tp_hash_add, tp_hash_finish, tp_hash_file.
 * All arithmetic is unsigned; transform loops have fixed bounds.
 */
#ifndef BUSTER_THROUGHPUT_HASH_H
#define BUSTER_THROUGHPUT_HASH_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct TpHash
{
    uint32_t h[8];
    uint64_t bytes;
    unsigned used;
    unsigned char block[64];
} TpHash;

static uint32_t tp_ror(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

static void tp_hash_block(TpHash* hash)
{
    static uint32_t const constants[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
    {
        unsigned char const* p = hash->block + i * 4;
        w[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    }
    for (unsigned i = 16; i < 64; ++i)
    {
        uint32_t x = w[i - 15], y = w[i - 2];
        w[i] = w[i - 16] + (tp_ror(x, 7) ^ tp_ror(x, 18) ^ (x >> 3)) + w[i - 7] +
               (tp_ror(y, 17) ^ tp_ror(y, 19) ^ (y >> 10));
    }
    uint32_t a = hash->h[0], b = hash->h[1], c = hash->h[2], d = hash->h[3];
    uint32_t e = hash->h[4], f = hash->h[5], g = hash->h[6], h = hash->h[7];
    for (unsigned i = 0; i < 64; ++i)
    {
        uint32_t t1 = h + (tp_ror(e, 6) ^ tp_ror(e, 11) ^ tp_ror(e, 25)) + ((e & f) ^ (~e & g)) + constants[i] + w[i];
        uint32_t t2 = (tp_ror(a, 2) ^ tp_ror(a, 13) ^ tp_ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    hash->h[0] += a; hash->h[1] += b; hash->h[2] += c; hash->h[3] += d;
    hash->h[4] += e; hash->h[5] += f; hash->h[6] += g; hash->h[7] += h;
}

static void tp_hash_init(TpHash* hash)
{
    *hash = (TpHash){{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u},0,0,{0}};
}

static void tp_hash_add(TpHash* hash, void const* bytes, size_t size)
{
    unsigned char const* input = (unsigned char const*)bytes;
    hash->bytes += (uint64_t)size;
    while (size)
    {
        size_t part = 64 - hash->used;
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
            tp_hash_block(hash);
            hash->used = 0;
        }
    }
}

static void tp_hash_finish(TpHash* hash, char result[65])
{
    unsigned char padding[128] = {0x80};
    uint64_t bits = hash->bytes * 8;
    unsigned count = hash->used < 56 ? 56 - hash->used : 120 - hash->used;
    for (unsigned i = 0; i < 8; ++i)
    {
        padding[count + i] = (unsigned char)(bits >> (56 - i * 8));
    }
    tp_hash_add(hash, padding, count + 8);
    for (unsigned i = 0; i < 8; ++i)
    {
        snprintf(result + i * 8, 9, "%08x", (unsigned)hash->h[i]);
    }
}

static int tp_hash_file(char const* path, char digest[65], uint64_t* bytes, uint64_t* lines)
{
    FILE* file = fopen(path, "rb");
    int ok = file != NULL;
    *bytes = 0;
    *lines = 0;
    if (ok)
    {
        TpHash hash;
        unsigned char buffer[32768];
        int last = '\n';
        tp_hash_init(&hash);
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), file)) != 0)
        {
            tp_hash_add(&hash, buffer, n);
            *bytes += (uint64_t)n;
            for (size_t i = 0; i < n; ++i)
            {
                *lines += buffer[i] == '\n';
            }
            last = buffer[n - 1];
        }
        *lines += last != '\n';
        ok = !ferror(file);
        if (fclose(file) != 0)
        {
            ok = 0;
        }
        tp_hash_finish(&hash, digest);
    }
    return ok;
}
#endif
