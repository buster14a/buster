#include <buster/tests/compiler/dwarf/dwarf_test.h>


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

BUSTER_TEST_F_DECL bool dwarf_line_lookup(ByteSlice debug_line, u64 address, DwarfLineRow* row)
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

BUSTER_TEST_F_DECL UnitTestResult dwarf_tests(UnitTestArguments* arguments)
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
            .declaration = {.path = S8("model.c"), .source = 0, .line = 2, .column = 3},
            .locations = dwarf_locations,
            .location_count = BUSTER_ARRAY_LENGTH(dwarf_locations),
            .kind = DEBUG_VARIABLE_PARAMETER,
        },
    };
    DebugScope dwarf_scopes[] = {
        {
            .kind = DEBUG_SCOPE_FUNCTION,
            .start = 0,
            .end = 32,
            .variables = dwarf_variable_ids,
            .variable_count = BUSTER_ARRAY_LENGTH(dwarf_variable_ids),
        },
    };
    DebugFunction dwarf_functions[] = {
        {
            .name = S8("debug_function"),
            .declaration = {.path = S8("model.c"), .source = 0, .line = 1},
            .symbol = {.value = 0},
            .type = 1,
            .scope = 0,
            .code_size = 32,
        },
    };
    DebugInlineSite dwarf_inline_sites[] = {
        {
            .function = dwarf_functions,
            .call_site = {.path = S8("model.c"), .source = 0, .line = 9, .column = 2},
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
    DwarfResult model_built = dwarf_build(arguments->arena, (DwarfInput){
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
                                                           });
    BUSTER_TEST(arguments, model_built.valid);
    BUSTER_TEST(arguments, model_built.sections[DWARF_SECTION_LOC].length > 16);
    BUSTER_TEST(arguments, model_built.sections[DWARF_SECTION_RANGES].length > 32);
    bool has_inline_abbrev = false;
    for (u64 byte_index = 0; byte_index < model_built.sections[DWARF_SECTION_ABBREV].length; byte_index += 1)
    {
        has_inline_abbrev |= model_built.sections[DWARF_SECTION_ABBREV].pointer[byte_index] == 25;
    }
    BUSTER_TEST(arguments, has_inline_abbrev);

    DebugVariable dwarf_global = {
        .name = S8("global_value"),
        .linkage_name = S8("global_value"),
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
    return result;
}
