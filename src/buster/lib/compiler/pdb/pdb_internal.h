#pragma once

// Private seam for the MSF container writer and its independent byte-reader
// tests. Stream construction stays in pdb.c; no PDB client owns this layout.
#include <buster/lib/compiler/pdb/pdb.h>

typedef struct PdbBuffer PdbBuffer;
struct PdbBuffer
{
    u8* bytes;
    u64 count;
    u64 capacity;
    bool overflow;
    u8 reserved[7];
};

BUSTER_F_DECL PdbResult pdb_msf_build(Arena* arena, PdbBuffer const* streams, u32 stream_count);
