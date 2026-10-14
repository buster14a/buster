// PDB tests own MSF/container checks, emitted-index ordering and independent
// Windows readback. pdb_tests registers the fixtures; pdb_test_gsi_sorting
// bounds sorting work, and pdb_test_gsi_mixed_reader diagnoses #3752 through
// the installed SDK's Unicode DbgHelp lookup APIs without changing the writer.
#include <buster/tests/compiler/pdb/pdb_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/pdb/pdb_internal.h>
#if BUSTER_WINDOWS
#include <buster/lib/system_headers.h>
#include <dbghelp.h>
#endif

// Reassemble one stream out of the MSF container the way a reader would, so
// the checks below see exactly the bytes a debugger would load.
BUSTER_GLOBAL_LOCAL ByteSlice pdb_test_stream_bytes(Arena* arena, ByteSlice image, u32 stream_index)
{
    ByteSlice result = {0};
    if (image.length < 56)
    {
        return result;
    }
    u32 block_size = pdb_read_u32(image, 32);
    u32 block_count = pdb_read_u32(image, 40);
    u32 directory_size = pdb_read_u32(image, 44);
    u32 block_map = pdb_read_u32(image, 52);
    if (!block_size || block_map >= block_count || directory_size < 4)
    {
        return result;
    }
    u8* directory_bytes = arena_allocate(arena, u8, directory_size);
    u64 copied = 0;
    while (copied < directory_size)
    {
        u32 block = pdb_read_u32(image, (u64)block_map * block_size + copied / block_size * 4);
        if (block >= block_count)
        {
            return result;
        }
        u64 chunk = BUSTER_MIN((u64)block_size, directory_size - copied);
        memcpy(directory_bytes + copied, image.pointer + (u64)block * block_size, chunk);
        copied += chunk;
    }
    ByteSlice directory = {.pointer = directory_bytes, .length = directory_size};
    u32 stream_count = pdb_read_u32(directory, 0);
    if (stream_index >= stream_count || 4 + (u64)stream_count * 4 > directory_size)
    {
        return result;
    }
    // Stream sizes come first, then each stream's block list back to back, so
    // the earlier streams' lists have to be stepped over to find this one's.
    u64 cursor = 4 + (u64)stream_count * 4;
    u32 size = 0;
    for (u32 index = 0; index < stream_count; index += 1)
    {
        size = pdb_read_u32(directory, 4 + (u64)index * 4);
        if (index == stream_index)
        {
            break;
        }
        cursor += ((u64)size + block_size - 1) / block_size * 4;
    }
    if (cursor + ((u64)size + block_size - 1) / block_size * 4 > directory_size)
    {
        return result;
    }
    u8* bytes = arena_allocate(arena, u8, size ? size : 1);
    u64 written = 0;
    while (written < size)
    {
        u32 block = pdb_read_u32(directory, cursor + written / block_size * 4);
        if (block >= block_count)
        {
            return result;
        }
        u64 chunk = BUSTER_MIN((u64)block_size, size - written);
        memcpy(bytes + written, image.pointer + (u64)block * block_size, chunk);
        written += chunk;
    }
    return (ByteSlice){.pointer = bytes, .length = size};
}

BUSTER_GLOBAL_LOCAL void pdb_test_emit_type_u16(PdbTestTypeBuffer* buffer, u16 value)
{
    if (buffer->count + sizeof(value) <= sizeof(buffer->bytes))
    {
        memcpy(buffer->bytes + buffer->count, &value, sizeof(value));
    }
    buffer->count += sizeof(value);
}

BUSTER_GLOBAL_LOCAL void pdb_test_emit_type_u32(PdbTestTypeBuffer* buffer, u32 value)
{
    if (buffer->count + sizeof(value) <= sizeof(buffer->bytes))
    {
        memcpy(buffer->bytes + buffer->count, &value, sizeof(value));
    }
    buffer->count += sizeof(value);
}

BUSTER_GLOBAL_LOCAL u64 pdb_test_type_record_begin(PdbTestTypeBuffer* buffer, u16 leaf)
{
    u64 offset = buffer->count;
    pdb_test_emit_type_u16(buffer, 0);
    pdb_test_emit_type_u16(buffer, leaf);
    return offset;
}

// The record length has to cover the trailing CodeView pad leaves, the way
// codeview_type_record_end writes them: a reader steps from one record to the
// next by that length alone, so padding left outside it desynchronizes the
// whole stream.
BUSTER_GLOBAL_LOCAL void pdb_test_type_record_end(PdbTestTypeBuffer* buffer, u64 offset)
{
    for (u64 padding = (4 - (buffer->count & 3)) & 3; padding != 0; padding -= 1)
    {
        if (buffer->count < sizeof(buffer->bytes))
        {
            buffer->bytes[buffer->count] = (u8)(0xf0 + padding);
        }
        buffer->count += 1;
    }
    u16 length = (u16)(buffer->count - offset - 2);
    if (offset + sizeof(length) <= sizeof(buffer->bytes))
    {
        memcpy(buffer->bytes + offset, &length, sizeof(length));
    }
}

BUSTER_GLOBAL_LOCAL void pdb_test_emit_modifier(PdbTestTypeBuffer* buffer, u32 modified)
{
    u64 record = pdb_test_type_record_begin(buffer, PDB_TEST_LF_MODIFIER);
    pdb_test_emit_type_u32(buffer, modified);
    pdb_test_emit_type_u16(buffer, 1);
    pdb_test_type_record_end(buffer, record);
}

BUSTER_GLOBAL_LOCAL void pdb_test_emit_pointer(PdbTestTypeBuffer* buffer, u32 referent)
{
    u64 record = pdb_test_type_record_begin(buffer, PDB_TEST_LF_POINTER);
    pdb_test_emit_type_u32(buffer, referent);
    // The 64-bit near pointer both native CodeView targets use.
    pdb_test_emit_type_u32(buffer, 0x0c);
    pdb_test_type_record_end(buffer, record);
}

// The three records a module contributes: a const-qualified base type, a
// pointer to it, and a pointer straight to `int`. Only `base` differs between
// the two modules, so the two pointer records come out byte-identical while
// meaning different things.
BUSTER_GLOBAL_LOCAL PdbTestTypeBuffer pdb_test_build_types(u32 base)
{
    PdbTestTypeBuffer buffer = {0};
    pdb_test_emit_type_u32(&buffer, 4);
    pdb_test_emit_modifier(&buffer, base);
    pdb_test_emit_pointer(&buffer, 0x1000);
    pdb_test_emit_pointer(&buffer, PDB_TEST_T_INT32);
    return buffer;
}


// Four mixed-width records force the remap to follow CodeView record boundaries,
// not the producer's current eight-byte no-digest shape.
BUSTER_GLOBAL_LOCAL void pdb_test_store_u32(u8* bytes, u64 offset, u32 value)
{
    memcpy(bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_checksum_records(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u8 blob[512] = {0};
    char const names[] = "\0unused-prefix.c\0none.c\0md5.c\0sha1.c\0sha256.c";
    u32 name_offsets[] = {17, 24, 30, 37};
    u32 mapped_offsets[] = {1, 8, 14, 21};
    u32 record_offsets[] = {0, 8, 32, 60};
    u8 digest_sizes[] = {0, 16, 20, 32};
    pdb_test_store_u32(blob, 0, 4);
    pdb_test_store_u32(blob, 4, 0xf3);
    pdb_test_store_u32(blob, 8, sizeof(names));
    memcpy(blob + 12, names, sizeof(names));
    u32 checksum_header = 12 + (((u32)sizeof(names) + 3) & ~(u32)3);
    u32 checksum_start = checksum_header + 8;
    pdb_test_store_u32(blob, checksum_header, 0xf4);
    pdb_test_store_u32(blob, checksum_header + 4, 100);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(record_offsets); index += 1)
    {
        u8* record = blob + checksum_start + record_offsets[index];
        u32 stride = (6 + (u32)digest_sizes[index] + 3) & ~(u32)3;
        memset(record, 0xcc, stride);
        pdb_test_store_u32(record, 0, name_offsets[index]);
        record[4] = digest_sizes[index];
        record[5] = (u8)index;
        for (u32 byte = 0; byte < digest_sizes[index]; byte += 1)
        {
            record[6 + byte] = (u8)(0x40 + index * 16 + byte);
        }
    }
    u32 lines_header = checksum_start + 100;
    u32 lines_start = lines_header + 8;
    pdb_test_store_u32(blob, lines_header, 0xf2);
    pdb_test_store_u32(blob, lines_header + 4, 92);
    blob[lines_start + 4] = 1; // Already-resolved .text segment.
    pdb_test_store_u32(blob, lines_start + 8, 16);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(record_offsets); index += 1)
    {
        u8* line = blob + lines_start + 12 + index * 20;
        pdb_test_store_u32(line, 0, record_offsets[index]);
        pdb_test_store_u32(line, 4, 1);
        pdb_test_store_u32(line, 8, 20);
        pdb_test_store_u32(line, 12, index * 4);
        pdb_test_store_u32(line, 16, 0x80000001u + index);
    }
    u32 blob_length = lines_start + 92;
    u8 original[sizeof(blob)];
    memcpy(original, blob, sizeof(blob));
    PdbSection section = {
        .name = S8(".text"), .virtual_address = 0x1000, .virtual_size = 16,
        .raw_size = 0x200, .raw_offset = 0x400, .characteristics = 0x60000020,
    };
    PdbInput input = {
        .module_name = S8("checksums.obj"), .codeview_symbols = {.pointer = blob, .length = blob_length},
        .sections = &section, .section_count = 1, .age = 1, .code_section = 1, .code_size = 16, .machine = 0x8664,
    };
    PdbResult built = pdb_build(arguments->arena, input);
    bool first_build_valid = BUSTER_REQUIRE(arguments, built.valid);
    BUSTER_TEST(arguments, memcmp(blob, original, sizeof(blob)) == 0);
    if (first_build_valid)
    {
        ByteSlice module = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_MODULE);
        // Symbol signature, checksum header/payload, line header/payload, then global-ref count.
        if (BUSTER_REQUIRE(arguments, module.length == 4 + 108 + 100 + 4))
        {
            BUSTER_TEST(arguments, pdb_read_u32(module, 4) == 0xf4);
            BUSTER_TEST(arguments, pdb_read_u32(module, 8) == 100);
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(record_offsets); index += 1)
            {
                u32 offset = record_offsets[index];
                u32 stride = (6 + (u32)digest_sizes[index] + 3) & ~(u32)3;
                BUSTER_TEST(arguments, pdb_read_u32(module, 12 + offset) == mapped_offsets[index]);
                BUSTER_TEST(arguments, memcmp(module.pointer + 12 + offset + 4,
                                             original + checksum_start + offset + 4, stride - 4) == 0);
            }
            // File IDs in line blocks are checksum-subsection offsets, not file ordinals.
            BUSTER_TEST(arguments, memcmp(module.pointer + 112, original + lines_header, 100) == 0);
        }
    }
    // Reuse the same record shapes in a second module: its remap must not move
    // any checksum or line-record boundary, and since it names the same paths
    // it points at the first module's /names copies rather than new ones.
    PdbModule modules[2] = {
        {.name = S8("first.obj"), .codeview_symbols = input.codeview_symbols, .code_section = 1, .code_size = 16},
        {.name = S8("second.obj"), .codeview_symbols = input.codeview_symbols, .code_section = 1, .code_offset = 16, .code_size = 16},
    };
    input.modules = modules;
    input.module_count = BUSTER_ARRAY_LENGTH(modules);
    section.virtual_size = 32; // Both modules' sixteen-byte ranges are in the image.
    built = pdb_build(arguments->arena, input);
    if (BUSTER_REQUIRE(arguments, built.valid))
    {
        ByteSlice second = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_COUNT);
        if (BUSTER_REQUIRE(arguments, second.length >= 212))
        {
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(record_offsets); index += 1)
            {
                u32 offset = record_offsets[index];
                u32 stride = (6 + (u32)digest_sizes[index] + 3) & ~(u32)3;
                BUSTER_TEST(arguments, pdb_read_u32(second, 12 + offset) == mapped_offsets[index]);
                BUSTER_TEST(arguments, memcmp(second.pointer + 12 + offset + 4,
                                             original + checksum_start + offset + 4, stride - 4) == 0);
            }
            BUSTER_TEST(arguments, memcmp(second.pointer + 112, original + lines_header, 100) == 0);
        }
    }
    input.modules = 0;
    input.module_count = 0;
    // The containing subsection remains fully present and aligned; only the
    // declared record extent is truncated, including missing inner padding.
    u32 truncated_sizes[] = {1, 4, 5, 6, 7, 9, 13, 14, 16, 29, 30, 31, 33, 59, 61, 99};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(truncated_sizes); index += 1)
    {
        u32 size = truncated_sizes[index];
        pdb_test_store_u32(blob, checksum_header + 4, size);
        input.codeview_symbols.length = checksum_start + ((size + 3) & ~(u32)3);
        built = pdb_build(arguments->arena, input);
        BUSTER_TEST(arguments, !built.valid && !built.bytes.pointer && !built.bytes.length);
    }
    memcpy(blob, original, sizeof(blob));
    input.codeview_symbols.length = blob_length;
    u32 invalid_offsets[] = {sizeof(names), UINT32_MAX};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_offsets); index += 1)
    {
        pdb_test_store_u32(blob, checksum_start, invalid_offsets[index]);
        built = pdb_build(arguments->arena, input);
        BUSTER_TEST(arguments, !built.valid && !built.bytes.pointer && !built.bytes.length);
    }
    pdb_test_store_u32(blob, checksum_start, sizeof(names) - 1);
    blob[12 + sizeof(names) - 1] = 'X';
    built = pdb_build(arguments->arena, input);
    BUSTER_TEST(arguments, !built.valid && !built.bytes.pointer && !built.bytes.length);
    memcpy(blob, original, sizeof(blob));
    input.codeview_symbols.length = blob_length - 1;
    built = pdb_build(arguments->arena, input);
    BUSTER_TEST(arguments, !built.valid);
    return result;
}


// Scope links are offsets in the final module symbol stream, including its
// four-byte signature, not offsets into the input .debug$S subsection layout.
// Independent restatement of the classic PDB string hash (hashStringV1).
BUSTER_GLOBAL_LOCAL u32 pdb_test_hash_v1(String8 text)
{
    u32 result = 0;
    u64 index = 0;
    for (; index + 4 <= text.length; index += 4)
    {
        result ^= pdb_read_u32((ByteSlice){.pointer = (u8*)text.pointer, .length = text.length}, index);
    }
    if (text.length - index >= 2)
    {
        result ^= (u32)((u8)text.pointer[index] | (u8)text.pointer[index + 1] << 8);
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

// Reads the name of the symbol-records-stream record at `offset`.
BUSTER_GLOBAL_LOCAL String8 pdb_test_record_name(ByteSlice records, u32 offset)
{
    String8 name = {0};
    u16 length = 0;
    u16 kind = 0;
    if ((u64)offset + 4 <= records.length)
    {
        memcpy(&length, records.pointer + offset, sizeof(length));
        memcpy(&kind, records.pointer + offset + 2, sizeof(kind));
    }
    // S_PROCREF, S_PUB32 and S_[GL]DATA32 all place their name after 14 bytes.
    bool named = kind == PDB_TEST_S_PROCREF || kind == PDB_TEST_S_PUB32 || kind == PDB_TEST_S_GDATA32 || kind == PDB_TEST_S_LDATA32;
    u64 name_offset = named ? 14 : 0;
    u64 end = (u64)offset + 2 + length;
    if (name_offset && end <= records.length)
    {
        u64 cursor = offset + name_offset;
        while (cursor < end && records.pointer[cursor])
        {
            cursor += 1;
        }
        if (cursor < end)
        {
            name = (String8){.pointer = (char8*)records.pointer + offset + name_offset, .length = cursor - offset - name_offset};
        }
    }
    return name;
}

// Re-derives one GSI hash table: every hash record names a record whose
// hashStringV1 bucket is the bucket the bitmap and bucket offsets place it in.
BUSTER_GLOBAL_LOCAL bool pdb_test_check_gsi(ByteSlice hash, ByteSlice records, u32* record_count)
{
    enum { BUCKETS = 4096, BITMAP_WORDS = (4096 + 32) / 32 };
    bool valid = hash.length >= 16 && pdb_read_u32(hash, 0) == 0xffffffffu && pdb_read_u32(hash, 4) == 0xf12f091au;
    u32 hr_size = valid ? pdb_read_u32(hash, 8) : 0;
    u32 bucket_bytes = valid ? pdb_read_u32(hash, 12) : 0;
    u32 count = hr_size / 8;
    valid = valid && !(hr_size % 8) && (u64)16 + hr_size + bucket_bytes == hash.length && bucket_bytes >= BITMAP_WORDS * 4;
    u64 bitmap = 16 + (u64)hr_size;
    u64 starts = bitmap + BITMAP_WORDS * 4;
    u32 present = 0;
    for (u32 bucket = 0; valid && bucket < BUCKETS; bucket += 1)
    {
        if (pdb_read_u32(hash, bitmap + bucket / 32 * 4) & (1u << (bucket % 32)))
        {
            valid = starts + (u64)present * 4 + 4 <= hash.length;
            u32 first = valid ? pdb_read_u32(hash, starts + (u64)present * 4) / 12 : 0;
            u32 next = count;
            // The next present bucket's start ends this one.
            for (u32 later = bucket + 1; valid && later < BUCKETS && next == count; later += 1)
            {
                if (pdb_read_u32(hash, bitmap + later / 32 * 4) & (1u << (later % 32)))
                {
                    next = pdb_read_u32(hash, starts + (u64)present * 4 + 4) / 12;
                }
            }
            valid = valid && first < next && next <= count;
            for (u32 index = first; valid && index < next; index += 1)
            {
                u32 offset = pdb_read_u32(hash, 16 + (u64)index * 8);
                String8 name = offset ? pdb_test_record_name(records, offset - 1) : (String8){0};
                valid = name.length && pdb_read_u32(hash, 20 + (u64)index * 8) == 1 && pdb_test_hash_v1(name) % BUCKETS == bucket;
            }
            present += 1;
        }
    }
    valid = valid && starts + (u64)present * 4 == hash.length;
    *record_count = count;
    return valid;
}

// Global and public indexes (#3411): DbgHelp treats a PDB with empty ones as
// public-only. Check the hash tables, the module references and the address map.
BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_global_indexes(UnitTestArguments* arguments, PdbResult built)
{
    UnitTestResult result = {0};
    ByteSlice records = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_SYMBOL_RECORDS);
    ByteSlice globals = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_GLOBALS);
    ByteSlice publics = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_PUBLICS);
    ByteSlice module = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_MODULE);
    u32 global_count = 0;
    BUSTER_TEST(arguments, records.length && pdb_test_check_gsi(globals, records, &global_count) && global_count);
    // Every S_PROCREF names the module-stream offset of a same-named S_GPROC32.
    u32 references = 0;
    for (u32 index = 0; index < global_count; index += 1)
    {
        u32 offset = pdb_read_u32(globals, 16 + (u64)index * 8) - 1;
        u16 kind = 0;
        memcpy(&kind, records.pointer + offset + 2, sizeof(kind));
        if (kind == PDB_TEST_S_PROCREF)
        {
            u32 module_offset = pdb_read_u32(records, (u64)offset + 8);
            u16 module_index = 0;
            memcpy(&module_index, records.pointer + offset + 12, sizeof(module_index));
            u16 target_kind = 0;
            String8 name = pdb_test_record_name(records, offset);
            bool target = name.pointer && module_index == 1 && (u64)module_offset + 40 + name.length <= module.length;
            if (target)
            {
                memcpy(&target_kind, module.pointer + module_offset + 2, sizeof(target_kind));
            }
            BUSTER_TEST(arguments, target && target_kind == PDB_TEST_S_GPROC32 &&
                                   !memcmp(module.pointer + module_offset + 39, name.pointer, name.length));
            references += 1;
        }
    }
    BUSTER_TEST(arguments, references >= 2);
    // Publics: header, the same hash form, then an address map sorted by
    // (section, offset) over S_PUB32 records.
    u32 hash_size = publics.length >= 28 ? pdb_read_u32(publics, 0) : 0;
    u32 map_size = publics.length >= 28 ? pdb_read_u32(publics, 4) : 0;
    bool header = publics.length >= 28 && (u64)28 + hash_size + map_size == publics.length && !(map_size % 4);
    u32 public_count = 0;
    BUSTER_TEST(arguments, header && pdb_test_check_gsi((ByteSlice){.pointer = publics.pointer + 28, .length = hash_size}, records, &public_count) &&
                           public_count == map_size / 4 && public_count >= 2);
    u64 previous = 0;
    for (u32 index = 0; header && index < map_size / 4; index += 1)
    {
        u32 offset = pdb_read_u32(publics, 28 + (u64)hash_size + (u64)index * 4);
        u16 kind = 0;
        u16 section = 0;
        memcpy(&kind, records.pointer + offset + 2, sizeof(kind));
        memcpy(&section, records.pointer + offset + 12, sizeof(section));
        u64 key = (u64)section << 32 | pdb_read_u32(records, (u64)offset + 8);
        BUSTER_TEST(arguments, kind == PDB_TEST_S_PUB32 && key >= previous);
        previous = key;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_scope_links(UnitTestArguments* arguments, PdbResult built, CodeviewResult codeview)
{
    UnitTestResult result = {0};
    ByteSlice stream = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_MODULE);
    u64 symbol_size = 4;
    for (u64 cursor = 4; cursor + 8 <= codeview.symbols.length;)
    {
        u32 kind = pdb_read_u32(codeview.symbols, cursor);
        u32 length = pdb_read_u32(codeview.symbols, cursor + 4);
        symbol_size += kind == 0xf1 ? length : 0;
        cursor += 8 + (((u64)length + 3) & ~(u64)3);
    }
    bool stream_complete = BUSTER_REQUIRE(arguments, symbol_size <= stream.length);
    u32 stack[16] = {0};
    u32 ends[16] = {0};
    u32 depth = 0;
    u32 procedures = 0;
    u32 blocks = 0;
    u32 inlines = 0;
    u64 cursor = 4;
    while (cursor + 4 <= symbol_size && stream_complete)
    {
        u16 length = 0;
        u16 kind = 0;
        memcpy(&length, stream.pointer + cursor, 2);
        memcpy(&kind, stream.pointer + cursor + 2, 2);
        if (!BUSTER_REQUIRE(arguments, length >= 2 && (u64)length + 2 <= symbol_size - cursor))
        {
            break;
        }
        if (kind == 0x1110 || kind == 0x1103 || kind == 0x114d)
        {
            if (!BUSTER_REQUIRE(arguments, depth < BUSTER_ARRAY_LENGTH(stack) && length >= 14))
            {
                break;
            }
            u32 parent = pdb_read_u32(stream, cursor + 4);
            u32 end = pdb_read_u32(stream, cursor + 8);
            BUSTER_TEST(arguments, parent == (depth ? stack[depth - 1] : 0));
            if (BUSTER_REQUIRE(arguments, end > cursor && (u64)end + 4 <= symbol_size))
            {
                u16 end_kind = 0;
                memcpy(&end_kind, stream.pointer + end + 2, 2);
                BUSTER_TEST(arguments, end_kind == (kind == 0x114d ? 0x114e : 0x0006));
            }
            stack[depth] = (u32)cursor;
            ends[depth++] = end;
            procedures += kind == 0x1110;
            blocks += kind == 0x1103;
            inlines += kind == 0x114d;
        }
        else if (kind == 0x0006 || kind == 0x114e)
        {
            if (!BUSTER_REQUIRE(arguments, depth && ends[depth - 1] == cursor))
            {
                break;
            }
            depth -= 1;
        }
        cursor += ((u64)length + 2 + 3) & ~(u64)3;
    }
    BUSTER_TEST(arguments, cursor == symbol_size && !depth);
    BUSTER_TEST(arguments, procedures == 2 && blocks == 4 && inlines == 2);
    return result;
}


BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_continuation_merge(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    enum { MEMBER_COUNT = 4096 };
    DebugTypeField* fields = arena_allocate(arguments->arena, DebugTypeField, MEMBER_COUNT);
    for (u32 index = 0; index < MEMBER_COUNT; index += 1)
    {
        fields[index] = (DebugTypeField){.name = string_format(arguments->arena, S8("continuation_member_with_a_long_name_{u32}"), index),
            .type = 0, .offset = index * 4};
    }
    PdbModule modules[2] = {0};
    for (u32 index = 0; index < 2; index += 1)
    {
        String8 path = index ? S8("second.c") : S8("first.c");
        DebugType types[] = {
            {.kind = DEBUG_TYPE_BASE, .name = index ? S8("f32") : S8("int"), .size = 4, .is_signed = true, .is_float = index != 0},
            {.kind = DEBUG_TYPE_STRUCT, .name = index ? S8("Second") : S8("First"), .size = MEMBER_COUNT * 4,
             .fields = fields, .field_count = MEMBER_COUNT},
        };
        DebugModel model = {.types = types, .type_count = 2, .valid = true};
        CodeviewResult cv = codeview_build(arguments->arena, (CodeviewInput){.model = &model, .file_paths = &path, .file_count = 1,
            .machine = CODEVIEW_MACHINE_X64});
        BUSTER_TEST(arguments, cv.valid);
        modules[index] = (PdbModule){.name = path, .codeview_symbols = cv.symbols, .codeview_types = cv.types, .code_size = 16, .code_section = 1};
    }
    PdbSection section = {.name = S8(".text"), .virtual_address = 0x1000, .virtual_size = 32, .raw_size = 32, .characteristics = 0x60000020};
    PdbResult built = pdb_build(arguments->arena, (PdbInput){.sections = &section, .section_count = 1, .modules = modules,
        .module_count = 2, .code_section = 1, .code_size = 32, .machine = 0x8664, .age = 1});
    if (BUSTER_REQUIRE(arguments, built.valid))
    {
        ByteSlice tpi = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_TPI);
        u64 offsets[64] = {0};
        u32 count = 0;
        u64 cursor = PDB_TEST_TPI_HEADER_SIZE;
        while (cursor + 4 <= tpi.length && count < BUSTER_ARRAY_LENGTH(offsets))
        {
            u16 length = 0;
            memcpy(&length, tpi.pointer + cursor, 2);
            if (length < 2 || (u64)length + 2 > tpi.length - cursor)
            {
                break;
            }
            offsets[count++] = cursor;
            cursor += (u64)length + 2;
        }
        BUSTER_TEST(arguments, cursor == tpi.length && count >= 12);
        u32 structures = 0;
        for (u32 index = 0; index < count; index += 1)
        {
            u16 kind = 0;
            memcpy(&kind, tpi.pointer + offsets[index] + 2, 2);
            if (kind != 0x1505)
            {
                continue;
            }
            structures += 1;
            u32 builtin = tpi.pointer[offsets[index] + 26] == 'F' ? 0x74u : 0x40u;
            u32 field_index = pdb_read_u32(tpi, offsets[index] + 8);
            u32 seen = 0;
            u32 links = 0;
            bool valid = true;
            while (field_index && valid && links < count)
            {
                valid = field_index >= 0x1000 && field_index - 0x1000 < count;
                BUSTER_TEST(arguments, valid);
                if (!valid)
                {
                    break;
                }
                u64 start = offsets[field_index - 0x1000];
                u16 length = 0;
                memcpy(&length, tpi.pointer + start, 2);
                memcpy(&kind, tpi.pointer + start + 2, 2);
                BUSTER_TEST(arguments, kind == 0x1203);
                u64 end = start + 2 + length;
                cursor = start + 4;
                u32 next = 0;
                while (cursor + 2 <= end && valid)
                {
                    memcpy(&kind, tpi.pointer + cursor, 2);
                    if (kind == 0x1404)
                    {
                        valid = end - cursor == 8;
                        next = pdb_read_u32(tpi, cursor + 4);
                        cursor += 8;
                    }
                    else
                    {
                        valid = kind == 0x150d && end - cursor >= 15 && seen < MEMBER_COUNT;
                        if (!valid)
                        {
                            break;
                        }
                        u32 type = pdb_read_u32(tpi, cursor + 4);
                        BUSTER_TEST(arguments, type >= 0x1000 && type - 0x1000 < count);
                        if (type >= 0x1000 && type - 0x1000 < count)
                        {
                            BUSTER_TEST(arguments, pdb_read_u32(tpi, offsets[type - 0x1000] + 4) == builtin);
                        }
                        BUSTER_TEST(arguments, pdb_read_u32(tpi, cursor + 10) == seen * 4);
                        cursor += 14;
                        while (cursor < end && tpi.pointer[cursor])
                        {
                            cursor += 1;
                        }
                        valid = cursor < end;
                        cursor = (cursor + 1 + 3) & ~(u64)3;
                        seen += 1;
                    }
                }
                BUSTER_TEST(arguments, valid && cursor == end);
                field_index = next;
                links += next != 0;
            }
            BUSTER_TEST(arguments, valid && !field_index && seen == MEMBER_COUNT && links >= 2);
        }
        BUSTER_TEST(arguments, structures == 2);
    }
    return result;
}

// This reader decodes little-endian fields itself; it never calls the writer's
// layout allocator or assumes consecutive stream/directory blocks.
BUSTER_GLOBAL_LOCAL u32 pdb_test_load32(u8 const* bytes)
{
    return (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

BUSTER_GLOBAL_LOCAL bool pdb_test_check_msf(Arena* arena, ByteSlice image)
{
    bool valid = image.length >= 56;
    u32 block_size = valid ? pdb_test_load32(image.pointer + 32) : 0;
    u32 block_count = valid ? pdb_test_load32(image.pointer + 40) : 0;
    u32 directory_size = valid ? pdb_test_load32(image.pointer + 44) : 0;
    u32 block_map = valid ? pdb_test_load32(image.pointer + 52) : 0;
    valid = valid && block_size == 4096 && (u64)block_count * block_size == image.length &&
            directory_size >= 4 && block_map < block_count &&
            ((u64)directory_size + block_size - 1) / block_size <= block_size / 4;
    if (valid)
    {
        u8* owners = arena_allocate_zeroed(arena, u8, block_count);
        owners[0] = 1;
        for (u32 block = 1; block < block_count; block += 1)
        {
            if (block % block_size == 1 || block % block_size == 2)
            {
                owners[block] = 1;
            }
        }
        valid = !owners[block_map];
        owners[block_map] = 1;
        u8* directory = arena_allocate(arena, u8, directory_size);
        for (u64 offset = 0; offset < directory_size && valid; offset += block_size)
        {
            u32 block = pdb_test_load32(image.pointer + (u64)block_map * block_size + offset / block_size * 4);
            valid = block < block_count && !owners[block];
            if (valid)
            {
                owners[block] = 1;
                memcpy(directory + offset, image.pointer + (u64)block * block_size, BUSTER_MIN((u64)block_size, directory_size - offset));
            }
        }
        u32 stream_count = valid ? pdb_test_load32(directory) : 0;
        u64 cursor = 4 + (u64)stream_count * 4;
        valid = valid && cursor <= directory_size;
        for (u32 stream = 0; stream < stream_count && valid; stream += 1)
        {
            u32 size = pdb_test_load32(directory + 4 + (u64)stream * 4);
            u64 count = ((u64)size + block_size - 1) / block_size;
            valid = count <= (directory_size - cursor) / 4;
            for (u64 index = 0; index < count && valid; index += 1)
            {
                u32 block = pdb_test_load32(directory + cursor);
                cursor += 4;
                valid = block < block_count && !owners[block];
                if (valid)
                {
                    owners[block] = 1;
                }
            }
        }
        valid = valid && cursor == directory_size;
        // The FPM is a logical byte stream whose physical blocks are separated
        // by BlockSize blocks. Its next block describes another 8*BlockSize bits.
        for (u64 block = 0; block < ((u64)block_count + block_size * 8 - 1) / (block_size * 8) * (block_size * 8) && valid; block += 1)
        {
            u64 byte_index = block / 8;
            u64 page = byte_index / block_size;
            u64 in_page = byte_index % block_size;
            bool expected_free = block >= block_count || !owners[block];
            for (u32 map = 1; map <= 2; map += 1)
            {
                u64 address = (map + page * block_size) * block_size + in_page;
                bool free = ((image.pointer[address] >> (block % 8)) & 1) != 0;
                valid = valid && free == expected_free;
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_layout_regressions(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = true}});
    u64 payload_size = 17 * 1024 * 1024;
    u8* symbols = arena_allocate_zeroed(arena, u8, payload_size + 12);
    pdb_test_store_u32(symbols, 0, 4);
    pdb_test_store_u32(symbols, 4, 0x80000000); // Ignored C13 subsection.
    pdb_test_store_u32(symbols, 8, (u32)payload_size);
    PdbSection section = {.name = S8(".text"), .virtual_size = 256, .characteristics = 0x60000020};
    PdbModule modules[] = {
        {.name = S8_INITIALIZER("first.obj"), .codeview_symbols = {.pointer = symbols, .length = payload_size + 12},
         .code_section = 1, .code_size = 16},
        {.name = S8_INITIALIZER("second.obj"), .codeview_symbols = {.pointer = symbols, .length = 4},
         .code_section = 1, .code_offset = 32, .code_size = 24},
    };
    PdbResult built = pdb_build(arena, (PdbInput){.sections = &section, .section_count = 1, .modules = modules, .module_count = 2});
    if (BUSTER_REQUIRE(arguments, built.valid))
    {
        BUSTER_TEST(arguments, pdb_test_check_msf(arena, built.bytes));
        ByteSlice dbi = pdb_test_stream_bytes(arena, built.bytes, 3);
        if (BUSTER_REQUIRE(arguments, dbi.length >= 64))
        {
            u64 contribution = 64 + pdb_test_load32(dbi.pointer + 24) + 4;
            if (BUSTER_REQUIRE(arguments, contribution + 56 <= dbi.length))
            {
                BUSTER_TEST(arguments, pdb_test_load32(dbi.pointer + contribution + 8) == 16);
                BUSTER_TEST(arguments, pdb_test_load32(dbi.pointer + contribution + 28 + 4) == 32);
                BUSTER_TEST(arguments, pdb_test_load32(dbi.pointer + contribution + 28 + 8) == 24);
                BUSTER_TEST(arguments, pdb_test_load32(dbi.pointer + contribution + 28 + 16) == 1);
            }
        }
    }
    arena_destroy(arena, 1);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_msf_boundaries(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // Below/above the first reserved pair; map, directory and stream crossings;
    // then below/at/above one bitmap block's 32768-bit capacity.
    u32 sizes[] = {4089, 4090, 4091, 4095, 32717, 32718, 32719};
    u32 expected_blocks[] = {4097, 4100, 4101, 4106, 32767, 32768, 32769};
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(sizes); fixture += 1)
    {
        Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = true}});
        u64 size = (u64)sizes[fixture] * 4096;
        u8* payload = arena_allocate(arena, u8, size);
        memset(payload, 0xa5, size);
        for (u32 block = 0; block < sizes[fixture]; block += 1)
        {
            pdb_test_store_u32(payload, (u64)block * 4096, block);
        }
        ByteWriter stream = {.bytes = payload, .count = size, .capacity = size};
        PdbResult built = pdb_msf_build(arena, &stream, 1);
        if (BUSTER_REQUIRE(arguments, built.valid))
        {
            BUSTER_TEST(arguments, pdb_test_load32(built.bytes.pointer + 40) == expected_blocks[fixture]);
            BUSTER_TEST(arguments, pdb_test_check_msf(arena, built.bytes));
            ByteSlice reconstructed = pdb_test_stream_bytes(arena, built.bytes, 0);
            BUSTER_TEST(arguments, reconstructed.length == size && memcmp(reconstructed.pointer, payload, size) == 0);
        }
        arena_destroy(arena, 1);
    }
    // Unsupported sizes must fail before attempting to read these deliberately
    // tiny backing buffers or allocate a multi-gigabyte image.
    u8 byte = 0;
    ByteWriter invalid = {.bytes = &byte, .count = UINT64_MAX};
    BUSTER_TEST(arguments, !pdb_msf_build(arguments->arena, &invalid, 1).valid);
    invalid.count = UINT32_MAX;
    BUSTER_TEST(arguments, !pdb_msf_build(arguments->arena, &invalid, 1).valid);
    invalid.count = (UINT64_C(1048575) - 100) * 4096;
    BUSTER_TEST(arguments, !pdb_msf_build(arguments->arena, &invalid, 1).valid);
    ByteWriter directory_overflow[] = {
        {.bytes = &byte, .count = UINT64_C(1048573) * 4096}, {.bytes = &byte, .count = 4096},
    };
    BUSTER_TEST(arguments, !pdb_msf_build(arguments->arena, directory_overflow, 2).valid);
    BUSTER_TEST(arguments, !pdb_msf_build(arguments->arena, &invalid, UINT32_MAX).valid);
    return result;
}

BUSTER_GLOBAL_LOCAL s32 pdb_test_address_owner(ByteSlice dbi, u32 section, u32 offset)
{
    s32 owner = -1;
    u64 start = 64 + pdb_test_load32(dbi.pointer + 24);
    u64 size = pdb_test_load32(dbi.pointer + 28);
    for (u64 cursor = start + 4; cursor + 28 <= start + size && cursor + 28 <= dbi.length; cursor += 28)
    {
        u8 const* entry = dbi.pointer + cursor;
        u32 entry_section = pdb_test_load32(entry) & 0xffff;
        u32 entry_offset = pdb_test_load32(entry + 4);
        u32 entry_size = pdb_test_load32(entry + 8);
        if (entry_section == section && offset >= entry_offset && offset - entry_offset < entry_size)
        {
            owner = owner == -1 ? (s32)(pdb_test_load32(entry + 16) & 0xffff) : -2;
        }
    }
    return owner;
}

BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_contributions(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u8 signature[] = {4, 0, 0, 0};
    PdbSection sections[] = {
        {.name = S8_INITIALIZER(".text"), .virtual_size = 256, .characteristics = 0x60000020},
        {.name = S8_INITIALIZER(".data"), .virtual_size = 128, .characteristics = 0xc0000040},
        {.name = S8_INITIALIZER(".rdata"), .virtual_size = 128, .characteristics = 0x40000040},
    };
    PdbContribution first[] = {{.section = 1, .offset = 0, .size = 16}, {.section = 2, .offset = 4, .size = 8}};
    PdbContribution second[] = {{.section = 1, .offset = 32, .size = 24}, {.section = 1, .offset = 80, .size = 16}};
    PdbModule modules[] = {
        {.name = S8_INITIALIZER("first.obj"), .contributions = first, .contribution_count = 2},
        {.name = S8_INITIALIZER("second.obj"), .contributions = second, .contribution_count = 2},
        {.name = S8_INITIALIZER("empty.obj")},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modules); index += 1)
    {
        modules[index].codeview_symbols = (ByteSlice){.pointer = signature, .length = sizeof(signature)};
    }
    PdbInput input = {.sections = sections, .section_count = 3, .modules = modules, .module_count = 3};
    PdbResult built = pdb_build(arguments->arena, input);
    PdbResult repeated = pdb_build(arguments->arena, input);
    if (BUSTER_REQUIRE(arguments, built.valid && repeated.valid))
    {
        BUSTER_TEST(arguments, built.bytes.length == repeated.bytes.length && memcmp(built.bytes.pointer, repeated.bytes.pointer, built.bytes.length) == 0);
        ByteSlice dbi = pdb_test_stream_bytes(arguments->arena, built.bytes, 3);
        if (BUSTER_REQUIRE(arguments, dbi.length >= 64))
        {
            u64 contributions = 64 + pdb_test_load32(dbi.pointer + 24) + 4;
            BUSTER_TEST(arguments, pdb_test_load32(dbi.pointer + 28) == 4 + 4 * 28);
            if (BUSTER_REQUIRE(arguments, contributions + 4 * 28 <= dbi.length))
            {
                u64 module = 64;
                for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modules); index += 1)
                {
                    if (!BUSTER_REQUIRE(arguments, module + 64 <= dbi.length))
                    {
                        break;
                    }
                    if (index < 2)
                    {
                        BUSTER_TEST(arguments, memcmp(dbi.pointer + module + 4, dbi.pointer + contributions + index * 2 * 28, 28) == 0);
                        BUSTER_TEST(arguments, (pdb_test_load32(dbi.pointer + module + 20) & 0xffff) == index);
                    }
                    else
                    {
                        u8 empty[28] = {0};
                        BUSTER_TEST(arguments, memcmp(dbi.pointer + module + 4, empty, sizeof(empty)) == 0);
                    }
                    // Skip the two terminated names and align the descriptor.
                    module += 64;
                    for (u32 name = 0; name < 2; name += 1)
                    {
                        while (module < dbi.length && dbi.pointer[module])
                        {
                            module += 1;
                        }
                        module += 1;
                    }
                    module = (module + 3) & ~(u64)3;
                }
            }
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 1, 0) == 0);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 1, 15) == 0);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 1, 16) == -1);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 1, 32) == 1);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 1, 55) == 1);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 1, 56) == -1);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 1, 79) == -1);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 1, 80) == 1);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 1, 95) == 1);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 2, 4) == 0);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 2, 12) == -1);
            BUSTER_TEST(arguments, pdb_test_address_owner(dbi, 3, 0) == -1);
        }
    }
    PdbContribution saved = second[1];
    second[1].size = UINT32_MAX;
    BUSTER_TEST(arguments, !pdb_build(arguments->arena, input).valid);
    second[1] = saved;
    second[1].offset = UINT32_MAX;
    BUSTER_TEST(arguments, !pdb_build(arguments->arena, input).valid);
    second[1] = saved;
    second[1].section = 4;
    BUSTER_TEST(arguments, !pdb_build(arguments->arena, input).valid);
    second[1] = saved;
    modules[1].contributions = 0;
    BUSTER_TEST(arguments, !pdb_build(arguments->arena, input).valid);
    input.module_count = UINT32_MAX;
    BUSTER_TEST(arguments, !pdb_build(arguments->arena, input).valid);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_large_source_count(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    enum { module_files = 32768, total_files = module_files * 2, name_stride = 8, checksum_stride = 8 };
    Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 28, .flags = {.no_pool = true}});
    // Both modules use the same short names. Distinct names within each module
    // avoid making this count-boundary test a hash-collision stress test.
    u32 string_size = 4 + module_files * name_stride;
    u32 checksum_header = 12 + string_size;
    u32 checksum_start = checksum_header + 8;
    u8* blob = arena_allocate_zeroed(arena, u8, checksum_start + total_files * checksum_stride);
    pdb_test_store_u32(blob, 0, 4);
    pdb_test_store_u32(blob, 4, 0xf3);
    pdb_test_store_u32(blob, 8, string_size);
    char const digits[] = "0123456789abcdef";
    for (u32 file = 0; file < module_files; file += 1)
    {
        u8* name = blob + 12 + 4 + file * name_stride;
        for (u32 digit = 0; digit < 4; digit += 1)
        {
            name[digit] = (u8)digits[(file >> (digit * 4)) & 15];
        }
        memcpy(name + 4, ".c", 2);
    }
    pdb_test_store_u32(blob, checksum_header, 0xf4);
    pdb_test_store_u32(blob, checksum_header + 4, module_files * checksum_stride);
    for (u32 file = 0; file < total_files; file += 1)
    {
        pdb_test_store_u32(blob, checksum_start + file * checksum_stride, 4 + file % module_files * name_stride);
    }
    ByteSlice symbols = {.pointer = blob, .length = checksum_start + module_files * checksum_stride};
    PdbSection section = {.name = S8(".text"), .virtual_size = 16, .characteristics = 0x60000020};
    PdbModule modules[] = {
        {.name = S8_INITIALIZER("first.obj"), .codeview_symbols = symbols},
        {.name = S8_INITIALIZER("second.obj"), .codeview_symbols = symbols},
    };
    PdbInput input = {.sections = &section, .section_count = 1, .modules = modules, .module_count = 2};
    PdbResult built = pdb_build(arena, input);
    if (BUSTER_REQUIRE(arguments, built.valid))
    {
        ByteSlice dbi = pdb_test_stream_bytes(arena, built.bytes, PDB_TEST_STREAM_DBI);
        if (BUSTER_REQUIRE(arguments, dbi.length >= PDB_TEST_DBI_HEADER_SIZE))
        {
            u64 source_start = PDB_TEST_DBI_HEADER_SIZE + (u64)pdb_read_u32(dbi, 24) + pdb_read_u32(dbi, 28) + pdb_read_u32(dbi, 32);
            u64 source_size = pdb_read_u32(dbi, 36);
            bool valid = source_start <= dbi.length && source_size <= dbi.length - source_start &&
                         source_size >= 12 + (u64)total_files * 4;
            if (BUSTER_REQUIRE(arguments, valid))
            {
                // NumSourceFiles wraps to zero; readers recover the full total
                // by summing the two 16-bit ModFileCounts instead.
                BUSTER_TEST(arguments, pdb_read_u32(dbi, source_start) == 2);
                u32 counts = pdb_read_u32(dbi, source_start + 8);
                BUSTER_TEST(arguments, (counts & 0xffff) == module_files && (counts >> 16) == module_files);
                u64 names_start = source_start + 12 + (u64)total_files * 4;
                u64 names_size = source_start + source_size - names_start;
                for (u32 file = 0; file < total_files && valid; file += 1)
                {
                    u32 offset = pdb_read_u32(dbi, source_start + 12 + (u64)file * 4);
                    valid = offset <= names_size && 7 <= names_size - offset;
                    if (valid)
                    {
                        u8 const* expected = blob + 12 + 4 + file % module_files * name_stride;
                        valid = memcmp(dbi.pointer + names_start + offset, expected, 7) == 0;
                    }
                }
                BUSTER_TEST(arguments, valid);
            }
        }
        for (u32 module_index = 0; module_index < BUSTER_ARRAY_LENGTH(modules); module_index += 1)
        {
            u32 stream_index = module_index ? PDB_TEST_STREAM_COUNT : PDB_TEST_STREAM_MODULE;
            ByteSlice module = pdb_test_stream_bytes(arena, built.bytes, stream_index);
            bool valid = module.length == 16 + module_files * checksum_stride;
            BUSTER_TEST(arguments, valid);
            // Both modules name the same paths, so both point at the one
            // /names copy of each.
            for (u32 file = 0; file < module_files && valid; file += 1)
            {
                u32 expected = 1 + file * 7;
                valid = pdb_read_u32(module, 12 + (u64)file * checksum_stride) == expected;
            }
            BUSTER_TEST(arguments, valid);
        }
        ByteSlice names = pdb_test_stream_bytes(arena, built.bytes, PDB_TEST_STREAM_NAMES);
        u64 names_buffer = 1 + (u64)module_files * 7;
        if (BUSTER_REQUIRE(arguments, names.length >= 12 + names_buffer + 8 && pdb_read_u32(names, 8) == names_buffer))
        {
            u32 bucket_count = pdb_read_u32(names, 12 + names_buffer);
            BUSTER_TEST(arguments, names.length == 12 + names_buffer + 4 + (u64)bucket_count * 4 + 4 &&
                                       pdb_read_u32(names, names.length - 4) == module_files);
        }
    }
    // The total may exceed 16 bits, but one module's ModFileCount may not.
    pdb_test_store_u32(blob, checksum_header + 4, total_files * checksum_stride);
    modules[0].codeview_symbols.length = checksum_start + total_files * checksum_stride;
    input.module_count = 1;
    BUSTER_TEST(arguments, !pdb_build(arena, input).valid);
    arena_destroy(arena, 1);
    return result;
}

// Hand-authored keys give a numeric ordering oracle for the collision cases.
// Repeating an eight-byte block cancels the XOR hash's four-byte chunks.
BUSTER_GLOBAL_LOCAL String8 pdb_test_collision_name(Arena* arena, u32 key, bool upper)
{
    char8* bytes = arena_allocate(arena, char8, 16);
    bytes[0] = upper ? 'F' : 'f';
    for (u32 digit = 8; digit > 1; digit -= 1)
    {
        bytes[digit - 1] = (char8)('0' + key % 10);
        key /= 10;
    }
    memcpy(bytes + 8, bytes, 8);
    return (String8){.pointer = bytes, .length = 16};
}

BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_gsi_sorting(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 counts[] = {0, 1, 15, 16, 17, 31, 257, 16000};
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(counts) + 3; fixture += 1)
    {
        bool ascending = fixture == BUSTER_ARRAY_LENGTH(counts);
        bool equal = fixture == BUSTER_ARRAY_LENGTH(counts) + 1;
        bool distributed = fixture == BUSTER_ARRAY_LENGTH(counts) + 2;
        u32 count = fixture < BUSTER_ARRAY_LENGTH(counts) ? counts[fixture] : distributed ? 257 : 16000;
        String8* names = arena_allocate(arguments->arena, String8, count ? count : 1);
        u32* order = arena_allocate(arguments->arena, u32, count ? count : 1);
        u32* expected = arena_allocate(arguments->arena, u32, count ? count : 1);
        u32 populations[4096] = {0};
        for (u32 index = 0; index < count; index += 1)
        {
            u32 key = equal ? 0 : ascending ? index : count - index - 1;
            names[index] = distributed ? string_format(arguments->arena, S8("symbol_{u32}_different"), key) :
                                        pdb_test_collision_name(arguments->arena, key, (index & 1) != 0);
            populations[pdb_test_hash_v1(names[index]) % 4096] += 1;
            expected[index] = equal || ascending ? index : count - index - 1;
        }
        u32 present = 0;
        for (u32 bucket = 0; bucket < BUSTER_ARRAY_LENGTH(populations); bucket += 1)
        {
            present += populations[bucket] != 0;
        }
        if (distributed)
        {
            // A small independent selection oracle orders (bucket, length,
            // byte string); these ordinary names are lowercase ASCII.
            for (u32 position = 0; position < count; position += 1)
            {
                u32 least = position;
                for (u32 next = position + 1; next < count; next += 1)
                {
                    String8 left = names[expected[next]];
                    String8 right = names[expected[least]];
                    u32 a = pdb_test_hash_v1(left) % 4096;
                    u32 b = pdb_test_hash_v1(right) % 4096;
                    bool less = a < b || (a == b && (left.length < right.length ||
                                (left.length == right.length && memcmp(left.pointer, right.pointer, left.length) < 0)));
                    least = less ? next : least;
                }
                u32 swap = expected[position];
                expected[position] = expected[least];
                expected[least] = swap;
            }
        }
        u64 comparisons = UINT64_MAX;
        u32 observed = pdb_test_gsi_order(arguments->arena, (SliceString8){.pointer = names, .length = count}, order, &comparisons);
        BUSTER_TEST(arguments, observed == present && (!count || memcmp(order, expected, (u64)count * sizeof(*order)) == 0));
        u32 levels = 0;
        for (u64 width = 1; width < count; width *= 2)
        {
            levels += 1;
        }
        u64 bound = (u64)count * BUSTER_MAX(levels, 16u);
        BUSTER_TEST(arguments, comparisons <= bound);
        BUSTER_TEST(arguments, distributed ? present > 32 : present == (count != 0));
        if (count == 16000)
        {
            arguments->show(arguments, S8("PDB_GSI_ORDER count={u32} ascending={u32} equal={u32} comparisons={u64} bound={u64}\n"),
                            count, (u32)ascending, (u32)equal, comparisons, bound);
        }
    }
    // Length precedes name bytes; ASCII case ties and duplicate UTF-8 names
    // retain their input record order across multiple merge runs.
    String8 mixed[35];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(mixed); index += 1)
    {
        mixed[index] = index < 17 ? S8("zzzzzzzz") : S8("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9");
    }
    u32 mixed_order[35];
    u64 mixed_comparisons = 0;
    pdb_test_gsi_order(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(mixed), mixed_order, &mixed_comparisons);
    // Check stability independently of which of the two format buckets wins.
    u32 previous[2] = {0};
    bool seen[2] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(mixed); index += 1)
    {
        u32 item = mixed_order[index];
        if (BUSTER_REQUIRE(arguments, item < BUSTER_ARRAY_LENGTH(mixed)))
        {
            BUSTER_TEST(arguments, item == index);
            u32 group = item >= 17;
            BUSTER_TEST(arguments, !seen[group] || item > previous[group]);
            previous[group] = item;
            seen[group] = true;
        }
    }
    return result;
}

// Bitwise reflected CRC-32, initial state zero and no final complement: an
// independent restatement of the table-driven hash the PDB writer uses.
BUSTER_GLOBAL_LOCAL u32 pdb_test_record_crc(u8 const* bytes, u64 length)
{
    u32 crc = 0;
    for (u64 offset = 0; offset < length; offset += 1)
    {
        crc ^= bytes[offset];
        for (u32 bit = 0; bit < 8; bit += 1)
        {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
        }
    }
    return crc;
}

// Microsoft's DbgHelp shares no code with Buster's PDB readers and faulted with
// 0xc0000005 on every Buster PDB while those readers passed (#3411). On a
// Windows host with the SDK debuggers, load the PDB through `dbh info`.
BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_dbghelp_consumer(UnitTestArguments* arguments, ByteSlice pdb)
{
    UnitTestResult result = {0};
#if BUSTER_WINDOWS
    String8 dbh = executable_resolve_in_path(arguments->arena, S8("dbh"));
    String8 kits = os_get_environment_variable(S8("ProgramFiles(x86)"));
    if (!dbh.length && kits.length)
    {
        String8 debugger_arch = BUSTER_CPU_ARCH_AARCH64 ? S8("arm64") : S8("x64");
        dbh = string_format(arguments->arena, S8("{S8}\\Windows Kits\\10\\Debuggers\\{S8}\\dbh.exe"), kits, debugger_arch);
    }
    String8 path = buster_test_temporary_path(arguments->arena, S8("buster-dbghelp"), S8(".pdb"));
    ProcessSpawnResult spawn = {0};
    if (dbh.length && BUSTER_REQUIRE(arguments, file_write(path, pdb)))
    {
        String8 args[] = {dbh, S8("-v"), path, S8("info")};
        ProcessSpawnOptions options = {.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                       .use_process_environment = true, .new_process_group = true};
        spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(args), (SliceString8){0}, (SliceString8){0}, options);
    }
    if (spawn.handle)
    {
        ProcessWaitResult wait = os_process_wait_deadline(arguments->arena, spawn, UINT64_C(30000000));
        ByteSlice output = wait.streams[STANDARD_STREAM_OUTPUT];
        String8 text = {.pointer = (char8*)output.pointer, .length = output.length};
        bool loaded = wait.result == PROCESS_RESULT_SUCCESS && string_first_sequence(text, S8("error 0x")) == BUSTER_STRING_NO_MATCH &&
                      string_first_sequence(text, S8("SymPdb")) != BUSTER_STRING_NO_MATCH;
        arguments->show(arguments, S8("PDB_DBGHELP status={S8} tool={S8}\n"), loaded ? S8("loaded") : S8("failed"), dbh);
        if (!loaded)
        {
            arguments->show(arguments, S8("PDB_DBGHELP output={S8}\n"), string_slice(text, 0, BUSTER_MIN(text.length, 2000)));
        }
        BUSTER_TEST(arguments, loaded);
    }
    else
    {
        arguments->show(arguments, S8("PDB_DBGHELP status=unavailable tool={S8}\n"), dbh);
    }
    if (dbh.length)
    {
        os_file_delete(path);
    }
#else
    (void)pdb;
    arguments->show(arguments, S8("PDB_DBGHELP status=unavailable reason=non-windows-host\n"));
#endif
    return result;
}


#if BUSTER_WINDOWS
// Only this fixture calls DbgHelp in-process, serially. Other PDB consumers
// launch dbh in a separate process. These are external reader APIs, not
// compiler callbacks; no DbgHelp import library is added to the build.
typedef BOOL (WINAPI PdbTestSymInitialize)(HANDLE, PCWSTR, BOOL);
typedef BOOL (WINAPI PdbTestSymCleanup)(HANDLE);
typedef DWORD (WINAPI PdbTestSymGetOptions)(void);
typedef DWORD (WINAPI PdbTestSymSetOptions)(DWORD);
typedef DWORD64 (WINAPI PdbTestSymLoadModule)(HANDLE, HANDLE, PCWSTR, PCWSTR, DWORD64, DWORD, PMODLOAD_DATA, DWORD);
typedef BOOL (WINAPI PdbTestSymFromName)(HANDLE, PCWSTR, PSYMBOL_INFOW);

typedef union PdbTestDbghelpProcedure PdbTestDbghelpProcedure;
union PdbTestDbghelpProcedure
{
    FARPROC procedure;
    PdbTestSymInitialize* initialize;
    PdbTestSymCleanup* cleanup;
    PdbTestSymGetOptions* get_options;
    PdbTestSymSetOptions* set_options;
    PdbTestSymLoadModule* load_module;
    PdbTestSymFromName* from_name;
};
#endif

// This diagnostic reports every collision lookup, including misses. Positive
// controls and any returned spelling/address are assertions, but a missing
// cyclic name is the unresolved #3752 observation, not a passing lookup.
BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_mixed_dbghelp_lookup(UnitTestArguments* arguments, ByteSlice pdb, SliceString8 names, u32 permutation)
{
    UnitTestResult result = {0};
#if BUSTER_WINDOWS
    String8 kits = os_get_environment_variable(S8("ProgramFiles(x86)"));
    String8 debugger_arch = BUSTER_CPU_ARCH_AARCH64 ? S8("arm64") : S8("x64");
    String8 dll_path = string_format(arguments->arena, S8("{S8}\\Windows Kits\\10\\Debuggers\\{S8}\\dbghelp.dll"), kits, debugger_arch);
    String16 dll_wide = string16_from_string8(arguments->arena, dll_path, true);
    HMODULE library = LoadLibraryW(dll_wide.pointer);
    DWORD provider_error = library ? 0 : GetLastError();
    arguments->show(arguments, S8("PDB_GSI_LOOKUP provider={S8} available={u32} error={u32}\n"), dll_path, (u32)(library != 0), (u32)provider_error);
    // The hosted Windows fixture requires the same installed SDK provider
    // already used by pdb_test_dbghelp_consumer; its absence is a failed gate.
    if (BUSTER_REQUIRE(arguments, library != 0))
    {
        PdbTestDbghelpProcedure initialize = {.procedure = GetProcAddress(library, "SymInitializeW")};
        PdbTestDbghelpProcedure cleanup = {.procedure = GetProcAddress(library, "SymCleanup")};
        PdbTestDbghelpProcedure get_options = {.procedure = GetProcAddress(library, "SymGetOptions")};
        PdbTestDbghelpProcedure set_options = {.procedure = GetProcAddress(library, "SymSetOptions")};
        PdbTestDbghelpProcedure load_module = {.procedure = GetProcAddress(library, "SymLoadModuleExW")};
        PdbTestDbghelpProcedure from_name = {.procedure = GetProcAddress(library, "SymFromNameW")};
        bool exports = initialize.initialize && cleanup.cleanup && get_options.get_options && set_options.set_options &&
                       load_module.load_module && from_name.from_name;
        if (BUSTER_REQUIRE(arguments, exports))
        {
            String8 path = buster_test_temporary_path(arguments->arena, S8("buster-mixed-gsi"), S8(".pdb"));
            if (BUSTER_REQUIRE(arguments, file_write(path, pdb)))
            {
                String16 path_wide = string16_from_string8(arguments->arena, path, true);
                DWORD previous_options = get_options.get_options();
                for (u32 publics_only = 0; publics_only < 2; publics_only += 1)
                {
                    // SymInitialize accepts a unique nonzero session token
                    // when invade-process is false; it need not be an OS handle.
                    HANDLE session = (HANDLE)pdb.pointer;
                    set_options.set_options(publics_only ? SYMOPT_PUBLICS_ONLY : SYMOPT_NO_PUBLICS);
                    BOOL initialized = initialize.initialize(session, 0, FALSE);
                    DWORD initialize_error = initialized ? 0 : GetLastError();
                    arguments->show(arguments, S8("PDB_GSI_LOOKUP initialized={u32} index={S8} error={u32}\n"),
                                    (u32)(initialized != 0), publics_only ? S8("publics") : S8("globals"), (u32)initialize_error);
                    if (BUSTER_REQUIRE(arguments, initialized != 0))
                    {
                        DWORD64 requested_base = UINT64_C(0x10000000);
                        DWORD64 base = load_module.load_module(session, 0, path_wide.pointer, 0, requested_base, 0x2000, 0, 0);
                        DWORD load_error = base ? 0 : GetLastError();
                        arguments->show(arguments, S8("PDB_GSI_LOOKUP loaded={u32} base={u64} error={u32}\n"),
                                        (u32)(base != 0), (u64)base, (u32)load_error);
                        if (BUSTER_REQUIRE(arguments, base == requested_base))
                        {
                            u32 missing = 0;
                            for (u32 index = 0; index < names.length; index += 1)
                            {
                                String16 query = string16_from_string8(arguments->arena, names.pointer[index], true);
                                u64 symbol_bytes = sizeof(SYMBOL_INFOW) + 64 * sizeof(WCHAR);
                                PSYMBOL_INFOW symbol = (PSYMBOL_INFOW)arena_allocate_zeroed(arguments->arena, u8, symbol_bytes);
                                symbol->SizeOfStruct = (ULONG)sizeof(SYMBOL_INFOW);
                                symbol->MaxNameLen = 64;
                                BOOL found = from_name.from_name(session, query.pointer, symbol);
                                DWORD lookup_error = found ? 0 : GetLastError();
                                u64 expected_address = (u64)base + 0x1000 + (u64)index * 4;
                                bool exact = found && symbol->NameLen == query.length &&
                                             memcmp(symbol->Name, query.pointer, query.length * sizeof(WCHAR)) == 0 &&
                                             symbol->Address == expected_address;
                                missing += !found;
                                arguments->show(arguments,
                                    S8("PDB_GSI_LOOKUP permutation={u32} count={u64} index={S8} name={S8} found={u32} exact={u32} address={u64} expected={u64} error={u32}\n"),
                                    permutation, names.length, publics_only ? S8("publics") : S8("globals"), names.pointer[index],
                                    (u32)(found != 0), (u32)exact, found ? (u64)symbol->Address : 0, expected_address, (u32)lookup_error);
                                BUSTER_TEST(arguments, !found || exact);
                                // The final two names are isolated ASCII and
                                // UTF-8 controls, so a broken provider or name
                                // encoding cannot masquerade as a GSI miss.
                                if (index + 2 >= names.length)
                                {
                                    BUSTER_TEST(arguments, exact);
                                }
                            }
                            arguments->show(arguments, S8("PDB_GSI_LOOKUP_SUMMARY permutation={u32} count={u64} index={S8} missing={u32} diagnostic=1\n"),
                                            permutation, names.length, publics_only ? S8("publics") : S8("globals"), missing);
                        }
                        BUSTER_TEST(arguments, cleanup.cleanup(session) != 0);
                    }
                }
                set_options.set_options(previous_options);
                os_file_delete(path);
            }
        }
        BUSTER_TEST(arguments, FreeLibrary(library) != 0);
    }
#else
    (void)pdb;
    (void)names;
    (void)permutation;
    arguments->show(arguments, S8("PDB_GSI_LOOKUP status=unavailable reason=non-windows-host\n"));
#endif
    return result;
}

// Repeating an eight-byte block keeps every cyclic name in the same format
// bucket. All six permutations exercise both insertion and merge sorting.
// The last two names are positive controls outside the colliding bucket.
BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_gsi_mixed_reader(UnitTestArguments* arguments)
{
    enum { RECORD_SIZE = 32, MAX_NAMES = 22 };
    UnitTestResult result = {0};
    u32 permutations[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    String8 blocks[] = {S8("a0000000"), S8("B0000000"), S8("C\xc3\xa9" "00000"), S8("A0000000"), S8("c\xc3\xa9" "00000")};
    for (u32 large = 0; large < 2; large += 1)
    {
        for (u32 permutation = 0; permutation < BUSTER_ARRAY_LENGTH(permutations); permutation += 1)
        {
            u32 triples = large ? 6 : 1;
            u32 count = triples * 3 + 4;
            String8 names[MAX_NAMES];
            ByteWriter symbols = byte_writer_make(arena_allocate(arguments->arena, u8, 12 + count * RECORD_SIZE), 12 + count * RECORD_SIZE);
            byte_writer_emit_u32_le(&symbols, 4);
            byte_writer_emit_u32_le(&symbols, 0xf1);
            byte_writer_emit_u32_le(&symbols, count * RECORD_SIZE);
            for (u32 index = 0; index < count; index += 1)
            {
                if (index + 2 < count)
                {
                    u32 key = index < triples * 3 ? index / 3 : 0;
                    u32 group = index < triples * 3 ? permutations[permutation][index % 3] : index - triples * 3 + 3;
                    char8* bytes = arena_allocate(arguments->arena, char8, 16);
                    memcpy(bytes, blocks[group].pointer, 8);
                    bytes[7] = (char8)('0' + key);
                    memcpy(bytes + 8, bytes, 8);
                    names[index] = (String8){.pointer = bytes, .length = 16};
                }
                else
                {
                    names[index] = index + 2 == count ? S8("ascii_control") : S8("\xc3\xa9" "_control");
                }
                byte_writer_emit_u16_le(&symbols, RECORD_SIZE - 2);
                byte_writer_emit_u16_le(&symbols, PDB_TEST_S_GDATA32);
                byte_writer_emit_u32_le(&symbols, PDB_TEST_T_INT32);
                byte_writer_emit_u32_le(&symbols, index * 4);
                byte_writer_emit_u16_le(&symbols, 1);
                byte_writer_emit_bytes(&symbols, names[index].pointer, names[index].length);
                byte_writer_emit_zero(&symbols, RECORD_SIZE - 14 - names[index].length);
                u32 bucket = pdb_test_hash_v1(names[index]) % 4096;
                u32 collision_bucket = pdb_test_hash_v1(names[0]) % 4096;
                BUSTER_TEST(arguments, index + 2 < count ? bucket == collision_bucket : bucket != collision_bucket);
            }
            PdbSection section = {.name = S8(".data"), .virtual_address = 0x1000, .virtual_size = count * 4, .raw_size = count * 4};
            String8 path = S8("mixed.c");
            DebugType types[] = {
                {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4, .is_signed = true},
                {.kind = DEBUG_TYPE_POINTER, .element_type = 0, .size = 8},
            };
            DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .valid = true};
            CodeviewResult codeview = codeview_build(arguments->arena, (CodeviewInput){.model = &model, .file_paths = &path, .file_count = 1,
                .machine = CODEVIEW_MACHINE_X64});
            PdbModule module = {.name = S8("mixed.obj"), .codeview_symbols = {.pointer = symbols.bytes, .length = symbols.count},
                                .codeview_types = codeview.types, .code_section = 1};
            PdbInput input = {.module_name = S8("mixed.obj"), .codeview_symbols = {.pointer = symbols.bytes, .length = symbols.count},
                              .sections = &section, .section_count = 1, .age = 1, .machine = 0x8664, .code_section = 1,
                              .modules = &module, .module_count = 1};
            BUSTER_TEST(arguments, !symbols.overflow && codeview.valid);
            PdbResult built = pdb_build(arguments->arena, input);
            if (BUSTER_REQUIRE(arguments, built.valid))
            {
                ByteSlice records = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_SYMBOL_RECORDS);
                for (u32 stream = PDB_TEST_STREAM_GLOBALS; stream <= PDB_TEST_STREAM_PUBLICS; stream += 1)
                {
                    ByteSlice hash = pdb_test_stream_bytes(arguments->arena, built.bytes, stream);
                    if (stream == PDB_TEST_STREAM_PUBLICS && BUSTER_REQUIRE(arguments, hash.length >= 28))
                    {
                        hash = (ByteSlice){.pointer = hash.pointer + 28, .length = pdb_read_u32(hash, 0)};
                    }
                    u32 record_count = 0;
                    BUSTER_TEST(arguments, pdb_test_check_gsi(hash, records, &record_count) && record_count == count);
                }
                UnitTestResult reader = pdb_test_mixed_dbghelp_lookup(arguments, built.bytes,
                    (SliceString8){.pointer = names, .length = count}, permutation);
                result.test_count += reader.test_count;
                result.succeeded_test_count += reader.succeeded_test_count;
            }
        }
    }
    return result;
}

// Exercise both on-disk indexes, including equal ASCII keys separated by
// other keys in the input. The numeric key and input ordinal are the oracle.
BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_gsi_colliding_indexes(UnitTestArguments* arguments)
{
    enum { COUNT = 258, RECORD_SIZE = 32 };
    UnitTestResult result = {0};
    ByteWriter symbols = byte_writer_make(arena_allocate(arguments->arena, u8, 12 + COUNT * RECORD_SIZE), 12 + COUNT * RECORD_SIZE);
    byte_writer_emit_u32_le(&symbols, 4);
    byte_writer_emit_u32_le(&symbols, 0xf1);
    byte_writer_emit_u32_le(&symbols, COUNT * RECORD_SIZE);
    for (u32 index = 0; index < COUNT; index += 1)
    {
        // Two descending runs put each case-insensitive tie in another run.
        u32 key = COUNT / 2 - 1 - index % (COUNT / 2);
        String8 name = pdb_test_collision_name(arguments->arena, key, index >= COUNT / 2);
        byte_writer_emit_u16_le(&symbols, RECORD_SIZE - 2);
        byte_writer_emit_u16_le(&symbols, PDB_TEST_S_GDATA32);
        byte_writer_emit_u32_le(&symbols, PDB_TEST_T_INT32);
        byte_writer_emit_u32_le(&symbols, index * 4);
        byte_writer_emit_u16_le(&symbols, 1);
        byte_writer_emit_bytes(&symbols, name.pointer, name.length);
        byte_writer_emit_zero(&symbols, 2);
    }
    PdbSection section = {.name = S8(".data"), .virtual_address = 0x1000, .virtual_size = COUNT * 4, .raw_size = COUNT * 4};
    // Include a real type record so the Windows reader can load this PDB too.
    String8 path = S8("colliding.c");
    DebugType types[] = {
        {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4, .is_signed = true},
        {.kind = DEBUG_TYPE_POINTER, .element_type = 0, .size = 8},
    };
    DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .valid = true};
    CodeviewResult codeview = codeview_build(arguments->arena, (CodeviewInput){.model = &model, .file_paths = &path, .file_count = 1,
        .machine = CODEVIEW_MACHINE_X64});
    PdbModule module = {.name = S8("colliding.obj"), .codeview_symbols = {.pointer = symbols.bytes, .length = symbols.count},
                        .codeview_types = codeview.types, .code_section = 1};
    PdbInput input = {.module_name = S8("colliding.obj"), .codeview_symbols = {.pointer = symbols.bytes, .length = symbols.count},
                      .sections = &section, .section_count = 1, .age = 1, .machine = 0x8664, .code_section = 1,
                      .modules = &module, .module_count = 1};
    BUSTER_TEST(arguments, !symbols.overflow && codeview.valid);
    PdbResult built = pdb_build(arguments->arena, input);
    PdbResult repeated = pdb_build(arguments->arena, input);
    if (BUSTER_REQUIRE(arguments, built.valid && repeated.valid))
    {
        BUSTER_TEST(arguments, built.bytes.length == repeated.bytes.length && memcmp(built.bytes.pointer, repeated.bytes.pointer, built.bytes.length) == 0);
        UnitTestResult consumer = pdb_test_dbghelp_consumer(arguments, built.bytes);
        result.test_count += consumer.test_count;
        result.succeeded_test_count += consumer.succeeded_test_count;
        ByteSlice records = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_SYMBOL_RECORDS);
        for (u32 stream = PDB_TEST_STREAM_GLOBALS; stream <= PDB_TEST_STREAM_PUBLICS; stream += 1)
        {
            ByteSlice hash = pdb_test_stream_bytes(arguments->arena, built.bytes, stream);
            if (stream == PDB_TEST_STREAM_PUBLICS && BUSTER_REQUIRE(arguments, hash.length >= 28))
            {
                hash = (ByteSlice){.pointer = hash.pointer + 28, .length = pdb_read_u32(hash, 0)};
            }
            u32 record_count = 0;
            if (BUSTER_REQUIRE(arguments, pdb_test_check_gsi(hash, records, &record_count) && record_count == COUNT))
            {
                for (u32 index = 0; index < COUNT; index += 1)
                {
                    u32 ordinal = COUNT / 2 - 1 - index / 2 + (index % 2) * (COUNT / 2);
                    u32 offset_plus_one = pdb_read_u32(hash, 16 + (u64)index * 8);
                    u64 offset = offset_plus_one ? offset_plus_one - 1 : records.length;
                    if (BUSTER_REQUIRE(arguments, offset + RECORD_SIZE <= records.length))
                    {
                        BUSTER_TEST(arguments, pdb_read_u32(records, offset + 8) == ordinal * 4);
                        String8 name = pdb_test_record_name(records, (u32)offset);
                        String8 expected = pdb_test_collision_name(arguments->arena, index / 2, ordinal >= COUNT / 2);
                        BUSTER_TEST(arguments, name.length == expected.length && memcmp(name.pointer, expected.pointer, expected.length) == 0);
                    }
                }
            }
        }
    }
    return result;
}

// DbgHelp faults on a PDB whose TPI stream has no hash stream (#3411), so the
// stream must name one whose buckets and sparse index offsets a reader can use.
// A 4096-member struct spans several 8 KiB index-offset intervals.
BUSTER_GLOBAL_LOCAL UnitTestResult pdb_test_tpi_hashes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    enum { MEMBER_COUNT = 4096, BUCKET_COUNT = 0x3ffff, INDEX_INTERVAL = 8192 };
    // hashStringV1("Node") % 0x3ffff, computed outside Buster.
    u32 const node_bucket = 49125;
    DebugTypeField* fields = arena_allocate(arguments->arena, DebugTypeField, MEMBER_COUNT);
    for (u32 index = 0; index < MEMBER_COUNT; index += 1)
    {
        fields[index] = (DebugTypeField){.name = string_format(arguments->arena, S8("hashed_member_with_a_long_name_{u32}"), index),
            .type = 0, .offset = index * 4};
    }
    String8 path = S8("hashes.c");
    DebugType types[] = {
        {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4, .is_signed = true},
        {.kind = DEBUG_TYPE_STRUCT, .name = S8("Node"), .size = MEMBER_COUNT * 4, .fields = fields, .field_count = MEMBER_COUNT},
        {.kind = DEBUG_TYPE_POINTER, .element_type = 1, .size = 8},
    };
    DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .valid = true};
    for (u32 architecture = 0; architecture < 2; architecture += 1)
    {
        CodeviewResult codeview = codeview_build(arguments->arena, (CodeviewInput){.model = &model, .file_paths = &path, .file_count = 1,
            .machine = architecture ? CODEVIEW_MACHINE_ARM64 : CODEVIEW_MACHINE_X64});
        PdbSection section = {.name = S8(".text"), .virtual_address = 0x1000, .virtual_size = 16, .raw_size = 16, .characteristics = 0x60000020};
        PdbModule module = {.name = path, .codeview_symbols = codeview.symbols, .codeview_types = codeview.types, .code_size = 16, .code_section = 1};
        PdbResult built = pdb_build(arguments->arena, (PdbInput){.sections = &section, .section_count = 1, .modules = &module, .module_count = 1,
            .code_section = 1, .code_size = 16, .machine = architecture ? 0xaa64 : 0x8664, .age = 1});
        if (BUSTER_REQUIRE(arguments, codeview.valid && built.valid))
        {
            ByteSlice tpi = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_TPI);
            u16 hash_stream = UINT16_MAX;
            if (tpi.length >= PDB_TEST_TPI_HEADER_SIZE)
            {
                memcpy(&hash_stream, tpi.pointer + 20, sizeof(hash_stream));
            }
            ByteSlice hashes = pdb_test_stream_bytes(arguments->arena, built.bytes, hash_stream);
            u32 count = tpi.length >= PDB_TEST_TPI_HEADER_SIZE ? pdb_read_u32(tpi, 12) - pdb_read_u32(tpi, 8) : 0;
            u32 values_offset = tpi.length >= PDB_TEST_TPI_HEADER_SIZE ? pdb_read_u32(tpi, 32) : 0;
            u32 values_size = tpi.length >= PDB_TEST_TPI_HEADER_SIZE ? pdb_read_u32(tpi, 36) : 0;
            u32 index_offset = tpi.length >= PDB_TEST_TPI_HEADER_SIZE ? pdb_read_u32(tpi, 40) : 0;
            u32 index_size = tpi.length >= PDB_TEST_TPI_HEADER_SIZE ? pdb_read_u32(tpi, 44) : 0;
            bool header = hash_stream != UINT16_MAX && pdb_read_u32(tpi, 24) == 4 && pdb_read_u32(tpi, 28) == BUCKET_COUNT &&
                          count && values_offset == 0 && values_size == (u64)count * 4 && index_offset == values_size &&
                          index_size && !(index_size & 7) && (u64)index_offset + index_size <= hashes.length && !pdb_read_u32(tpi, 52);
            if (BUSTER_REQUIRE(arguments, header))
            {
                // Walk the records, checking each stored bucket against the
                // independent rule and counting 8 KiB boundary crossings.
                u64 cursor = PDB_TEST_TPI_HEADER_SIZE;
                u32 record = 0;
                u32 node_matches = 0;
                u32 crc_mismatches = 0;
                u32 crossings = 0;
                u32 entry = 0;
                bool offsets_match = true;
                while (cursor + 4 <= tpi.length && record < count)
                {
                    u16 length = 0;
                    u16 kind = 0;
                    memcpy(&length, tpi.pointer + cursor, sizeof(length));
                    memcpy(&kind, tpi.pointer + cursor + 2, sizeof(kind));
                    u64 size = 2 + (u64)length;
                    u64 start = cursor - PDB_TEST_TPI_HEADER_SIZE;
                    u32 bucket = pdb_read_u32(hashes, (u64)record * 4);
                    bool tag = kind == 0x1505 || kind == 0x1506 || kind == 0x1507;
                    if (kind == 0x1505 && bucket == node_bucket)
                    {
                        node_matches += 1;
                    }
                    if (!tag && bucket != pdb_test_record_crc(tpi.pointer + cursor, size) % BUCKET_COUNT)
                    {
                        crc_mismatches += 1;
                    }
                    // Every record whose span reaches a new interval (and the
                    // first) owns the next index-offset entry.
                    if (!record || (start + size) / INDEX_INTERVAL > start / INDEX_INTERVAL)
                    {
                        crossings += 1;
                        offsets_match = offsets_match && (u64)entry * 8 + 8 <= index_size &&
                                        pdb_read_u32(hashes, index_offset + (u64)entry * 8) == PDB_TEST_TYPE_INDEX_BASE + record &&
                                        pdb_read_u32(hashes, index_offset + (u64)entry * 8 + 4) == start;
                        entry += 1;
                    }
                    cursor += size;
                    record += 1;
                }
                BUSTER_TEST(arguments, cursor == tpi.length && record == count);
                BUSTER_TEST(arguments, node_matches == 1 && !crc_mismatches);
                BUSTER_TEST(arguments, offsets_match && crossings > 2 && index_size == crossings * 8);
            }
            if (!architecture)
            {
                UnitTestResult consumer = pdb_test_dbghelp_consumer(arguments, built.bytes);
                result.test_count += consumer.test_count;
                result.succeeded_test_count += consumer.succeeded_test_count;
            }
        }
    }
    return result;
}

UnitTestResult pdb_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = pdb_test_checksum_records(arguments);
    UnitTestResult sorting = pdb_test_gsi_sorting(arguments);
    result.test_count += sorting.test_count;
    result.succeeded_test_count += sorting.succeeded_test_count;
    UnitTestResult colliding = pdb_test_gsi_colliding_indexes(arguments);
    result.test_count += colliding.test_count;
    result.succeeded_test_count += colliding.succeeded_test_count;
    UnitTestResult mixed_reader = pdb_test_gsi_mixed_reader(arguments);
    result.test_count += mixed_reader.test_count;
    result.succeeded_test_count += mixed_reader.succeeded_test_count;
    UnitTestResult hashes = pdb_test_tpi_hashes(arguments);
    result.test_count += hashes.test_count;
    result.succeeded_test_count += hashes.succeeded_test_count;
    UnitTestResult source_count = pdb_test_large_source_count(arguments);
    result.test_count += source_count.test_count;
    result.succeeded_test_count += source_count.succeeded_test_count;
    UnitTestResult layout = pdb_test_layout_regressions(arguments);
    result.test_count += layout.test_count;
    result.succeeded_test_count += layout.succeeded_test_count;
    UnitTestResult boundaries = pdb_test_msf_boundaries(arguments);
    result.test_count += boundaries.test_count;
    result.succeeded_test_count += boundaries.succeeded_test_count;
    UnitTestResult contributions = pdb_test_contributions(arguments);
    result.test_count += contributions.test_count;
    result.succeeded_test_count += contributions.succeeded_test_count;
    UnitTestResult continuations = pdb_test_continuation_merge(arguments);
    result.test_count += continuations.test_count;
    result.succeeded_test_count += continuations.succeeded_test_count;
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
    DebugTypeId model_parameter_types[] = {0};
    DebugType model_types[] = {
        {
            .name = S8("int"),
            .kind = DEBUG_TYPE_BASE,
            .size = 4,
            .alignment = 4,
            .bit_width = 32,
            .is_signed = true,
        },
        {
            .name = S8("pdb_function"),
            .kind = DEBUG_TYPE_FUNCTION,
            .return_type = 0,
            .parameter_types = model_parameter_types,
            .parameter_count = 1,
        },
    };
    DebugLocationRange model_locations[] = {
        {.start = 0, .end = 0x100, .location = {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -8}},
    };
    DebugVariableId model_variable_ids[] = {0};
    DebugVariable model_variables[] = {
        {
            .name = S8("value"),
            .type = 0,
            .declaration = {.source = 0, .line = 4, .column = 5},
            .locations = model_locations,
            .location_count = BUSTER_ARRAY_LENGTH(model_locations),
            .kind = DEBUG_VARIABLE_PARAMETER,
        },
    };
    DebugScope model_scopes[] = {
        {
            .kind = DEBUG_SCOPE_FUNCTION,
            .start = 0,
            .end = 0x100,
            .variables = model_variable_ids,
            .variable_count = BUSTER_ARRAY_LENGTH(model_variable_ids),
        },
        {.kind = DEBUG_SCOPE_LEXICAL, .parent = 0, .start = 0x10, .end = 0x80},
        {.kind = DEBUG_SCOPE_LEXICAL, .parent = 1, .start = 0x20, .end = 0x40},
    };
    DebugFunction model_functions[] = {
        {
            .name = S8("main"),
            .declaration = {.source = 0, .line = 3, .column = 1},
            .type = 1,
            .scope = 0,
            .code_size = 0x100,
        },
        {
            .name = S8("helper"),
            .declaration = {.source = 1, .line = 9, .column = 1},
            .type = 1,
            .scope = 0,
            .code_offset = 0x100,
            .code_size = 0x100,
        },
    };
    DebugInlineSite inline_sites[] = {
        {.function = model_functions, .start = 0x20, .end = 0x40, .has_ranges = true},
        {.function = model_functions + 1, .start = 0x120, .end = 0x140, .has_ranges = true},
    };
    DebugModel model = {
        .inline_sites = inline_sites,
        .inline_site_count = BUSTER_ARRAY_LENGTH(inline_sites),
        .source_paths = files,
        .types = model_types,
        .functions = model_functions,
        .scopes = model_scopes,
        .variables = model_variables,
        .source_count = BUSTER_ARRAY_LENGTH(files),
        .type_count = BUSTER_ARRAY_LENGTH(model_types),
        .function_count = BUSTER_ARRAY_LENGTH(model_functions),
        .scope_count = BUSTER_ARRAY_LENGTH(model_scopes),
        .variable_count = BUSTER_ARRAY_LENGTH(model_variables),
        .valid = true,
    };
    CodeviewResult codeview = codeview_build(arguments->arena, (CodeviewInput){
                                                                  .model = &model,
                                                                  .producer = S8("buster"),
                                                                  .file_paths = files,
                                                                  .functions = functions,
                                                                  .lines = lines,
                                                                  .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                  .function_count = BUSTER_ARRAY_LENGTH(functions),
                                                                  .line_count = BUSTER_ARRAY_LENGTH(lines),
                                                                  .machine = CODEVIEW_MACHINE_X64,
                                                              });
    if (BUSTER_REQUIRE(arguments, codeview.valid))
    {
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
        PdbModule module = {
            .name = S8("demo.obj"),
            .codeview_symbols = codeview.symbols,
            .codeview_types = codeview.types,
            .code_size = 0x200,
            .code_section = 1,
        };
        PdbInput input = {
            .sections = sections,
            .section_count = BUSTER_ARRAY_LENGTH(sections),
            .age = 1,
            .code_section = 1,
            .code_size = 0x200,
            .machine = 0x8664,
            .modules = &module,
            .module_count = 1,
        };
        for (u32 index = 0; index < 16; index += 1)
        {
            input.guid[index] = (u8)(index + 1);
        }
        PdbResult built = pdb_build(arguments->arena, input);
        if (BUSTER_REQUIRE(arguments, built.valid))
        {
            UnitTestResult global_indexes = pdb_test_global_indexes(arguments, built);
            result.test_count += global_indexes.test_count;
            result.succeeded_test_count += global_indexes.succeeded_test_count;
            UnitTestResult scope_links = pdb_test_scope_links(arguments, built, codeview);
            result.test_count += scope_links.test_count;
            result.succeeded_test_count += scope_links.succeeded_test_count;
            BUSTER_TEST(arguments, built.bytes.length % PDB_TEST_BLOCK_SIZE == 0);
            BUSTER_TEST(arguments, memcmp(built.bytes.pointer,
                                          "Microsoft C/C++ MSF 7.00\r\n\x1a"
                                          "DS",
                                          30) == 0);
            u32 block_size = pdb_read_u32(built.bytes, 32);
            u32 block_count = pdb_read_u32(built.bytes, 40);
            u32 directory_size = pdb_read_u32(built.bytes, 44);
            u32 block_map = pdb_read_u32(built.bytes, 52);
            BUSTER_TEST(arguments, block_size == PDB_TEST_BLOCK_SIZE);
            BUSTER_TEST(arguments, (u64)block_count * PDB_TEST_BLOCK_SIZE == built.bytes.length);
            BUSTER_TEST(arguments, block_map < block_count);
            // Walk the directory the way a reader would and check every stream lands
            // inside the file.
            u32 directory_block = pdb_read_u32(built.bytes, (u64)block_map * PDB_TEST_BLOCK_SIZE);
            BUSTER_TEST(arguments, directory_block < block_count);
            u64 directory_base = (u64)directory_block * PDB_TEST_BLOCK_SIZE;
            BUSTER_TEST(arguments, directory_size >= 4);
            u32 stream_count = pdb_read_u32(built.bytes, directory_base);
            // The module's type records add the TPI hash stream after the fixed set.
            if (BUSTER_REQUIRE(arguments, stream_count == PDB_TEST_STREAM_COUNT + 1))
            {
                u64 block_cursor = directory_base + 4 + (u64)stream_count * 4;
                u32 info_block = 0;
                u32 dbi_block = 0;
                u32 module_block = 0;
                for (u32 index = 0; index < stream_count; index += 1)
                {
                    u32 size = pdb_read_u32(built.bytes, directory_base + 4 + (u64)index * 4);
                    u32 blocks = (size + PDB_TEST_BLOCK_SIZE - 1) / PDB_TEST_BLOCK_SIZE;
                    for (u32 block = 0; block < blocks; block += 1)
                    {
                        u32 block_index = pdb_read_u32(built.bytes, block_cursor);
                        block_cursor += 4;
                        BUSTER_TEST(arguments, block_index < block_count);
                        if (!block && index == PDB_TEST_STREAM_INFO)
                        {
                            info_block = block_index;
                        }
                        if (!block && index == PDB_TEST_STREAM_DBI)
                        {
                            dbi_block = block_index;
                        }
                        if (!block && index == PDB_TEST_STREAM_MODULE)
                        {
                            module_block = block_index;
                        }
                    }
                }
                // The info stream must carry the identity the image will repeat.
                BUSTER_TEST(arguments, info_block != 0);
                u64 info_base = (u64)info_block * PDB_TEST_BLOCK_SIZE;
                BUSTER_TEST(arguments, pdb_read_u32(built.bytes, info_base) == PDB_TEST_INFO_VERSION_VC70);
                BUSTER_TEST(arguments, pdb_read_u32(built.bytes, info_base + 8) == 1);
                BUSTER_TEST(arguments, memcmp(built.bytes.pointer + info_base + 12, input.guid, sizeof(input.guid)) == 0);
                // The DBI header must point at the module and section header streams.
                BUSTER_TEST(arguments, dbi_block != 0);
                u64 dbi_base = (u64)dbi_block * PDB_TEST_BLOCK_SIZE;
                BUSTER_TEST(arguments, pdb_read_u32(built.bytes, dbi_base) == 0xffffffff);
                BUSTER_TEST(arguments, pdb_read_u32(built.bytes, dbi_base + 4) == PDB_TEST_DBI_VERSION_V70);
                u16 dbi_machine = 0;
                memcpy(&dbi_machine, built.bytes.pointer + dbi_base + 58, sizeof(dbi_machine));
                BUSTER_TEST(arguments, dbi_machine == 0x8664);
                u32 dbi_module_size = pdb_read_u32(built.bytes, dbi_base + 24);
                u32 dbi_contribution_size = pdb_read_u32(built.bytes, dbi_base + 28);
                BUSTER_TEST(arguments, dbi_module_size != 0 && dbi_contribution_size != 0);
                u16 module_stream = 0;
                memcpy(&module_stream, built.bytes.pointer + dbi_base + PDB_TEST_DBI_HEADER_SIZE + 4 + PDB_TEST_SECTION_CONTRIBUTION_SIZE + 2,
                       sizeof(module_stream));
                BUSTER_TEST(arguments, module_stream == PDB_TEST_STREAM_MODULE);
                BUSTER_TEST(arguments, module_block != 0);
                bool found_procedure = false;
                bool found_local = false;
                bool found_frame = false;
                if (module_block)
                {
                    u64 module_base = (u64)module_block * PDB_TEST_BLOCK_SIZE;
                    u64 module_end = BUSTER_MIN(module_base + PDB_TEST_BLOCK_SIZE, built.bytes.length);
                    u64 symbol_offset = module_base + 4;
                    while (symbol_offset + 4 <= module_end)
                    {
                        u16 record_length = 0;
                        u16 record_kind = 0;
                        memcpy(&record_length, built.bytes.pointer + symbol_offset, sizeof(record_length));
                        memcpy(&record_kind, built.bytes.pointer + symbol_offset + 2, sizeof(record_kind));
                        if (record_length < 2 || symbol_offset + 2 + record_length > module_end)
                        {
                            break;
                        }
                        found_procedure |= record_kind == PDB_TEST_S_GPROC32;
                        found_local |= record_kind == PDB_TEST_S_LOCAL;
                        found_frame |= record_kind == PDB_TEST_S_DEFRANGE_REGISTER_REL;
                        symbol_offset += (UINT64_C(2) + record_length + UINT64_C(3)) & ~UINT64_C(3);
                    }
                }
                BUSTER_TEST(arguments, found_procedure && found_local && found_frame);
#if !BUSTER_IOS
                // Leave the file on disk where external validators can read it. The iOS
                // app keeps the complete PDB validation above in memory instead.
                String8 pdb_path = buster_test_temporary_path(arguments->arena, S8("buster-pdb"), S8(".pdb"));
                BUSTER_TEST(arguments, file_write(pdb_path, built.bytes));
#endif
                // Type indices are local to each object, so records that are byte-identical
                // across two modules can still name different types. Both modules below
                // contribute the same three raw records, but their first record modifies a
                // different base type, so their second record — identical bytes, pointing
                // at local 0x1000 — must survive as two distinct types. Only the third
                // record, a pointer to the same builtin in both, may merge.
                PdbTestTypeBuffer first_types = pdb_test_build_types(PDB_TEST_T_INT32);
                PdbTestTypeBuffer second_types = pdb_test_build_types(PDB_TEST_T_REAL32);
                BUSTER_TEST(arguments, first_types.count == second_types.count && first_types.count <= sizeof(first_types.bytes));
                BUSTER_TEST(arguments, memcmp(first_types.bytes + 16, second_types.bytes + 16, 24) == 0);
                PdbModule merge_modules[] = {
                    {
                        .name = S8("first.obj"),
                        .codeview_symbols = codeview.symbols,
                        .codeview_types = {.pointer = first_types.bytes, .length = first_types.count},
                        .code_size = 0x100,
                        .code_section = 1,
                    },
                    {
                        .name = S8("second.obj"),
                        .codeview_symbols = codeview.symbols,
                        .codeview_types = {.pointer = second_types.bytes, .length = second_types.count},
                        .code_offset = 0x100,
                        .code_size = 0x100,
                        .code_section = 1,
                    },
                };
                PdbInput merge_input = input;
                merge_input.modules = merge_modules;
                merge_input.module_count = BUSTER_ARRAY_LENGTH(merge_modules);
                PdbResult merged = pdb_build(arguments->arena, merge_input);
                BUSTER_TEST(arguments, merged.valid);
                ByteSlice tpi = merged.valid ? pdb_test_stream_bytes(arguments->arena, merged.bytes, PDB_TEST_STREAM_TPI) : (ByteSlice){0};
                BUSTER_TEST(arguments, tpi.length > PDB_TEST_TPI_HEADER_SIZE);
                if (tpi.length > PDB_TEST_TPI_HEADER_SIZE)
                {
                    u32 index_begin = pdb_read_u32(tpi, 8);
                    u32 index_end = pdb_read_u32(tpi, 12);
                    BUSTER_TEST(arguments, index_begin == PDB_TEST_TYPE_INDEX_BASE);
                    // Five types, not four: the two pointers to a modified type stay apart
                    // while the two pointers to `int` collapse into one.
                    BUSTER_TEST(arguments, index_end == PDB_TEST_TYPE_INDEX_BASE + 5);
                    u32 const_int_index = UINT32_MAX;
                    u32 const_float_index = UINT32_MAX;
                    u32 pointer_to_const_int = UINT32_MAX;
                    u32 pointer_to_const_float = UINT32_MAX;
                    u32 pointer_to_int_count = 0;
                    u32 modifier_count = 0;
                    u32 pointer_count = 0;
                    u64 offset = PDB_TEST_TPI_HEADER_SIZE;
                    u32 type_index = PDB_TEST_TYPE_INDEX_BASE;
                    // Two passes: the first names the modifier records, the second checks
                    // which of them each pointer record refers to.
                    for (u32 pass = 0; pass < 2; pass += 1)
                    {
                        offset = PDB_TEST_TPI_HEADER_SIZE;
                        type_index = PDB_TEST_TYPE_INDEX_BASE;
                        while (offset + 4 <= tpi.length)
                        {
                            u16 record_length = 0;
                            u16 leaf = 0;
                            memcpy(&record_length, tpi.pointer + offset, sizeof(record_length));
                            memcpy(&leaf, tpi.pointer + offset + 2, sizeof(leaf));
                            if (record_length < 2 || offset + 2 + record_length > tpi.length)
                            {
                                break;
                            }
                            u32 referenced = pdb_read_u32(tpi, offset + 4);
                            if (leaf == PDB_TEST_LF_MODIFIER)
                            {
                                modifier_count += !pass;
                                const_int_index = referenced == PDB_TEST_T_INT32 ? type_index : const_int_index;
                                const_float_index = referenced == PDB_TEST_T_REAL32 ? type_index : const_float_index;
                            }
                            else if (leaf == PDB_TEST_LF_POINTER && pass)
                            {
                                pointer_count += 1;
                                pointer_to_int_count += referenced == PDB_TEST_T_INT32;
                                pointer_to_const_int = referenced == const_int_index ? type_index : pointer_to_const_int;
                                pointer_to_const_float = referenced == const_float_index ? type_index : pointer_to_const_float;
                            }
                            offset += (UINT64_C(2) + record_length + UINT64_C(3)) & ~UINT64_C(3);
                            type_index += 1;
                        }
                    }
                    BUSTER_TEST(arguments, type_index == index_end);
                    BUSTER_TEST(arguments, modifier_count == 2 && pointer_count == 3);
                    BUSTER_TEST(arguments, const_int_index != UINT32_MAX && const_float_index != UINT32_MAX);
                    BUSTER_TEST(arguments, const_int_index != const_float_index);
                    // The bug this guards against merged these two into one record, so the
                    // second module's `const float*` resolved to the first module's
                    // `const int*`.
                    BUSTER_TEST(arguments, pointer_to_const_int != UINT32_MAX && pointer_to_const_float != UINT32_MAX);
                    BUSTER_TEST(arguments, pointer_to_const_int != pointer_to_const_float);
                    BUSTER_TEST(arguments, pointer_to_int_count == 1);
                    // Both modules were handed the same symbol blob, whose S_GPROC32 names
                    // local type 0x1001 — the pointer record. Each module's symbols must
                    // therefore land on its own pointer, not on a shared one.
                    u32 procedure_types[BUSTER_ARRAY_LENGTH(merge_modules)] = {UINT32_MAX, UINT32_MAX};
                    for (u32 module_index = 0; module_index < BUSTER_ARRAY_LENGTH(merge_modules); module_index += 1)
                    {
                        u32 stream_index = module_index ? PDB_TEST_STREAM_COUNT + module_index - 1 : PDB_TEST_STREAM_MODULE;
                        ByteSlice module_symbols = pdb_test_stream_bytes(arguments->arena, merged.bytes, stream_index);
                        u64 symbol_offset = 4;
                        while (symbol_offset + 4 <= module_symbols.length)
                        {
                            u16 record_length = 0;
                            u16 record_kind = 0;
                            memcpy(&record_length, module_symbols.pointer + symbol_offset, sizeof(record_length));
                            memcpy(&record_kind, module_symbols.pointer + symbol_offset + 2, sizeof(record_kind));
                            if (record_length < 2 || symbol_offset + 2 + record_length > module_symbols.length)
                            {
                                break;
                            }
                            if (record_kind == PDB_TEST_S_GPROC32 && symbol_offset + 32 <= module_symbols.length)
                            {
                                procedure_types[module_index] = pdb_read_u32(module_symbols, symbol_offset + 28);
                            }
                            symbol_offset += (UINT64_C(2) + record_length + UINT64_C(3)) & ~UINT64_C(3);
                        }
                    }
                    BUSTER_TEST(arguments, procedure_types[0] == pointer_to_const_int);
                    BUSTER_TEST(arguments, procedure_types[1] == pointer_to_const_float);

                    // S_LDATA32 remains in its module stream and needs the same
                    // module-local type-index rewrite as S_GDATA32. Both modules
                    // spell type 0x1001, but their merged pointer types differ.
                    DebugVariable data_variables[] = {
                        {.name = S8("hidden"), .linkage_name = S8("hidden"), .type = 1,
                         .symbol = {.value = 10}, .kind = DEBUG_VARIABLE_GLOBAL, .is_internal = true},
                        {.name = S8("public_data"), .linkage_name = S8("public_data"), .type = 1,
                         .symbol = {.value = 11}, .kind = DEBUG_VARIABLE_GLOBAL},
                    };
                    DebugType data_types[] = {
                        {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4, .is_signed = true},
                        {.kind = DEBUG_TYPE_POINTER, .element_type = 0, .size = 8},
                    };
                    DebugModel data_model = {.types = data_types, .type_count = BUSTER_ARRAY_LENGTH(data_types), .variables = data_variables,
                                             .variable_count = BUSTER_ARRAY_LENGTH(data_variables), .valid = true};
                    CodeviewResult data_codeview = codeview_build(arguments->arena, (CodeviewInput){.model = &data_model,
                        .file_paths = files, .file_count = BUSTER_ARRAY_LENGTH(files), .machine = CODEVIEW_MACHINE_X64});
                    BUSTER_TEST(arguments, data_codeview.valid);
                    if (data_codeview.valid)
                    {
                        PdbModule data_modules[] = {merge_modules[0], merge_modules[1]};
                        data_modules[0].codeview_symbols = data_codeview.symbols;
                        data_modules[1].codeview_symbols = data_codeview.symbols;
                        PdbInput data_input = merge_input;
                        data_input.modules = data_modules;
                        PdbResult data_merged = pdb_build(arguments->arena, data_input);
                        BUSTER_TEST(arguments, data_merged.valid);
                        if (data_merged.valid)
                        {
                            for (u32 module_index = 0; module_index < BUSTER_ARRAY_LENGTH(data_modules); module_index += 1)
                            {
                                u32 stream_index = module_index ? PDB_TEST_STREAM_COUNT + module_index - 1 : PDB_TEST_STREAM_MODULE;
                                ByteSlice symbols = pdb_test_stream_bytes(arguments->arena, data_merged.bytes, stream_index);
                                u32 expected_type = module_index ? pointer_to_const_float : pointer_to_const_int;
                                u32 local_count = 0;
                                u32 public_count = 0;
                                u64 record_cursor = 4;
                                while (record_cursor + 4 <= symbols.length)
                                {
                                    u16 length = 0;
                                    u16 kind = 0;
                                    memcpy(&length, symbols.pointer + record_cursor, sizeof(length));
                                    memcpy(&kind, symbols.pointer + record_cursor + 2, sizeof(kind));
                                    if (length < 2 || (u64)length + 2 > symbols.length - record_cursor)
                                    {
                                        break;
                                    }
                                    if ((kind == PDB_TEST_S_LDATA32 || kind == PDB_TEST_S_GDATA32) && length >= 16)
                                    {
                                        u64 name = record_cursor + 14;
                                        u64 end = record_cursor + 2 + length;
                                        u64 name_end = name;
                                        while (name_end < end && symbols.pointer[name_end]) name_end += 1;
                                        String8 spelling = {.pointer = (char8*)symbols.pointer + name, .length = name_end - name};
                                        u32 mapped_type = pdb_read_u32(symbols, record_cursor + 4);
                                        local_count += kind == PDB_TEST_S_LDATA32 && name_end < end && string_equal(spelling, S8("hidden")) &&
                                                       mapped_type == expected_type;
                                        public_count += kind == PDB_TEST_S_GDATA32 && name_end < end && string_equal(spelling, S8("public_data")) &&
                                                        mapped_type == expected_type;
                                    }
                                    record_cursor += (UINT64_C(2) + length + UINT64_C(3)) & ~UINT64_C(3);
                                }
                                BUSTER_TEST(arguments, local_count == 1 && public_count == 1);
                            }
                            BUSTER_TEST(arguments, pointer_to_const_float != PDB_TEST_TYPE_INDEX_BASE + 1);
                            // The global index copies each module's top-level data record
                            // with its own merged type; only the global one is public.
                            ByteSlice records = pdb_test_stream_bytes(arguments->arena, data_merged.bytes, PDB_TEST_STREAM_SYMBOL_RECORDS);
                            u32 hidden_types[2] = {0};
                            u32 public_types[2] = {0};
                            u32 hidden_count = 0;
                            u32 data_count = 0;
                            u32 public_records = 0;
                            for (u64 cursor = 0; cursor + 4 <= records.length;)
                            {
                                u16 length = 0;
                                u16 kind = 0;
                                memcpy(&length, records.pointer + cursor, sizeof(length));
                                memcpy(&kind, records.pointer + cursor + 2, sizeof(kind));
                                String8 name = pdb_test_record_name(records, (u32)cursor);
                                if (kind == PDB_TEST_S_LDATA32 && string_equal(name, S8("hidden")) && hidden_count < 2)
                                {
                                    hidden_types[hidden_count++] = pdb_read_u32(records, cursor + 4);
                                }
                                if (kind == PDB_TEST_S_GDATA32 && string_equal(name, S8("public_data")) && data_count < 2)
                                {
                                    public_types[data_count++] = pdb_read_u32(records, cursor + 4);
                                }
                                public_records += kind == PDB_TEST_S_PUB32 && string_equal(name, S8("public_data"));
                                cursor += 2 + (u64)length;
                            }
                            BUSTER_TEST(arguments, hidden_count == 2 && data_count == 2 && public_records == 2);
                            BUSTER_TEST(arguments, hidden_types[0] == pointer_to_const_int && hidden_types[1] == pointer_to_const_float &&
                                                   public_types[0] == pointer_to_const_int && public_types[1] == pointer_to_const_float);
                        }
                    }

                    // S_LPROC32 shares S_GPROC32's layout and needs the same
                    // module-local procedure type rewrite (#3032 lesson): a static
                    // function must not keep an unmapped type index in a merged PDB.
                    DebugFunction procedure_functions[] = {
                        {.name = S8("hidden"), .symbol = {.value = 20}, .type = 1, .scope = 0, .code_size = 0x10, .is_internal = true},
                        {.name = S8("api"), .symbol = {.value = 21}, .type = 1, .scope = 1, .code_offset = 0x10, .code_size = 0x10},
                    };
                    DebugScope procedure_scopes[] = {
                        {.parent = DEBUG_ID_INVALID, .kind = DEBUG_SCOPE_FUNCTION},
                        {.parent = DEBUG_ID_INVALID, .kind = DEBUG_SCOPE_FUNCTION},
                    };
                    DwarfFunction procedure_seeds[] = {
                        {.name = S8("hidden"), .code_size = 0x10, .line = 1},
                        {.name = S8("api"), .code_offset = 0x10, .code_size = 0x10, .line = 2},
                    };
                    DebugModel procedure_model = {.types = data_types, .type_count = BUSTER_ARRAY_LENGTH(data_types), .functions = procedure_functions,
                                                  .function_count = BUSTER_ARRAY_LENGTH(procedure_functions), .scopes = procedure_scopes,
                                                  .scope_count = BUSTER_ARRAY_LENGTH(procedure_scopes), .valid = true};
                    CodeviewResult procedure_codeview = codeview_build(arguments->arena, (CodeviewInput){.model = &procedure_model,
                        .file_paths = files, .file_count = BUSTER_ARRAY_LENGTH(files), .functions = procedure_seeds,
                        .function_count = BUSTER_ARRAY_LENGTH(procedure_seeds), .machine = CODEVIEW_MACHINE_X64});
                    BUSTER_TEST(arguments, procedure_codeview.valid);
                    if (procedure_codeview.valid)
                    {
                        PdbModule procedure_modules[] = {merge_modules[0], merge_modules[1]};
                        procedure_modules[0].codeview_symbols = procedure_codeview.symbols;
                        procedure_modules[1].codeview_symbols = procedure_codeview.symbols;
                        PdbInput procedure_input = merge_input;
                        procedure_input.modules = procedure_modules;
                        PdbResult procedure_merged = pdb_build(arguments->arena, procedure_input);
                        BUSTER_TEST(arguments, procedure_merged.valid);
                        if (procedure_merged.valid)
                        {
                            for (u32 module_index = 0; module_index < BUSTER_ARRAY_LENGTH(procedure_modules); module_index += 1)
                            {
                                u32 stream_index = module_index ? PDB_TEST_STREAM_COUNT + module_index - 1 : PDB_TEST_STREAM_MODULE;
                                ByteSlice symbols = pdb_test_stream_bytes(arguments->arena, procedure_merged.bytes, stream_index);
                                u32 expected_type = module_index ? pointer_to_const_float : pointer_to_const_int;
                                u32 local_count = 0;
                                u32 public_count = 0;
                                u64 symbol_offset = 4;
                                while (symbol_offset + 4 <= symbols.length)
                                {
                                    u16 length = 0;
                                    u16 kind = 0;
                                    memcpy(&length, symbols.pointer + symbol_offset, sizeof(length));
                                    memcpy(&kind, symbols.pointer + symbol_offset + 2, sizeof(kind));
                                    if (length < 2 || (u64)length + 2 > symbols.length - symbol_offset)
                                    {
                                        break;
                                    }
                                    if ((kind == PDB_TEST_S_LPROC32 || kind == PDB_TEST_S_GPROC32) && symbol_offset + 32 <= symbols.length)
                                    {
                                        u32 mapped_type = pdb_read_u32(symbols, symbol_offset + 28);
                                        local_count += kind == PDB_TEST_S_LPROC32 && mapped_type == expected_type;
                                        public_count += kind == PDB_TEST_S_GPROC32 && mapped_type == expected_type;
                                    }
                                    symbol_offset += (UINT64_C(2) + length + UINT64_C(3)) & ~UINT64_C(3);
                                }
                                BUSTER_TEST(arguments, local_count == 1 && public_count == 1);
                            }
                        }
                    }
                }
#if !BUSTER_IOS
                if (merged.valid)
                {
                    String8 merged_path = buster_test_temporary_path(arguments->arena, S8("buster-pdb-merged"), S8(".pdb"));
                    BUSTER_TEST(arguments, file_write(merged_path, merged.bytes));
                }
#endif

                // Invalid input must be rejected rather than producing a broken file.
                PdbInput invalid = input;
                invalid.section_count = 0;
                BUSTER_TEST(arguments, !pdb_build(arguments->arena, invalid).valid);
            }
        }
    }

    return result;
}
#endif
