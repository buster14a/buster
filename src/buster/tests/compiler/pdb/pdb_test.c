#include <buster/tests/compiler/pdb/pdb_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/pdb/pdb_internal.h>

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
    BUSTER_TEST(arguments, built.valid);
    BUSTER_TEST(arguments, memcmp(blob, original, sizeof(blob)) == 0);
    if (built.valid)
    {
        ByteSlice module = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_MODULE);
        // Symbol signature, checksum header/payload, line header/payload, then global-ref count.
        BUSTER_TEST(arguments, module.length == 4 + 108 + 100 + 4);
        if (module.length >= 212)
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
    // Reuse the same record shapes in a second module: its remap must use its
    // own names offsets without moving any checksum or line-record boundary.
    PdbModule modules[2] = {
        {.name = S8("first.obj"), .codeview_symbols = input.codeview_symbols, .code_section = 1, .code_size = 16},
        {.name = S8("second.obj"), .codeview_symbols = input.codeview_symbols, .code_section = 1, .code_offset = 16, .code_size = 16},
    };
    input.modules = modules;
    input.module_count = BUSTER_ARRAY_LENGTH(modules);
    section.virtual_size = 32; // Both modules' sixteen-byte ranges are in the image.
    built = pdb_build(arguments->arena, input);
    BUSTER_TEST(arguments, built.valid);
    if (built.valid)
    {
        ByteSlice second = pdb_test_stream_bytes(arguments->arena, built.bytes, PDB_TEST_STREAM_COUNT);
        BUSTER_TEST(arguments, second.length >= 212);
        if (second.length >= 212)
        {
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(record_offsets); index += 1)
            {
                u32 offset = record_offsets[index];
                u32 stride = (6 + (u32)digest_sizes[index] + 3) & ~(u32)3;
                BUSTER_TEST(arguments, pdb_read_u32(second, 12 + offset) == mapped_offsets[index] + 29);
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
    BUSTER_TEST(arguments, symbol_size <= stream.length);
    u32 stack[16] = {0};
    u32 ends[16] = {0};
    u32 depth = 0;
    u32 procedures = 0;
    u32 blocks = 0;
    u32 inlines = 0;
    u64 cursor = 4;
    while (cursor + 4 <= symbol_size && symbol_size <= stream.length)
    {
        u16 length = 0;
        u16 kind = 0;
        memcpy(&length, stream.pointer + cursor, 2);
        memcpy(&kind, stream.pointer + cursor + 2, 2);
        BUSTER_TEST(arguments, length >= 2 && (u64)length + 2 <= symbol_size - cursor);
        if (length < 2 || (u64)length + 2 > symbol_size - cursor)
        {
            break;
        }
        if (kind == 0x1110 || kind == 0x1103 || kind == 0x114d)
        {
            BUSTER_TEST(arguments, depth < BUSTER_ARRAY_LENGTH(stack) && length >= 14);
            if (depth == BUSTER_ARRAY_LENGTH(stack) || length < 14)
            {
                break;
            }
            u32 parent = pdb_read_u32(stream, cursor + 4);
            u32 end = pdb_read_u32(stream, cursor + 8);
            BUSTER_TEST(arguments, parent == (depth ? stack[depth - 1] : 0));
            BUSTER_TEST(arguments, end > cursor && (u64)end + 4 <= symbol_size);
            if (end > cursor && (u64)end + 4 <= symbol_size)
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
            BUSTER_TEST(arguments, depth && ends[depth - 1] == cursor);
            if (!depth)
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
            {.kind = DEBUG_TYPE_BASE, .name = index ? S8("f32") : S8("int"), .size = 4, .is_signed = true},
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
    BUSTER_TEST(arguments, built.valid);
    if (built.valid)
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
    BUSTER_TEST(arguments, built.valid);
    if (built.valid)
    {
        BUSTER_TEST(arguments, pdb_test_check_msf(arena, built.bytes));
        ByteSlice dbi = pdb_test_stream_bytes(arena, built.bytes, 3);
        BUSTER_TEST(arguments, dbi.length >= 64);
        if (dbi.length >= 64)
        {
            u64 contribution = 64 + pdb_test_load32(dbi.pointer + 24) + 4;
            BUSTER_TEST(arguments, contribution + 56 <= dbi.length);
            if (contribution + 56 <= dbi.length)
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
        PdbBuffer stream = {.bytes = payload, .count = size, .capacity = size};
        PdbResult built = pdb_msf_build(arena, &stream, 1);
        BUSTER_TEST(arguments, built.valid);
        if (built.valid)
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
    PdbBuffer invalid = {.bytes = &byte, .count = UINT64_MAX};
    BUSTER_TEST(arguments, !pdb_msf_build(arguments->arena, &invalid, 1).valid);
    invalid.count = UINT32_MAX;
    BUSTER_TEST(arguments, !pdb_msf_build(arguments->arena, &invalid, 1).valid);
    invalid.count = (UINT64_C(1048575) - 100) * 4096;
    BUSTER_TEST(arguments, !pdb_msf_build(arguments->arena, &invalid, 1).valid);
    PdbBuffer directory_overflow[] = {
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
    BUSTER_TEST(arguments, built.valid && repeated.valid);
    if (built.valid && repeated.valid)
    {
        BUSTER_TEST(arguments, built.bytes.length == repeated.bytes.length && memcmp(built.bytes.pointer, repeated.bytes.pointer, built.bytes.length) == 0);
        ByteSlice dbi = pdb_test_stream_bytes(arguments->arena, built.bytes, 3);
        BUSTER_TEST(arguments, dbi.length >= 64);
        if (dbi.length >= 64)
        {
            u64 contributions = 64 + pdb_test_load32(dbi.pointer + 24) + 4;
            BUSTER_TEST(arguments, pdb_test_load32(dbi.pointer + 28) == 4 + 4 * 28);
            BUSTER_TEST(arguments, contributions + 4 * 28 <= dbi.length);
            u64 module = 64;
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modules); index += 1)
            {
                BUSTER_TEST(arguments, module + 64 <= dbi.length);
                if (module + 64 <= dbi.length && contributions + 4 * 28 <= dbi.length)
                {
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
    BUSTER_TEST(arguments, built.valid);
    if (built.valid)
    {
        ByteSlice dbi = pdb_test_stream_bytes(arena, built.bytes, PDB_TEST_STREAM_DBI);
        BUSTER_TEST(arguments, dbi.length >= PDB_TEST_DBI_HEADER_SIZE);
        if (dbi.length >= PDB_TEST_DBI_HEADER_SIZE)
        {
            u64 source_start = PDB_TEST_DBI_HEADER_SIZE + (u64)pdb_read_u32(dbi, 24) + pdb_read_u32(dbi, 28) + pdb_read_u32(dbi, 32);
            u64 source_size = pdb_read_u32(dbi, 36);
            bool valid = source_start <= dbi.length && source_size <= dbi.length - source_start &&
                         source_size >= 12 + (u64)total_files * 4;
            BUSTER_TEST(arguments, valid);
            if (valid)
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
            for (u32 file = 0; file < module_files && valid; file += 1)
            {
                u32 expected = 1 + (module_index * module_files + file) * 7;
                valid = pdb_read_u32(module, 12 + (u64)file * checksum_stride) == expected;
            }
            BUSTER_TEST(arguments, valid);
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

UnitTestResult pdb_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = pdb_test_checksum_records(arguments);
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
    BUSTER_TEST(arguments, codeview.valid);
    if (codeview.valid)
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
        BUSTER_TEST(arguments, built.valid);
        if (!built.valid)
        {
            return result;
        }
        UnitTestResult scope_links = pdb_test_scope_links(arguments, built, codeview);
        result.test_count += scope_links.test_count;
        result.succeeded_test_count += scope_links.succeeded_test_count;
        BUSTER_TEST(arguments, built.bytes.length % PDB_TEST_BLOCK_SIZE == 0);
        BUSTER_TEST(arguments, memcmp(built.bytes.pointer, "Microsoft C/C++ MSF 7.00\r\n\x1a" "DS", 30) == 0);
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
        BUSTER_TEST(arguments, stream_count == PDB_TEST_STREAM_COUNT);
        if (stream_count != PDB_TEST_STREAM_COUNT)
        {
            return result;
        }
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
        memcpy(&module_stream, built.bytes.pointer + dbi_base + PDB_TEST_DBI_HEADER_SIZE + 4 + PDB_TEST_SECTION_CONTRIBUTION_SIZE + 2, sizeof(module_stream));
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
                found_frame |= record_kind == PDB_TEST_S_DEFRANGE_FRAMEPOINTER_REL;
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

    return result;
}
#endif
