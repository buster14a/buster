#include <buster/compiler/pdb/pdb.h>
#include <buster/string.h>
#include <buster/integer.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/file.h>
// The test drives the real CodeView emitter so the split logic is exercised
// against the exact blob the object writers produce.
#include <buster/compiler/codeview/codeview.h>
#endif

#include <string.h>

enum
{
    PDB_BLOCK_SIZE = 4096,
    PDB_FREE_BLOCK_MAP = 2,
    PDB_FIRST_DATA_BLOCK = 3,
};

// Stream numbers 0-4 are fixed by the format; the rest are ours to assign.
enum
{
    PDB_STREAM_OLD_DIRECTORY,
    PDB_STREAM_INFO,
    PDB_STREAM_TPI,
    PDB_STREAM_DBI,
    PDB_STREAM_IPI,
    PDB_STREAM_GLOBALS,
    PDB_STREAM_PUBLICS,
    PDB_STREAM_SYMBOL_RECORDS,
    PDB_STREAM_SECTION_HEADERS,
    PDB_STREAM_MODULE,
    PDB_STREAM_NAMES,
    PDB_STREAM_COUNT,
};

enum
{
    PDB_INFO_VERSION_VC70 = 20000404,
    PDB_FEATURE_VC140 = 20140508,
    PDB_TPI_VERSION_V80 = 20040203,
    PDB_DBI_VERSION_V70 = 19990903,
    PDB_TPI_HEADER_SIZE = 56,
    PDB_DBI_HEADER_SIZE = 64,
    PDB_SECTION_CONTRIBUTION_SIZE = 28,
    PDB_DBG_HEADER_COUNT = 11,
    PDB_DBG_HEADER_SECTION_HEADERS = 5,
    PDB_GSI_HASH_HEADER_SIZE = 16,
    PDB_PUBLICS_HEADER_SIZE = 28,
    PDB_NAMED_STREAM_CAPACITY = 8,
    PDB_CHECKSUM_ENTRY_SIZE = 8,
};

#define PDB_STRING_TABLE_SIGNATURE 0xeffeeffeu

#define PDB_GSI_HASH_VERSION 0xf12f091au

#define PDB_SECTION_CONTRIBUTION_VERSION 0xf12eba2du

enum
{
    PDB_DEBUG_S_SYMBOLS = 0xf1,
    PDB_DEBUG_S_STRINGTABLE = 0xf3,
    PDB_DEBUG_S_FILECHKSMS = 0xf4,
};

typedef struct PdbBuffer PdbBuffer;
struct PdbBuffer
{
    u8* bytes;
    u64 count;
    u64 capacity;
    bool overflow;
    u8 reserved[7];
};

BUSTER_GLOBAL_LOCAL void pdb_emit_bytes(PdbBuffer* buffer, void const* source, u64 size)
{
    if (buffer->count + size > buffer->capacity)
    {
        buffer->overflow = true;
        return;
    }
    if (size)
    {
        memcpy(buffer->bytes + buffer->count, source, size);
    }
    buffer->count += size;
}

BUSTER_GLOBAL_LOCAL void pdb_emit_zero(PdbBuffer* buffer, u64 size)
{
    if (buffer->count + size > buffer->capacity)
    {
        buffer->overflow = true;
        return;
    }
    memset(buffer->bytes + buffer->count, 0, size);
    buffer->count += size;
}

BUSTER_GLOBAL_LOCAL void pdb_emit_u16(PdbBuffer* buffer, u16 value)
{
    pdb_emit_bytes(buffer, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void pdb_emit_u32(PdbBuffer* buffer, u32 value)
{
    pdb_emit_bytes(buffer, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void pdb_emit_align4(PdbBuffer* buffer)
{
    while (buffer->count & 3)
    {
        pdb_emit_zero(buffer, 1);
    }
}

BUSTER_GLOBAL_LOCAL void pdb_write_u32_at(PdbBuffer* buffer, u64 offset, u32 value)
{
    if (offset + sizeof(value) > buffer->count)
    {
        buffer->overflow = true;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL u32 pdb_read_u32(ByteSlice bytes, u64 offset)
{
    u32 value = 0;
    memcpy(&value, bytes.pointer + offset, sizeof(value));
    return value;
}

// The bucket a named stream lands in is derived from this hash, so readers
// probing for a name find it. Mirrors the classic PDB string hash.
BUSTER_GLOBAL_LOCAL u32 pdb_hash_string_v1(String8 text)
{
    u32 result = 0;
    u64 index = 0;
    for (; index + 4 <= text.length; index += 4)
    {
        u32 chunk = 0;
        memcpy(&chunk, text.pointer + index, sizeof(chunk));
        result ^= chunk;
    }
    if (text.length - index >= 2)
    {
        u16 chunk = 0;
        memcpy(&chunk, text.pointer + index, sizeof(chunk));
        result ^= chunk;
        index += 2;
    }
    if (text.length - index == 1)
    {
        result ^= (u8)text.pointer[index];
    }
    result |= 0x20202020u;
    result ^= result >> 11;
    result ^= result >> 16;
    return result;
}

// Splits a C13 blob: symbol subsection payloads become the module stream's
// symbol region, and every other subsection is copied verbatim into its C13
// region.
typedef struct PdbCodeviewSplit PdbCodeviewSplit;
struct PdbCodeviewSplit
{
    ByteSlice symbols;
    ByteSlice c13;
    ByteSlice string_table;
    ByteSlice checksums;
    bool valid;
    u8 reserved[7];
};

BUSTER_GLOBAL_LOCAL PdbCodeviewSplit pdb_split_codeview(Arena* arena, ByteSlice blob)
{
    PdbCodeviewSplit result = {0};
    if (blob.length < 4)
    {
        return result;
    }
    u8* symbol_bytes = arena_allocate(arena, u8, blob.length);
    u8* c13_bytes = arena_allocate(arena, u8, blob.length);
    u64 symbol_count = 0;
    u64 c13_count = 0;
    u64 offset = 4;
    while (offset + 8 <= blob.length)
    {
        u32 kind = pdb_read_u32(blob, offset);
        u32 length = pdb_read_u32(blob, offset + 4);
        u64 payload = offset + 8;
        if (length > blob.length - payload)
        {
            return result;
        }
        if (kind == PDB_DEBUG_S_SYMBOLS)
        {
            memcpy(symbol_bytes + symbol_count, blob.pointer + payload, length);
            symbol_count += length;
        }
        else
        {
            memcpy(c13_bytes + c13_count, blob.pointer + offset, 8 + (u64)length);
            c13_count += 8 + (u64)length;
            while (c13_count & 3)
            {
                c13_bytes[c13_count++] = 0;
            }
            if (kind == PDB_DEBUG_S_STRINGTABLE)
            {
                result.string_table = (ByteSlice){
                    .pointer = blob.pointer + payload,
                    .length = length,
                };
            }
            else if (kind == PDB_DEBUG_S_FILECHKSMS)
            {
                result.checksums = (ByteSlice){
                    .pointer = blob.pointer + payload,
                    .length = length,
                };
            }
        }
        offset = payload + ((length + 3u) & ~3u);
    }
    result.symbols = (ByteSlice){
        .pointer = symbol_bytes,
        .length = symbol_count,
    };
    result.c13 = (ByteSlice){
        .pointer = c13_bytes,
        .length = c13_count,
    };
    result.valid = true;
    return result;
}


// A PDB's checksum entries index the global /names stream rather than the
// object-local string table, so the C13 region is rebuilt with remapped
// offsets and the now-redundant string table dropped.
BUSTER_GLOBAL_LOCAL ByteSlice pdb_rebuild_c13(Arena* arena, ByteSlice blob, u32 const* names_offsets, u32 file_count)
{
    ByteSlice result = {0};
    u8* bytes = arena_allocate(arena, u8, blob.length + 8);
    u64 count = 0;
    u64 offset = 4;
    while (offset + 8 <= blob.length)
    {
        u32 kind = pdb_read_u32(blob, offset);
        u32 length = pdb_read_u32(blob, offset + 4);
        u64 payload = offset + 8;
        if (length > blob.length - payload)
        {
            return result;
        }
        if (kind != PDB_DEBUG_S_SYMBOLS && kind != PDB_DEBUG_S_STRINGTABLE)
        {
            u64 start = count;
            memcpy(bytes + count, blob.pointer + offset, 8 + (u64)length);
            count += 8 + (u64)length;
            if (kind == PDB_DEBUG_S_FILECHKSMS)
            {
                for (u32 file_index = 0; file_index < length / PDB_CHECKSUM_ENTRY_SIZE; file_index += 1)
                {
                    u32 mapped = file_index < file_count ? names_offsets[file_index] : 0;
                    memcpy(bytes + start + 8 + (u64)file_index * PDB_CHECKSUM_ENTRY_SIZE, &mapped, sizeof(mapped));
                }
            }
            while (count & 3)
            {
                bytes[count++] = 0;
            }
        }
        offset = payload + ((length + 3u) & ~3u);
    }
    result = (ByteSlice){
        .pointer = bytes,
        .length = count,
    };
    return result;
}

PdbResult pdb_build(Arena* arena, PdbInput input)
{
    PdbResult result = {0};
    if (!arena || !input.section_count || !input.sections || !input.codeview_symbols.length)
    {
        return result;
    }
    PdbCodeviewSplit split = pdb_split_codeview(arena, input.codeview_symbols);
    if (!split.valid)
    {
        return result;
    }
    String8 module_name = input.module_name.length ? input.module_name : S8("buster.obj");

    // Source file names come from the checksum table's string-table offsets,
    // so the DBI file list matches the line tables exactly.
    u32 source_file_count = split.checksums.length ? (u32)(split.checksums.length / PDB_CHECKSUM_ENTRY_SIZE) : 0;
    String8* source_names = arena_allocate(arena, String8, source_file_count ? source_file_count : 1);
    u32* names_offsets = arena_allocate(arena, u32, source_file_count ? source_file_count : 1);
    u64 source_names_size = 0;
    // The /names buffer opens with an empty string, so offset zero is unused.
    u64 names_buffer_size = 1;
    for (u32 file_index = 0; file_index < source_file_count; file_index += 1)
    {
        u32 string_offset = pdb_read_u32(split.checksums, (u64)file_index * PDB_CHECKSUM_ENTRY_SIZE);
        if (string_offset >= split.string_table.length)
        {
            source_file_count = file_index;
            break;
        }
        u64 length = 0;
        while (string_offset + length < split.string_table.length && split.string_table.pointer[string_offset + length])
        {
            length += 1;
        }
        source_names[file_index] = (String8){
            .pointer = (char8*)(split.string_table.pointer + string_offset),
            .length = length,
        };
        names_offsets[file_index] = (u32)names_buffer_size;
        names_buffer_size += length + 1;
        source_names_size += length + 1;
    }
    ByteSlice c13 = pdb_rebuild_c13(arena, input.codeview_symbols, names_offsets, source_file_count);
    if (!c13.pointer)
    {
        return result;
    }
    split.c13 = c13;

    u64 names_key_capacity = 16;
    PdbBuffer* streams = arena_allocate(arena, PdbBuffer, PDB_STREAM_COUNT);
    memset(streams, 0, PDB_STREAM_COUNT * sizeof(*streams));
#define PDB_STREAM_BEGIN(index, size)                                                                                                                          \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        streams[(index)].bytes = arena_allocate(arena, u8, (size));                                                                                            \
        streams[(index)].capacity = (size);                                                                                                                    \
    } while (0)

    // Stream 1: PDB info, carrying the identity the image's RSDS entry repeats.
    PDB_STREAM_BEGIN(PDB_STREAM_INFO, 128 + names_key_capacity);
    PdbBuffer* info = streams + PDB_STREAM_INFO;
    pdb_emit_u32(info, PDB_INFO_VERSION_VC70);
    pdb_emit_u32(info, 0);
    pdb_emit_u32(info, input.age ? input.age : 1);
    pdb_emit_bytes(info, input.guid, sizeof(input.guid));
    // Named stream map holding a single "/names" entry, placed in the bucket a
    // reader will probe for it.
    String8 names_key = S8("/names");
    pdb_emit_u32(info, (u32)(names_key.length + 1));
    pdb_emit_bytes(info, names_key.pointer, names_key.length);
    pdb_emit_zero(info, 1);
    pdb_emit_u32(info, 1);
    pdb_emit_u32(info, PDB_NAMED_STREAM_CAPACITY);
    pdb_emit_u32(info, 1);
    pdb_emit_u32(info, 1u << (pdb_hash_string_v1(names_key) % PDB_NAMED_STREAM_CAPACITY));
    pdb_emit_u32(info, 0);
    pdb_emit_u32(info, 0);
    pdb_emit_u32(info, PDB_STREAM_NAMES);
    pdb_emit_u32(info, PDB_FEATURE_VC140);

    // Streams 2 and 4: empty but structurally valid type and id records.
    for (u32 pass = 0; pass < 2; pass += 1)
    {
        u32 stream_index = pass ? PDB_STREAM_IPI : PDB_STREAM_TPI;
        PDB_STREAM_BEGIN(stream_index, PDB_TPI_HEADER_SIZE);
        PdbBuffer* type_stream = streams + stream_index;
        pdb_emit_u32(type_stream, PDB_TPI_VERSION_V80);
        pdb_emit_u32(type_stream, PDB_TPI_HEADER_SIZE);
        pdb_emit_u32(type_stream, 0x1000);
        pdb_emit_u32(type_stream, 0x1000);
        pdb_emit_u32(type_stream, 0);
        pdb_emit_u16(type_stream, 0xffff);
        pdb_emit_u16(type_stream, 0xffff);
        pdb_emit_u32(type_stream, 4);
        pdb_emit_u32(type_stream, 0x3ffff);
        pdb_emit_u32(type_stream, 0);
        pdb_emit_u32(type_stream, 0);
        pdb_emit_u32(type_stream, 0);
        pdb_emit_u32(type_stream, 0);
        pdb_emit_u32(type_stream, 0);
        pdb_emit_u32(type_stream, 0);
    }

    // Stream 8: the image section table, which maps addresses to sections.
    PDB_STREAM_BEGIN(PDB_STREAM_SECTION_HEADERS, (u64)input.section_count * 40);
    PdbBuffer* section_headers = streams + PDB_STREAM_SECTION_HEADERS;
    for (u32 section_index = 0; section_index < input.section_count; section_index += 1)
    {
        PdbSection* section = input.sections + section_index;
        u64 header = section_headers->count;
        pdb_emit_zero(section_headers, 40);
        u64 name_length = BUSTER_MIN(section->name.length, 8);
        if (name_length)
        {
            memcpy(section_headers->bytes + header, section->name.pointer, name_length);
        }
        pdb_write_u32_at(section_headers, header + 8, section->virtual_size);
        pdb_write_u32_at(section_headers, header + 12, section->virtual_address);
        pdb_write_u32_at(section_headers, header + 16, section->raw_size);
        pdb_write_u32_at(section_headers, header + 20, section->raw_offset);
        pdb_write_u32_at(section_headers, header + 36, section->characteristics);
    }

    // Stream 9: the module's symbols followed by its C13 line tables.
    u64 module_capacity = 8 + split.symbols.length + split.c13.length;
    PDB_STREAM_BEGIN(PDB_STREAM_MODULE, module_capacity);
    PdbBuffer* module = streams + PDB_STREAM_MODULE;
    pdb_emit_u32(module, 4);
    pdb_emit_bytes(module, split.symbols.pointer, split.symbols.length);
    u32 symbol_byte_size = (u32)(4 + split.symbols.length);
    pdb_emit_bytes(module, split.c13.pointer, split.c13.length);
    pdb_emit_u32(module, 0);

    // Stream 3: DBI, describing the one module and its section contributions.
    u64 module_info_size = align_forward(64 + module_name.length + 1 + module_name.length + 1, 4);
    u64 section_contribution_size = 4 + (u64)input.section_count * PDB_SECTION_CONTRIBUTION_SIZE;
    u64 section_map_size = 4 + (u64)input.section_count * 20;
    u64 source_info_size = align_forward(4 + 2 + 2 + (u64)source_file_count * 4 + source_names_size, 4);
    // Readers resolve a module's paths through the edit-and-continue name
    // table, so an empty but well-formed string table must be present.
    u64 ec_substream_size = align_forward(12 + 1 + 4 + 4 + 4, 4);
    u64 dbg_header_size = PDB_DBG_HEADER_COUNT * sizeof(u16);
    PDB_STREAM_BEGIN(PDB_STREAM_DBI,
                     PDB_DBI_HEADER_SIZE + module_info_size + section_contribution_size + section_map_size + source_info_size + ec_substream_size +
                         dbg_header_size);
    PdbBuffer* dbi = streams + PDB_STREAM_DBI;
    pdb_emit_u32(dbi, 0xffffffff);
    pdb_emit_u32(dbi, PDB_DBI_VERSION_V70);
    pdb_emit_u32(dbi, input.age ? input.age : 1);
    pdb_emit_u16(dbi, PDB_STREAM_GLOBALS);
    pdb_emit_u16(dbi, 0x8e1f);
    pdb_emit_u16(dbi, PDB_STREAM_PUBLICS);
    pdb_emit_u16(dbi, 0);
    pdb_emit_u16(dbi, PDB_STREAM_SYMBOL_RECORDS);
    pdb_emit_u16(dbi, 0);
    pdb_emit_u32(dbi, (u32)module_info_size);
    pdb_emit_u32(dbi, (u32)section_contribution_size);
    pdb_emit_u32(dbi, (u32)section_map_size);
    pdb_emit_u32(dbi, (u32)source_info_size);
    pdb_emit_u32(dbi, 0);
    pdb_emit_u32(dbi, 0);
    pdb_emit_u32(dbi, (u32)dbg_header_size);
    pdb_emit_u32(dbi, (u32)ec_substream_size);
    pdb_emit_u16(dbi, 0);
    pdb_emit_u16(dbi, input.machine);
    pdb_emit_u32(dbi, 0);
    u64 module_info_start = dbi->count;
    pdb_emit_u32(dbi, 0);
    u64 module_contribution = dbi->count;
    pdb_emit_zero(dbi, PDB_SECTION_CONTRIBUTION_SIZE);
    pdb_emit_u16(dbi, 0);
    pdb_emit_u16(dbi, PDB_STREAM_MODULE);
    pdb_emit_u32(dbi, symbol_byte_size);
    pdb_emit_u32(dbi, 0);
    pdb_emit_u32(dbi, (u32)split.c13.length);
    pdb_emit_u16(dbi, (u16)source_file_count);
    pdb_emit_u16(dbi, 0);
    pdb_emit_u32(dbi, 0);
    pdb_emit_u32(dbi, 0);
    pdb_emit_u32(dbi, 0);
    pdb_emit_bytes(dbi, module_name.pointer, module_name.length);
    pdb_emit_zero(dbi, 1);
    pdb_emit_bytes(dbi, module_name.pointer, module_name.length);
    pdb_emit_zero(dbi, 1);
    pdb_emit_align4(dbi);
    // The module owns the code section, so its contribution covers that range.
    u32 contribution_section = input.code_section ? input.code_section : 1;
    u32 contribution_index = contribution_section - 1;
    pdb_write_u32_at(dbi, module_contribution, contribution_section);
    pdb_write_u32_at(dbi, module_contribution + 4, 0);
    pdb_write_u32_at(dbi, module_contribution + 8, input.code_size);
    pdb_write_u32_at(dbi, module_contribution + 12, contribution_index < input.section_count ? input.sections[contribution_index].characteristics : 0);
    if (dbi->count - module_info_start != module_info_size)
    {
        return result;
    }
    pdb_emit_u32(dbi, PDB_SECTION_CONTRIBUTION_VERSION);
    for (u32 section_index = 0; section_index < input.section_count; section_index += 1)
    {
        PdbSection* section = input.sections + section_index;
        pdb_emit_u16(dbi, (u16)(section_index + 1));
        pdb_emit_u16(dbi, 0);
        pdb_emit_u32(dbi, 0);
        pdb_emit_u32(dbi, section->virtual_size);
        pdb_emit_u32(dbi, section->characteristics);
        pdb_emit_u16(dbi, 0);
        pdb_emit_u16(dbi, 0);
        pdb_emit_u32(dbi, 0);
        pdb_emit_u32(dbi, 0);
    }
    pdb_emit_u16(dbi, (u16)input.section_count);
    pdb_emit_u16(dbi, (u16)input.section_count);
    for (u32 section_index = 0; section_index < input.section_count; section_index += 1)
    {
        pdb_emit_u16(dbi, 0x109);
        pdb_emit_u16(dbi, 0);
        pdb_emit_u16(dbi, 0);
        pdb_emit_u16(dbi, (u16)(section_index + 1));
        pdb_emit_u16(dbi, 0xffff);
        pdb_emit_u16(dbi, 0xffff);
        pdb_emit_u32(dbi, 0);
        pdb_emit_u32(dbi, input.sections[section_index].virtual_size);
    }
    u64 source_info_start = dbi->count;
    pdb_emit_u16(dbi, 1);
    pdb_emit_u16(dbi, (u16)source_file_count);
    pdb_emit_u16(dbi, 0);
    pdb_emit_u16(dbi, (u16)source_file_count);
    u64 name_offset_start = dbi->count;
    pdb_emit_zero(dbi, (u64)source_file_count * 4);
    u64 names_start = dbi->count;
    for (u32 file_index = 0; file_index < source_file_count; file_index += 1)
    {
        pdb_write_u32_at(dbi, name_offset_start + (u64)file_index * 4, (u32)(dbi->count - names_start));
        pdb_emit_bytes(dbi, source_names[file_index].pointer, source_names[file_index].length);
        pdb_emit_zero(dbi, 1);
    }
    pdb_emit_align4(dbi);
    if (dbi->count - source_info_start != source_info_size)
    {
        return result;
    }
    u64 ec_start = dbi->count;
    pdb_emit_u32(dbi, PDB_STRING_TABLE_SIGNATURE);
    pdb_emit_u32(dbi, 1);
    pdb_emit_u32(dbi, 1);
    pdb_emit_zero(dbi, 1);
    pdb_emit_u32(dbi, 1);
    pdb_emit_u32(dbi, 0);
    pdb_emit_u32(dbi, 0);
    pdb_emit_align4(dbi);
    if (dbi->count - ec_start != ec_substream_size)
    {
        return result;
    }
    for (u32 index = 0; index < PDB_DBG_HEADER_COUNT; index += 1)
    {
        pdb_emit_u16(dbi, index == PDB_DBG_HEADER_SECTION_HEADERS ? (u16)PDB_STREAM_SECTION_HEADERS : (u16)0xffff);
    }

    // Streams 5 and 6 hold no records yet, but readers still expect their
    // hash headers to be present and well formed.
    PDB_STREAM_BEGIN(PDB_STREAM_GLOBALS, PDB_GSI_HASH_HEADER_SIZE);
    PdbBuffer* globals = streams + PDB_STREAM_GLOBALS;
    pdb_emit_u32(globals, 0xffffffff);
    pdb_emit_u32(globals, PDB_GSI_HASH_VERSION);
    pdb_emit_u32(globals, 0);
    pdb_emit_u32(globals, 0);
    PDB_STREAM_BEGIN(PDB_STREAM_PUBLICS, PDB_PUBLICS_HEADER_SIZE + PDB_GSI_HASH_HEADER_SIZE);
    PdbBuffer* publics = streams + PDB_STREAM_PUBLICS;
    pdb_emit_u32(publics, PDB_GSI_HASH_HEADER_SIZE);
    pdb_emit_u32(publics, 0);
    pdb_emit_u32(publics, 0);
    pdb_emit_u32(publics, 0);
    pdb_emit_u16(publics, 0);
    pdb_emit_u16(publics, 0);
    pdb_emit_u32(publics, 0);
    pdb_emit_u32(publics, 0);
    pdb_emit_u32(publics, 0xffffffff);
    pdb_emit_u32(publics, PDB_GSI_HASH_VERSION);
    pdb_emit_u32(publics, 0);
    pdb_emit_u32(publics, 0);
    // Stream 10: the PDB string table the checksum entries now index.
    u32 names_bucket_count = 2;
    while (names_bucket_count < source_file_count * 2 + 2)
    {
        names_bucket_count *= 2;
    }
    PDB_STREAM_BEGIN(PDB_STREAM_NAMES, 12 + names_buffer_size + 4 + (u64)names_bucket_count * 4 + 4);
    PdbBuffer* names = streams + PDB_STREAM_NAMES;
    pdb_emit_u32(names, PDB_STRING_TABLE_SIGNATURE);
    pdb_emit_u32(names, 1);
    pdb_emit_u32(names, (u32)names_buffer_size);
    pdb_emit_zero(names, 1);
    for (u32 file_index = 0; file_index < source_file_count; file_index += 1)
    {
        pdb_emit_bytes(names, source_names[file_index].pointer, source_names[file_index].length);
        pdb_emit_zero(names, 1);
    }
    pdb_emit_u32(names, names_bucket_count);
    u64 names_bucket_start = names->count;
    pdb_emit_zero(names, (u64)names_bucket_count * 4);
    for (u32 file_index = 0; file_index < source_file_count; file_index += 1)
    {
        u32 bucket = pdb_hash_string_v1(source_names[file_index]) % names_bucket_count;
        while (pdb_read_u32((ByteSlice){.pointer = names->bytes, .length = names->count}, names_bucket_start + (u64)bucket * 4))
        {
            bucket = (bucket + 1) % names_bucket_count;
        }
        pdb_write_u32_at(names, names_bucket_start + (u64)bucket * 4, names_offsets[file_index]);
    }
    pdb_emit_u32(names, source_file_count);
    // Stream 7 stays empty: no symbol records are published yet.
    for (u32 index = 0; index < PDB_STREAM_COUNT; index += 1)
    {
        if (streams[index].overflow)
        {
            return result;
        }
    }
#undef PDB_STREAM_BEGIN

    // Lay the streams out as MSF blocks, then the directory and its block map.
    u32* stream_block_counts = arena_allocate(arena, u32, PDB_STREAM_COUNT);
    u32 data_block_count = 0;
    for (u32 index = 0; index < PDB_STREAM_COUNT; index += 1)
    {
        stream_block_counts[index] = (u32)((streams[index].count + PDB_BLOCK_SIZE - 1) / PDB_BLOCK_SIZE);
        data_block_count += stream_block_counts[index];
    }
    u64 directory_size = 4 + (u64)PDB_STREAM_COUNT * 4 + (u64)data_block_count * 4;
    u32 directory_block_count = (u32)((directory_size + PDB_BLOCK_SIZE - 1) / PDB_BLOCK_SIZE);
    u32 first_directory_block = PDB_FIRST_DATA_BLOCK + data_block_count;
    u32 block_map_block = first_directory_block + directory_block_count;
    u32 block_count = block_map_block + 1;
    u64 file_size = (u64)block_count * PDB_BLOCK_SIZE;
    u8* bytes = arena_allocate(arena, u8, file_size);
    memset(bytes, 0, file_size);

    static char8 const magic[] = "Microsoft C/C++ MSF 7.00\r\n\x1a"
                                 "DS\0\0";
    memcpy(bytes, magic, 32);
    u32 superblock[] = {
        PDB_BLOCK_SIZE, PDB_FREE_BLOCK_MAP, block_count, (u32)directory_size, 0, block_map_block,
    };
    memcpy(bytes + 32, superblock, sizeof(superblock));
    // Both free block maps mark every block outside the image as free.
    for (u32 map = 1; map <= 2; map += 1)
    {
        memset(bytes + (u64)map * PDB_BLOCK_SIZE, 0xff, PDB_BLOCK_SIZE);
    }
    for (u32 block = 0; block < block_count; block += 1)
    {
        u64 map_offset = (u64)PDB_FREE_BLOCK_MAP * PDB_BLOCK_SIZE + block / 8;
        bytes[map_offset] = (u8)(bytes[map_offset] & ~(1u << (block % 8)));
        u64 other_offset = (u64)1 * PDB_BLOCK_SIZE + block / 8;
        bytes[other_offset] = (u8)(bytes[other_offset] & ~(1u << (block % 8)));
    }

    PdbBuffer directory = {
        .bytes = arena_allocate(arena, u8, directory_size),
        .capacity = directory_size,
    };
    pdb_emit_u32(&directory, PDB_STREAM_COUNT);
    for (u32 index = 0; index < PDB_STREAM_COUNT; index += 1)
    {
        pdb_emit_u32(&directory, (u32)streams[index].count);
    }
    u32 next_block = PDB_FIRST_DATA_BLOCK;
    for (u32 index = 0; index < PDB_STREAM_COUNT; index += 1)
    {
        for (u32 block = 0; block < stream_block_counts[index]; block += 1)
        {
            u64 destination = (u64)(next_block + block) * PDB_BLOCK_SIZE;
            u64 offset = (u64)block * PDB_BLOCK_SIZE;
            u64 size = BUSTER_MIN((u64)PDB_BLOCK_SIZE, streams[index].count - offset);
            memcpy(bytes + destination, streams[index].bytes + offset, size);
            pdb_emit_u32(&directory, next_block + block);
        }
        next_block += stream_block_counts[index];
    }
    if (directory.overflow || directory.count != directory_size)
    {
        return result;
    }
    memcpy(bytes + (u64)first_directory_block * PDB_BLOCK_SIZE, directory.bytes, directory.count);
    for (u32 block = 0; block < directory_block_count; block += 1)
    {
        u32 value = first_directory_block + block;
        memcpy(bytes + (u64)block_map_block * PDB_BLOCK_SIZE + (u64)block * 4, &value, sizeof(value));
    }
    result.bytes = (ByteSlice){
        .pointer = bytes,
        .length = file_size,
    };
    result.valid = true;
    return result;
}

#if BUSTER_INCLUDE_TESTS
UnitTestResult pdb_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    DwarfFunction functions[] = {
        {
            .name = S8_INITIALIZER("main"),
            .code_offset = 0,
            .code_size = 0x100,
            .file = 0,
            .line = 3,
        },
        {
            .name = S8_INITIALIZER("helper"),
            .code_offset = 0x100,
            .code_size = 0x100,
            .file = 1,
            .line = 9,
        },
    };
    DwarfLineEntry lines[] = {
        {.code_offset = 0, .file = 0, .line = 3, .column = 1},
        {.code_offset = 0x40, .file = 0, .line = 4, .column = 5},
        {.code_offset = 0x100, .file = 1, .line = 9, .column = 1},
        {.code_offset = 0x180, .file = 1, .line = 11, .column = 3},
    };
    String8 files[] = {
        S8_INITIALIZER("main.c"),
        S8_INITIALIZER("helper.h"),
    };
    CodeviewResult codeview = codeview_build(arguments->arena, (CodeviewInput){
                                                                  .producer = S8("buster"),
                                                                  .file_paths = files,
                                                                  .functions = functions,
                                                                  .lines = lines,
                                                                  .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                  .function_count = BUSTER_ARRAY_LENGTH(functions),
                                                                  .line_count = BUSTER_ARRAY_LENGTH(lines),
                                                                  .machine = CODEVIEW_MACHINE_X64,
                                                              });
    BUSTER_TEST(arguments, codeview.valid);
    if (!codeview.valid)
    {
        return result;
    }
    PdbSection sections[] = {
        {
            .name = S8_INITIALIZER(".text"),
            .virtual_address = 0x1000,
            .virtual_size = 0x200,
            .raw_size = 0x200,
            .raw_offset = 0x400,
            .characteristics = 0x60000020,
        },
    };
    PdbInput input = {
        .module_name = S8("demo.obj"),
        .codeview_symbols = codeview.symbols,
        .sections = sections,
        .section_count = BUSTER_ARRAY_LENGTH(sections),
        .age = 1,
        .code_section = 1,
        .code_size = 0x200,
        .machine = 0x8664,
    };
    for (u32 index = 0; index < 16; index += 1)
    {
        input.guid[index] = (u8)(index + 1);
    }
    PdbResult built = pdb_build(arguments->arena, input);
    BUSTER_TEST(arguments, built.valid);
    if (!built.valid)
    {
        return result;
    }
    BUSTER_TEST(arguments, built.bytes.length % PDB_BLOCK_SIZE == 0);
    BUSTER_TEST(arguments, memcmp(built.bytes.pointer, "Microsoft C/C++ MSF 7.00\r\n\x1a" "DS", 30) == 0);
    u32 block_size = pdb_read_u32(built.bytes, 32);
    u32 block_count = pdb_read_u32(built.bytes, 40);
    u32 directory_size = pdb_read_u32(built.bytes, 44);
    u32 block_map = pdb_read_u32(built.bytes, 52);
    BUSTER_TEST(arguments, block_size == PDB_BLOCK_SIZE);
    BUSTER_TEST(arguments, (u64)block_count * PDB_BLOCK_SIZE == built.bytes.length);
    BUSTER_TEST(arguments, block_map < block_count);
    // Walk the directory the way a reader would and check every stream lands
    // inside the file.
    u32 directory_block = pdb_read_u32(built.bytes, (u64)block_map * PDB_BLOCK_SIZE);
    BUSTER_TEST(arguments, directory_block < block_count);
    u64 directory_base = (u64)directory_block * PDB_BLOCK_SIZE;
    BUSTER_TEST(arguments, directory_size >= 4);
    u32 stream_count = pdb_read_u32(built.bytes, directory_base);
    BUSTER_TEST(arguments, stream_count == PDB_STREAM_COUNT);
    if (stream_count != PDB_STREAM_COUNT)
    {
        return result;
    }
    u64 block_cursor = directory_base + 4 + (u64)stream_count * 4;
    u32 info_block = 0;
    u32 dbi_block = 0;
    for (u32 index = 0; index < stream_count; index += 1)
    {
        u32 size = pdb_read_u32(built.bytes, directory_base + 4 + (u64)index * 4);
        u32 blocks = (size + PDB_BLOCK_SIZE - 1) / PDB_BLOCK_SIZE;
        for (u32 block = 0; block < blocks; block += 1)
        {
            u32 block_index = pdb_read_u32(built.bytes, block_cursor);
            block_cursor += 4;
            BUSTER_TEST(arguments, block_index < block_count);
            if (!block && index == PDB_STREAM_INFO)
            {
                info_block = block_index;
            }
            if (!block && index == PDB_STREAM_DBI)
            {
                dbi_block = block_index;
            }
        }
    }
    // The info stream must carry the identity the image will repeat.
    BUSTER_TEST(arguments, info_block != 0);
    u64 info_base = (u64)info_block * PDB_BLOCK_SIZE;
    BUSTER_TEST(arguments, pdb_read_u32(built.bytes, info_base) == PDB_INFO_VERSION_VC70);
    BUSTER_TEST(arguments, pdb_read_u32(built.bytes, info_base + 8) == 1);
    BUSTER_TEST(arguments, memcmp(built.bytes.pointer + info_base + 12, input.guid, sizeof(input.guid)) == 0);
    // The DBI header must point at the module and section header streams.
    BUSTER_TEST(arguments, dbi_block != 0);
    u64 dbi_base = (u64)dbi_block * PDB_BLOCK_SIZE;
    BUSTER_TEST(arguments, pdb_read_u32(built.bytes, dbi_base) == 0xffffffff);
    BUSTER_TEST(arguments, pdb_read_u32(built.bytes, dbi_base + 4) == PDB_DBI_VERSION_V70);
    u16 dbi_machine = 0;
    memcpy(&dbi_machine, built.bytes.pointer + dbi_base + 58, sizeof(dbi_machine));
    BUSTER_TEST(arguments, dbi_machine == 0x8664);
    u32 dbi_module_size = pdb_read_u32(built.bytes, dbi_base + 24);
    u32 dbi_contribution_size = pdb_read_u32(built.bytes, dbi_base + 28);
    BUSTER_TEST(arguments, dbi_module_size != 0 && dbi_contribution_size != 0);
    u16 module_stream = 0;
    memcpy(&module_stream, built.bytes.pointer + dbi_base + PDB_DBI_HEADER_SIZE + 4 + PDB_SECTION_CONTRIBUTION_SIZE + 2, sizeof(module_stream));
    BUSTER_TEST(arguments, module_stream == PDB_STREAM_MODULE);
    // Leave the file on disk so external validators can read it.
    String8 pdb_path = buster_test_temporary_path(arguments->arena, S8("buster-pdb"), S8(".pdb"));
    BUSTER_TEST(arguments, file_write(pdb_path, built.bytes));
    // Invalid input must be rejected rather than producing a broken file.
    PdbInput invalid = input;
    invalid.section_count = 0;
    BUSTER_TEST(arguments, !pdb_build(arguments->arena, invalid).valid);
    return result;
}
#endif
