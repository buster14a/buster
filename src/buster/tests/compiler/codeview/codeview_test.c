#include <buster/tests/compiler/codeview/codeview_test.h>
#if BUSTER_INCLUDE_TESTS


BUSTER_GLOBAL_LOCAL u16 codeview_test_u16(u8 const* bytes)
{
    u16 value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

BUSTER_GLOBAL_LOCAL u32 codeview_test_u32(u8 const* bytes)
{
    u32 value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

// Decode the produced stream independently, following LF_INDEX rather than
// assuming field lists are contiguous or are emitted in primary-type order.
BUSTER_GLOBAL_LOCAL UnitTestResult codeview_test_large_types(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    enum { MEMBER_COUNT = 4096, TYPE_BASE = 0x1000, MAX_RECORD = 0xff00 };
    String8 path = S8("large.c");
    DebugTypeField* fields = arena_allocate(arguments->arena, DebugTypeField, MEMBER_COUNT);
    DebugEnumMember* members = arena_allocate(arguments->arena, DebugEnumMember, MEMBER_COUNT);
    for (u32 index = 0; index < MEMBER_COUNT; index += 1)
    {
        String8 name = string_format(arguments->arena, S8("member_with_a_deliberately_long_debug_name_{u32}"), index);
        fields[index] = (DebugTypeField){.name = name, .type = 0, .offset = index * 4};
        members[index] = (DebugEnumMember){.name = name, .value = index};
    }
    DebugTypeId parameters[] = {0};
    DebugType types[] = {
        {.kind = DEBUG_TYPE_BASE, .name = S8("int"), .size = 4, .is_signed = true},
        // An argument list preceding a field list used to invalidate the
        // precomputed auxiliary indices even without any continuation records.
        {.kind = DEBUG_TYPE_FUNCTION, .return_type = 0, .parameter_types = parameters, .parameter_count = 1},
        {.kind = DEBUG_TYPE_STRUCT, .name = S8("Large"), .size = MEMBER_COUNT * 4, .fields = fields, .field_count = MEMBER_COUNT},
        {.kind = DEBUG_TYPE_ENUM, .name = S8("Values"), .size = 4, .enum_members = members, .enum_member_count = MEMBER_COUNT},
        {.kind = DEBUG_TYPE_UNION, .name = S8("Choice"), .size = 4, .fields = fields, .field_count = 1},
    };
    DebugModel model = {.types = types, .type_count = BUSTER_ARRAY_LENGTH(types), .valid = true};
    CodeviewInput input = {.model = &model, .file_paths = &path, .file_count = 1, .machine = CODEVIEW_MACHINE_X64};
    CodeviewResult built = codeview_build(arguments->arena, input);
    BUSTER_TEST(arguments, built.valid);
    u64 offsets[64] = {0};
    u32 record_count = 0;
    u64 cursor = 4;
    while (built.valid && cursor + 4 <= built.types.length && record_count < BUSTER_ARRAY_LENGTH(offsets))
    {
        u32 size = (u32)codeview_test_u16(built.types.pointer + cursor) + 2;
        BUSTER_TEST(arguments, size >= 4 && size <= MAX_RECORD && !(size & 3));
        if (size < 4 || size > built.types.length - cursor)
        {
            break;
        }
        offsets[record_count++] = cursor;
        cursor += size;
    }
    BUSTER_TEST(arguments, built.valid && cursor == built.types.length && record_count >= 12);
    if (built.valid && cursor == built.types.length && record_count >= 12)
    {
        u32 arguments_index = codeview_test_u32(built.types.pointer + offsets[1] + 12);
        BUSTER_TEST(arguments, arguments_index >= TYPE_BASE && arguments_index - TYPE_BASE < record_count);
        if (arguments_index >= TYPE_BASE && arguments_index - TYPE_BASE < record_count)
        {
            u8* record = built.types.pointer + offsets[arguments_index - TYPE_BASE];
            BUSTER_TEST(arguments, codeview_test_u16(record + 2) == 0x1201 && codeview_test_u32(record + 8) == TYPE_BASE);
        }
        for (u32 type_index = 2; type_index <= 4; type_index += 1)
        {
            bool enumeration = type_index == 3;
            u32 field_index = codeview_test_u32(built.types.pointer + offsets[type_index] + (enumeration ? 12 : 8));
            u32 seen = 0;
            u32 links = 0;
            bool valid = true;
            while (field_index && valid && links < record_count)
            {
                valid = field_index >= TYPE_BASE && field_index - TYPE_BASE < record_count;
                BUSTER_TEST(arguments, valid);
                if (!valid)
                {
                    break;
                }
                u64 offset = offsets[field_index - TYPE_BASE];
                u64 end = offset + 2 + codeview_test_u16(built.types.pointer + offset);
                BUSTER_TEST(arguments, codeview_test_u16(built.types.pointer + offset + 2) == 0x1203);
                cursor = offset + 4;
                u32 next = 0;
                while (cursor < end && valid)
                {
                    u8* record = built.types.pointer + cursor;
                    u16 leaf = codeview_test_u16(record);
                    if (leaf == 0x1404)
                    {
                        valid = end - cursor == 8 && !codeview_test_u16(record + 2);
                        next = codeview_test_u32(record + 4);
                        valid &= next < field_index && next >= TYPE_BASE;
                        cursor += 8;
                    }
                    else
                    {
                        u32 prefix = enumeration ? 10 : 14;
                        valid = leaf == (enumeration ? 0x1502 : 0x150d) && seen < MEMBER_COUNT && end - cursor > prefix;
                        if (!valid)
                        {
                            break;
                        }
                        BUSTER_TEST(arguments, codeview_test_u16(record + prefix - 6) == 0x8003);
                        BUSTER_TEST(arguments, codeview_test_u32(record + prefix - 4) == seen * (enumeration ? 1u : 4u));
                        if (!enumeration)
                        {
                            BUSTER_TEST(arguments, codeview_test_u32(record + 4) == TYPE_BASE);
                        }
                        String8 expected = fields[seen].name;
                        valid = expected.length + 1 <= end - cursor - prefix;
                        if (!valid)
                        {
                            break;
                        }
                        BUSTER_TEST(arguments, !memcmp(record + prefix, expected.pointer, expected.length) && !record[prefix + expected.length]);
                        cursor += prefix + expected.length + 1;
                        u32 padding = (u32)((4 - (cursor & 3)) & 3);
                        for (u32 pad = padding; pad && cursor < end; pad -= 1)
                        {
                            BUSTER_TEST(arguments, built.types.pointer[cursor++] == 0xf0 + pad);
                        }
                        seen += 1;
                    }
                }
                BUSTER_TEST(arguments, valid && cursor == end);
                field_index = next;
                links += next != 0;
            }
            BUSTER_TEST(arguments, !field_index && valid && seen == (type_index == 4 ? 1u : MEMBER_COUNT));
            BUSTER_TEST(arguments, type_index == 4 || links >= 2);
        }
        // LF_UNION has no derived/vshape words: the numeric size starts at +12.
        BUSTER_TEST(arguments, codeview_test_u16(built.types.pointer + offsets[4] + 12) == 0x8003);
    }

    // Exact reserved-continuation boundary, then one byte over. A single member
    // cannot itself be continued; do not wrap the length or loop on overflow.
    char8* large_name = arena_allocate(arguments->arena, char8, MAX_RECORD);
    memset(large_name, 'x', MAX_RECORD);
    fields[0].name = (String8){.pointer = large_name, .length = MAX_RECORD - 4 - 8 - 15};
    types[2].field_count = 1;
    model.type_count = 3;
    BUSTER_TEST(arguments, codeview_build(arguments->arena, input).valid);
    fields[0].name.length += 1;
    BUSTER_TEST(arguments, !codeview_build(arguments->arena, input).valid);
    fields[0].name = S8("field");
    types[2].name = (String8){.pointer = large_name, .length = MAX_RECORD};
    BUSTER_TEST(arguments, !codeview_build(arguments->arena, input).valid);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codeview_test_scope_growth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 previous = 0;
    for (u32 count = 256; count <= 1024; count *= 4)
    {
        String8 path = S8("scopes.c");
        DebugType type = {.kind = DEBUG_TYPE_FUNCTION, .return_type = DEBUG_ID_INVALID};
        DebugScope* scopes = arena_allocate(arguments->arena, DebugScope, count * 3);
        DebugFunction* functions = arena_allocate(arguments->arena, DebugFunction, count);
        DwarfFunction* legacy = arena_allocate(arguments->arena, DwarfFunction, count);
        for (u32 index = 0; index < count; index += 1)
        {
            scopes[index] = (DebugScope){.kind = DEBUG_SCOPE_FUNCTION, .parent = DEBUG_SCOPE_INVALID, .start = index * 8, .end = index * 8 + 8};
            scopes[count + index] = (DebugScope){.kind = DEBUG_SCOPE_LEXICAL, .parent = index, .start = index * 8 + 1, .end = index * 8 + 7};
            scopes[count * 2 + index] = (DebugScope){.kind = DEBUG_SCOPE_LEXICAL, .parent = count + index, .start = index * 8 + 2, .end = index * 8 + 6};
            functions[index] = (DebugFunction){.name = S8("f"), .scope = index, .type = 0, .code_offset = index * 8, .code_size = 8};
            legacy[index] = (DwarfFunction){.name = S8("f"), .code_offset = index * 8, .code_size = 8, .line = 1};
        }
        DebugModel model = {.types = &type, .type_count = 1, .scopes = scopes, .scope_count = count * 3,
                            .functions = functions, .function_count = count, .valid = true};
        u64 start = arguments->arena->position;
        CodeviewResult built = codeview_build(arguments->arena, (CodeviewInput){.model = &model, .file_paths = &path, .file_count = 1,
            .functions = legacy, .function_count = count, .machine = CODEVIEW_MACHINE_X64});
        u64 allocated = arguments->arena->position - start;
        BUSTER_TEST(arguments, built.valid);
        // Includes output buffers, relocations, types and traversal scratch.
        BUSTER_TEST(arguments, allocated < (u64)count * 1024 + 16384);
        BUSTER_TEST(arguments, !previous || allocated < previous * 6);
        if (arguments->show)
        {
            arguments->show(arguments, S8("CODEVIEW_SCOPE_ALLOCATION functions={u32} scopes={u32} bytes={u64}\n"), count, count * 3, allocated);
        }
        previous = allocated;
    }
    return result;
}

UnitTestResult codeview_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = codeview_test_large_types(arguments);
    UnitTestResult growth = codeview_test_scope_growth(arguments);
    result.test_count += growth.test_count;
    result.succeeded_test_count += growth.succeeded_test_count;
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
    // A failed build leaves nothing walkable, and a subsection that overruns the
    // buffer stops the walk; both cases skip the structural tallies below rather
    // than leaving the function early.
    bool walkable = built.valid;
    u32 symbol_subsections = 0;
    u32 line_subsections = 0;
    u32 checksum_subsections = 0;
    u32 string_subsections = 0;
    u32 checked_lines = 0;
    if (walkable)
    {
        u32 signature;
        memcpy(&signature, built.symbols.pointer, sizeof(signature));
        BUSTER_TEST(arguments, signature == CODEVIEW_TEST_SIGNATURE_C13);
        for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
        {
            CodeviewRelocation relocation = built.relocations[relocation_index];
            u64 width = relocation.kind == CODEVIEW_RELOCATION_SECREL32 ? 4 : 2;
            BUSTER_TEST(arguments, relocation.offset + width <= built.symbols.length);
            BUSTER_TEST(arguments, relocation.function < BUSTER_ARRAY_LENGTH(functions));
        }
        // Walk the subsections and check the payload structure.
        u64 offset = 4;
        while (offset + 8 <= built.symbols.length && walkable)
        {
            u32 kind;
            u32 length;
            memcpy(&kind, built.symbols.pointer + offset, sizeof(kind));
            memcpy(&length, built.symbols.pointer + offset + 4, sizeof(length));
            u64 payload = offset + 8;
            walkable = payload + length <= built.symbols.length;
            BUSTER_TEST(arguments, walkable);
            if (walkable)
            {
                symbol_subsections += kind == CODEVIEW_TEST_SYMBOLS;
                checksum_subsections += kind == CODEVIEW_TEST_FILECHKSMS;
                string_subsections += kind == CODEVIEW_TEST_STRINGTABLE;
                if (kind == CODEVIEW_TEST_LINES && length >= 12 + 12 + 8)
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
                    BUSTER_TEST(arguments, (first_line & CODEVIEW_TEST_LINE_NUMBER_MASK) == function->line);
                    BUSTER_TEST(arguments, first_line & CODEVIEW_TEST_LINE_STATEMENT);
                    checked_lines += 1;
                }
                if (kind == CODEVIEW_TEST_STRINGTABLE)
                {
                    BUSTER_TEST(arguments, length >= 1 + files[0].length + 1 + files[1].length + 1);
                    BUSTER_TEST(arguments, built.symbols.pointer[payload] == 0);
                    BUSTER_TEST(arguments, memcmp(built.symbols.pointer + payload + 1, files[0].pointer, files[0].length) == 0);
                }
                offset = payload + ((length + 3) & ~3u);
            }
        }
    }

    if (walkable)
    {
        BUSTER_TEST(arguments, symbol_subsections == 1 + BUSTER_ARRAY_LENGTH(functions));
        BUSTER_TEST(arguments, line_subsections == BUSTER_ARRAY_LENGTH(functions));
        BUSTER_TEST(arguments, checksum_subsections == 1 && string_subsections == 1);
        BUSTER_TEST(arguments, checked_lines == BUSTER_ARRAY_LENGTH(functions));
    }
    // Invalid input: an out-of-range file index must be rejected.
    DwarfLineEntry invalid_line = {.code_offset = 0, .file = 9, .line = 1, .column = 1};
    CodeviewInput invalid = input;
    invalid.lines = &invalid_line;
    invalid.line_count = 1;
    BUSTER_TEST(arguments, !codeview_build(arguments->arena, invalid).valid);

    // The format backend also accepts the neutral model directly.  Keep a
    // register-to-frame transition, a parameter, and a synthetic inline site
    // here so the C13 records cannot silently regress to line-only output.
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
            .name = S8("model_function"),
            .kind = DEBUG_TYPE_FUNCTION,
            .return_type = 0,
            .parameter_types = model_parameter_types,
            .parameter_count = 1,
        },
    };
    DebugLocationPiece model_pieces[] = {
        {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -24, .value_offset = 0, .size = 4},
        {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RAX, .value_offset = 4, .size = 4},
    };
    DebugLocationRange model_locations[] = {
        {.start = 0, .end = 12, .location = {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RAX}},
        {.start = 12, .end = 24, .location = {.kind = DEBUG_LOCATION_PIECEWISE, .pieces = model_pieces, .piece_count = BUSTER_ARRAY_LENGTH(model_pieces)}},
        {.start = 24, .end = 32, .location = {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -16}},
    };
    DebugVariableId model_variable_ids[] = {0};
    DebugVariable model_variables[] = {
        {
            .name = S8("value"),
            .type = 0,
            .declaration = {.source = 0, .line = 2, .column = 5},
            .locations = model_locations,
            .location_count = BUSTER_ARRAY_LENGTH(model_locations),
            .local = {.value = 0},
            .kind = DEBUG_VARIABLE_PARAMETER,
        },
    };
    DebugScope model_scopes[] = {
        {
            .kind = DEBUG_SCOPE_FUNCTION,
            .start = 0,
            .end = 32,
            .variables = model_variable_ids,
            .variable_count = BUSTER_ARRAY_LENGTH(model_variable_ids),
        },
    };
    DebugFunction model_functions[] = {
        {
            .name = S8("model_function"),
            .declaration = {.source = 0, .line = 1, .column = 1},
            .symbol = {.value = 0},
            .type = 1,
            .scope = 0,
            .code_size = 32,
        },
    };
    DebugInlineSite model_inline_sites[] = {
        {
            .function = model_functions,
            .call_site = {.source = 0, .line = 8, .column = 9},
            .start = 4,
            .end = 20,
            .has_ranges = true,
        },
    };
    DebugModel model = {
        .source_paths = files,
        .types = model_types,
        .functions = model_functions,
        .scopes = model_scopes,
        .variables = model_variables,
        .inline_sites = model_inline_sites,
        .source_count = BUSTER_ARRAY_LENGTH(files),
        .type_count = BUSTER_ARRAY_LENGTH(model_types),
        .function_count = BUSTER_ARRAY_LENGTH(model_functions),
        .scope_count = BUSTER_ARRAY_LENGTH(model_scopes),
        .variable_count = BUSTER_ARRAY_LENGTH(model_variables),
        .inline_site_count = BUSTER_ARRAY_LENGTH(model_inline_sites),
        .valid = true,
    };
    DwarfFunction model_function = {
        .name = S8("model_function"),
        .code_offset = 0,
        .code_size = 32,
        .file = 0,
        .line = 1,
    };
    DwarfLineEntry model_line = {.code_offset = 0, .file = 0, .line = 1, .column = 1};
    CodeviewResult model_built = codeview_build(arguments->arena, (CodeviewInput){
                                                                     .model = &model,
                                                                     .producer = S8("buster"),
                                                                     .file_paths = files,
                                                                     .functions = &model_function,
                                                                     .lines = &model_line,
                                                                     .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                     .function_count = 1,
                                                                     .line_count = 1,
                                                                     .machine = CODEVIEW_MACHINE_X64,
                                                                 });
    BUSTER_TEST(arguments, model_built.valid && model_built.types.length > 4);
    bool found_local = false;
    bool found_procedure_type = false;
    bool found_register = false;
    bool found_frame = false;
    bool found_subfield = false;
    bool found_inline = false;
    u64 model_symbol_offset = 4;
    while (model_symbol_offset + 8 <= model_built.symbols.length)
    {
        u32 subsection_kind = 0;
        u32 subsection_length = 0;
        memcpy(&subsection_kind, model_built.symbols.pointer + model_symbol_offset, sizeof(subsection_kind));
        memcpy(&subsection_length, model_built.symbols.pointer + model_symbol_offset + 4, sizeof(subsection_length));
        u64 payload = model_symbol_offset + 8;
        if (payload + subsection_length > model_built.symbols.length)
        {
            break;
        }
        if (subsection_kind == CODEVIEW_TEST_SYMBOLS)
        {
            u64 record_offset = payload;
            u64 record_end = payload + subsection_length;
            while (record_offset + 4 <= record_end)
            {
                u16 record_length = 0;
                u16 record_kind = 0;
                memcpy(&record_length, model_built.symbols.pointer + record_offset, sizeof(record_length));
                memcpy(&record_kind, model_built.symbols.pointer + record_offset + 2, sizeof(record_kind));
                if (record_length < 2 || record_offset + 2 + record_length > record_end)
                {
                    break;
                }
                if (record_kind == CODEVIEW_TEST_S_GPROC32 && record_offset + 32 <= record_end)
                {
                    u32 type_index = 0;
                    memcpy(&type_index, model_built.symbols.pointer + record_offset + 28, sizeof(type_index));
                    found_procedure_type |= type_index == 0x1001u;
                }
                found_local |= record_kind == CODEVIEW_TEST_S_LOCAL;
                found_register |= record_kind == CODEVIEW_TEST_S_DEFRANGE_REGISTER;
                found_frame |= record_kind == CODEVIEW_TEST_S_DEFRANGE_FRAMEPOINTER_REL;
                found_subfield |= record_kind == CODEVIEW_TEST_S_DEFRANGE_SUBFIELD || record_kind == CODEVIEW_TEST_S_DEFRANGE_SUBFIELD_REGISTER;
                found_inline |= record_kind == CODEVIEW_TEST_S_INLINESITE;
                record_offset += 2 + record_length;
            }
        }
        model_symbol_offset = payload + ((subsection_length + 3) & ~3u);
    }
    BUSTER_TEST(arguments, found_procedure_type && found_local && found_register && found_frame && found_subfield && found_inline);

    DebugVariable global_variable = {
        .name = S8("global_value"),
        .linkage_name = S8("global_value"),
        .type = 0,
        .kind = DEBUG_VARIABLE_GLOBAL,
    };
    DebugModel globals_only = model;
    globals_only.functions = 0;
    globals_only.scopes = 0;
    globals_only.variables = &global_variable;
    globals_only.function_count = 0;
    globals_only.scope_count = 0;
    globals_only.variable_count = 1;
    globals_only.inline_sites = 0;
    globals_only.inline_site_count = 0;
    CodeviewResult globals_built = codeview_build(arguments->arena, (CodeviewInput){
                                                                      .model = &globals_only,
                                                                      .producer = S8("buster"),
                                                                      .file_paths = files,
                                                                      .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                      .machine = CODEVIEW_MACHINE_X64,
                                                                  });
    BUSTER_TEST(arguments, globals_built.valid && globals_built.relocation_count == 2);
    return result;
}
#endif
