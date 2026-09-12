#pragma once

#include <buster/lib/base.h>

BUSTER_F_DECL u64 buster_hash_64(u8* pointer, u64 length);

// Streaming SHA-256. init begins a message, add accepts arbitrary chunking
// (including null with zero length), finish_hex consumes the state and writes
// 64 lowercase hexadecimal digits plus a terminator. Reinitialize before reuse.
#define SHA256_HEX_CAPACITY 65
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
BUSTER_F_DECL void sha256_finish_hex(Sha256* hash, char8 result[SHA256_HEX_CAPACITY]);
