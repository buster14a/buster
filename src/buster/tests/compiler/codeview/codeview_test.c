#include <buster/tests/compiler/codeview/codeview_test.h>
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
    BUSTER_TEST(arguments, signature == CODEVIEW_TEST_SIGNATURE_C13);
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
