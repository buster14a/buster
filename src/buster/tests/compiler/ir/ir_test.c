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

BUSTER_GLOBAL_LOCAL u32 ir_test_direct_call_count(IrProgram* program, IrFunction* function, String8 name)
{
    u32 count = 0;
    for (u32 index = 0; function && index < function->instruction_count; index += 1)
    {
        IrInstruction* call = function->instructions + index;
        if (call->opcode != IR_OPCODE_CALL || !call->operand_count || !call->operands)
        {
            continue;
        }
        IrValueId callee_value = call->operands[0];
        if (callee_value.value >= function->value_count)
        {
            continue;
        }
        IrInstructionId definition = function->values[callee_value.value].definition;
        if (definition.value >= function->instruction_count)
        {
            continue;
        }
        IrInstruction* callee = function->instructions + definition.value;
        IrSymbol* symbol = callee->opcode == IR_OPCODE_FUNCTION ? ir_symbol_from_id(&program->symbols, callee->symbol) : 0;
        count += symbol && string_equal(symbol->name, name);
    }
    return count;
}

#include <buster/tests/compiler/ir/ir_promotion_test.c>
#include <buster/tests/compiler/ir/ir_fast_test.c>
#include <buster/tests/compiler/ir/ir_cfg_test.c>
#include <buster/tests/compiler/ir/ir_integer_test.c>

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
#include <buster/tests/compiler/ir/ir_construction_protocol_test.c>
#include <buster/tests/compiler/ir/ir_label_owner_test.c>
#include <buster/tests/compiler/ir/ir_label_sets_test.c>
#include <buster/tests/compiler/ir/ir_label_paths_test.c>
#include <buster/tests/compiler/ir/ir_validate_test.c>
#include <buster/tests/compiler/ir/ir_label_transfer_test.c>

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

// Exercise the VA operation leaf with complete types and missing table storage.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_va_instruction_type_storage(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // These controls exercise the operation leaf; row and function bounds are
    // validated by their separate canonical IR checks.
    IrType types[] = {
        {.kind = IR_TYPE_VA_LIST},
        {.kind = IR_TYPE_FUNCTION, .is_variadic = true},
        {.kind = IR_TYPE_POINTER, .element_type = {.value = 0}},
        {.kind = IR_TYPE_VOID},
        {.kind = IR_TYPE_INTEGER},
    };
    IrValueId operands[] = {{.value = 0}};
    IrValue values[] = {
        {.canonical_type = {.value = 2}, .category = IR_VALUE_VALUE},
        {.canonical_type = {.value = 0}, .category = IR_VALUE_VALUE},
    };
    IrProgram program = {.types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
    IrFunction function = {.canonical_type = {.value = 1}, .values = values, .value_count = BUSTER_ARRAY_LENGTH(values)};
    typedef struct IrTestVaInstructionCase
    {
        IrOpcode opcode;
        IrTypeId canonical_type;
        IrValueId result;
        u32 operand_count;
    } IrTestVaInstructionCase;
    IrTestVaInstructionCase cases[] = {
        {IR_OPCODE_VA_START, {.value = 0}, {.value = 1}, 0},
        {IR_OPCODE_VA_COPY, {.value = 0}, {.value = 1}, 1},
        {IR_OPCODE_VA_END, {.value = 3}, IR_VALUE_ID_INVALID, 1},
        {IR_OPCODE_VA_ARG, {.value = 4}, {.value = 1}, 1},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        values[1].canonical_type = cases[index].canonical_type;
        IrInstruction instruction = {
            .operands = operands,
            .canonical_type = cases[index].canonical_type,
            .result = cases[index].result,
            .operand_count = cases[index].operand_count,
            .opcode = (u8)cases[index].opcode,
        };
        BUSTER_TEST(arguments, ir_test_validate_va_instruction_operation(&program, &function, types + 1, &instruction) == IR_VALIDATION_NONE);
        program.types.types = 0;
        BUSTER_TEST(arguments,
                    ir_test_validate_va_instruction_operation(&program, &function, types + 1, &instruction) == IR_VALIDATION_OPERATION);
        program.types.types = types;
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


BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_unreachable_payload(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // Complete independent raw IR: only the terminal row changes. Counts
    // always have storage, and all references stay in range.
    for (u32 variant = 0; variant < 6; variant += 1)
    {
        IrTypeId parameter_type = {.value = 1};
        IrType types[] = {
            {.id = {.value = 0}, .kind = IR_TYPE_VOID, .layout = {.alignment = 1, .resolved = true}},
            {.id = {.value = 1}, .kind = IR_TYPE_INTEGER, .bit_width = 32, .is_signed = true,
             .layout = {.size = 4, .alignment = 4, .resolved = true}},
            {.id = {.value = 2}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 0},
             .parameter_types = &parameter_type, .parameter_count = 1},
        };
        u64 immediate = 0;
        IrValueId operand = {.value = 0};
        IrBlockId target = {.value = 0};
        IrValue values[] = {
            {.canonical_type = {.value = 1}, .definition = {.value = 0}, .category = IR_VALUE_VALUE},
            {.canonical_type = {.value = 0}, .definition = {.value = 1}, .category = IR_VALUE_VALUE},
        };
        IrInstruction instructions[] = {
            {.opcode = IR_OPCODE_ARGUMENT, .canonical_type = {.value = 1}, .result = {.value = 0},
             .immediates = &immediate, .immediate_count = 1, .next = {.value = 1}},
            {.opcode = IR_OPCODE_UNREACHABLE, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
             .next = IR_INSTRUCTION_ID_INVALID},
        };
        switch (variant)
        {
            case 0: break;
            case 1: instructions[1].canonical_type.value = 1; break;
            case 2: instructions[1].operands = &operand; instructions[1].operand_count = 1; break;
            case 3: instructions[1].targets = &target; instructions[1].target_count = 1; break;
            case 4: instructions[1].immediates = &immediate; instructions[1].immediate_count = 1; break;
            case 5: instructions[1].result.value = 1; break;
        }
        IrBlock block = {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 1},
                         .sealed = true, .terminated = true};
        IrFunction function = {.id = {.value = 0}, .canonical_type = {.value = 2}, .state = IR_FUNCTION_LOWERED,
                               .entry = {.value = 0}, .blocks = &block, .block_count = 1,
                               .instructions = instructions, .instruction_count = BUSTER_ARRAY_LENGTH(instructions),
                               .values = values, .value_count = variant == 5 ? 2 : 1};
        IrModule module = {.functions = &function, .function_count = 1};
        IrProgram program = {.arena = arguments->arena, .modules = &module, .module_count = 1,
                             .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
        // Byte snapshots: structure assignment need not copy padding.
        u8 saved_instruction[sizeof(instructions[1])];
        u8 saved_block[sizeof(block)];
        memcpy(saved_instruction, &instructions[1], sizeof(saved_instruction));
        memcpy(saved_block, &block, sizeof(saved_block));
        IrValidationError expected = variant ? IR_VALIDATION_OPERATION : IR_VALIDATION_NONE;
        IrValidationResult validation = ir_validate_canonical_module(&program, &module);
        BUSTER_TEST(arguments, validation.error == expected);
        if (variant)
        {
            BUSTER_TEST(arguments, validation.function.value == 0 && validation.block.value == 0 && validation.instruction.value == 1);
        }
        IrValidationResult prepared = ir_prepare_canonical_module(&program, &module, false);
        BUSTER_TEST(arguments, prepared.error == expected);
        BUSTER_TEST(arguments, prepared.boundary == IR_VALIDATION_BOUNDARY_CANONICAL_INPUT);
        if (variant)
        {
            BUSTER_TEST(arguments, prepared.function.value == 0 && prepared.block.value == 0 && prepared.instruction.value == 1);
            BUSTER_TEST(arguments, !function.published_cfg && !module.local_promotion_complete && !module.fast_complete);
            BUSTER_TEST(arguments, memcmp(saved_instruction, &instructions[1], sizeof(saved_instruction)) == 0);
            BUSTER_TEST(arguments, memcmp(saved_block, &block, sizeof(saved_block)) == 0);
        }
        else
        {
            BUSTER_TEST(arguments, function.published_cfg != 0);
            if (function.published_cfg)
            {
                BUSTER_TEST(arguments, function.published_cfg->edge_count == 0);
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(&program, &module).error == IR_VALIDATION_NONE);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_unary_categories_case(UnitTestArguments* arguments, IrUnaryOperation operation,
                                                                         u32 type_index, u32 variant)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    // Complete raw input independent of C lowering and construction helpers.
    // LOCAL remains a PLACE and LOAD remains the explicit value boundary;
    // only the UNARY operand selection or operand/result category changes.
    IrType types[] = {
        {.id = {.value = 0}, .kind = IR_TYPE_INTEGER, .bit_width = 64, .is_signed = true,
         .layout = {.size = 8, .alignment = 8, .resolved = true}},
        {.id = {.value = 1}, .kind = IR_TYPE_FLOAT, .bit_width = 64,
         .layout = {.size = 8, .alignment = 8, .resolved = true}},
        {.id = {.value = 2}, .kind = IR_TYPE_BOOLEAN, .bit_width = 1,
         .layout = {.size = 1, .alignment = 1, .resolved = true}},
        {.id = {.value = 3}, .kind = IR_TYPE_VECTOR, .element_type = {.value = 0}, .element_count = 2,
         .layout = {.size = 16, .alignment = 16, .resolved = true}},
        {.id = {.value = 4}, .kind = IR_TYPE_VECTOR, .element_type = {.value = 1}, .element_count = 2,
         .layout = {.size = 16, .alignment = 16, .resolved = true}},
        {.id = {.value = 5}, .kind = IR_TYPE_VOID},
        {.id = {.value = 6}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 5}},
    };
    IrValueId operands[] = {{.value = 0}, {.value = variant == 1 || variant == 3 ? 0 : 1}};
    IrValue values[] = {
        {.canonical_type = {.value = type_index}, .definition = {.value = 0}, .category = IR_VALUE_PLACE},
        {.canonical_type = {.value = type_index}, .definition = {.value = 1}, .category = IR_VALUE_VALUE},
        {.canonical_type = {.value = type_index}, .definition = {.value = 2}, .category = IR_VALUE_VALUE},
    };
    if (variant == 2 || variant == 3)
    {
        values[2].category = IR_VALUE_PLACE;
    }
    else if (variant == 4)
    {
        values[1].category = IR_VALUE_COUNT;
    }
    else if (variant == 5)
    {
        values[2].category = IR_VALUE_COUNT;
    }
    IrInstruction instructions[] = {
        {.opcode = IR_OPCODE_LOCAL, .canonical_type = {.value = type_index}, .result = {.value = 0},
         .canonical_local = {.value = 0}, .next = {.value = 1}},
        {.opcode = IR_OPCODE_LOAD, .canonical_type = {.value = type_index}, .result = {.value = 1},
         .operands = operands, .operand_count = 1, .next = {.value = 2}},
        {.opcode = IR_OPCODE_UNARY, .unary_operation = (u8)operation, .canonical_type = {.value = type_index},
         .result = {.value = 2}, .operands = operands + 1, .operand_count = 1, .next = {.value = 3}},
        {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 5}, .result = IR_VALUE_ID_INVALID,
         .next = IR_INSTRUCTION_ID_INVALID},
    };
    IrBlock block = {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 3},
                     .sealed = true, .terminated = true};
    IrFunction function = {.id = {.value = 0}, .canonical_type = {.value = 6}, .state = IR_FUNCTION_LOWERED, .entry = {.value = 0},
                           .blocks = &block, .block_count = 1, .instructions = instructions,
                           .instruction_count = BUSTER_ARRAY_LENGTH(instructions), .values = values,
                           .value_count = BUSTER_ARRAY_LENGTH(values), .local_count = 1};
    IrModule module = {.functions = &function, .function_count = 1};
    IrProgram program = {.arena = arguments->arena, .modules = &module, .module_count = 1,
                         .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
    IrValidationResult validation = ir_validate_canonical_module(&program, &module);
    IrValidationError expected = variant ? IR_VALIDATION_OPERATION : IR_VALIDATION_NONE;
    BUSTER_TEST(arguments, validation.error == expected);
    // A missing rejection in the failure-first baseline must not send
    // deliberately malformed values through promotion or publication.
    if (validation.error == expected)
    {
        if (variant)
        {
            BUSTER_TEST(arguments, validation.function.value == 0 && validation.block.value == 0 && validation.instruction.value == 2);
            u8 instruction_snapshot[sizeof(instructions)];
            u8 value_snapshot[sizeof(values)];
            u8 block_snapshot[sizeof(block)];
            memcpy(instruction_snapshot, instructions, sizeof(instructions));
            memcpy(value_snapshot, values, sizeof(values));
            memcpy(block_snapshot, &block, sizeof(block));
            IrValidationResult prepared = ir_prepare_canonical_module(&program, &module, false);
            BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_OPERATION);
            BUSTER_TEST(arguments, prepared.boundary == IR_VALIDATION_BOUNDARY_CANONICAL_INPUT);
            BUSTER_TEST(arguments, prepared.function.value == 0 && prepared.block.value == 0 && prepared.instruction.value == 2);
            BUSTER_TEST(arguments, function.published_cfg == 0 && !module.local_promotion_complete && !module.fast_complete);
            BUSTER_TEST(arguments, function.instruction_count == BUSTER_ARRAY_LENGTH(instructions) &&
                                   function.value_count == BUSTER_ARRAY_LENGTH(values));
            BUSTER_TEST(arguments, memcmp(instruction_snapshot, instructions, sizeof(instructions)) == 0);
            BUSTER_TEST(arguments, memcmp(value_snapshot, values, sizeof(values)) == 0);
            BUSTER_TEST(arguments, memcmp(block_snapshot, &block, sizeof(block)) == 0);
        }
        else
        {
            IrValidationResult prepared = ir_prepare_canonical_module(&program, &module, false);
            BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, prepared.boundary == IR_VALIDATION_BOUNDARY_CANONICAL_INPUT);
            BUSTER_TEST(arguments, function.published_cfg != 0 && module.local_promotion_complete);
            if (function.published_cfg)
            {
                BUSTER_TEST(arguments, function.published_cfg->edge_count == 0);
                BUSTER_TEST(arguments, ir_validate_canonical_module(&program, &module).error == IR_VALIDATION_NONE);
            }
        }
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_unary_categories(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        IrUnaryOperation operation;
        u32 type_index;
    } const cases[] = {
        {IR_UNARY_INTEGER_NEGATE, 0},
        {IR_UNARY_FLOAT_NEGATE, 1},
        {IR_UNARY_INTEGER_BITWISE_NOT, 0},
        {IR_UNARY_INTEGER_COUNT_LEADING_ZEROS, 0},
        {IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS, 0},
        {IR_UNARY_INTEGER_POPULATION_COUNT, 0},
        {IR_UNARY_BOOLEAN_NOT, 2},
        {IR_UNARY_VECTOR_INTEGER_NEGATE, 3},
        {IR_UNARY_VECTOR_FLOAT_NEGATE, 4},
        {IR_UNARY_VECTOR_INTEGER_BITWISE_NOT, 3},
    };
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(cases) == IR_UNARY_COUNT);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        for (u32 variant = 0; variant < 6; variant += 1)
        {
            UnitTestResult cell = ir_test_canonical_unary_categories_case(arguments, cases[index].operation, cases[index].type_index, variant);
            result.test_count += cell.test_count;
            result.succeeded_test_count += cell.succeeded_test_count;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_test_canonical_binary_fixture(IrBinaryOperation operation, u32 operand_type, u32 result_type)
{
    // Independent complete-module data: the arguments, definitions, block
    // ownership and return stay valid while the BINARY family changes.
    IrTypeId parameter_types[] = {{.value = operand_type}, {.value = operand_type}};
    IrType types[] = {
        {.id = {.value = 0}, .kind = IR_TYPE_INTEGER, .bit_width = 64, .is_signed = true,
         .layout = {.size = 8, .alignment = 8, .resolved = true}},
        {.id = {.value = 1}, .kind = IR_TYPE_FLOAT, .bit_width = 64,
         .layout = {.size = 8, .alignment = 8, .resolved = true}},
        {.id = {.value = 2}, .kind = IR_TYPE_BOOLEAN, .bit_width = 1,
         .layout = {.size = 1, .alignment = 1, .resolved = true}},
        {.id = {.value = 3}, .kind = IR_TYPE_POINTER, .element_type = {.value = 0},
         .layout = {.size = 8, .alignment = 8, .resolved = true}},
        {.id = {.value = 4}, .kind = IR_TYPE_VECTOR, .element_type = {.value = 0}, .element_count = 2,
         .layout = {.size = 16, .alignment = 16, .resolved = true}},
        {.id = {.value = 5}, .kind = IR_TYPE_VECTOR, .element_type = {.value = 1}, .element_count = 2,
         .layout = {.size = 16, .alignment = 16, .resolved = true}},
        {.id = {.value = 6}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = result_type},
         .parameter_types = parameter_types, .parameter_count = BUSTER_ARRAY_LENGTH(parameter_types)},
    };
    u64 argument_indices[] = {0, 1};
    IrValueId operands[] = {{.value = 0}, {.value = 1}, {.value = 2}};
    IrValue values[] = {
        {.canonical_type = {.value = operand_type}, .definition = {.value = 0}, .category = IR_VALUE_VALUE},
        {.canonical_type = {.value = operand_type}, .definition = {.value = 1}, .category = IR_VALUE_VALUE},
        {.canonical_type = {.value = result_type}, .definition = {.value = 2}, .category = IR_VALUE_VALUE},
    };
    IrInstruction instructions[] = {
        {.opcode = IR_OPCODE_ARGUMENT, .canonical_type = {.value = operand_type}, .result = {.value = 0},
         .immediates = argument_indices, .immediate_count = 1, .next = {.value = 1}},
        {.opcode = IR_OPCODE_ARGUMENT, .canonical_type = {.value = operand_type}, .result = {.value = 1},
         .immediates = argument_indices + 1, .immediate_count = 1, .next = {.value = 2}},
        {.opcode = IR_OPCODE_BINARY, .binary_operation = (u8)operation, .canonical_type = {.value = result_type},
         .result = {.value = 2}, .operands = operands, .operand_count = 2, .next = {.value = 3}},
        {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = result_type}, .result = IR_VALUE_ID_INVALID,
         .operands = operands + 2, .operand_count = 1, .next = IR_INSTRUCTION_ID_INVALID},
    };
    IrBlock block = {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 3},
                     .sealed = true, .terminated = true};
    IrFunction function = {.canonical_type = {.value = 6}, .state = IR_FUNCTION_LOWERED, .entry = {.value = 0},
                           .blocks = &block, .block_count = 1, .instructions = instructions,
                           .instruction_count = BUSTER_ARRAY_LENGTH(instructions), .values = values,
                           .value_count = BUSTER_ARRAY_LENGTH(values)};
    IrModule module = {.functions = &function, .function_count = 1};
    IrProgram program = {.modules = &module, .module_count = 1,
                         .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
    return ir_validate_canonical_module(&program, &module);
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_binary_families(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    typedef struct IrTestBinaryFamilyCase IrTestBinaryFamilyCase;
    struct IrTestBinaryFamilyCase
    {
        IrBinaryOperation operation;
        bool floating;
        bool comparison;
    };
    IrTestBinaryFamilyCase cases[] = {
        {IR_BINARY_INTEGER_ADD, false, false},
        {IR_BINARY_INTEGER_SUBTRACT, false, false},
        {IR_BINARY_INTEGER_MULTIPLY, false, false},
        {IR_BINARY_SIGNED_DIVIDE, false, false},
        {IR_BINARY_UNSIGNED_DIVIDE, false, false},
        {IR_BINARY_FLOAT_ADD, true, false},
        {IR_BINARY_FLOAT_SUBTRACT, true, false},
        {IR_BINARY_FLOAT_MULTIPLY, true, false},
        {IR_BINARY_FLOAT_DIVIDE, true, false},
        {IR_BINARY_SIGNED_REMAINDER, false, false},
        {IR_BINARY_UNSIGNED_REMAINDER, false, false},
        {IR_BINARY_SHIFT_LEFT, false, false},
        {IR_BINARY_SIGNED_SHIFT_RIGHT, false, false},
        {IR_BINARY_UNSIGNED_SHIFT_RIGHT, false, false},
        {IR_BINARY_INTEGER_BITWISE_AND, false, false},
        {IR_BINARY_INTEGER_BITWISE_OR, false, false},
        {IR_BINARY_INTEGER_BITWISE_XOR, false, false},
        {IR_BINARY_INTEGER_EQUAL, false, true},
        {IR_BINARY_INTEGER_NOT_EQUAL, false, true},
        {IR_BINARY_FLOAT_EQUAL, true, true},
        {IR_BINARY_FLOAT_NOT_EQUAL, true, true},
        {IR_BINARY_SIGNED_LESS, false, true},
        {IR_BINARY_SIGNED_LESS_EQUAL, false, true},
        {IR_BINARY_SIGNED_GREATER, false, true},
        {IR_BINARY_SIGNED_GREATER_EQUAL, false, true},
        {IR_BINARY_UNSIGNED_LESS, false, true},
        {IR_BINARY_UNSIGNED_LESS_EQUAL, false, true},
        {IR_BINARY_UNSIGNED_GREATER, false, true},
        {IR_BINARY_UNSIGNED_GREATER_EQUAL, false, true},
        {IR_BINARY_FLOAT_LESS, true, true},
        {IR_BINARY_FLOAT_LESS_EQUAL, true, true},
        {IR_BINARY_FLOAT_GREATER, true, true},
        {IR_BINARY_FLOAT_GREATER_EQUAL, true, true},
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(cases) == 33);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        for (u32 wrong_family = 0; wrong_family < 2; wrong_family += 1)
        {
            u32 operand_type = (u32)cases[index].floating ^ wrong_family;
            u32 result_type = cases[index].comparison ? 2 : operand_type;
            IrValidationResult validation = ir_test_canonical_binary_fixture(cases[index].operation, operand_type, result_type);
            BUSTER_TEST(arguments, validation.error == (wrong_family ? IR_VALIDATION_OPERATION : IR_VALIDATION_NONE));
            if (wrong_family)
            {
                BUSTER_TEST(arguments, validation.function.value == 0 && validation.block.value == 0 && validation.instruction.value == 2);
            }
        }
    }
    // Preserve the neighboring operation classes and integer/floating vector
    // arithmetic and comparison masks through the same full validator.
    IrBinaryOperation controls[] = {
        IR_BINARY_BOOLEAN_AND, IR_BINARY_BOOLEAN_OR, IR_BINARY_POINTER_EQUAL, IR_BINARY_POINTER_NOT_EQUAL,
        IR_BINARY_VECTOR_INTEGER_ADD, IR_BINARY_VECTOR_FLOAT_ADD, IR_BINARY_VECTOR_INTEGER_EQUAL, IR_BINARY_VECTOR_FLOAT_EQUAL,
    };
    u32 control_operands[] = {2, 2, 3, 3, 4, 5, 4, 5};
    u32 control_results[] = {2, 2, 2, 2, 4, 5, 4, 4};
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(controls) == BUSTER_ARRAY_LENGTH(control_operands));
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(controls) == BUSTER_ARRAY_LENGTH(control_results));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(controls); index += 1)
    {
        IrValidationResult validation = ir_test_canonical_binary_fixture(controls[index], control_operands[index], control_results[index]);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    // Operations in no family stay rejected whatever the operand shape,
    // including every shape some family above accepts.
    IrBinaryOperation outside[] = {IR_BINARY_BOOLEAN_EQUAL, IR_BINARY_BOOLEAN_NOT_EQUAL, IR_BINARY_RANGE, IR_BINARY_COUNT};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(outside); index += 1)
    {
        for (u32 operand_type = 0; operand_type < 6; operand_type += 1)
        {
            for (u32 comparison = 0; comparison < 2; comparison += 1)
            {
                IrValidationResult validation = ir_test_canonical_binary_fixture(outside[index], operand_type, comparison ? 2 : operand_type);
                BUSTER_TEST(arguments, validation.error == IR_VALIDATION_OPERATION);
                BUSTER_TEST(arguments, validation.function.value == 0 && validation.block.value == 0 && validation.instruction.value == 2);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_binary_categories_case(UnitTestArguments* arguments, IrBinaryOperation operation,
                                                                          u32 operand_type, u32 result_type)
{
    UnitTestResult result = {0};
    for (u32 variant = 0; variant < 9; variant += 1)
    {
        // Raw complete-module data, independent of the builder/frontend:
        // LOCAL defines a place; LOAD defines the corresponding value.
        IrType types[] = {
            {.id = {.value = 0}, .kind = IR_TYPE_INTEGER, .bit_width = 64, .is_signed = true,
             .layout = {.size = 8, .alignment = 8, .resolved = true}},
            {.id = {.value = 1}, .kind = IR_TYPE_FLOAT, .bit_width = 64,
             .layout = {.size = 8, .alignment = 8, .resolved = true}},
            {.id = {.value = 2}, .kind = IR_TYPE_BOOLEAN, .bit_width = 1,
             .layout = {.size = 1, .alignment = 1, .resolved = true}},
            {.id = {.value = 3}, .kind = IR_TYPE_POINTER, .element_type = {.value = 0},
             .layout = {.size = 8, .alignment = 8, .resolved = true}},
            {.id = {.value = 4}, .kind = IR_TYPE_VECTOR, .element_type = {.value = 0}, .element_count = 2,
             .layout = {.size = 16, .alignment = 16, .resolved = true}},
            {.id = {.value = 5}, .kind = IR_TYPE_VECTOR, .element_type = {.value = 1}, .element_count = 2,
             .layout = {.size = 16, .alignment = 16, .resolved = true}},
            {.id = {.value = 6}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 7}},
            {.id = {.value = 7}, .kind = IR_TYPE_VOID},
        };
        IrValueId places[] = {{.value = 0}, {.value = 1}};
        bool uses_memory[] = {true, true};
        u32 place_mask = variant < 8 ? variant : 0;
        IrValueId binary_operands[] = {{.value = place_mask & 1 ? 0 : 2}, {.value = place_mask & 2 ? 1 : 3}};
        IrValue values[] = {
            {.canonical_type = {.value = operand_type}, .definition = {.value = 0}, .category = IR_VALUE_PLACE},
            {.canonical_type = {.value = operand_type}, .definition = {.value = 1}, .category = IR_VALUE_PLACE},
            {.canonical_type = {.value = operand_type}, .definition = {.value = 2}, .category = IR_VALUE_VALUE},
            {.canonical_type = {.value = operand_type}, .definition = {.value = 3}, .category = IR_VALUE_VALUE},
            {.canonical_type = {.value = result_type}, .definition = {.value = 4},
             .category = variant == 8 ? IR_VALUE_COUNT : (place_mask & 4 ? IR_VALUE_PLACE : IR_VALUE_VALUE)},
        };
        IrInstruction instructions[] = {
            {.opcode = IR_OPCODE_LOCAL, .canonical_type = {.value = operand_type}, .canonical_local = {.value = 0},
             .result = {.value = 0}, .next = {.value = 1}},
            {.opcode = IR_OPCODE_LOCAL, .canonical_type = {.value = operand_type}, .canonical_local = {.value = 1},
             .result = {.value = 1}, .next = {.value = 2}},
            {.opcode = IR_OPCODE_LOAD, .canonical_type = {.value = operand_type},
             .operands = places, .operand_count = 1, .result = {.value = 2}, .next = {.value = 3}},
            {.opcode = IR_OPCODE_LOAD, .canonical_type = {.value = operand_type},
             .operands = places + 1, .operand_count = 1, .result = {.value = 3}, .next = {.value = 4}},
            {.opcode = IR_OPCODE_BINARY, .binary_operation = (u8)operation, .canonical_type = {.value = result_type},
             .operands = binary_operands, .operand_count = 2, .result = {.value = 4}, .next = {.value = 5}},
            // The binary result is unused: RETURN cannot mask its category.
            {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 7}, .result = IR_VALUE_ID_INVALID,
             .next = IR_INSTRUCTION_ID_INVALID},
        };
        IrBlock block = {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 5},
                         .sealed = true, .terminated = true};
        IrFunction function = {.canonical_type = {.value = 6}, .state = IR_FUNCTION_LOWERED, .entry = {.value = 0},
                               .blocks = &block, .block_count = 1, .instructions = instructions,
                               .instruction_count = BUSTER_ARRAY_LENGTH(instructions), .values = values,
                               .value_count = BUSTER_ARRAY_LENGTH(values), .local_places = places,
                               .local_uses_memory = uses_memory, .local_count = BUSTER_ARRAY_LENGTH(places)};
        IrModule module = {.functions = &function, .function_count = 1};
        IrProgram program = {.arena = arguments->arena, .modules = &module, .module_count = 1,
                             .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
        IrValidationResult validation = ir_validate_canonical_module(&program, &module);
        BUSTER_TEST(arguments, validation.error == (variant ? IR_VALIDATION_OPERATION : IR_VALIDATION_NONE));
        if (variant && validation.error == IR_VALIDATION_OPERATION)
        {
            BUSTER_TEST(arguments, validation.function.value == 0 && validation.block.value == 0 && validation.instruction.value == 4);
            IrValidationResult prepared = ir_prepare_canonical_module(&program, &module, false);
            BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_OPERATION);
            BUSTER_TEST(arguments, prepared.function.value == 0 && prepared.block.value == 0 && prepared.instruction.value == 4);
            BUSTER_TEST(arguments, prepared.boundary == IR_VALIDATION_BOUNDARY_CANONICAL_INPUT);
            BUSTER_TEST(arguments, !function.published_cfg && !module.local_promotion_complete && !module.fast_complete);
            BUSTER_TEST(arguments, instructions[4].operands == binary_operands && instructions[4].result.value == 4 &&
                                   values[4].category == (variant == 8 ? IR_VALUE_COUNT : (place_mask & 4 ? IR_VALUE_PLACE : IR_VALUE_VALUE)));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_binary_categories(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    typedef struct IrTestBinaryCategoryCase IrTestBinaryCategoryCase;
    struct IrTestBinaryCategoryCase
    {
        IrBinaryOperation operation;
        u32 operand_type;
        u32 result_type;
    };
    // Explicit operation/type expectations, including every scalar family
    // and the neighboring Boolean, pointer and vector controls.
    IrTestBinaryCategoryCase cases[] = {
        {IR_BINARY_INTEGER_ADD, 0, 0},
        {IR_BINARY_INTEGER_SUBTRACT, 0, 0},
        {IR_BINARY_INTEGER_MULTIPLY, 0, 0},
        {IR_BINARY_SIGNED_DIVIDE, 0, 0},
        {IR_BINARY_UNSIGNED_DIVIDE, 0, 0},
        {IR_BINARY_FLOAT_ADD, 1, 1},
        {IR_BINARY_FLOAT_SUBTRACT, 1, 1},
        {IR_BINARY_FLOAT_MULTIPLY, 1, 1},
        {IR_BINARY_FLOAT_DIVIDE, 1, 1},
        {IR_BINARY_SIGNED_REMAINDER, 0, 0},
        {IR_BINARY_UNSIGNED_REMAINDER, 0, 0},
        {IR_BINARY_SHIFT_LEFT, 0, 0},
        {IR_BINARY_SIGNED_SHIFT_RIGHT, 0, 0},
        {IR_BINARY_UNSIGNED_SHIFT_RIGHT, 0, 0},
        {IR_BINARY_INTEGER_BITWISE_AND, 0, 0},
        {IR_BINARY_INTEGER_BITWISE_OR, 0, 0},
        {IR_BINARY_INTEGER_BITWISE_XOR, 0, 0},
        {IR_BINARY_INTEGER_EQUAL, 0, 2},
        {IR_BINARY_INTEGER_NOT_EQUAL, 0, 2},
        {IR_BINARY_FLOAT_EQUAL, 1, 2},
        {IR_BINARY_FLOAT_NOT_EQUAL, 1, 2},
        {IR_BINARY_SIGNED_LESS, 0, 2},
        {IR_BINARY_SIGNED_LESS_EQUAL, 0, 2},
        {IR_BINARY_SIGNED_GREATER, 0, 2},
        {IR_BINARY_SIGNED_GREATER_EQUAL, 0, 2},
        {IR_BINARY_UNSIGNED_LESS, 0, 2},
        {IR_BINARY_UNSIGNED_LESS_EQUAL, 0, 2},
        {IR_BINARY_UNSIGNED_GREATER, 0, 2},
        {IR_BINARY_UNSIGNED_GREATER_EQUAL, 0, 2},
        {IR_BINARY_FLOAT_LESS, 1, 2},
        {IR_BINARY_FLOAT_LESS_EQUAL, 1, 2},
        {IR_BINARY_FLOAT_GREATER, 1, 2},
        {IR_BINARY_FLOAT_GREATER_EQUAL, 1, 2},
        {IR_BINARY_BOOLEAN_AND, 2, 2},
        {IR_BINARY_BOOLEAN_OR, 2, 2},
        {IR_BINARY_POINTER_EQUAL, 3, 2},
        {IR_BINARY_POINTER_NOT_EQUAL, 3, 2},
        {IR_BINARY_VECTOR_INTEGER_ADD, 4, 4},
        {IR_BINARY_VECTOR_FLOAT_ADD, 5, 5},
        {IR_BINARY_VECTOR_INTEGER_EQUAL, 4, 4},
        {IR_BINARY_VECTOR_FLOAT_EQUAL, 5, 4},
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(cases) == 41);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        UnitTestResult categories = ir_test_canonical_binary_categories_case(arguments, cases[index].operation,
                                                                            cases[index].operand_type, cases[index].result_type);
        result.test_count += categories.test_count;
        result.succeeded_test_count += categories.succeeded_test_count;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_array_categories(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // The positive rows cover nonempty ARRAY, VECTOR and an empty ARRAY.
    // Each failure changes only one category in an otherwise complete module.
    for (u32 variant = 0; variant < 9; variant += 1)
    {
        IrType types[] = {
            {.id = {.value = 0}, .kind = IR_TYPE_VOID, .layout = {.alignment = 1, .resolved = true}},
            {.id = {.value = 1}, .kind = IR_TYPE_INTEGER, .bit_width = 64, .is_signed = true,
             .layout = {.size = 8, .alignment = 8, .resolved = true}},
            {.id = {.value = 2}, .kind = IR_TYPE_ARRAY, .element_type = {.value = 1}, .element_count = 1,
             .layout = {.size = 8, .alignment = 8, .resolved = true}},
            {.id = {.value = 3}, .kind = IR_TYPE_VECTOR, .element_type = {.value = 1}, .element_count = 2,
             .layout = {.size = 16, .alignment = 16, .resolved = true}},
            {.id = {.value = 4}, .kind = IR_TYPE_ARRAY, .element_type = {.value = 1}, .element_count = 0,
             .layout = {.size = 0, .alignment = 8, .resolved = true}},
            {.id = {.value = 5}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 0}},
        };
        u32 array_type_index = variant < 3 ? 2 : variant < 7 ? 3 : 4;
        u32 array_operand_count = variant < 3 ? 1 : variant < 7 ? 2 : 0;
        bool bad_operand = variant == 1 || variant == 4 || variant == 5;
        bool bad_result = variant == 2 || variant == 6 || variant == 8;
        IrValueId local_places[] = {{.value = 0}, {.value = 1}};
        bool local_uses_memory[] = {true, true};
        IrValueId value_operands[] = {{.value = 2}, {.value = 3}};
        IrValueId place_operands[] = {{.value = 0}, {.value = 1}};
        IrValueId first_place_operand[] = {{.value = 0}, {.value = 3}};
        IrValueId last_place_operand[] = {{.value = 2}, {.value = 1}};
        IrValueId* construction_operands = value_operands;
        if (bad_operand)
        {
            construction_operands = variant == 4 ? first_place_operand : variant == 5 ? last_place_operand : place_operands;
        }
        IrValue values[] = {
            {.canonical_type = {.value = 1}, .definition = {.value = 0}, .category = IR_VALUE_PLACE},
            {.canonical_type = {.value = 1}, .definition = {.value = 1}, .category = IR_VALUE_PLACE},
            {.canonical_type = {.value = 1}, .definition = {.value = 2}, .category = IR_VALUE_VALUE},
            {.canonical_type = {.value = 1}, .definition = {.value = 3}, .category = IR_VALUE_VALUE},
            {.canonical_type = {.value = array_type_index}, .definition = {.value = 4},
             .category = bad_result ? IR_VALUE_PLACE : IR_VALUE_VALUE},
        };
        IrInstruction instructions[] = {
            {.opcode = IR_OPCODE_LOCAL, .canonical_type = {.value = 1}, .canonical_local = {.value = 0},
             .result = {.value = 0}, .next = {.value = 1}},
            {.opcode = IR_OPCODE_LOCAL, .canonical_type = {.value = 1}, .canonical_local = {.value = 1},
             .result = {.value = 1}, .next = {.value = 2}},
            {.opcode = IR_OPCODE_LOAD, .canonical_type = {.value = 1}, .operands = local_places,
             .operand_count = 1, .result = {.value = 2}, .next = {.value = 3}},
            {.opcode = IR_OPCODE_LOAD, .canonical_type = {.value = 1}, .operands = local_places + 1,
             .operand_count = 1, .result = {.value = 3}, .next = {.value = 4}},
            {.opcode = IR_OPCODE_ARRAY, .canonical_type = {.value = array_type_index},
             .operands = array_operand_count ? construction_operands : 0,
             .operand_count = array_operand_count, .result = {.value = 4}, .next = {.value = 5}},
            {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
             .next = IR_INSTRUCTION_ID_INVALID},
        };
        IrBlock block = {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 5},
                         .sealed = true, .terminated = true};
        IrFunction function = {.id = {.value = 0}, .canonical_type = {.value = 5}, .state = IR_FUNCTION_LOWERED,
                               .entry = {.value = 0}, .blocks = &block, .block_count = 1, .instructions = instructions,
                               .instruction_count = BUSTER_ARRAY_LENGTH(instructions), .values = values,
                               .value_count = BUSTER_ARRAY_LENGTH(values), .local_places = local_places,
                               .local_uses_memory = local_uses_memory, .local_count = BUSTER_ARRAY_LENGTH(local_places)};
        IrModule module = {.functions = &function, .function_count = 1};
        IrProgram program = {.arena = arguments->arena, .modules = &module, .module_count = 1,
                             .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
        bool malformed = bad_operand || bad_result;
        u8 instruction_snapshot[sizeof(instructions[4])];
        u8 result_snapshot[sizeof(values[4])];
        u8 block_snapshot[sizeof(block)];
        memcpy(instruction_snapshot, instructions + 4, sizeof(instruction_snapshot));
        memcpy(result_snapshot, values + 4, sizeof(result_snapshot));
        memcpy(block_snapshot, &block, sizeof(block_snapshot));
        IrValidationError expected = malformed ? IR_VALIDATION_OPERATION : IR_VALIDATION_NONE;
        IrValidationResult validation = ir_validate_canonical_module(&program, &module);
        IrValidationResult reference = ir_test_validate_canonical_module_reference(&program, &module);
        BUSTER_TEST(arguments, validation.error == expected);
        BUSTER_TEST(arguments, reference.error == expected);
        if (malformed && validation.error == expected && reference.error == expected)
        {
            BUSTER_TEST(arguments, validation.function.value == 0 && validation.block.value == 0 && validation.instruction.value == 4);
            BUSTER_TEST(arguments, reference.function.value == 0 && reference.block.value == 0 && reference.instruction.value == 4);
            IrValidationResult prepared = ir_prepare_canonical_module(&program, &module, false);
            BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_OPERATION);
            BUSTER_TEST(arguments, prepared.function.value == 0 && prepared.block.value == 0 && prepared.instruction.value == 4);
            BUSTER_TEST(arguments, prepared.boundary == IR_VALIDATION_BOUNDARY_CANONICAL_INPUT);
            BUSTER_TEST(arguments, !function.published_cfg && !module.local_promotion_complete && !module.fast_complete);
            BUSTER_TEST(arguments, memcmp(instruction_snapshot, instructions + 4, sizeof(instruction_snapshot)) == 0);
            BUSTER_TEST(arguments, memcmp(result_snapshot, values + 4, sizeof(result_snapshot)) == 0);
            BUSTER_TEST(arguments, memcmp(block_snapshot, &block, sizeof(block_snapshot)) == 0);
        }
        else if (!malformed && validation.error == IR_VALIDATION_NONE)
        {
            IrValidationResult prepared = ir_prepare_canonical_module(&program, &module, false);
            BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, function.published_cfg != 0);
            BUSTER_TEST(arguments, ir_validate_canonical_module(&program, &module).error == IR_VALIDATION_NONE);
        }
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

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_switch_case(UnitTestArguments* arguments, u32 bit_width, bool is_signed,
                                                                u64* keys, u32 case_count, bool shared_target, IrValidationError expected)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    IrTypeId parameter_type = {.value = 1};
    IrType types[] = {
        {.id = {.value = 0}, .kind = IR_TYPE_VOID},
        {.id = {.value = 1}, .kind = IR_TYPE_INTEGER, .bit_width = (u16)bit_width, .is_signed = is_signed,
         .layout = {.size = bit_width / 8, .alignment = bit_width / 8, .resolved = true}},
        {.id = {.value = 2}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 0},
         .parameter_types = &parameter_type, .parameter_count = 1},
    };
    u64 argument_index = 0;
    IrValueId selector = {.value = 0};
    IrValue value = {.canonical_type = {.value = 1}, .definition = {.value = 0}, .category = IR_VALUE_VALUE};
    IrBlockId* targets = arena_allocate(temporary.arena, IrBlockId, case_count + 1);
    u64* original_keys = arena_allocate(temporary.arena, u64, case_count);
    IrBlockId* original_targets = arena_allocate(temporary.arena, IrBlockId, case_count + 1);
    for (u32 index = 0; index < case_count; index += 1)
    {
        targets[index].value = shared_target ? 1 : 1 + index % 2;
        original_keys[index] = keys[index];
        original_targets[index] = targets[index];
    }
    targets[case_count].value = 3;
    original_targets[case_count] = targets[case_count];
    // Raw original rows: default is last; distinct keys may share a target.
    IrInstruction instructions[] = {
        {.opcode = IR_OPCODE_ARGUMENT, .canonical_type = {.value = 1}, .result = {.value = 0},
         .immediates = &argument_index, .immediate_count = 1, .next = {.value = 1}},
        {.opcode = IR_OPCODE_SWITCH, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
         .operands = &selector, .operand_count = 1, .immediates = keys, .immediate_count = (u16)case_count,
         .targets = targets, .target_count = (u16)(case_count + 1), .next = IR_INSTRUCTION_ID_INVALID},
        {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID, .next = IR_INSTRUCTION_ID_INVALID},
        {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID, .next = IR_INSTRUCTION_ID_INVALID},
        {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID, .next = IR_INSTRUCTION_ID_INVALID},
    };
    IrBlock blocks[] = {
        {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 1}, .sealed = true, .terminated = true},
        {.id = {.value = 1}, .first_instruction = {.value = 2}, .last_instruction = {.value = 2}, .sealed = true, .terminated = true},
        {.id = {.value = 2}, .first_instruction = {.value = 3}, .last_instruction = {.value = 3}, .sealed = true, .terminated = true},
        {.id = {.value = 3}, .first_instruction = {.value = 4}, .last_instruction = {.value = 4}, .sealed = true, .terminated = true},
    };
    IrFunction function = {.canonical_type = {.value = 2}, .state = IR_FUNCTION_LOWERED, .entry = {.value = 0},
                           .blocks = blocks, .block_count = BUSTER_ARRAY_LENGTH(blocks), .instructions = instructions,
                           .instruction_count = BUSTER_ARRAY_LENGTH(instructions), .values = &value, .value_count = 1};
    IrModule module = {.functions = &function, .function_count = 1};
    IrProgram program = {.arena = temporary.arena, .modules = &module, .module_count = 1,
                         .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
    IrValidationResult validation = ir_validate_canonical_module(&program, &module);
    BUSTER_TEST(arguments, validation.error == expected);
    if (expected != IR_VALIDATION_NONE && validation.error == IR_VALIDATION_BRANCH_TARGET)
    {
        BUSTER_TEST(arguments, validation.function.value == 0 && validation.block.value == 0 && validation.instruction.value == 1);
        IrValidationResult prepared = ir_prepare_canonical_module(&program, &module, false);
        BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_BRANCH_TARGET);
        BUSTER_TEST(arguments, prepared.function.value == 0 && prepared.block.value == 0 && prepared.instruction.value == 1);
        BUSTER_TEST(arguments, prepared.boundary == IR_VALIDATION_BOUNDARY_CANONICAL_INPUT);
        BUSTER_TEST(arguments, !function.published_cfg && !module.local_promotion_complete && !module.fast_complete);
    }
    bool keys_unchanged = true;
    bool targets_unchanged = true;
    for (u32 index = 0; index < case_count; index += 1)
    {
        keys_unchanged &= keys[index] == original_keys[index];
    }
    for (u32 index = 0; index <= case_count; index += 1)
    {
        targets_unchanged &= targets[index].value == original_targets[index].value;
    }
    BUSTER_TEST(arguments, keys_unchanged && targets_unchanged);
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_switch_keys(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    typedef struct IrTestSwitchKeysCase IrTestSwitchKeysCase;
    struct IrTestSwitchKeysCase
    {
        u64 keys[5];
        u32 count;
        u32 bit_width;
        bool is_signed;
        bool shared_target;
        bool duplicate;
    };
    IrTestSwitchKeysCase cases[] = {
        {{0}, 0, 32, false, false, false},
        {{7}, 1, 32, false, false, false},
        {{263}, 1, 8, false, false, false},
        {{1, 7, 9}, 3, 32, false, false, false},
        {{9, 7, 1}, 3, 32, false, false, false},
        {{7, 1, 9}, 3, 32, false, false, false},
        {{7, 1, 9}, 3, 32, false, true, false},
        {{7, 7}, 2, 32, false, false, true},
        {{7, 7}, 2, 32, false, true, true},
        {{7, 9, 7}, 3, 32, false, false, true},
        {{7, 9, 7}, 3, 32, false, true, true},
        {{7, 263}, 2, 8, false, false, true},
        {{263, 7}, 2, 8, false, false, true},
        {{UINT64_C(0xff), UINT64_MAX}, 2, 8, true, false, true},
        {{UINT64_MAX, 0, 1}, 3, 8, true, false, false},
        {{7, UINT64_C(65543)}, 2, 16, false, false, true},
        {{UINT64_C(65535), UINT64_MAX}, 2, 16, true, false, true},
        {{7, UINT64_C(0x10000000007)}, 2, 64, false, false, false},
        {{7, UINT64_C(0x10000000007)}, 2, 32, false, false, true},
        {{UINT64_C(0xffffffff), UINT64_MAX}, 2, 32, true, false, true},
        {{0, UINT64_MAX, UINT64_C(0x8000000000000000), UINT64_C(0x7fffffffffffffff)}, 4, 64, true, false, false},
        {{0, UINT64_MAX, UINT64_C(0x8000000000000000), UINT64_C(0x7fffffffffffffff)}, 4, 64, false, false, false},
        {{0, UINT64_MAX, UINT64_C(0x8000000000000000), UINT64_MAX}, 4, 64, true, false, true},
        {{0, UINT64_MAX, UINT64_C(0x8000000000000000), UINT64_MAX}, 4, 64, false, true, true},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        IrTestSwitchKeysCase* test = cases + index;
        UnitTestResult keys = ir_test_canonical_switch_case(arguments, test->bit_width, test->is_signed, test->keys, test->count,
                                                          test->shared_target, test->duplicate ? IR_VALIDATION_BRANCH_TARGET : IR_VALIDATION_NONE);
        result.test_count += keys.test_count;
        result.succeeded_test_count += keys.succeeded_test_count;
    }
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    enum { IR_TEST_SWITCH_KEY_COUNT = 4096 };
    u64* large_keys = arena_allocate(temporary.arena, u64, IR_TEST_SWITCH_KEY_COUNT);
    for (u32 order = 0; order < 4; order += 1)
    {
        for (u32 index = 0; index < IR_TEST_SWITCH_KEY_COUNT; index += 1)
        {
            // Multiplication by an odd number permutes residues modulo 4096.
            u64 key = order == 0 ? index : (order == 1 ? IR_TEST_SWITCH_KEY_COUNT - 1 - index : (UINT64_C(2053) * index) & (IR_TEST_SWITCH_KEY_COUNT - 1));
            large_keys[index] = order == 3 ? (key << 48) | (key << 16) | 7 : key;
        }
        UnitTestResult valid = ir_test_canonical_switch_case(arguments, 64, false, large_keys, IR_TEST_SWITCH_KEY_COUNT,
                                                           order == 3, IR_VALIDATION_NONE);
        result.test_count += valid.test_count;
        result.succeeded_test_count += valid.succeeded_test_count;
        large_keys[IR_TEST_SWITCH_KEY_COUNT - 1] = large_keys[0];
        UnitTestResult duplicate = ir_test_canonical_switch_case(arguments, 64, false, large_keys, IR_TEST_SWITCH_KEY_COUNT,
                                                               order == 3, IR_VALIDATION_BRANCH_TARGET);
        result.test_count += duplicate.test_count;
        result.succeeded_test_count += duplicate.succeeded_test_count;
    }
    scratch_end(temporary);
    return result;
}

// Independent mutable and dense backing for the same two-edge join. Poison
// scratch before every validation; only nonempty metadata needs a provenance
// calculation after the structural incoming checks have succeeded.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_parameter_validation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    enum
    {
        IR_TEST_PARAMETER_VALID,
        IR_TEST_PARAMETER_ID,
        IR_TEST_PARAMETER_TYPE,
        IR_TEST_PARAMETER_INCOMING_ID,
        IR_TEST_PARAMETER_INCOMING_TYPE,
        IR_TEST_PARAMETER_SHORT,
        IR_TEST_PARAMETER_LONG,
        IR_TEST_PARAMETER_MISSING,
        IR_TEST_PARAMETER_EXTRA,
        IR_TEST_PARAMETER_ORDER,
        IR_TEST_PARAMETER_TAIL,
        IR_TEST_PARAMETER_NULL_TAIL,
        IR_TEST_PARAMETER_PROVENANCE,
        IR_TEST_PARAMETER_COUNT,
    };
    for (u32 published = 0; published < 2; published += 1)
    {
        for (u32 metadata_kind = 0; metadata_kind < 3; metadata_kind += 1)
        {
            for (u32 variant = 0; variant < IR_TEST_PARAMETER_COUNT; variant += 1)
            {
                if ((variant == IR_TEST_PARAMETER_PROVENANCE && metadata_kind != 2) ||
                    (published && (variant == IR_TEST_PARAMETER_TAIL || variant == IR_TEST_PARAMETER_NULL_TAIL)))
                {
                    continue;
                }
                bool labels = metadata_kind == 2;
                IrType types[] = {
                    {.id = {.value = 0}, .kind = IR_TYPE_VOID, .layout = {.alignment = 1, .resolved = true}},
                    {.id = {.value = 1}, .kind = labels ? IR_TYPE_POINTER : IR_TYPE_INTEGER, .bit_width = 32,
                     .element_type = {.value = 0}, .layout = {.size = labels ? 8 : 4, .alignment = labels ? 8 : 4, .resolved = true}},
                    {.id = {.value = 2}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 1}},
                    {.id = {.value = 3}, .kind = labels ? IR_TYPE_POINTER : IR_TYPE_INTEGER, .bit_width = 64,
                     .element_type = {.value = 0}, .layout = {.size = 8, .alignment = 8, .resolved = true}},
                };
                IrValue values[] = {
                    {.canonical_type = {.value = 1}, .definition = {.value = 0}, .category = IR_VALUE_VALUE},
                    {.canonical_type = {.value = 1}, .definition = {.value = 2}, .category = IR_VALUE_VALUE},
                    {.canonical_type = {.value = 1}, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE},
                };
                u64 immediates[] = {10, 20};
                IrBlockId target = {.value = 2};
                IrBlockId label_blocks[] = {{.value = 0}, {.value = 1}};
                IrValueId returned = {.value = 2};
                IrInstruction instructions[] = {
                    {.opcode = labels ? IR_OPCODE_LABEL_ADDRESS : IR_OPCODE_CONSTANT_INTEGER, .canonical_type = {.value = 1},
                     .result = {.value = 0}, .next = {.value = 1}, .immediates = labels ? 0 : immediates,
                     .immediate_count = (u16)!labels, .targets = labels ? label_blocks : 0, .target_count = (u16)labels},
                    {.opcode = IR_OPCODE_BRANCH, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
                     .next = IR_INSTRUCTION_ID_INVALID, .targets = &target, .target_count = 1},
                    {.opcode = labels ? IR_OPCODE_LABEL_ADDRESS : IR_OPCODE_CONSTANT_INTEGER, .canonical_type = {.value = 1},
                     .result = {.value = 1}, .next = {.value = 3}, .immediates = labels ? 0 : immediates + 1,
                     .immediate_count = (u16)!labels, .targets = labels ? label_blocks + 1 : 0, .target_count = (u16)labels},
                    {.opcode = IR_OPCODE_BRANCH, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
                     .next = IR_INSTRUCTION_ID_INVALID, .targets = &target, .target_count = 1},
                    {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
                     .next = IR_INSTRUCTION_ID_INVALID, .operands = &returned, .operand_count = 1},
                };
                IrPredecessor predecessors[] = {{.block = {.value = 0}}, {.block = {.value = 1}}};
                predecessors[0].next = predecessors + 1;
                IrIncoming incoming[] = {
                    {.predecessor = {.value = 0}, .value = {.value = 0}},
                    {.predecessor = {.value = 1}, .value = {.value = 1}},
                    {.predecessor = {.value = 1}, .value = {.value = 1}},
                };
                incoming[0].next = incoming + 1;
                IrBlockParameter parameter = {.value = {.value = 2}, .canonical_type = {.value = 1},
                    .incoming_count = 2, .first_incoming = incoming, .last_incoming = incoming + 1};
                IrBlock blocks[] = {
                    {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 1}, .terminated = true, .sealed = true},
                    {.id = {.value = 1}, .first_instruction = {.value = 2}, .last_instruction = {.value = 3}, .terminated = true, .sealed = true},
                    {.id = {.value = 2}, .first_instruction = {.value = 4}, .last_instruction = {.value = 4}, .terminated = true, .sealed = true,
                     .parameter_count = 1, .predecessor_count = 2, .first_parameter = &parameter, .last_parameter = &parameter,
                     .first_predecessor = predecessors, .last_predecessor = predecessors + 1},
                };
                IrCfgBlock cfg_blocks[] = {
                    {.first_instruction = 0, .instruction_count = 2, .successor_count = 1},
                    {.first_instruction = 2, .instruction_count = 2, .successor_offset = 1, .successor_count = 1},
                    {.first_instruction = 4, .instruction_count = 1, .successor_offset = 2, .predecessor_count = 2, .parameter_count = 1},
                };
                IrCfgEdge edges[] = {
                    {.source = {.value = 0}, .destination = {.value = 2}, .argument_offset = 0},
                    {.source = {.value = 1}, .destination = {.value = 2}, .argument_offset = 1},
                };
                u32 predecessor_edges[] = {0, 1};
                IrCfgParameter cfg_parameter = {.value = {.value = 2}, .canonical_type = {.value = 1}};
                IrValueId cfg_arguments[] = {{.value = 0}, {.value = 1}, {.value = 1}};
                IrPublishedCfg cfg = {.arena = arguments->arena, .blocks = cfg_blocks, .edges = edges,
                    .predecessors = predecessor_edges, .parameters = &cfg_parameter, .arguments = cfg_arguments,
                    .block_count = 3, .instruction_count = 5, .edge_count = 2, .parameter_count = 1, .argument_count = 2};
                IrValueId metadata_values[] = {{.value = 0}, {.value = 1}, {.value = 2}};
                IrValueLabelMetadata metadata[] = {
                    {.is_label_value = true, .label_blocks = label_blocks, .label_block_count = 1},
                    {.is_label_value = true, .label_blocks = label_blocks + 1, .label_block_count = 1},
                    {.is_label_value = true, .label_blocks = label_blocks, .label_block_count = 2},
                };
                IrFunction function = {.id = {.value = 7}, .canonical_type = {.value = 2}, .state = IR_FUNCTION_LOWERED,
                    .entry = {.value = 0}, .blocks = blocks, .block_count = 3, .instructions = instructions,
                    .instruction_count = 5, .values = values, .value_count = 3};
                if (metadata_kind == 1)
                {
                    metadata[2] = (IrValueLabelMetadata){0};
                    function.label_metadata_values = metadata_values + 2;
                    function.label_metadata = metadata + 2;
                    function.label_metadata_count = 1;
                }
                else if (labels)
                {
                    function.label_metadata_values = metadata_values;
                    function.label_metadata = metadata;
                    function.label_metadata_count = 3;
                }
                if (published)
                {
                    function.published_cfg = &cfg;
                    blocks[2].first_parameter = 0;
                    blocks[2].last_parameter = 0;
                    blocks[2].first_predecessor = 0;
                    blocks[2].last_predecessor = 0;
                }
                switch (variant)
                {
                    case IR_TEST_PARAMETER_ID: parameter.value.value = cfg_parameter.value.value = 3; break;
                    case IR_TEST_PARAMETER_TYPE: parameter.canonical_type.value = cfg_parameter.canonical_type.value = 3; break;
                    case IR_TEST_PARAMETER_INCOMING_ID: incoming[0].value.value = cfg_arguments[0].value = 3; break;
                    case IR_TEST_PARAMETER_INCOMING_TYPE: values[0].canonical_type.value = instructions[0].canonical_type.value = 3; break;
                    case IR_TEST_PARAMETER_SHORT: parameter.incoming_count = cfg.argument_count = 1; break;
                    case IR_TEST_PARAMETER_LONG: parameter.incoming_count = cfg.argument_count = 3; break;
                    case IR_TEST_PARAMETER_MISSING: parameter.first_incoming = 0; cfg.arguments = 0; break;
                    case IR_TEST_PARAMETER_EXTRA: incoming[1].next = incoming + 2; edges[1].argument_offset = 2; break;
                    case IR_TEST_PARAMETER_ORDER: incoming[0].predecessor.value = 1; predecessor_edges[0] = 1; break;
                    case IR_TEST_PARAMETER_TAIL: parameter.last_incoming = incoming; break;
                    case IR_TEST_PARAMETER_NULL_TAIL: parameter.last_incoming = 0; break;
                    case IR_TEST_PARAMETER_PROVENANCE: metadata[2].label_block_count = 1; break;
                    default: break;
                }
                IrModule module = {.functions = &function, .function_count = 1};
                IrProgram program = {.arena = arguments->arena, .modules = &module, .module_count = 1,
                    .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}, .data_layout = {.pointer = {.size = 8}}};
                for (u32 repetition = 0; repetition < 2; repetition += 1)
                {
                    TemporalArena dirty = scratch_begin(0, 0);
                    u8* bytes = arena_allocate(dirty.arena, u8, 4096);
                    memset(bytes, 0xa5, 4096);
                    scratch_end(dirty);
#if BUSTER_BENCH_ALLOCATIONS
                    IrConstructionCounters before = ir_construction_counters();
#endif
                    IrValidationResult validation = ir_validate_canonical_module(&program, &module);
                    BUSTER_TEST(arguments, validation.error == (variant == IR_TEST_PARAMETER_VALID ? IR_VALIDATION_NONE : IR_VALIDATION_BLOCK_PARAMETER));
                    if (variant != IR_TEST_PARAMETER_VALID)
                    {
                        bool scope_failure = published && variant >= IR_TEST_PARAMETER_SHORT && variant <= IR_TEST_PARAMETER_NULL_TAIL;
                        BUSTER_TEST(arguments, validation.function.value == 7);
                        BUSTER_TEST(arguments, validation.block.value == (scope_failure ? IR_ID_UNDERLYING_INVALID : 2));
                        BUSTER_TEST(arguments, validation.instruction.value == IR_ID_UNDERLYING_INVALID);
                    }
#if BUSTER_BENCH_ALLOCATIONS
                    if (variant == IR_TEST_PARAMETER_VALID)
                    {
                        IrConstructionCounters after = ir_construction_counters();
                        BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
                        BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_PARAMETER_PROVENANCE_CHECKS] -
                                               before.values[IR_CONSTRUCTION_VALIDATION_PARAMETER_PROVENANCE_CHECKS] == (metadata_kind != 0));
                        BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_INCOMING_VALUES] -
                                               before.values[IR_CONSTRUCTION_VALIDATION_INCOMING_VALUES] == 2);
                    }
#endif
                }
            }
        }
    }
    // An empty incoming list is valid, but its tail must still be null. These
    // values are defined by the parameter, rather than an instruction row.
    for (u32 published = 0; published < 2; published += 1)
    {
        for (u32 stale = 0; stale < 2; stale += 1)
        {
            if (published && stale)
            {
                continue;
            }
            IrType types[] = {
                {.id = {.value = 0}, .kind = IR_TYPE_INTEGER, .bit_width = 32, .layout = {.size = 4, .alignment = 4, .resolved = true}},
                {.id = {.value = 1}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 0}},
            };
            IrValue value = {.canonical_type = {.value = 0}, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE};
            IrValueId returned = {.value = 0};
            IrInstruction instruction = {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
                .next = IR_INSTRUCTION_ID_INVALID, .operands = &returned, .operand_count = 1};
            IrIncoming unused = {0};
            IrBlockParameter parameter = {.canonical_type = {.value = 0}, .value = {.value = 0}, .last_incoming = stale ? &unused : 0};
            IrBlock block = {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 0},
                .parameter_count = 1, .first_parameter = &parameter, .last_parameter = &parameter, .terminated = true, .sealed = true};
            IrCfgBlock cfg_block = {.instruction_count = 1, .parameter_count = 1};
            IrCfgParameter cfg_parameter = {.canonical_type = {.value = 0}, .value = {.value = 0}};
            IrPublishedCfg cfg = {.arena = arguments->arena, .blocks = &cfg_block, .parameters = &cfg_parameter,
                .block_count = 1, .instruction_count = 1, .parameter_count = 1};
            IrFunction function = {.canonical_type = {.value = 1}, .state = IR_FUNCTION_LOWERED, .entry = {.value = 0},
                .blocks = &block, .block_count = 1, .instructions = &instruction, .instruction_count = 1, .values = &value, .value_count = 1};
            if (published)
            {
                function.published_cfg = &cfg;
                block.first_parameter = 0;
                block.last_parameter = 0;
            }
            IrModule module = {.functions = &function, .function_count = 1};
            IrProgram program = {.arena = arguments->arena, .modules = &module, .module_count = 1,
                .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
            IrValidationResult validation = ir_validate_canonical_module(&program, &module);
            BUSTER_TEST(arguments, validation.error == (stale ? IR_VALIDATION_BLOCK_PARAMETER : IR_VALIDATION_NONE));
            if (stale)
            {
                BUSTER_TEST(arguments, validation.block.value == (published ? IR_ID_UNDERLYING_INVALID : 0));
                BUSTER_TEST(arguments, validation.instruction.value == IR_ID_UNDERLYING_INVALID);
            }
        }
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
    // The fused walk clears one visited byte per row and one definition byte
    // per value, not a block id per row.
    IR_VALIDATION_EXPECT(VALIDATION_OWNERSHIP_BYTES_CLEARED, 3 + 1);
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

// The definition the validator must agree with: no two relocations of one
// global share a byte. Only the test compares every pair.
BUSTER_GLOBAL_LOCAL bool ir_test_relocations_pairwise_overlap_free(IrGlobalRelocation* relocations, u32 count, u64 width)
{
    bool result = true;
    for (u32 index = 0; index < count; index += 1)
    {
        for (u32 previous = 0; previous < index; previous += 1)
        {
            result &= relocations[previous].offset >= relocations[index].offset + width ||
                      relocations[index].offset >= relocations[previous].offset + width;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 ir_test_relocation_random(u64* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state >> 33;
}

// Ordered, reversed, shuffled, duplicated, misaligned and out-of-range tables
// against the pairwise definition. The table spans 2^20 bytes so unordered
// offsets need three radix passes. The counting build also pins the work: one
// comparison per row for an ordered table and one bounded sort otherwise.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_global_relocation_overlap(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    enum { IR_TEST_RELOCATION_SLOTS = 1 << 17, IR_TEST_RELOCATION_CAPACITY = 4096, IR_TEST_RELOCATION_TRIALS = 768 };
    u32 width = 8;
    u64 table_size = (u64)IR_TEST_RELOCATION_SLOTS * width;
    IrProgram program = ir_program_initialize(arena, 1, 2, 1, 0);
    program.arena = arena;
    program.data_layout.pointer.size = width;
    IrTypeId pointer = ir_program_add_type(&program, (IrType){
                                                         .kind = IR_TYPE_POINTER,
                                                         .layout = {.size = 8, .alignment = 8, .resolved = true},
                                                     });
    IrTypeId table_type = ir_program_add_type(&program, (IrType){
                                                            .kind = IR_TYPE_ARRAY,
                                                            .element_type = pointer,
                                                            .element_count = IR_TEST_RELOCATION_SLOTS,
                                                            .layout = {.size = table_size, .alignment = 8, .resolved = true},
                                                        });
    IrSymbolId symbol = ir_program_add_symbol(&program, (IrSymbol){
                                                          .type = table_type,
                                                          .kind = IR_SYMBOL_DATA,
                                                          .linkage = IR_LINKAGE_INTERNAL,
                                                          .is_definition = true,
                                                      });
    u8* bytes = arena_allocate(arena, u8, table_size);
    IrGlobalRelocation* relocations = arena_allocate(arena, IrGlobalRelocation, IR_TEST_RELOCATION_CAPACITY);
    IrGlobal* global = ir_module_add_global(arena, program.modules, (IrGlobal){
                                                                      .symbol = symbol,
                                                                      .type = table_type,
                                                                      .bytes = (ByteSlice){.pointer = bytes, .length = table_size},
                                                                      .relocations = relocations,
                                                                      .initializer_kind = IR_GLOBAL_INITIALIZER_BYTES,
                                                                  });
    if (BUSTER_REQUIRE(arguments, global != 0 && bytes && relocations && symbol.value != IR_ID_UNDERLYING_INVALID &&
                                  table_type.value != IR_ID_UNDERLYING_INVALID))
    {
        memset(bytes, 0, table_size);
        for (u32 index = 0; index < IR_TEST_RELOCATION_CAPACITY; index += 1)
        {
            relocations[index] = (IrGlobalRelocation){.symbol = symbol};
        }
        u64 state = 0x1310;
        u32 invalid_count = 0;
        for (u32 trial = 0; trial < IR_TEST_RELOCATION_TRIALS; trial += 1)
        {
            u32 count = trial % 41;
            u32 shape = (trial / 41) % 6;
            u64 slot = ir_test_relocation_random(&state) % 64;
            // Ascending distinct slots with random gaps; each shape then
            // reverses, shuffles, duplicates, or draws free byte offsets.
            for (u32 index = 0; index < count; index += 1)
            {
                slot += 1 + ir_test_relocation_random(&state) % 3;
                u64 offset = slot * width;
                if (shape == 4)
                {
                    offset = ir_test_relocation_random(&state) % (table_size - width + 1);
                }
                else if (shape == 5 && index % 5 == 4)
                {
                    offset -= 3;
                }
                relocations[index].offset = offset;
            }
            for (u32 index = 0; shape == 1 && index < count / 2; index += 1)
            {
                IrGlobalRelocation swap = relocations[index];
                relocations[index] = relocations[count - 1 - index];
                relocations[count - 1 - index] = swap;
            }
            for (u32 index = count; (shape == 2 || shape == 3 || shape == 4) && index > 1; index -= 1)
            {
                u32 other = (u32)(ir_test_relocation_random(&state) % index);
                IrGlobalRelocation swap = relocations[index - 1];
                relocations[index - 1] = relocations[other];
                relocations[other] = swap;
            }
            if (shape == 3 && count > 1)
            {
                u32 destination = (u32)(ir_test_relocation_random(&state) % count);
                u32 source = (u32)(ir_test_relocation_random(&state) % count);
                relocations[destination].offset = relocations[source].offset;
            }
            global->relocation_count = count;
            bool expected = ir_test_relocations_pairwise_overlap_free(relocations, count, width);
            invalid_count += !expected;
            IrValidationError error = ir_validate_canonical_module(&program, program.modules).error;
            BUSTER_TEST(arguments, error == (expected ? IR_VALIDATION_NONE : IR_VALIDATION_OPERATION));
        }
        // The mix must exercise both verdicts, not just one of them.
        BUSTER_TEST(arguments, invalid_count != 0 && invalid_count != IR_TEST_RELOCATION_TRIALS);

        u64 edge_offsets[][3] = {
            {0, 8, 16}, {16, 8, 0}, {8, 0, 16}, {0, 7, 16}, {16, 0, 9}, {0, 0, 8},
            {table_size - 8, 0, 8}, {table_size - 7, 0, 8}, {8, 0, table_size - 7},
        };
        bool edge_valid[] = {true, true, true, false, false, false, true, false, false};
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(edge_offsets) == BUSTER_ARRAY_LENGTH(edge_valid));
        for (u32 edge = 0; edge < BUSTER_ARRAY_LENGTH(edge_offsets); edge += 1)
        {
            for (u32 index = 0; index < 3; index += 1)
            {
                relocations[index].offset = edge_offsets[edge][index];
            }
            global->relocation_count = 3;
            IrValidationError error = ir_validate_canonical_module(&program, program.modules).error;
            BUSTER_TEST(arguments, error == (edge_valid[edge] ? IR_VALIDATION_NONE : IR_VALIDATION_OPERATION));
        }
        // Without a pointer width no relocation fits, in either order.
        relocations[0].offset = 8;
        relocations[1].offset = 0;
        global->relocation_count = 2;
        program.data_layout.pointer.size = 0;
        BUSTER_TEST(arguments, ir_validate_canonical_module(&program, program.modules).error == IR_VALIDATION_OPERATION);
        program.data_layout.pointer.size = width;
        BUSTER_TEST(arguments, ir_validate_canonical_module(&program, program.modules).error == IR_VALIDATION_NONE);

#if BUSTER_BENCH_ALLOCATIONS
        // 4096 rows at 8-byte strides: the largest offset, 32760, has two key
        // bytes, so the reversed table takes two radix passes over its rows.
        u32 rows = IR_TEST_RELOCATION_CAPACITY;
        for (u32 reversed = 0; reversed < 2; reversed += 1)
        {
            for (u32 index = 0; index < rows; index += 1)
            {
                relocations[index].offset = (reversed ? rows - 1 - index : index) * width;
            }
            global->relocation_count = rows;
            IrConstructionCounters before = ir_construction_counters();
            IrValidationError error = ir_validate_canonical_module(&program, program.modules).error;
            IrConstructionCounters after = ir_construction_counters();
            BUSTER_TEST(arguments, error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
#define IR_RELOCATION_EXPECT(counter, expected) \
    BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_##counter] - before.values[IR_CONSTRUCTION_##counter] == (expected))
            IR_RELOCATION_EXPECT(VALIDATION_GLOBAL_RELOCATIONS, rows);
            // Ordered: rows - 1 neighbour checks. Reversed: the first check
            // fails, then the sorted copy has rows - 1 neighbour checks.
            IR_RELOCATION_EXPECT(VALIDATION_GLOBAL_RELOCATION_PAIRS, reversed ? rows : rows - 1);
            IR_RELOCATION_EXPECT(VALIDATION_GLOBAL_RELOCATION_SORTS, reversed);
            IR_RELOCATION_EXPECT(VALIDATION_GLOBAL_RELOCATION_SORT_ROWS, reversed ? 2 * rows : 0);
#undef IR_RELOCATION_EXPECT
        }
#endif
    }
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

BUSTER_GLOBAL_LOCAL IrFunction* ir_test_inline_find_function(IrModule* module, String8 name)
{
    IrFunction* result = 0;
    for (u32 index = 0; index < module->function_count && !result; index += 1)
    {
        if (string_equal(module->functions[index].name, name))
        {
            result = module->functions + index;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_canonical_inline(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8(
        "static int tiny_leaf(int value) { return value * 3 + 7; }\n"
        "static inline __attribute__((always_inline)) int forced_leaf(int value) { return value + 11; }\n"
        "static __attribute__((noinline)) int blocked_leaf(int value) { return value + 13; }\n"
        "typedef struct InlinePair { int first; int second; } InlinePair;\n"
        "static __attribute__((noinline)) int ordinary_nested(int value) { int result = value; for (int index = 0; index < 3; index += 1) result += index; return result; }\n"
        "static inline __attribute__((always_inline)) int forced_cfg(int value)\n"
        "{ int local = ordinary_nested(value); if (local > 4) local += 3; else local -= 2; return local; }\n"
        "static inline __attribute__((always_inline)) InlinePair forced_aggregate(int value)\n"
        "{ InlinePair result; result.first = ordinary_nested(value); result.second = value + 3; return result; }\n"
        "static int (*volatile escaped_leaf)(int) = tiny_leaf;\n"
        "static int recursive_leaf(int value)\n"
        "{\n"
        "    return value ? recursive_leaf(value - 1) + 1 : 0;\n"
        "}\n"
        "static int mutual_odd(int value);\n"
        "static int mutual_even(int value)\n"
        "{\n"
        "    return value ? mutual_odd(value - 1) : 1;\n"
        "}\n"
        "static int mutual_odd(int value)\n"
        "{\n"
        "    return value ? mutual_even(value - 1) : 0;\n"
        "}\n"
        "int caller(int value)\n"
        "{\n"
        "    int nested = forced_cfg(value);\n"
        "    InlinePair pair = forced_aggregate(value);\n"
        "    if (value > 0) return nested + pair.first + pair.second + tiny_leaf(value) + forced_leaf(value) + blocked_leaf(value);\n"
        "    return nested + pair.first + pair.second + tiny_leaf(-value) + forced_leaf(-value) + blocked_leaf(-value);\n"
        "}\n"
        "int main(void)\n"
        "{\n"
        "    return caller(2) + escaped_leaf(1) + recursive_leaf(1) + mutual_odd(3);\n"
        "}\n");
    CPreprocessResult preprocess = c_preprocess(arguments->arena, source,
        (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
    CAnalysisResult analysis = c_parse(arguments->arena, preprocess);
    CIRLowerResult lowered = {0};
    if (!preprocess.error_count && !analysis.diagnostic_count)
    {
        lowered = c_lower_to_ir(arguments->arena, S8("canonical-inline-ir.c"), preprocess, analysis, target_native);
    }
    BUSTER_TEST(arguments, preprocess.error_count == 0);
    BUSTER_TEST(arguments, analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, lowered.diagnostic_count == 0 && lowered.program != 0);
    if (lowered.program && !lowered.diagnostic_count)
    {
        IrProgram* program = lowered.program;
        IrModule* module = program->modules;
        program->fast_passes = 0;
        program->inline_options = (IrInlineOptions){
            .tiny = true,
            .max_callee_instructions = IR_INLINE_TINY_INSTRUCTIONS,
            .max_function_growth = IR_INLINE_FUNCTION_GROWTH,
            .max_module_growth = IR_INLINE_MODULE_GROWTH,
            .max_call_sites = IR_INLINE_CALL_SITES,
        };
        BUSTER_TEST(arguments, module->function_count == 11);
        BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
        IrFunction* debug_caller_before = ir_test_inline_find_function(module, S8("caller"));
        IrFunction* debug_inlinee_before = ir_test_inline_find_function(module, S8("forced_cfg"));
        u32 caller_debug_local_count_before = debug_caller_before ? debug_caller_before->debug_local_count : 0;
        u32 caller_debug_scope_count_before = debug_caller_before ? debug_caller_before->debug_scope_count : 0;
        u32 inlinee_debug_local_count = debug_inlinee_before ? debug_inlinee_before->debug_local_count : 0;
        u32 inlinee_debug_scope_count = debug_inlinee_before ? debug_inlinee_before->debug_scope_count : 0;
        BUSTER_TEST(arguments, debug_inlinee_before && inlinee_debug_local_count != 0);
        IrValidationResult prepared = ir_prepare_canonical_module(program, module, false);
        BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
        BUSTER_TEST(arguments, module->inline_complete);
        BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
        BUSTER_TEST(arguments, module->inlining.candidates >= 4);
        BUSTER_TEST(arguments, module->inlining.inlined >= 4);
        BUSTER_TEST(arguments, module->inlining.always_inlined >= 2);
        BUSTER_TEST(arguments, module->inlining.copied_instructions != 0);
        BUSTER_TEST(arguments, module->inlining.growth <= program->inline_options.max_module_growth);
        // Planning visits share the compiler's explicit fast-work ceiling;
        // the count includes graph and metadata scans, not just copied rows.
        BUSTER_TEST(arguments, module->inlining.visits != 0 && module->inlining.visits <= IR_FAST_WORK_BUDGET);
        BUSTER_TEST(arguments, module->inlining.recursion_skips != 0);
        IrFunction* caller = ir_test_inline_find_function(module, S8("caller"));
        IrFunction* tiny_leaf = ir_test_inline_find_function(module, S8("tiny_leaf"));
        IrFunction* blocked_leaf = ir_test_inline_find_function(module, S8("blocked_leaf"));
        IrFunction* recursive_leaf = ir_test_inline_find_function(module, S8("recursive_leaf"));
        IrFunction* mutual_even = ir_test_inline_find_function(module, S8("mutual_even"));
        IrFunction* mutual_odd = ir_test_inline_find_function(module, S8("mutual_odd"));
        BUSTER_TEST(arguments, caller && tiny_leaf && blocked_leaf && recursive_leaf && mutual_even && mutual_odd);
        if (caller)
        {
            BUSTER_TEST(arguments, caller->debug_local_count >= caller_debug_local_count_before + inlinee_debug_local_count);
            BUSTER_TEST(arguments, caller->debug_scope_count >= caller_debug_scope_count_before + inlinee_debug_scope_count + 1);
            for (u32 i = caller_debug_local_count_before; i < caller->debug_local_count; i += 1)
                BUSTER_TEST(arguments, caller->debug_locals[i].scope <= caller->debug_scope_count);
            for (u32 i = 0; i < caller->debug_scope_count; i += 1)
                BUSTER_TEST(arguments, caller->debug_scopes[i].parent <= i);
        }
        if (caller && blocked_leaf)
        {
            BUSTER_TEST(arguments, ir_test_direct_call_count(program, caller, S8("forced_leaf")) == 0);
            BUSTER_TEST(arguments, ir_test_direct_call_count(program, caller, S8("forced_cfg")) == 0);
            BUSTER_TEST(arguments, ir_test_direct_call_count(program, caller, S8("forced_aggregate")) == 0);
            BUSTER_TEST(arguments, ir_test_direct_call_count(program, caller, S8("ordinary_nested")) == 2);
            BUSTER_TEST(arguments, ir_test_direct_call_count(program, caller, S8("blocked_leaf")) == 2);
        }
        if (recursive_leaf)
        {
            BUSTER_TEST(arguments, ir_test_opcode_count(recursive_leaf, IR_OPCODE_CALL) != 0);
        }
        if (mutual_even && mutual_odd)
        {
            BUSTER_TEST(arguments, ir_test_opcode_count(mutual_even, IR_OPCODE_CALL) != 0);
            BUSTER_TEST(arguments, ir_test_opcode_count(mutual_odd, IR_OPCODE_CALL) != 0);
        }
        // Address escape keeps the function available even though its direct
        // calls were copied into the multi-block caller.
        BUSTER_TEST(arguments, tiny_leaf != 0);
    }
    // Zero budgets request the public defaults; normalize them on a fresh,
    // unpublished module so these defaults remain part of the direct IR API.
    {
        TemporalArena defaults_temporary = scratch_begin(&arguments->arena, 1);
        String8 defaults_source = S8(
            "static int small_leaf(int value) { return value + 1; }\n"
            "int small_caller(int value) { return small_leaf(value); }\n");
        CPreprocessResult defaults_preprocess = c_preprocess(defaults_temporary.arena, defaults_source,
            (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
        CAnalysisResult defaults_analysis = c_parse(defaults_temporary.arena, defaults_preprocess);
        CIRLowerResult defaults_lowered = {0};
        if (!defaults_preprocess.error_count && !defaults_analysis.diagnostic_count)
        {
            defaults_lowered = c_lower_to_ir(defaults_temporary.arena, S8("canonical-inline-defaults.c"),
                defaults_preprocess, defaults_analysis, target_native);
        }
        BUSTER_TEST(arguments, defaults_preprocess.error_count == 0 && defaults_analysis.diagnostic_count == 0);
        BUSTER_TEST(arguments, defaults_lowered.diagnostic_count == 0 && defaults_lowered.program != 0);
        if (BUSTER_REQUIRE(arguments, defaults_lowered.program && defaults_lowered.program->module_count == 1))
        {
            IrProgram* defaults_program = defaults_lowered.program;
            IrModule* defaults_module = defaults_program->modules;
            defaults_program->inline_options = (IrInlineOptions){
                .tiny = true,
                .max_callee_instructions = 0,
                .max_function_growth = 0,
                .max_module_growth = 0,
                .max_call_sites = 0,
            };
            IrValidationResult defaults_prepared = ir_prepare_canonical_module(defaults_program, defaults_module, false);
            BUSTER_TEST(arguments, defaults_prepared.error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, defaults_program->inline_options.tiny);
            BUSTER_TEST(arguments, defaults_program->inline_options.max_callee_instructions == IR_INLINE_TINY_INSTRUCTIONS);
            BUSTER_TEST(arguments, defaults_program->inline_options.max_function_growth == IR_INLINE_FUNCTION_GROWTH);
            BUSTER_TEST(arguments, IR_INLINE_FUNCTION_GROWTH == 2048u);
            BUSTER_TEST(arguments, defaults_program->inline_options.max_module_growth == IR_INLINE_MODULE_GROWTH);
            BUSTER_TEST(arguments, defaults_program->inline_options.max_call_sites == IR_INLINE_CALL_SITES);
            BUSTER_TEST(arguments, defaults_module->inline_complete);
            BUSTER_TEST(arguments, defaults_module->inlining.inlined >= 1);
            IrFunction* defaults_caller = ir_test_inline_find_function(defaults_module, S8("small_caller"));
            BUSTER_TEST(arguments, defaults_caller != 0);
            BUSTER_TEST(arguments, ir_test_direct_call_count(defaults_program, defaults_caller, S8("small_leaf")) == 0);
            BUSTER_TEST(arguments, ir_validate_canonical_module(defaults_program, defaults_module).error == IR_VALIDATION_NONE);
        }
        scratch_end(defaults_temporary);
    }

    // Mandatory sites must be budgeted before optional sites, even when
    // the optional caller appears first in source/module order. Measure the
    // exact cost of this required expansion from a control module, then give
    // the competing module exactly that much growth budget.
    {
        TemporalArena required_temporary = scratch_begin(&arguments->arena, 1);
        String8 required_source = S8(
            "static inline __attribute__((always_inline)) int required_leaf(int value) { return value + 1; }\n"
            "int required_caller(int value) { return required_leaf(value); }\n");
        CPreprocessResult required_preprocess = c_preprocess(required_temporary.arena, required_source,
            (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
        CAnalysisResult required_analysis = c_parse(required_temporary.arena, required_preprocess);
        CIRLowerResult required_lowered = {0};
        if (!required_preprocess.error_count && !required_analysis.diagnostic_count)
        {
            required_lowered = c_lower_to_ir(required_temporary.arena, S8("canonical-inline-required-budget.c"),
                required_preprocess, required_analysis, target_native);
        }
        BUSTER_TEST(arguments, required_preprocess.error_count == 0 && required_analysis.diagnostic_count == 0);
        BUSTER_TEST(arguments, required_lowered.diagnostic_count == 0 && required_lowered.program != 0);
        u64 required_growth = 0;
        if (BUSTER_REQUIRE(arguments, required_lowered.program && required_lowered.program->module_count == 1))
        {
            IrProgram* required_program = required_lowered.program;
            IrModule* required_module = required_program->modules;
            required_program->fast_passes = 0;
            required_program->inline_options = (IrInlineOptions){
                .tiny = false,
                .max_callee_instructions = IR_INLINE_TINY_INSTRUCTIONS,
                .max_function_growth = IR_INLINE_FUNCTION_GROWTH,
                .max_module_growth = IR_INLINE_MODULE_GROWTH,
                .max_call_sites = IR_INLINE_CALL_SITES,
            };
            IrValidationResult required_prepared = ir_prepare_canonical_module(required_program, required_module, false);
            BUSTER_TEST(arguments, required_prepared.error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, required_module->inlining.always_inlined == 1);
            IrFunction* required_caller = ir_test_inline_find_function(required_module, S8("required_caller"));
            BUSTER_TEST(arguments, required_caller != 0);
            BUSTER_TEST(arguments, ir_test_direct_call_count(required_program, required_caller, S8("required_leaf")) == 0);
            required_growth = required_module->inlining.growth;
            BUSTER_TEST(arguments, required_growth > 0);
            BUSTER_TEST(arguments, ir_validate_canonical_module(required_program, required_module).error == IR_VALIDATION_NONE);
        }

        TemporalArena priority_temporary = scratch_begin(&arguments->arena, 1);
        String8 priority_source = S8(
            "static int optional_leaf(int value) { return value * 2 + 3; }\n"
            "int optional_caller(int value) { return optional_leaf(value); }\n"
            "static inline __attribute__((always_inline)) int required_leaf(int value) { return value + 1; }\n"
            "int required_caller(int value) { return required_leaf(value); }\n");
        CPreprocessResult priority_preprocess = c_preprocess(priority_temporary.arena, priority_source,
            (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
        CAnalysisResult priority_analysis = c_parse(priority_temporary.arena, priority_preprocess);
        CIRLowerResult priority_lowered = {0};
        if (!priority_preprocess.error_count && !priority_analysis.diagnostic_count)
        {
            priority_lowered = c_lower_to_ir(priority_temporary.arena, S8("canonical-inline-priority-budget.c"),
                priority_preprocess, priority_analysis, target_native);
        }
        BUSTER_TEST(arguments, priority_preprocess.error_count == 0 && priority_analysis.diagnostic_count == 0);
        BUSTER_TEST(arguments, priority_lowered.diagnostic_count == 0 && priority_lowered.program != 0);
        if (BUSTER_REQUIRE(arguments, priority_lowered.program && priority_lowered.program->module_count == 1 && required_growth > 0))
        {
            IrProgram* priority_program = priority_lowered.program;
            IrModule* priority_module = priority_program->modules;
            priority_program->fast_passes = 0;
            priority_program->inline_options = (IrInlineOptions){
                .tiny = true,
                .max_callee_instructions = IR_INLINE_TINY_INSTRUCTIONS,
                .max_function_growth = IR_INLINE_FUNCTION_GROWTH,
                .max_module_growth = (u32)required_growth,
                .max_call_sites = IR_INLINE_CALL_SITES,
            };
            IrValidationResult priority_prepared = ir_prepare_canonical_module(priority_program, priority_module, false);
            BUSTER_TEST(arguments, priority_prepared.error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, priority_module->inline_complete);
            IrFunction* optional_caller = ir_test_inline_find_function(priority_module, S8("optional_caller"));
            IrFunction* required_caller = ir_test_inline_find_function(priority_module, S8("required_caller"));
            BUSTER_TEST(arguments, optional_caller != 0 && required_caller != 0);
            BUSTER_TEST(arguments, priority_module->inlining.always_inlined == 1);
            BUSTER_TEST(arguments, priority_module->inlining.growth == required_growth);
            BUSTER_TEST(arguments, priority_module->inlining.budget_skips != 0);
            BUSTER_TEST(arguments, ir_test_direct_call_count(priority_program, required_caller, S8("required_leaf")) == 0);
            BUSTER_TEST(arguments, ir_test_direct_call_count(priority_program, optional_caller, S8("optional_leaf")) == 1);
            BUSTER_TEST(arguments, ir_validate_canonical_module(priority_program, priority_module).error == IR_VALIDATION_NONE);
        }
        scratch_end(priority_temporary);
        scratch_end(required_temporary);
    }

    // A leading acyclic mandatory feeder must not hide a later required
    // cycle from the graph-ordering fallback.
    {
        TemporalArena cycle_temporary = scratch_begin(&arguments->arena, 1);
        String8 cycle_source = S8(
            "static inline __attribute__((always_inline)) int cycle_b(int value);\n"
            "static int feeder(int value) { return cycle_b(value); }\n"
            "static inline __attribute__((always_inline)) int cycle_c(int value);\n"
            "static inline __attribute__((always_inline)) int cycle_b(int value) { return cycle_c(value); }\n"
            "static inline __attribute__((always_inline)) int cycle_c(int value) { return cycle_b(value); }\n"
            "int main(void) { return feeder(1); }\n");
        CPreprocessResult cycle_preprocess = c_preprocess(cycle_temporary.arena, cycle_source,
            (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
        CAnalysisResult cycle_analysis = c_parse(cycle_temporary.arena, cycle_preprocess);
        CIRLowerResult cycle_lowered = {0};
        if (!cycle_preprocess.error_count && !cycle_analysis.diagnostic_count)
        {
            cycle_lowered = c_lower_to_ir(cycle_temporary.arena, S8("canonical-inline-leading-cycle.c"),
                cycle_preprocess, cycle_analysis, target_native);
        }
        BUSTER_TEST(arguments, cycle_preprocess.error_count == 0 && cycle_analysis.diagnostic_count == 0);
        BUSTER_TEST(arguments, cycle_lowered.diagnostic_count == 0 && cycle_lowered.program != 0);
        if (BUSTER_REQUIRE(arguments, cycle_lowered.program && cycle_lowered.program->module_count == 1))
        {
            IrProgram* cycle_program = cycle_lowered.program;
            IrModule* cycle_module = cycle_program->modules;
            BUSTER_TEST(arguments, cycle_module->function_count == 4);
            u32 feeder_index = cycle_module->function_count;
            for (u32 index = 0; index < cycle_module->function_count; index += 1)
            {
                IrSymbol* symbol = ir_symbol_from_id(&cycle_program->symbols, cycle_module->functions[index].symbol);
                if (symbol && string_equal(symbol->name, S8("feeder")))
                {
                    feeder_index = index;
                }
            }
            BUSTER_TEST(arguments, feeder_index < cycle_module->function_count);
            // Prototypes can reserve function slots before the definitions.
            // Put the acyclic feeder first to expose the old first-residual
            // cycle selector, keeping each whole function and its owned
            // instruction/source payload together.
            if (feeder_index < cycle_module->function_count && feeder_index != 0)
            {
                IrFunction feeder_function = cycle_module->functions[feeder_index];
                for (u32 index = feeder_index; index > 0; index -= 1)
                {
                    cycle_module->functions[index] = cycle_module->functions[index - 1];
                }
                cycle_module->functions[0] = feeder_function;
                // Function IDs index this array; symbol IDs in CALL rows do
                // not change when the function records are reordered.
                for (u32 index = 0; index < cycle_module->function_count; index += 1)
                {
                    cycle_module->functions[index].id.value = index;
                }
            }
            IrSymbol* leading_symbol = cycle_module->function_count ?
                ir_symbol_from_id(&cycle_program->symbols, cycle_module->functions[0].symbol) : 0;
            BUSTER_TEST(arguments, leading_symbol && string_equal(leading_symbol->name, S8("feeder")));
            // Edges are selected by the callee's directive. This ordinary
            // feeder is first but has no incoming required edge: main calls
            // it, while the mandatory cycle begins at its call to cycle_b.
            BUSTER_TEST(arguments, leading_symbol && !leading_symbol->always_inline);
            IrFunction* cycle_b_function = 0;
            IrFunction* cycle_c_function = 0;
            IrFunction* main_function = 0;
            for (u32 index = 0; index < cycle_module->function_count; index += 1)
            {
                IrSymbol* symbol = ir_symbol_from_id(&cycle_program->symbols, cycle_module->functions[index].symbol);
                if (symbol && string_equal(symbol->name, S8("cycle_b"))) cycle_b_function = cycle_module->functions + index;
                if (symbol && string_equal(symbol->name, S8("cycle_c"))) cycle_c_function = cycle_module->functions + index;
                if (symbol && string_equal(symbol->name, S8("main"))) main_function = cycle_module->functions + index;
            }
            BUSTER_TEST(arguments, cycle_b_function && cycle_c_function && main_function);
            if (cycle_b_function && cycle_c_function)
            {
                IrSymbol* cycle_b_symbol = ir_symbol_from_id(&cycle_program->symbols, cycle_b_function->symbol);
                IrSymbol* cycle_c_symbol = ir_symbol_from_id(&cycle_program->symbols, cycle_c_function->symbol);
                BUSTER_TEST(arguments, cycle_b_symbol && cycle_b_symbol->always_inline);
                BUSTER_TEST(arguments, cycle_c_symbol && cycle_c_symbol->always_inline);
                BUSTER_TEST(arguments, ir_test_direct_call_count(cycle_program, cycle_module->functions, S8("cycle_b")) == 1);
                BUSTER_TEST(arguments, ir_test_direct_call_count(cycle_program, cycle_b_function, S8("cycle_c")) == 1);
                BUSTER_TEST(arguments, ir_test_direct_call_count(cycle_program, cycle_c_function, S8("cycle_b")) == 1);
            }
            if (main_function)
            {
                IrSymbol* main_symbol = ir_symbol_from_id(&cycle_program->symbols, main_function->symbol);
                BUSTER_TEST(arguments, main_symbol && !main_symbol->always_inline);
                BUSTER_TEST(arguments, ir_test_direct_call_count(cycle_program, main_function, S8("feeder")) == 1);
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(cycle_program, cycle_module).error == IR_VALIDATION_NONE);
            cycle_program->fast_passes = 0;
            cycle_program->inline_options = (IrInlineOptions){
                .tiny = false,
                .max_callee_instructions = IR_INLINE_TINY_INSTRUCTIONS,
                .max_function_growth = IR_INLINE_FUNCTION_GROWTH,
                .max_module_growth = IR_INLINE_MODULE_GROWTH,
                .max_call_sites = IR_INLINE_CALL_SITES,
            };
            IrValidationResult cycle_prepared = ir_prepare_canonical_module(cycle_program, cycle_module, false);
            BUSTER_TEST(arguments, cycle_prepared.error == IR_VALIDATION_INLINE_REQUIRED);
            BUSTER_TEST(arguments, !cycle_module->inline_complete);
            BUSTER_TEST(arguments, cycle_module->inlining.recursion_skips != 0);
            BUSTER_TEST(arguments, cycle_prepared.function.value < cycle_module->function_count);
            if (cycle_prepared.function.value < cycle_module->function_count)
            {
                IrFunction* failed_caller = 0;
                for (u32 index = 0; index < cycle_module->function_count; index += 1)
                {
                    if (cycle_module->functions[index].id.value == cycle_prepared.function.value)
                    {
                        failed_caller = cycle_module->functions + index;
                    }
                }
                BUSTER_TEST(arguments, failed_caller != 0);
                if (failed_caller)
                {
                    BUSTER_TEST(arguments, cycle_prepared.instruction.value < failed_caller->instruction_count);
                }
                if (failed_caller && cycle_prepared.instruction.value < failed_caller->instruction_count)
                {
                    IrInstruction* failed_call = failed_caller->instructions + cycle_prepared.instruction.value;
                    BUSTER_TEST(arguments, failed_call->opcode == IR_OPCODE_CALL);
                    BUSTER_TEST(arguments, failed_caller->instruction_canonical_sources != 0);
                    if (failed_caller->instruction_canonical_sources)
                    {
                        BUSTER_TEST(arguments, failed_caller->instruction_canonical_sources[cycle_prepared.instruction.value].length != 0);
                    }
                    bool targets_required_callee = false;
                    if (failed_call->operand_count && failed_call->operands &&
                        failed_call->operands[0].value < failed_caller->value_count)
                    {
                        IrInstructionId callee_definition = failed_caller->values[failed_call->operands[0].value].definition;
                        if (callee_definition.value < failed_caller->instruction_count)
                        {
                            IrInstruction* callee = failed_caller->instructions + callee_definition.value;
                            IrSymbol* symbol = callee->opcode == IR_OPCODE_FUNCTION ?
                                ir_symbol_from_id(&cycle_program->symbols, callee->symbol) : 0;
                            targets_required_callee = symbol && symbol->always_inline;
                        }
                    }
                    BUSTER_TEST(arguments, targets_required_callee);
                }
            }
        }
        scratch_end(cycle_temporary);
    }

    // An explicit function-growth limit on a required call reports the
    // refusing budget and the original source call site.
    {
        TemporalArena refusal_temporary = scratch_begin(&arguments->arena, 1);
        String8 refusal_source = S8(
            "static inline __attribute__((always_inline)) int required_leaf(int value) { return value + 1; }\n"
            "int required_caller(int value) { return required_leaf(value); }\n");
        CPreprocessResult refusal_preprocess = c_preprocess(refusal_temporary.arena, refusal_source,
            (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
        CAnalysisResult refusal_analysis = c_parse(refusal_temporary.arena, refusal_preprocess);
        CIRLowerResult refusal_lowered = {0};
        if (!refusal_preprocess.error_count && !refusal_analysis.diagnostic_count)
        {
            refusal_lowered = c_lower_to_ir(refusal_temporary.arena, S8("canonical-inline-growth-refusal.c"),
                refusal_preprocess, refusal_analysis, target_native);
        }
        BUSTER_TEST(arguments, refusal_preprocess.error_count == 0 && refusal_analysis.diagnostic_count == 0);
        BUSTER_TEST(arguments, refusal_lowered.diagnostic_count == 0 && refusal_lowered.program != 0);
        if (BUSTER_REQUIRE(arguments, refusal_lowered.program && refusal_lowered.program->module_count == 1))
        {
            IrProgram* refusal_program = refusal_lowered.program;
            IrModule* refusal_module = refusal_program->modules;
            refusal_program->fast_passes = 0;
            refusal_program->inline_options = (IrInlineOptions){
                .tiny = false,
                .max_callee_instructions = IR_INLINE_TINY_INSTRUCTIONS,
                .max_function_growth = 1,
                .max_module_growth = IR_INLINE_MODULE_GROWTH,
                .max_call_sites = IR_INLINE_CALL_SITES,
            };
            BUSTER_TEST(arguments, ir_validate_canonical_module(refusal_program, refusal_module).error == IR_VALIDATION_NONE);
            IrValidationResult refusal = ir_prepare_canonical_module(refusal_program, refusal_module, false);
            BUSTER_TEST(arguments, refusal.error == IR_VALIDATION_INLINE_REQUIRED);
            BUSTER_TEST(arguments, refusal_module->inlining.required_budget_reason == IR_INLINE_BUDGET_FUNCTION_GROWTH);
            BUSTER_TEST(arguments, refusal_module->inlining.required_budget_limit == 1);
            BUSTER_TEST(arguments, refusal_module->inlining.required_budget_demand > refusal_module->inlining.required_budget_limit);
            BUSTER_TEST(arguments, !refusal_module->inline_complete);
            BUSTER_TEST(arguments, refusal.function.value < refusal_module->function_count);
            if (refusal.function.value < refusal_module->function_count)
            {
                IrFunction* failed_caller = 0;
                for (u32 index = 0; index < refusal_module->function_count; index += 1)
                {
                    if (refusal_module->functions[index].id.value == refusal.function.value)
                    {
                        failed_caller = refusal_module->functions + index;
                    }
                }
                BUSTER_TEST(arguments, failed_caller != 0);
                if (failed_caller)
                {
                    IrSymbol* caller_symbol = ir_symbol_from_id(&refusal_program->symbols, failed_caller->symbol);
                    BUSTER_TEST(arguments, caller_symbol && string_equal(caller_symbol->name, S8("required_caller")));
                    BUSTER_TEST(arguments, refusal.instruction.value < failed_caller->instruction_count);
                    if (refusal.instruction.value < failed_caller->instruction_count)
                    {
                        IrInstruction* failed_call = failed_caller->instructions + refusal.instruction.value;
                        BUSTER_TEST(arguments, failed_call->opcode == IR_OPCODE_CALL);
                        BUSTER_TEST(arguments, failed_caller->instruction_canonical_sources != 0);
                        if (failed_caller->instruction_canonical_sources)
                        {
                            BUSTER_TEST(arguments, failed_caller->instruction_canonical_sources[refusal.instruction.value].length != 0);
                        }
                        bool targets_required_leaf = false;
                        if (failed_call->operand_count && failed_call->operands &&
                            failed_call->operands[0].value < failed_caller->value_count)
                        {
                            IrInstructionId callee_definition = failed_caller->values[failed_call->operands[0].value].definition;
                            if (callee_definition.value < failed_caller->instruction_count)
                            {
                                IrInstruction* callee = failed_caller->instructions + callee_definition.value;
                                IrSymbol* symbol = callee->opcode == IR_OPCODE_FUNCTION ?
                                    ir_symbol_from_id(&refusal_program->symbols, callee->symbol) : 0;
                                targets_required_leaf = symbol && symbol->always_inline &&
                                    string_equal(symbol->name, S8("required_leaf"));
                            }
                        }
                        BUSTER_TEST(arguments, targets_required_leaf);
                    }
                }
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(refusal_program, refusal_module).error == IR_VALIDATION_NONE);
        }
        scratch_end(refusal_temporary);
    }

    // A real caller with substantial spare capacity must still admit a
    // required inline. The old per-call estimate multiplied the caller's
    // full existing tables by four, so size the fixture from the row layouts
    // until that estimate exceeds 16 MiB.
    {
        TemporalArena capacity_temporary = scratch_begin(&arguments->arena, 1);
        String8 capacity_source = S8(
            "static inline __attribute__((always_inline)) int required_leaf(int value) { return value + 1; }\n"
            "int required_caller(int value) { return required_leaf(value); }\n");
        CPreprocessResult capacity_preprocess = c_preprocess(capacity_temporary.arena, capacity_source,
            (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
        CAnalysisResult capacity_analysis = c_parse(capacity_temporary.arena, capacity_preprocess);
        CIRLowerResult capacity_lowered = {0};
        if (!capacity_preprocess.error_count && !capacity_analysis.diagnostic_count)
        {
            capacity_lowered = c_lower_to_ir(capacity_temporary.arena, S8("canonical-inline-capacity.c"),
                capacity_preprocess, capacity_analysis, target_native);
        }
        BUSTER_TEST(arguments, capacity_preprocess.error_count == 0 && capacity_analysis.diagnostic_count == 0);
        BUSTER_TEST(arguments, capacity_lowered.diagnostic_count == 0 && capacity_lowered.program != 0);
        if (BUSTER_REQUIRE(arguments, capacity_lowered.program && capacity_lowered.program->module_count == 1))
        {
            IrProgram* capacity_program = capacity_lowered.program;
            IrModule* capacity_module = capacity_program->modules;
            capacity_program->fast_passes = 0;
            IrFunction* caller = 0;
            for (u32 index = 0; index < capacity_module->function_count; index += 1)
            {
                IrSymbol* symbol = ir_symbol_from_id(&capacity_program->symbols, capacity_module->functions[index].symbol);
                if (symbol && string_equal(symbol->name, S8("required_caller")))
                {
                    caller = capacity_module->functions + index;
                }
            }
            BUSTER_TEST(arguments, caller != 0);
            IrInstructionId call_id = IR_INSTRUCTION_ID_INVALID;
            IrInstructionId call_predecessor = IR_INSTRUCTION_ID_INVALID;
            IrBlockId call_block = IR_BLOCK_ID_INVALID;
            bool call_found = false;
            if (caller)
            {
                for (u32 block_index = 0; block_index < caller->block_count && !call_found; block_index += 1)
                {
                    IrBlock* block = caller->blocks + block_index;
                    IrInstructionId previous = IR_INSTRUCTION_ID_INVALID;
                    IrInstructionId current = block->first_instruction;
                    while (current.value < caller->instruction_count)
                    {
                        IrInstruction* row = caller->instructions + current.value;
                        bool calls_required_leaf = false;
                        if (row->opcode == IR_OPCODE_CALL && row->operand_count && row->operands &&
                            row->operands[0].value < caller->value_count)
                        {
                            IrInstructionId definition = caller->values[row->operands[0].value].definition;
                            if (definition.value < caller->instruction_count)
                            {
                                IrInstruction* callee = caller->instructions + definition.value;
                                IrSymbol* symbol = callee->opcode == IR_OPCODE_FUNCTION ?
                                    ir_symbol_from_id(&capacity_program->symbols, callee->symbol) : 0;
                                calls_required_leaf = symbol && symbol->always_inline &&
                                    string_equal(symbol->name, S8("required_leaf"));
                            }
                        }
                        if (calls_required_leaf)
                        {
                            call_id = current;
                            call_predecessor = previous;
                            call_block = block->id;
                            call_found = true;
                            break;
                        }
                        previous = current;
                        current = row->next;
                    }
                }
            }
            BUSTER_TEST(arguments, call_found);
            if (BUSTER_REQUIRE(arguments, caller && call_found && call_id.value < caller->instruction_count))
            {
                IrInstruction* original_call = caller->instructions + call_id.value;
                IrSourceRange original_call_source = ir_instruction_canonical_source(caller, call_id);
                BUSTER_TEST(arguments, original_call_source.length != 0);
                BUSTER_TEST(arguments, original_call->result.value < caller->value_count);
                IrTypeId constant_type = {.value = 0};
                if (original_call->result.value < caller->value_count)
                {
                    constant_type = caller->values[original_call->result.value].canonical_type;
                }
                u64 row_bytes = (u64)sizeof(IrInstruction) + sizeof(IrSourceRange);
                u64 old_storage_limit = (u64)16 * 1024 * 1024;
                u64 required_population = old_storage_limit / (4 * row_bytes) + 1;
                u64 insert_count = required_population > caller->instruction_count ?
                    required_population - caller->instruction_count : 1;
                BUSTER_TEST(arguments, insert_count < IR_FAST_WORK_BUDGET);
                bool inserted_all = false;
                if (insert_count < IR_FAST_WORK_BUDGET && original_call->result.value < caller->value_count)
                {
                    inserted_all = true;
                    IrInstructionId after = call_predecessor;
                    IrCommitRefusal refusal = IR_COMMIT_REFUSAL_COUNT;
                    for (u32 index = 0; index < (u32)insert_count; index += 1)
                    {
                        IrValueId value = ir_protocol_value(capacity_temporary.arena, caller, constant_type);
                        IrInstructionId inserted = ir_block_insert_instruction_after(capacity_temporary.arena, caller, call_block, after,
                            ir_protocol_constant(capacity_temporary.arena, constant_type, value, 0), (IrSourceRange){0}, &refusal);
                        if (refusal != IR_COMMIT_ACCEPTED || inserted.value >= caller->instruction_count)
                        {
                            inserted_all = false;
                            break;
                        }
                        after = inserted;
                    }
                }
                BUSTER_TEST(arguments, inserted_all);
                if (inserted_all)
                {
                    u64 old_estimate = 4 * (u64)caller->instruction_count * row_bytes;
                    BUSTER_TEST(arguments, old_estimate > old_storage_limit);
                    BUSTER_TEST(arguments, caller->instruction_capacity > caller->instruction_count &&
                        caller->instruction_capacity - caller->instruction_count >= IR_INLINE_FUNCTION_GROWTH);
                    BUSTER_TEST(arguments, caller->value_capacity > caller->value_count &&
                        caller->value_capacity - caller->value_count >= IR_INLINE_FUNCTION_GROWTH);
                    IrSourceRange call_source_after_insert = ir_instruction_canonical_source(caller, call_id);
                    BUSTER_TEST(arguments, call_source_after_insert.source.value == original_call_source.source.value);
                    BUSTER_TEST(arguments, call_source_after_insert.offset == original_call_source.offset);
                    BUSTER_TEST(arguments, call_source_after_insert.length == original_call_source.length);
                    BUSTER_TEST(arguments, ir_test_direct_call_count(capacity_program, caller, S8("required_leaf")) == 1);
                    BUSTER_TEST(arguments, ir_validate_canonical_module(capacity_program, capacity_module).error == IR_VALIDATION_NONE);
                    capacity_program->inline_options = (IrInlineOptions){
                        .tiny = false,
                        .max_callee_instructions = IR_INLINE_TINY_INSTRUCTIONS,
                        .max_function_growth = IR_INLINE_FUNCTION_GROWTH,
                        .max_module_growth = IR_INLINE_MODULE_GROWTH,
                        .max_call_sites = IR_INLINE_CALL_SITES,
                    };
                    IrValidationResult prepared = ir_prepare_canonical_module(capacity_program, capacity_module, false);
                    BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
                    BUSTER_TEST(arguments, capacity_module->inline_complete);
                    BUSTER_TEST(arguments, capacity_module->inlining.always_inlined == 1);
                    BUSTER_TEST(arguments, capacity_module->inlining.inlined == 1);
                    BUSTER_TEST(arguments, capacity_module->inlining.budget_skips == 0);
                    BUSTER_TEST(arguments, ir_test_direct_call_count(capacity_program, caller, S8("required_leaf")) == 0);
                    BUSTER_TEST(arguments, ir_validate_canonical_module(capacity_program, capacity_module).error == IR_VALIDATION_NONE);
                }
            }
        }
        scratch_end(capacity_temporary);
    }

    return result;
}

UnitTestResult ir_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = ir_promotion_tests(arguments);
    BUSTER_TEST_FIXTURE(arguments, ir_test_canonical_inline);
    BUSTER_TEST_FIXTURE(arguments, ir_test_label_owner_index);
    BUSTER_TEST_FIXTURE(arguments, ir_test_label_sets);
    BUSTER_TEST_FIXTURE(arguments, ir_test_label_paths);
    BUSTER_TEST_FIXTURE(arguments, ir_test_label_transfer);
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

    UnitTestResult switch_keys = ir_test_canonical_switch_keys(arguments);
    result.test_count += switch_keys.test_count;
    result.succeeded_test_count += switch_keys.succeeded_test_count;
    UnitTestResult parameter_validation = ir_test_canonical_parameter_validation(arguments);
    result.test_count += parameter_validation.test_count;
    result.succeeded_test_count += parameter_validation.succeeded_test_count;
    UnitTestResult integer_semantics = ir_integer_tests(arguments);
    result.test_count += integer_semantics.test_count;
    result.succeeded_test_count += integer_semantics.succeeded_test_count;
    UnitTestResult relocation_overlap = ir_test_global_relocation_overlap(arguments);
    result.test_count += relocation_overlap.test_count;
    result.succeeded_test_count += relocation_overlap.succeeded_test_count;
    UnitTestResult protocol = ir_construction_protocol_tests(arguments);
    result.test_count += protocol.test_count;
    result.succeeded_test_count += protocol.succeeded_test_count;
    BUSTER_TEST_FIXTURE(arguments, ir_validate_equivalence_tests);
    BUSTER_TEST_FIXTURE(arguments, ir_test_va_instruction_type_storage);

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

    UnitTestResult unreachable_payload = ir_test_canonical_unreachable_payload(arguments);
    result.test_count += unreachable_payload.test_count;
    result.succeeded_test_count += unreachable_payload.succeeded_test_count;

    UnitTestResult unary_categories = ir_test_canonical_unary_categories(arguments);
    result.succeeded_test_count += unary_categories.succeeded_test_count;
    result.test_count += unary_categories.test_count;

    UnitTestResult binary_families = ir_test_canonical_binary_families(arguments);
    result.succeeded_test_count += binary_families.succeeded_test_count;
    result.test_count += binary_families.test_count;

    UnitTestResult binary_categories = ir_test_canonical_binary_categories(arguments);
    result.test_count += binary_categories.test_count;
    result.succeeded_test_count += binary_categories.succeeded_test_count;

    UnitTestResult array_categories = ir_test_canonical_array_categories(arguments);
    result.test_count += array_categories.test_count;
    result.succeeded_test_count += array_categories.succeeded_test_count;

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

    // Alignment tail padding is NO_CLASS, and a leading padding eightbyte
    // must not move the surviving piece down in its object representation.
    {
        IrProgram fixture = ir_program_initialize(arguments->arena, 0, 12, 0, 0);
        IrTypeId scalar_types[] = {
            ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 64,
                .layout = {.size = 8, .alignment = 8, .resolved = true}}),
            ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_FLOAT, .bit_width = 64,
                .layout = {.size = 8, .alignment = 8, .resolved = true}}),
            ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 32,
                .layout = {.size = 4, .alignment = 4, .resolved = true}}),
        };
        for (u32 shape = 0; shape < BUSTER_ARRAY_LENGTH(scalar_types); shape += 1)
        {
            for (u32 upper = 0; upper < 2; upper += 1)
            {
                IrField* field = arena_allocate(arguments->arena, IrField, 1);
                *field = (IrField){.name = S8("value"), .type = scalar_types[shape], .offset = upper * 8};
                IrTypeId record = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_STRUCT,
                    .fields = field, .field_count = 1, .layout = {.size = 16, .alignment = 16, .resolved = true}});
                for (u32 use = 0; use < IR_ABI_USE_COUNT; use += 1)
                {
                    IrAbiValue abi = ir_type_abi_value(&fixture, record, IR_ABI_CONVENTION_SYSTEMV_X86_64, (IrAbiUse)use);
                    BUSTER_TEST(arguments, !abi.indirect && !abi.memory && abi.part_count == 1);
                    BUSTER_TEST(arguments, abi.parts[0].abi_class == (shape == 1 ? IR_ABI_CLASS_FLOAT : IR_ABI_CLASS_INTEGER));
                    BUSTER_TEST(arguments, abi.parts[0].value_offset == upper * 8 && abi.parts[0].size == 8);
                    IrAbiValue windows = ir_type_abi_value(&fixture, record, IR_ABI_CONVENTION_WIN64_X86_64, (IrAbiUse)use);
                    BUSTER_TEST(arguments, windows.indirect && windows.part_count == 1 &&
                                           windows.parts[0].abi_class == IR_ABI_CLASS_POINTER);
                }
            }
        }
    }

    // A GNU empty record is a real value with no SysV transport pieces. It
    // consumes neither a register nor a stack slot for arguments or results.
    // Keep this distinct from a nonempty all-NO_CLASS record, whose frontend
    // support remains deliberately gated.
    {
        IrProgram fixture = ir_program_initialize(arguments->arena, 0, 2, 0, 0);
        IrTypeId empty = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_STRUCT,
            .layout = {.size = 0, .alignment = 1, .resolved = true}});
        IrField* field = arena_allocate(arguments->arena, IrField, 1);
        *field = (IrField){.type = empty};
        IrTypeId wrapper = ir_program_add_type(&fixture, (IrType){.kind = IR_TYPE_STRUCT, .fields = field, .field_count = 1,
            .layout = {.size = 0, .alignment = 1, .resolved = true}});
        for (u32 use = 0; use < IR_ABI_USE_COUNT; use += 1)
        {
            IrAbiValue abi = ir_type_abi_value(&fixture, empty, IR_ABI_CONVENTION_SYSTEMV_X86_64, (IrAbiUse)use);
            IrAbiValue wrapped = ir_type_abi_value(&fixture, wrapper, IR_ABI_CONVENTION_SYSTEMV_X86_64, (IrAbiUse)use);
            BUSTER_TEST(arguments, !abi.indirect && !abi.memory && abi.part_count == 0);
            BUSTER_TEST(arguments, !wrapped.indirect && !wrapped.memory && wrapped.part_count == 0);
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
                    IrAbiValue public_value = ir_type_abi_value(&abi_program, (IrTypeId){type}, (IrAbiConvention)convention, (IrAbiUse)use);
                    // IrAbiValue names its two tail bytes explicitly and every
                    // classifier result initializes them; there is no padding.
                    BUSTER_CT_CHECK(sizeof(IrAbiValue) == sizeof(IrAbiPart) * IR_ABI_MAX_PARTS + sizeof(u32) + 4);
                    BUSTER_TEST(arguments, memcmp(&actual, &expected, sizeof(actual)) == 0);
                    BUSTER_TEST(arguments, memcmp(&public_value, &expected, sizeof(public_value)) == 0);
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

    // A resident answer must not bypass either public entry's validation.
    // These views retain warmed page pointers, so accepting one would be a hit.
    IrAbiValue empty_abi = {0};
    IrAbiContext warmed_context = published_view.abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64];
    u64 warmed_position = arguments->arena->position;
    for (u32 guard = 0; guard < 4; guard += 1)
    {
        IrAbiContext rejected = warmed_context;
        switch (guard)
        {
            case 0: rejected.type_storage = abi_program.types.types + 1; break;
            case 1: rejected.page_capacity = 0; break;
            case 2: rejected.convention = IR_ABI_CONVENTION_COUNT; break;
            case 3: rejected.arena = 0; break;
        }
        IrAbiValue rejected_value = ir_abi_context_value(&published_view, &rejected, abi_f80, IR_ABI_USE_RESULT);
        BUSTER_TEST(arguments, memcmp(&rejected_value, &empty_abi, sizeof(rejected_value)) == 0);
        BUSTER_TEST(arguments, rejected.classified_values == warmed_context.classified_values &&
                              rejected.allocated_bytes == warmed_context.allocated_bytes);
        // A zero arena means uninitialized for the program-owned API. It
        // legitimately creates a fresh context; the other guards must reject.
        if (guard != 3)
        {
            published_view.abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64] = rejected;
            rejected_value = ir_type_abi_value(&published_view, abi_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
            BUSTER_TEST(arguments, memcmp(&rejected_value, &empty_abi, sizeof(rejected_value)) == 0);
        }
    }
    published_view.abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64] = warmed_context;
    IrAbiValue rejected_type = ir_type_abi_value(&published_view, (IrTypeId){published_view.types.count}, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    IrAbiValue rejected_use = ir_type_abi_value(&published_view, abi_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_COUNT);
    IrAbiValue rejected_convention = ir_type_abi_value(&published_view, abi_f80, IR_ABI_CONVENTION_COUNT, IR_ABI_USE_RESULT);
    IrAbiValue rejected_program = ir_type_abi_value(0, abi_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    IrAbiValue rejected_context = ir_abi_context_value(&published_view, 0, abi_f80, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, memcmp(&rejected_type, &empty_abi, sizeof(rejected_type)) == 0);
    BUSTER_TEST(arguments, memcmp(&rejected_use, &empty_abi, sizeof(rejected_use)) == 0);
    BUSTER_TEST(arguments, memcmp(&rejected_convention, &empty_abi, sizeof(rejected_convention)) == 0);
    BUSTER_TEST(arguments, memcmp(&rejected_program, &empty_abi, sizeof(rejected_program)) == 0);
    BUSTER_TEST(arguments, memcmp(&rejected_context, &empty_abi, sizeof(rejected_context)) == 0);
    BUSTER_TEST(arguments, arguments->arena->position == warmed_position);

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
