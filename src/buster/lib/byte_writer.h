#pragma once

// Non-owning writes into one pre-reserved span. Allocation, growth and lifetime
// belong to the caller: the writer never allocates or changes its backing span.
// Append, patch and alignment operations commit all requested bytes or none.
// A failed operation preserves bytes/count and latches overflow; all later
// operations are inert. Patches may only address the already-written prefix.
// Empty operations accept null data without pointer arithmetic or memory calls.
// Source and destination of a nonempty copy must not overlap.
// byte_writer_commit publishes the prefix only on success, leaving the caller's
// output unchanged on failure. It neither copies nor transfers storage ownership.

#include <buster/lib/base.h>

typedef struct ByteWriter ByteWriter;
struct ByteWriter
{
    u8* bytes;
    u64 count;
    u64 capacity;
    bool overflow;
    u8 reserved[7];
};

BUSTER_F_DECL ByteWriter byte_writer_make(u8* bytes, u64 capacity);
BUSTER_F_DECL void byte_writer_emit_bytes(ByteWriter* writer, void const* source, u64 size);
BUSTER_F_DECL void byte_writer_emit_zero(ByteWriter* writer, u64 size);
BUSTER_F_DECL void byte_writer_emit_u8(ByteWriter* writer, u8 value);
BUSTER_F_DECL void byte_writer_emit_u16_le(ByteWriter* writer, u16 value);
BUSTER_F_DECL void byte_writer_emit_u32_le(ByteWriter* writer, u32 value);
BUSTER_F_DECL void byte_writer_patch_bytes(ByteWriter* writer, u64 offset, void const* source, u64 size);
BUSTER_F_DECL void byte_writer_patch_u16_le(ByteWriter* writer, u64 offset, u16 value);
BUSTER_F_DECL void byte_writer_patch_u32_le(ByteWriter* writer, u64 offset, u32 value);
BUSTER_F_DECL void byte_writer_align4(ByteWriter* writer);
BUSTER_F_DECL bool byte_writer_commit(ByteWriter const* writer, ByteSlice* output);
