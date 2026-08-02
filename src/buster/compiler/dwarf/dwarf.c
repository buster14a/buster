#include <buster/compiler/dwarf/dwarf.h>
#include <buster/hash.h>
#include <buster/string.h>

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
    DW_TAG_SUBPROGRAM = 0x2e,
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
};

enum
{
    DW_FORM_ADDR = 0x01,
    DW_FORM_DATA2 = 0x05,
    DW_FORM_DATA8 = 0x07,
    DW_FORM_STRP = 0x0e,
    DW_FORM_UDATA = 0x0f,
    DW_FORM_SEC_OFFSET = 0x17,
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
    memcpy(buffer->bytes + buffer->count, source, size);
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

DwarfResult dwarf_build(Arena* arena, DwarfInput input)
{
    DwarfResult result = {0};
    if (!arena || !input.file_count || !input.file_paths || (input.function_count && !input.functions) || (input.line_count && !input.lines) ||
        !input.function_count)
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

#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL bool dwarf_test_read_uleb128(ByteSlice bytes, u64* offset, u64* value)
{
    u64 shift = 0;
    u64 decoded = 0;
    while (*offset < bytes.length)
    {
        u8 byte = bytes.pointer[*offset];
        *offset += 1;
        decoded |= (u64)(byte & 0x7f) << shift;
        if (!(byte & 0x80))
        {
            *value = decoded;
            return true;
        }
        shift += 7;
        if (shift >= 64)
        {
            break;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool dwarf_test_read_sleb128(ByteSlice bytes, u64* offset, s64* value)
{
    u64 shift = 0;
    u64 decoded = 0;
    while (*offset < bytes.length)
    {
        u8 byte = bytes.pointer[*offset];
        *offset += 1;
        decoded |= (u64)(byte & 0x7f) << shift;
        shift += 7;
        if (!(byte & 0x80))
        {
            if (shift < 64 && (byte & 0x40))
            {
                decoded |= ~(u64)0 << shift;
            }
            *value = (s64)decoded;
            return true;
        }
        if (shift >= 64)
        {
            break;
        }
    }
    return false;
}

bool dwarf_line_lookup(ByteSlice debug_line, u64 address, DwarfLineRow* row)
{
    if (debug_line.length < 16 || !row)
    {
        return false;
    }
    u32 unit_length;
    memcpy(&unit_length, debug_line.pointer, sizeof(unit_length));
    u16 version;
    memcpy(&version, debug_line.pointer + 4, sizeof(version));
    u32 header_length;
    memcpy(&header_length, debug_line.pointer + 6, sizeof(header_length));
    if (version != DWARF_VERSION || (u64)unit_length + 4 > debug_line.length)
    {
        return false;
    }
    u64 offset = 10;
    u8 minimum_instruction_length = debug_line.pointer[offset];
    s8 line_base = (s8)debug_line.pointer[offset + 3];
    u8 line_range = debug_line.pointer[offset + 4];
    u8 opcode_base = debug_line.pointer[offset + 5];
    if (!minimum_instruction_length || !line_range || !opcode_base)
    {
        return false;
    }
    u64 program_offset = 10 + header_length;
    u64 unit_end = (u64)unit_length + 4;
    DwarfLineRow state = {
        .file = 1,
        .line = 1,
    };
    DwarfLineRow previous = {0};
    bool have_previous = false;
    offset = program_offset;
    while (offset < unit_end)
    {
        u8 opcode = debug_line.pointer[offset];
        offset += 1;
        bool emit_row = false;
        bool end_sequence = false;
        if (opcode >= opcode_base)
        {
            u8 adjusted = opcode - opcode_base;
            state.address += (u64)(adjusted / line_range) * minimum_instruction_length;
            state.line = (u32)((s64)state.line + line_base + (adjusted % line_range));
            emit_row = true;
        }
        else if (opcode == 0)
        {
            u64 length;
            if (!dwarf_test_read_uleb128(debug_line, &offset, &length) || offset + length > unit_end || !length)
            {
                return false;
            }
            u8 extended = debug_line.pointer[offset];
            if (extended == DW_LNE_END_SEQUENCE)
            {
                emit_row = true;
                end_sequence = true;
            }
            else if (extended == DW_LNE_SET_ADDRESS && length == 1 + DWARF_ADDRESS_SIZE)
            {
                memcpy(&state.address, debug_line.pointer + offset + 1, sizeof(state.address));
            }
            offset += length;
        }
        else if (opcode == DW_LNS_COPY)
        {
            emit_row = true;
        }
        else if (opcode == DW_LNS_ADVANCE_PC)
        {
            u64 advance;
            if (!dwarf_test_read_uleb128(debug_line, &offset, &advance))
            {
                return false;
            }
            state.address += advance * minimum_instruction_length;
        }
        else if (opcode == DW_LNS_ADVANCE_LINE)
        {
            s64 delta;
            if (!dwarf_test_read_sleb128(debug_line, &offset, &delta))
            {
                return false;
            }
            state.line = (u32)((s64)state.line + delta);
        }
        else if (opcode == DW_LNS_SET_FILE)
        {
            u64 file;
            if (!dwarf_test_read_uleb128(debug_line, &offset, &file))
            {
                return false;
            }
            state.file = (u32)file;
        }
        else if (opcode == DW_LNS_SET_COLUMN)
        {
            u64 column;
            if (!dwarf_test_read_uleb128(debug_line, &offset, &column))
            {
                return false;
            }
            state.column = (u32)column;
        }
        else
        {
            u32 operand_count = opcode < sizeof((u8[]){0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1}) + 1 ? (u8[]){0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1}[opcode - 1] : 0;
            for (u32 operand = 0; operand < operand_count; operand += 1)
            {
                u64 ignored;
                if (!dwarf_test_read_uleb128(debug_line, &offset, &ignored))
                {
                    return false;
                }
            }
        }
        if (emit_row)
        {
            if (have_previous && previous.address <= address && address < state.address)
            {
                *row = previous;
                return true;
            }
            previous = state;
            previous.end_sequence = end_sequence;
            have_previous = !end_sequence;
            if (end_sequence)
            {
                state = (DwarfLineRow){
                    .file = 1,
                    .line = 1,
                };
            }
        }
    }
    return false;
}

UnitTestResult dwarf_tests(UnitTestArguments* arguments)
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
        {.code_offset = 16, .file = 0, .line = 5, .column = 5},
        {.code_offset = 32, .file = 1, .line = 10, .column = 1},
        {.code_offset = 48, .file = 1, .line = 12, .column = 9},
    };
    DwarfInput input = {
        .producer = S8("buster"),
        .comp_dir = S8("."),
        .file_paths = files,
        .functions = functions,
        .lines = lines,
        .code_size = 80,
        .file_count = BUSTER_ARRAY_LENGTH(files),
        .function_count = BUSTER_ARRAY_LENGTH(functions),
        .line_count = BUSTER_ARRAY_LENGTH(lines),
        .language = 0x000c,
    };
    DwarfResult built = dwarf_build(arguments->arena, input);
    BUSTER_TEST(arguments, built.valid);
    BUSTER_TEST(arguments, built.sections[DWARF_SECTION_INFO].length > 11);
    BUSTER_TEST(arguments, built.sections[DWARF_SECTION_ABBREV].length > 8);
    BUSTER_TEST(arguments, built.sections[DWARF_SECTION_LINE].length > 32);
    BUSTER_TEST(arguments, built.sections[DWARF_SECTION_STR].length > 8);
    BUSTER_TEST(arguments, built.relocation_count == 2 * BUSTER_ARRAY_LENGTH(functions) + 7);
    if (!built.valid)
    {
        return result;
    }
    u32 info_unit_length;
    memcpy(&info_unit_length, built.sections[DWARF_SECTION_INFO].pointer, sizeof(info_unit_length));
    BUSTER_TEST(arguments, (u64)info_unit_length + 4 == built.sections[DWARF_SECTION_INFO].length);
    u16 info_version;
    memcpy(&info_version, built.sections[DWARF_SECTION_INFO].pointer + 4, sizeof(info_version));
    BUSTER_TEST(arguments, info_version == DWARF_VERSION);
    u32 line_unit_length;
    memcpy(&line_unit_length, built.sections[DWARF_SECTION_LINE].pointer, sizeof(line_unit_length));
    BUSTER_TEST(arguments, (u64)line_unit_length + 4 == built.sections[DWARF_SECTION_LINE].length);
    for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
    {
        DwarfRelocation relocation = built.relocations[relocation_index];
        ByteSlice section = built.sections[relocation.section];
        BUSTER_TEST(arguments, relocation.offset + (relocation.address ? 8u : 4u) <= section.length);
        BUSTER_TEST(arguments, relocation.address || relocation.target < DWARF_SECTION_COUNT);
    }
    // Resolve the address slots as if the text base were at 0x400000, then
    // check the line table maps addresses back to the recorded lines. The
    // 32-bit section-offset slots already hold single-object values.
    u64 text_base = 0x400000;
    for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
    {
        DwarfRelocation relocation = built.relocations[relocation_index];
        if (!relocation.address)
        {
            continue;
        }
        u64 value = text_base + (u64)relocation.addend;
        memcpy(built.sections[relocation.section].pointer + relocation.offset, &value, sizeof(value));
    }
    DwarfLineRow row = {0};
    BUSTER_TEST(arguments, dwarf_line_lookup(built.sections[DWARF_SECTION_LINE], text_base + 0, &row));
    BUSTER_TEST(arguments, row.line == 3 && row.file == 1 && row.column == 1);
    BUSTER_TEST(arguments, dwarf_line_lookup(built.sections[DWARF_SECTION_LINE], text_base + 10, &row));
    BUSTER_TEST(arguments, row.line == 4 && row.column == 5);
    BUSTER_TEST(arguments, dwarf_line_lookup(built.sections[DWARF_SECTION_LINE], text_base + 40, &row));
    BUSTER_TEST(arguments, row.line == 10 && row.file == 2);
    BUSTER_TEST(arguments, dwarf_line_lookup(built.sections[DWARF_SECTION_LINE], text_base + 79, &row));
    BUSTER_TEST(arguments, row.line == 12 && row.column == 9);
    // Invalid input: file index out of range must be rejected.
    DwarfLineEntry invalid_line = {.code_offset = 0, .file = 7, .line = 1, .column = 1};
    DwarfInput invalid = input;
    invalid.lines = &invalid_line;
    invalid.line_count = 1;
    DwarfResult rejected = dwarf_build(arguments->arena, invalid);
    BUSTER_TEST(arguments, !rejected.valid);
    return result;
}
#endif
