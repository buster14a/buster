#include <buster/compiler/codeview/codeview.h>
#include <buster/string.h>

#include <string.h>

enum
{
    CV_SIGNATURE_C13 = 4,
};

enum
{
    DEBUG_S_SYMBOLS = 0xf1,
    DEBUG_S_LINES = 0xf2,
    DEBUG_S_STRINGTABLE = 0xf3,
    DEBUG_S_FILECHKSMS = 0xf4,
};

enum
{
    S_END = 0x0006,
    S_OBJNAME = 0x1101,
    S_GPROC32 = 0x1110,
    S_COMPILE3 = 0x113c,
};

#define CODEVIEW_LINE_STATEMENT 0x80000000u
#define CODEVIEW_LINE_NUMBER_MASK 0x00ffffffu

typedef struct CodeviewBuffer CodeviewBuffer;
struct CodeviewBuffer
{
    u8* bytes;
    u64 count;
    u64 capacity;
    bool overflow;
    u8 reserved[7];
};

BUSTER_GLOBAL_LOCAL void codeview_emit_bytes(CodeviewBuffer* buffer, void const* source, u64 size)
{
    if (buffer->count + size > buffer->capacity)
    {
        buffer->overflow = true;
        return;
    }
    memcpy(buffer->bytes + buffer->count, source, size);
    buffer->count += size;
}

BUSTER_GLOBAL_LOCAL void codeview_emit_u8(CodeviewBuffer* buffer, u8 value)
{
    codeview_emit_bytes(buffer, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void codeview_emit_u16(CodeviewBuffer* buffer, u16 value)
{
    codeview_emit_bytes(buffer, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void codeview_emit_u32(CodeviewBuffer* buffer, u32 value)
{
    codeview_emit_bytes(buffer, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void codeview_write_u16_at(CodeviewBuffer* buffer, u64 offset, u16 value)
{
    if (offset + sizeof(value) > buffer->count)
    {
        buffer->overflow = true;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void codeview_write_u32_at(CodeviewBuffer* buffer, u64 offset, u32 value)
{
    if (offset + sizeof(value) > buffer->count)
    {
        buffer->overflow = true;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void codeview_align4(CodeviewBuffer* buffer)
{
    while (buffer->count & 3)
    {
        codeview_emit_u8(buffer, 0);
    }
}

// Opens a subsection and returns the offset of its length field, which
// codeview_subsection_end patches once the payload size is known.
BUSTER_GLOBAL_LOCAL u64 codeview_subsection_begin(CodeviewBuffer* buffer, u32 kind)
{
    codeview_align4(buffer);
    codeview_emit_u32(buffer, kind);
    u64 length_offset = buffer->count;
    codeview_emit_u32(buffer, 0);
    return length_offset;
}

BUSTER_GLOBAL_LOCAL void codeview_subsection_end(CodeviewBuffer* buffer, u64 length_offset)
{
    codeview_write_u32_at(buffer, length_offset, (u32)(buffer->count - (length_offset + 4)));
    codeview_align4(buffer);
}

// Opens a symbol record and returns the offset of its length field; the
// record length excludes the length field itself and includes 4-alignment
// padding, which codeview_record_end appends.
BUSTER_GLOBAL_LOCAL u64 codeview_record_begin(CodeviewBuffer* buffer, u16 record_type)
{
    u64 length_offset = buffer->count;
    codeview_emit_u16(buffer, 0);
    codeview_emit_u16(buffer, record_type);
    return length_offset;
}

BUSTER_GLOBAL_LOCAL void codeview_record_end(CodeviewBuffer* buffer, u64 length_offset)
{
    codeview_align4(buffer);
    codeview_write_u16_at(buffer, length_offset, (u16)(buffer->count - (length_offset + 2)));
}

CodeviewResult codeview_build(Arena* arena, CodeviewInput input)
{
    CodeviewResult result = {0};
    if (!arena || !input.file_count || !input.file_paths || !input.function_count || !input.functions || (input.line_count && !input.lines))
    {
        return result;
    }
    for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
    {
        if (input.functions[function_index].file >= input.file_count)
        {
            return result;
        }
    }
    u32 previous_offset = 0;
    for (u32 line_index = 0; line_index < input.line_count; line_index += 1)
    {
        DwarfLineEntry* entry = input.lines + line_index;
        if (entry->file >= input.file_count || entry->code_offset < previous_offset)
        {
            return result;
        }
        previous_offset = entry->code_offset;
    }
    u64 path_bytes = 0;
    for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
    {
        path_bytes += input.file_paths[file_index].length + 1;
    }
    u64 name_bytes = 0;
    for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
    {
        name_bytes += input.functions[function_index].name.length + 1;
    }
    u64 symbol_capacity = 256 + input.producer.length * 2 + name_bytes + (u64)input.function_count * 96 + (u64)input.line_count * 24 +
                          (u64)input.file_count * 24 + path_bytes;
    CodeviewBuffer symbols = {
        .bytes = arena_allocate(arena, u8, symbol_capacity),
        .capacity = symbol_capacity,
    };
    result.relocations = arena_allocate(arena, CodeviewRelocation, (u64)input.function_count * 4);
    codeview_emit_u32(&symbols, CV_SIGNATURE_C13);

    // Translation-unit records: object name and compiler description.
    u64 unit_symbols = codeview_subsection_begin(&symbols, DEBUG_S_SYMBOLS);
    u64 objname = codeview_record_begin(&symbols, S_OBJNAME);
    codeview_emit_u32(&symbols, 0);
    codeview_emit_bytes(&symbols, input.file_paths[0].pointer, input.file_paths[0].length);
    codeview_emit_u8(&symbols, 0);
    codeview_record_end(&symbols, objname);
    u64 compile3 = codeview_record_begin(&symbols, S_COMPILE3);
    codeview_emit_u32(&symbols, 0);
    codeview_emit_u16(&symbols, input.machine);
    for (u32 version_index = 0; version_index < 8; version_index += 1)
    {
        codeview_emit_u16(&symbols, 0);
    }
    codeview_emit_bytes(&symbols, input.producer.pointer, input.producer.length);
    codeview_emit_u8(&symbols, 0);
    codeview_record_end(&symbols, compile3);
    codeview_subsection_end(&symbols, unit_symbols);

    // One symbols subsection and one lines subsection per function.
    u32 line_cursor = 0;
    for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
    {
        DwarfFunction* function = input.functions + function_index;
        u64 function_symbols = codeview_subsection_begin(&symbols, DEBUG_S_SYMBOLS);
        u64 procedure = codeview_record_begin(&symbols, S_GPROC32);
        codeview_emit_u32(&symbols, 0);
        u64 end_pointer_offset = symbols.count;
        codeview_emit_u32(&symbols, 0);
        codeview_emit_u32(&symbols, 0);
        codeview_emit_u32(&symbols, function->code_size);
        codeview_emit_u32(&symbols, 0);
        codeview_emit_u32(&symbols, function->code_size);
        codeview_emit_u32(&symbols, 0);
        result.relocations[result.relocation_count++] = (CodeviewRelocation){
            .offset = symbols.count,
            .function = function_index,
            .kind = CODEVIEW_RELOCATION_SECREL32,
        };
        codeview_emit_u32(&symbols, 0);
        result.relocations[result.relocation_count++] = (CodeviewRelocation){
            .offset = symbols.count,
            .function = function_index,
            .kind = CODEVIEW_RELOCATION_SECTION16,
        };
        codeview_emit_u16(&symbols, 0);
        codeview_emit_u8(&symbols, 0);
        codeview_emit_bytes(&symbols, function->name.pointer, function->name.length);
        codeview_emit_u8(&symbols, 0);
        codeview_record_end(&symbols, procedure);
        u64 end_record = symbols.count;
        u64 end_marker = codeview_record_begin(&symbols, S_END);
        codeview_record_end(&symbols, end_marker);
        if (end_record <= UINT32_MAX)
        {
            codeview_write_u32_at(&symbols, end_pointer_offset, (u32)end_record);
        }
        codeview_subsection_end(&symbols, function_symbols);

        u64 function_lines = codeview_subsection_begin(&symbols, DEBUG_S_LINES);
        result.relocations[result.relocation_count++] = (CodeviewRelocation){
            .offset = symbols.count,
            .function = function_index,
            .kind = CODEVIEW_RELOCATION_SECREL32,
        };
        codeview_emit_u32(&symbols, 0);
        result.relocations[result.relocation_count++] = (CodeviewRelocation){
            .offset = symbols.count,
            .function = function_index,
            .kind = CODEVIEW_RELOCATION_SECTION16,
        };
        codeview_emit_u16(&symbols, 0);
        codeview_emit_u16(&symbols, 0);
        codeview_emit_u32(&symbols, function->code_size);
        u32 function_end = function->code_offset + function->code_size;
        while (line_cursor < input.line_count && input.lines[line_cursor].code_offset < function->code_offset)
        {
            line_cursor += 1;
        }
        while (line_cursor < input.line_count && input.lines[line_cursor].code_offset < function_end)
        {
            u32 run_file = input.lines[line_cursor].file;
            u64 block_start = symbols.count;
            codeview_emit_u32(&symbols, run_file * 8);
            u64 count_offset = symbols.count;
            codeview_emit_u32(&symbols, 0);
            codeview_emit_u32(&symbols, 0);
            u32 run_lines = 0;
            while (line_cursor < input.line_count && input.lines[line_cursor].code_offset < function_end && input.lines[line_cursor].file == run_file)
            {
                DwarfLineEntry* entry = input.lines + line_cursor;
                line_cursor += 1;
                if (!entry->line)
                {
                    continue;
                }
                codeview_emit_u32(&symbols, entry->code_offset - function->code_offset);
                codeview_emit_u32(&symbols, (entry->line & CODEVIEW_LINE_NUMBER_MASK) | CODEVIEW_LINE_STATEMENT);
                run_lines += 1;
            }
            codeview_write_u32_at(&symbols, count_offset, run_lines);
            codeview_write_u32_at(&symbols, count_offset + 4, (u32)(symbols.count - block_start));
        }
        codeview_subsection_end(&symbols, function_lines);
    }

    // File checksum table (checksum kind "none") and the string table it
    // references; line blocks index checksums by byte offset (8 per file).
    u64 checksums = codeview_subsection_begin(&symbols, DEBUG_S_FILECHKSMS);
    for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
    {
        codeview_emit_u32(&symbols, 1 + (u32)file_index);
        codeview_emit_u8(&symbols, 0);
        codeview_emit_u8(&symbols, 0);
        codeview_emit_u16(&symbols, 0);
    }
    codeview_subsection_end(&symbols, checksums);
    u64 string_table = codeview_subsection_begin(&symbols, DEBUG_S_STRINGTABLE);
    u64 string_base = symbols.count;
    codeview_emit_u8(&symbols, 0);
    u32* string_offsets = arena_allocate(arena, u32, input.file_count);
    for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
    {
        string_offsets[file_index] = (u32)(symbols.count - string_base);
        codeview_emit_bytes(&symbols, input.file_paths[file_index].pointer, input.file_paths[file_index].length);
        codeview_emit_u8(&symbols, 0);
    }
    codeview_subsection_end(&symbols, string_table);
    // Patch the checksum entries now that string offsets are known.
    for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
    {
        codeview_write_u32_at(&symbols, checksums + 4 + (u64)file_index * 8, string_offsets[file_index]);
    }

    CodeviewBuffer types = {
        .bytes = arena_allocate(arena, u8, 4),
        .capacity = 4,
    };
    codeview_emit_u32(&types, CV_SIGNATURE_C13);
    if (symbols.overflow || types.overflow || symbols.count > UINT32_MAX)
    {
        return result;
    }
    result.symbols = (ByteSlice){
        .pointer = symbols.bytes,
        .length = symbols.count,
    };
    result.types = (ByteSlice){
        .pointer = types.bytes,
        .length = types.count,
    };
    result.valid = true;
    return result;
}

#if BUSTER_INCLUDE_TESTS
UnitTestResult codeview_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 files[] = {
        S8_INITIALIZER("main.c"),
        S8_INITIALIZER("helper.h"),
    };
    DwarfFunction functions[] = {
        {
            .name = S8_INITIALIZER("main"),
            .code_offset = 0,
            .code_size = 32,
            .file = 0,
            .line = 3,
        },
        {
            .name = S8_INITIALIZER("helper"),
            .code_offset = 32,
            .code_size = 48,
            .file = 1,
            .line = 10,
        },
    };
    DwarfLineEntry lines[] = {
        {.code_offset = 0, .file = 0, .line = 3, .column = 1},
        {.code_offset = 8, .file = 0, .line = 4, .column = 5},
        {.code_offset = 32, .file = 1, .line = 10, .column = 1},
        {.code_offset = 48, .file = 1, .line = 12, .column = 9},
    };
    CodeviewInput input = {
        .producer = S8("buster"),
        .file_paths = files,
        .functions = functions,
        .lines = lines,
        .file_count = BUSTER_ARRAY_LENGTH(files),
        .function_count = BUSTER_ARRAY_LENGTH(functions),
        .line_count = BUSTER_ARRAY_LENGTH(lines),
        .machine = CODEVIEW_MACHINE_X64,
    };
    CodeviewResult built = codeview_build(arguments->arena, input);
    BUSTER_TEST(arguments, built.valid);
    BUSTER_TEST(arguments, built.types.length == 4);
    BUSTER_TEST(arguments, built.symbols.length > 16);
    BUSTER_TEST(arguments, built.relocation_count == 4 * BUSTER_ARRAY_LENGTH(functions));
    if (!built.valid)
    {
        return result;
    }
    u32 signature;
    memcpy(&signature, built.symbols.pointer, sizeof(signature));
    BUSTER_TEST(arguments, signature == CV_SIGNATURE_C13);
    for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
    {
        CodeviewRelocation relocation = built.relocations[relocation_index];
        u64 width = relocation.kind == CODEVIEW_RELOCATION_SECREL32 ? 4 : 2;
        BUSTER_TEST(arguments, relocation.offset + width <= built.symbols.length);
        BUSTER_TEST(arguments, relocation.function < BUSTER_ARRAY_LENGTH(functions));
    }
    // Walk the subsections and check the payload structure.
    u32 symbol_subsections = 0;
    u32 line_subsections = 0;
    u32 checksum_subsections = 0;
    u32 string_subsections = 0;
    u32 checked_lines = 0;
    u64 offset = 4;
    while (offset + 8 <= built.symbols.length)
    {
        u32 kind;
        u32 length;
        memcpy(&kind, built.symbols.pointer + offset, sizeof(kind));
        memcpy(&length, built.symbols.pointer + offset + 4, sizeof(length));
        u64 payload = offset + 8;
        BUSTER_TEST(arguments, payload + length <= built.symbols.length);
        if (payload + length > built.symbols.length)
        {
            return result;
        }
        symbol_subsections += kind == DEBUG_S_SYMBOLS;
        checksum_subsections += kind == DEBUG_S_FILECHKSMS;
        string_subsections += kind == DEBUG_S_STRINGTABLE;
        if (kind == DEBUG_S_LINES && length >= 12 + 12 + 8)
        {
            line_subsections += 1;
            u32 contribution_size;
            u32 file_id;
            u32 line_count;
            u32 first_line;
            memcpy(&contribution_size, built.symbols.pointer + payload + 8, sizeof(contribution_size));
            memcpy(&file_id, built.symbols.pointer + payload + 12, sizeof(file_id));
            memcpy(&line_count, built.symbols.pointer + payload + 16, sizeof(line_count));
            memcpy(&first_line, built.symbols.pointer + payload + 24 + 4, sizeof(first_line));
            DwarfFunction* function = functions + (line_subsections - 1);
            BUSTER_TEST(arguments, contribution_size == function->code_size);
            BUSTER_TEST(arguments, file_id == function->file * 8);
            BUSTER_TEST(arguments, line_count >= 1);
            BUSTER_TEST(arguments, (first_line & CODEVIEW_LINE_NUMBER_MASK) == function->line);
            BUSTER_TEST(arguments, first_line & CODEVIEW_LINE_STATEMENT);
            checked_lines += 1;
        }
        if (kind == DEBUG_S_STRINGTABLE)
        {
            BUSTER_TEST(arguments, length >= 1 + files[0].length + 1 + files[1].length + 1);
            BUSTER_TEST(arguments, built.symbols.pointer[payload] == 0);
            BUSTER_TEST(arguments, memcmp(built.symbols.pointer + payload + 1, files[0].pointer, files[0].length) == 0);
        }
        offset = payload + ((length + 3) & ~3u);
    }
    BUSTER_TEST(arguments, symbol_subsections == 1 + BUSTER_ARRAY_LENGTH(functions));
    BUSTER_TEST(arguments, line_subsections == BUSTER_ARRAY_LENGTH(functions));
    BUSTER_TEST(arguments, checksum_subsections == 1 && string_subsections == 1);
    BUSTER_TEST(arguments, checked_lines == BUSTER_ARRAY_LENGTH(functions));
    // Invalid input: an out-of-range file index must be rejected.
    DwarfLineEntry invalid_line = {.code_offset = 0, .file = 9, .line = 1, .column = 1};
    CodeviewInput invalid = input;
    invalid.lines = &invalid_line;
    invalid.line_count = 1;
    BUSTER_TEST(arguments, !codeview_build(arguments->arena, invalid).valid);
    return result;
}
#endif
