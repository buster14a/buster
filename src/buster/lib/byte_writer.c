// Bounded byte writing, shared by CodeView and PDB. byte_writer_range owns
// subtraction-based range checks and the sticky failure state; emit/patch
// helpers encode little-endian fields. byte_writer_commit is the publication
// boundary. Format layout, diagnostics and arena ownership stay with callers.

#include <buster/lib/byte_writer.h>
#include <string.h>

ByteWriter byte_writer_make(u8* bytes, u64 capacity)
{
    ByteWriter result = {.bytes = bytes, .capacity = capacity, .overflow = capacity && !bytes};
    return result;
}

BUSTER_GLOBAL_LOCAL bool byte_writer_range(ByteWriter* writer, u64 limit, u64 offset, u64 size)
{
    bool valid = !writer->overflow && writer->count <= writer->capacity && (!writer->capacity || writer->bytes) &&
                 offset <= limit && size <= limit - offset;
    writer->overflow = !valid;
    return valid;
}

void byte_writer_emit_bytes(ByteWriter* writer, void const* source, u64 size)
{
    writer->overflow |= size && !source;
    if (byte_writer_range(writer, writer->capacity, writer->count, size))
    {
        if (size)
        {
            memcpy(writer->bytes + writer->count, source, size);
        }
        writer->count += size;
    }
}

void byte_writer_emit_zero(ByteWriter* writer, u64 size)
{
    if (byte_writer_range(writer, writer->capacity, writer->count, size))
    {
        if (size)
        {
            memset(writer->bytes + writer->count, 0, size);
        }
        writer->count += size;
    }
}

void byte_writer_emit_u8(ByteWriter* writer, u8 value)
{
    byte_writer_emit_bytes(writer, &value, sizeof(value));
}

void byte_writer_emit_u16_le(ByteWriter* writer, u16 value)
{
    u8 bytes[] = {(u8)value, (u8)(value >> 8)};
    byte_writer_emit_bytes(writer, bytes, sizeof(bytes));
}

void byte_writer_emit_u32_le(ByteWriter* writer, u32 value)
{
    u8 bytes[] = {(u8)value, (u8)(value >> 8), (u8)(value >> 16), (u8)(value >> 24)};
    byte_writer_emit_bytes(writer, bytes, sizeof(bytes));
}

void byte_writer_patch_bytes(ByteWriter* writer, u64 offset, void const* source, u64 size)
{
    writer->overflow |= size && !source;
    if (byte_writer_range(writer, writer->count, offset, size) && size)
    {
        memcpy(writer->bytes + offset, source, size);
    }
}

void byte_writer_patch_u16_le(ByteWriter* writer, u64 offset, u16 value)
{
    u8 bytes[] = {(u8)value, (u8)(value >> 8)};
    byte_writer_patch_bytes(writer, offset, bytes, sizeof(bytes));
}

void byte_writer_patch_u32_le(ByteWriter* writer, u64 offset, u32 value)
{
    u8 bytes[] = {(u8)value, (u8)(value >> 8), (u8)(value >> 16), (u8)(value >> 24)};
    byte_writer_patch_bytes(writer, offset, bytes, sizeof(bytes));
}

void byte_writer_align4(ByteWriter* writer)
{
    byte_writer_emit_zero(writer, (4 - (writer->count & 3)) & 3);
}

bool byte_writer_commit(ByteWriter const* writer, ByteSlice* output)
{
    bool valid = !writer->overflow && writer->count <= writer->capacity && (!writer->capacity || writer->bytes);
    if (valid)
    {
        *output = (ByteSlice){.pointer = writer->bytes, .length = writer->count};
    }
    return valid;
}
