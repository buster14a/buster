#include <buster/tests/compiler/ir/ir_test.h>
#include <buster/lib/compiler/ir/ir_internal.h>
#include <buster/lib/compiler/ir/ir_construction.h>
#include <buster/lib/time.h>
#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL u32 ir_test_opcode_count(IrFunction* function, IrOpcode opcode)
{
    u32 count = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        count += function->instructions[index].opcode == opcode;
    }
    return count;
}

#include <buster/tests/compiler/ir/ir_promotion_test.c>
#include <buster/tests/compiler/ir/ir_fast_test.c>
#include <buster/tests/compiler/ir/ir_cfg_test.c>

BUSTER_GLOBAL_LOCAL u32 ir_test_binary_operation_count(IrFunction* function, IrBinaryOperation operation)
{
    u32 count = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* instruction = function->instructions + index;
        count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation == operation;
    }
    return count;
}

#include <buster/tests/compiler/ir/ir_complex_value_test.c>

BUSTER_GLOBAL_LOCAL IrValidationResult ir_test_canonical_wide_float_constant(Arena* arena, u32 bit_width, u64 low, u64 high,
                                                                                     u32 immediate_count, u32 target_count,
                                                                                     u64 layout_size, u32 layout_alignment)
{
    IrProgram program = ir_program_initialize(arena, 1, 2, 0, 0);
    IrTypeId wide = ir_program_add_type(&program, (IrType){
                                                        .kind = IR_TYPE_FLOAT,
                                                        .bit_width = bit_width,
                                                        .layout = {.size = layout_size, .alignment = layout_alignment, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
                                                    });
    IrTypeId function_type = ir_program_add_type(&program, (IrType){
                                                                   .kind = IR_TYPE_FUNCTION,
                                                                   .return_type = wide,
                                                                   .calling_convention = IR_CALLING_CONVENTION_C,
                                                                   .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true},
                                                               });
    IrFunction* function = ir_module_add_function(arena, program.modules, (IrFunction){
                                                                               .canonical_type = function_type,
                                                                               .entry = (IrBlockId){.value = 0},
                                                                               .state = IR_FUNCTION_LOWERED,
                                                                           });
    IrBlock* block = function ? ir_function_add_block(arena, function, (IrBlock){
                                                                       .first_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                       .last_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                       .terminated = true,
                                                                       .sealed = true,
                                                                   })
                              : 0;
    IrValueId value = function ? ir_function_add_value(arena, function, (IrValue){
                                                                      .canonical_type = wide,
                                                                      .definition = IR_INSTRUCTION_ID_INVALID,
                                                                      .category = IR_VALUE_VALUE,
                                                                  })
                               : IR_VALUE_ID_INVALID;
    u64* immediates = arena_allocate(arena, u64, 2);
    if (immediates)
    {
        immediates[0] = low;
        immediates[1] = high;
    }
    IrBlockId* targets = target_count ? arena_allocate(arena, IrBlockId, target_count) : 0;
    for (u32 target_index = 0; targets && target_index < target_count; target_index += 1)
    {
        targets[target_index] = (IrBlockId){.value = 0};
    }
    IrInstructionId constant = function ? ir_function_add_instruction(arena, function, (IrInstruction){
                                                                                         .immediates = immediates,
                                                                                         .canonical_type = wide,
                                                                                         .targets = targets,
                                                                                         .target_count = (u16)target_count,
                                                                                         .result = value,
                                                                                         .opcode = IR_OPCODE_CONSTANT_FLOAT,
                                                                                         .immediate_count = (u16)immediate_count,
                                                                                         .next = IR_INSTRUCTION_ID_INVALID,
                                                                                     },
                                                                        (IrSourceRange){0})
                                           : IR_INSTRUCTION_ID_INVALID;
    IrValueId* operands = arena_allocate(arena, IrValueId, 1);
    if (operands)
    {
        operands[0] = value;
    }
    IrInstructionId returned = function ? ir_function_add_instruction(arena, function, (IrInstruction){
                                                                                         .operands = operands,
                                                                                         .operand_count = 1,
                                                                                         .canonical_type = wide,
                                                                                         .result = IR_VALUE_ID_INVALID,
                                                                                         .opcode = IR_OPCODE_RETURN,
                                                                                         .next = IR_INSTRUCTION_ID_INVALID,
                                                                                     },
                                                                        (IrSourceRange){0})
                                           : IR_INSTRUCTION_ID_INVALID;
    if (function && block && value.value != IR_ID_UNDERLYING_INVALID && constant.value != IR_ID_UNDERLYING_INVALID && returned.value != IR_ID_UNDERLYING_INVALID)
    {
        function->values[value.value].definition = constant;
        function->instructions[constant.value].next = returned;
        block->first_instruction = constant;
        block->last_instruction = returned;
    }
    return ir_validate_canonical_module(&program, program.modules);
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_test_canonical_float_global(Arena* arena, u32 bit_width, IrGlobalInitializerKind initializer_kind)
{
    IrProgram program = ir_program_initialize(arena, 1, 1, 1, 0);
    u64 size = bit_width / 8;
    u32 alignment = (u32)size;
    if (bit_width == 80)
    {
        size = 16;
        alignment = 16;
    }
    IrTypeId floating = ir_program_add_type(&program, (IrType){
                                                           .kind = IR_TYPE_FLOAT,
                                                           .bit_width = bit_width,
                                                           .layout = {.size = size, .alignment = alignment, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
                                                       });
    IrSymbolId symbol = ir_program_add_symbol(&program, (IrSymbol){
                                                                    .type = floating,
                                                                    .kind = IR_SYMBOL_DATA,
                                                                    .linkage = IR_LINKAGE_INTERNAL,
                                                                    .is_definition = true,
                                                                });
    u8* bytes = arena_allocate(arena, u8, size);
    if (bytes)
    {
        memset(bytes, 0, size);
    }
    IrGlobal* global = ir_module_add_global(arena, program.modules, (IrGlobal){
                                                                          .symbol = symbol,
                                                                          .type = floating,
                                                                          .bytes = (ByteSlice){.pointer = bytes, .length = size},
                                                                          .initializer_bits = 1,
                                                                          .initializer_kind = initializer_kind,
                                                                      });
    IrValidationResult result = {.error = IR_VALIDATION_INVALID_ID};
    if (global && floating.value != IR_ID_UNDERLYING_INVALID && symbol.value != IR_ID_UNDERLYING_INVALID)
    {
        result = ir_validate_canonical_module(&program, program.modules);
    }

    return result;
}

// Keep every backing object local and vary one structural fault at a time.
// Rejection must precede pointer arithmetic and signature-dependent iteration.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_call_validation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrValidationError expected[] = {
        IR_VALIDATION_NONE,
        IR_VALIDATION_CALL_SIGNATURE, IR_VALIDATION_CALL_SIGNATURE,
        IR_VALIDATION_CALL_SIGNATURE, IR_VALIDATION_CALL_SIGNATURE,
        IR_VALIDATION_CALL_SIGNATURE, IR_VALIDATION_CALL_SIGNATURE,
        IR_VALIDATION_INVALID_ID, IR_VALIDATION_INVALID_ID,
        IR_VALIDATION_INVALID_ID, IR_VALIDATION_INVALID_ID,
        IR_VALIDATION_INVALID_ID, IR_VALIDATION_INVALID_ID,
        IR_VALIDATION_OPERATION, IR_VALIDATION_OPERATION,
        IR_VALIDATION_INVALID_ID, IR_VALIDATION_INVALID_ID,
        IR_VALIDATION_INVALID_ID,
        IR_VALIDATION_INVALID_ID, IR_VALIDATION_INVALID_ID,
        IR_VALIDATION_INVALID_ID, IR_VALIDATION_INVALID_ID,
        IR_VALIDATION_INVALID_ID, IR_VALIDATION_INVALID_ID,
        IR_VALIDATION_INVALID_ID,
        IR_VALIDATION_NONE, IR_VALIDATION_NONE, IR_VALIDATION_NONE,
        IR_VALIDATION_CALL_SIGNATURE, IR_VALIDATION_CALL_SIGNATURE,
        IR_VALIDATION_INVALID_ID, IR_VALIDATION_INVALID_ID,
    };
    for (u32 variant = 0; variant < BUSTER_ARRAY_LENGTH(expected); variant += 1)
    {
        IrTypeId pointer_type = {.value = 1};
        IrType types[] = {
            {.id = {.value = 0}, .kind = IR_TYPE_VOID, .layout = {.alignment = 1, .resolved = true}},
            {.id = {.value = 1}, .kind = IR_TYPE_POINTER, .element_type = {.value = 3},
             .layout = {.size = 8, .alignment = 8, .resolved = true}},
            {.id = {.value = 2}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 0},
             .parameter_types = &pointer_type, .parameter_count = 1},
            {.id = {.value = 3}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 0}},
        };
        u64 argument_index = 0;
        IrValueId operands[] = {{.value = 0}, {.value = 0}};
        IrValue value = {.canonical_type = {.value = 1}, .definition = {.value = 0}, .category = IR_VALUE_VALUE};
        IrInstruction instructions[] = {
            {.opcode = IR_OPCODE_ARGUMENT, .canonical_type = {.value = 1}, .result = {.value = 0},
             .immediates = &argument_index, .immediate_count = 1, .next = {.value = 1}},
            {.opcode = IR_OPCODE_CALL, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
             .symbol = IR_SYMBOL_ID_INVALID, .operands = operands, .operand_count = 1, .next = {.value = 2}},
            {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
             .next = IR_INSTRUCTION_ID_INVALID},
        };
        IrBlock block = {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 2},
                         .sealed = true, .terminated = true};
        IrFunction function = {.canonical_type = {.value = 2}, .state = IR_FUNCTION_LOWERED, .entry = {.value = 0},
                               .blocks = &block, .block_count = 1, .instructions = instructions, .instruction_count = 3,
                               .values = &value, .value_count = 1};
        IrModule module = {.functions = &function, .function_count = 1};
        IrProgram program = {.modules = &module, .module_count = 1,
                             .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
        switch (variant)
        {
            case 0: break;
            case 1: types[1].element_type = IR_TYPE_ID_INVALID; break;
            case 2: types[1].element_type.value = 0; break;
            case 3: instructions[1].operand_count = 0; break;
            case 4:
                types[3].parameter_count = 1;
                instructions[1].operand_count = 2;
                break;
            case 5:
                types[3].parameter_count = UINT32_MAX;
                types[3].parameter_types = &pointer_type;
                types[3].is_variadic = true;
                break;
            case 6: types[3].return_type = IR_TYPE_ID_INVALID; break;
            case 7: module.functions = 0; break;
            case 8: program.types.types = 0; break;
            case 9: function.blocks = 0; break;
            case 10: function.instructions = 0; break;
            case 11: function.values = 0; break;
            case 12: types[2].parameter_types = 0; break;
            case 13: instructions[1].operands = 0; break;
            case 14: instructions[0].immediates = 0; break;
            case 15: operands[0] = IR_VALUE_ID_INVALID; break;
            case 16: function.entry = IR_BLOCK_ID_INVALID; break;
            case 17: instructions[0].next.value = function.instruction_count; break;
            case 18: program.modules = 0; break;
            case 19: program.symbols.count = 1; break;
            case 20: module.global_count = 1; break;
            case 21: module.alias_count = 1; break;
            case 22: module.initializer_count = 1; break;
            case 23: function.label_metadata_count = 1; break;
            case 24: function.extra_count = 1; break;
            case 25: types[3].is_variadic = true; break;
            case 26:
            case 27:
            case 28:
                types[3].parameter_count = 1;
                types[3].parameter_types = &pointer_type;
                types[3].is_variadic = variant == 27;
                instructions[1].operand_count = variant == 28 ? 1 : 2;
                break;
            case 29:
                types[3].parameter_count = UINT32_MAX;
                types[3].parameter_types = &pointer_type;
                break;
            case 30:
            case 31: break;
            default: BUSTER_TODO();
        }
        IrValidationResult validation = ir_validate_canonical_module(variant == 30 ? 0 : &program, variant == 31 ? 0 : &module);
        BUSTER_TEST(arguments, validation.error == expected[variant]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_construction_appends(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrFunction function = {0};
#if BUSTER_BENCH_ALLOCATIONS
    IrConstructionCounters before = ir_construction_counters();
#endif
    BUSTER_TEST(arguments, ir_function_add_block(0, &function, (IrBlock){0}) == 0);
    BUSTER_TEST(arguments, ir_function_add_value(arguments->arena, 0, (IrValue){0}).value == IR_ID_UNDERLYING_INVALID);
    BUSTER_TEST(arguments, ir_function_add_instruction(0, &function, (IrInstruction){0}, (IrSourceRange){0}).value == IR_ID_UNDERLYING_INVALID);
    // Initial zero capacity, exact powers of two, and repeated growth
    // retain row identity and the parallel canonical source array.
    for (u32 index = 0; index < 65; index += 1)
    {
        IrBlock* block = ir_function_add_block(arguments->arena, &function, (IrBlock){.sealed = true});
        IrValueId value = ir_function_add_value(arguments->arena, &function, (IrValue){.definition = {.value = index}});
        IrInstructionId instruction = ir_function_add_instruction(arguments->arena, &function,
            (IrInstruction){.opcode = IR_OPCODE_CONSTANT_INTEGER, .result = value, .next = IR_INSTRUCTION_ID_INVALID},
            (IrSourceRange){.source = {.value = 7}, .offset = index * 3, .length = 2});
        BUSTER_TEST(arguments, block && block->id.value == index);
        BUSTER_TEST(arguments, value.value == index && instruction.value == index);
        BUSTER_TEST(arguments, function.block_count == index + 1 && function.instruction_count == index + 1 && function.value_count == index + 1);
    }
    BUSTER_TEST(arguments, function.block_capacity == 128 && function.instruction_capacity == 128 && function.value_capacity == 128);
    for (u32 index = 0; index < 65; index += 1)
    {
        BUSTER_TEST(arguments, function.blocks[index].id.value == index && function.blocks[index].sealed);
        BUSTER_TEST(arguments, function.values[index].definition.value == index);
        BUSTER_TEST(arguments, function.instructions[index].opcode == IR_OPCODE_CONSTANT_INTEGER && function.instructions[index].result.value == index);
        BUSTER_TEST(arguments, function.instructions[index].next.value == IR_ID_UNDERLYING_INVALID);
        IrSourceRange source = function.instruction_canonical_sources[index];
        BUSTER_TEST(arguments, source.source.value == 7 && source.offset == index * 3 && source.length == 2);
    }
#if BUSTER_BENCH_ALLOCATIONS
    IrConstructionCounters after = ir_construction_counters();
    BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
#define IR_CONSTRUCTION_EXPECT(counter, expected) \
    BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_##counter] - before.values[IR_CONSTRUCTION_##counter] == (expected))
    IR_CONSTRUCTION_EXPECT(BLOCK_APPENDS, 65);
    IR_CONSTRUCTION_EXPECT(VALUE_APPENDS, 65);
    IR_CONSTRUCTION_EXPECT(INSTRUCTION_APPENDS, 65);
    IR_CONSTRUCTION_EXPECT(OPERAND_SLOTS_APPENDED, 0);
    IR_CONSTRUCTION_EXPECT(BLOCK_GROWS, 5);
    IR_CONSTRUCTION_EXPECT(VALUE_GROWS, 4);
    IR_CONSTRUCTION_EXPECT(INSTRUCTION_GROWS, 4);
    IR_CONSTRUCTION_EXPECT(BLOCK_ROWS_COPIED, 120);
    IR_CONSTRUCTION_EXPECT(VALUE_ROWS_COPIED, 112);
    IR_CONSTRUCTION_EXPECT(INSTRUCTION_ROWS_COPIED, 112);
    IR_CONSTRUCTION_EXPECT(SOURCE_ROWS_COPIED, 112);
    IR_CONSTRUCTION_EXPECT(SOURCE_ROWS_CLEARED, 240);
#undef IR_CONSTRUCTION_EXPECT
    for (u32 index = 0; index < IR_CONSTRUCTION_COUNT; index += 1)
    {
        BUSTER_TEST(arguments, ir_construction_counter_name((IrConstructionCounter)index).length != 0);
    }
    BUSTER_TEST(arguments, ir_construction_counter_name(IR_CONSTRUCTION_COUNT).length == 0);
#endif
    return result;
}

typedef struct IrValidationPrecedenceCase IrValidationPrecedenceCase;
struct IrValidationPrecedenceCase
{
    // Per function: 0 healthy, 1 structural (missing block array), 2 ownership
    // (an unowned row), 3 semantic check (unterminated block).
    u8 faults[3];
    bool alias_fault;
    IrValidationError error;
    u32 function;
};

// The validation pass walks each function's ownership and then checks it while
// its rows are cached. Its result must still be the one the former
// whole-module sweeps produced: structure outranks ownership, ownership
// outranks globals/aliases/initializers, those outrank function checks, and
// the lowest function wins within a category. Each case plants faults of two
// categories in different functions, the higher-ranked one later where it can.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_validation_precedence(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrType types[] = {
        {.id = {.value = 0}, .kind = IR_TYPE_VOID, .layout = {.alignment = 1, .resolved = true}},
        {.id = {.value = 1}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 0}},
    };
    IrValidationPrecedenceCase cases[] = {
        {.faults = {0, 0, 0}, .error = IR_VALIDATION_NONE, .function = IR_ID_UNDERLYING_INVALID},
        {.faults = {3, 2, 0}, .error = IR_VALIDATION_INSTRUCTION_OWNERSHIP, .function = 1},
        {.faults = {2, 0, 1}, .error = IR_VALIDATION_INVALID_ID, .function = 2},
        {.faults = {3, 0, 1}, .error = IR_VALIDATION_INVALID_ID, .function = 2},
        {.faults = {3, 0, 0}, .alias_fault = true, .error = IR_VALIDATION_ALIAS_TARGET, .function = IR_ID_UNDERLYING_INVALID},
        {.faults = {0, 0, 2}, .alias_fault = true, .error = IR_VALIDATION_INSTRUCTION_OWNERSHIP, .function = 2},
        {.faults = {0, 3, 3}, .error = IR_VALIDATION_UNTERMINATED_BLOCK, .function = 1},
        {.faults = {2, 2, 0}, .error = IR_VALIDATION_INSTRUCTION_OWNERSHIP, .function = 0},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(cases); case_index += 1)
    {
        IrValidationPrecedenceCase test_case = cases[case_index];
        IrInstruction rows[3][2];
        IrBlock blocks[3];
        IrFunction functions[3];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(functions); index += 1)
        {
            u8 fault = test_case.faults[index];
            for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(rows[index]); row += 1)
            {
                rows[index][row] = (IrInstruction){.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0},
                                                   .result = IR_VALUE_ID_INVALID, .next = IR_INSTRUCTION_ID_INVALID};
            }
            blocks[index] = (IrBlock){.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 0},
                                      .sealed = true, .terminated = fault != 3};
            functions[index] = (IrFunction){.id = {.value = index}, .canonical_type = {.value = 1}, .state = IR_FUNCTION_LOWERED,
                                            .entry = {.value = 0}, .blocks = fault == 1 ? 0 : blocks + index, .block_count = 1,
                                            .instructions = rows[index], .instruction_count = fault == 2 ? 2 : 1};
        }
        IrSymbolAlias alias = {.symbol = IR_SYMBOL_ID_INVALID, .target = IR_SYMBOL_ID_INVALID};
        IrModule module = {.functions = functions, .function_count = BUSTER_ARRAY_LENGTH(functions),
                           .aliases = test_case.alias_fault ? &alias : 0, .alias_count = test_case.alias_fault ? 1 : 0};
        IrProgram program = {.arena = arguments->arena, .modules = &module, .module_count = 1,
                             .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
        IrValidationResult validation = ir_validate_canonical_module(&program, &module);
        BUSTER_TEST(arguments, validation.error == test_case.error);
        BUSTER_TEST(arguments, validation.function.value == test_case.function);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_validation_census(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_BENCH_ALLOCATIONS
    IrTypeId parameter_type = {.value = 1};
    IrType types[] = {
        {.id = {.value = 0}, .kind = IR_TYPE_VOID, .layout = {.alignment = 1, .resolved = true}},
        {.id = {.value = 1}, .kind = IR_TYPE_POINTER, .element_type = {.value = 3},
         .layout = {.size = 8, .alignment = 8, .resolved = true}},
        {.id = {.value = 2}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 0},
         .parameter_types = &parameter_type, .parameter_count = 1},
        {.id = {.value = 3}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 0}},
    };
    u64 argument_index = 0;
    IrValueId call_operand = {.value = 0};
    IrValue value = {.canonical_type = {.value = 1}, .definition = {.value = 0}, .category = IR_VALUE_VALUE};
    IrInstruction instructions[] = {
        {.opcode = IR_OPCODE_ARGUMENT, .canonical_type = {.value = 1}, .result = {.value = 0},
         .immediates = &argument_index, .immediate_count = 1, .next = {.value = 1}},
        {.opcode = IR_OPCODE_CALL, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
         .symbol = IR_SYMBOL_ID_INVALID, .operands = &call_operand, .operand_count = 1, .next = {.value = 2}},
        {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
         .next = IR_INSTRUCTION_ID_INVALID},
    };
    IrBlock block = {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 2},
                     .sealed = true, .terminated = true};
    IrFunction function = {.canonical_type = {.value = 2}, .state = IR_FUNCTION_LOWERED, .entry = {.value = 0},
                           .blocks = &block, .block_count = 1, .instructions = instructions, .instruction_count = 3,
                           .values = &value, .value_count = 1};
    IrModule module = {.functions = &function, .function_count = 1};
    IrProgram program = {.modules = &module, .module_count = 1,
                         .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
    IrConstructionCounters before = ir_construction_counters();
    IrValidationResult validation = ir_validate_canonical_module(&program, &module);
    IrConstructionCounters after = ir_construction_counters();
    BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
#define IR_VALIDATION_EXPECT(counter, expected) \
    BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_##counter] - before.values[IR_CONSTRUCTION_##counter] == (expected))
    IR_VALIDATION_EXPECT(VALIDATION_CALLS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_OWNERSHIP_FUNCTION_SCANS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_OWNERSHIP_FUNCTIONS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_OWNERSHIP_BLOCKS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_OWNERSHIP_INSTRUCTIONS, 3);
    IR_VALIDATION_EXPECT(VALIDATION_OWNERSHIP_BYTES_CLEARED, sizeof(IrBlockId) * 3);
    IR_VALIDATION_EXPECT(VALIDATION_FUNCTIONS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_VALUE_BLOCKS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_VALUES, 1);
    IR_VALIDATION_EXPECT(VALIDATION_VALUE_PROVENANCE_CHECKS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_BLOCKS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_INSTRUCTIONS, 3);
    IR_VALIDATION_EXPECT(VALIDATION_OPERAND_IDS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_RESULT_RELATIONSHIPS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_OPERATION_CHECKS, 3);
    IR_VALIDATION_EXPECT(VALIDATION_CALL_CHECKS, 1);
    IR_VALIDATION_EXPECT(VALIDATION_TERMINATOR_CHECKS, 3);
#undef IR_VALIDATION_EXPECT
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_bfloat16_representation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    IrProgram program = ir_program_initialize(arena, 1, 3, 0, 0);
    IrTypeId ieee16 = ir_program_add_type(&program, (IrType){
                                                        .kind = IR_TYPE_FLOAT,
                                                        .bit_width = 16,
                                                        .float_format = IR_FLOAT_FORMAT_IEEE,
                                                        .layout = {.size = 2, .alignment = 2, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
                                                    });
    IrTypeId brain16 = ir_program_add_type(&program, (IrType){
                                                         .kind = IR_TYPE_FLOAT,
                                                         .bit_width = 16,
                                                         .float_format = IR_FLOAT_FORMAT_BFLOAT16,
                                                         .layout = {.size = 2, .alignment = 2, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
                                                     });
    IrTypeId function_type = ir_program_add_type(&program, (IrType){
                                                               .kind = IR_TYPE_FUNCTION,
                                                               .return_type = ieee16,
                                                               .calling_convention = IR_CALLING_CONVENTION_C,
                                                               .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true},
                                                           });
    IrFunction* function = ir_module_add_function(arena, program.modules, (IrFunction){
                                                                            .canonical_type = function_type,
                                                                            .entry = (IrBlockId){.value = 0},
                                                                            .state = IR_FUNCTION_LOWERED,
                                                                        });
    IrBlock* block = function ? ir_function_add_block(arena, function, (IrBlock){
                                                                      .first_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                      .last_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                      .terminated = true,
                                                                      .sealed = true,
                                                                  })
                              : 0;
    IrValueId source = function ? ir_function_add_value(arena, function, (IrValue){
                                                                          .canonical_type = ieee16,
                                                                          .definition = IR_INSTRUCTION_ID_INVALID,
                                                                          .category = IR_VALUE_VALUE,
                                                                      })
                                : IR_VALUE_ID_INVALID;
    IrValueId converted = function ? ir_function_add_value(arena, function, (IrValue){
                                                                              .canonical_type = brain16,
                                                                              .definition = IR_INSTRUCTION_ID_INVALID,
                                                                              .category = IR_VALUE_VALUE,
                                                                          })
                                   : IR_VALUE_ID_INVALID;
    u64* immediates = arena_allocate(arena, u64, 1);
    if (immediates)
    {
        immediates[0] = 0x3e00;
    }
    IrValueId* cast_operands = arena_allocate(arena, IrValueId, 1);
    if (cast_operands)
    {
        cast_operands[0] = source;
    }
    IrValueId* return_operands = arena_allocate(arena, IrValueId, 1);
    if (return_operands)
    {
        return_operands[0] = source;
    }
    IrInstructionId constant = function ? ir_function_add_instruction(arena, function, (IrInstruction){
                                                                                         .immediates = immediates,
                                                                                         .immediate_count = 1,
                                                                                         .canonical_type = ieee16,
                                                                                         .result = source,
                                                                                         .opcode = IR_OPCODE_CONSTANT_FLOAT,
                                                                                         .next = IR_INSTRUCTION_ID_INVALID,
                                                                                     },
                                                                        (IrSourceRange){0})
                                        : IR_INSTRUCTION_ID_INVALID;
    IrInstructionId cast = function ? ir_function_add_instruction(arena, function, (IrInstruction){
                                                                                     .operands = cast_operands,
                                                                                     .operand_count = 1,
                                                                                     .canonical_type = brain16,
                                                                                     .result = converted,
                                                                                     .opcode = IR_OPCODE_CAST,
                                                                                     .conversion_operation = IR_CONVERSION_IDENTITY,
                                                                                     .next = IR_INSTRUCTION_ID_INVALID,
                                                                                 },
                                                                    (IrSourceRange){0})
                                    : IR_INSTRUCTION_ID_INVALID;
    IrInstructionId returned = function ? ir_function_add_instruction(arena, function, (IrInstruction){
                                                                                         .operands = return_operands,
                                                                                         .operand_count = 1,
                                                                                         .canonical_type = ieee16,
                                                                                         .result = IR_VALUE_ID_INVALID,
                                                                                         .opcode = IR_OPCODE_RETURN,
                                                                                         .next = IR_INSTRUCTION_ID_INVALID,
                                                                                     },
                                                                        (IrSourceRange){0})
                                        : IR_INSTRUCTION_ID_INVALID;
    if (BUSTER_REQUIRE(arguments, function != 0 && block != 0 && immediates && cast_operands && return_operands &&
                                  source.value != IR_ID_UNDERLYING_INVALID && converted.value != IR_ID_UNDERLYING_INVALID &&
                                  constant.value != IR_ID_UNDERLYING_INVALID && cast.value != IR_ID_UNDERLYING_INVALID &&
                                  returned.value != IR_ID_UNDERLYING_INVALID))
    {
        function->values[source.value].definition = constant;
        function->values[converted.value].definition = cast;
        function->instructions[constant.value].next = cast;
        function->instructions[cast.value].next = returned;
        block->first_instruction = constant;
        block->last_instruction = returned;
        BUSTER_TEST(arguments, ir_validate_canonical_module(&program, program.modules).error == IR_VALIDATION_OPERATION);
        function->instructions[cast.value].canonical_type = ieee16;
        function->values[converted.value].canonical_type = ieee16;
        BUSTER_TEST(arguments, ir_validate_canonical_module(&program, program.modules).error == IR_VALIDATION_NONE);
        function->instructions[constant.value].immediates[0] = UINT64_C(0x13e00);
        BUSTER_TEST(arguments, ir_validate_canonical_module(&program, program.modules).error != IR_VALIDATION_NONE);
        function->instructions[constant.value].immediates[0] = 0x3e00;
    }

    IrProgram global_program = ir_program_initialize(arena, 1, 1, 1, 0);
    IrTypeId global_type = ir_program_add_type(&global_program, (IrType){
                                                                    .kind = IR_TYPE_FLOAT,
                                                                    .bit_width = 16,
                                                                    .float_format = IR_FLOAT_FORMAT_BFLOAT16,
                                                                    .layout = {.size = 2, .alignment = 2, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
                                                                });
    IrSymbolId symbol = ir_program_add_symbol(&global_program, (IrSymbol){
                                                                   .type = global_type,
                                                                   .kind = IR_SYMBOL_DATA,
                                                                   .linkage = IR_LINKAGE_INTERNAL,
                                                                   .is_definition = true,
                                                               });
    u8* global_bytes = arena_allocate(arena, u8, 2);
    if (global_bytes)
    {
        global_bytes[0] = 0xc0;
        global_bytes[1] = 0x3f;
    }
    IrGlobal* global = ir_module_add_global(arena, global_program.modules, (IrGlobal){
                                                                             .symbol = symbol,
                                                                             .type = global_type,
                                                                             .bytes = (ByteSlice){.pointer = global_bytes, .length = 2},
                                                                             .initializer_bits = 0x3fc0,
                                                                             .initializer_kind = IR_GLOBAL_INITIALIZER_FLOAT,
                                                                         });
    IrType* global_type_value = ir_type_from_id(&global_program.types, global_type);
    if (BUSTER_REQUIRE(arguments, global != 0 && global_bytes && global_type_value &&
                                  global_type.value != IR_ID_UNDERLYING_INVALID && symbol.value != IR_ID_UNDERLYING_INVALID))
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(&global_program, global_program.modules).error == IR_VALIDATION_NONE);
        global->initializer_bits = UINT64_C(0x13fc0);
        BUSTER_TEST(arguments, ir_validate_canonical_module(&global_program, global_program.modules).error != IR_VALIDATION_NONE);
        global->initializer_bits = 0x3fc0;
        global_type_value->bit_width = 32;
        global_type_value->layout.size = 4;
        BUSTER_TEST(arguments, ir_validate_canonical_module(&global_program, global_program.modules).error != IR_VALIDATION_NONE);
        global_type_value->bit_width = 16;
        global_type_value->layout.size = 2;
        global_type_value->float_format = IR_FLOAT_FORMAT_COUNT;
        BUSTER_TEST(arguments, ir_validate_canonical_module(&global_program, global_program.modules).error != IR_VALIDATION_NONE);
        global_type_value->float_format = IR_FLOAT_FORMAT_BFLOAT16;
    }
    return result;
}

UnitTestResult ir_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = ir_promotion_tests(arguments);
    UnitTestResult fast = ir_fast_tests(arguments);
    result.test_count += fast.test_count;
    result.succeeded_test_count += fast.succeeded_test_count;
    UnitTestResult cfg = ir_cfg_publication_tests(arguments);
    result.test_count += cfg.test_count;
    result.succeeded_test_count += cfg.succeeded_test_count;
    UnitTestResult construction = ir_test_construction_appends(arguments);
    result.test_count += construction.test_count;
    result.succeeded_test_count += construction.succeeded_test_count;
    UnitTestResult validation_census = ir_test_validation_census(arguments);
    result.test_count += validation_census.test_count;
    result.succeeded_test_count += validation_census.succeeded_test_count;
    UnitTestResult validation_precedence = ir_test_validation_precedence(arguments);
    result.test_count += validation_precedence.test_count;
    result.succeeded_test_count += validation_precedence.succeeded_test_count;

    IrFieldAccessPiece expected_field_access[][IR_FIELD_ACCESS_PIECE_CAPACITY] = {
        {{.offset = 0, .size = 1}},
        {{.offset = 0, .size = 2}},
        {{.offset = 0, .size = 2}, {.offset = 2, .size = 1}},
        {{.offset = 0, .size = 4}},
        {{.offset = 0, .size = 4}, {.offset = 4, .size = 1}},
        {{.offset = 0, .size = 4}, {.offset = 4, .size = 2}},
        {{.offset = 0, .size = 4}, {.offset = 4, .size = 2}, {.offset = 6, .size = 1}},
        {{.offset = 0, .size = 8}},
        {{.offset = 0, .size = 8}, {.offset = 8, .size = 1}},
    };
    u32 expected_field_access_counts[] = {1, 1, 2, 1, 2, 2, 3, 1, 2};
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(expected_field_access) == IR_FIELD_ACCESS_MAX_SIZE);
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(expected_field_access) == BUSTER_ARRAY_LENGTH(expected_field_access_counts));
    for (u32 size = 1; size <= IR_FIELD_ACCESS_MAX_SIZE; size += 1)
    {
        IrFieldAccessPiece pieces[IR_FIELD_ACCESS_PIECE_CAPACITY];
        for (u32 piece = 0; piece < BUSTER_ARRAY_LENGTH(pieces); piece += 1)
        {
            pieces[piece] = (IrFieldAccessPiece){.offset = 0xa5, .size = 0x5a};
        }
        u32 piece_count = ir_field_access_pieces(size, pieces);
        u32 expected_count = expected_field_access_counts[size - 1];
        BUSTER_TEST(arguments, piece_count == expected_count);
        BUSTER_TEST(arguments, piece_count <= IR_FIELD_ACCESS_PIECE_CAPACITY);
        u64 covered = 0;
        for (u32 piece = 0; piece < BUSTER_ARRAY_LENGTH(pieces); piece += 1)
        {
            if (piece < piece_count)
            {
                BUSTER_TEST(arguments, pieces[piece].offset == expected_field_access[size - 1][piece].offset);
                BUSTER_TEST(arguments, pieces[piece].size == expected_field_access[size - 1][piece].size);
                BUSTER_TEST(arguments, pieces[piece].offset == covered);
                covered += pieces[piece].size;
            }
            else
            {
                BUSTER_TEST(arguments, pieces[piece].offset == 0xa5 && pieces[piece].size == 0x5a);
            }
        }
        BUSTER_TEST(arguments, covered == size);
    }
    u64 invalid_field_access_sizes[] = {0, IR_FIELD_ACCESS_MAX_SIZE + 1, UINT64_MAX};
    for (u32 invalid = 0; invalid < BUSTER_ARRAY_LENGTH(invalid_field_access_sizes); invalid += 1)
    {
        IrFieldAccessPiece pieces[IR_FIELD_ACCESS_PIECE_CAPACITY];
        for (u32 piece = 0; piece < BUSTER_ARRAY_LENGTH(pieces); piece += 1)
        {
            pieces[piece] = (IrFieldAccessPiece){.offset = 0xa5, .size = 0x5a};
        }
        BUSTER_TEST(arguments, ir_field_access_pieces(invalid_field_access_sizes[invalid], pieces) == 0);
        for (u32 piece = 0; piece < BUSTER_ARRAY_LENGTH(pieces); piece += 1)
        {
            BUSTER_TEST(arguments, pieces[piece].offset == 0xa5 && pieces[piece].size == 0x5a);
        }
    }
    BUSTER_TEST(arguments, ir_field_access_pieces(IR_FIELD_ACCESS_MAX_SIZE, 0) == 0);

    UnitTestResult call_validation = ir_test_canonical_call_validation(arguments);
    result.succeeded_test_count += call_validation.succeeded_test_count;
    result.test_count += call_validation.test_count;

    // SSEUP shares the preceding register. Union merging can either keep
    // that pair, replace the upper class, or orphan it from an integer head.
    {
        IrProgram fixture = ir_program_initialize(arguments->arena, 0, 16, 0, 0);
        IrTypeId integer = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 64,
            .layout = {.size = 8, .alignment = 8, .resolved = true}});
        IrTypeId floating = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_FLOAT, .bit_width = 64,
            .layout = {.size = 8, .alignment = 8, .resolved = true}});
        IrTypeId vector = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_VECTOR, .element_type = integer, .element_count = 2,
            .layout = {.size = 16, .alignment = 16, .resolved = true}});
        IrTypeId floats = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_ARRAY, .element_type = floating, .element_count = 2,
            .layout = {.size = 16, .alignment = 8, .resolved = true}});
        IrTypeId integers = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_ARRAY, .element_type = integer, .element_count = 2,
            .layout = {.size = 16, .alignment = 8, .resolved = true}});
        IrTypeId overlays[] = {vector, floats, integers, integer};
        for (u32 shape = 0; shape < BUSTER_ARRAY_LENGTH(overlays); shape += 1)
        {
            IrField* fields = arena_allocate(arguments->arena, IrField, 2);
            fields[0] = (IrField){.type = vector};
            fields[1] = (IrField){.type = overlays[shape]};
            IrTypeId record = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_UNION, .fields = fields, .field_count = 2,
                .layout = {.size = 16, .alignment = 16, .resolved = true}});
            for (u32 use = 0; use < IR_ABI_USE_COUNT; use += 1)
            {
                IrAbiValue abi = ir_type_abi_value(&fixture, record, IR_ABI_CONVENTION_SYSTEMV_X86_64, (IrAbiUse)use);
                BUSTER_TEST(arguments, !abi.indirect && !abi.memory && abi.part_count == (shape ? 2u : 1u));
                BUSTER_TEST(arguments, abi.parts[0].abi_class == (shape == 0 ? IR_ABI_CLASS_VECTOR : shape == 1 ? IR_ABI_CLASS_FLOAT : IR_ABI_CLASS_INTEGER));
                BUSTER_TEST(arguments, abi.parts[0].size == (shape ? 8u : 16u));
                if (shape)
                {
                    BUSTER_TEST(arguments, abi.parts[1].abi_class == (shape == 2 ? IR_ABI_CLASS_INTEGER : IR_ABI_CLASS_FLOAT));
                    BUSTER_TEST(arguments, abi.parts[1].value_offset == 8 && abi.parts[1].size == 8);
                }
            }
        }
    }

    // A large unrelated type table must not turn a two-field ABI query into
    // a type-table-sized scratch request. Use fresh scratch arenas so an old
    // high-water mark cannot conceal an allocation regression. The ABI-context
    // cache remains owned by fixture_arena, not by the classifier's worklist.
    {
        Arena* fixture_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(2), .flags = {.no_pool = 1}});
        BUSTER_TEST(arguments, fixture_arena != 0);
        if (fixture_arena)
        {
            IrProgram fixture = ir_program_initialize(fixture_arena, 0, 1036, 0, 0);
            IrType integer_type = {.kind = IR_TYPE_INTEGER, .bit_width = 64,
                                  .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}};
            IrTypeId integer_id = ir_program_add_type(&fixture, integer_type);
            IrTypeId float_id = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_FLOAT, .bit_width = 64,
                .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true}});
            for (u32 index = 0; index < 1024; index += 1)
            {
                ir_program_add_type(&fixture, integer_type);
            }
            IrField* small_fields = arena_allocate(fixture_arena, IrField, 2);
            small_fields[0] = (IrField){.type = float_id, .offset = 0};
            small_fields[1] = (IrField){.type = integer_id, .offset = 8};
            IrType small_type = {.kind = IR_TYPE_STRUCT, .fields = small_fields, .field_count = 2,
                .layout = {.size = 16, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true}};
            IrTypeId small_id = ir_program_add_type(&fixture, small_type);
            // Forty pending siblings, each with forty alternatives, require
            // two growth steps. The float sibling pending below both grows
            // must survive copying; otherwise the first part becomes INTEGER.
            IrTypeId nested = integer_id;
            for (u32 level = 0; level < 2; level += 1)
            {
                IrField* alternatives = arena_allocate(fixture_arena, IrField, 40);
                for (u32 index = 0; index < 40; index += 1)
                {
                    alternatives[index] = (IrField){.type = nested};
                }
                nested = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_UNION, .fields = alternatives, .field_count = 40,
                    .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true}});
            }
            IrField* grown_fields = arena_allocate(fixture_arena, IrField, 2);
            grown_fields[0] = small_fields[0];
            grown_fields[1] = (IrField){.type = nested, .offset = 8};
            small_type.fields = grown_fields;
            IrTypeId grown_id = ir_program_add_type(&fixture, small_type);
            IrField* invalid_fields = arena_allocate(fixture_arena, IrField, 40);
            for (u32 index = 0; index < 40; index += 1)
            {
                invalid_fields[index] = (IrField){.type = integer_id};
            }
            // The first popped field is invalid, after the frontier has grown.
            // A failed classification must rewind its temporary storage too.
            invalid_fields[39].type = IR_TYPE_ID_INVALID;
            IrTypeId invalid_id = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_UNION,
                .fields = invalid_fields, .field_count = 40,
                .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true}});
            // Reject an impossible array count before adding it to a nonempty
            // frontier. That addition would otherwise wrap a u64 to zero.
            IrTypeId excessive_array = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_ARRAY,
                .element_type = integer_id, .element_count = UINT64_MAX,
                .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true}});
            IrField* excessive_fields = arena_allocate(fixture_arena, IrField, 2);
            excessive_fields[0] = small_fields[0];
            excessive_fields[1] = (IrField){.type = excessive_array, .offset = 8};
            small_type.fields = excessive_fields;
            IrTypeId excessive_id = ir_program_add_type(&fixture, small_type);

            ThreadContext* previous = thread_context_selected();
            arena_pool_release_thread();
            ThreadContext* isolated = thread_context_allocate();
            BUSTER_TEST(arguments, isolated != 0);
            if (isolated)
            {
                thread_context_select(isolated);
                u64 positions[SCRATCH_ARENA_COUNT];
                u64 dirty[SCRATCH_ARENA_COUNT];
                for (u32 index = 0; index < (u32)SCRATCH_ARENA_COUNT; index += 1)
                {
                    positions[index] = isolated->arenas[index]->position;
                    dirty[index] = arena_dirty_position(isolated->arenas[index]);
                }
                IrAbiValue small = ir_type_abi_value(&fixture, small_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
                bool local_only = true;
                for (u32 index = 0; index < (u32)SCRATCH_ARENA_COUNT; index += 1)
                {
                    local_only &= isolated->arenas[index]->position == positions[index] && arena_dirty_position(isolated->arenas[index]) == dirty[index];
                }
                IrAbiValue grown = ir_type_abi_value(&fixture, grown_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
                bool rewound = true;
                bool grew = false;
                for (u32 index = 0; index < (u32)SCRATCH_ARENA_COUNT; index += 1)
                {
                    Arena* scratch = isolated->arenas[index];
                    rewound &= scratch->position == positions[index];
                    grew |= arena_dirty_position(scratch) > dirty[index];
                    TemporalArena poison = arena_begin_temporal(scratch);
                    u8* bytes = arena_allocate(scratch, u8, BUSTER_KB(16));
                    memset(bytes, 0xa5, BUSTER_KB(16));
                    scratch_end(poison);
                }
                u64 classifications = fixture.abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64].classified_values;
                IrAbiValue cached = ir_type_abi_value(&fixture, grown_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
                bool cache_reused = classifications == 2 &&
                    fixture.abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64].classified_values == classifications;
                IrAbiValue invalid = ir_type_abi_value(&fixture, invalid_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
                IrAbiValue excessive = ir_type_abi_value(&fixture, excessive_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
                bool invalid_rewound = true;
                for (u32 index = 0; index < (u32)SCRATCH_ARENA_COUNT; index += 1)
                {
                    invalid_rewound &= isolated->arenas[index]->position == positions[index];
                }
                thread_context_release(isolated);
                thread_context_select(previous);
                BUSTER_TEST(arguments, local_only);
                BUSTER_TEST(arguments, rewound && grew);
                BUSTER_TEST(arguments, small.part_count == 2 && !small.memory && !small.indirect);
                BUSTER_TEST(arguments, small.parts[0].abi_class == IR_ABI_CLASS_FLOAT && small.parts[1].abi_class == IR_ABI_CLASS_INTEGER);
                BUSTER_TEST(arguments, grown.part_count == 2 && !grown.memory && !grown.indirect);
                BUSTER_TEST(arguments, grown.parts[0].abi_class == IR_ABI_CLASS_FLOAT && grown.parts[1].abi_class == IR_ABI_CLASS_INTEGER);
                BUSTER_TEST(arguments, cached.part_count == grown.part_count && cached.parts[0].abi_class == grown.parts[0].abi_class &&
                                       cached.parts[1].abi_class == grown.parts[1].abi_class);
                BUSTER_TEST(arguments, cache_reused);
                BUSTER_TEST(arguments, invalid_rewound && invalid.indirect && invalid.part_count == 1);
                BUSTER_TEST(arguments, excessive.indirect && excessive.part_count == 1);
            }
            BUSTER_TEST(arguments, arena_destroy(fixture_arena, 1));
        }
    }

    // Independent ABI contexts share immutable types but not unnamed-field
    // policy or cached classifications. Cover both eightbytes and ABI uses.
    {
        IrProgram bitfields = ir_program_initialize(arguments->arena, 0, 8, 0, 0);
        IrTypeId scalar = ir_program_add_type(&bitfields, (IrType){.kind = IR_TYPE_FLOAT, .bit_width = 32,
            .layout = {.size = 4, .alignment = 4, .resolved = true}});
        IrTypeId integer_type = ir_program_add_type(&bitfields, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 32,
            .layout = {.size = 4, .alignment = 4, .resolved = true}});
        IrField fields[3][2] = {0};
        IrTypeId records[3];
        for (u32 shape = 0; shape < 3; shape += 1)
        {
            fields[shape][0] = (IrField){.name = S8("lead"), .type = scalar};
            fields[shape][1] = (IrField){.name = shape == 2 ? S8("named") : (String8){0}, .type = integer_type,
                .offset = 4, .is_bit_field = true, .bit_width = shape == 1 ? 0 : 20};
            records[shape] = ir_program_add_type(&bitfields, (IrType){.kind = IR_TYPE_STRUCT, .fields = fields[shape], .field_count = 2,
                .layout = {.size = 8, .alignment = 4, .resolved = true}});
        }
        IrTypeId array = ir_program_add_type(&bitfields, (IrType){.kind = IR_TYPE_ARRAY, .element_type = records[0], .element_count = 2,
            .layout = {.size = 16, .alignment = 4, .resolved = true}});
        IrField nested_field = {.name = S8("nested"), .type = records[0]};
        IrTypeId nested = ir_program_add_type(&bitfields, (IrType){.kind = IR_TYPE_STRUCT, .fields = &nested_field, .field_count = 1,
            .layout = {.size = 8, .alignment = 4, .resolved = true}});
        IrType original_types[8];
        memcpy(original_types, bitfields.types.types, bitfields.types.count * sizeof(*original_types));
        IrAbiContext padding = ir_abi_context_initialize(arguments->arena, &bitfields.types, IR_ABI_CONVENTION_SYSTEMV_X86_64);
        IrAbiContext integer = ir_abi_context_initialize(arguments->arena, &bitfields.types, IR_ABI_CONVENTION_SYSTEMV_X86_64);
        integer.sysv_unnamed_bitfields_integer = true;
        for (u32 use = 0; use < IR_ABI_USE_COUNT; use += 1)
        {
            for (u32 shape = 0; shape < 3; shape += 1)
            {
                IrAbiValue old_value = ir_abi_context_value(&bitfields, &padding, records[shape], (IrAbiUse)use);
                IrAbiValue new_value = ir_abi_context_value(&bitfields, &integer, records[shape], (IrAbiUse)use);
                BUSTER_TEST(arguments, old_value.part_count == 1 && old_value.parts[0].abi_class ==
                                      (shape == 2 ? IR_ABI_CLASS_INTEGER : IR_ABI_CLASS_FLOAT));
                BUSTER_TEST(arguments, new_value.part_count == 1 && new_value.parts[0].abi_class ==
                                      (shape == 1 ? IR_ABI_CLASS_FLOAT : IR_ABI_CLASS_INTEGER));
            }
            IrAbiValue old_array = ir_abi_context_value(&bitfields, &padding, array, (IrAbiUse)use);
            IrAbiValue new_array = ir_abi_context_value(&bitfields, &integer, array, (IrAbiUse)use);
            BUSTER_TEST(arguments, old_array.part_count == 2 && old_array.parts[0].abi_class == IR_ABI_CLASS_FLOAT &&
                                  old_array.parts[1].abi_class == IR_ABI_CLASS_FLOAT);
            BUSTER_TEST(arguments, new_array.part_count == 2 && new_array.parts[0].abi_class == IR_ABI_CLASS_INTEGER &&
                                  new_array.parts[1].abi_class == IR_ABI_CLASS_INTEGER);
            BUSTER_TEST(arguments, ir_abi_context_value(&bitfields, &padding, nested, (IrAbiUse)use).parts[0].abi_class == IR_ABI_CLASS_FLOAT);
            BUSTER_TEST(arguments, ir_abi_context_value(&bitfields, &integer, nested, (IrAbiUse)use).parts[0].abi_class == IR_ABI_CLASS_INTEGER);
        }
        integer.sysv_unnamed_bitfields_integer = false;
        ir_abi_context_invalidate(&integer);
        BUSTER_TEST(arguments, ir_abi_context_value(&bitfields, &integer, records[0], IR_ABI_USE_RESULT).parts[0].abi_class == IR_ABI_CLASS_FLOAT);
        BUSTER_TEST(arguments, memcmp(original_types, bitfields.types.types, bitfields.types.count * sizeof(*original_types)) == 0);
    }

    IrProgram abi_program = ir_program_initialize(arguments->arena, 0, 32, 0, 0);
    IrTypeId abi_f32 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 32,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
    });
    IrTypeId abi_f64 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 64,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
    });
    IrTypeId abi_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 80,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
    });
    IrTypeId abi_f128 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 128,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
    });
    IrTypeId abi_integer = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_INTEGER,
        .bit_width = 32,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true},
    });
    IrTypeId abi_u8 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_INTEGER,
        .bit_width = 8,
        .layout = {.size = 1, .alignment = 1, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true},
    });
    u32 abi_short_vector_sizes[] = {1, 2, 4};
    IrTypeId abi_short_vectors[BUSTER_ARRAY_LENGTH(abi_short_vector_sizes)];
    for (u32 vector_index = 0; vector_index < BUSTER_ARRAY_LENGTH(abi_short_vectors); vector_index += 1)
    {
        u32 size = abi_short_vector_sizes[vector_index];
        abi_short_vectors[vector_index] = ir_program_add_type(&abi_program, (IrType){
            .kind = IR_TYPE_VECTOR,
            .element_type = abi_u8,
            .element_count = size,
            .layout = {.size = size, .alignment = size, .abi_class = IR_ABI_CLASS_VECTOR, .resolved = true},
        });
    }
    IrTypeId abi_enum = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_ENUM,
        .bit_width = 32,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true},
    });
    IrField* abi_struct_f80_fields = arena_allocate(arguments->arena, IrField, 1);
    abi_struct_f80_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    IrTypeId abi_struct_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_f80_fields,
        .field_count = 1,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    // `long double _Complex` and the identically laid out plain struct: the
    // two differ only in `is_complex`, which is what System V's COMPLEX_X87
    // class is keyed on, so the pair is the whole test.
    IrField* abi_complex_f80_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_complex_f80_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_complex_f80_fields[1] = (IrField){.type = abi_f80, .offset = 16};
    IrTypeId abi_complex_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_complex_f80_fields,
        .element_type = abi_f80,
        .field_count = 2,
        .is_complex = true,
        .layout = {.size = 32, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_pair_f80_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_pair_f80_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_pair_f80_fields[1] = (IrField){.type = abi_f80, .offset = 16};
    IrTypeId abi_pair_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_pair_f80_fields,
        .element_type = abi_f80,
        .field_count = 2,
        .layout = {.size = 32, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_same_f80_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_union_same_f80_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_union_same_f80_fields[1] = (IrField){.type = abi_f80, .offset = 0};
    IrTypeId abi_union_same_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_same_f80_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_mixed_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_union_mixed_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_union_mixed_fields[1] = (IrField){.type = abi_integer, .offset = 0};
    IrTypeId abi_union_mixed = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_mixed_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_f80_f64_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_union_f80_f64_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_union_f80_f64_fields[1] = (IrField){.type = abi_f64, .offset = 0};
    IrTypeId abi_union_f80_f64 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_f80_f64_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_struct_unaligned_f80_fields = arena_allocate(arguments->arena, IrField, 1);
    abi_struct_unaligned_f80_fields[0] = (IrField){.type = abi_f80, .offset = 1};
    IrTypeId abi_struct_unaligned_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_unaligned_f80_fields,
        .field_count = 1,
        // Deliberately model a packed/unaligned f80 within the 16-byte
        // classifier limit.  The field's own 16-byte alignment/extent must
        // force the aggregate to MEMORY rather than exposing x87 classes.
        .layout = {.size = 16, .alignment = 1, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_struct_enum_fields = arena_allocate(arguments->arena, IrField, 1);
    abi_struct_enum_fields[0] = (IrField){.type = abi_enum, .offset = 0};
    IrTypeId abi_struct_enum = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_enum_fields,
        .field_count = 1,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_struct_large_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_struct_large_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_struct_large_fields[1] = (IrField){.type = abi_integer, .offset = 16};
    IrTypeId abi_struct_large = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_large_fields,
        .field_count = 2,
        .layout = {.size = 32, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });

    IrField* abi_struct_f64x2_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_struct_f64x2_fields[0] = (IrField){.type = abi_f64, .offset = 0};
    abi_struct_f64x2_fields[1] = (IrField){.type = abi_f64, .offset = 8};
    IrTypeId abi_struct_f64x2 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_f64x2_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrTypeId abi_union_f64[4];
    u32 abi_union_f64_count = (u32)(sizeof(abi_union_f64) / sizeof(abi_union_f64[0]));
    for (u32 index = 0; index < abi_union_f64_count; index += 1)
    {
        IrField* fields = arena_allocate(arguments->arena, IrField, index + 1);
        for (u32 field_index = 0; field_index <= index; field_index += 1)
        {
            fields[field_index] = (IrField){.type = abi_f64, .offset = 0};
        }
        abi_union_f64[index] = ir_program_add_type(&abi_program, (IrType){
            .kind = IR_TYPE_UNION,
            .fields = fields,
            .field_count = index + 1,
            .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
        });
    }
    IrField* abi_union_float_double_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_union_float_double_fields[0] = (IrField){.type = abi_f32, .offset = 0};
    abi_union_float_double_fields[1] = (IrField){.type = abi_f64, .offset = 0};
    IrTypeId abi_union_float_double = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_float_double_fields,
        .field_count = 2,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_double_integer_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_union_double_integer_fields[0] = (IrField){.type = abi_f64, .offset = 0};
    abi_union_double_integer_fields[1] = (IrField){.type = abi_integer, .offset = 0};
    IrTypeId abi_union_double_integer = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_double_integer_fields,
        .field_count = 2,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_struct_f64x2_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_union_struct_f64x2_fields[0] = (IrField){.type = abi_struct_f64x2, .offset = 0};
    abi_union_struct_f64x2_fields[1] = (IrField){.type = abi_f64, .offset = 0};
    IrTypeId abi_union_struct_f64x2 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_struct_f64x2_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_struct_union_f64_fields = arena_allocate(arguments->arena, IrField, 2);
    abi_struct_union_f64_fields[0] = (IrField){.type = abi_union_f64[0], .offset = 0};
    abi_struct_union_f64_fields[1] = (IrField){.type = abi_f64, .offset = 8};
    IrTypeId abi_struct_union_f64 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_union_f64_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });

    IrAbiValue abi_f80_argument = ir_type_abi_value(&abi_program, abi_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_f80_variadic = ir_type_abi_value(&abi_program, abi_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_VARIADIC_ARGUMENT);
    IrAbiValue abi_f80_result = ir_type_abi_value(&abi_program, abi_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, ir_type_from_id(&abi_program.types, abi_f80)->layout.alignment == 16);
    BUSTER_TEST(arguments, abi_f80_argument.part_count == 1 && abi_f80_argument.memory && !abi_f80_argument.indirect);
    BUSTER_TEST(arguments, abi_f80_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_f80_argument.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_f80_variadic.part_count == 1 && abi_f80_variadic.memory && !abi_f80_variadic.indirect);
    BUSTER_TEST(arguments, abi_f80_variadic.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_f80_variadic.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_f80_result.part_count == 2 && !abi_f80_result.memory && !abi_f80_result.indirect);
    BUSTER_TEST(arguments, abi_f80_result.parts[0].abi_class == IR_ABI_CLASS_X87 && abi_f80_result.parts[0].value_offset == 0 && abi_f80_result.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_f80_result.parts[1].abi_class == IR_ABI_CLASS_X87_UP && abi_f80_result.parts[1].value_offset == 8 && abi_f80_result.parts[1].size == 8);

    IrAbiConvention aarch64_conventions[] = {IR_ABI_CONVENTION_AAPCS64, IR_ABI_CONVENTION_DARWIN_AARCH64,
                                             IR_ABI_CONVENTION_WINDOWS_AARCH64};
    for (u32 convention = 0; convention < BUSTER_ARRAY_LENGTH(aarch64_conventions); convention += 1)
    {
        for (u32 vector_index = 0; vector_index < BUSTER_ARRAY_LENGTH(abi_short_vectors); vector_index += 1)
        {
            u32 size = abi_short_vector_sizes[vector_index];
            IrTypeId vector = abi_short_vectors[vector_index];
            IrAbiValue argument = ir_type_abi_value(&abi_program, vector, aarch64_conventions[convention], IR_ABI_USE_ARGUMENT);
            IrAbiValue variadic = ir_type_abi_value(&abi_program, vector, aarch64_conventions[convention], IR_ABI_USE_VARIADIC_ARGUMENT);
            IrAbiValue result_value = ir_type_abi_value(&abi_program, vector, aarch64_conventions[convention], IR_ABI_USE_RESULT);
            BUSTER_TEST(arguments, argument.part_count == 1 && argument.parts[0].abi_class == IR_ABI_CLASS_INTEGER &&
                                      argument.parts[0].size == size && !argument.indirect && !argument.memory);
            BUSTER_TEST(arguments, variadic.part_count == 1 && variadic.parts[0].abi_class == IR_ABI_CLASS_INTEGER &&
                                      variadic.parts[0].size == size && !variadic.indirect && !variadic.memory);
            BUSTER_TEST(arguments, result_value.part_count == 1 && result_value.parts[0].abi_class == IR_ABI_CLASS_VECTOR &&
                                      result_value.parts[0].size == size && !result_value.indirect && !result_value.memory);
        }
    }

    IrAbiValue abi_struct_f80_argument = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_struct_f80_variadic = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_VARIADIC_ARGUMENT);
    IrAbiValue abi_struct_f80_result = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_struct_f80_argument.part_count == 1 && abi_struct_f80_argument.memory && !abi_struct_f80_argument.indirect);
    BUSTER_TEST(arguments, abi_struct_f80_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_struct_f80_argument.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_struct_f80_variadic.part_count == 1 && abi_struct_f80_variadic.memory && !abi_struct_f80_variadic.indirect);
    BUSTER_TEST(arguments, abi_struct_f80_result.part_count == 2 && !abi_struct_f80_result.memory && !abi_struct_f80_result.indirect);
    BUSTER_TEST(arguments, abi_struct_f80_result.parts[0].abi_class == IR_ABI_CLASS_X87 && abi_struct_f80_result.parts[0].value_offset == 0 && abi_struct_f80_result.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_struct_f80_result.parts[1].abi_class == IR_ABI_CLASS_X87_UP && abi_struct_f80_result.parts[1].value_offset == 8 && abi_struct_f80_result.parts[1].size == 8);

    // COMPLEX_X87: four eightbyte parts, one X87/X87_UP pair per half, in
    // layout order, so parts[0] is the real half that comes back in ST(0) and
    // parts[2] the imaginary half in ST(1). The argument stays the 32-byte
    // memory slot the size rule gives it, which is what clang passes.
    IrAbiValue abi_complex_f80_argument = ir_type_abi_value(&abi_program, abi_complex_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_complex_f80_result = ir_type_abi_value(&abi_program, abi_complex_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_complex_f80_argument.part_count == 1 && abi_complex_f80_argument.memory && !abi_complex_f80_argument.indirect);
    BUSTER_TEST(arguments, abi_complex_f80_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_complex_f80_argument.parts[0].size == 32);
    BUSTER_TEST(arguments, abi_complex_f80_result.part_count == 4 && !abi_complex_f80_result.memory && !abi_complex_f80_result.indirect);
    BUSTER_TEST(arguments, abi_complex_f80_result.parts[0].abi_class == IR_ABI_CLASS_X87 && abi_complex_f80_result.parts[0].value_offset == 0);
    BUSTER_TEST(arguments, abi_complex_f80_result.parts[1].abi_class == IR_ABI_CLASS_X87_UP && abi_complex_f80_result.parts[1].value_offset == 8);
    BUSTER_TEST(arguments, abi_complex_f80_result.parts[2].abi_class == IR_ABI_CLASS_X87 && abi_complex_f80_result.parts[2].value_offset == 16);
    BUSTER_TEST(arguments, abi_complex_f80_result.parts[3].abi_class == IR_ABI_CLASS_X87_UP && abi_complex_f80_result.parts[3].value_offset == 24);
    BUSTER_TEST(arguments, ir_abi_value_is_complex_x87_result(&abi_program, abi_complex_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64));
    // The same two fields without `is_complex` are returned in memory through
    // a hidden pointer, which is what clang compiles for the plain struct.
    IrAbiValue abi_pair_f80_result = ir_type_abi_value(&abi_program, abi_pair_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_pair_f80_result.part_count == 1 && abi_pair_f80_result.indirect && !abi_pair_f80_result.memory);
    BUSTER_TEST(arguments, abi_pair_f80_result.parts[0].abi_class == IR_ABI_CLASS_POINTER && abi_pair_f80_result.parts[0].size == 8);
    BUSTER_TEST(arguments, !ir_abi_value_is_complex_x87_result(&abi_program, abi_pair_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64));
    // Only System V has the class: Win64 returns the same value through a
    // hidden pointer, and the predicate answers for the convention it is
    // asked about rather than for the type alone.
    BUSTER_TEST(arguments, !ir_abi_value_is_complex_x87_result(&abi_program, abi_complex_f80, IR_ABI_CONVENTION_WIN64_X86_64));

    IrAbiValue abi_union_same_f80_argument = ir_type_abi_value(&abi_program, abi_union_same_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_union_same_f80_result = ir_type_abi_value(&abi_program, abi_union_same_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_union_same_f80_argument.part_count == 1 && abi_union_same_f80_argument.memory && !abi_union_same_f80_argument.indirect);
    BUSTER_TEST(arguments, abi_union_same_f80_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_union_same_f80_argument.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_union_same_f80_result.part_count == 2 && !abi_union_same_f80_result.memory && !abi_union_same_f80_result.indirect);
    BUSTER_TEST(arguments, abi_union_same_f80_result.parts[0].abi_class == IR_ABI_CLASS_X87 && abi_union_same_f80_result.parts[0].value_offset == 0 && abi_union_same_f80_result.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_union_same_f80_result.parts[1].abi_class == IR_ABI_CLASS_X87_UP && abi_union_same_f80_result.parts[1].value_offset == 8 && abi_union_same_f80_result.parts[1].size == 8);

    IrAbiValue abi_union_mixed_argument = ir_type_abi_value(&abi_program, abi_union_mixed, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_union_mixed_result = ir_type_abi_value(&abi_program, abi_union_mixed, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_union_mixed_argument.part_count == 1 && abi_union_mixed_argument.memory && !abi_union_mixed_argument.indirect);
    BUSTER_TEST(arguments, abi_union_mixed_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_union_mixed_argument.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_union_mixed_result.part_count == 1 && abi_union_mixed_result.indirect && !abi_union_mixed_result.memory);
    BUSTER_TEST(arguments, abi_union_mixed_result.parts[0].abi_class == IR_ABI_CLASS_POINTER && abi_union_mixed_result.parts[0].size == 8);

    IrAbiValue abi_union_f80_f64_argument = ir_type_abi_value(&abi_program, abi_union_f80_f64, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_union_f80_f64_result = ir_type_abi_value(&abi_program, abi_union_f80_f64, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_union_f80_f64_argument.part_count == 1 && abi_union_f80_f64_argument.memory && !abi_union_f80_f64_argument.indirect);
    BUSTER_TEST(arguments, abi_union_f80_f64_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_union_f80_f64_argument.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_union_f80_f64_result.part_count == 1 && abi_union_f80_f64_result.indirect && !abi_union_f80_f64_result.memory);
    BUSTER_TEST(arguments, abi_union_f80_f64_result.parts[0].abi_class == IR_ABI_CLASS_POINTER && abi_union_f80_f64_result.parts[0].size == 8);

    IrAbiValue abi_struct_unaligned_f80_argument =
        ir_type_abi_value(&abi_program, abi_struct_unaligned_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_struct_unaligned_f80_result =
        ir_type_abi_value(&abi_program, abi_struct_unaligned_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_struct_unaligned_f80_argument.part_count == 1 && abi_struct_unaligned_f80_argument.memory && !abi_struct_unaligned_f80_argument.indirect);
    BUSTER_TEST(arguments, abi_struct_unaligned_f80_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_struct_unaligned_f80_argument.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_struct_unaligned_f80_result.part_count == 1 && abi_struct_unaligned_f80_result.indirect && !abi_struct_unaligned_f80_result.memory);
    BUSTER_TEST(arguments, abi_struct_unaligned_f80_result.parts[0].abi_class == IR_ABI_CLASS_POINTER && abi_struct_unaligned_f80_result.parts[0].size == 8);

    IrAbiValue abi_enum_argument = ir_type_abi_value(&abi_program, abi_enum, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_enum_result = ir_type_abi_value(&abi_program, abi_enum, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    IrAbiValue abi_struct_enum_argument = ir_type_abi_value(&abi_program, abi_struct_enum, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_struct_enum_result = ir_type_abi_value(&abi_program, abi_struct_enum, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_enum_argument.part_count == 1 && !abi_enum_argument.memory && !abi_enum_argument.indirect);
    BUSTER_TEST(arguments, abi_enum_argument.parts[0].abi_class == IR_ABI_CLASS_INTEGER && abi_enum_argument.parts[0].size == 4);
    BUSTER_TEST(arguments, abi_enum_result.part_count == 1 && !abi_enum_result.memory && !abi_enum_result.indirect);
    BUSTER_TEST(arguments, abi_enum_result.parts[0].abi_class == IR_ABI_CLASS_INTEGER && abi_enum_result.parts[0].size == 4);
    BUSTER_TEST(arguments, abi_struct_enum_argument.part_count == 1 && !abi_struct_enum_argument.memory && !abi_struct_enum_argument.indirect);
    BUSTER_TEST(arguments, abi_struct_enum_argument.parts[0].abi_class == IR_ABI_CLASS_INTEGER && abi_struct_enum_argument.parts[0].size == 4);
    BUSTER_TEST(arguments, abi_struct_enum_result.part_count == 1 && !abi_struct_enum_result.memory && !abi_struct_enum_result.indirect);
    BUSTER_TEST(arguments, abi_struct_enum_result.parts[0].abi_class == IR_ABI_CLASS_INTEGER && abi_struct_enum_result.parts[0].size == 4);

    IrAbiValue abi_struct_large_argument = ir_type_abi_value(&abi_program, abi_struct_large, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_struct_large_result = ir_type_abi_value(&abi_program, abi_struct_large, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_struct_large_argument.part_count == 1 && abi_struct_large_argument.memory && !abi_struct_large_argument.indirect);
    BUSTER_TEST(arguments, abi_struct_large_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_struct_large_argument.parts[0].size == 32);
    BUSTER_TEST(arguments, abi_struct_large_result.part_count == 1 && abi_struct_large_result.indirect && !abi_struct_large_result.memory);
    BUSTER_TEST(arguments, abi_struct_large_result.parts[0].abi_class == IR_ABI_CLASS_POINTER && abi_struct_large_result.parts[0].size == 8);

    for (u32 index = 0; index < abi_union_f64_count; index += 1)
    {
        IrAbiValue argument = ir_type_abi_value(&abi_program, abi_union_f64[index], IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
        IrAbiValue result_value = ir_type_abi_value(&abi_program, abi_union_f64[index], IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_RESULT);
        BUSTER_TEST(arguments, argument.part_count == 1 && !argument.indirect && !argument.memory && argument.parts[0].abi_class == IR_ABI_CLASS_FLOAT &&
                                     argument.parts[0].value_offset == 0 && argument.parts[0].size == 8);
        BUSTER_TEST(arguments, result_value.part_count == 1 && !result_value.indirect && !result_value.memory && result_value.parts[0].abi_class == IR_ABI_CLASS_FLOAT &&
                                       result_value.parts[0].value_offset == 0 && result_value.parts[0].size == 8);
    }
    IrAbiValue abi_union_float_double_aapcs = ir_type_abi_value(&abi_program, abi_union_float_double, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_union_float_double_result = ir_type_abi_value(&abi_program, abi_union_float_double, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_RESULT);
    IrAbiValue abi_union_double_integer_aapcs = ir_type_abi_value(&abi_program, abi_union_double_integer, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_union_double_integer_result = ir_type_abi_value(&abi_program, abi_union_double_integer, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_union_float_double_aapcs.part_count == 1 && !abi_union_float_double_aapcs.indirect && !abi_union_float_double_aapcs.memory &&
                                 abi_union_float_double_aapcs.parts[0].abi_class == IR_ABI_CLASS_INTEGER && abi_union_float_double_aapcs.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_union_float_double_result.part_count == 1 && !abi_union_float_double_result.indirect && !abi_union_float_double_result.memory &&
                                 abi_union_float_double_result.parts[0].abi_class == IR_ABI_CLASS_INTEGER && abi_union_float_double_result.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_union_double_integer_aapcs.part_count == 1 && !abi_union_double_integer_aapcs.indirect && !abi_union_double_integer_aapcs.memory &&
                                 abi_union_double_integer_aapcs.parts[0].abi_class == IR_ABI_CLASS_INTEGER && abi_union_double_integer_aapcs.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_union_double_integer_result.part_count == 1 && !abi_union_double_integer_result.indirect && !abi_union_double_integer_result.memory &&
                                 abi_union_double_integer_result.parts[0].abi_class == IR_ABI_CLASS_INTEGER && abi_union_double_integer_result.parts[0].size == 8);
    IrAbiValue abi_union_struct_f64x2_aapcs = ir_type_abi_value(&abi_program, abi_union_struct_f64x2, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_union_struct_f64x2_result = ir_type_abi_value(&abi_program, abi_union_struct_f64x2, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_RESULT);
    IrAbiValue abi_struct_union_f64_aapcs = ir_type_abi_value(&abi_program, abi_struct_union_f64, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_struct_union_f64_result = ir_type_abi_value(&abi_program, abi_struct_union_f64, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_union_struct_f64x2_aapcs.part_count == 2 && !abi_union_struct_f64x2_aapcs.indirect && !abi_union_struct_f64x2_aapcs.memory &&
                                 abi_union_struct_f64x2_aapcs.parts[0].abi_class == IR_ABI_CLASS_FLOAT && abi_union_struct_f64x2_aapcs.parts[0].size == 8 &&
                                 abi_union_struct_f64x2_aapcs.parts[1].abi_class == IR_ABI_CLASS_FLOAT && abi_union_struct_f64x2_aapcs.parts[1].value_offset == 8 &&
                                 abi_union_struct_f64x2_aapcs.parts[1].size == 8);
    BUSTER_TEST(arguments, abi_union_struct_f64x2_result.part_count == 2 && !abi_union_struct_f64x2_result.indirect && !abi_union_struct_f64x2_result.memory &&
                                 abi_union_struct_f64x2_result.parts[0].abi_class == IR_ABI_CLASS_FLOAT && abi_union_struct_f64x2_result.parts[0].size == 8 &&
                                 abi_union_struct_f64x2_result.parts[1].abi_class == IR_ABI_CLASS_FLOAT && abi_union_struct_f64x2_result.parts[1].value_offset == 8 &&
                                 abi_union_struct_f64x2_result.parts[1].size == 8);
    BUSTER_TEST(arguments, abi_struct_union_f64_aapcs.part_count == 2 && !abi_struct_union_f64_aapcs.indirect && !abi_struct_union_f64_aapcs.memory &&
                                 abi_struct_union_f64_aapcs.parts[0].abi_class == IR_ABI_CLASS_FLOAT && abi_struct_union_f64_aapcs.parts[0].size == 8 &&
                                 abi_struct_union_f64_aapcs.parts[1].abi_class == IR_ABI_CLASS_FLOAT && abi_struct_union_f64_aapcs.parts[1].value_offset == 8 &&
                                 abi_struct_union_f64_aapcs.parts[1].size == 8);
    BUSTER_TEST(arguments, abi_struct_union_f64_result.part_count == 2 && !abi_struct_union_f64_result.indirect && !abi_struct_union_f64_result.memory &&
                                 abi_struct_union_f64_result.parts[0].abi_class == IR_ABI_CLASS_FLOAT && abi_struct_union_f64_result.parts[0].size == 8 &&
                                 abi_struct_union_f64_result.parts[1].abi_class == IR_ABI_CLASS_FLOAT && abi_struct_union_f64_result.parts[1].value_offset == 8 &&
                                 abi_struct_union_f64_result.parts[1].size == 8);

    IrAbiValue abi_f128_aapcs_argument = ir_type_abi_value(&abi_program, abi_f128, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_f128_aapcs_result = ir_type_abi_value(&abi_program, abi_f128, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_RESULT);
    IrAbiValue abi_f128_darwin_argument = ir_type_abi_value(&abi_program, abi_f128, IR_ABI_CONVENTION_DARWIN_AARCH64, IR_ABI_USE_ARGUMENT);
    BUSTER_TEST(arguments, abi_f128_aapcs_argument.part_count == 1 && !abi_f128_aapcs_argument.indirect && !abi_f128_aapcs_argument.memory &&
                               abi_f128_aapcs_argument.parts[0].abi_class == IR_ABI_CLASS_VECTOR && abi_f128_aapcs_argument.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_f128_aapcs_result.part_count == 1 && !abi_f128_aapcs_result.indirect && !abi_f128_aapcs_result.memory &&
                               abi_f128_aapcs_result.parts[0].abi_class == IR_ABI_CLASS_VECTOR && abi_f128_aapcs_result.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_f128_darwin_argument.part_count == 1 && abi_f128_darwin_argument.memory && !abi_f128_darwin_argument.indirect &&
                               abi_f128_darwin_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_f128_darwin_argument.parts[0].size == 16);

    IrAbiValue abi_f32_systemv = ir_type_abi_value(&abi_program, abi_f32, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_f64_systemv = ir_type_abi_value(&abi_program, abi_f64, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    IrAbiValue abi_f32_win64 = ir_type_abi_value(&abi_program, abi_f32, IR_ABI_CONVENTION_WIN64_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_f64_aapcs = ir_type_abi_value(&abi_program, abi_f64, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_f32_systemv.part_count == 1 && abi_f32_systemv.parts[0].abi_class == IR_ABI_CLASS_FLOAT && abi_f32_systemv.parts[0].size == 4);
    BUSTER_TEST(arguments, abi_f64_systemv.part_count == 1 && abi_f64_systemv.parts[0].abi_class == IR_ABI_CLASS_FLOAT && abi_f64_systemv.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_f32_win64.part_count == 1 && abi_f32_win64.parts[0].abi_class == IR_ABI_CLASS_FLOAT && !abi_f32_win64.indirect);
    BUSTER_TEST(arguments, abi_f64_aapcs.part_count == 1 && abi_f64_aapcs.parts[0].abi_class == IR_ABI_CLASS_FLOAT && !abi_f64_aapcs.indirect);

    IrAbiValue abi_f80_win64_argument = ir_type_abi_value(&abi_program, abi_f80, IR_ABI_CONVENTION_WIN64_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_f80_win64_result = ir_type_abi_value(&abi_program, abi_f80, IR_ABI_CONVENTION_WIN64_X86_64, IR_ABI_USE_RESULT);
    IrAbiValue abi_f80_aapcs_argument = ir_type_abi_value(&abi_program, abi_f80, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_f80_aapcs_result = ir_type_abi_value(&abi_program, abi_f80, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_RESULT);
    IrAbiValue abi_struct_f80_win64_argument = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_WIN64_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_struct_f80_win64_result = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_WIN64_X86_64, IR_ABI_USE_RESULT);
    IrAbiValue abi_struct_f80_aapcs_argument = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_struct_f80_aapcs_result = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_f80_win64_argument.part_count == 1 && abi_f80_win64_argument.memory && !abi_f80_win64_argument.indirect);
    BUSTER_TEST(arguments, abi_f80_win64_result.part_count == 1 && abi_f80_win64_result.indirect && abi_f80_win64_result.parts[0].abi_class == IR_ABI_CLASS_POINTER);
    BUSTER_TEST(arguments, abi_f80_aapcs_argument.part_count == 1 && abi_f80_aapcs_argument.memory && !abi_f80_aapcs_argument.indirect);
    BUSTER_TEST(arguments, abi_f80_aapcs_result.part_count == 1 && abi_f80_aapcs_result.indirect && abi_f80_aapcs_result.parts[0].abi_class == IR_ABI_CLASS_POINTER);
    BUSTER_TEST(arguments, abi_struct_f80_win64_argument.part_count == 1 && abi_struct_f80_win64_argument.indirect && !abi_struct_f80_win64_argument.memory);
    BUSTER_TEST(arguments, abi_struct_f80_win64_argument.parts[0].abi_class == IR_ABI_CLASS_POINTER && abi_struct_f80_win64_argument.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_struct_f80_win64_result.part_count == 1 && abi_struct_f80_win64_result.indirect && !abi_struct_f80_win64_result.memory);
    BUSTER_TEST(arguments, abi_struct_f80_win64_result.parts[0].abi_class == IR_ABI_CLASS_POINTER && abi_struct_f80_win64_result.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_struct_f80_aapcs_argument.part_count == 1 && !abi_struct_f80_aapcs_argument.indirect && !abi_struct_f80_aapcs_argument.memory);
    BUSTER_TEST(arguments, abi_struct_f80_aapcs_argument.parts[0].abi_class == IR_ABI_CLASS_FLOAT && abi_struct_f80_aapcs_argument.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_struct_f80_aapcs_result.part_count == 1 && !abi_struct_f80_aapcs_result.indirect && !abi_struct_f80_aapcs_result.memory);
    BUSTER_TEST(arguments, abi_struct_f80_aapcs_result.parts[0].abi_class == IR_ABI_CLASS_FLOAT && abi_struct_f80_aapcs_result.parts[0].size == 16);

    // Compare every cache use to the unchanged, uncached classifier over the
    // scalar/aggregate ABI corpus above, in an interleaved convention order.
    IrAbiContext abi_contexts[IR_ABI_CONVENTION_COUNT];
    for (u32 convention = 0; convention < IR_ABI_CONVENTION_COUNT; convention += 1)
    {
        abi_contexts[convention] = ir_abi_context_initialize(arguments->arena, &abi_program.types, (IrAbiConvention)convention);
    }
    for (u32 repetition = 0; repetition < 3; repetition += 1)
    {
        for (u32 type = 0; type < abi_program.types.count; type += 1)
        {
            for (u32 use = 0; use < IR_ABI_USE_COUNT; use += 1)
            {
                for (u32 convention = 0; convention < IR_ABI_CONVENTION_COUNT; convention += 1)
                {
                    IrAbiValue expected = ir_test_abi_reference(&abi_program, (IrTypeId){type}, (IrAbiConvention)convention, (IrAbiUse)use);
                    IrAbiValue actual = ir_abi_context_value(&abi_program, abi_contexts + convention, (IrTypeId){type}, (IrAbiUse)use);
                    // IrAbiValue names its two tail bytes explicitly and every
                    // classifier result initializes them; there is no padding.
                    BUSTER_CT_CHECK(sizeof(IrAbiValue) == sizeof(IrAbiPart) * IR_ABI_MAX_PARTS + sizeof(u32) + 4);
                    BUSTER_TEST(arguments, memcmp(&actual, &expected, sizeof(actual)) == 0);
                }
            }
        }
    }
    for (u32 convention = 0; convention < IR_ABI_CONVENTION_COUNT; convention += 1)
    {
        u32 uses = convention == IR_ABI_CONVENTION_WINDOWS_AARCH64 ? 3 : 2;
        BUSTER_TEST(arguments, abi_contexts[convention].classified_values == (u64)uses * abi_program.types.count);
        BUSTER_TEST(arguments, abi_contexts[convention].pages[IR_ABI_USE_ARGUMENT] != abi_program.abi_contexts[convention].pages[IR_ABI_USE_ARGUMENT]);
    }
    IrAbiContext independent = ir_abi_context_initialize(arguments->arena, &abi_program.types, IR_ABI_CONVENTION_SYSTEMV_X86_64);
    IrAbiValue independent_result = ir_abi_context_value(&abi_program, &independent, abi_f80, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, independent.classified_values == 1 && independent_result.part_count == 2);
    BUSTER_TEST(arguments, independent.pages[IR_ABI_USE_RESULT] != abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64].pages[IR_ABI_USE_RESULT]);
    u64 other_classifications = abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64].classified_values;
    ir_abi_context_invalidate(&independent);
    independent_result = ir_abi_context_value(&abi_program, &independent, abi_f80, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, independent.classified_values == 2 && independent_result.part_count == 2);
    BUSTER_TEST(arguments, abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64].classified_values == other_classifications);
    // Published immutable tables may supply count without builder capacity.
    IrProgram published_view = {.arena = arguments->arena, .types = {.types = abi_program.types.types, .count = abi_program.types.count}};
    IrAbiValue published_result = ir_type_abi_value(&published_view, abi_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, published_result.part_count == 2 && published_result.parts[0].abi_class == IR_ABI_CLASS_X87);

    // Grow across cache-page boundaries, defer unresolved layout, and give a
    // second compilation identical ids with different language type contents.
    IrProgram cache_program = ir_program_initialize(arguments->arena, 0, 260, 0, 0);
    IrTypeId cache_pending = ir_program_add_type(&cache_program, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 64});
    IrAbiValue pending_value = ir_type_abi_value(&cache_program, cache_pending, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    BUSTER_TEST(arguments, pending_value.part_count == 0 && cache_program.abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64].classified_values == 0);
    cache_program.types.types[cache_pending.value].layout = (IrTypeLayout){.size = 8, .alignment = 8, .resolved = true};
    pending_value = ir_type_abi_value(&cache_program, cache_pending, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    BUSTER_TEST(arguments, pending_value.part_count == 1 && pending_value.parts[0].abi_class == IR_ABI_CLASS_INTEGER);
    IrAbiValue foreign_value = ir_abi_context_value(&cache_program, &independent, cache_pending, IR_ABI_USE_ARGUMENT);
    BUSTER_TEST(arguments, foreign_value.part_count == 0 && independent.classified_values == 2);
    for (u32 type = 1; type < 130; type += 1)
    {
        ir_program_add_type(&cache_program, (IrType){.kind = IR_TYPE_FLOAT, .bit_width = 64,
                                                   .layout = {.size = 8, .alignment = 8, .resolved = true}});
    }
    ir_program_add_type(&cache_program, (IrType){.kind = IR_TYPE_FUNCTION, .calling_convention = IR_CALLING_CONVENTION_WIN64,
                                               .layout = {.size = 8, .alignment = 8, .resolved = true}});
    ir_prepare_program_abi(&cache_program, IR_ABI_CONVENTION_SYSTEMV_X86_64);
    BUSTER_TEST(arguments, cache_program.abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64].classified_values == 1);
    BUSTER_TEST(arguments, cache_program.abi_contexts[IR_ABI_CONVENTION_WIN64_X86_64].classified_values == 0);
    u64 reserved_bytes = 0;
    for (u32 convention = 0; convention < IR_ABI_CONVENTION_COUNT; convention += 1)
    {
        reserved_bytes += cache_program.abi_contexts[convention].allocated_bytes;
    }
    // Two active conventions still reserve much less than the old 848-byte
    // all-convention object per type; no bytes belong to the other targets.
    BUSTER_TEST(arguments, reserved_bytes < (u64)cache_program.types.count * 400);
    BUSTER_TEST(arguments, !cache_program.abi_contexts[IR_ABI_CONVENTION_AAPCS64].arena);
    TemporalArena abi_attempt = arena_begin_temporal(arguments->arena);
    for (u32 attempt = 0; attempt < 2; attempt += 1)
    {
        for (u32 type = 0; type < cache_program.types.count; type += 1)
        {
            for (u32 convention = IR_ABI_CONVENTION_SYSTEMV_X86_64; convention <= IR_ABI_CONVENTION_WIN64_X86_64; convention += 1)
            {
                for (u32 use = 0; use < IR_ABI_USE_COUNT; use += 1)
                {
                    IrAbiValue expected = ir_test_abi_reference(&cache_program, (IrTypeId){type}, (IrAbiConvention)convention, (IrAbiUse)use);
                    IrAbiValue actual = ir_type_abi_value(&cache_program, (IrTypeId){type}, (IrAbiConvention)convention, (IrAbiUse)use);
                    BUSTER_TEST(arguments, memcmp(&actual, &expected, sizeof(actual)) == 0);
                }
            }
        }
        BUSTER_TEST(arguments, arguments->arena->position == abi_attempt.position);
        u8* discarded = arena_allocate(arguments->arena, u8, 16384);
        memset(discarded, 0xcc, 16384);
        scratch_end(abi_attempt);
    }
    // Mutating a language layout invalidates all conventions and dependent
    // aggregate classifications through one explicit program operation.
    cache_program.types.types[cache_pending.value].bit_width = 32;
    cache_program.types.types[cache_pending.value].layout.size = 4;
    ir_program_invalidate_abi(&cache_program);
    pending_value = ir_type_abi_value(&cache_program, cache_pending, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    BUSTER_TEST(arguments, pending_value.parts[0].size == 4);
    pending_value = ir_type_abi_value(&cache_program, cache_pending, IR_ABI_CONVENTION_WIN64_X86_64, IR_ABI_USE_ARGUMENT);
    BUSTER_TEST(arguments, pending_value.parts[0].size == 4);

    IrProgram dependent = ir_program_initialize(arguments->arena, 0, 2, 0, 0);
    IrTypeId dependent_leaf = ir_program_add_type(&dependent, (IrType){.kind = IR_TYPE_FLOAT, .bit_width = 64,
                                                                    .layout = {.size = 8, .alignment = 8, .resolved = true}});
    IrField dependent_field = {.type = dependent_leaf};
    IrTypeId dependent_aggregate = ir_program_add_type(&dependent, (IrType){.kind = IR_TYPE_STRUCT, .fields = &dependent_field, .field_count = 1,
                                                                         .layout = {.size = 8, .alignment = 8, .resolved = true}});
    IrAbiValue dependent_before = ir_type_abi_value(&dependent, dependent_aggregate, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, dependent_before.parts[0].abi_class == IR_ABI_CLASS_FLOAT);
    dependent.types.types[dependent_leaf.value].kind = IR_TYPE_INTEGER;
    ir_program_invalidate_abi(&dependent);
    IrAbiValue dependent_after = ir_type_abi_value(&dependent, dependent_aggregate, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, dependent_after.parts[0].abi_class == IR_ABI_CLASS_INTEGER);

    if (os_get_environment_variable(S8("BUSTER_ABI_CACHE_BENCH")).length)
    {
        // An opt-in classifier/cache microbenchmark; timing never gates tests.
        // The old eager path resolves argument + result for every type and
        // duplicates argument storage for the unused variadic result.
        enum { ABI_BENCH_REPETITIONS = 4096 };
        IrAbiContext measured = ir_abi_context_initialize(arguments->arena, &abi_program.types, IR_ABI_CONVENTION_SYSTEMV_X86_64);
        TimeDataType start = timestamp_take();
        ir_abi_context_reserve(&measured, abi_program.types.count);
        u64 reserve_ns = timestamp_ns_between(start, timestamp_take());
        u64 checksum = 0;
        start = timestamp_take();
        for (u32 type = 0; type < abi_program.types.count; type += 1)
        {
            for (u32 use = 0; use < IR_ABI_USE_VARIADIC_ARGUMENT; use += 1)
            {
                IrAbiValue value = ir_abi_context_value(&abi_program, &measured, (IrTypeId){type}, (IrAbiUse)use);
                checksum += value.part_count + value.parts[0].size;
            }
        }
        u64 cold_ns = timestamp_ns_between(start, timestamp_take());
        start = timestamp_take();
        for (u32 repetition = 0; repetition < ABI_BENCH_REPETITIONS; repetition += 1)
        {
            for (u32 type = 0; type < abi_program.types.count; type += 1)
            {
                for (u32 use = 0; use < IR_ABI_USE_VARIADIC_ARGUMENT; use += 1)
                {
                    IrAbiValue value = ir_abi_context_value(&abi_program, &measured, (IrTypeId){type}, (IrAbiUse)use);
                    checksum += value.part_count + value.parts[0].size;
                }
            }
        }
        u64 warm_ns = timestamp_ns_between(start, timestamp_take());
        start = timestamp_take();
        for (u32 repetition = 0; repetition < ABI_BENCH_REPETITIONS; repetition += 1)
        {
            for (u32 type = 0; type < abi_program.types.count; type += 1)
            {
                for (u32 use = 0; use < IR_ABI_USE_VARIADIC_ARGUMENT; use += 1)
                {
                    IrAbiValue value = ir_test_abi_reference(&abi_program, (IrTypeId){type}, IR_ABI_CONVENTION_SYSTEMV_X86_64, (IrAbiUse)use);
                    checksum += value.part_count + value.parts[0].size;
                }
            }
        }
        u64 uncached_ns = timestamp_ns_between(start, timestamp_take());
        u64 queries = (u64)ABI_BENCH_REPETITIONS * abi_program.types.count * IR_ABI_USE_VARIADIC_ARGUMENT;
        string_print(S8("ABI_CACHE_BENCH types={u32} reserve_ns={u64} cold_ns={u64} warm_ns={u64} uncached_ns={u64} warm_queries={u64} classified={u64} bytes={u64} type_bytes={u64} checksum={u64}\n"),
                     abi_program.types.count, reserve_ns, cold_ns, warm_ns, uncached_ns, queries, measured.classified_values,
                     measured.allocated_bytes, sizeof(IrType) * abi_program.types.count, checksum);
        BUSTER_TEST(arguments, measured.classified_values == (u64)abi_program.types.count * IR_ABI_USE_VARIADIC_ARGUMENT);
    }

    IrValidationResult valid_f80_constant =
        ir_test_canonical_wide_float_constant(arguments->arena, 80, UINT64_C(0x8000000000000001), UINT64_C(0x7fff), 2, 0, 16, 16);
    BUSTER_TEST(arguments, valid_f80_constant.error == IR_VALIDATION_NONE);
    IrValidationResult malformed_f80_count = ir_test_canonical_wide_float_constant(arguments->arena, 80, UINT64_C(1), UINT64_C(0), 1, 0, 16, 16);
    BUSTER_TEST(arguments, malformed_f80_count.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_payload = ir_test_canonical_wide_float_constant(arguments->arena, 80, UINT64_C(1), UINT64_C(0x10000), 2, 0, 16, 16);
    BUSTER_TEST(arguments, malformed_f80_payload.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_extra = ir_test_canonical_wide_float_constant(arguments->arena, 80, UINT64_C(1), UINT64_C(0), 3, 0, 16, 16);
    BUSTER_TEST(arguments, malformed_f80_extra.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_target = ir_test_canonical_wide_float_constant(arguments->arena, 80, UINT64_C(1), UINT64_C(0), 2, 1, 16, 16);
    BUSTER_TEST(arguments, malformed_f80_target.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_layout = ir_test_canonical_wide_float_constant(arguments->arena, 80, UINT64_C(1), UINT64_C(0), 2, 0, 10, 16);
    BUSTER_TEST(arguments, malformed_f80_layout.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_alignment = ir_test_canonical_wide_float_constant(arguments->arena, 80, UINT64_C(1), UINT64_C(0), 2, 0, 16, 8);
    BUSTER_TEST(arguments, malformed_f80_alignment.error == IR_VALIDATION_OPERATION);
    IrValidationResult valid_f128_constant = ir_test_canonical_wide_float_constant(
        arguments->arena, 128, UINT64_C(0x0123456789abcdef), UINT64_C(0x3fff000000000000), 2, 0, 16, 16);
    BUSTER_TEST(arguments, valid_f128_constant.error == IR_VALIDATION_NONE);
    IrValidationResult malformed_f128_count = ir_test_canonical_wide_float_constant(
        arguments->arena, 128, UINT64_C(1), UINT64_C(0), 1, 0, 16, 16);
    BUSTER_TEST(arguments, malformed_f128_count.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f128_extra = ir_test_canonical_wide_float_constant(
        arguments->arena, 128, UINT64_C(1), UINT64_C(0), 3, 0, 16, 16);
    BUSTER_TEST(arguments, malformed_f128_extra.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f128_target = ir_test_canonical_wide_float_constant(
        arguments->arena, 128, UINT64_C(1), UINT64_C(0), 2, 1, 16, 16);
    BUSTER_TEST(arguments, malformed_f128_target.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f128_layout = ir_test_canonical_wide_float_constant(
        arguments->arena, 128, UINT64_C(1), UINT64_C(0), 2, 0, 8, 16);
    BUSTER_TEST(arguments, malformed_f128_layout.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f128_alignment = ir_test_canonical_wide_float_constant(
        arguments->arena, 128, UINT64_C(1), UINT64_C(0), 2, 0, 16, 8);
    BUSTER_TEST(arguments, malformed_f128_alignment.error == IR_VALIDATION_OPERATION);
    IrValidationResult valid_f80_global_bytes = ir_test_canonical_float_global(arguments->arena, 80, IR_GLOBAL_INITIALIZER_BYTES);
    BUSTER_TEST(arguments, valid_f80_global_bytes.error == IR_VALIDATION_NONE);
    IrValidationResult malformed_f80_global_float = ir_test_canonical_float_global(arguments->arena, 80, IR_GLOBAL_INITIALIZER_FLOAT);
    BUSTER_TEST(arguments, malformed_f80_global_float.error == IR_VALIDATION_OPERATION);
    IrValidationResult valid_f16_global_float = ir_test_canonical_float_global(arguments->arena, 16, IR_GLOBAL_INITIALIZER_FLOAT);
    BUSTER_TEST(arguments, valid_f16_global_float.error == IR_VALIDATION_NONE);
    UnitTestResult bfloat16 = ir_test_bfloat16_representation(arguments);
    result.succeeded_test_count += bfloat16.succeeded_test_count;
    result.test_count += bfloat16.test_count;

    String8 c_source = S8("int choose(int a, int b)\n"
                         "{\n"
                         "    int value = a;\n"
                         "    if (a < b) value += b;\n"
                         "    else value -= b;\n"
                         "    return value;\n"
                         "}\n");
    CPreprocessResult preprocess = c_preprocess(arguments->arena, c_source,
                                                (CPreprocessOptions){
                                                    .target = target_native,
                                                    .data_layout = target_data_layout(target_native),
                                                });
    CAnalysisResult analysis = c_parse(arguments->arena, preprocess);
    CIRLowerResult lowered = {0};
    if (!preprocess.error_count && !analysis.diagnostic_count)
    {
        lowered = c_lower_to_ir(arguments->arena, S8("canonical-ir.c"), preprocess, analysis, target_native);
    }
    BUSTER_TEST(arguments, preprocess.error_count == 0);
    BUSTER_TEST(arguments, analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
    BUSTER_TEST(arguments, lowered.program != 0);
    if (lowered.program)
    {
        BUSTER_TEST(arguments, lowered.program->module_count == 1);
        IrModule* module = lowered.program->modules;
        BUSTER_TEST(arguments, module->function_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
        if (module->function_count)
        {
            IrFunction* function = module->functions;
            BUSTER_STRING_TEST(arguments, function->name, S8("choose"));
            BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_BRANCH_IF) == 1);
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_RETURN) >= 1);
            BUSTER_TEST(arguments, ir_test_binary_operation_count(function, IR_BINARY_SIGNED_LESS) == 1);
            // The opcode summary is what lets a consumer skip a scan. A
            // lowered function that holds no atomic and no inline assembly
            // must answer no, and one built row by row must answer yes for
            // exactly the opcode it was given.
            BUSTER_TEST(arguments, (function->opcode_summary & IR_OPCODE_SUMMARY_KNOWN) != 0);
            BUSTER_TEST(arguments, !ir_function_may_contain_opcodes(function, IR_OPCODE_BIT(IR_OPCODE_ATOMIC_LOAD) |
                                                                                 IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)));
        }
    }

    IrProgram summary_program = ir_program_initialize(arguments->arena, 1, 1, 1, 0);
    IrFunction* summary_function = ir_module_add_function(arguments->arena, summary_program.modules, (IrFunction){
                                                                                                        .state = IR_FUNCTION_LOWERED,
                                                                                                    });
    BUSTER_TEST(arguments, summary_function != 0);
    if (summary_function)
    {
        BUSTER_TEST(arguments, summary_function->operand_total == 0);
        BUSTER_TEST(arguments, !ir_function_may_contain_opcodes(summary_function, IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)));
        ir_function_add_instruction(arguments->arena, summary_function,
                                    (IrInstruction){
                                        .result = IR_VALUE_ID_INVALID,
                                        .operand_count = 3,
                                        .opcode = IR_OPCODE_INLINE_ASSEMBLY,
                                        .next = IR_INSTRUCTION_ID_INVALID,
                                    },
                                    (IrSourceRange){0});
        BUSTER_TEST(arguments, summary_function->operand_total_rows != summary_function->instruction_count);
        BUSTER_TEST(arguments, ir_function_may_contain_opcodes(summary_function, IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)));
        BUSTER_TEST(arguments, !ir_function_may_contain_opcodes(summary_function, IR_OPCODE_BIT(IR_OPCODE_ATOMIC_LOAD)));
        ir_function_add_instruction(arguments->arena, summary_function,
                                    (IrInstruction){
                                        .result = IR_VALUE_ID_INVALID,
                                        .operand_count = 1,
                                        .opcode = IR_OPCODE_LABEL_ADDRESS,
                                        .next = IR_INSTRUCTION_ID_INVALID,
                                    },
                                    (IrSourceRange){0});
        BUSTER_TEST(arguments, summary_function->operand_total_rows != summary_function->instruction_count);
        BUSTER_TEST(arguments, ir_function_may_contain_opcodes(summary_function, IR_OPCODE_BIT(IR_OPCODE_LABEL_ADDRESS)));
        BUSTER_TEST(arguments, !ir_function_may_contain_opcodes(summary_function, IR_OPCODE_BIT(IR_OPCODE_INDIRECT_BRANCH)));
    }
    // Rows written straight into `instructions` never reach the builder, so
    // the summary stays unknown and every query answers yes.
    IrFunction unbuilt_function = {0};
    BUSTER_TEST(arguments, ir_function_may_contain_opcodes(&unbuilt_function, IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)));
    UnitTestResult complex_values = ir_complex_value_tests(arguments);
    result.test_count += complex_values.test_count;
    result.succeeded_test_count += complex_values.succeeded_test_count;
    return result;
}
#endif
