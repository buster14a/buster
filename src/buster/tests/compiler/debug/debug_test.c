#include <buster/tests/compiler/debug/debug_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/codegen/machine_schedule_internal.h>

BUSTER_GLOBAL_LOCAL UnitTestResult debug_test_scheduled_line_marks(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 const counts[] = {0, 1, 2, 3, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 256, 257, 1023};
    u32 random = 0x9e3779b9u;
    for (u32 size_index = 0; size_index < BUSTER_ARRAY_LENGTH(counts); size_index += 1)
    {
        u32 count = counts[size_index];
        for (u32 pattern = 0; pattern < 7; pattern += 1)
        {
            u64 case_position = arguments->arena->position;
            MachineLineMark* original = arena_allocate(arguments->arena, MachineLineMark, count ? count : 1);
            MachineLineMark* saved = arena_allocate(arguments->arena, MachineLineMark, count ? count : 1);
            MachineLineMark* expected = arena_allocate(arguments->arena, MachineLineMark, count ? count : 1);
            u32* rows = arena_allocate(arguments->arena, u32, count ? count : 1);
            for (u32 index = 0; index < count; index += 1)
            {
                rows[index] = pattern == 1 || pattern == 3 ? count - 1 - index : index;
            }
            if (pattern == 2)
            {
                for (u32 remaining = count; remaining > 1; remaining -= 1)
                {
                    random ^= random << 13;
                    random ^= random >> 17;
                    random ^= random << 5;
                    u32 other = random % remaining;
                    u32 swap = rows[remaining - 1];
                    rows[remaining - 1] = rows[other];
                    rows[other] = swap;
                }
            }
            for (u32 index = 0; index < count; index += 1)
            {
                u32 row = pattern == 3 ? index / 3 : index;
                if (pattern == 4 && index % 3 == 0)
                {
                    row = count;
                }
                if (pattern == 5)
                {
                    row = index % 3 == 0 ? UINT32_MAX : (index % 3 == 1 ? count : index);
                }
                if (pattern == 6 && count > 1 && index >= count - 2)
                {
                    row = 2 * count - 3 - index;
                }
                original[index] = (MachineLineMark){.row = row, .instruction = index};
                saved[index] = original[index];
                expected[index] = original[index];
                if (row < count)
                {
                    expected[index].row = rows[row];
                }
            }
            // Independent reference: the former complete stable insertion sort.
            // Keep this quadratic oracle bounded to the small differential cases.
            for (u32 index = 1; index < count; index += 1)
            {
                MachineLineMark mark = expected[index];
                u32 shift = index;
                while (shift && expected[shift - 1].row > mark.row)
                {
                    expected[shift] = expected[shift - 1];
                    shift -= 1;
                }
                expected[shift] = mark;
            }
            // Account for exactly the retained output allocation, including
            // alignment. Using one arena also tests the harder aliasing case.
            u64 output_position = arguments->arena->position;
            MachineLineMark* reserved = arena_allocate(arguments->arena, MachineLineMark, count ? count : 1);
            BUSTER_UNUSED(reserved);
            u64 retained_position = arguments->arena->position;
            arena_set_position(arguments->arena, output_position);
            MachineLineMark* actual = machine_schedule_remap_line_marks(arguments->arena, arguments->arena,
                                                                         count ? original : 0, count,
                                                                         count ? rows : 0, count);
            BUSTER_TEST(arguments, actual != 0);
            BUSTER_TEST(arguments, memcmp(actual, expected, (u64)count * sizeof(MachineLineMark)) == 0);
            BUSTER_TEST(arguments, memcmp(original, saved, (u64)count * sizeof(MachineLineMark)) == 0);
            BUSTER_TEST(arguments, arguments->arena->position == retained_position);
            // Sorted input needs no merge workspace, even above the tiny path.
            // row_count == 0 deliberately supplies no map and preserves all keys.
            MachineLineMark* repeated = machine_schedule_remap_line_marks(arguments->arena, 0, actual, count, 0, 0);
            BUSTER_TEST(arguments, memcmp(repeated, actual, (u64)count * sizeof(MachineLineMark)) == 0);
            arena_set_position(arguments->arena, case_position);
        }
    }

    // One mark per row of a large reversed publication. The exact linear
    // oracle avoids adding quadratic work to every platform's test suite.
    u64 large_position = arguments->arena->position;
    u32 const large_count = 65537;
    MachineLineMark* original = arena_allocate(arguments->arena, MachineLineMark, large_count);
    u32* rows = arena_allocate(arguments->arena, u32, large_count);
    for (u32 index = 0; index < large_count; index += 1)
    {
        original[index] = (MachineLineMark){.row = index, .instruction = index};
        rows[index] = large_count - 1 - index;
    }
    MachineLineMark* actual = machine_schedule_remap_line_marks(arguments->arena, arguments->arena,
                                                                 original, large_count, rows, large_count);
    bool exact = true;
    bool untouched = true;
    for (u32 index = 0; index < large_count; index += 1)
    {
        exact = exact && actual[index].row == index && actual[index].instruction == large_count - 1 - index;
        untouched = untouched && original[index].row == index && original[index].instruction == index &&
                    rows[index] == large_count - 1 - index;
    }
    BUSTER_TEST(arguments, exact);
    BUSTER_TEST(arguments, untouched);
    arena_set_position(arguments->arena, large_position);
    return result;
}

BUSTER_GLOBAL_LOCAL bool debug_test_location_ranges_equal(DebugVariable* left, DebugVariable* right)
{
    bool equal = left->location_count == right->location_count;
    for (u32 index = 0; equal && index < left->location_count; index += 1)
    {
        DebugLocationRange* left_range = left->locations + index;
        DebugLocationRange* right_range = right->locations + index;
        DebugLocation* left_location = &left_range->location;
        DebugLocation* right_location = &right_range->location;
        equal = left_range->start == right_range->start && left_range->end == right_range->end &&
                left_location->kind == right_location->kind && left_location->reg == right_location->reg &&
                left_location->frame_offset == right_location->frame_offset && left_location->constant == right_location->constant &&
                left_location->piece_count == right_location->piece_count;
        if (equal && left_location->piece_count)
        {
            equal = memcmp(left_location->pieces, right_location->pieces,
                           sizeof(DebugLocationPiece) * left_location->piece_count) == 0;
        }
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL UnitTestResult debug_test_location_local_index(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    DebugLocationSeed locations[] = {
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 0, .end = 2,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 11}},
        {.function_symbol = {.value = 7}, .local = {.value = 4}, .start = 2, .end = 4,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 23}},
        {.function_symbol = {.value = 9}, .local = {.value = 3}, .start = 4, .end = 6,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 37}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 6, .end = 8,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 41}},
        {.function_symbol = {.value = 7}, .local = {.value = 4}, .start = 8, .end = 10,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 53}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 10, .end = 12,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 67}},
    };
    DebugLocationSeed saved[BUSTER_ARRAY_LENGTH(locations)];
    memcpy(saved, locations, sizeof(locations));
    DebugModelInput linear_input = {
        .locations = locations,
        .location_count = BUSTER_ARRAY_LENGTH(locations),
    };
    DebugLocationIndex location_index = debug_location_index_build(arguments->arena, locations, (u32)BUSTER_ARRAY_LENGTH(locations));
    BUSTER_TEST(arguments, location_index.bucket_ends && location_index.order &&
                           location_index.local_bucket_ends && location_index.local_order);
    DebugModelInput indexed_input = linear_input;
    indexed_input.location_index = &location_index;

    DebugVariable linear_exact = {0};
    DebugVariable indexed_exact = {0};
    debug_variable_add_location(arguments->arena, &linear_input, &linear_exact, (IrSymbolId){.value = 7}, (IrLocalId){.value = 3}, 0, 12);
    debug_variable_add_location(arguments->arena, &indexed_input, &indexed_exact, (IrSymbolId){.value = 7}, (IrLocalId){.value = 3}, 0, 12);
    BUSTER_TEST(arguments, debug_test_location_ranges_equal(&linear_exact, &indexed_exact));
    BUSTER_TEST(arguments, indexed_exact.location_count == 3 && indexed_exact.locations[0].start == 0 &&
                           indexed_exact.locations[1].start == 6 && indexed_exact.locations[2].start == 10);

    DebugVariable linear_wildcard = {0};
    DebugVariable indexed_wildcard = {0};
    debug_variable_add_location(arguments->arena, &linear_input, &linear_wildcard, (IrSymbolId){.value = 7}, IR_LOCAL_ID_INVALID, 0, 12);
    debug_variable_add_location(arguments->arena, &indexed_input, &indexed_wildcard, (IrSymbolId){.value = 7}, IR_LOCAL_ID_INVALID, 0, 12);
    BUSTER_TEST(arguments, debug_test_location_ranges_equal(&linear_wildcard, &indexed_wildcard));
    BUSTER_TEST(arguments, indexed_wildcard.location_count == 5 && indexed_wildcard.locations[0].start == 0 &&
                           indexed_wildcard.locations[1].start == 2 && indexed_wildcard.locations[2].start == 6 &&
                           indexed_wildcard.locations[3].start == 8 && indexed_wildcard.locations[4].start == 10);
    BUSTER_TEST(arguments, memcmp(locations, saved, sizeof(locations)) == 0);
    return result;
}

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
    BUSTER_TEST(arguments, built.bucket_count == 4 && built.location_count == 3 &&
                           built.local_bucket_ends && built.local_order);

    // Keep the backing arrays at their declared sizes. The malformed cases
    // exercise validation of public metadata, not inaccessible test storage.
    for (u32 variant = 0; variant < 27; variant += 1)
    {
        u32 ends[4];
        u32 order[3];
        u32 local_ends[4];
        u32 local_order[3];
        memcpy(ends, built.bucket_ends, sizeof(ends));
        memcpy(order, built.order, sizeof(order));
        memcpy(local_ends, built.local_bucket_ends, sizeof(local_ends));
        memcpy(local_order, built.local_order, sizeof(local_order));
        DebugLocationIndex index = built;
        index.bucket_ends = ends;
        index.order = order;
        index.local_bucket_ends = local_ends;
        index.local_order = local_order;
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
        case 5: index.local_bucket_ends = 0; break;
        case 6: index.local_order = 0; break;
        case 7: ends[0] = 4; break;
        case 8: ends[1] = 2; break;
        case 9: ends[0] = 2; ends[1] = 2; ends[2] = 2; ends[3] = 2; break;
        case 10: order[0] = 3; break;
        case 11: order[0] = UINT32_MAX; break;
        case 12: order[1] = 0; break;
        case 13: order[0] = 1; order[1] = 0; break;
        case 14: ends[0] = 0; break;
        case 15: local_ends[0] = 4; break;
        case 16: local_ends[3] = 2; break;
        case 17: local_ends[0] = 2; local_ends[1] = 2; local_ends[2] = 2; local_ends[3] = 2; break;
        case 18: local_order[0] = 3; break;
        case 19: local_order[0] = UINT32_MAX; break;
        case 20: local_order[0] = 0; local_order[1] = 0; local_order[2] = 0; break;
        case 21: local_order[0] = 2; local_order[1] = 1; local_order[2] = 0; break;
        case 22: locations[0].location.piece_count = 1; break;
        case 23: input.inline_site_count = 1; break;
        case 24: index.locations = locations + 1; ends[0] = UINT32_MAX; local_ends[0] = UINT32_MAX; break;
        case 25: index.location_count = 2; ends[0] = UINT32_MAX; local_ends[0] = UINT32_MAX; break;
        case 26: break;
        default: break;
        }
        DebugModel model = debug_model_build(arguments->arena, input);
        bool expected_valid = variant >= 24;
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
    UnitTestResult local_index = debug_test_location_local_index(arguments);
    result.succeeded_test_count += local_index.succeeded_test_count;
    result.test_count += local_index.test_count;
    UnitTestResult scheduled = debug_test_scheduled_line_marks(arguments);
    result.succeeded_test_count += scheduled.succeeded_test_count;
    result.test_count += scheduled.test_count;
    BUSTER_TEST(arguments, debug_register_dwarf_number((Target){.cpu_arch = CPU_ARCH_X86_64}, DEBUG_REGISTER_X86_RAX) == 0);
    BUSTER_TEST(arguments, debug_register_dwarf_number((Target){.cpu_arch = CPU_ARCH_X86_64}, DEBUG_REGISTER_X86_RSP) == 7);
    BUSTER_TEST(arguments, debug_register_dwarf_number((Target){.cpu_arch = CPU_ARCH_AARCH64}, DEBUG_REGISTER_AARCH64_X29) == 29);
    BUSTER_TEST(arguments, debug_register_codeview_number((Target){.cpu_arch = CPU_ARCH_X86_64}, DEBUG_REGISTER_X86_R15) == 343);
    // Every register against CodeView's own enumeration (CV_HREG_e in
    // Microsoft's cvconst.h, as LLVM's CodeViewRegisters.def mirrors it), not
    // against our enum order: RAX and R15 alone are the two registers where
    // "328 + hardware index" happened to agree with it (#1440).
    typedef struct DebugCodeViewRegisterCase DebugCodeViewRegisterCase;
    struct DebugCodeViewRegisterCase
    {
        CpuArch arch;
        DebugRegister reg;
        u32 codeview;
    };
    DebugCodeViewRegisterCase codeview_registers[] = {
        {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_RAX, 328},   {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_RBX, 329},
        {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_RCX, 330},   {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_RDX, 331},
        {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_RSI, 332},   {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_RDI, 333},
        {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_RBP, 334},   {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_RSP, 335},
        {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_R8, 336},    {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_R12, 340},
        {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_XMM0, 154},  {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_XMM7, 161},
        {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_XMM8, 252},  {CPU_ARCH_X86_64, DEBUG_REGISTER_X86_XMM15, 259},
        {CPU_ARCH_AARCH64, DEBUG_REGISTER_AARCH64_X0, 50}, {CPU_ARCH_AARCH64, DEBUG_REGISTER_AARCH64_X28, 78},
        {CPU_ARCH_AARCH64, DEBUG_REGISTER_AARCH64_X29, 79}, {CPU_ARCH_AARCH64, DEBUG_REGISTER_AARCH64_X30, 80},
        {CPU_ARCH_AARCH64, DEBUG_REGISTER_AARCH64_SP, 81}, {CPU_ARCH_AARCH64, DEBUG_REGISTER_AARCH64_V0, 180},
        {CPU_ARCH_AARCH64, DEBUG_REGISTER_AARCH64_V31, 211},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(codeview_registers); index += 1)
    {
        DebugCodeViewRegisterCase expected = codeview_registers[index];
        BUSTER_TEST(arguments, debug_register_codeview_number((Target){.cpu_arch = expected.arch}, expected.reg) == expected.codeview);
    }

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

    // The location index is an accelerator, never a filter: it must reproduce
    // the linear scan exactly, including seed order, and a stale index must be
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
    DebugVariableId scope_variable = debug_variable_add(arguments->arena, &scope_model, &location_input, lexical_scope,
                                                         S8("value"), 0, (DebugSourceLocation){.line = 7},
                                                         DEBUG_VARIABLE_LOCAL, (IrSymbolId){.value = 7}, (IrLocalId){.value = 3}, 8, 32);
    BUSTER_TEST(arguments, function_scope == 0 && lexical_scope == 1);
    BUSTER_TEST(arguments, scope_variable == 0 && scope_model.scopes[lexical_scope].variable_count == 1);
    BUSTER_TEST(arguments, scope_model.variables[scope_variable].declaration.line == 7);

    // Invalid IDs are rejected before any pointer arithmetic. A foreign
    // pointer cannot be passed to the ID-based builder at all.
    DebugVariableId invalid_scope_variable = debug_variable_add(arguments->arena, &scope_model, &location_input, DEBUG_SCOPE_INVALID, S8("invalid"), 0,
                                                                 (DebugSourceLocation){0}, DEBUG_VARIABLE_LOCAL, IR_SYMBOL_ID_INVALID,
                                                                 IR_LOCAL_ID_INVALID, 0, 1);
    DebugVariableId out_of_range_variable = debug_variable_add(arguments->arena, &scope_model, &location_input,
                                                                scope_model.scope_count, S8("past"), 0,
                                                                (DebugSourceLocation){0}, DEBUG_VARIABLE_LOCAL, IR_SYMBOL_ID_INVALID,
                                                                IR_LOCAL_ID_INVALID, 0, 1);
    DebugModel empty_scope_model = {0};
    DebugVariableId empty_scope_variable = debug_variable_add(arguments->arena, &empty_scope_model, &location_input, 0, S8("empty"), 0,
                                                               (DebugSourceLocation){0}, DEBUG_VARIABLE_LOCAL, IR_SYMBOL_ID_INVALID,
                                                               IR_LOCAL_ID_INVALID, 0, 1);
    empty_scope_model.scope_count = 1;
    DebugVariableId null_storage_variable = debug_variable_add(arguments->arena, &empty_scope_model, &location_input, 0, S8("missing"), 0,
                                                                (DebugSourceLocation){0}, DEBUG_VARIABLE_LOCAL, IR_SYMBOL_ID_INVALID,
                                                                IR_LOCAL_ID_INVALID, 0, 1);
    BUSTER_TEST(arguments, invalid_scope_variable == DEBUG_ID_INVALID);
    BUSTER_TEST(arguments, out_of_range_variable == DEBUG_ID_INVALID);
    BUSTER_TEST(arguments, empty_scope_variable == DEBUG_ID_INVALID && empty_scope_model.variable_count == 0);
    BUSTER_TEST(arguments, null_storage_variable == DEBUG_ID_INVALID && empty_scope_model.variable_count == 0);
    BUSTER_TEST(arguments, scope_model.variable_count == 1 && scope_model.scopes[lexical_scope].variable_count == 1);
    DebugVariableId first_scope_variable = debug_variable_add(arguments->arena, &scope_model, &location_input, function_scope, S8("first"), 0,
                                                               (DebugSourceLocation){0}, DEBUG_VARIABLE_LOCAL, IR_SYMBOL_ID_INVALID,
                                                               IR_LOCAL_ID_INVALID, 0, 1);
    BUSTER_TEST(arguments, first_scope_variable == 1 && scope_model.variables[first_scope_variable].scope == function_scope);
    BUSTER_TEST(arguments, scope_model.scopes[function_scope].variable_count == 1 && scope_model.scopes[lexical_scope].variable_count == 1);

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
