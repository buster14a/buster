// PDB writing: packages the CodeView streams codeview.c produced into the
// MSF container Windows debuggers load — superblock, free-page maps,
// directory, and the fixed DBI/TPI/IPI/info streams — for the PE linker's
// /debug output.

#include <buster/lib/compiler/pdb/pdb.h>
#include <buster/lib/string.h>
#include <buster/lib/integer.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>

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
    PDB_CHECKSUM_HEADER_SIZE = 6,
    // A merge round can only expose new matches one level up the type graph,
    // so rounds are capped rather than run to a fixed point. The cap bounds
    // how much merging happens, never whether the result is correct.
    PDB_TYPE_MERGE_ROUND_LIMIT = 8,
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

enum
{
    PDB_S_CONSTANT = 0x1107,
    PDB_S_GDATA32 = 0x110d,
    PDB_S_LOCAL = 0x113e,
    PDB_S_GPROC32 = 0x1110,
    PDB_S_DEFRANGE_FRAMEPOINTER_REL = 0x1142,
    PDB_S_INLINESITE = 0x114d,
    PDB_LF_MODIFIER = 0x1001,
    PDB_LF_POINTER = 0x1002,
    PDB_LF_PROCEDURE = 0x1008,
    PDB_LF_ARGLIST = 0x1201,
    PDB_LF_FIELDLIST = 0x1203,
    PDB_LF_ENUMERATE = 0x1502,
    PDB_LF_ARRAY = 0x1503,
    PDB_LF_STRUCTURE = 0x1505,
    PDB_LF_UNION = 0x1506,
    PDB_LF_ENUM = 0x1507,
    PDB_LF_ALIAS = 0x150a,
    PDB_LF_MEMBER = 0x150d,
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

// The remaining-space form of `count + size > capacity`: `count` never passes
// `capacity`, so the subtraction cannot underflow, and unlike the sum it
// cannot wrap past a `size` large enough to make the test pass.
BUSTER_GLOBAL_LOCAL void pdb_emit_bytes(PdbBuffer* buffer, void const* source, u64 size)
{
    if (size > buffer->capacity - buffer->count)
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
    if (size > buffer->capacity - buffer->count)
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
    if (offset > buffer->count || sizeof(value) > buffer->count - offset)
    {
        buffer->overflow = true;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

u32 pdb_read_u32(ByteSlice bytes, u64 offset)
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
    u32* checksum_offsets;
    u32 checksum_count;
    bool valid;
    u8 reserved[3];
};

typedef struct PdbTypeModule PdbTypeModule;
struct PdbTypeModule
{
    u32* local_to_global;
    u32 count;
};

typedef struct PdbTypeRecord PdbTypeRecord;
struct PdbTypeRecord
{
    ByteSlice bytes;
    u32 module_index;
    u8 reserved[4];
};

BUSTER_GLOBAL_LOCAL PdbCodeviewSplit pdb_split_codeview(Arena* arena, ByteSlice blob)
{
    PdbCodeviewSplit result = {0};
    if (blob.pointer && blob.length >= 4)
    {
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
            // The remap index belongs to one checksum/string table pair.
            // Reject duplicate tables instead of applying the last table's
            // offsets to an earlier, differently sized subsection.
            u64 padded_length = ((u64)length + 3) & ~(u64)3;
            if (padded_length > blob.length - payload ||
                (kind == PDB_DEBUG_S_STRINGTABLE && result.string_table.pointer) ||
                (kind == PDB_DEBUG_S_FILECHKSMS && result.checksums.pointer))
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
            offset = payload + padded_length;
        }
        result.symbols = (ByteSlice){
            .pointer = symbol_bytes,
            .length = symbol_count,
        };
        result.c13 = (ByteSlice){
            .pointer = c13_bytes,
            .length = c13_count,
        };
        result.valid = offset == blob.length;
    }

    return result;
}


// CodeView records are {u32 filename, u8 size, u8 kind, digest[size]},
// padded to four bytes. Validate once and retain the exact record offsets;
// line records refer to these offsets, so neither digest nor padding may move.
BUSTER_GLOBAL_LOCAL bool pdb_index_checksums(Arena* arena, PdbCodeviewSplit* split)
{
    ByteSlice bytes = split->checksums;
    bool valid = bytes.length <= UINT32_MAX && (!bytes.length || bytes.pointer);
    if (valid)
    {
        // Eight is the minimum aligned record size, not a record stride.
        u64 capacity = bytes.length / ((PDB_CHECKSUM_HEADER_SIZE + 3u) & ~3u);
        u32* offsets = arena_allocate(arena, u32, capacity ? capacity : 1);
        u32 count = 0;
        u64 offset = 0;
        while (valid && offset < bytes.length)
        {
            u64 remaining = bytes.length - offset;
            if (remaining < PDB_CHECKSUM_HEADER_SIZE)
                valid = false;
            else
            {
                u32 stride = (PDB_CHECKSUM_HEADER_SIZE + (u32)bytes.pointer[offset + 4] + 3u) & ~3u;
                if (stride > remaining)
                    valid = false;
                else
                {
                    offsets[count++] = (u32)offset;
                    offset += stride;
                }
            }
        }
        if (valid)
        {
            split->checksum_offsets = offsets;
            split->checksum_count = count;
        }
    }
    return valid;
}

// A PDB's checksum entries index the global /names stream rather than the
// object-local string table, so the C13 region is rebuilt with remapped
// offsets and the now-redundant string table dropped.
BUSTER_GLOBAL_LOCAL ByteSlice pdb_rebuild_c13(Arena* arena, ByteSlice blob, u32 const* names_offsets, u32 const* checksum_offsets, u32 file_count)
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
                for (u32 file_index = 0; file_index < file_count; file_index += 1)
                {
                    u32 mapped = names_offsets[file_index];
                    memcpy(bytes + start + 8 + checksum_offsets[file_index], &mapped, sizeof(mapped));
                }
            }
            while (count & 3)
            {
                bytes[count++] = 0;
            }
        }
        offset = payload + (((u64)length + 3) & ~(u64)3);
    }
    result = (ByteSlice){
        .pointer = bytes,
        .length = count,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL u32 pdb_codeview_type_record_count(ByteSlice types, bool* valid)
{
    u32 result = 0;
    if (valid)
    {
        *valid = false;
    }
    if (types.length >= 4)
    {
        u64 offset = 4;
        while (offset < types.length)
        {
            if (offset + 2 > types.length)
            {
                return result;
            }
            u16 length = 0;
            memcpy(&length, types.pointer + offset, sizeof(length));
            if (length < 2 || (u64)length > types.length - offset - 2)
            {
                return result;
            }
            u64 record_size = 2 + length;
            u64 aligned = (record_size + 3) & ~3u;
            if (aligned > types.length - offset || result == UINT32_MAX)
            {
                return result;
            }
            result += 1;
            offset += aligned;
        }
        if (valid)
        {
            *valid = true;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool pdb_read_u16_checked(ByteSlice bytes, u64 offset, u16* value)
{
    bool result;
    if (offset > bytes.length || sizeof(u16) > bytes.length - offset)
    {
        result = false;
    }
    else
    {
        memcpy(value, bytes.pointer + offset, sizeof(*value));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool pdb_read_u32_checked(ByteSlice bytes, u64 offset, u32* value)
{
    bool result;
    if (offset > bytes.length || sizeof(u32) > bytes.length - offset)
    {
        result = false;
    }
    else
    {
        memcpy(value, bytes.pointer + offset, sizeof(*value));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool pdb_write_u32_checked(ByteSlice bytes, u64 offset, u32 value)
{
    bool result;
    if (offset > bytes.length || sizeof(u32) > bytes.length - offset)
    {
        result = false;
    }
    else
    {
        memcpy(bytes.pointer + offset, &value, sizeof(value));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool pdb_type_index_map(PdbTypeModule* module, ByteSlice bytes, u64 offset)
{
    u32 value = 0;
    if (!module || !pdb_read_u32_checked(bytes, offset, &value))
    {
        return false;
    }
    if (value < 0x1000)
    {
        return true;
    }
    if (value - 0x1000 >= module->count)
    {
        return false;
    }
    u32 mapped = module->local_to_global[value - 0x1000];
    return mapped != UINT32_MAX && pdb_write_u32_checked(bytes, offset, mapped);
}

BUSTER_GLOBAL_LOCAL u64 pdb_skip_numeric(ByteSlice bytes, u64 offset)
{
    u16 leaf = 0;
    if (!pdb_read_u16_checked(bytes, offset, &leaf))
    {
        return bytes.length + 1;
    }
    offset += sizeof(leaf);
    if (leaf < 0x8000)
    {
        return offset;
    }
    u64 size = leaf == 0x8000 || leaf == 0x8005 ? 1
               : leaf == 0x8001 || leaf == 0x8002 || leaf == 0x8006 || leaf == 0x8007 ? 2
               : leaf == 0x8003 || leaf == 0x8004 || leaf == 0x8008 || leaf == 0x800b || leaf == 0x8010 ? 4
                                                                                                             : 8;
    return offset + size;
}

BUSTER_GLOBAL_LOCAL u64 pdb_skip_name(ByteSlice bytes, u64 offset)
{
    while (offset < bytes.length && bytes.pointer[offset])
    {
        offset += 1;
    }
    return offset < bytes.length ? offset + 1 : bytes.length + 1;
}

BUSTER_GLOBAL_LOCAL bool pdb_rewrite_type_record(PdbTypeModule* module, ByteSlice record)
{
    u16 leaf = 0;
    if (!pdb_read_u16_checked(record, 2, &leaf))
    {
        return false;
    }
    switch (leaf)
    {
    case PDB_LF_MODIFIER:
    case PDB_LF_POINTER:
        return pdb_type_index_map(module, record, 4);
    case PDB_LF_ARRAY:
        return pdb_type_index_map(module, record, 4) && pdb_type_index_map(module, record, 8);
    case PDB_LF_STRUCTURE:
    case PDB_LF_UNION:
        return pdb_type_index_map(module, record, 8) && pdb_type_index_map(module, record, 12) && pdb_type_index_map(module, record, 16);
    case PDB_LF_ENUM:
        return pdb_type_index_map(module, record, 8) && pdb_type_index_map(module, record, 12);
    case PDB_LF_ALIAS:
        return pdb_type_index_map(module, record, 4);
    case PDB_LF_PROCEDURE:
        return pdb_type_index_map(module, record, 4) && pdb_type_index_map(module, record, 12);
    case PDB_LF_ARGLIST:
    {
        u32 count = 0;
        if (!pdb_read_u32_checked(record, 4, &count) || count > (record.length - 8) / 4)
        {
            return false;
        }
        for (u32 index = 0; index < count; index += 1)
        {
            if (!pdb_type_index_map(module, record, 8 + (u64)index * 4))
            {
                return false;
            }
        }
        return true;
    }
    case PDB_LF_FIELDLIST:
    {
        u64 cursor = 4;
        while (cursor + 2 <= record.length)
        {
            u16 member_leaf = 0;
            if (!pdb_read_u16_checked(record, cursor, &member_leaf))
            {
                return false;
            }
            if (member_leaf == PDB_LF_MEMBER)
            {
                if (cursor + 8 > record.length || !pdb_type_index_map(module, record, cursor + 4))
                {
                    return false;
                }
                cursor = pdb_skip_numeric(record, cursor + 8);
                cursor = pdb_skip_name(record, cursor);
            }
            else if (member_leaf == PDB_LF_ENUMERATE)
            {
                cursor = pdb_skip_numeric(record, cursor + 4);
                cursor = pdb_skip_name(record, cursor);
            }
            else
            {
                // Field-list padding and future leaf records are left intact;
                // the known records above are the only ones emitted by the
                // current CodeView frontend.
                break;
            }
            if (cursor > record.length)
            {
                return false;
            }
        }
        return true;
    }
    default:
        return true;
    }
}

BUSTER_GLOBAL_LOCAL bool pdb_collect_type_records(ByteSlice types, u32 module_index, PdbTypeRecord* records, u32* cursor, u32 capacity)
{
    if (!types.length)
    {
        return true;
    }
    if (types.length < 4)
    {
        return false;
    }
    u64 offset = 4;
    while (offset < types.length)
    {
        u16 length = 0;
        if (!pdb_read_u16_checked(types, offset, &length) || length < 2 || (u64)length > types.length - offset - 2)
        {
            return false;
        }
        u64 record_size = 2 + length;
        u64 aligned = (record_size + 3) & ~UINT64_C(3);
        if (aligned > types.length - offset || *cursor == capacity)
        {
            return false;
        }
        records[*cursor] = (PdbTypeRecord){
            .bytes = {.pointer = types.pointer + offset, .length = aligned},
            .module_index = module_index,
        };
        *cursor += 1;
        offset += aligned;
    }
    return offset == types.length;
}

BUSTER_GLOBAL_LOCAL bool pdb_rewrite_symbol_types(ByteSlice symbols, PdbTypeModule* module)
{
    u64 offset = 0;
    while (offset + 4 <= symbols.length)
    {
        u16 length = 0;
        u16 kind = 0;
        if (!pdb_read_u16_checked(symbols, offset, &length) || !pdb_read_u16_checked(symbols, offset + 2, &kind) || length < 2 ||
            (u64)length > symbols.length - offset - 2)
        {
            return false;
        }
        u64 record_size = 2 + length;
        u64 aligned = (record_size + 3) & ~UINT64_C(3);
        if (aligned > symbols.length - offset)
        {
            return false;
        }
        u64 type_offset = UINT64_MAX;
        if (kind == PDB_S_LOCAL || kind == PDB_S_CONSTANT || kind == PDB_S_GDATA32)
        {
            type_offset = offset + 4;
        }
        else if (kind == PDB_S_GPROC32)
        {
            // S_GPROC32 stores debug_start at +20, debug_end at +24, and
            // the procedure type index at +28 (all offsets include the
            // record's two-byte length and two-byte kind fields).
            type_offset = offset + 28;
        }
        else if (kind == PDB_S_INLINESITE)
        {
            type_offset = offset + 12;
        }
        if (type_offset != UINT64_MAX)
        {
            if (!pdb_type_index_map(module, symbols, type_offset))
            {
                return false;
            }
        }
        offset += aligned;
    }
    return offset == symbols.length;
}

PdbResult pdb_build(Arena* arena, PdbInput input)
{
    PdbResult result = {0};
    if (!arena || !input.section_count || !input.sections)
    {
        return result;
    }
    u32 module_count = input.module_count ? input.module_count : 1;
    if (input.module_count && !input.modules)
    {
        return result;
    }
    PdbModule* modules = arena_allocate(arena, PdbModule, module_count);
    if (input.module_count)
    {
        memcpy(modules, input.modules, (u64)module_count * sizeof(*modules));
    }
    else
    {
        modules[0] = (PdbModule){
            .name = input.module_name,
            .codeview_symbols = input.codeview_symbols,
            .code_offset = 0,
            .code_size = input.code_size,
            .code_section = input.code_section,
        };
    }
    PdbCodeviewSplit* splits = arena_allocate(arena, PdbCodeviewSplit, module_count);
    u32* source_counts = arena_allocate(arena, u32, module_count);
    u32 total_source_file_count = 0;
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        if (!modules[module_index].codeview_symbols.length)
        {
            return result;
        }
        splits[module_index] = pdb_split_codeview(arena, modules[module_index].codeview_symbols);
        if (!splits[module_index].valid)
        {
            return result;
        }
        if (!pdb_index_checksums(arena, splits + module_index))
        {
            return result;
        }
        source_counts[module_index] = splits[module_index].checksum_count;
        if (total_source_file_count > UINT32_MAX - source_counts[module_index])
        {
            return result;
        }
        total_source_file_count += source_counts[module_index];
    }
    String8* source_names = arena_allocate(arena, String8, total_source_file_count ? total_source_file_count : 1);
    u32* all_names_offsets = arena_allocate(arena, u32, total_source_file_count ? total_source_file_count : 1);
    u64 source_names_size = 0;
    u64 names_buffer_size = 1;
    u32 source_cursor = 0;
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        PdbCodeviewSplit* split = splits + module_index;
        u32 source_count = source_counts[module_index];
        u32* names_offsets = arena_allocate(arena, u32, source_count ? source_count : 1);
        for (u32 file_index = 0; file_index < source_count; file_index += 1)
        {
            u32 string_offset = pdb_read_u32(split->checksums, split->checksum_offsets[file_index]);
            if (string_offset >= split->string_table.length)
            {
                return result;
            }
            u64 length = 0;
            while (string_offset + length < split->string_table.length && split->string_table.pointer[string_offset + length])
            {
                length += 1;
            }
            if (string_offset + length == split->string_table.length || names_buffer_size > UINT32_MAX ||
                length + 1 > UINT32_MAX - names_buffer_size)
            {
                return result;
            }
            source_names[source_cursor] = (String8){
                .pointer = (char8*)(split->string_table.pointer + string_offset),
                .length = length,
            };
            names_offsets[file_index] = (u32)names_buffer_size;
            all_names_offsets[source_cursor] = names_offsets[file_index];
            source_cursor += 1;
            names_buffer_size += length + 1;
            source_names_size += length + 1;
        }
        split->c13 = pdb_rebuild_c13(arena, modules[module_index].codeview_symbols, names_offsets, split->checksum_offsets, source_count);
        if (!split->c13.pointer)
        {
            return result;
        }
    }

    u32 source_file_count = total_source_file_count;

    // CodeView type indices are local to each object, so give every
    // module-local record its own provisional global index first and rewrite
    // each record through the map of the module it came from.  Only then can
    // records be compared: two records may be byte-identical while their local
    // indices name different types, and merging those raw bytes would leave one
    // module's symbols pointing at a record rewritten with the other module's
    // map.
    u32 total_type_record_count = 0;
    PdbTypeModule* type_modules = arena_allocate(arena, PdbTypeModule, module_count);
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        bool valid_types = true;
        u32 count = pdb_codeview_type_record_count(modules[module_index].codeview_types, &valid_types);
        if (modules[module_index].codeview_types.length && !valid_types)
        {
            return result;
        }
        if (count > UINT32_MAX - 0x1000 - total_type_record_count)
        {
            return result;
        }
        type_modules[module_index].count = count;
        type_modules[module_index].local_to_global = arena_allocate(arena, u32, count ? count : 1);
        for (u32 type_index = 0; type_index < count; type_index += 1)
        {
            type_modules[module_index].local_to_global[type_index] = 0x1000 + total_type_record_count + type_index;
        }
        total_type_record_count += count;
    }
    PdbTypeRecord* records = arena_allocate(arena, PdbTypeRecord, total_type_record_count ? total_type_record_count : 1);
    u32 record_cursor = 0;
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        if (!pdb_collect_type_records(modules[module_index].codeview_types, module_index, records, &record_cursor, total_type_record_count))
        {
            return result;
        }
    }
    if (record_cursor != total_type_record_count)
    {
        return result;
    }
    // Normalize: each record is copied and rewritten through its own module's
    // map, so from here on every reference is a global index and the bytes of
    // two records mean the same thing exactly when they compare equal.
    ByteSlice* normalized = arena_allocate(arena, ByteSlice, total_type_record_count ? total_type_record_count : 1);
    for (u32 record_index = 0; record_index < total_type_record_count; record_index += 1)
    {
        PdbTypeRecord* record = records + record_index;
        u8* bytes = arena_allocate(arena, u8, record->bytes.length ? record->bytes.length : 1);
        if (record->bytes.length)
        {
            memcpy(bytes, record->bytes.pointer, record->bytes.length);
        }
        normalized[record_index] = (ByteSlice){.pointer = bytes, .length = record->bytes.length};
        if (!pdb_rewrite_type_record(type_modules + record->module_index, normalized[record_index]))
        {
            return result;
        }
    }
    // Merge the normalized records.  Each round hashes the survivors, keeps the
    // first of every equal group, and renumbers the references of what is left;
    // that renumbering can make records one level up the type graph equal, so
    // rounds repeat until one merges nothing.  The CodeView emitter references
    // records both forwards and backwards, so cyclic groups simply stop merging
    // instead of resolving — every round is sound on its own.
    u32 slot_count = total_type_record_count ? total_type_record_count : 1;
    u32* live = arena_allocate(arena, u32, slot_count);
    u32* survivors = arena_allocate(arena, u32, slot_count);
    u32* round_map = arena_allocate(arena, u32, slot_count);
    u32* final_index = arena_allocate(arena, u32, slot_count);
    for (u32 record_index = 0; record_index < total_type_record_count; record_index += 1)
    {
        live[record_index] = record_index;
        final_index[record_index] = 0x1000 + record_index;
    }
    u32 live_count = total_type_record_count;
    u64 bucket_capacity = 16;
    while (bucket_capacity < (u64)total_type_record_count * 2)
    {
        bucket_capacity *= 2;
    }
    u32* buckets = arena_allocate(arena, u32, bucket_capacity);
    u64* bucket_hashes = arena_allocate(arena, u64, bucket_capacity);
    for (u32 round = 0; round < PDB_TYPE_MERGE_ROUND_LIMIT && live_count; round += 1)
    {
        memset(buckets, 0xff, bucket_capacity * sizeof(*buckets));
        u32 survivor_count = 0;
        for (u32 position = 0; position < live_count; position += 1)
        {
            ByteSlice bytes = normalized[live[position]];
            u64 hash = buster_hash_64(bytes.pointer, bytes.length);
            u64 bucket = hash & (bucket_capacity - 1);
            u32 match = UINT32_MAX;
            while (buckets[bucket] != UINT32_MAX)
            {
                ByteSlice candidate = normalized[survivors[buckets[bucket]]];
                if (bucket_hashes[bucket] == hash && candidate.length == bytes.length &&
                    (!bytes.length || memcmp(candidate.pointer, bytes.pointer, bytes.length) == 0))
                {
                    match = buckets[bucket];
                    break;
                }
                bucket = (bucket + 1) & (bucket_capacity - 1);
            }
            if (match == UINT32_MAX)
            {
                match = survivor_count;
                survivors[survivor_count++] = live[position];
                buckets[bucket] = match;
                bucket_hashes[bucket] = hash;
            }
            round_map[position] = 0x1000 + match;
        }
        if (survivor_count == live_count)
        {
            break;
        }
        PdbTypeModule merged = {.local_to_global = round_map, .count = live_count};
        for (u32 position = 0; position < survivor_count; position += 1)
        {
            if (!pdb_rewrite_type_record(&merged, normalized[survivors[position]]))
            {
                return result;
            }
        }
        for (u32 record_index = 0; record_index < total_type_record_count; record_index += 1)
        {
            final_index[record_index] = round_map[final_index[record_index] - 0x1000];
        }
        memcpy(live, survivors, (u64)survivor_count * sizeof(*live));
        live_count = survivor_count;
    }
    // Compose each module's local map with the merge result, then point its
    // symbols at the surviving records.
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        PdbTypeModule* type_module = type_modules + module_index;
        for (u32 type_index = 0; type_index < type_module->count; type_index += 1)
        {
            type_module->local_to_global[type_index] = final_index[type_module->local_to_global[type_index] - 0x1000];
        }
        if (!pdb_rewrite_symbol_types(splits[module_index].symbols, type_module))
        {
            return result;
        }
    }
    u64 names_key_capacity = 16;
    u32 stream_count = PDB_STREAM_COUNT + (module_count > 1 ? module_count - 1 : 0);
    PdbBuffer* streams = arena_allocate(arena, PdbBuffer, stream_count);
    memset(streams, 0, (u64)stream_count * sizeof(*streams));
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

    // Streams 2 and 4: structurally valid type and id records.  The TPI
    // records have already been deduplicated and rewritten above; IPI remains
    // an empty but valid stream until the compiler emits id records.
    u64 type_record_bytes = 0;
    for (u32 live_position = 0; live_position < live_count; live_position += 1)
    {
        ByteSlice type = normalized[live[live_position]];
        if (type.length > UINT32_MAX - type_record_bytes)
        {
            return result;
        }
        type_record_bytes += type.length;
    }
    for (u32 pass = 0; pass < 2; pass += 1)
    {
        u32 stream_index = pass ? PDB_STREAM_IPI : PDB_STREAM_TPI;
        u64 capacity = PDB_TPI_HEADER_SIZE + (pass ? 0 : type_record_bytes);
        PDB_STREAM_BEGIN(stream_index, capacity);
        PdbBuffer* type_stream = streams + stream_index;
        pdb_emit_u32(type_stream, PDB_TPI_VERSION_V80);
        pdb_emit_u32(type_stream, PDB_TPI_HEADER_SIZE);
        pdb_emit_u32(type_stream, 0x1000);
        pdb_emit_u32(type_stream, 0x1000 + (pass ? 0 : live_count));
        pdb_emit_u32(type_stream, (u32)(pass ? 0 : type_record_bytes));
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
        if (!pass)
        {
            for (u32 live_position = 0; live_position < live_count; live_position += 1)
            {
                ByteSlice type = normalized[live[live_position]];
                if (type.length)
                {
                    pdb_emit_bytes(type_stream, type.pointer, type.length);
                }
            }
        }
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
    u32* module_stream_indices = arena_allocate(arena, u32, module_count);
    u32* module_symbol_sizes = arena_allocate(arena, u32, module_count);
    u32* module_c13_sizes = arena_allocate(arena, u32, module_count);
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        u32 stream_index = module_index ? PDB_STREAM_COUNT + module_index - 1 : PDB_STREAM_MODULE;
        module_stream_indices[module_index] = stream_index;
        PdbCodeviewSplit* split = splits + module_index;
        u64 module_capacity = 8 + split->symbols.length + split->c13.length;
        PDB_STREAM_BEGIN(stream_index, module_capacity);
        PdbBuffer* module = streams + stream_index;
        pdb_emit_u32(module, 4);
        pdb_emit_bytes(module, split->symbols.pointer, split->symbols.length);
        module_symbol_sizes[module_index] = (u32)(4 + split->symbols.length);
        pdb_emit_bytes(module, split->c13.pointer, split->c13.length);
        module_c13_sizes[module_index] = (u32)split->c13.length;
        pdb_emit_u32(module, 0);
    }

    // Stream 3: DBI, describing the one module and its section contributions.
    u64 module_info_size = 0;
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        String8 module_name = modules[module_index].name.length ? modules[module_index].name : S8("buster.obj");
        module_info_size += align_forward(64 + module_name.length + 1 + module_name.length + 1, 4);
    }
    u64 section_contribution_size = 4 + (u64)module_count * input.section_count * PDB_SECTION_CONTRIBUTION_SIZE;
    u64 section_map_size = 4 + (u64)input.section_count * 20;
    u64 source_info_size = align_forward(4 + (u64)module_count * 4 + (u64)source_file_count * 4 + source_names_size, 4);
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
    u32* module_contribution_offsets = arena_allocate(arena, u32, module_count);
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        PdbModule* source_module = modules + module_index;
        String8 module_name = source_module->name.length ? source_module->name : S8("buster.obj");
        pdb_emit_u32(dbi, 0);
        module_contribution_offsets[module_index] = (u32)dbi->count;
        pdb_emit_zero(dbi, PDB_SECTION_CONTRIBUTION_SIZE);
        pdb_emit_u16(dbi, 0);
        pdb_emit_u16(dbi, (u16)module_stream_indices[module_index]);
        pdb_emit_u32(dbi, module_symbol_sizes[module_index]);
        pdb_emit_u32(dbi, 0);
        pdb_emit_u32(dbi, module_c13_sizes[module_index]);
        pdb_emit_u16(dbi, (u16)source_counts[module_index]);
        pdb_emit_u16(dbi, 0);
        pdb_emit_u32(dbi, 0);
        pdb_emit_u32(dbi, 0);
        pdb_emit_u32(dbi, 0);
        pdb_emit_bytes(dbi, module_name.pointer, module_name.length);
        pdb_emit_zero(dbi, 1);
        pdb_emit_bytes(dbi, module_name.pointer, module_name.length);
        pdb_emit_zero(dbi, 1);
        pdb_emit_align4(dbi);
        u32 contribution_section = source_module->code_section ? source_module->code_section : (input.code_section ? input.code_section : 1);
        u32 contribution_index = contribution_section - 1;
        u32 code_size = source_module->code_size;
        pdb_write_u32_at(dbi, module_contribution_offsets[module_index], contribution_section);
        pdb_write_u32_at(dbi, module_contribution_offsets[module_index] + 4, source_module->code_offset);
        pdb_write_u32_at(dbi, module_contribution_offsets[module_index] + 8, code_size);
        pdb_write_u32_at(dbi, module_contribution_offsets[module_index] + 12,
                         contribution_index < input.section_count ? input.sections[contribution_index].characteristics : 0);
    }
    if (dbi->count - module_info_start != module_info_size)
    {
        return result;
    }
    pdb_emit_u32(dbi, PDB_SECTION_CONTRIBUTION_VERSION);
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        (void)module_index;
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
    pdb_emit_u16(dbi, (u16)module_count);
    pdb_emit_u16(dbi, (u16)source_file_count);
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        pdb_emit_u16(dbi, 0);
    }
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        pdb_emit_u16(dbi, (u16)source_counts[module_index]);
    }
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
        pdb_write_u32_at(names, names_bucket_start + (u64)bucket * 4, all_names_offsets[file_index]);
    }
    pdb_emit_u32(names, source_file_count);
    // Stream 7 stays empty: no symbol records are published yet.
    for (u32 index = 0; index < stream_count; index += 1)
    {
        if (streams[index].overflow)
        {
            return result;
        }
    }
#undef PDB_STREAM_BEGIN

    // Lay the streams out as MSF blocks, then the directory and its block map.
    u32* stream_block_counts = arena_allocate(arena, u32, stream_count);
    u32 data_block_count = 0;
    for (u32 index = 0; index < stream_count; index += 1)
    {
        stream_block_counts[index] = (u32)((streams[index].count + PDB_BLOCK_SIZE - 1) / PDB_BLOCK_SIZE);
        data_block_count += stream_block_counts[index];
    }
    u64 directory_size = 4 + (u64)stream_count * 4 + (u64)data_block_count * 4;
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
    u32* superblock = arena_allocate(arena, u32, 6);
    superblock[0] = PDB_BLOCK_SIZE;
    superblock[1] = PDB_FREE_BLOCK_MAP;
    superblock[2] = block_count;
    superblock[3] = (u32)directory_size;
    superblock[4] = 0;
    superblock[5] = block_map_block;
    memcpy(bytes + 32, superblock, sizeof(*superblock) * 6);
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
    pdb_emit_u32(&directory, stream_count);
    for (u32 index = 0; index < stream_count; index += 1)
    {
        pdb_emit_u32(&directory, (u32)streams[index].count);
    }
    u32 next_block = PDB_FIRST_DATA_BLOCK;
    for (u32 index = 0; index < stream_count; index += 1)
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
