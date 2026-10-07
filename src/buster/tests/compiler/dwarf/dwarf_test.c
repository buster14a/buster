#include <buster/tests/compiler/dwarf/dwarf_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/string.h>


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

enum
{
    DWARF_TEST_TAG_BASE_TYPE = 0x24,
    DWARF_TEST_AT_BYTE_SIZE = 0x0b,
    DWARF_TEST_AT_BIT_SIZE = 0x0d,
    DWARF_TEST_AT_ENCODING = 0x3e,
    DWARF_TEST_FORM_ADDR = 0x01,
    DWARF_TEST_FORM_DATA1 = 0x0b,
    DWARF_TEST_FORM_DATA2 = 0x05,
    DWARF_TEST_FORM_DATA4 = 0x06,
    DWARF_TEST_FORM_DATA8 = 0x07,
    DWARF_TEST_FORM_STRP = 0x0e,
    DWARF_TEST_FORM_UDATA = 0x0f,
    DWARF_TEST_FORM_SEC_OFFSET = 0x17,
    DWARF_TEST_FORM_REF4 = 0x13,
    DWARF_TEST_FORM_EXPRLOC = 0x18,
};

typedef struct DwarfTestAbbrev DwarfTestAbbrev;
struct DwarfTestAbbrev
{
    u32 tag;
    u32 attribute_count;
    u32 attributes[8];
    u32 forms[8];
    bool children;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL bool dwarf_test_find_abbrev(ByteSlice bytes, u32 wanted, DwarfTestAbbrev* result)
{
    u64 offset = 0;
    while (offset < bytes.length)
    {
        u64 number;
        if (!dwarf_test_read_uleb128(bytes, &offset, &number))
        {
            return false;
        }
        if (!number)
        {
            return false;
        }
        u64 tag;
        if (!dwarf_test_read_uleb128(bytes, &offset, &tag) || offset >= bytes.length)
        {
            return false;
        }
        DwarfTestAbbrev candidate = {
            .tag = (u32)tag,
            .children = bytes.pointer[offset++] != 0,
        };
        for (;;)
        {
            u64 attribute;
            u64 form;
            if (!dwarf_test_read_uleb128(bytes, &offset, &attribute) || !dwarf_test_read_uleb128(bytes, &offset, &form))
            {
                return false;
            }
            if (!attribute && !form)
            {
                break;
            }
            if (candidate.attribute_count >= BUSTER_ARRAY_LENGTH(candidate.attributes))
            {
                return false;
            }
            candidate.attributes[candidate.attribute_count] = (u32)attribute;
            candidate.forms[candidate.attribute_count] = (u32)form;
            candidate.attribute_count += 1;
        }
        if (number == wanted)
        {
            *result = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool dwarf_test_skip_form(ByteSlice bytes, u64* offset, u32 form)
{
    u64 size = 0;
    switch (form)
    {
    case DWARF_TEST_FORM_ADDR:
    case DWARF_TEST_FORM_DATA8:
        size = 8;
        break;
    case DWARF_TEST_FORM_DATA4:
    case DWARF_TEST_FORM_STRP:
    case DWARF_TEST_FORM_SEC_OFFSET:
    case DWARF_TEST_FORM_REF4:
        size = 4;
        break;
    case DWARF_TEST_FORM_DATA2:
        size = 2;
        break;
    case DWARF_TEST_FORM_DATA1:
        size = 1;
        break;
    case DWARF_TEST_FORM_UDATA:
    {
        u64 ignored;
        return dwarf_test_read_uleb128(bytes, offset, &ignored);
    }
    case DWARF_TEST_FORM_EXPRLOC:
    {
        u64 length;
        if (!dwarf_test_read_uleb128(bytes, offset, &length) || length > bytes.length - *offset)
        {
            return false;
        }
        *offset += length;
        return true;
    }
    default:
        return false;
    }
    if (size > bytes.length - *offset)
    {
        return false;
    }
    *offset += size;
    return true;
}

typedef struct DwarfTestBaseDie DwarfTestBaseDie;
struct DwarfTestBaseDie
{
    u64 byte_size;
    u64 bit_size;
    u8 encoding;
    bool has_bit_size;
    u8 reserved[6];
};

BUSTER_GLOBAL_LOCAL bool dwarf_test_read_base_die(ByteSlice info, ByteSlice abbrev_bytes, u64* offset, u32 expected_abbrev,
                                                    DwarfTestBaseDie* result)
{
    u64 number;
    if (!dwarf_test_read_uleb128(info, offset, &number) || number != expected_abbrev)
    {
        return false;
    }
    DwarfTestAbbrev abbrev;
    if (!dwarf_test_find_abbrev(abbrev_bytes, (u32)number, &abbrev) || abbrev.tag != DWARF_TEST_TAG_BASE_TYPE || abbrev.children)
    {
        return false;
    }
    DwarfTestBaseDie candidate = {0};
    bool have_byte_size = false;
    bool have_encoding = false;
    for (u32 index = 0; index < abbrev.attribute_count; index += 1)
    {
        u32 attribute = abbrev.attributes[index];
        u32 form = abbrev.forms[index];
        if (attribute == DWARF_TEST_AT_BYTE_SIZE)
        {
            if (form != DWARF_TEST_FORM_DATA8 || *offset > info.length || sizeof(candidate.byte_size) > info.length - *offset)
            {
                return false;
            }
            memcpy(&candidate.byte_size, info.pointer + *offset, sizeof(candidate.byte_size));
            *offset += sizeof(candidate.byte_size);
            have_byte_size = true;
        }
        else if (attribute == DWARF_TEST_AT_BIT_SIZE)
        {
            if (form != DWARF_TEST_FORM_DATA8 || *offset > info.length || sizeof(candidate.bit_size) > info.length - *offset)
            {
                return false;
            }
            memcpy(&candidate.bit_size, info.pointer + *offset, sizeof(candidate.bit_size));
            *offset += sizeof(candidate.bit_size);
            candidate.has_bit_size = true;
        }
        else if (attribute == DWARF_TEST_AT_ENCODING)
        {
            if (form != DWARF_TEST_FORM_DATA1 || *offset >= info.length)
            {
                return false;
            }
            candidate.encoding = info.pointer[(*offset)++];
            have_encoding = true;
        }
        else if (!dwarf_test_skip_form(info, offset, form))
        {
            return false;
        }
    }
    if (!have_byte_size || !have_encoding)
    {
        return false;
    }
    *result = candidate;
    return true;
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
    if (version != DWARF_TEST_VERSION || (u64)unit_length + 4 > debug_line.length)
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
            if (extended == DWARF_TEST_LNE_END_SEQUENCE)
            {
                emit_row = true;
                end_sequence = true;
            }
            else if (extended == DWARF_TEST_LNE_SET_ADDRESS && length == 1 + DWARF_TEST_ADDRESS_SIZE)
            {
                memcpy(&state.address, debug_line.pointer + offset + 1, sizeof(state.address));
            }
            offset += length;
        }
        else if (opcode == DWARF_TEST_LNS_COPY)
        {
            emit_row = true;
        }
        else if (opcode == DWARF_TEST_LNS_ADVANCE_PC)
        {
            u64 advance;
            if (!dwarf_test_read_uleb128(debug_line, &offset, &advance))
            {
                return false;
            }
            state.address += advance * minimum_instruction_length;
        }
        else if (opcode == DWARF_TEST_LNS_ADVANCE_LINE)
        {
            s64 delta;
            if (!dwarf_test_read_sleb128(debug_line, &offset, &delta))
            {
                return false;
            }
            state.line = (u32)((s64)state.line + delta);
        }
        else if (opcode == DWARF_TEST_LNS_SET_FILE)
        {
            u64 file;
            if (!dwarf_test_read_uleb128(debug_line, &offset, &file))
            {
                return false;
            }
            state.file = (u32)file;
        }
        else if (opcode == DWARF_TEST_LNS_SET_COLUMN)
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


// Apply the same text-base relocation a linker applies, then interpret lists
// under DWARF v4's CU-relative rule. Distinct module bases exercise the
// concatenated-object case that a relocatable object at base zero cannot show.
BUSTER_GLOBAL_LOCAL UnitTestResult dwarf_test_list_bases(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 path = S8("bases.c");
    DebugType types[] = {
        {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4, .is_signed = true},
        {.kind = DEBUG_TYPE_FUNCTION, .return_type = 0},
    };
    DebugLocationRange location = {.start = 0x34, .end = 0x48, .location = {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -8}};
    DebugVariable variable = {.name = S8("local"), .type = 0, .kind = DEBUG_VARIABLE_LOCAL, .locations = &location, .location_count = 1};
    DebugVariableId variable_id = 0;
    DebugScope scopes[] = {
        {.kind = DEBUG_SCOPE_FUNCTION, .parent = DEBUG_SCOPE_INVALID, .start = 0x30, .end = 0x50, .variables = &variable_id, .variable_count = 1},
        {.kind = DEBUG_SCOPE_LEXICAL, .parent = 0, .start = 0x38, .end = 0x48},
    };
    DebugFunction function = {.name = S8("offset_function"), .type = 1, .scope = 0, .code_offset = 0x30, .code_size = 0x20};
    DebugModel model = {.types = types, .type_count = 2, .functions = &function, .function_count = 1,
        .scopes = scopes, .scope_count = 2, .variables = &variable, .variable_count = 1, .valid = true};
    u64 bases[] = {0x401000, 0x403000};
    for (u32 module = 0; module < BUSTER_ARRAY_LENGTH(bases); module += 1)
    {
        DwarfResult built = dwarf_build(arguments->arena, (DwarfInput){.model = &model, .file_paths = &path, .file_count = 1,
            .producer = S8("buster"), .comp_dir = S8("."), .code_size = 0x50, .target = {.cpu_arch = CPU_ARCH_X86_64}});
        BUSTER_TEST(arguments, built.valid);
        if (!built.valid)
        {
            continue;
        }
        for (u32 index = 0; index < built.relocation_count; index += 1)
        {
            DwarfRelocation relocation = built.relocations[index];
            if (relocation.address)
            {
                ByteSlice bytes = built.sections[relocation.section];
                BUSTER_TEST(arguments, relocation.offset + 8 <= bytes.length);
                if (relocation.offset + 8 <= bytes.length)
                {
                    u64 value = bases[module] + (u64)relocation.addend;
                    memcpy(bytes.pointer + relocation.offset, &value, sizeof(value));
                }
            }
        }
        ByteSlice info = built.sections[DWARF_SECTION_INFO];
        u64 cursor = 11;
        u64 abbreviation = 0;
        DwarfTestAbbrev cu = {0};
        bool valid = dwarf_test_read_uleb128(info, &cursor, &abbreviation) &&
                     dwarf_test_find_abbrev(built.sections[DWARF_SECTION_ABBREV], (u32)abbreviation, &cu);
        u64 low_pc = 0;
        for (u32 index = 0; valid && index < cu.attribute_count; index += 1)
        {
            if (cu.attributes[index] == 0x11 && cu.forms[index] == DWARF_TEST_FORM_ADDR && cursor + 8 <= info.length)
            {
                memcpy(&low_pc, info.pointer + cursor, sizeof(low_pc));
            }
            valid = dwarf_test_skip_form(info, &cursor, cu.forms[index]);
        }
        BUSTER_TEST(arguments, valid && low_pc == bases[module]);
        ByteSlice ranges = built.sections[DWARF_SECTION_RANGES];
        ByteSlice locations = built.sections[DWARF_SECTION_LOC];
        BUSTER_TEST(arguments, ranges.length == 64 && locations.length >= 34);
        if (ranges.length == 64 && locations.length >= 34)
        {
            u64 endpoints[8];
            memcpy(endpoints, ranges.pointer, sizeof(endpoints));
            BUSTER_TEST(arguments, low_pc + endpoints[0] == bases[module] + 0x30 && low_pc + endpoints[1] == bases[module] + 0x50);
            BUSTER_TEST(arguments, !endpoints[2] && !endpoints[3]);
            BUSTER_TEST(arguments, low_pc + endpoints[4] == bases[module] + 0x38 && low_pc + endpoints[5] == bases[module] + 0x48);
            BUSTER_TEST(arguments, !endpoints[6] && !endpoints[7]);
            memcpy(endpoints, locations.pointer, 16);
            BUSTER_TEST(arguments, low_pc + endpoints[0] == bases[module] + 0x34 && low_pc + endpoints[1] == bases[module] + 0x48);
        }
    }
    return result;
}

// Array bounds and bit-field geometry, checked against the DWARF 4 encodings
// rather than against dwarf.c's own names (#1440): bounds live on a
// DW_TAG_subrange_type child (5.5.2, 5.11) -- written on the array itself
// they were invisible, and lldb printed every array as {} -- and a bit-field
// names its bits from the record's start with DW_AT_data_bit_offset and
// DW_AT_bit_size (5.5.6) -- without them lldb read the whole declared type.
// The expected numbers are the model's; the model's layout is checked against
// Clang by record_layout_tests.
BUSTER_GLOBAL_LOCAL UnitTestResult dwarf_test_array_and_bit_field_geometry(UnitTestArguments* arguments)
{
    enum
    {
        DWARF_SPEC_TAG_ARRAY_TYPE = 0x01,
        DWARF_SPEC_TAG_MEMBER = 0x0d,
        DWARF_SPEC_TAG_SUBRANGE_TYPE = 0x21,
        DWARF_SPEC_AT_NAME = 0x03,
        DWARF_SPEC_AT_BIT_SIZE = 0x0d,
        DWARF_SPEC_AT_UPPER_BOUND = 0x2f,
        DWARF_SPEC_AT_COUNT = 0x37,
        DWARF_SPEC_AT_DATA_MEMBER_LOCATION = 0x38,
        DWARF_SPEC_AT_DATA_BIT_OFFSET = 0x6b,
    };
    UnitTestResult result = {0};
    String8 path = S8("geometry.c");
    DebugTypeField fields[] = {
        {.name = S8("a"), .type = 1, .offset = 0},
        {.name = S8("b"), .type = 0, .offset = 0, .bit_offset = 8, .bit_width = 3, .is_bit_field = true},
        {.name = S8("c"), .type = 0, .offset = 4, .bit_offset = 5, .bit_width = 13, .is_bit_field = true},
    };
    DebugType types[] = {
        {.kind = DEBUG_TYPE_BASE, .name = S8("unsigned int"), .size = 4},
        {.kind = DEBUG_TYPE_BASE, .name = S8("char"), .size = 1, .is_signed = true},
        {.kind = DEBUG_TYPE_ARRAY, .element_type = 0, .element_count = 7, .size = 28},
        {.kind = DEBUG_TYPE_ARRAY, .element_type = 2, .element_count = 3, .size = 84},
        {.kind = DEBUG_TYPE_STRUCT, .name = S8("geometry"), .size = 8, .fields = fields, .field_count = BUSTER_ARRAY_LENGTH(fields)},
        {.kind = DEBUG_TYPE_FUNCTION, .return_type = 0},
    };
    DebugFunction function = {.name = S8("geometry_function"), .type = 5, .scope = DEBUG_SCOPE_INVALID, .code_size = 0x10};
    DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .functions = &function, .function_count = 1, .valid = true};
    DwarfResult built = dwarf_build(arguments->arena, (DwarfInput){.model = &model, .file_paths = &path, .file_count = 1,
        .producer = S8("buster"), .comp_dir = S8("."), .code_size = 0x10, .target = {.cpu_arch = CPU_ARCH_X86_64}});
    if (BUSTER_REQUIRE(arguments, built.valid))
    {
        ByteSlice info = built.sections[DWARF_SECTION_INFO];
        ByteSlice abbreviations = built.sections[DWARF_SECTION_ABBREV];
        ByteSlice strings = built.sections[DWARF_SECTION_STR];
        // A DW_FORM_strp slot is written as zero with its .debug_str offset
        // in a relocation, as a linker would resolve it.
        for (u32 index = 0; index < built.relocation_count; index += 1)
        {
            DwarfRelocation relocation = built.relocations[index];
            if (!relocation.address && relocation.section == DWARF_SECTION_INFO && relocation.target == DWARF_SECTION_STR &&
                relocation.offset + 4 <= info.length)
            {
                u32 string_offset = (u32)relocation.addend;
                memcpy(info.pointer + relocation.offset, &string_offset, sizeof(string_offset));
            }
        }
        u64 cursor = 11;
        u32 arrays_with_children = 0;
        u32 arrays_with_upper_bound = 0;
        u64 counts[4] = {0};
        u32 count_total = 0;
        bool b_checked = false;
        bool c_checked = false;
        bool valid = true;
        while (valid && cursor < info.length)
        {
            u64 number = 0;
            valid = dwarf_test_read_uleb128(info, &cursor, &number);
            DwarfTestAbbrev abbreviation = {0};
            if (!valid || !number)
            {
                continue;
            }
            valid = dwarf_test_find_abbrev(abbreviations, (u32)number, &abbreviation);
            String8 name = {0};
            u64 data_bit_offset = UINT64_MAX;
            u64 bit_size = UINT64_MAX;
            u64 count = UINT64_MAX;
            bool member_location = false;
            for (u32 index = 0; valid && index < abbreviation.attribute_count; index += 1)
            {
                u32 attribute = abbreviation.attributes[index];
                u32 form = abbreviation.forms[index];
                u64 value = 0;
                if (form == DWARF_TEST_FORM_UDATA)
                {
                    valid = dwarf_test_read_uleb128(info, &cursor, &value);
                }
                else if (form == DWARF_TEST_FORM_STRP && cursor + 4 <= info.length)
                {
                    u32 string_offset = 0;
                    memcpy(&string_offset, info.pointer + cursor, sizeof(string_offset));
                    cursor += 4;
                    value = string_offset;
                    u64 end = string_offset;
                    while (end < strings.length && strings.pointer[end])
                    {
                        end += 1;
                    }
                    name = string_offset < strings.length ? (String8){.pointer = (char8*)strings.pointer + string_offset, .length = end - string_offset}
                                                          : (String8){0};
                }
                else
                {
                    valid = dwarf_test_skip_form(info, &cursor, form);
                }
                data_bit_offset = attribute == DWARF_SPEC_AT_DATA_BIT_OFFSET ? value : data_bit_offset;
                bit_size = attribute == DWARF_SPEC_AT_BIT_SIZE ? value : bit_size;
                count = attribute == DWARF_SPEC_AT_COUNT ? value : count;
                member_location |= attribute == DWARF_SPEC_AT_DATA_MEMBER_LOCATION;
                arrays_with_upper_bound += abbreviation.tag == DWARF_SPEC_TAG_ARRAY_TYPE && attribute == DWARF_SPEC_AT_UPPER_BOUND;
            }
            arrays_with_children += abbreviation.tag == DWARF_SPEC_TAG_ARRAY_TYPE && abbreviation.children;
            if (abbreviation.tag == DWARF_SPEC_TAG_SUBRANGE_TYPE && count_total < BUSTER_ARRAY_LENGTH(counts))
            {
                counts[count_total++] = count;
            }
            if (abbreviation.tag == DWARF_SPEC_TAG_MEMBER && string_equal(name, S8("b")))
            {
                b_checked = data_bit_offset == 8 && bit_size == 3 && !member_location;
            }
            if (abbreviation.tag == DWARF_SPEC_TAG_MEMBER && string_equal(name, S8("c")))
            {
                c_checked = data_bit_offset == 4 * 8 + 5 && bit_size == 13 && !member_location;
            }
        }
        BUSTER_TEST(arguments, valid);
        BUSTER_TEST(arguments, arrays_with_children == 2 && arrays_with_upper_bound == 0);
        BUSTER_TEST(arguments, count_total == 2 && counts[0] == 7 && counts[1] == 3);
        BUSTER_TEST(arguments, b_checked && c_checked);
    }
    return result;
}

// Read the emitted DIE graph, not the writer's abbreviation numbers: the
// volatile member and pointer target must both reach the same qualifier DIE.
BUSTER_GLOBAL_LOCAL UnitTestResult dwarf_test_volatile_type_reference(UnitTestArguments* arguments)
{
    enum {TAG_BASE = 0x24, TAG_POINTER = 0x0f, TAG_MEMBER = 0x0d, TAG_CONST = 0x26, TAG_VOLATILE = 0x35, AT_TYPE = 0x49};
    UnitTestResult result = {0};
    DebugTypeField field = {.name = S8("counter"), .type = 1};
    DebugType types[] = {
        {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4, .is_signed = true},
        {.kind = DEBUG_TYPE_QUALIFIED, .unqualified_type = 0, .is_volatile = true},
        {.kind = DEBUG_TYPE_POINTER, .element_type = 1, .size = 8},
        {.kind = DEBUG_TYPE_STRUCT, .name = S8("holder"), .size = 4, .fields = &field, .field_count = 1},
        {.kind = DEBUG_TYPE_QUALIFIED, .unqualified_type = 0, .is_const = true},
    };
    DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .valid = true};
    String8 path = S8("volatile.c");
    DwarfResult built = dwarf_build(arguments->arena, (DwarfInput){.model = &model, .file_paths = &path, .file_count = 1,
        .producer = S8("buster"), .comp_dir = S8("."), .target = {.cpu_arch = CPU_ARCH_X86_64}});
    if (BUSTER_REQUIRE(arguments, built.valid))
    {
        ByteSlice info = built.sections[DWARF_SECTION_INFO];
        ByteSlice abbreviations = built.sections[DWARF_SECTION_ABBREV];
        u64 cursor = 11;
        u32 base_offset = 0;
        u32 volatile_offset = 0;
        u32 volatile_target = 0;
        u32 pointer_target = 0;
        u32 member_target = 0;
        u32 const_target = 0;
        u32 base_count = 0;
        u32 volatile_count = 0;
        u32 const_count = 0;
        bool valid = true;
        while (valid && cursor < info.length)
        {
            u32 die_offset = (u32)cursor;
            u64 number = 0;
            valid = dwarf_test_read_uleb128(info, &cursor, &number);
            if (valid && number)
            {
                DwarfTestAbbrev abbreviation = {0};
                valid = dwarf_test_find_abbrev(abbreviations, (u32)number, &abbreviation);
                u32 target = 0;
                for (u32 index = 0; valid && index < abbreviation.attribute_count; index += 1)
                {
                    if (abbreviation.attributes[index] == AT_TYPE && abbreviation.forms[index] == DWARF_TEST_FORM_REF4 &&
                        cursor + sizeof(target) <= info.length)
                    {
                        memcpy(&target, info.pointer + cursor, sizeof(target));
                    }
                    valid = dwarf_test_skip_form(info, &cursor, abbreviation.forms[index]);
                }
                if (abbreviation.tag == TAG_BASE)
                {
                    base_offset = die_offset;
                    base_count += 1;
                }
                else if (abbreviation.tag == TAG_VOLATILE)
                {
                    volatile_offset = die_offset;
                    volatile_target = target;
                    volatile_count += 1;
                }
                else if (abbreviation.tag == TAG_CONST)
                {
                    const_target = target;
                    const_count += 1;
                }
                else if (abbreviation.tag == TAG_POINTER)
                {
                    pointer_target = target;
                }
                else if (abbreviation.tag == TAG_MEMBER)
                {
                    member_target = target;
                }
            }
        }
        BUSTER_TEST(arguments, valid && cursor == info.length);
        BUSTER_TEST(arguments, base_count == 1 && volatile_count == 1 && const_count == 1);
        BUSTER_TEST(arguments, base_offset && volatile_offset && volatile_target == base_offset && const_target == base_offset);
        BUSTER_TEST(arguments, pointer_target == volatile_offset && member_target == volatile_offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult dwarf_test_global_linkage(UnitTestArguments* arguments)
{
    enum {TEST_TAG_VARIABLE = 0x34, TEST_AT_NAME = 0x03, TEST_AT_EXTERNAL = 0x3f};
    UnitTestResult result = {0};
    String8 path = S8("linkage.c");
    DebugType type = {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4};
    DebugVariable variables[] = {
        {.name = S8("static_hidden"), .linkage_name = S8("static_hidden"), .symbol = {.value = 4},
         .type = 0, .kind = DEBUG_VARIABLE_GLOBAL, .is_internal = true},
        {.name = S8("public_data"), .linkage_name = S8("public_data"), .symbol = {.value = 5},
         .type = 0, .kind = DEBUG_VARIABLE_GLOBAL},
    };
    DebugModel model = {.types = &type, .type_count = 1, .variables = variables, .variable_count = BUSTER_ARRAY_LENGTH(variables), .valid = true};
    DwarfResult built = dwarf_build(arguments->arena, (DwarfInput){.model = &model, .file_paths = &path, .file_count = 1,
        .producer = S8("buster"), .comp_dir = S8("."), .target = {.cpu_arch = CPU_ARCH_X86_64}});
    if (BUSTER_REQUIRE(arguments, built.valid))
    {
        ByteSlice info = built.sections[DWARF_SECTION_INFO];
        ByteSlice abbreviations = built.sections[DWARF_SECTION_ABBREV];
        ByteSlice strings = built.sections[DWARF_SECTION_STR];
        for (u32 index = 0; index < built.relocation_count; index += 1)
        {
            DwarfRelocation relocation = built.relocations[index];
            if (!relocation.address && relocation.section == DWARF_SECTION_INFO && relocation.target == DWARF_SECTION_STR &&
                relocation.offset + 4 <= info.length)
            {
                u32 string_offset = (u32)relocation.addend;
                memcpy(info.pointer + relocation.offset, &string_offset, sizeof(string_offset));
            }
        }
        u64 cursor = 11;
        u32 static_count = 0;
        u32 public_count = 0;
        bool valid = true;
        while (valid && cursor < info.length)
        {
            u64 number = 0;
            valid = dwarf_test_read_uleb128(info, &cursor, &number);
            if (valid && number)
            {
                DwarfTestAbbrev abbreviation = {0};
                valid = dwarf_test_find_abbrev(abbreviations, (u32)number, &abbreviation);
                bool external = false;
                u8 external_value = 0;
                String8 name = {0};
                for (u32 index = 0; valid && index < abbreviation.attribute_count; index += 1)
                {
                    if (abbreviation.attributes[index] == TEST_AT_NAME && abbreviation.forms[index] == DWARF_TEST_FORM_STRP &&
                        cursor + 4 <= info.length)
                    {
                        u32 offset = 0;
                        memcpy(&offset, info.pointer + cursor, sizeof(offset));
                        u64 end = offset;
                        while (end < strings.length && strings.pointer[end]) end += 1;
                        if (offset < strings.length && end < strings.length)
                        {
                            name = (String8){.pointer = (char8*)strings.pointer + offset, .length = end - offset};
                        }
                    }
                    if (abbreviation.attributes[index] == TEST_AT_EXTERNAL)
                    {
                        external = true;
                        if (abbreviation.forms[index] == DWARF_TEST_FORM_DATA1 && cursor < info.length)
                        {
                            external_value = info.pointer[cursor];
                        }
                        else
                        {
                            valid = false;
                        }
                    }
                    valid = valid && dwarf_test_skip_form(info, &cursor, abbreviation.forms[index]);
                }
                if (abbreviation.tag == TEST_TAG_VARIABLE)
                {
                    static_count += string_equal(name, S8("static_hidden")) && !external;
                    public_count += string_equal(name, S8("public_data")) && external && external_value == 1;
                }
            }
        }
        BUSTER_TEST(arguments, valid && cursor == info.length && static_count == 1 && public_count == 1);
        u32 addresses = 0;
        for (u32 index = 0; index < built.relocation_count; index += 1)
        {
            addresses += built.relocations[index].symbol_address &&
                         (built.relocations[index].symbol.value == 4 || built.relocations[index].symbol.value == 5);
        }
        BUSTER_TEST(arguments, addresses == 2);
    }
    return result;
}

UnitTestResult dwarf_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = dwarf_test_list_bases(arguments);
    UnitTestResult linkage = dwarf_test_global_linkage(arguments);
    result.test_count += linkage.test_count;
    result.succeeded_test_count += linkage.succeeded_test_count;
    UnitTestResult geometry = dwarf_test_array_and_bit_field_geometry(arguments);
    result.succeeded_test_count += geometry.succeeded_test_count;
    result.test_count += geometry.test_count;
    UnitTestResult volatile_types = dwarf_test_volatile_type_reference(arguments);
    result.succeeded_test_count += volatile_types.succeeded_test_count;
    result.test_count += volatile_types.test_count;
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
    if (built.valid)
    {
        u32 info_unit_length;
        memcpy(&info_unit_length, built.sections[DWARF_SECTION_INFO].pointer, sizeof(info_unit_length));
        BUSTER_TEST(arguments, (u64)info_unit_length + 4 == built.sections[DWARF_SECTION_INFO].length);
        u16 info_version;
        memcpy(&info_version, built.sections[DWARF_SECTION_INFO].pointer + 4, sizeof(info_version));
        BUSTER_TEST(arguments, info_version == DWARF_TEST_VERSION);
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

        DwarfInput empty_optional_strings = input;
        empty_optional_strings.producer = (String8){0};
        empty_optional_strings.comp_dir = (String8){0};
        BUSTER_TEST(arguments, dwarf_build(arguments->arena, empty_optional_strings).valid);

        String8 invalid_file = {.length = 1};
        DwarfInput invalid_string = input;
        invalid_string.file_paths = &invalid_file;
        invalid_string.file_count = 1;
        invalid_string.function_count = 0;
        invalid_string.line_count = 0;
        BUSTER_TEST(arguments, !dwarf_build(arguments->arena, invalid_string).valid);

        DebugTypeId dwarf_parameter_types[] = {0};
        DebugType dwarf_types[] = {
            {
                .name = S8("int"),
                .kind = DEBUG_TYPE_BASE,
                .size = 4,
                .alignment = 4,
                .bit_width = 32,
                .is_signed = true,
            },
            {
                .name = S8("debug_function"),
                .kind = DEBUG_TYPE_FUNCTION,
                .return_type = 0,
                .parameter_types = dwarf_parameter_types,
                .parameter_count = 1,
            },
        };
        DebugLocationPiece dwarf_pieces[] = {
            {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RAX, .size = 4},
            {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -24, .value_offset = 4, .size = 4},
        };
        DebugLocationRange dwarf_locations[] = {
            {.start = 0, .end = 8, .location = {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RCX}},
            {.start = 8, .end = 16, .location = {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -16}},
            {.start = 16, .end = 24, .location = {.kind = DEBUG_LOCATION_PIECEWISE, .pieces = dwarf_pieces, .piece_count = BUSTER_ARRAY_LENGTH(dwarf_pieces)}},
            {.start = 24, .end = 32, .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 7}},
        };
        DebugVariableId dwarf_variable_ids[] = {0};
        DebugVariable dwarf_variables[] = {
            {
                .name = S8("parameter"),
                .type = 0,
                .declaration = {.source = 0, .line = 2, .column = 3},
                .locations = dwarf_locations,
                .location_count = BUSTER_ARRAY_LENGTH(dwarf_locations),
                .kind = DEBUG_VARIABLE_PARAMETER,
            },
        };
        DebugScope dwarf_scopes[] = {
            {
                .parent = DEBUG_SCOPE_INVALID,
                .kind = DEBUG_SCOPE_FUNCTION,
                .start = 0,
                .end = 32,
                .variables = dwarf_variable_ids,
                .variable_count = BUSTER_ARRAY_LENGTH(dwarf_variable_ids),
            },
            {
                .parent = 0,
                .kind = DEBUG_SCOPE_LEXICAL,
                .start = 4,
                .end = 20,
            },
            {
                .parent = 1,
                .kind = DEBUG_SCOPE_LEXICAL,
                .start = 8,
                .end = 12,
            },
            {
                .parent = 0,
                .kind = DEBUG_SCOPE_LEXICAL,
                .start = 20,
                .end = 28,
            },
        };
        DebugFunction dwarf_functions[] = {
            {
                .name = S8("debug_function"),
                .declaration = {.source = 0, .line = 1},
                .symbol = {.value = 0},
                .type = 1,
                .scope = 0,
                .code_size = 32,
            },
        };
        DebugInlineSite dwarf_inline_sites[] = {
            {
                .function = dwarf_functions,
                .call_site = {.source = 0, .line = 9, .column = 2},
                .start = 4,
                .end = 20,
                .has_ranges = true,
            },
        };
        DebugModel dwarf_model = {
            .source_paths = files,
            .types = dwarf_types,
            .functions = dwarf_functions,
            .scopes = dwarf_scopes,
            .variables = dwarf_variables,
            .inline_sites = dwarf_inline_sites,
            .source_count = BUSTER_ARRAY_LENGTH(files),
            .type_count = BUSTER_ARRAY_LENGTH(dwarf_types),
            .function_count = BUSTER_ARRAY_LENGTH(dwarf_functions),
            .scope_count = BUSTER_ARRAY_LENGTH(dwarf_scopes),
            .variable_count = BUSTER_ARRAY_LENGTH(dwarf_variables),
            .inline_site_count = BUSTER_ARRAY_LENGTH(dwarf_inline_sites),
            .valid = true,
        };
        DwarfFunction dwarf_model_function = {
            .name = S8("debug_function"),
            .code_offset = 0,
            .code_size = 32,
            .file = 0,
            .line = 1,
        };
        DwarfLineEntry dwarf_model_line = {.code_offset = 0, .file = 0, .line = 1, .column = 1};
        DwarfInput dwarf_model_input = {
            .model = &dwarf_model,
            .target = (Target){.cpu_arch = CPU_ARCH_X86_64},
            .producer = S8("buster"),
            .comp_dir = S8("."),
            .file_paths = files,
            .functions = &dwarf_model_function,
            .lines = &dwarf_model_line,
            .code_size = 32,
            .file_count = BUSTER_ARRAY_LENGTH(files),
            .function_count = 1,
            .line_count = 1,
            .language = 0x000c,
        };
        DwarfResult model_built = dwarf_build(arguments->arena, dwarf_model_input);
        BUSTER_TEST(arguments, model_built.valid);
        BUSTER_TEST(arguments, model_built.sections[DWARF_SECTION_LOC].length > 16);
        BUSTER_TEST(arguments, model_built.sections[DWARF_SECTION_RANGES].length > 32);
        DwarfResult model_built_repeat = dwarf_build(arguments->arena, dwarf_model_input);
        bool model_outputs_match = model_built.valid && model_built_repeat.valid;
        for (u32 section_index = 0; model_outputs_match && section_index < DWARF_SECTION_COUNT; section_index += 1)
        {
            ByteSlice first = model_built.sections[section_index];
            ByteSlice second = model_built_repeat.sections[section_index];
            model_outputs_match = first.length == second.length && (!first.length || memcmp(first.pointer, second.pointer, first.length) == 0);
        }
        BUSTER_TEST(arguments, model_outputs_match);
        bool has_inline_abbrev = false;
        for (u64 byte_index = 0; byte_index < model_built.sections[DWARF_SECTION_ABBREV].length; byte_index += 1)
        {
            has_inline_abbrev |= model_built.sections[DWARF_SECTION_ABBREV].pointer[byte_index] == 25;
        }
        BUSTER_TEST(arguments, has_inline_abbrev);

        // Independent DWARF v4 bytes for the existing register/frame/piece/
        // constant fixture. A repeat build alone cannot prove byte stability.
        u8 const ordinary_locations[] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x90, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x91, 0x70, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x90, 0x00, 0x93, 0x04, 0x91, 0x68,
            0x93, 0x04, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x03, 0x00, 0x10, 0x07, 0x9f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        ByteSlice ordinary_loc = model_built.sections[DWARF_SECTION_LOC];
        BUSTER_TEST(arguments, ordinary_loc.length == sizeof(ordinary_locations) &&
                               memcmp(ordinary_loc.pointer, ordinary_locations, sizeof(ordinary_locations)) == 0);

        // Sixteen genuine changes for one variable exceed the old 160-byte
        // estimate. Decode each entry, including the uncovered one-byte gaps.
        DebugLocationRange many_locations[16];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(many_locations); index += 1)
        {
            many_locations[index] = (DebugLocationRange){
                .start = index * 2,
                .end = index * 2 + 1,
                .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = index},
            };
        }
        dwarf_variables[0].locations = many_locations;
        dwarf_variables[0].location_count = BUSTER_ARRAY_LENGTH(many_locations);
        DwarfResult many_built = dwarf_build(arguments->arena, dwarf_model_input);
        ByteSlice many_loc = many_built.sections[DWARF_SECTION_LOC];
        bool many_matches = many_built.valid && many_loc.length == 16 * 21 + 16;
        for (u32 index = 0; many_matches && index < BUSTER_ARRAY_LENGTH(many_locations); index += 1)
        {
            u64 start;
            u64 end;
            u16 length;
            u64 offset = (u64)index * 21;
            memcpy(&start, many_loc.pointer + offset, sizeof(start));
            memcpy(&end, many_loc.pointer + offset + 8, sizeof(end));
            memcpy(&length, many_loc.pointer + offset + 16, sizeof(length));
            u8 const expression[] = {0x10, (u8)index, 0x9f};
            many_matches = start == (u64)index * 2 && end == (u64)index * 2 + 1 && length == sizeof(expression) &&
                           memcmp(many_loc.pointer + offset + 18, expression, sizeof(expression)) == 0;
        }
        u8 const list_end[16] = {0};
        many_matches = many_matches && memcmp(many_loc.pointer + 16 * 21, list_end, sizeof(list_end)) == 0;
        BUSTER_TEST(arguments, many_matches);

        // Eight register entries fill the old allocation exactly. The ninth
        // then reaches its unchecked length patch at offset 160, isolating
        // the failed writes followed by an unbounded patch in the old writer.
        DebugLocationRange register_locations[9];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(register_locations); index += 1)
        {
            register_locations[index] = (DebugLocationRange){
                .start = index * 2,
                .end = index * 2 + 1,
                .location = {.kind = DEBUG_LOCATION_REGISTER,
                             .reg = (index & 1) ? DEBUG_REGISTER_X86_RAX : DEBUG_REGISTER_X86_RCX},
            };
        }
        dwarf_variables[0].locations = register_locations;
        dwarf_variables[0].location_count = BUSTER_ARRAY_LENGTH(register_locations);
        DwarfResult register_built = dwarf_build(arguments->arena, dwarf_model_input);
        BUSTER_TEST(arguments, register_built.valid && register_built.sections[DWARF_SECTION_LOC].length == 9 * 20 + 16);
        dwarf_variables[0].locations = many_locations;
        dwarf_variables[0].location_count = BUSTER_ARRAY_LENGTH(many_locations);

        // A repeated ID and a descendant reference are separate serialized
        // lists. Their relocation addends must follow the actual scope walk.
        DebugVariableId repeated_ids[] = {0, 0};
        dwarf_scopes[0].variables = repeated_ids;
        dwarf_scopes[0].variable_count = BUSTER_ARRAY_LENGTH(repeated_ids);
        dwarf_scopes[1].variables = dwarf_variable_ids;
        dwarf_scopes[1].variable_count = 1;
        DwarfResult repeated_built = dwarf_build(arguments->arena, dwarf_model_input);
        ByteSlice repeated_loc = repeated_built.sections[DWARF_SECTION_LOC];
        bool repeated_matches = many_matches && repeated_built.valid && repeated_loc.length == many_loc.length * 3;
        for (u32 index = 0; repeated_matches && index < 3; index += 1)
        {
            repeated_matches = memcmp(repeated_loc.pointer + many_loc.length * index, many_loc.pointer, many_loc.length) == 0;
        }
        u32 location_relocations = 0;
        for (u32 index = 0; repeated_matches && index < repeated_built.relocation_count; index += 1)
        {
            DwarfRelocation relocation = repeated_built.relocations[index];
            if (relocation.section == DWARF_SECTION_INFO && relocation.target == DWARF_SECTION_LOC)
            {
                repeated_matches = relocation.addend == (s64)(many_loc.length * location_relocations);
                location_relocations += 1;
            }
        }
        BUSTER_TEST(arguments, repeated_matches && location_relocations == 3);

        // Two functions sharing the same bounded scope tree serialize it
        // twice. Keep this topology small enough to isolate location storage.
        DebugFunction shared_functions[] = {dwarf_functions[0], dwarf_functions[0]};
        dwarf_model.functions = shared_functions;
        dwarf_model.function_count = BUSTER_ARRAY_LENGTH(shared_functions);
        dwarf_model.scope_count = 2;
        dwarf_model.inline_site_count = 0;
        DwarfResult shared_built = dwarf_build(arguments->arena, dwarf_model_input);
        ByteSlice shared_loc = shared_built.sections[DWARF_SECTION_LOC];
        bool shared_matches = repeated_matches && shared_built.valid && shared_loc.length == many_loc.length * 6;
        for (u32 index = 0; shared_matches && index < 6; index += 1)
        {
            shared_matches = memcmp(shared_loc.pointer + many_loc.length * index, many_loc.pointer, many_loc.length) == 0;
        }
        BUSTER_TEST(arguments, shared_matches);
        dwarf_model.functions = dwarf_functions;
        dwarf_model.function_count = BUSTER_ARRAY_LENGTH(dwarf_functions);
        dwarf_model.scope_count = BUSTER_ARRAY_LENGTH(dwarf_scopes);
        dwarf_model.inline_site_count = BUSTER_ARRAY_LENGTH(dwarf_inline_sites);
        dwarf_scopes[0].variables = dwarf_variable_ids;
        dwarf_scopes[0].variable_count = 1;
        dwarf_scopes[1].variables = 0;
        dwarf_scopes[1].variable_count = 0;

        DebugLocationRange skipped_locations[] = {
            {.start = 0, .end = 8, .location = {.kind = DEBUG_LOCATION_UNAVAILABLE}},
            {.start = 8, .end = 8, .location = {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RAX}},
            {.start = 16, .end = 8, .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 7}},
        };
        dwarf_variables[0].locations = skipped_locations;
        dwarf_variables[0].location_count = BUSTER_ARRAY_LENGTH(skipped_locations);
        DwarfResult skipped_built = dwarf_build(arguments->arena, dwarf_model_input);
        ByteSlice skipped_loc = skipped_built.sections[DWARF_SECTION_LOC];
        BUSTER_TEST(arguments, skipped_built.valid && skipped_loc.length == sizeof(list_end) &&
                               memcmp(skipped_loc.pointer, list_end, sizeof(list_end)) == 0);
        dwarf_variables[0].locations = 0;
        dwarf_variables[0].location_count = 0;
        DwarfResult empty_built = dwarf_build(arguments->arena, dwarf_model_input);
        ByteSlice empty_loc = empty_built.sections[DWARF_SECTION_LOC];
        BUSTER_TEST(arguments, empty_built.valid && empty_loc.length == sizeof(list_end) &&
                               memcmp(empty_loc.pointer, list_end, sizeof(list_end)) == 0);

        DebugLocationRange empty_piece_location = {
            .start = 0,
            .end = 32,
            .location = {.kind = DEBUG_LOCATION_PIECEWISE},
        };
        dwarf_variables[0].locations = &empty_piece_location;
        dwarf_variables[0].location_count = 1;
        DwarfResult empty_piece_built = dwarf_build(arguments->arena, dwarf_model_input);
        ByteSlice empty_piece_loc = empty_piece_built.sections[DWARF_SECTION_LOC];
        BUSTER_TEST(arguments, empty_piece_built.valid && empty_piece_loc.length == 18 + sizeof(list_end) &&
                               empty_piece_loc.pointer[16] == 0 && empty_piece_loc.pointer[17] == 0 &&
                               memcmp(empty_piece_loc.pointer + 18, list_end, sizeof(list_end)) == 0);

        // Every constant/piece pair is five literal bytes. The largest
        // representable expression succeeds; one additional operand byte
        // refuses the model instead of truncating its two-byte length.
        u32 const boundary_piece_count = UINT16_MAX / 5;
        DebugLocationPiece* boundary_pieces = arena_allocate(arguments->arena, DebugLocationPiece, boundary_piece_count);
        for (u32 index = 0; index < boundary_piece_count; index += 1)
        {
            boundary_pieces[index] = (DebugLocationPiece){
                .kind = DEBUG_LOCATION_CONSTANT,
                .constant = 0,
                .value_offset = index * 4,
                .size = 4,
            };
        }
        DebugType boundary_types[] = {
            dwarf_types[0],
            dwarf_types[1],
            {.kind = DEBUG_TYPE_ARRAY, .element_type = 0, .element_count = boundary_piece_count, .size = (u64)boundary_piece_count * 4},
        };
        dwarf_model.types = boundary_types;
        dwarf_model.type_count = BUSTER_ARRAY_LENGTH(boundary_types);
        dwarf_variables[0].type = 2;
        DebugLocationRange boundary_location = {
            .start = 0,
            .end = 32,
            .location = {.kind = DEBUG_LOCATION_PIECEWISE, .pieces = boundary_pieces, .piece_count = boundary_piece_count},
        };
        dwarf_variables[0].locations = &boundary_location;
        dwarf_variables[0].location_count = 1;
        DwarfResult boundary_built = dwarf_build(arguments->arena, dwarf_model_input);
        ByteSlice boundary_loc = boundary_built.sections[DWARF_SECTION_LOC];
        bool boundary_matches = boundary_built.valid && boundary_loc.length == 18 + UINT16_MAX + 16;
        u8 const piece_expression[] = {0x10, 0x00, 0x9f, 0x93, 0x04};
        for (u32 index = 0; boundary_matches && index < boundary_piece_count; index += 1)
        {
            boundary_matches = memcmp(boundary_loc.pointer + 18 + (u64)index * sizeof(piece_expression),
                                      piece_expression, sizeof(piece_expression)) == 0;
        }
        boundary_matches = boundary_matches && boundary_loc.pointer[16] == 0xff && boundary_loc.pointer[17] == 0xff &&
                           memcmp(boundary_loc.pointer + 18 + UINT16_MAX, list_end, sizeof(list_end)) == 0;
        BUSTER_TEST(arguments, boundary_matches);
        boundary_pieces[boundary_piece_count - 1].constant = 128;
        DwarfResult oversized_built = dwarf_build(arguments->arena, dwarf_model_input);
        bool oversized_empty = !oversized_built.valid && !oversized_built.relocation_count;
        for (u32 index = 0; oversized_empty && index < DWARF_SECTION_COUNT; index += 1)
        {
            oversized_empty = !oversized_built.sections[index].pointer && !oversized_built.sections[index].length;
        }
        BUSTER_TEST(arguments, oversized_empty);

        // An unreachable oversized variable contributes no location list.
        dwarf_scopes[0].variables = 0;
        dwarf_scopes[0].variable_count = 0;
        DwarfResult unreachable_built = dwarf_build(arguments->arena, dwarf_model_input);
        BUSTER_TEST(arguments, unreachable_built.valid && !unreachable_built.sections[DWARF_SECTION_LOC].length);
        dwarf_scopes[0].variables = dwarf_variable_ids;
        dwarf_scopes[0].variable_count = 1;
        dwarf_variables[0].kind = DEBUG_VARIABLE_GLOBAL;
        DwarfResult global_built = dwarf_build(arguments->arena, dwarf_model_input);
        BUSTER_TEST(arguments, global_built.valid && !global_built.sections[DWARF_SECTION_LOC].length);
        dwarf_variables[0].kind = DEBUG_VARIABLE_PARAMETER;
        dwarf_model.types = dwarf_types;
        dwarf_model.type_count = BUSTER_ARRAY_LENGTH(dwarf_types);
        dwarf_variables[0].type = 0;
        dwarf_variables[0].locations = dwarf_locations;
        dwarf_variables[0].location_count = BUSTER_ARRAY_LENGTH(dwarf_locations);


        // A SysV x87 long double has an 80-bit semantic value in a 16-byte
        // storage slot.  Decode the emitted abbreviation and DIEs instead of
        // merely searching for the value: the attribute must be standard
        // DW_AT_bit_size=80, while naturally sized f32/f64 retain abbreviation 2
        // and its original byte layout.  The C frontend's float/double/long
        // double spellings are covered alongside the fNN aliases.
        DebugType dwarf_float_types[] = {
            {
                .name = S8("f32"),
                .kind = DEBUG_TYPE_BASE,
                .size = 4,
                .alignment = 4,
                .bit_width = 32,
                .is_float = true,
            },
            {
                .name = S8("f64"),
                .kind = DEBUG_TYPE_BASE,
                .size = 8,
                .alignment = 8,
                .bit_width = 64,
                .is_float = true,
            },
            {
                .name = S8("f80"),
                .kind = DEBUG_TYPE_BASE,
                .size = 16,
                .alignment = 16,
                .bit_width = 80,
                .is_float = true,
            },
            {
                .name = S8("float"),
                .kind = DEBUG_TYPE_BASE,
                .size = 4,
                .alignment = 4,
                .bit_width = 32,
                .is_float = true,
            },
            {
                .name = S8("double"),
                .kind = DEBUG_TYPE_BASE,
                .size = 8,
                .alignment = 8,
                .bit_width = 64,
                .is_float = true,
            },
            {
                .name = S8("long double"),
                .kind = DEBUG_TYPE_BASE,
                .size = 16,
                .alignment = 16,
                .bit_width = 80,
                .is_float = true,
            },
            {
                .name = S8("_Float16"),
                .kind = DEBUG_TYPE_BASE,
                .size = 2,
                .alignment = 2,
                .bit_width = 16,
                .is_float = true,
            },
            {
                // Spelled like a float but structurally an integer (#2737).
                .name = S8("float_count"),
                .kind = DEBUG_TYPE_BASE,
                .size = 4,
                .alignment = 4,
                .bit_width = 32,
            },
        };
        DebugModel dwarf_float_model = {
            .source_paths = files,
            .types = dwarf_float_types,
            .source_count = BUSTER_ARRAY_LENGTH(files),
            .type_count = BUSTER_ARRAY_LENGTH(dwarf_float_types),
            .valid = true,
        };
        DwarfResult dwarf_float_built = dwarf_build(arguments->arena, (DwarfInput){
                                                                           .model = &dwarf_float_model,
                                                                           .target = (Target){.cpu_arch = CPU_ARCH_X86_64},
                                                                           .producer = S8("buster"),
                                                                           .comp_dir = S8("."),
                                                                           .file_paths = files,
                                                                           .code_size = 1,
                                                                           .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                           .language = 0x000c,
                                                                       });
        BUSTER_TEST(arguments, dwarf_float_built.valid);
        DwarfTestAbbrev f32_abbrev = {0};
        DwarfTestAbbrev f80_abbrev = {0};
        BUSTER_TEST(arguments, dwarf_test_find_abbrev(dwarf_float_built.sections[DWARF_SECTION_ABBREV], 2, &f32_abbrev));
        BUSTER_TEST(arguments, dwarf_test_find_abbrev(dwarf_float_built.sections[DWARF_SECTION_ABBREV], 26, &f80_abbrev));
        BUSTER_TEST(arguments, f32_abbrev.tag == DWARF_TEST_TAG_BASE_TYPE && !f32_abbrev.children && f32_abbrev.attribute_count == 5);
        BUSTER_TEST(arguments, f32_abbrev.attributes[0] == 0x03 && f32_abbrev.attributes[1] == DWARF_TEST_AT_BYTE_SIZE &&
                                 f32_abbrev.attributes[2] == DWARF_TEST_AT_ENCODING && f32_abbrev.attributes[3] == 0x3a && f32_abbrev.attributes[4] == 0x3b);
        BUSTER_TEST(arguments, f32_abbrev.forms[0] == DWARF_TEST_FORM_STRP && f32_abbrev.forms[1] == DWARF_TEST_FORM_DATA8 &&
                                 f32_abbrev.forms[2] == DWARF_TEST_FORM_DATA1 && f32_abbrev.forms[3] == DWARF_TEST_FORM_UDATA &&
                                 f32_abbrev.forms[4] == DWARF_TEST_FORM_UDATA);
        BUSTER_TEST(arguments, f80_abbrev.tag == DWARF_TEST_TAG_BASE_TYPE && !f80_abbrev.children && f80_abbrev.attribute_count == 6);
        BUSTER_TEST(arguments, f80_abbrev.attributes[1] == DWARF_TEST_AT_BYTE_SIZE && f80_abbrev.attributes[2] == DWARF_TEST_AT_BIT_SIZE &&
                                 f80_abbrev.attributes[3] == DWARF_TEST_AT_ENCODING);
        BUSTER_TEST(arguments, f80_abbrev.forms[1] == DWARF_TEST_FORM_DATA8 && f80_abbrev.forms[2] == DWARF_TEST_FORM_DATA8 &&
                                 f80_abbrev.forms[3] == DWARF_TEST_FORM_DATA1);
        if (dwarf_float_built.valid)
        {
            ByteSlice float_info = dwarf_float_built.sections[DWARF_SECTION_INFO];
            ByteSlice float_abbrev = dwarf_float_built.sections[DWARF_SECTION_ABBREV];
            u64 info_offset = 11;
            u64 cu_abbrev_number;
            bool cu_decoded = dwarf_test_read_uleb128(float_info, &info_offset, &cu_abbrev_number) && cu_abbrev_number == 1;
            DwarfTestAbbrev cu_abbrev = {0};
            cu_decoded = cu_decoded && dwarf_test_find_abbrev(float_abbrev, 1, &cu_abbrev);
            if (cu_decoded)
            {
                for (u32 attribute_index = 0; attribute_index < cu_abbrev.attribute_count; attribute_index += 1)
                {
                    cu_decoded = dwarf_test_skip_form(float_info, &info_offset, cu_abbrev.forms[attribute_index]) && cu_decoded;
                }
            }
            DwarfTestBaseDie f32_die = {0};
            DwarfTestBaseDie f64_die = {0};
            DwarfTestBaseDie f80_die = {0};
            DwarfTestBaseDie float_die = {0};
            DwarfTestBaseDie double_die = {0};
            DwarfTestBaseDie long_double_die = {0};
            cu_decoded = cu_decoded && dwarf_test_read_base_die(float_info, float_abbrev, &info_offset, 2, &f32_die);
            cu_decoded = cu_decoded && dwarf_test_read_base_die(float_info, float_abbrev, &info_offset, 2, &f64_die);
            cu_decoded = cu_decoded && dwarf_test_read_base_die(float_info, float_abbrev, &info_offset, 26, &f80_die);
            cu_decoded = cu_decoded && dwarf_test_read_base_die(float_info, float_abbrev, &info_offset, 2, &float_die);
            cu_decoded = cu_decoded && dwarf_test_read_base_die(float_info, float_abbrev, &info_offset, 2, &double_die);
            cu_decoded = cu_decoded && dwarf_test_read_base_die(float_info, float_abbrev, &info_offset, 26, &long_double_die);
            DwarfTestBaseDie half_die = {0};
            DwarfTestBaseDie spelled_float_die = {0};
            cu_decoded = cu_decoded && dwarf_test_read_base_die(float_info, float_abbrev, &info_offset, 2, &half_die);
            cu_decoded = cu_decoded && dwarf_test_read_base_die(float_info, float_abbrev, &info_offset, 2, &spelled_float_die);
            BUSTER_TEST(arguments, cu_decoded);
            BUSTER_TEST(arguments, f32_die.byte_size == 4 && f32_die.bit_size == 0 && !f32_die.has_bit_size && f32_die.encoding == 0x04);
            BUSTER_TEST(arguments, f64_die.byte_size == 8 && f64_die.bit_size == 0 && !f64_die.has_bit_size && f64_die.encoding == 0x04);
            BUSTER_TEST(arguments, f80_die.byte_size == 16 && f80_die.bit_size == 80 && f80_die.has_bit_size && f80_die.encoding == 0x04);
            BUSTER_TEST(arguments, float_die.byte_size == 4 && float_die.bit_size == 0 && !float_die.has_bit_size && float_die.encoding == 0x04);
            BUSTER_TEST(arguments, double_die.byte_size == 8 && double_die.bit_size == 0 && !double_die.has_bit_size && double_die.encoding == 0x04);
            BUSTER_TEST(arguments, long_double_die.byte_size == 16 && long_double_die.bit_size == 80 && long_double_die.has_bit_size && long_double_die.encoding == 0x04);
            // DW_ATE_float is 0x04 and DW_ATE_unsigned 0x07 (DWARF 5, table 7.11).
            BUSTER_TEST(arguments, half_die.byte_size == 2 && half_die.encoding == 0x04);
            BUSTER_TEST(arguments, spelled_float_die.byte_size == 4 && spelled_float_die.encoding == 0x07);
        }

        DebugType dwarf_narrow_float_types[] = {
            dwarf_float_types[0],
            dwarf_float_types[1],
        };
        DebugModel dwarf_narrow_float_model = dwarf_float_model;
        dwarf_narrow_float_model.types = dwarf_narrow_float_types;
        dwarf_narrow_float_model.type_count = BUSTER_ARRAY_LENGTH(dwarf_narrow_float_types);
        DwarfResult dwarf_narrow_float_a = dwarf_build(arguments->arena, (DwarfInput){
                                                                                .model = &dwarf_narrow_float_model,
                                                                                .target = (Target){.cpu_arch = CPU_ARCH_X86_64},
                                                                                .producer = S8("buster"),
                                                                                .comp_dir = S8("."),
                                                                                .file_paths = files,
                                                                                .code_size = 1,
                                                                                .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                                .language = 0x000c,
                                                                            });
        DwarfResult dwarf_narrow_float_b = dwarf_build(arguments->arena, (DwarfInput){
                                                                                .model = &dwarf_narrow_float_model,
                                                                                .target = (Target){.cpu_arch = CPU_ARCH_X86_64},
                                                                                .producer = S8("buster"),
                                                                                .comp_dir = S8("."),
                                                                                .file_paths = files,
                                                                                .code_size = 1,
                                                                                .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                                .language = 0x000c,
                                                                            });
        BUSTER_TEST(arguments, dwarf_narrow_float_a.valid && dwarf_narrow_float_b.valid);
        if (dwarf_narrow_float_a.valid && dwarf_narrow_float_b.valid)
        {
            ByteSlice a_abbrev = dwarf_narrow_float_a.sections[DWARF_SECTION_ABBREV];
            ByteSlice b_abbrev = dwarf_narrow_float_b.sections[DWARF_SECTION_ABBREV];
            ByteSlice a_info = dwarf_narrow_float_a.sections[DWARF_SECTION_INFO];
            ByteSlice b_info = dwarf_narrow_float_b.sections[DWARF_SECTION_INFO];
            BUSTER_TEST(arguments, a_abbrev.length == b_abbrev.length && memcmp(a_abbrev.pointer, b_abbrev.pointer, a_abbrev.length) == 0);
            BUSTER_TEST(arguments, a_info.length == b_info.length && memcmp(a_info.pointer, b_info.pointer, a_info.length) == 0);
            DwarfTestAbbrev absent_padded_abbrev = {0};
            BUSTER_TEST(arguments, !dwarf_test_find_abbrev(a_abbrev, 26, &absent_padded_abbrev));
        }

        // A bit width larger than the storage slot is inconsistent metadata, not
        // a padded value.  Keep the normal base-type abbreviation rather than
        // emitting an impossible DW_AT_bit_size.
        DebugType dwarf_inconsistent_float = {
            .name = S8("long double"),
            .kind = DEBUG_TYPE_BASE,
            .size = 16,
            .alignment = 16,
            .bit_width = 160,
            .is_float = true,
        };
        DebugModel dwarf_inconsistent_model = dwarf_narrow_float_model;
        dwarf_inconsistent_model.types = &dwarf_inconsistent_float;
        dwarf_inconsistent_model.type_count = 1;
        DwarfResult dwarf_inconsistent_built = dwarf_build(arguments->arena, (DwarfInput){
                                                                                     .model = &dwarf_inconsistent_model,
                                                                                     .target = (Target){.cpu_arch = CPU_ARCH_X86_64},
                                                                                     .producer = S8("buster"),
                                                                                     .comp_dir = S8("."),
                                                                                     .file_paths = files,
                                                                                     .code_size = 1,
                                                                                     .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                                     .language = 0x000c,
                                                                                 });
        BUSTER_TEST(arguments, dwarf_inconsistent_built.valid);
        if (dwarf_inconsistent_built.valid)
        {
            ByteSlice inconsistent_info = dwarf_inconsistent_built.sections[DWARF_SECTION_INFO];
            ByteSlice inconsistent_abbrev = dwarf_inconsistent_built.sections[DWARF_SECTION_ABBREV];
            DwarfTestAbbrev absent_padded_abbrev = {0};
            BUSTER_TEST(arguments, !dwarf_test_find_abbrev(inconsistent_abbrev, 26, &absent_padded_abbrev));
            u64 info_offset = 11;
            u64 cu_abbrev_number;
            bool decoded = dwarf_test_read_uleb128(inconsistent_info, &info_offset, &cu_abbrev_number) && cu_abbrev_number == 1;
            DwarfTestAbbrev cu_abbrev = {0};
            decoded = decoded && dwarf_test_find_abbrev(inconsistent_abbrev, 1, &cu_abbrev);
            if (decoded)
            {
                for (u32 attribute_index = 0; attribute_index < cu_abbrev.attribute_count; attribute_index += 1)
                {
                    decoded = dwarf_test_skip_form(inconsistent_info, &info_offset, cu_abbrev.forms[attribute_index]) && decoded;
                }
            }
            DwarfTestBaseDie inconsistent_die = {0};
            decoded = decoded && dwarf_test_read_base_die(inconsistent_info, inconsistent_abbrev, &info_offset, 2, &inconsistent_die);
            BUSTER_TEST(arguments, decoded && inconsistent_die.byte_size == 16 && !inconsistent_die.has_bit_size && inconsistent_die.encoding == 0x04);
        }

        DebugVariable dwarf_global = {
            .name = S8("global_value"),
            .linkage_name = S8("global_value"),
            .symbol = {.value = 7},
            .type = 0,
            .kind = DEBUG_VARIABLE_GLOBAL,
        };
        DebugModel dwarf_globals_only = dwarf_model;
        dwarf_globals_only.functions = 0;
        dwarf_globals_only.scopes = 0;
        dwarf_globals_only.variables = &dwarf_global;
        dwarf_globals_only.function_count = 0;
        dwarf_globals_only.scope_count = 0;
        dwarf_globals_only.variable_count = 1;
        dwarf_globals_only.inline_sites = 0;
        dwarf_globals_only.inline_site_count = 0;
        DwarfResult globals_built = dwarf_build(arguments->arena, (DwarfInput){
                                                                        .model = &dwarf_globals_only,
                                                                        .target = (Target){.cpu_arch = CPU_ARCH_X86_64},
                                                                        .producer = S8("buster"),
                                                                        .comp_dir = S8("."),
                                                                        .file_paths = files,
                                                                        .code_size = 1,
                                                                        .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                        .language = 0x000c,
                                                                    });
        BUSTER_TEST(arguments, globals_built.valid && globals_built.sections[DWARF_SECTION_INFO].length > 16);
        // The address relocation names the variable and carries its program
        // symbol, which the object writer resolves without the name.
        u32 global_address_relocations = 0;
        for (u32 index = 0; globals_built.valid && index < globals_built.relocation_count; index += 1)
        {
            DwarfRelocation relocation = globals_built.relocations[index];
            global_address_relocations += relocation.symbol_address && string_equal(relocation.symbol_name, S8("global_value")) &&
                                          relocation.symbol.value == 7;
        }
        BUSTER_TEST(arguments, global_address_relocations == 1);

        CodegenUnwindAction x64_actions[] = {
            {.code_offset = 1, .kind = CODEGEN_UNWIND_ACTION_PUSH_REGISTER, .register_index = 5},
            {.code_offset = 4, .kind = CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, .register_index = 5},
            {.code_offset = 11, .value = 32, .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK},
        };
        CodegenFunctionDescriptor x64_cfi_function = {
            .unwind_actions = x64_actions,
            .code_size = 32,
            .prolog_size = 11,
            .unwind_action_count = BUSTER_ARRAY_LENGTH(x64_actions),
        };
        DwarfCfiResult x64_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                       .functions = &x64_cfi_function,
                                                                       .target = {.cpu_arch = CPU_ARCH_X86_64},
                                                                       .function_count = 1,
                                                                   });
        BUSTER_TEST(arguments, x64_cfi.valid);
        BUSTER_TEST(arguments, x64_cfi.bytes.length >= 40);
        BUSTER_TEST(arguments, x64_cfi.relocation_count == 1);
        if (x64_cfi.valid && x64_cfi.bytes.length >= 12 && x64_cfi.relocation_count == 1)
        {
            BUSTER_TEST(arguments, x64_cfi.bytes.pointer[8] == 1);
            BUSTER_TEST(arguments, x64_cfi.bytes.pointer[9] == 'z' && x64_cfi.bytes.pointer[10] == 'R' && x64_cfi.bytes.pointer[11] == 0);
            BUSTER_TEST(arguments, x64_cfi.relocations[0].offset + 4 <= x64_cfi.bytes.length);
            u32 initial_location = UINT32_MAX;
            memcpy(&initial_location, x64_cfi.bytes.pointer + x64_cfi.relocations[0].offset, sizeof(initial_location));
            BUSTER_TEST(arguments, initial_location == 0);
        }

        CodegenUnwindAction a64_actions[] = {
            {.code_offset = 4, .value = 16, .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK},
            {.code_offset = 4, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 29},
            {.code_offset = 4, .value = 8, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 30},
            {.code_offset = 8, .kind = CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, .register_index = 29},
            {.code_offset = 12, .value = 32, .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK},
            {.code_offset = 16, .value = 32, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 28},
        };
        CodegenFunctionDescriptor a64_cfi_function = {
            .unwind_actions = a64_actions,
            .code_size = 64,
            .prolog_size = 16,
            .unwind_action_count = BUSTER_ARRAY_LENGTH(a64_actions),
        };
        DwarfCfiResult a64_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                       .functions = &a64_cfi_function,
                                                                       .target = {.cpu_arch = CPU_ARCH_AARCH64},
                                                                       .function_count = 1,
                                                                   });
        BUSTER_TEST(arguments, a64_cfi.valid);
        BUSTER_TEST(arguments, a64_cfi.bytes.length >= 40);
        BUSTER_TEST(arguments, a64_cfi.relocation_count == 1);

        CodegenUnwindAction invalid_cfi_action = {
            .code_offset = 4,
            .value = 24,
            .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER,
            .register_index = 30,
        };
        CodegenFunctionDescriptor invalid_cfi_function = {
            .unwind_actions = &invalid_cfi_action,
            .code_size = 8,
            .prolog_size = 4,
            .unwind_action_count = 1,
        };
        DwarfCfiResult invalid_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                          .functions = &invalid_cfi_function,
                                                                          .target = {.cpu_arch = CPU_ARCH_AARCH64},
                                                                          .function_count = 1,
                                                                      });
        BUSTER_TEST(arguments, !invalid_cfi.valid);

        invalid_cfi_function.prolog_size = invalid_cfi_function.code_size + 1;
        invalid_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                            .functions = &invalid_cfi_function,
                                                            .target = {.cpu_arch = CPU_ARCH_AARCH64},
                                                            .function_count = 1,
                                                        });
        BUSTER_TEST(arguments, !invalid_cfi.valid);

        CodegenUnwindAction unaligned_x64_actions[] = {
            {.code_offset = 1, .value = 4, .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK},
            {.code_offset = 2, .kind = CODEGEN_UNWIND_ACTION_PUSH_REGISTER, .register_index = 5},
        };
        CodegenFunctionDescriptor unaligned_x64_function = {
            .unwind_actions = unaligned_x64_actions,
            .code_size = 8,
            .prolog_size = 2,
            .unwind_action_count = BUSTER_ARRAY_LENGTH(unaligned_x64_actions),
        };
        invalid_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                            .functions = &unaligned_x64_function,
                                                            .target = {.cpu_arch = CPU_ARCH_X86_64},
                                                            .function_count = 1,
                                                        });
        BUSTER_TEST(arguments, !invalid_cfi.valid);
    }

    return result;
}
#endif
