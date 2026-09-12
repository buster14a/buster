#pragma once

// Private seam for the MSF container writer and its independent byte-reader
// tests. Stream construction stays in pdb.c; no PDB client owns this layout.
#include <buster/lib/compiler/pdb/pdb.h>
#include <buster/lib/byte_writer.h>

BUSTER_F_DECL PdbResult pdb_msf_build(Arena* arena, ByteWriter const* streams, u32 stream_count);
