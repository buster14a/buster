#include <buster/lib/compiler/dwarf/dwarf.h>
#include <buster/lib/compiler/debug/debug.h>
#include <buster/lib/hash.h>
#include <buster/lib/string.h>

#include <string.h>

enum
{
    DWARF_VERSION = 4,
    DWARF_ADDRESS_SIZE = 8,
    DWARF_LINE_BASE = -5,
    DWARF_LINE_RANGE = 14,
    DWARF_OPCODE_BASE = 13,
};

enum
{
    DW_TAG_COMPILE_UNIT = 0x11,
    DW_TAG_ARRAY_TYPE = 0x01,
    DW_TAG_MEMBER = 0x0d,
    DW_TAG_POINTER_TYPE = 0x0f,
    DW_TAG_STRUCTURE_TYPE = 0x13,
    DW_TAG_SUBROUTINE_TYPE = 0x15,
    DW_TAG_TYPEDEF = 0x16,
    DW_TAG_UNION_TYPE = 0x17,
    DW_TAG_SUBRANGE_TYPE = 0x21,
    DW_TAG_BASE_TYPE = 0x24,
    DW_TAG_CONST_TYPE = 0x26,
    DW_TAG_ENUMERATOR = 0x28,
    DW_TAG_SUBPROGRAM = 0x2e,
    DW_TAG_FORMAL_PARAMETER = 0x05,
    DW_TAG_VARIABLE = 0x34,
    DW_TAG_VOLATILE_TYPE = 0x35,
    DW_TAG_ENUMERATION_TYPE = 0x04,
    DW_TAG_LEXICAL_BLOCK = 0x0b,
    DW_TAG_INLINED_SUBROUTINE = 0x1d,
    DW_TAG_UNSPECIFIED_TYPE = 0x3b,
};

enum
{
    DW_AT_NAME = 0x03,
    DW_AT_STMT_LIST = 0x10,
    DW_AT_LOW_PC = 0x11,
    DW_AT_HIGH_PC = 0x12,
    DW_AT_LANGUAGE = 0x13,
    DW_AT_COMP_DIR = 0x1b,
    DW_AT_PRODUCER = 0x25,
    DW_AT_DECL_FILE = 0x3a,
    DW_AT_DECL_LINE = 0x3b,
    DW_AT_TYPE = 0x49,
    DW_AT_BYTE_SIZE = 0x0b,
    DW_AT_ENCODING = 0x3e,
    DW_AT_UPPER_BOUND = 0x2f,
    DW_AT_DATA_MEMBER_LOCATION = 0x38,
    DW_AT_LOCATION = 0x02,
    DW_AT_FRAME_BASE = 0x40,
    DW_AT_RANGES = 0x55,
    DW_AT_ABSTRACT_ORIGIN = 0x31,
    DW_AT_CALL_FILE = 0x58,
    DW_AT_CALL_LINE = 0x59,
    DW_AT_CONST_VALUE = 0x1c,
    DW_AT_EXTERNAL = 0x3f,
};

enum
{
    DW_FORM_ADDR = 0x01,
    DW_FORM_DATA2 = 0x05,
    DW_FORM_DATA8 = 0x07,
    DW_FORM_STRP = 0x0e,
    DW_FORM_UDATA = 0x0f,
    DW_FORM_SEC_OFFSET = 0x17,
    DW_FORM_DATA1 = 0x0b,
    DW_FORM_REF4 = 0x13,
    DW_FORM_EXPRLOC = 0x18,
};

enum
{
    DW_OP_ADDR = 0x03,
    DW_OP_CONSTU = 0x10,
    DW_OP_REGX = 0x90,
    DW_OP_FBREG = 0x91,
    DW_OP_CALL_FRAME_CFA = 0x9c,
    DW_OP_PIECE = 0x93,
    DW_OP_STACK_VALUE = 0x9f,
};

enum
{
    DW_LNS_COPY = 1,
    DW_LNS_ADVANCE_PC = 2,
    DW_LNS_ADVANCE_LINE = 3,
    DW_LNS_SET_FILE = 4,
    DW_LNS_SET_COLUMN = 5,
};

enum
{
    DW_LNE_END_SEQUENCE = 1,
    DW_LNE_SET_ADDRESS = 2,
};

typedef struct DwarfBuffer DwarfBuffer;
struct DwarfBuffer
{
    u8* bytes;
    u64 count;
    u64 capacity;
    bool overflow;
    u8 reserved[7];
};

BUSTER_GLOBAL_LOCAL void dwarf_emit_bytes(DwarfBuffer* buffer, void const* source, u64 size)
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

BUSTER_GLOBAL_LOCAL void dwarf_emit_u8(DwarfBuffer* buffer, u8 value)
{
    dwarf_emit_bytes(buffer, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void dwarf_emit_u16(DwarfBuffer* buffer, u16 value)
{
    dwarf_emit_bytes(buffer, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void dwarf_emit_u32(DwarfBuffer* buffer, u32 value)
{
    dwarf_emit_bytes(buffer, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void dwarf_emit_u64(DwarfBuffer* buffer, u64 value)
{
    dwarf_emit_bytes(buffer, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void dwarf_emit_uleb128(DwarfBuffer* buffer, u64 value)
{
    for (;;)
    {
        u8 byte = value & 0x7f;
        value >>= 7;
        if (value)
        {
            byte |= 0x80;
        }
        dwarf_emit_u8(buffer, byte);
        if (!value)
        {
            break;
        }
    }
}

BUSTER_GLOBAL_LOCAL void dwarf_emit_sleb128(DwarfBuffer* buffer, s64 value)
{
    for (;;)
    {
        u8 byte = (u8)(value & 0x7f);
        value >>= 7;
        bool done = (value == 0 && !(byte & 0x40)) || (value == -1 && (byte & 0x40));
        if (!done)
        {
            byte |= 0x80;
        }
        dwarf_emit_u8(buffer, byte);
        if (done)
        {
            break;
        }
    }
}

BUSTER_GLOBAL_LOCAL void dwarf_write_u32_at(DwarfBuffer* buffer, u64 offset, u32 value)
{
    if (offset + sizeof(value) > buffer->count)
    {
        buffer->overflow = true;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

typedef struct DwarfStringTable DwarfStringTable;
struct DwarfStringTable
{
    DwarfBuffer* buffer;
    String8* strings;
    u32* offsets;
    u32* slots;
    u32 count;
    u32 capacity;
    u32 slot_mask;
    u32 reserved;
};

BUSTER_GLOBAL_LOCAL DwarfStringTable dwarf_string_table_make(Arena* arena, DwarfBuffer* buffer, u32 capacity)
{
    u32 slot_count = 16;
    while (slot_count < capacity * 2)
    {
        slot_count <<= 1;
    }
    DwarfStringTable result = {
        .buffer = buffer,
        .strings = arena_allocate(arena, String8, capacity),
        .offsets = arena_allocate(arena, u32, capacity),
        .slots = arena_allocate(arena, u32, slot_count),
        .capacity = capacity,
        .slot_mask = slot_count - 1,
    };
    for (u32 slot_index = 0; slot_index < slot_count; slot_index += 1)
    {
        result.slots[slot_index] = UINT32_MAX;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 dwarf_string_intern(DwarfStringTable* table, String8 string)
{
    u64 hash = buster_hash_64(string.pointer ? (u8*)string.pointer : (u8*)"", string.length);
    u32 slot_index = (u32)hash & table->slot_mask;
    for (;;)
    {
        u32 entry = table->slots[slot_index];
        if (entry == UINT32_MAX)
        {
            break;
        }
        if (string_equal(table->strings[entry], string))
        {
            return table->offsets[entry];
        }
        slot_index = (slot_index + 1) & table->slot_mask;
    }
    if (table->count >= table->capacity || table->buffer->count > UINT32_MAX)
    {
        table->buffer->overflow = true;
        return 0;
    }
    u32 offset = (u32)table->buffer->count;
    dwarf_emit_bytes(table->buffer, string.pointer, string.length);
    dwarf_emit_u8(table->buffer, 0);
    table->strings[table->count] = string;
    table->offsets[table->count] = offset;
    table->slots[slot_index] = table->count;
    table->count += 1;
    return offset;
}

BUSTER_GLOBAL_LOCAL void dwarf_abbrev_pair(DwarfBuffer* buffer, u32 attribute, u32 form)
{
    dwarf_emit_uleb128(buffer, attribute);
    dwarf_emit_uleb128(buffer, form);
}

DwarfResult dwarf_build_legacy(Arena* arena, DwarfInput input)
{
    DwarfResult result = {0};
    if (!arena || !input.file_count || !input.file_paths || (input.function_count && !input.functions) || (input.line_count && !input.lines))
    {
        return result;
    }
    for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
    {
        DwarfFunction* function = input.functions + function_index;
        if (function->file >= input.file_count || function->code_offset > input.code_size || function->code_size > input.code_size - function->code_offset)
        {
            return result;
        }
    }
    u32 previous_offset = 0;
    for (u32 line_index = 0; line_index < input.line_count; line_index += 1)
    {
        DwarfLineEntry* entry = input.lines + line_index;
        if (entry->file >= input.file_count || entry->code_offset > input.code_size || entry->code_offset < previous_offset)
        {
            return result;
        }
        previous_offset = entry->code_offset;
    }
    u64 string_bytes = input.producer.length + input.comp_dir.length + 2;
    u64 file_path_bytes = 0;
    for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
    {
        string_bytes += input.file_paths[file_index].length + 1;
        file_path_bytes += input.file_paths[file_index].length + 1;
    }
    for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
    {
        string_bytes += input.functions[function_index].name.length + 1;
    }
    DwarfBuffer str = {
        .bytes = arena_allocate(arena, u8, string_bytes),
        .capacity = string_bytes,
    };
    DwarfBuffer abbrev = {
        .bytes = arena_allocate(arena, u8, 64),
        .capacity = 64,
    };
    u64 info_capacity = 64 + (u64)input.function_count * 40;
    DwarfBuffer info = {
        .bytes = arena_allocate(arena, u8, info_capacity),
        .capacity = info_capacity,
    };
    u64 line_capacity = 64 + file_path_bytes + (u64)input.file_count * 4 + (u64)input.line_count * 32;
    DwarfBuffer line = {
        .bytes = arena_allocate(arena, u8, line_capacity),
        .capacity = line_capacity,
    };
    result.relocations = arena_allocate(arena, DwarfRelocation, (u64)input.function_count * 2 + 7);
    DwarfStringTable strings = dwarf_string_table_make(arena, &str, input.file_count + input.function_count + 2);

    // .debug_abbrev: abbreviation 1 is the compile unit, abbreviation 2 the subprogram.
    dwarf_emit_uleb128(&abbrev, 1);
    dwarf_emit_uleb128(&abbrev, DW_TAG_COMPILE_UNIT);
    dwarf_emit_u8(&abbrev, 1);
    dwarf_abbrev_pair(&abbrev, DW_AT_PRODUCER, DW_FORM_STRP);
    dwarf_abbrev_pair(&abbrev, DW_AT_LANGUAGE, DW_FORM_DATA2);
    dwarf_abbrev_pair(&abbrev, DW_AT_NAME, DW_FORM_STRP);
    dwarf_abbrev_pair(&abbrev, DW_AT_COMP_DIR, DW_FORM_STRP);
    dwarf_abbrev_pair(&abbrev, DW_AT_LOW_PC, DW_FORM_ADDR);
    dwarf_abbrev_pair(&abbrev, DW_AT_HIGH_PC, DW_FORM_DATA8);
    dwarf_abbrev_pair(&abbrev, DW_AT_STMT_LIST, DW_FORM_SEC_OFFSET);
    dwarf_abbrev_pair(&abbrev, 0, 0);
    dwarf_emit_uleb128(&abbrev, 2);
    dwarf_emit_uleb128(&abbrev, DW_TAG_SUBPROGRAM);
    dwarf_emit_u8(&abbrev, 0);
    dwarf_abbrev_pair(&abbrev, DW_AT_NAME, DW_FORM_STRP);
    dwarf_abbrev_pair(&abbrev, DW_AT_DECL_FILE, DW_FORM_UDATA);
    dwarf_abbrev_pair(&abbrev, DW_AT_DECL_LINE, DW_FORM_UDATA);
    dwarf_abbrev_pair(&abbrev, DW_AT_LOW_PC, DW_FORM_ADDR);
    dwarf_abbrev_pair(&abbrev, DW_AT_HIGH_PC, DW_FORM_DATA8);
    dwarf_abbrev_pair(&abbrev, 0, 0);
    dwarf_emit_uleb128(&abbrev, 0);

    // .debug_info: one DWARF 4 compile unit.
    dwarf_emit_u32(&info, 0);
    dwarf_emit_u16(&info, DWARF_VERSION);
    result.relocations[result.relocation_count++] = (DwarfRelocation){
        .offset = info.count,
        .section = DWARF_SECTION_INFO,
        .target = DWARF_SECTION_ABBREV,
    };
    dwarf_emit_u32(&info, 0);
    dwarf_emit_u8(&info, DWARF_ADDRESS_SIZE);
    dwarf_emit_uleb128(&info, 1);
    result.relocations[result.relocation_count++] = (DwarfRelocation){
        .addend = dwarf_string_intern(&strings, input.producer),
        .offset = info.count,
        .section = DWARF_SECTION_INFO,
        .target = DWARF_SECTION_STR,
    };
    dwarf_emit_u32(&info, (u32)result.relocations[result.relocation_count - 1].addend);
    dwarf_emit_u16(&info, input.language);
    result.relocations[result.relocation_count++] = (DwarfRelocation){
        .addend = dwarf_string_intern(&strings, input.file_paths[0]),
        .offset = info.count,
        .section = DWARF_SECTION_INFO,
        .target = DWARF_SECTION_STR,
    };
    dwarf_emit_u32(&info, (u32)result.relocations[result.relocation_count - 1].addend);
    result.relocations[result.relocation_count++] = (DwarfRelocation){
        .addend = dwarf_string_intern(&strings, input.comp_dir),
        .offset = info.count,
        .section = DWARF_SECTION_INFO,
        .target = DWARF_SECTION_STR,
    };
    dwarf_emit_u32(&info, (u32)result.relocations[result.relocation_count - 1].addend);
    result.relocations[result.relocation_count++] = (DwarfRelocation){
        .offset = info.count,
        .section = DWARF_SECTION_INFO,
        .address = true,
    };
    dwarf_emit_u64(&info, 0);
    dwarf_emit_u64(&info, input.code_size);
    result.relocations[result.relocation_count++] = (DwarfRelocation){
        .offset = info.count,
        .section = DWARF_SECTION_INFO,
        .target = DWARF_SECTION_LINE,
    };
    dwarf_emit_u32(&info, 0);
    for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
    {
        DwarfFunction* function = input.functions + function_index;
        dwarf_emit_uleb128(&info, 2);
        result.relocations[result.relocation_count++] = (DwarfRelocation){
            .addend = dwarf_string_intern(&strings, function->name),
            .offset = info.count,
            .section = DWARF_SECTION_INFO,
            .target = DWARF_SECTION_STR,
        };
        dwarf_emit_u32(&info, (u32)result.relocations[result.relocation_count - 1].addend);
        dwarf_emit_uleb128(&info, (u64)function->file + 1);
        dwarf_emit_uleb128(&info, function->line);
        result.relocations[result.relocation_count++] = (DwarfRelocation){
            .addend = function->code_offset,
            .offset = info.count,
            .section = DWARF_SECTION_INFO,
            .address = true,
        };
        dwarf_emit_u64(&info, 0);
        dwarf_emit_u64(&info, function->code_size);
    }
    dwarf_emit_uleb128(&info, 0);
    if (info.count > UINT32_MAX)
    {
        return result;
    }
    dwarf_write_u32_at(&info, 0, (u32)(info.count - 4));

    // .debug_line: one DWARF 4 line program with a single sequence.
    dwarf_emit_u32(&line, 0);
    dwarf_emit_u16(&line, DWARF_VERSION);
    dwarf_emit_u32(&line, 0);
    u64 header_start = line.count;
    dwarf_emit_u8(&line, 1);
    dwarf_emit_u8(&line, 1);
    dwarf_emit_u8(&line, 1);
    dwarf_emit_u8(&line, (u8)(s8)DWARF_LINE_BASE);
    dwarf_emit_u8(&line, DWARF_LINE_RANGE);
    dwarf_emit_u8(&line, DWARF_OPCODE_BASE);
    u8 const standard_opcode_lengths[DWARF_OPCODE_BASE - 1] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    dwarf_emit_bytes(&line, standard_opcode_lengths, sizeof(standard_opcode_lengths));
    dwarf_emit_u8(&line, 0);
    for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
    {
        String8 path = input.file_paths[file_index];
        dwarf_emit_bytes(&line, path.pointer, path.length);
        dwarf_emit_u8(&line, 0);
        dwarf_emit_uleb128(&line, 0);
        dwarf_emit_uleb128(&line, 0);
        dwarf_emit_uleb128(&line, 0);
    }
    dwarf_emit_u8(&line, 0);
    if (line.count > UINT32_MAX)
    {
        return result;
    }
    dwarf_write_u32_at(&line, header_start - 4, (u32)(line.count - header_start));
    dwarf_emit_u8(&line, 0);
    dwarf_emit_uleb128(&line, 1 + DWARF_ADDRESS_SIZE);
    dwarf_emit_u8(&line, DW_LNE_SET_ADDRESS);
    result.relocations[result.relocation_count++] = (DwarfRelocation){
        .offset = line.count,
        .section = DWARF_SECTION_LINE,
        .address = true,
    };
    dwarf_emit_u64(&line, 0);
    u32 current_address = 0;
    u32 current_file = 1;
    u32 current_line = 1;
    u32 current_column = 0;
    u32 emitted_line_count = 0;
    for (u32 line_index = 0; line_index < input.line_count; line_index += 1)
    {
        DwarfLineEntry* entry = input.lines + line_index;
        if (!entry->line)
        {
            continue;
        }
        if (emitted_line_count && entry->code_offset == current_address)
        {
            continue;
        }
        if (emitted_line_count && entry->file + 1 == current_file && entry->line == current_line && entry->column == current_column)
        {
            continue;
        }
        if (entry->file + 1 != current_file)
        {
            current_file = entry->file + 1;
            dwarf_emit_u8(&line, DW_LNS_SET_FILE);
            dwarf_emit_uleb128(&line, current_file);
        }
        if (entry->column != current_column)
        {
            current_column = entry->column;
            dwarf_emit_u8(&line, DW_LNS_SET_COLUMN);
            dwarf_emit_uleb128(&line, current_column);
        }
        u32 address_advance = entry->code_offset - current_address;
        s64 line_delta = (s64)entry->line - (s64)current_line;
        s64 adjusted = line_delta - DWARF_LINE_BASE;
        u64 special = 0;
        bool special_valid = false;
        if (line_delta >= DWARF_LINE_BASE && line_delta < DWARF_LINE_BASE + DWARF_LINE_RANGE)
        {
            special = (u64)adjusted + (u64)DWARF_LINE_RANGE * address_advance + DWARF_OPCODE_BASE;
            special_valid = special <= 255;
        }
        if (special_valid)
        {
            dwarf_emit_u8(&line, (u8)special);
        }
        else
        {
            if (line_delta)
            {
                dwarf_emit_u8(&line, DW_LNS_ADVANCE_LINE);
                dwarf_emit_sleb128(&line, line_delta);
            }
            if (address_advance)
            {
                dwarf_emit_u8(&line, DW_LNS_ADVANCE_PC);
                dwarf_emit_uleb128(&line, address_advance);
            }
            dwarf_emit_u8(&line, DW_LNS_COPY);
        }
        current_address = entry->code_offset;
        current_line = entry->line;
        emitted_line_count += 1;
    }
    if (input.code_size > current_address)
    {
        dwarf_emit_u8(&line, DW_LNS_ADVANCE_PC);
        dwarf_emit_uleb128(&line, input.code_size - current_address);
    }
    dwarf_emit_u8(&line, 0);
    dwarf_emit_uleb128(&line, 1);
    dwarf_emit_u8(&line, DW_LNE_END_SEQUENCE);
    if (line.count > UINT32_MAX)
    {
        return result;
    }
    dwarf_write_u32_at(&line, 0, (u32)(line.count - 4));

    if (str.overflow || abbrev.overflow || info.overflow || line.overflow)
    {
        return result;
    }
    result.sections[DWARF_SECTION_INFO] = (ByteSlice){
        .pointer = info.bytes,
        .length = info.count,
    };
    result.sections[DWARF_SECTION_ABBREV] = (ByteSlice){
        .pointer = abbrev.bytes,
        .length = abbrev.count,
    };
    result.sections[DWARF_SECTION_LINE] = (ByteSlice){
        .pointer = line.bytes,
        .length = line.count,
    };
    result.sections[DWARF_SECTION_STR] = (ByteSlice){
        .pointer = str.bytes,
        .length = str.count,
    };
    result.valid = true;
    return result;
}

typedef struct DwarfModelRefPatch DwarfModelRefPatch;
struct DwarfModelRefPatch
{
    u64 offset;
    u32 target;
    bool function;
    u8 reserved[3];
};

typedef struct DwarfModelWriter DwarfModelWriter;
struct DwarfModelWriter
{
    Arena* arena;
    DwarfInput input;
    DebugModel* model;
    DwarfBuffer str;
    DwarfBuffer abbrev;
    DwarfBuffer info;
    DwarfBuffer loc;
    DwarfBuffer ranges;
    DwarfStringTable strings;
    DwarfRelocation* relocations;
    DwarfModelRefPatch* refs;
    u32 relocation_count;
    u32 relocation_capacity;
    u32 ref_count;
    u32 ref_capacity;
    u32* type_offsets;
    u32* function_offsets;
};

BUSTER_GLOBAL_LOCAL void dwarf_model_relocation(DwarfModelWriter* writer, DwarfRelocation relocation)
{
    if (writer->relocation_count >= writer->relocation_capacity)
    {
        writer->info.overflow = true;
        return;
    }
    writer->relocations[writer->relocation_count++] = relocation;
}

BUSTER_GLOBAL_LOCAL void dwarf_model_reference(DwarfModelWriter* writer, u32 target, bool function)
{
    if (writer->ref_count >= writer->ref_capacity)
    {
        writer->info.overflow = true;
        return;
    }
    writer->refs[writer->ref_count++] = (DwarfModelRefPatch){
        .offset = writer->info.count,
        .target = target,
        .function = function,
    };
    dwarf_emit_u32(&writer->info, 0);
}

BUSTER_GLOBAL_LOCAL u32 dwarf_model_file(DebugModel* model, DebugSourceLocation location, u32 file_count)
{
    if (location.source < file_count)
    {
        return location.source + 1;
    }
    if (model && model->source_count)
    {
        for (u32 index = 0; index < model->source_count && index < file_count; index += 1)
        {
            if (string_equal(model->source_paths[index], location.path))
            {
                return index + 1;
            }
        }
    }
    return 1;
}

BUSTER_GLOBAL_LOCAL u32 dwarf_model_line(DebugSourceLocation location)
{
    return location.line ? location.line : 1;
}

BUSTER_GLOBAL_LOCAL void dwarf_model_string(DwarfModelWriter* writer, String8 string)
{
    u32 offset = dwarf_string_intern(&writer->strings, string);
    dwarf_model_relocation(writer, (DwarfRelocation){
                                          .addend = offset,
                                          .offset = writer->info.count,
                                          .section = DWARF_SECTION_INFO,
                                          .target = DWARF_SECTION_STR,
                                      });
    dwarf_emit_u32(&writer->info, 0);
}

BUSTER_GLOBAL_LOCAL void dwarf_model_type_reference(DwarfModelWriter* writer, DebugTypeId type)
{
    if (type == DEBUG_ID_INVALID || type >= writer->model->type_count)
    {
        // A pointer to an incomplete/void frontend type still has a valid
        // DWARF type edge.  Keep the graph closed by referring to the model's
        // explicit void type instead of leaving a dangling ref4 of zero.
        type = DEBUG_ID_INVALID;
        for (u32 index = 0; index < writer->model->type_count; index += 1)
        {
            if (writer->model->types[index].kind == DEBUG_TYPE_VOID)
            {
                type = index;
                break;
            }
        }
    }
    if (type == DEBUG_ID_INVALID)
    {
        dwarf_emit_u32(&writer->info, 0);
    }
    else
    {
        dwarf_model_reference(writer, type, false);
    }
}

BUSTER_GLOBAL_LOCAL void dwarf_model_function_reference(DwarfModelWriter* writer, u32 function_index)
{
    if (function_index == UINT32_MAX)
    {
        dwarf_emit_u32(&writer->info, 0);
    }
    else
    {
        dwarf_model_reference(writer, function_index, true);
    }
}

BUSTER_GLOBAL_LOCAL u32 dwarf_model_emit_ranges(DwarfModelWriter* writer, u32 start, u32 end)
{
    u32 offset = (u32)writer->ranges.count;
    if (end <= start)
    {
        end = start + 1;
    }
    dwarf_model_relocation(writer, (DwarfRelocation){
                                          .addend = start,
                                          .offset = writer->ranges.count,
                                          .section = DWARF_SECTION_RANGES,
                                          .address = true,
                                      });
    dwarf_emit_u64(&writer->ranges, 0);
    dwarf_model_relocation(writer, (DwarfRelocation){
                                          .addend = end,
                                          .offset = writer->ranges.count,
                                          .section = DWARF_SECTION_RANGES,
                                          .address = true,
                                      });
    dwarf_emit_u64(&writer->ranges, 0);
    dwarf_emit_u64(&writer->ranges, 0);
    dwarf_emit_u64(&writer->ranges, 0);
    return offset;
}

BUSTER_GLOBAL_LOCAL u32 dwarf_model_emit_location_expression(DwarfModelWriter* writer, DebugLocation location)
{
    u32 expression_start = (u32)writer->loc.count;
    if (location.kind == DEBUG_LOCATION_UNAVAILABLE)
    {
        return expression_start;
    }
    if (location.kind == DEBUG_LOCATION_PIECEWISE)
    {
        for (u32 piece_index = 0; piece_index < location.piece_count; piece_index += 1)
        {
            DebugLocationPiece* piece = location.pieces + piece_index;
            if (piece->kind == DEBUG_LOCATION_REGISTER)
            {
                dwarf_emit_u8(&writer->loc, DW_OP_REGX);
                dwarf_emit_uleb128(&writer->loc, debug_register_dwarf_number(writer->input.target, piece->reg));
            }
            else if (piece->kind == DEBUG_LOCATION_FRAME)
            {
                dwarf_emit_u8(&writer->loc, DW_OP_FBREG);
                dwarf_emit_sleb128(&writer->loc, piece->frame_offset);
            }
            else if (piece->kind == DEBUG_LOCATION_CONSTANT)
            {
                dwarf_emit_u8(&writer->loc, DW_OP_CONSTU);
                dwarf_emit_uleb128(&writer->loc, piece->constant);
                dwarf_emit_u8(&writer->loc, DW_OP_STACK_VALUE);
            }
            if (piece->size)
            {
                dwarf_emit_u8(&writer->loc, DW_OP_PIECE);
                dwarf_emit_uleb128(&writer->loc, piece->size);
            }
        }
    }
    else if (location.kind == DEBUG_LOCATION_REGISTER)
    {
        dwarf_emit_u8(&writer->loc, DW_OP_REGX);
        dwarf_emit_uleb128(&writer->loc, debug_register_dwarf_number(writer->input.target, location.reg));
    }
    else if (location.kind == DEBUG_LOCATION_FRAME)
    {
        dwarf_emit_u8(&writer->loc, DW_OP_FBREG);
        dwarf_emit_sleb128(&writer->loc, location.frame_offset);
    }
    else if (location.kind == DEBUG_LOCATION_CONSTANT)
    {
        dwarf_emit_u8(&writer->loc, DW_OP_CONSTU);
        dwarf_emit_uleb128(&writer->loc, location.constant);
        dwarf_emit_u8(&writer->loc, DW_OP_STACK_VALUE);
    }
    return expression_start;
}

BUSTER_GLOBAL_LOCAL u32 dwarf_model_emit_location_list(DwarfModelWriter* writer, DebugVariable* variable)
{
    u32 offset = (u32)writer->loc.count;
    for (u32 range_index = 0; range_index < variable->location_count; range_index += 1)
    {
        DebugLocationRange* range = variable->locations + range_index;
        if (range->end <= range->start || range->location.kind == DEBUG_LOCATION_UNAVAILABLE)
        {
            continue;
        }
        dwarf_model_relocation(writer, (DwarfRelocation){
                                              .addend = range->start,
                                              .offset = writer->loc.count,
                                              .section = DWARF_SECTION_LOC,
                                              .address = true,
                                          });
        dwarf_emit_u64(&writer->loc, 0);
        dwarf_model_relocation(writer, (DwarfRelocation){
                                              .addend = range->end,
                                              .offset = writer->loc.count,
                                              .section = DWARF_SECTION_LOC,
                                              .address = true,
                                          });
        dwarf_emit_u64(&writer->loc, 0);
        u64 length_offset = writer->loc.count;
        dwarf_emit_u16(&writer->loc, 0);
        u64 before = writer->loc.count;
        dwarf_model_emit_location_expression(writer, range->location);
        u16 length = (u16)BUSTER_MIN(writer->loc.count - before, UINT16_MAX);
        memcpy(writer->loc.bytes + length_offset, &length, sizeof(length));
    }
    dwarf_emit_u64(&writer->loc, 0);
    dwarf_emit_u64(&writer->loc, 0);
    return offset;
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_location_attribute(DwarfModelWriter* writer, DebugVariable* variable)
{
    u32 offset = dwarf_model_emit_location_list(writer, variable);
    dwarf_model_relocation(writer, (DwarfRelocation){
                                          .addend = offset,
                                          .offset = writer->info.count,
                                          .section = DWARF_SECTION_INFO,
                                          .target = DWARF_SECTION_LOC,
                                      });
    dwarf_emit_u32(&writer->info, 0);
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_ranges_attribute(DwarfModelWriter* writer, u32 start, u32 end)
{
    u32 offset = dwarf_model_emit_ranges(writer, start, end);
    dwarf_model_relocation(writer, (DwarfRelocation){
                                          .addend = offset,
                                          .offset = writer->info.count,
                                          .section = DWARF_SECTION_INFO,
                                          .target = DWARF_SECTION_RANGES,
                                      });
    dwarf_emit_u32(&writer->info, 0);
}

BUSTER_GLOBAL_LOCAL void dwarf_model_abbrev(DwarfBuffer* buffer, u32 number, u32 tag, bool children, const u32* attributes,
                                            const u32* forms, u32 count)
{
    dwarf_emit_uleb128(buffer, number);
    dwarf_emit_uleb128(buffer, tag);
    dwarf_emit_u8(buffer, children ? 1 : 0);
    for (u32 index = 0; index < count; index += 1)
    {
        dwarf_abbrev_pair(buffer, attributes[index], forms[index]);
    }
    dwarf_abbrev_pair(buffer, 0, 0);
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_abbreviations(DwarfBuffer* buffer)
{
    static const u32 cu_attributes[] = {DW_AT_PRODUCER, DW_AT_LANGUAGE, DW_AT_NAME, DW_AT_COMP_DIR, DW_AT_LOW_PC, DW_AT_HIGH_PC, DW_AT_STMT_LIST};
    static const u32 cu_forms[] = {DW_FORM_STRP, DW_FORM_DATA2, DW_FORM_STRP, DW_FORM_STRP, DW_FORM_ADDR, DW_FORM_DATA8, DW_FORM_SEC_OFFSET};
    static const u32 base_attributes[] = {DW_AT_NAME, DW_AT_BYTE_SIZE, DW_AT_ENCODING, DW_AT_DECL_FILE, DW_AT_DECL_LINE};
    static const u32 base_forms[] = {DW_FORM_STRP, DW_FORM_DATA8, DW_FORM_DATA1, DW_FORM_UDATA, DW_FORM_UDATA};
    static const u32 pointer_attributes[] = {DW_AT_TYPE, DW_AT_BYTE_SIZE};
    static const u32 pointer_forms[] = {DW_FORM_REF4, DW_FORM_DATA8};
    static const u32 array_attributes[] = {DW_AT_TYPE, DW_AT_UPPER_BOUND, DW_AT_BYTE_SIZE};
    static const u32 array_forms[] = {DW_FORM_REF4, DW_FORM_DATA8, DW_FORM_DATA8};
    static const u32 aggregate_attributes[] = {DW_AT_NAME, DW_AT_BYTE_SIZE, DW_AT_DECL_FILE, DW_AT_DECL_LINE};
    static const u32 aggregate_forms[] = {DW_FORM_STRP, DW_FORM_DATA8, DW_FORM_UDATA, DW_FORM_UDATA};
    static const u32 member_attributes[] = {DW_AT_NAME, DW_AT_TYPE, DW_AT_DATA_MEMBER_LOCATION, DW_AT_DECL_FILE, DW_AT_DECL_LINE};
    static const u32 member_forms[] = {DW_FORM_STRP, DW_FORM_REF4, DW_FORM_UDATA, DW_FORM_UDATA, DW_FORM_UDATA};
    static const u32 enum_attributes[] = {DW_AT_NAME, DW_AT_BYTE_SIZE, DW_AT_DECL_FILE, DW_AT_DECL_LINE};
    static const u32 enum_forms[] = {DW_FORM_STRP, DW_FORM_DATA8, DW_FORM_UDATA, DW_FORM_UDATA};
    static const u32 enumerator_attributes[] = {DW_AT_NAME, DW_AT_CONST_VALUE, DW_AT_DECL_FILE, DW_AT_DECL_LINE};
    static const u32 enumerator_forms[] = {DW_FORM_STRP, DW_FORM_DATA8, DW_FORM_UDATA, DW_FORM_UDATA};
    static const u32 typedef_attributes[] = {DW_AT_NAME, DW_AT_TYPE, DW_AT_DECL_FILE, DW_AT_DECL_LINE};
    static const u32 typedef_forms[] = {DW_FORM_STRP, DW_FORM_REF4, DW_FORM_UDATA, DW_FORM_UDATA};
    static const u32 function_type_attributes[] = {DW_AT_NAME, DW_AT_TYPE};
    static const u32 function_type_forms[] = {DW_FORM_STRP, DW_FORM_REF4};
    static const u32 parameter_type_attributes[] = {DW_AT_TYPE};
    static const u32 parameter_type_forms[] = {DW_FORM_REF4};
    static const u32 function_attributes[] = {DW_AT_NAME, DW_AT_DECL_FILE, DW_AT_DECL_LINE, DW_AT_RANGES, DW_AT_TYPE, DW_AT_FRAME_BASE};
    static const u32 function_forms[] = {DW_FORM_STRP, DW_FORM_UDATA, DW_FORM_UDATA, DW_FORM_SEC_OFFSET, DW_FORM_REF4, DW_FORM_EXPRLOC};
    static const u32 lexical_attributes[] = {DW_AT_RANGES};
    static const u32 lexical_forms[] = {DW_FORM_SEC_OFFSET};
    static const u32 variable_attributes[] = {DW_AT_NAME, DW_AT_DECL_FILE, DW_AT_DECL_LINE, DW_AT_TYPE, DW_AT_LOCATION};
    static const u32 variable_forms[] = {DW_FORM_STRP, DW_FORM_UDATA, DW_FORM_UDATA, DW_FORM_REF4, DW_FORM_SEC_OFFSET};
    static const u32 global_attributes[] = {DW_AT_NAME, DW_AT_DECL_FILE, DW_AT_DECL_LINE, DW_AT_TYPE, DW_AT_LOCATION, DW_AT_EXTERNAL};
    static const u32 global_forms[] = {DW_FORM_STRP, DW_FORM_UDATA, DW_FORM_UDATA, DW_FORM_REF4, DW_FORM_EXPRLOC, DW_FORM_DATA1};
    static const u32 inline_attributes[] = {DW_AT_ABSTRACT_ORIGIN, DW_AT_CALL_FILE, DW_AT_CALL_LINE, DW_AT_RANGES};
    static const u32 inline_forms[] = {DW_FORM_REF4, DW_FORM_UDATA, DW_FORM_UDATA, DW_FORM_SEC_OFFSET};
    static const u32 qualified_attributes[] = {DW_AT_TYPE};
    static const u32 qualified_forms[] = {DW_FORM_REF4};
    static const u32 void_attributes[] = {DW_AT_NAME, DW_AT_DECL_FILE, DW_AT_DECL_LINE};
    static const u32 void_forms[] = {DW_FORM_STRP, DW_FORM_UDATA, DW_FORM_UDATA};
    dwarf_model_abbrev(buffer, 1, DW_TAG_COMPILE_UNIT, true, cu_attributes, cu_forms, BUSTER_ARRAY_LENGTH(cu_attributes));
    dwarf_model_abbrev(buffer, 2, DW_TAG_BASE_TYPE, false, base_attributes, base_forms, BUSTER_ARRAY_LENGTH(base_attributes));
    dwarf_model_abbrev(buffer, 3, DW_TAG_POINTER_TYPE, false, pointer_attributes, pointer_forms, BUSTER_ARRAY_LENGTH(pointer_attributes));
    dwarf_model_abbrev(buffer, 4, DW_TAG_ARRAY_TYPE, false, array_attributes, array_forms, BUSTER_ARRAY_LENGTH(array_attributes));
    dwarf_model_abbrev(buffer, 5, DW_TAG_STRUCTURE_TYPE, true, aggregate_attributes, aggregate_forms, BUSTER_ARRAY_LENGTH(aggregate_attributes));
    dwarf_model_abbrev(buffer, 6, DW_TAG_UNION_TYPE, true, aggregate_attributes, aggregate_forms, BUSTER_ARRAY_LENGTH(aggregate_attributes));
    dwarf_model_abbrev(buffer, 7, DW_TAG_MEMBER, false, member_attributes, member_forms, BUSTER_ARRAY_LENGTH(member_attributes));
    dwarf_model_abbrev(buffer, 8, DW_TAG_ENUMERATION_TYPE, true, enum_attributes, enum_forms, BUSTER_ARRAY_LENGTH(enum_attributes));
    dwarf_model_abbrev(buffer, 9, DW_TAG_ENUMERATOR, false, enumerator_attributes, enumerator_forms, BUSTER_ARRAY_LENGTH(enumerator_attributes));
    dwarf_model_abbrev(buffer, 10, DW_TAG_TYPEDEF, false, typedef_attributes, typedef_forms, BUSTER_ARRAY_LENGTH(typedef_attributes));
    dwarf_model_abbrev(buffer, 11, DW_TAG_SUBROUTINE_TYPE, true, function_type_attributes, function_type_forms,
                       BUSTER_ARRAY_LENGTH(function_type_attributes));
    dwarf_model_abbrev(buffer, 12, DW_TAG_FORMAL_PARAMETER, false, parameter_type_attributes, parameter_type_forms,
                       BUSTER_ARRAY_LENGTH(parameter_type_attributes));
    dwarf_model_abbrev(buffer, 13, DW_TAG_SUBPROGRAM, true, function_attributes, function_forms, BUSTER_ARRAY_LENGTH(function_attributes));
    dwarf_model_abbrev(buffer, 14, DW_TAG_LEXICAL_BLOCK, true, lexical_attributes, lexical_forms, BUSTER_ARRAY_LENGTH(lexical_attributes));
    dwarf_model_abbrev(buffer, 15, DW_TAG_VARIABLE, false, variable_attributes, variable_forms, BUSTER_ARRAY_LENGTH(variable_attributes));
    dwarf_model_abbrev(buffer, 16, DW_TAG_FORMAL_PARAMETER, false, variable_attributes, variable_forms, BUSTER_ARRAY_LENGTH(variable_attributes));
    dwarf_model_abbrev(buffer, 17, DW_TAG_VARIABLE, false, global_attributes, global_forms, BUSTER_ARRAY_LENGTH(global_attributes));
    dwarf_model_abbrev(buffer, 18, DW_TAG_INLINED_SUBROUTINE, true, inline_attributes, inline_forms, BUSTER_ARRAY_LENGTH(inline_attributes));
    dwarf_model_abbrev(buffer, 19, DW_TAG_CONST_TYPE, false, qualified_attributes, qualified_forms, BUSTER_ARRAY_LENGTH(qualified_attributes));
    dwarf_model_abbrev(buffer, 20, DW_TAG_UNSPECIFIED_TYPE, false, void_attributes, void_forms, BUSTER_ARRAY_LENGTH(void_attributes));
    dwarf_model_abbrev(buffer, 21, DW_TAG_ARRAY_TYPE, false, array_attributes, array_forms, BUSTER_ARRAY_LENGTH(array_attributes));
    dwarf_model_abbrev(buffer, 22, DW_TAG_SUBROUTINE_TYPE, false, function_type_attributes, function_type_forms,
                       BUSTER_ARRAY_LENGTH(function_type_attributes));
    dwarf_model_abbrev(buffer, 23, DW_TAG_SUBPROGRAM, false, function_attributes, function_forms, BUSTER_ARRAY_LENGTH(function_attributes));
    dwarf_model_abbrev(buffer, 24, DW_TAG_LEXICAL_BLOCK, false, lexical_attributes, lexical_forms, BUSTER_ARRAY_LENGTH(lexical_attributes));
    dwarf_model_abbrev(buffer, 25, DW_TAG_INLINED_SUBROUTINE, false, inline_attributes, inline_forms, BUSTER_ARRAY_LENGTH(inline_attributes));
    dwarf_emit_uleb128(buffer, 0);
}

BUSTER_GLOBAL_LOCAL u8 dwarf_model_base_encoding(DebugType* type)
{
    if (type->bit_width == 1 || string_equal(type->name, S8("bool")) || string_equal(type->name, S8("boolean")))
    {
        return 0x02;
    }
    if (type->name.length && (type->name.pointer[0] == 'f' || type->name.pointer[0] == 'F'))
    {
        return 0x04;
    }
    return type->is_signed ? 0x05 : 0x07;
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_declaration(DwarfModelWriter* writer, DebugSourceLocation declaration)
{
    dwarf_emit_uleb128(&writer->info, dwarf_model_file(writer->model, declaration, writer->input.file_count));
    dwarf_emit_uleb128(&writer->info, dwarf_model_line(declaration));
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_type(DwarfModelWriter* writer, DebugTypeId type_id)
{
    if (type_id >= writer->model->type_count)
    {
        return;
    }
    DebugType* type = writer->model->types + type_id;
    writer->type_offsets[type_id] = (u32)writer->info.count;
    DebugTypeId unqualified = type->unqualified_type;
    switch (type->kind)
    {
    case DEBUG_TYPE_VOID:
        dwarf_emit_uleb128(&writer->info, 20);
        dwarf_model_string(writer, type->name);
        dwarf_model_emit_declaration(writer, type->declaration);
        break;
    case DEBUG_TYPE_POINTER:
        dwarf_emit_uleb128(&writer->info, 3);
        dwarf_model_type_reference(writer, type->element_type);
        dwarf_emit_u64(&writer->info, type->size);
        break;
    case DEBUG_TYPE_ARRAY:
        dwarf_emit_uleb128(&writer->info, 4);
        dwarf_model_type_reference(writer, type->element_type);
        dwarf_emit_u64(&writer->info, type->element_count ? type->element_count - 1 : 0);
        dwarf_emit_u64(&writer->info, type->size);
        break;
    case DEBUG_TYPE_VECTOR:
        dwarf_emit_uleb128(&writer->info, 21);
        dwarf_model_type_reference(writer, type->element_type);
        dwarf_emit_u64(&writer->info, type->element_count ? type->element_count - 1 : 0);
        dwarf_emit_u64(&writer->info, type->size);
        break;
    case DEBUG_TYPE_STRUCT:
    case DEBUG_TYPE_UNION:
        dwarf_emit_uleb128(&writer->info, type->kind == DEBUG_TYPE_STRUCT ? 5 : 6);
        dwarf_model_string(writer, type->name);
        dwarf_emit_u64(&writer->info, type->size);
        dwarf_model_emit_declaration(writer, type->declaration);
        for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
        {
            DebugTypeField* field = type->fields + field_index;
            dwarf_emit_uleb128(&writer->info, 7);
            dwarf_model_string(writer, field->name);
            dwarf_model_type_reference(writer, field->type);
            dwarf_emit_uleb128(&writer->info, field->offset);
            dwarf_model_emit_declaration(writer, field->declaration);
        }
        dwarf_emit_u8(&writer->info, 0);
        break;
    case DEBUG_TYPE_ENUM:
        dwarf_emit_uleb128(&writer->info, 8);
        dwarf_model_string(writer, type->name);
        dwarf_emit_u64(&writer->info, type->size);
        dwarf_model_emit_declaration(writer, type->declaration);
        for (u32 member_index = 0; member_index < type->enum_member_count; member_index += 1)
        {
            DebugEnumMember* member = type->enum_members + member_index;
            dwarf_emit_uleb128(&writer->info, 9);
            dwarf_model_string(writer, member->name);
            dwarf_emit_u64(&writer->info, member->value);
            dwarf_model_emit_declaration(writer, member->declaration);
        }
        dwarf_emit_u8(&writer->info, 0);
        break;
    case DEBUG_TYPE_TYPEDEF:
        dwarf_emit_uleb128(&writer->info, 10);
        dwarf_model_string(writer, type->name);
        dwarf_model_type_reference(writer, type->element_type != DEBUG_ID_INVALID ? type->element_type : unqualified);
        dwarf_model_emit_declaration(writer, type->declaration);
        break;
    case DEBUG_TYPE_FUNCTION:
        dwarf_emit_uleb128(&writer->info, type->parameter_count ? 11 : 22);
        dwarf_model_string(writer, type->name);
        dwarf_model_type_reference(writer, type->return_type);
        for (u32 parameter_index = 0; parameter_index < type->parameter_count; parameter_index += 1)
        {
            dwarf_emit_uleb128(&writer->info, 12);
            dwarf_model_type_reference(writer, type->parameter_types[parameter_index]);
        }
        if (type->parameter_count)
        {
            dwarf_emit_u8(&writer->info, 0);
        }
        break;
    case DEBUG_TYPE_QUALIFIED:
        dwarf_emit_uleb128(&writer->info, 19);
        dwarf_model_type_reference(writer, type->unqualified_type != DEBUG_ID_INVALID ? type->unqualified_type : type->element_type);
        break;
    case DEBUG_TYPE_BASE:
        dwarf_emit_uleb128(&writer->info, 2);
        dwarf_model_string(writer, type->name);
        dwarf_emit_u64(&writer->info, type->size);
        dwarf_emit_u8(&writer->info, dwarf_model_base_encoding(type));
        dwarf_model_emit_declaration(writer, type->declaration);
        break;
    case DEBUG_TYPE_COUNT:
        break;
    }
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_variable(DwarfModelWriter* writer, DebugVariable* variable, bool parameter)
{
    dwarf_emit_uleb128(&writer->info, parameter ? 16 : 15);
    dwarf_model_string(writer, variable->name);
    dwarf_model_emit_declaration(writer, variable->declaration);
    dwarf_model_type_reference(writer, variable->type);
    dwarf_model_emit_location_attribute(writer, variable);
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_global(DwarfModelWriter* writer, DebugVariable* variable)
{
    dwarf_emit_uleb128(&writer->info, 17);
    dwarf_model_string(writer, variable->name);
    dwarf_model_emit_declaration(writer, variable->declaration);
    dwarf_model_type_reference(writer, variable->type);
    dwarf_emit_uleb128(&writer->info, 9);
    dwarf_emit_u8(&writer->info, DW_OP_ADDR);
    u64 address_offset = writer->info.count;
    dwarf_model_relocation(writer, (DwarfRelocation){
                                          .offset = address_offset,
                                          .section = DWARF_SECTION_INFO,
                                          .address = true,
                                          .symbol_address = true,
                                          .symbol_name = variable->linkage_name.length ? variable->linkage_name : variable->name,
                                      });
    dwarf_emit_u64(&writer->info, 0);
    dwarf_emit_u8(&writer->info, 1);
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_scope_variables(DwarfModelWriter* writer, DebugScope* scope)
{
    for (u32 variable_index = 0; variable_index < scope->variable_count; variable_index += 1)
    {
        DebugVariable* variable = writer->model->variables + scope->variables[variable_index];
        if (variable->kind == DEBUG_VARIABLE_GLOBAL)
        {
            continue;
        }
        dwarf_model_emit_variable(writer, variable, variable->kind == DEBUG_VARIABLE_PARAMETER);
    }
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_inline(DwarfModelWriter* writer, DebugInlineSite* site)
{
    // Inline-variable children are not produced until an inliner supplies
    // their declarations.  Use the leaf form for the current capability so
    // strict DWARF consumers do not see a children flag with only a null
    // terminator.
    dwarf_emit_uleb128(&writer->info, 25);
    u32 function_index = UINT32_MAX;
    if (site->function && site->function >= writer->model->functions && site->function < writer->model->functions + writer->model->function_count)
    {
        function_index = (u32)(site->function - writer->model->functions);
    }
    dwarf_model_function_reference(writer, function_index);
    dwarf_emit_uleb128(&writer->info, dwarf_model_file(writer->model, site->call_site, writer->input.file_count));
    dwarf_emit_uleb128(&writer->info, dwarf_model_line(site->call_site));
    dwarf_model_emit_ranges_attribute(writer, site->start, site->end);
}

typedef struct DwarfModelScopeFrame DwarfModelScopeFrame;
struct DwarfModelScopeFrame
{
    DebugScopeId scope;
    u32 next_child;
    bool entered;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL bool dwarf_model_scope_has_child(DebugModel* model, DebugScopeId parent)
{
    for (u32 scope_index = 0; scope_index < model->scope_count; scope_index += 1)
    {
        if (model->scopes[scope_index].parent == parent)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_scope_tree(DwarfModelWriter* writer, DebugScopeId root)
{
    if (root == DEBUG_SCOPE_INVALID || root >= writer->model->scope_count)
    {
        return;
    }
    DwarfModelScopeFrame* stack = arena_allocate(writer->arena, DwarfModelScopeFrame, writer->model->scope_count + 1);
    u32 stack_count = 1;
    stack[0] = (DwarfModelScopeFrame){.scope = root, .entered = true};
    for (;;)
    {
        DwarfModelScopeFrame* frame = stack + stack_count - 1;
        u32 child = UINT32_MAX;
        while (frame->next_child < writer->model->scope_count)
        {
            u32 candidate = frame->next_child++;
            if (candidate != root && writer->model->scopes[candidate].parent == frame->scope)
            {
                child = candidate;
                break;
            }
        }
        if (child != UINT32_MAX)
        {
            DebugScope* child_scope = writer->model->scopes + child;
            bool has_child = child_scope->variable_count != 0 || dwarf_model_scope_has_child(writer->model, child);
            dwarf_emit_uleb128(&writer->info, has_child ? 14 : 24);
            dwarf_model_emit_ranges_attribute(writer, child_scope->start, child_scope->end);
            dwarf_model_emit_scope_variables(writer, child_scope);
            if (has_child)
            {
                stack[stack_count++] = (DwarfModelScopeFrame){.scope = child};
            }
            continue;
        }
        dwarf_emit_u8(&writer->info, 0);
        stack_count -= 1;
        if (!stack_count)
        {
            break;
        }
    }
}

BUSTER_GLOBAL_LOCAL DebugTypeId dwarf_model_function_return_type(DebugModel* model, DebugTypeId function_type)
{
    // DW_AT_TYPE on a DW_TAG_subprogram is the result type.  The complete
    // callable signature is emitted separately as a subroutine type and as
    // the child formal-parameter DIEs used by debuggers when evaluating calls.
    if (model && function_type != DEBUG_ID_INVALID && function_type < model->type_count && model->types[function_type].kind == DEBUG_TYPE_FUNCTION)
    {
        return model->types[function_type].return_type;
    }
    return function_type;
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_frame_base(DwarfModelWriter* writer)
{
    // Codegen normalizes frame locations to these registers, including the
    // AArch64 translation from final-SP offsets to the saved X29 value.
    DebugRegister frame_register = writer->input.target.cpu_arch == CPU_ARCH_X86_64 ? DEBUG_REGISTER_X86_RBP : DEBUG_REGISTER_AARCH64_X29;
    u32 dwarf_register = debug_register_dwarf_number(writer->input.target, frame_register);
    if (dwarf_register != UINT32_MAX)
    {
        dwarf_emit_uleb128(&writer->info, 2);
        dwarf_emit_u8(&writer->info, DW_OP_REGX);
        dwarf_emit_uleb128(&writer->info, dwarf_register);
    }
    else
    {
        dwarf_emit_uleb128(&writer->info, 1);
        dwarf_emit_u8(&writer->info, DW_OP_CALL_FRAME_CFA);
    }
}

BUSTER_GLOBAL_LOCAL void dwarf_model_emit_function(DwarfModelWriter* writer, u32 function_index)
{
    DebugFunction* function = writer->model->functions + function_index;
    writer->function_offsets[function_index] = (u32)writer->info.count;
    bool has_children = false;
    if (function->scope < writer->model->scope_count)
    {
        DebugScope* scope = writer->model->scopes + function->scope;
        has_children = scope->variable_count != 0 || dwarf_model_scope_has_child(writer->model, function->scope);
    }
    for (u32 inline_index = 0; inline_index < writer->model->inline_site_count; inline_index += 1)
    {
        if (writer->model->inline_sites[inline_index].function == function)
        {
            has_children = true;
            break;
        }
    }
    dwarf_emit_uleb128(&writer->info, has_children ? 13 : 23);
    dwarf_model_string(writer, function->name);
    dwarf_model_emit_declaration(writer, function->declaration);
    dwarf_model_emit_ranges_attribute(writer, function->code_offset, function->code_offset + function->code_size);
    dwarf_model_type_reference(writer, dwarf_model_function_return_type(writer->model, function->type));
    dwarf_model_emit_frame_base(writer);

    if (has_children && function->scope < writer->model->scope_count)
    {
        dwarf_model_emit_scope_variables(writer, writer->model->scopes + function->scope);
    }
    for (u32 inline_index = 0; has_children && inline_index < writer->model->inline_site_count; inline_index += 1)
    {
        DebugInlineSite* site = writer->model->inline_sites + inline_index;
        if (site->function == function)
        {
            dwarf_model_emit_inline(writer, site);
        }
    }
    if (has_children)
    {
        dwarf_model_emit_scope_tree(writer, function->scope);
    }
}

DwarfResult dwarf_build_model(Arena* arena, DwarfInput input)
{
    DwarfResult result = {0};
    DebugModel* model = input.model;
    if (!arena || !model || !model->valid)
    {
        return result;
    }
    DwarfInput line_input = input;
    line_input.model = 0;
    String8* model_paths = 0;
    if (!line_input.file_count || !line_input.file_paths)
    {
        u32 count = model->source_count ? model->source_count : 1;
        model_paths = arena_allocate(arena, String8, count);
        for (u32 index = 0; index < count; index += 1)
        {
            model_paths[index] = model->source_count ? model->source_paths[index] : S8(".");
        }
        line_input.file_paths = model_paths;
        line_input.file_count = count;
    }
    DwarfFunction* model_functions = 0;
    if (!line_input.function_count || !line_input.functions)
    {
        model_functions = arena_allocate(arena, DwarfFunction, model->function_count);
        for (u32 index = 0; index < model->function_count; index += 1)
        {
            DebugFunction* function = model->functions + index;
            model_functions[index] = (DwarfFunction){
                .name = function->name,
                .code_offset = function->code_offset,
                .code_size = function->code_size,
                .file = function->declaration.source,
                .line = function->declaration.line,
            };
        }
        line_input.functions = model_functions;
        line_input.function_count = model->function_count;
    }
    if (!line_input.code_size)
    {
        for (u32 index = 0; index < line_input.function_count; index += 1)
        {
            u64 end = (u64)line_input.functions[index].code_offset + line_input.functions[index].code_size;
            line_input.code_size = BUSTER_MAX(line_input.code_size, end);
        }
    }
    DwarfResult line_result = dwarf_build_legacy(arena, line_input);
    if (!line_result.valid)
    {
        return result;
    }
    u64 string_capacity = 32 + input.producer.length + input.comp_dir.length;
    u64 string_entry_capacity = 32 + line_input.file_count + model->type_count + model->function_count;
    for (u32 file_index = 0; file_index < line_input.file_count; file_index += 1)
    {
        string_capacity += line_input.file_paths[file_index].length + 1;
    }
    u64 reference_capacity = 32 + (u64)model->type_count * 8 + (u64)model->function_count * 4 + (u64)model->variable_count * 2;
    for (u32 type_index = 0; type_index < model->type_count; type_index += 1)
    {
        DebugType* type = model->types + type_index;
        string_capacity += type->name.length + type->declaration_name.length + 2;
        string_entry_capacity += type->field_count + type->enum_member_count;
        for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
        {
            string_capacity += type->fields[field_index].name.length + 1;
        }
        for (u32 member_index = 0; member_index < type->enum_member_count; member_index += 1)
        {
            string_capacity += type->enum_members[member_index].name.length + 1;
        }
        reference_capacity += type->field_count + type->parameter_count;
    }
    for (u32 function_index = 0; function_index < model->function_count; function_index += 1)
    {
        string_capacity += model->functions[function_index].name.length + 1;
    }
    for (u32 variable_index = 0; variable_index < model->variable_count; variable_index += 1)
    {
        string_capacity += model->variables[variable_index].name.length + 1;
    }
    string_entry_capacity += model->variable_count + model->inline_site_count;
    u64 info_capacity = 1024 + string_capacity * 12 + reference_capacity * 12;
    u64 range_capacity = 32 + ((u64)model->function_count + model->scope_count + model->inline_site_count) * 32;
    u64 location_capacity = 32 + (u64)model->variable_count * 128;
    u64 relocation_capacity = 64 + (u64)model->function_count * 20 + (u64)model->scope_count * 8 + (u64)model->variable_count * 12 +
                              (u64)model->type_count * 16;
    DwarfModelWriter writer = {
        .arena = arena,
        .input = input,
        .model = model,
        .str = {.bytes = arena_allocate(arena, u8, string_capacity), .capacity = string_capacity},
        .abbrev = {.bytes = arena_allocate(arena, u8, 2048), .capacity = 2048},
        .info = {.bytes = arena_allocate(arena, u8, info_capacity), .capacity = info_capacity},
        .loc = {.bytes = arena_allocate(arena, u8, location_capacity), .capacity = location_capacity},
        .ranges = {.bytes = arena_allocate(arena, u8, range_capacity), .capacity = range_capacity},
        .relocations = arena_allocate(arena, DwarfRelocation, relocation_capacity),
        .refs = arena_allocate(arena, DwarfModelRefPatch, reference_capacity),
        .relocation_capacity = (u32)BUSTER_MIN(relocation_capacity, UINT32_MAX),
        .ref_capacity = (u32)BUSTER_MIN(reference_capacity, UINT32_MAX),
        .type_offsets = arena_allocate(arena, u32, model->type_count ? model->type_count : 1),
        .function_offsets = arena_allocate(arena, u32, model->function_count ? model->function_count : 1),
    };
    writer.strings = dwarf_string_table_make(arena, &writer.str, (u32)BUSTER_MIN(string_entry_capacity, UINT32_MAX));
    dwarf_model_emit_abbreviations(&writer.abbrev);
    dwarf_emit_u32(&writer.info, 0);
    dwarf_emit_u16(&writer.info, DWARF_VERSION);
    dwarf_model_relocation(&writer, (DwarfRelocation){
                                          .offset = writer.info.count,
                                          .section = DWARF_SECTION_INFO,
                                          .target = DWARF_SECTION_ABBREV,
                                      });
    dwarf_emit_u32(&writer.info, 0);
    dwarf_emit_u8(&writer.info, DWARF_ADDRESS_SIZE);
    dwarf_emit_uleb128(&writer.info, 1);
    dwarf_model_string(&writer, line_input.file_paths[0]);
    dwarf_emit_u16(&writer.info, line_input.language ? line_input.language : 0x0002);
    dwarf_model_string(&writer, line_input.file_paths[0]);
    dwarf_model_string(&writer, line_input.comp_dir);
    dwarf_model_relocation(&writer, (DwarfRelocation){
                                          .offset = writer.info.count,
                                          .section = DWARF_SECTION_INFO,
                                          .address = true,
                                      });
    dwarf_emit_u64(&writer.info, 0);
    dwarf_emit_u64(&writer.info, line_input.code_size);
    dwarf_model_relocation(&writer, (DwarfRelocation){
                                          .offset = writer.info.count,
                                          .section = DWARF_SECTION_INFO,
                                          .target = DWARF_SECTION_LINE,
                                      });
    dwarf_emit_u32(&writer.info, 0);
    for (u32 type_index = 0; type_index < model->type_count; type_index += 1)
    {
        dwarf_model_emit_type(&writer, type_index);
    }
    for (u32 variable_index = 0; variable_index < model->variable_count; variable_index += 1)
    {
        DebugVariable* variable = model->variables + variable_index;
        if (variable->kind == DEBUG_VARIABLE_GLOBAL)
        {
            dwarf_model_emit_global(&writer, variable);
        }
    }
    for (u32 function_index = 0; function_index < model->function_count; function_index += 1)
    {
        dwarf_model_emit_function(&writer, function_index);
    }
    dwarf_emit_u8(&writer.info, 0);
    if (writer.info.count >= 4 && writer.info.count - 4 <= UINT32_MAX)
    {
        dwarf_write_u32_at(&writer.info, 0, (u32)(writer.info.count - 4));
    }
    for (u32 patch_index = 0; patch_index < writer.ref_count; patch_index += 1)
    {
        DwarfModelRefPatch patch = writer.refs[patch_index];
        u32 target_offset = 0;
        if (patch.function)
        {
            if (patch.target < model->function_count)
            {
                target_offset = writer.function_offsets[patch.target];
            }
        }
        else if (patch.target < model->type_count)
        {
            target_offset = writer.type_offsets[patch.target];
        }
        // DW_FORM_ref4 is relative to the beginning of the compilation unit,
        // including its four-byte length field.  The writer's offsets already
        // use that same section-relative origin.
        dwarf_write_u32_at(&writer.info, patch.offset, target_offset);
    }
    for (u32 relocation_index = 0; relocation_index < line_result.relocation_count; relocation_index += 1)
    {
        DwarfRelocation relocation = line_result.relocations[relocation_index];
        if (relocation.section == DWARF_SECTION_LINE)
        {
            dwarf_model_relocation(&writer, relocation);
        }
    }
    result.sections[DWARF_SECTION_INFO] = (ByteSlice){.pointer = writer.info.bytes, .length = writer.info.count};
    result.sections[DWARF_SECTION_ABBREV] = (ByteSlice){.pointer = writer.abbrev.bytes, .length = writer.abbrev.count};
    result.sections[DWARF_SECTION_LINE] = line_result.sections[DWARF_SECTION_LINE];
    result.sections[DWARF_SECTION_STR] = (ByteSlice){.pointer = writer.str.bytes, .length = writer.str.count};
    result.sections[DWARF_SECTION_LOC] = (ByteSlice){.pointer = writer.loc.bytes, .length = writer.loc.count};
    result.sections[DWARF_SECTION_RANGES] = (ByteSlice){.pointer = writer.ranges.bytes, .length = writer.ranges.count};
    result.relocations = writer.relocations;
    result.relocation_count = writer.relocation_count;
    result.valid = !writer.str.overflow && !writer.abbrev.overflow && !writer.info.overflow && !writer.loc.overflow && !writer.ranges.overflow;
    return result;
}

DwarfResult dwarf_build(Arena* arena, DwarfInput input)
{
    return input.model ? dwarf_build_model(arena, input) : dwarf_build_legacy(arena, input);
}
