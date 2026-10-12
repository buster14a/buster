#pragma once

// Private seam for the MSF container writer and its independent byte-reader
// tests. Stream construction stays in pdb.c; no PDB client owns this layout.
#include <buster/lib/compiler/pdb/pdb.h>
#include <buster/lib/byte_writer.h>

BUSTER_F_DECL PdbResult pdb_msf_build(Arena* arena, ByteWriter const* streams, u32 stream_count);

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/string.h>

BUSTER_F_DECL u32 pdb_test_gsi_order(Arena* arena, SliceString8 names, u32* order, u64* comparisons);
#endif
