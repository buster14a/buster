#include <buster/tests/compiler/debug/debug_test.h>
#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL UnitTestResult debug_test_location_index_validation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrSymbol symbol = {.name = S8("tracked"), .id = {.value = 0}, .kind = IR_SYMBOL_DATA};
    IrProgram program = {.symbols = {.symbols = &symbol, .count = 1}};
    IrGlobal global = {.symbol = {.value = 0}, .type = IR_TYPE_ID_INVALID};
    IrModule module = {.globals = &global, .global_count = 1};
    DebugLocationSeed locations[] = {
        {.function_symbol = {.value = 0}, .local = IR_LOCAL_ID_INVALID, .start = 0, .end = 2,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 11}},
        {.function_symbol = {.value = 0}, .local = IR_LOCAL_ID_INVALID, .start = 2, .end = 4,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 23}},
        {.function_symbol = {.value = 0}, .local = IR_LOCAL_ID_INVALID, .start = 4, .end = 6,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 37}},
    };
    DebugLocationIndex built = debug_location_index_build(arguments->arena, locations, (u32)BUSTER_ARRAY_LENGTH(locations));
    BUSTER_TEST(arguments, built.bucket_count == 4 && built.location_count == 3);

    // Keep the backing arrays at their declared sizes. The malformed cases
    // exercise validation of public metadata, not inaccessible test storage.
    for (u32 variant = 0; variant < 18; variant += 1)
    {
        u32 ends[4];
        u32 order[3];
        memcpy(ends, built.bucket_ends, sizeof(ends));
        memcpy(order, built.order, sizeof(order));
        DebugLocationIndex index = built;
        index.bucket_ends = ends;
        index.order = order;
        DebugModelInput input = {
            .program = &program, .module = &module, .locations = locations,
            .location_count = 3, .location_index = &index,
        };
        switch (variant)
        {
        case 0: input.locations = 0; index.locations = 0; break;
        case 1: index.bucket_count = 0; break;
        case 2: index.bucket_count = 3; break;
        case 3: index.bucket_ends = 0; break;
        case 4: index.order = 0; break;
        case 5: ends[0] = 4; break;
        case 6: ends[1] = 2; break;
        case 7: ends[0] = 2; ends[1] = 2; ends[2] = 2; ends[3] = 2; break;
        case 8: order[0] = 3; break;
        case 9: order[0] = UINT32_MAX; break;
        case 10: order[1] = 0; break;
        case 11: order[0] = 1; order[1] = 0; break;
        case 12: ends[0] = 0; break;
        case 13: locations[0].location.piece_count = 1; break;
        case 14: input.inline_site_count = 1; break;
        case 15: index.locations = locations + 1; ends[0] = UINT32_MAX; break;
        case 16: index.location_count = 2; ends[0] = UINT32_MAX; break;
        case 17: break;
        default: break;
        }
        DebugModel model = debug_model_build(arguments->arena, input);
        bool expected_valid = variant >= 15;
        BUSTER_TEST(arguments, model.valid == expected_valid);
        if (expected_valid && model.valid)
        {
            BUSTER_TEST(arguments, model.variable_count == 1 && model.variables[0].location_count == 3);
            bool matches = model.variable_count == 1 && model.variables[0].location_count == 3;
            for (u32 range = 0; matches && range < 3; range += 1)
            {
                DebugLocationRange* actual = model.variables[0].locations + range;
                matches = actual->start == locations[range].start && actual->end == locations[range].end &&
                          actual->location.constant == locations[range].location.constant;
            }
            BUSTER_TEST(arguments, matches);
        }
        else if (!expected_valid)
        {
            BUSTER_TEST(arguments, model.variable_count == 0 && model.scopes == 0 && model.root_scope == DEBUG_SCOPE_INVALID);
        }
        locations[0].location.piece_count = 0;
    }

    DebugLocationIndex empty_index = {0};
    DebugModel empty = debug_model_build(arguments->arena, (DebugModelInput){.program = &program, .module = &module, .location_index = &empty_index});
    BUSTER_TEST(arguments, empty.valid && empty.variable_count == 1 && empty.variables[0].location_count == 1);
    BUSTER_TEST(arguments, empty.variables[0].locations[0].location.kind == DEBUG_LOCATION_UNAVAILABLE);
    DebugModel automatic = debug_model_build(arguments->arena, (DebugModelInput){
        .program = &program, .module = &module, .locations = locations, .location_count = 3,
    });
    BUSTER_TEST(arguments, automatic.valid && automatic.variable_count == 1 && automatic.variables[0].location_count == 3);
    return result;
}

UnitTestResult debug_model_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = debug_test_location_index_validation(arguments);
    BUSTER_TEST(arguments, debug_register_dwarf_number((Target){.cpu_arch = CPU_ARCH_X86_64}, DEBUG_REGISTER_X86_RAX) == 0);
    BUSTER_TEST(arguments, debug_register_dwarf_number((Target){.cpu_arch = CPU_ARCH_X86_64}, DEBUG_REGISTER_X86_RSP) == 7);
    BUSTER_TEST(arguments, debug_register_dwarf_number((Target){.cpu_arch = CPU_ARCH_AARCH64}, DEBUG_REGISTER_AARCH64_X29) == 29);
    BUSTER_TEST(arguments, debug_register_codeview_number((Target){.cpu_arch = CPU_ARCH_X86_64}, DEBUG_REGISTER_X86_R15) == 343);

    // Location transitions are intentionally tested independently of a
    // backend allocator: the neutral model must retain every range and every
    // aggregate piece the backend supplies.
    DebugLocationPiece pieces[] = {
        {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -16, .value_offset = 0, .size = 4},
        {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RAX, .value_offset = 4, .size = 4},
    };
    DebugLocationSeed locations[] = {
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 0, .end = 8,
         .location = {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_R10}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 8, .end = 16,
         .location = {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -24}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 16, .end = 24,
         .location = {.kind = DEBUG_LOCATION_PIECEWISE, .pieces = pieces, .piece_count = BUSTER_ARRAY_LENGTH(pieces)}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 24, .end = 32,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 42}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 32, .end = 40,
         .location = {.kind = DEBUG_LOCATION_UNAVAILABLE}},
    };
    DebugModelInput location_input = {
        .locations = locations,
        .location_count = BUSTER_ARRAY_LENGTH(locations),
    };
    DebugVariable variable = {.local = {.value = 3}};
    debug_variable_add_location(arguments->arena, &location_input, &variable, (IrSymbolId){.value = 7}, (IrLocalId){.value = 3}, 0, 40);
    BUSTER_TEST(arguments, variable.location_count == BUSTER_ARRAY_LENGTH(locations));
    BUSTER_TEST(arguments, variable.locations[0].location.kind == DEBUG_LOCATION_REGISTER);
    BUSTER_TEST(arguments, variable.locations[1].location.kind == DEBUG_LOCATION_FRAME && variable.locations[1].location.frame_offset == -24);
    BUSTER_TEST(arguments, variable.locations[2].location.kind == DEBUG_LOCATION_PIECEWISE && variable.locations[2].location.piece_count == 2);
    BUSTER_TEST(arguments, variable.locations[3].location.kind == DEBUG_LOCATION_CONSTANT && variable.locations[3].location.constant == 42);
    BUSTER_TEST(arguments, variable.locations[4].location.kind == DEBUG_LOCATION_UNAVAILABLE);

    // The symbol index is an accelerator, never a filter: it must reproduce the
    // linear scan exactly, including seed order, and a stale index must be
    // rejected rather than silently dropping locations.
    DebugLocationIndex location_index = debug_location_index_build(arguments->arena, locations, (u32)BUSTER_ARRAY_LENGTH(locations));
    location_input.location_index = &location_index;
    DebugVariable indexed_variable = {.local = {.value = 3}};
    debug_variable_add_location(arguments->arena, &location_input, &indexed_variable, (IrSymbolId){.value = 7}, (IrLocalId){.value = 3}, 0, 40);
    BUSTER_TEST(arguments, indexed_variable.location_count == variable.location_count);
    bool indexed_matches = true;
    for (u32 range_index = 0; range_index < indexed_variable.location_count; range_index += 1)
    {
        DebugLocationRange* expected = variable.locations + range_index;
        DebugLocationRange* actual = indexed_variable.locations + range_index;
        indexed_matches = indexed_matches && expected->start == actual->start && expected->end == actual->end &&
                          expected->location.kind == actual->location.kind && expected->location.piece_count == actual->location.piece_count;
    }
    BUSTER_TEST(arguments, indexed_matches);
    DebugVariable missing_variable = {.local = {.value = 3}};
    debug_variable_add_location(arguments->arena, &location_input, &missing_variable, (IrSymbolId){.value = 9}, (IrLocalId){.value = 3}, 4, 12);
    BUSTER_TEST(arguments, missing_variable.location_count == 1 && missing_variable.locations[0].location.kind == DEBUG_LOCATION_UNAVAILABLE);
    DebugLocationIndex stale_index = location_index;
    stale_index.location_count -= 1;
    location_input.location_index = &stale_index;
    DebugVariable stale_variable = {.local = {.value = 3}};
    debug_variable_add_location(arguments->arena, &location_input, &stale_variable, (IrSymbolId){.value = 7}, (IrLocalId){.value = 3}, 0, 40);
    BUSTER_TEST(arguments, stale_variable.location_count == BUSTER_ARRAY_LENGTH(locations));
    location_input.location_index = &location_index;

    DebugModel scope_model = {
        .scopes = arena_allocate(arguments->arena, DebugScope, 4),
        .variables = arena_allocate(arguments->arena, DebugVariable, 4),
    };
    DebugScopeId function_scope = debug_scope_add(arguments->arena, &scope_model, DEBUG_SCOPE_INVALID, DEBUG_SCOPE_FUNCTION,
                                                   (DebugSourceLocation){.line = 4}, 0, 40, 4);
    DebugScopeId lexical_scope = debug_scope_add(arguments->arena, &scope_model, function_scope, DEBUG_SCOPE_LEXICAL,
                                                  (DebugSourceLocation){.line = 6}, 8, 32, 4);
    DebugVariableId scope_variable = debug_variable_add(arguments->arena, &scope_model, &location_input, scope_model.scopes + lexical_scope,
                                                         S8("value"), 0, (DebugSourceLocation){.line = 7},
                                                         DEBUG_VARIABLE_LOCAL, (IrSymbolId){.value = 7}, (IrLocalId){.value = 3}, 8, 32);
    BUSTER_TEST(arguments, function_scope == 0 && lexical_scope == 1);
    BUSTER_TEST(arguments, scope_variable == 0 && scope_model.scopes[lexical_scope].variable_count == 1);
    BUSTER_TEST(arguments, scope_model.variables[scope_variable].declaration.line == 7);

    DebugScope foreign_scope = {0};
    DebugVariableId null_scope_variable = debug_variable_add(arguments->arena, &scope_model, &location_input, 0, S8("null"), 0,
                                                              (DebugSourceLocation){0}, DEBUG_VARIABLE_LOCAL, IR_SYMBOL_ID_INVALID,
                                                              IR_LOCAL_ID_INVALID, 0, 1);
    DebugVariableId foreign_scope_variable = debug_variable_add(arguments->arena, &scope_model, &location_input, &foreign_scope, S8("foreign"), 0,
                                                                 (DebugSourceLocation){0}, DEBUG_VARIABLE_LOCAL, IR_SYMBOL_ID_INVALID,
                                                                 IR_LOCAL_ID_INVALID, 0, 1);
    DebugVariableId out_of_range_variable = debug_variable_add(arguments->arena, &scope_model, &location_input,
                                                                scope_model.scopes + scope_model.scope_count, S8("past"), 0,
                                                                (DebugSourceLocation){0}, DEBUG_VARIABLE_LOCAL, IR_SYMBOL_ID_INVALID,
                                                                IR_LOCAL_ID_INVALID, 0, 1);
    BUSTER_TEST(arguments, null_scope_variable == DEBUG_ID_INVALID);
    BUSTER_TEST(arguments, foreign_scope_variable == DEBUG_ID_INVALID);
    BUSTER_TEST(arguments, out_of_range_variable == DEBUG_ID_INVALID);
    BUSTER_TEST(arguments, scope_model.variable_count == 1 && scope_model.scopes[lexical_scope].variable_count == 1);

    // Canonical IR type graphs may be recursive.  The model keeps the
    // frontend names while preserving the cycle through explicit IDs.
    // Canonical ranges carry an offset, not a line: the source's own text is
    // what turns one back into a line, so the fixture supplies both.
    String8 canonical_text = S8("one\ntwo\nthree\n");
    IrField canonical_field = {.name = S8("next"), .type = {.value = 2}, .source = {.source = {.value = 0}, .offset = 8}};
    IrType canonical_types[] = {
        {.name = S8("void"), .id = {.value = 0}, .unqualified_type = IR_TYPE_ID_INVALID, .kind = IR_TYPE_VOID},
        {.name = S8("Node"), .id = {.value = 1}, .unqualified_type = IR_TYPE_ID_INVALID, .fields = &canonical_field, .field_count = 1,
         .layout = {.size = 8, .alignment = 8}, .kind = IR_TYPE_STRUCT},
        {.name = S8("Node*"), .id = {.value = 2}, .unqualified_type = IR_TYPE_ID_INVALID, .element_type = {.value = 1},
         .layout = {.size = 8, .alignment = 8}, .kind = IR_TYPE_POINTER},
    };
    IrSource canonical_source = {.path = S8("node.c"), .text = canonical_text, .id = {.value = 0}};
    IrSymbol canonical_symbol = {
        .name = S8("node_function"), .id = {.value = 0}, .type = {.value = 1}, .source = {.source = {.value = 0}, .offset = 4},
        .kind = IR_SYMBOL_FUNCTION,
    };
    IrProgram canonical_program = {
        .types = {.types = canonical_types, .count = BUSTER_ARRAY_LENGTH(canonical_types)},
        .symbols = {.symbols = &canonical_symbol, .count = 1},
        .sources = {.sources = &canonical_source, .count = 1},
    };
    DebugFunctionSeed canonical_function = {.name = S8("node_function"), .symbol = {.value = 0}, .code_size = 16};
    DebugModel canonical_model = debug_model_build(arguments->arena, (DebugModelInput){
                                                                          .program = &canonical_program,
                                                                          .functions = &canonical_function,
                                                                          .function_count = 1,
                                                                      });
    BUSTER_TEST(arguments, canonical_model.valid && canonical_model.type_count == 3);
    BUSTER_TEST(arguments, canonical_model.types[1].kind == DEBUG_TYPE_STRUCT && canonical_model.types[1].fields[0].type == 2);
    BUSTER_TEST(arguments, canonical_model.types[2].kind == DEBUG_TYPE_POINTER && canonical_model.types[2].element_type == 1);
    BUSTER_TEST(arguments, string_equal(canonical_model.types[1].name, S8("Node")));


    return result;
}
#endif
