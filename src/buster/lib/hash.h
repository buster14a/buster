#pragma once

#include <buster/lib/base.h>

BUSTER_F_DECL u64 buster_hash_64(u8* pointer, u64 length);
#if BUSTER_BENCH_ALLOCATIONS
// Calling-thread buster_hash_64 traffic for the frontend source-fact census
// (compiler/frontend/c/c_census.h). Cumulative; reporting never resets it.
typedef struct BusterHashCensus BusterHashCensus;
struct BusterHashCensus
{
    u64 calls;
    u64 bytes;
};
BUSTER_F_DECL BusterHashCensus buster_hash_census(void);
#endif

// Streaming SHA-256. init begins a message, add accepts arbitrary chunking
// (including null with zero length), finish_hex consumes the state and writes
// 64 lowercase hexadecimal digits plus a terminator; finish writes the raw 32
// bytes and sha256_bytes hashes one contiguous message. This is the single
// SHA-256 implementation for the compiler and linker (build.c-side tools keep
// their own because they cannot include lib headers). Reinitialize before reuse.
#define SHA256_HEX_CAPACITY 65
#define SHA256_DIGEST_SIZE 32
typedef struct Sha256 Sha256;
struct Sha256
{
    u32 h[8];
    u64 bytes;
    u32 used;
    u8 block[64];
    u32 reserved;
};
BUSTER_F_DECL void sha256_init(Sha256* hash);
BUSTER_F_DECL void sha256_add(Sha256* hash, void const* bytes, u64 size);
BUSTER_F_DECL void sha256_finish(Sha256* hash, u8 result[SHA256_DIGEST_SIZE]);
BUSTER_F_DECL void sha256_bytes(void const* bytes, u64 size, u8 result[SHA256_DIGEST_SIZE]);
BUSTER_F_DECL void sha256_finish_hex(Sha256* hash, char8 result[SHA256_HEX_CAPACITY]);
