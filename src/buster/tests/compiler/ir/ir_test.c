#include <buster/tests/compiler/ir/ir_test.h>
#if BUSTER_INCLUDE_TESTS

typedef struct IrFixtureTest IrFixtureTest;
struct IrFixtureTest
{
    String8 path;
};

BUSTER_GLOBAL_LOCAL IrFixtureTest ir_fixture_tests[] = {
    {S8_INITIALIZER("tests/basic_vector.bbb")},
    {S8_INITIALIZER("tests/basic_vector_error.bbb")},
    {S8_INITIALIZER("tests/basic_code_non_function_type_error.bbb")},
    {S8_INITIALIZER("tests/basic_return_without_value_error.bbb")},
    {S8_INITIALIZER("tests/basic_variadic.bbb")},
    {S8_INITIALIZER("tests/basic_variadic_error.bbb")},
    {S8_INITIALIZER("tests/array_slices.bbb")},
    {S8_INITIALIZER("tests/basic_array_literal.bbb")},
    {S8_INITIALIZER("tests/basic_assignment.bbb")},
    {S8_INITIALIZER("tests/basic_binary_literal.bbb")},
    {S8_INITIALIZER("tests/basic_bitwise_not.bbb")},
    {S8_INITIALIZER("tests/basic_boolean_operators.bbb")},
    {S8_INITIALIZER("tests/basic_break.bbb")},
    {S8_INITIALIZER("tests/basic_character_literal.bbb")},
    {S8_INITIALIZER("tests/basic_comment.bbb")},
    {S8_INITIALIZER("tests/basic_compile_time.bbb")},
    {S8_INITIALIZER("tests/compile_time_argument_error.bbb")},
    {S8_INITIALIZER("tests/modules/core/math.bbb")},
    {S8_INITIALIZER("tests/modules/system/platform.bbb")},
    {S8_INITIALIZER("tests/basic_continue.bbb")},
    {S8_INITIALIZER("tests/basic_else_if.bbb")},
    {S8_INITIALIZER("tests/basic_enum.bbb")},
    {S8_INITIALIZER("tests/basic_float.bbb")},
    {S8_INITIALIZER("tests/basic_for.bbb")},
    {S8_INITIALIZER("tests/basic_function_call.bbb")},
    {S8_INITIALIZER("tests/basic_hexadecimal_literal.bbb")},
    {S8_INITIALIZER("tests/basic_if_else.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_add.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_and.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_compare.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_divide.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_mod.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_multiply.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_or.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_precedence.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_shift_left.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_shift_right.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_sub.bbb")},
    {S8_INITIALIZER("tests/basic_integer_literal_xor.bbb")},
    {S8_INITIALIZER("tests/basic_logical_not.bbb")},
    {S8_INITIALIZER("tests/basic_loop.bbb")},
    {S8_INITIALIZER("tests/basic_import.bbb")},
    {S8_INITIALIZER("tests/basic_minimal.bbb")},
    {S8_INITIALIZER("tests/basic_octal_literal.bbb")},
    {S8_INITIALIZER("tests/basic_pointer.bbb")},
    {S8_INITIALIZER("tests/basic_string_literal.bbb")},
    {S8_INITIALIZER("tests/basic_struct.bbb")},
    {S8_INITIALIZER("tests/basic_switch.bbb")},
    {S8_INITIALIZER("tests/basic_type_alias.bbb")},
    {S8_INITIALIZER("tests/basic_unary_minus.bbb")},
    {S8_INITIALIZER("tests/basic_unary_plus.bbb")},
    {S8_INITIALIZER("tests/basic_union.bbb")},
    {S8_INITIALIZER("tests/basic_variable.bbb")},
};

BUSTER_GLOBAL_LOCAL u32 ir_test_opcode_count(IrFunction* function, IrOpcode opcode)
{
    u32 count = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        count += function->instructions[index].opcode == opcode;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL u32 ir_test_unary_operation_count(IrFunction* function, IrUnaryOperation operation)
{
    u32 count = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* instruction = function->instructions + index;
        count += instruction->opcode == IR_OPCODE_UNARY && instruction->unary_operation == operation;
    }
    return count;
}

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

BUSTER_GLOBAL_LOCAL u32 ir_test_parameter_count(IrFunction* function)
{
    u32 count = 0;
    for (u32 index = 0; index < function->block_count; index += 1)
    {
        count += function->blocks[index].parameter_count;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL u32 ir_test_conversion_count(IrModule* module, IrConversionOperation operation)
{
    u32 count = 0;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = function->instructions + instruction_index;
            count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == operation;
        }
    }
    return count;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_test_canonical_f80_constant(Arena* arena, u64 significand, u64 sign_exponent, u32 immediate_count, u32 target_count,
                                                                      u64 layout_size, u32 layout_alignment)
{
    IrProgram program = ir_program_initialize(arena, 1, 2, 0, 0);
    IrTypeId f80 = ir_program_add_type(&program, (IrType){
                                                       .kind = IR_TYPE_FLOAT,
                                                       .bit_width = 80,
                                                       .layout = {.size = layout_size, .alignment = layout_alignment, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
                                                   });
    IrTypeId function_type = ir_program_add_type(&program, (IrType){
                                                                   .kind = IR_TYPE_FUNCTION,
                                                                   .return_type = f80,
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
                                                                      .canonical_type = f80,
                                                                      .definition = IR_INSTRUCTION_ID_INVALID,
                                                                      .category = IR_VALUE_VALUE,
                                                                  })
                               : IR_VALUE_ID_INVALID;
    u64* immediates = arena_allocate(arena, u64, 2);
    if (immediates)
    {
        immediates[0] = significand;
        immediates[1] = sign_exponent;
    }
    IrBlockId* targets = target_count ? arena_allocate(arena, IrBlockId, target_count) : 0;
    for (u32 target_index = 0; targets && target_index < target_count; target_index += 1)
    {
        targets[target_index] = (IrBlockId){.value = 0};
    }
    IrInstructionId constant = function ? ir_function_add_instruction(arena, function, (IrInstruction){
                                                                                         .immediates = immediates,
                                                                                         .canonical_type = f80,
                                                                                         .targets = targets,
                                                                                         .target_count = target_count,
                                                                                         .result = value,
                                                                                         .opcode = IR_OPCODE_CONSTANT_FLOAT,
                                                                                         .immediate_count = immediate_count,
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
                                                                                         .canonical_type = f80,
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
    if (!global || floating.value == IR_ID_UNDERLYING_INVALID || symbol.value == IR_ID_UNDERLYING_INVALID)
    {
        return (IrValidationResult){.error = IR_VALIDATION_INVALID_ID};
    }
    return ir_validate_canonical_module(&program, program.modules);
}

UnitTestResult ir_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);

    // The frontend currently rejects a scalar f80 spelling, but nested
    // aggregates can still reach the canonical IR.  Exercise the neutral IR
    // classifier directly so the x87 contract is covered before the frontend
    // guard and backend lowering land together.
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
    IrTypeId abi_integer = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_INTEGER,
        .bit_width = 32,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true},
    });
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

    IrAbiValue abi_struct_f80_argument = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue abi_struct_f80_variadic = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_VARIADIC_ARGUMENT);
    IrAbiValue abi_struct_f80_result = ir_type_abi_value(&abi_program, abi_struct_f80, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, abi_struct_f80_argument.part_count == 1 && abi_struct_f80_argument.memory && !abi_struct_f80_argument.indirect);
    BUSTER_TEST(arguments, abi_struct_f80_argument.parts[0].abi_class == IR_ABI_CLASS_MEMORY && abi_struct_f80_argument.parts[0].size == 16);
    BUSTER_TEST(arguments, abi_struct_f80_variadic.part_count == 1 && abi_struct_f80_variadic.memory && !abi_struct_f80_variadic.indirect);
    BUSTER_TEST(arguments, abi_struct_f80_result.part_count == 2 && !abi_struct_f80_result.memory && !abi_struct_f80_result.indirect);
    BUSTER_TEST(arguments, abi_struct_f80_result.parts[0].abi_class == IR_ABI_CLASS_X87 && abi_struct_f80_result.parts[0].value_offset == 0 && abi_struct_f80_result.parts[0].size == 8);
    BUSTER_TEST(arguments, abi_struct_f80_result.parts[1].abi_class == IR_ABI_CLASS_X87_UP && abi_struct_f80_result.parts[1].value_offset == 8 && abi_struct_f80_result.parts[1].size == 8);

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

    IrValidationResult valid_f80_constant =
        ir_test_canonical_f80_constant(arguments->arena, UINT64_C(0x8000000000000001), UINT64_C(0x7fff), 2, 0, 16, 16);
    BUSTER_TEST(arguments, valid_f80_constant.error == IR_VALIDATION_NONE);
    IrValidationResult malformed_f80_count = ir_test_canonical_f80_constant(arguments->arena, UINT64_C(1), UINT64_C(0), 1, 0, 16, 16);
    BUSTER_TEST(arguments, malformed_f80_count.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_payload = ir_test_canonical_f80_constant(arguments->arena, UINT64_C(1), UINT64_C(0x10000), 2, 0, 16, 16);
    BUSTER_TEST(arguments, malformed_f80_payload.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_extra = ir_test_canonical_f80_constant(arguments->arena, UINT64_C(1), UINT64_C(0), 3, 0, 16, 16);
    BUSTER_TEST(arguments, malformed_f80_extra.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_target = ir_test_canonical_f80_constant(arguments->arena, UINT64_C(1), UINT64_C(0), 2, 1, 16, 16);
    BUSTER_TEST(arguments, malformed_f80_target.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_layout = ir_test_canonical_f80_constant(arguments->arena, UINT64_C(1), UINT64_C(0), 2, 0, 10, 16);
    BUSTER_TEST(arguments, malformed_f80_layout.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f80_alignment = ir_test_canonical_f80_constant(arguments->arena, UINT64_C(1), UINT64_C(0), 2, 0, 16, 8);
    BUSTER_TEST(arguments, malformed_f80_alignment.error == IR_VALIDATION_OPERATION);
    IrValidationResult valid_f80_global_bytes = ir_test_canonical_float_global(arguments->arena, 80, IR_GLOBAL_INITIALIZER_BYTES);
    BUSTER_TEST(arguments, valid_f80_global_bytes.error == IR_VALIDATION_NONE);
    IrValidationResult malformed_f80_global_float = ir_test_canonical_float_global(arguments->arena, 80, IR_GLOBAL_INITIALIZER_FLOAT);
    BUSTER_TEST(arguments, malformed_f80_global_float.error == IR_VALIDATION_OPERATION);
    IrValidationResult malformed_f16_global_float = ir_test_canonical_float_global(arguments->arena, 16, IR_GLOBAL_INITIALIZER_FLOAT);
    BUSTER_TEST(arguments, malformed_f16_global_float.error == IR_VALIDATION_OPERATION);

    String8 focused_source = S8("code choose : fn (a: s32, b: s32) s32\n"
                                "{\n"
                                "    data value: s32 = a;\n"
                                "    if (a < b)\n"
                                "    {\n"
                                "        value += b;\n"
                                "    }\n"
                                "    else\n"
                                "    {\n"
                                "        value -= b;\n"
                                "    }\n"
                                "    return @cast(value);\n"
                                "}\n");
    TokenizerResult focused_tokens = tokenize(arguments->arena, focused_source.pointer, focused_source.length);
    ParserResult focused_parser = parser_parse(arguments->arena, expression_arena, focused_source, focused_tokens);
    BUSTER_TEST(arguments, focused_tokens.error_count == 0);
    BUSTER_TEST(arguments, focused_parser.diagnostic_count == 0);
    AnalysisSourceInput focused_input = {
        .path = S8("focused-ir.bbb"),
        .parser = &focused_parser,
    };
    AnalysisResult focused_analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 900}, S8("focused-ir"), &focused_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &focused_analysis);
    IrModule focused_module = ir_analyze_and_generate_module(arguments->arena, &focused_analysis);
    BUSTER_TEST(arguments, focused_analysis.diagnostic_count == 0);
    u32 explicit_conversion_count = 0;
    AnalysisBody* focused_body = focused_analysis.module.bodies;
    for (AnalysisTypedExpression* expression = focused_body->first_expression; expression; expression = expression->next)
    {
        for (u32 node_index = 0; node_index < expression->ast.count; node_index += 1)
        {
            explicit_conversion_count += expression->nodes[node_index].conversion == ANALYSIS_CONVERSION_EXPLICIT;
        }
    }
    BUSTER_TEST(arguments, explicit_conversion_count == 1);
    BUSTER_TEST(arguments, focused_module.function_count == 1);
    BUSTER_TEST(arguments, focused_module.lowered_function_count == 1);
    IrFunction* focused_function = focused_module.functions;
    BUSTER_TEST(arguments, focused_function->state == IR_FUNCTION_LOWERED);
    BUSTER_TEST(arguments, focused_function->block_count == 5);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_BRANCH_IF) == 1);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_BINARY) == 3);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_CAST) == 1);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_STORE) == 0);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_LOCAL) == 0);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_LOAD) == 0);
    BUSTER_TEST(arguments, ir_test_parameter_count(focused_function) == 1);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_RETURN) == 1);
    BUSTER_TEST(arguments, ir_test_binary_operation_count(focused_function, IR_BINARY_SIGNED_LESS) == 1);
    BUSTER_TEST(arguments, ir_test_binary_operation_count(focused_function, IR_BINARY_INTEGER_ADD) == 1);
    BUSTER_TEST(arguments, ir_test_binary_operation_count(focused_function, IR_BINARY_INTEGER_SUBTRACT) == 1);
    IrValidationResult focused_validation = ir_validate_module(&focused_analysis, &focused_module);
    BUSTER_TEST(arguments, focused_validation.error == IR_VALIDATION_NONE);
    IrInstruction* focused_binary = 0;
    for (u32 instruction_index = 0; instruction_index < focused_function->instruction_count; instruction_index += 1)
    {
        IrInstruction* candidate = focused_function->instructions + instruction_index;
        if (candidate->opcode == IR_OPCODE_BINARY)
        {
            focused_binary = candidate;
            break;
        }
    }
    BUSTER_TEST(arguments, focused_binary != 0);
    if (focused_binary)
    {
        IrBinaryOperation saved_operation = focused_binary->binary_operation;
        focused_binary->binary_operation = IR_BINARY_FLOAT_ADD;
        IrValidationResult wrong_operation = ir_validate_module(&focused_analysis, &focused_module);
        BUSTER_TEST(arguments, wrong_operation.error == IR_VALIDATION_OPERATION);
        focused_binary->binary_operation = saved_operation;
        IrValidationResult restored_operation = ir_validate_module(&focused_analysis, &focused_module);
        BUSTER_TEST(arguments, restored_operation.error == IR_VALIDATION_NONE);
    }

    // Following `next` proves nothing about ownership: the chains can cycle,
    // two blocks can share one, last_instruction can name an instruction the
    // chain never reaches, and an instruction can belong to no block while
    // every later pass still reads it out of the dense array. The owner array
    // is what rules those out, and each mutation below is invisible to the
    // per-instruction checks that follow it.
    IrBlockId* focused_owners = arena_allocate(arguments->arena, IrBlockId, focused_function->instruction_count);
    IrInstructionOwnership focused_ownership = ir_function_instruction_owners(focused_function, focused_owners);
    BUSTER_TEST(arguments, focused_ownership.error == IR_VALIDATION_NONE);
    u32 focused_owned_count = 0;
    IrBlock* focused_first_block = 0;
    IrBlock* focused_second_block = 0;
    IrBlock* focused_long_block = 0;
    for (u32 block_index = 0; block_index < focused_function->block_count; block_index += 1)
    {
        IrBlock* block = focused_function->blocks + block_index;
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = focused_function->instructions[id.value].next)
        {
            BUSTER_TEST(arguments, focused_owners[id.value].value == block_index);
            focused_owned_count += 1;
        }
        if (block->first_instruction.value == IR_ID_UNDERLYING_INVALID)
        {
            continue;
        }
        if (!focused_first_block)
        {
            focused_first_block = block;
        }
        else if (!focused_second_block)
        {
            focused_second_block = block;
        }
        if (!focused_long_block && block->first_instruction.value != block->last_instruction.value)
        {
            focused_long_block = block;
        }
    }
    BUSTER_TEST(arguments, focused_owned_count == focused_function->instruction_count);
    BUSTER_TEST(arguments, focused_second_block != 0);
    BUSTER_TEST(arguments, focused_long_block != 0);
    if (focused_long_block)
    {
        IrInstructionId saved_next = focused_function->instructions[focused_long_block->last_instruction.value].next;
        focused_function->instructions[focused_long_block->last_instruction.value].next = focused_long_block->first_instruction;
        IrValidationResult cycle = ir_validate_module(&focused_analysis, &focused_module);
        BUSTER_TEST(arguments, cycle.error == IR_VALIDATION_INSTRUCTION_OWNERSHIP);
        BUSTER_TEST(arguments, cycle.block.value == focused_long_block->id.value);
        BUSTER_TEST(arguments, cycle.instruction.value == focused_long_block->first_instruction.value);
        focused_function->instructions[focused_long_block->last_instruction.value].next = saved_next;
        BUSTER_TEST(arguments, ir_validate_module(&focused_analysis, &focused_module).error == IR_VALIDATION_NONE);

        IrInstructionId saved_first = focused_long_block->first_instruction;
        focused_long_block->first_instruction = focused_function->instructions[saved_first.value].next;
        IrValidationResult unowned = ir_validate_module(&focused_analysis, &focused_module);
        BUSTER_TEST(arguments, unowned.error == IR_VALIDATION_INSTRUCTION_OWNERSHIP);
        BUSTER_TEST(arguments, unowned.instruction.value == saved_first.value);
        focused_long_block->first_instruction = saved_first;
        BUSTER_TEST(arguments, ir_validate_module(&focused_analysis, &focused_module).error == IR_VALIDATION_NONE);

        IrInstructionId saved_last = focused_long_block->last_instruction;
        focused_long_block->last_instruction = focused_long_block->first_instruction;
        IrValidationResult tail = ir_validate_module(&focused_analysis, &focused_module);
        BUSTER_TEST(arguments, tail.error == IR_VALIDATION_INSTRUCTION_OWNERSHIP);
        BUSTER_TEST(arguments, tail.block.value == focused_long_block->id.value);
        focused_long_block->last_instruction = saved_last;
        BUSTER_TEST(arguments, ir_validate_module(&focused_analysis, &focused_module).error == IR_VALIDATION_NONE);
    }
    if (focused_first_block && focused_second_block)
    {
        IrInstructionId saved_first = focused_second_block->first_instruction;
        IrInstructionId saved_last = focused_second_block->last_instruction;
        focused_second_block->first_instruction = focused_first_block->first_instruction;
        focused_second_block->last_instruction = focused_first_block->last_instruction;
        IrValidationResult shared = ir_validate_module(&focused_analysis, &focused_module);
        BUSTER_TEST(arguments, shared.error == IR_VALIDATION_INSTRUCTION_OWNERSHIP);
        BUSTER_TEST(arguments, shared.block.value == focused_second_block->id.value);
        BUSTER_TEST(arguments, shared.instruction.value == focused_first_block->first_instruction.value);
        focused_second_block->first_instruction = saved_first;
        focused_second_block->last_instruction = saved_last;
        BUSTER_TEST(arguments, ir_validate_module(&focused_analysis, &focused_module).error == IR_VALIDATION_NONE);
    }

    String8 typed_operation_source = S8("code typed_operations : fn (\n"
                                        "    signed_value: s32,\n"
                                        "    unsigned_value: u32,\n"
                                        "    float_value: f32,\n"
                                        "    pointer: &s32) bool\n"
                                        "{\n"
                                        "    data integer: s32 = -signed_value;\n"
                                        "    data floating: f32 = -float_value;\n"
                                        "    return signed_value < integer or\n"
                                        "        unsigned_value < 1 or\n"
                                        "        float_value < floating or\n"
                                        "        pointer == pointer;\n"
                                        "}\n");
    TokenizerResult typed_operation_tokens = tokenize(arguments->arena, typed_operation_source.pointer, typed_operation_source.length);
    ParserResult typed_operation_parser = parser_parse(arguments->arena, expression_arena, typed_operation_source, typed_operation_tokens);
    BUSTER_TEST(arguments, typed_operation_tokens.error_count == 0);
    BUSTER_TEST(arguments, typed_operation_parser.diagnostic_count == 0);
    AnalysisSourceInput typed_operation_input = {
        .path = S8("typed-operations-ir.bbb"),
        .parser = &typed_operation_parser,
    };
    AnalysisResult typed_operation_analysis =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 906}, S8("typed-operations-ir"), &typed_operation_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &typed_operation_analysis);
    IrModule typed_operation_module = ir_analyze_and_generate_module(arguments->arena, &typed_operation_analysis);
    BUSTER_TEST(arguments, typed_operation_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, typed_operation_module.lowered_function_count == 1);
    IrFunction* typed_operation_function = typed_operation_module.functions;
    BUSTER_TEST(arguments, ir_test_unary_operation_count(typed_operation_function, IR_UNARY_INTEGER_NEGATE) == 1);
    BUSTER_TEST(arguments, ir_test_unary_operation_count(typed_operation_function, IR_UNARY_FLOAT_NEGATE) == 1);
    BUSTER_TEST(arguments, ir_test_binary_operation_count(typed_operation_function, IR_BINARY_SIGNED_LESS) == 1);
    BUSTER_TEST(arguments, ir_test_binary_operation_count(typed_operation_function, IR_BINARY_UNSIGNED_LESS) == 1);
    BUSTER_TEST(arguments, ir_test_binary_operation_count(typed_operation_function, IR_BINARY_FLOAT_LESS) == 1);
    BUSTER_TEST(arguments, ir_test_binary_operation_count(typed_operation_function, IR_BINARY_POINTER_EQUAL) == 1);
    BUSTER_TEST(arguments, ir_test_binary_operation_count(typed_operation_function, IR_BINARY_BOOLEAN_OR) == 3);
    IrValidationResult typed_operation_validation = ir_validate_module(&typed_operation_analysis, &typed_operation_module);
    BUSTER_TEST(arguments, typed_operation_validation.error == IR_VALIDATION_NONE);

    String8 short_source = S8("code side : fn () bool\n"
                              "{\n"
                              "    return true;\n"
                              "}\n"
                              "code lazy : fn (condition: bool) bool\n"
                              "{\n"
                              "    return condition and? side() or? condition;\n"
                              "}\n");
    TokenizerResult short_tokens = tokenize(arguments->arena, short_source.pointer, short_source.length);
    ParserResult short_parser = parser_parse(arguments->arena, expression_arena, short_source, short_tokens);
    BUSTER_TEST(arguments, short_tokens.error_count == 0);
    BUSTER_TEST(arguments, short_parser.diagnostic_count == 0);
    AnalysisSourceInput short_input = {
        .path = S8("short-circuit-ir.bbb"),
        .parser = &short_parser,
    };
    AnalysisResult short_analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 901}, S8("short-circuit-ir"), &short_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &short_analysis);
    IrModule short_module = ir_analyze_and_generate_module(arguments->arena, &short_analysis);
    BUSTER_TEST(arguments, short_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, short_module.function_count == 2);
    BUSTER_TEST(arguments, short_module.lowered_function_count == 2);
    IrFunction* lazy_function = 0;
    for (u32 function_index = 0; function_index < short_module.function_count; function_index += 1)
    {
        if (string_equal(short_module.functions[function_index].name, S8("lazy")))
        {
            lazy_function = short_module.functions + function_index;
        }
    }
    BUSTER_TEST(arguments, lazy_function != 0);
    if (lazy_function)
    {
        BUSTER_TEST(arguments, lazy_function->block_count == 8);
        BUSTER_TEST(arguments, ir_test_opcode_count(lazy_function, IR_OPCODE_BRANCH_IF) == 2);
        BUSTER_TEST(arguments, ir_test_opcode_count(lazy_function, IR_OPCODE_CALL) == 1);
        BUSTER_TEST(arguments, ir_test_parameter_count(lazy_function) >= 2);
        BUSTER_TEST(arguments, ir_test_opcode_count(lazy_function, IR_OPCODE_BINARY) == 0);
    }
    IrValidationResult short_validation = ir_validate_module(&short_analysis, &short_module);
    BUSTER_TEST(arguments, short_validation.error == IR_VALIDATION_NONE);

    String8 conversion_source = S8("code literal : fn () u8 { return 1; }\n"
                                   "code widen : fn (value: u8) s32 { return @cast(value); }\n"
                                   "code sign_widen : fn (value: s8) s32 { return @cast(value); }\n"
                                   "code narrow : fn (value: s32) u8 { return @cast(value); }\n"
                                   "code reinterpret : fn (value: s32) u32 { return @cast(value); }\n"
                                   "code float_widen : fn (value: f32) f64 { return @cast(value); }\n"
                                   "code float_narrow : fn (value: f64) f32 { return @cast(value); }\n"
                                   "code signed_float : fn (value: s32) f64 { return @cast(value); }\n"
                                   "code unsigned_float : fn (value: u32) f64 { return @cast(value); }\n"
                                   "code float_signed : fn (value: f64) s32 { return @cast(value); }\n"
                                   "code float_unsigned : fn (value: f64) u32 { return @cast(value); }\n"
                                   "code pointer : fn (value: &s32) &u8 { return @cast(value); }\n");
    TokenizerResult conversion_tokens = tokenize(arguments->arena, conversion_source.pointer, conversion_source.length);
    ParserResult conversion_parser = parser_parse(arguments->arena, expression_arena, conversion_source, conversion_tokens);
    BUSTER_TEST(arguments, conversion_tokens.error_count == 0);
    BUSTER_TEST(arguments, conversion_parser.diagnostic_count == 0);
    AnalysisSourceInput conversion_input = {
        .path = S8("conversion-ir.bbb"),
        .parser = &conversion_parser,
    };
    AnalysisResult conversion_analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 902}, S8("conversion-ir"), &conversion_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &conversion_analysis);
    IrModule conversion_module = ir_analyze_and_generate_module(arguments->arena, &conversion_analysis);
    BUSTER_TEST(arguments, conversion_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, conversion_module.lowered_function_count == 12);
    BUSTER_TEST(arguments, ir_test_opcode_count(conversion_module.functions, IR_OPCODE_CAST) == 0);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_INTEGER_ZERO_EXTEND) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_INTEGER_SIGN_EXTEND) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_INTEGER_TRUNCATE) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_INTEGER_REINTERPRET) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_FLOAT_EXTEND) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_FLOAT_TRUNCATE) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER) == 1);
    BUSTER_TEST(arguments, ir_test_conversion_count(&conversion_module, IR_CONVERSION_POINTER_REINTERPRET) == 1);
    IrValidationResult conversion_validation = ir_validate_module(&conversion_analysis, &conversion_module);
    BUSTER_TEST(arguments, conversion_validation.error == IR_VALIDATION_NONE);
    IrInstruction* sign_extension = 0;
    for (u32 function_index = 0; function_index < conversion_module.function_count && !sign_extension; function_index += 1)
    {
        IrFunction* function = conversion_module.functions + function_index;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* candidate = function->instructions + instruction_index;
            if (candidate->opcode == IR_OPCODE_CAST && candidate->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND)
            {
                sign_extension = candidate;
                break;
            }
        }
    }
    BUSTER_TEST(arguments, sign_extension != 0);
    if (sign_extension)
    {
        sign_extension->conversion_operation = IR_CONVERSION_INTEGER_ZERO_EXTEND;
        IrValidationResult wrong_conversion = ir_validate_module(&conversion_analysis, &conversion_module);
        BUSTER_TEST(arguments, wrong_conversion.error == IR_VALIDATION_OPERATION);
        sign_extension->conversion_operation = IR_CONVERSION_INTEGER_SIGN_EXTEND;
        IrValidationResult restored_conversion = ir_validate_module(&conversion_analysis, &conversion_module);
        BUSTER_TEST(arguments, restored_conversion.error == IR_VALIDATION_NONE);
    }

    String8 loop_lowering_source = S8("code sum : fn () s32\n"
                                      "{\n"
                                      "    data total: s32 = 0;\n"
                                      "    for (data value = 0 .. 3)\n"
                                      "    {\n"
                                      "        total += value;\n"
                                      "    }\n"
                                      "    for (data value = @reverse(0 .. 3))\n"
                                      "    {\n"
                                      "        total += value;\n"
                                      "    }\n"
                                      "    return total;\n"
                                      "}\n");
    TokenizerResult loop_lowering_tokens = tokenize(arguments->arena, loop_lowering_source.pointer, loop_lowering_source.length);
    ParserResult loop_lowering_parser = parser_parse(arguments->arena, expression_arena, loop_lowering_source, loop_lowering_tokens);
    BUSTER_TEST(arguments, loop_lowering_tokens.error_count == 0);
    BUSTER_TEST(arguments, loop_lowering_parser.diagnostic_count == 0);
    AnalysisSourceInput loop_lowering_input = {
        .path = S8("loop-lowering-ir.bbb"),
        .parser = &loop_lowering_parser,
    };
    AnalysisResult loop_lowering_analysis =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 907}, S8("loop-lowering-ir"), &loop_lowering_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &loop_lowering_analysis);
    IrModule loop_lowering_module = ir_analyze_and_generate_module(arguments->arena, &loop_lowering_analysis);
    BUSTER_TEST(arguments, loop_lowering_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, loop_lowering_module.lowered_function_count == 1);
    IrFunction* loop_lowering_function = loop_lowering_module.functions;
    BUSTER_TEST(arguments, loop_lowering_function->block_count == 10);
    BUSTER_TEST(arguments, ir_test_opcode_count(loop_lowering_function, IR_OPCODE_LENGTH) == 2);
    BUSTER_TEST(arguments, ir_test_opcode_count(loop_lowering_function, IR_OPCODE_INDEX) == 2);
    BUSTER_TEST(arguments, ir_test_opcode_count(loop_lowering_function, IR_OPCODE_REVERSE) == 1);
    BUSTER_TEST(arguments, ir_test_binary_operation_count(loop_lowering_function, IR_BINARY_UNSIGNED_LESS) == 2);
    IrValidationResult loop_lowering_validation = ir_validate_module(&loop_lowering_analysis, &loop_lowering_module);
    BUSTER_TEST(arguments, loop_lowering_validation.error == IR_VALIDATION_NONE);

    String8 namespace_math_source = S8("code add : fn (a: s32, b: s32) s32\n"
                                       "{\n"
                                       "    return a + b;\n"
                                       "}\n"
                                       "code identity : fn ($value: $T) $T\n"
                                       "{\n"
                                       "    return value;\n"
                                       "}\n");
    String8 namespace_app_source = S8("import math = \"core/math\";\n"
                                      "code use : fn () s32\n"
                                      "{\n"
                                      "    return math.add(2, 3) + math.identity(5);\n"
                                      "}\n");
    String8 namespace_app_two_source = S8("import math = \"core/math\";\n"
                                          "code use_again : fn () s32\n"
                                          "{\n"
                                          "    return math.identity(5);\n"
                                          "}\n");
    TokenizerResult namespace_math_tokens = tokenize(arguments->arena, namespace_math_source.pointer, namespace_math_source.length);
    ParserResult namespace_math_parser = parser_parse(arguments->arena, expression_arena, namespace_math_source, namespace_math_tokens);
    TokenizerResult namespace_app_tokens = tokenize(arguments->arena, namespace_app_source.pointer, namespace_app_source.length);
    ParserResult namespace_app_parser = parser_parse(arguments->arena, expression_arena, namespace_app_source, namespace_app_tokens);
    TokenizerResult namespace_app_two_tokens = tokenize(arguments->arena, namespace_app_two_source.pointer, namespace_app_two_source.length);
    ParserResult namespace_app_two_parser = parser_parse(arguments->arena, expression_arena, namespace_app_two_source, namespace_app_two_tokens);
    AnalysisSourceInput namespace_math_input = {
        .path = S8("math.bbb"),
        .parser = &namespace_math_parser,
    };
    AnalysisSourceInput namespace_app_input = {
        .path = S8("app.bbb"),
        .parser = &namespace_app_parser,
    };
    AnalysisSourceInput namespace_app_two_input = {
        .path = S8("app-two.bbb"),
        .parser = &namespace_app_two_parser,
    };
    AnalysisResult namespace_math = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 910}, S8("core/math"), &namespace_math_input, 1);
    AnalysisResult namespace_app = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 911}, S8("app"), &namespace_app_input, 1);
    AnalysisResult namespace_app_two = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 912}, S8("app-two"), &namespace_app_two_input, 1);
    AnalysisResult* namespace_modules[] = {
        &namespace_app_two,
        &namespace_app,
        &namespace_math,
    };
    analysis_resolve_program_interfaces(arguments->arena, namespace_modules, BUSTER_ARRAY_LENGTH(namespace_modules));
    IrModule namespace_module = ir_analyze_and_generate_module(arguments->arena, &namespace_app);
    IrModule namespace_math_module = ir_analyze_and_generate_module(arguments->arena, &namespace_math);
    IrModule namespace_app_two_module = ir_analyze_and_generate_module(arguments->arena, &namespace_app_two);
    BUSTER_TEST(arguments, namespace_app.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_app_two.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_math.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_math.instantiation_count == 1);
    BUSTER_TEST(arguments, namespace_math.first_instantiation && namespace_math.first_instantiation->requester_count == 2);
    BUSTER_TEST(arguments, namespace_math.first_instantiation && namespace_math.first_instantiation->codegen_owner.value == 910);
    BUSTER_TEST(arguments, namespace_module.lowered_function_count == 1);
    BUSTER_TEST(arguments, namespace_module.function_count == 1);
    BUSTER_TEST(arguments, namespace_app_two_module.lowered_function_count == 1);
    BUSTER_TEST(arguments, namespace_app_two_module.function_count == 1);
    BUSTER_TEST(arguments, namespace_math_module.lowered_function_count == 2);
    BUSTER_TEST(arguments, namespace_math_module.function_count == 2);
    IrFunction* namespace_use = namespace_module.functions;
    BUSTER_TEST(arguments, ir_test_opcode_count(namespace_use, IR_OPCODE_FUNCTION) == 2);
    BUSTER_TEST(arguments, ir_test_opcode_count(namespace_use, IR_OPCODE_CALL) == 2);
    bool found_external_reference = false;
    for (u32 instruction_index = 0; instruction_index < namespace_use->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = namespace_use->instructions + instruction_index;
        if (instruction->opcode == IR_OPCODE_FUNCTION)
        {
            found_external_reference |= instruction->entity.module.value == 910;
        }
    }
    BUSTER_TEST(arguments, found_external_reference);
    IrValidationResult namespace_validation = ir_validate_module(&namespace_app, &namespace_module);
    BUSTER_TEST(arguments, namespace_validation.error == IR_VALIDATION_NONE);
    IrValidationResult namespace_app_two_validation = ir_validate_module(&namespace_app_two, &namespace_app_two_module);
    BUSTER_TEST(arguments, namespace_app_two_validation.error == IR_VALIDATION_NONE);
    IrValidationResult namespace_math_validation = ir_validate_module(&namespace_math, &namespace_math_module);
    BUSTER_TEST(arguments, namespace_math_validation.error == IR_VALIDATION_NONE);
    AnalysisProgram namespace_program_analysis = {
        .module_results = namespace_modules,
        .module_count = BUSTER_ARRAY_LENGTH(namespace_modules),
    };
    IrProgram namespace_program = ir_generate_program(arguments->arena, &namespace_program_analysis);
    u32 namespace_specialized_function_count = 0;
    String8 namespace_specialized_name = {0};
    for (u32 module_index = 0; module_index < namespace_program.module_count; module_index += 1)
    {
        IrModule* program_module = namespace_program.modules + module_index;
        for (u32 function_index = 0; function_index < program_module->function_count; function_index += 1)
        {
            IrFunction* function = program_module->functions + function_index;
            if (function->instantiation.value != ANALYSIS_ID_UNDERLYING_INVALID)
            {
                namespace_specialized_function_count += 1;
                namespace_specialized_name = function->name;
            }
        }
    }
    BUSTER_TEST(arguments, namespace_specialized_function_count == 1);
    BUSTER_TEST(arguments, namespace_math.first_instantiation && string_equal(namespace_specialized_name, namespace_math.first_instantiation->symbol_name));

    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(ir_fixture_tests); fixture_index += 1)
    {
        TemporalArena fixture_temporary = arena_begin_temporal(arguments->arena);
        IrFixtureTest fixture = ir_fixture_tests[fixture_index];
        FileMapRead source_file = file_map_read(arguments->arena, fixture.path, (FileReadOptions){0});
        String8 source = BYTE_SLICE_TO_STRING(8, source_file.bytes);
        BUSTER_TEST(arguments, source.pointer != 0);
        TokenizerResult tokenizer = tokenize(arguments->arena, source.pointer, source.length);
        ParserResult parser = parser_parse(arguments->arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, parser.diagnostic_count == 0);
        AnalysisSourceInput input = {.path = fixture.path, .parser = &parser};
        AnalysisResult analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 1000 + fixture_index}, S8("ir-fixture"), &input, 1);
        analysis_resolve_module_interfaces(arguments->arena, &analysis);
        IrModule module = ir_analyze_and_generate_module(arguments->arena, &analysis);
        u32 generic_code_count = 0;
        for (u32 entity_index = 0; entity_index < analysis.module.entity_count; entity_index += 1)
        {
            AnalysisEntity* entity = analysis.module.entities + entity_index;
            if (entity->kind == ANALYSIS_ENTITY_CODE && analysis_entity_is_generic(fixture_temporary.arena, &analysis, entity))
            {
                generic_code_count += 1;
            }
        }
        BUSTER_TEST(arguments, module.function_count == parser.code_count - generic_code_count + analysis.instantiation_count);
        BUSTER_TEST(arguments, module.lowered_function_count + module.rejected_function_count <= module.function_count);
        IrValidationResult validation = ir_validate_module(&analysis, &module);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
        if (string_equal(fixture.path, S8("tests/basic_vector.bbb")))
        {
            AnalysisEntity* main_entity = 0;
            for (u32 entity_index = 0; entity_index < analysis.module.entity_count; entity_index += 1)
            {
                AnalysisEntity* candidate = analysis.module.entities + entity_index;
                if (candidate->kind == ANALYSIS_ENTITY_CODE && string_equal(candidate->name, S8("main")))
                {
                    main_entity = candidate;
                    break;
                }
            }
            IrFunction* main_function = 0;
            if (main_entity)
            {
                for (u32 function_index = 0; function_index < module.function_count; function_index += 1)
                {
                    IrFunction* candidate = module.functions + function_index;
                    if (candidate->entity.module.value == main_entity->id.module.value && candidate->entity.index.value == main_entity->id.index.value)
                    {
                        main_function = candidate;
                        break;
                    }
                }
            }
            BUSTER_TEST(arguments, main_function != 0);
            if (main_function)
            {
                BUSTER_TEST(arguments, ir_test_binary_operation_count(main_function, IR_BINARY_VECTOR_FLOAT_ADD) == 1);
                BUSTER_TEST(arguments, ir_test_binary_operation_count(main_function, IR_BINARY_VECTOR_FLOAT_LESS) == 1);
                BUSTER_TEST(arguments, ir_test_binary_operation_count(main_function, IR_BINARY_VECTOR_FLOAT_EQUAL) == 1);
                BUSTER_TEST(arguments, ir_test_opcode_count(main_function, IR_OPCODE_UNARY) == 1);
                BUSTER_TEST(arguments, ir_test_opcode_count(main_function, IR_OPCODE_INDEX) == 1);
            }
        }
        if (string_equal(fixture.path, S8("tests/basic_pointer.bbb")))
        {
            BUSTER_TEST(arguments, module.function_count == 1);
            AnalysisLocal* number = 0;
            AnalysisBody* pointer_body = analysis.module.bodies;
            for (u32 local_index = 0; local_index < pointer_body->local_count; local_index += 1)
            {
                if (string_equal(pointer_body->locals[local_index].name, S8("number")))
                {
                    number = pointer_body->locals + local_index;
                }
            }
            BUSTER_TEST(arguments, number != 0);
            if (number)
            {
                BUSTER_TEST(arguments, number->address_taken);
                BUSTER_TEST(arguments, number->requires_storage);
            }
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_LOCAL) >= 1);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_LOAD) >= 1);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_STORE) >= 1);
        }
        else if (string_equal(fixture.path, S8("tests/basic_variadic.bbb")))
        {
            BUSTER_TEST(arguments, module.function_count == 5);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_VA_START) == 1);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_VA_COPY) == 1);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_VA_ARG) == 1);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_VA_END) == 2);
        }
        else if (string_equal(fixture.path, S8("tests/basic_for.bbb")))
        {
            BUSTER_TEST(arguments, module.function_count == 1);
            BUSTER_TEST(arguments, ir_test_parameter_count(module.functions) > 0);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_LOCAL) == 0);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_LOAD) == 0);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_STORE) == 0);
        }
        else if (string_equal(fixture.path, S8("tests/basic_compile_time.bbb")))
        {
            BUSTER_TEST(arguments, analysis.instantiation_count == 3);
            BUSTER_TEST(arguments, module.function_count == 4);
            BUSTER_TEST(arguments, module.lowered_function_count == 4);
            IrFunction* main_function = 0;
            u32 specialized_count = 0;
            for (u32 function_index = 0; function_index < module.function_count; function_index += 1)
            {
                IrFunction* function = module.functions + function_index;
                if (string_equal(function->name, S8("main")))
                {
                    main_function = function;
                }
                if (function->instantiation.value != ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    AnalysisInstantiation* instantiation = ir_instantiation_from_id(&analysis, function->instantiation);
                    AnalysisType* concrete = analysis_type_from_id(&analysis, function->type);
                    BUSTER_TEST(arguments, instantiation != 0);
                    BUSTER_TEST(arguments, instantiation && string_equal(function->name, instantiation->symbol_name));
                    BUSTER_TEST(arguments, concrete->kind == ANALYSIS_TYPE_FUNCTION);
                    BUSTER_TEST(arguments, concrete->kind != ANALYSIS_TYPE_FUNCTION || concrete->as.function.argument_count == 0);
                    specialized_count += 1;
                }
            }
            BUSTER_TEST(arguments, specialized_count == 3);
            BUSTER_TEST(arguments, main_function != 0);
            if (main_function)
            {
                u32 call_count = 0;
                IrInstruction* first_call = 0;
                AnalysisInstantiationId first = ANALYSIS_INSTANTIATION_ID_INVALID;
                AnalysisInstantiationId second = ANALYSIS_INSTANTIATION_ID_INVALID;
                for (u32 instruction_index = 0; instruction_index < main_function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = main_function->instructions + instruction_index;
                    if (instruction->opcode != IR_OPCODE_CALL)
                    {
                        continue;
                    }
                    BUSTER_TEST(arguments, instruction->operand_count == 1);
                    BUSTER_TEST(arguments, instruction->instantiation.value != ANALYSIS_ID_UNDERLYING_INVALID);
                    if (call_count == 0)
                    {
                        first_call = instruction;
                        first = instruction->instantiation;
                    }
                    else if (call_count == 1)
                    {
                        second = instruction->instantiation;
                    }
                    call_count += 1;
                }
                BUSTER_TEST(arguments, call_count == 4);
                BUSTER_TEST(arguments, first.value == second.value);
                BUSTER_TEST(arguments, first_call != 0);
                if (first_call)
                {
                    u32 saved_operand_count = first_call->operand_count;
                    first_call->operand_count = 0;
                    IrValidationResult missing_target = ir_validate_module(&analysis, &module);
                    BUSTER_TEST(arguments, missing_target.error == IR_VALIDATION_CALL_TARGET);
                    first_call->operand_count = saved_operand_count;

                    IrValue* call_target = main_function->values + first_call->operands[0].value;
                    AnalysisType* call_signature = analysis_type_from_id(&analysis, call_target->type);
                    AnalysisTypeId saved_return_type = call_signature->as.function.return_type;
                    call_signature->as.function.return_type = analysis.types.builtin.bool_type;
                    IrValidationResult wrong_signature = ir_validate_module(&analysis, &module);
                    BUSTER_TEST(arguments, wrong_signature.error == IR_VALIDATION_CALL_SIGNATURE);
                    call_signature->as.function.return_type = saved_return_type;

                    AnalysisInstantiationId saved_instantiation = first_call->instantiation;
                    first_call->instantiation = ANALYSIS_INSTANTIATION_ID_INVALID;
                    IrValidationResult wrong_specialization = ir_validate_module(&analysis, &module);
                    BUSTER_TEST(arguments, wrong_specialization.error == IR_VALIDATION_CALL_TARGET);
                    first_call->instantiation = saved_instantiation;

                    IrValidationResult restored = ir_validate_module(&analysis, &module);
                    BUSTER_TEST(arguments, restored.error == IR_VALIDATION_NONE);
                }
            }
        }
        else if (string_equal(fixture.path, S8("tests/compile_time_argument_error.bbb")))
        {
            BUSTER_TEST(arguments, analysis.instantiation_count == 0);
            BUSTER_TEST(arguments, module.function_count == 1);
            BUSTER_TEST(arguments, module.lowered_function_count == 0);
            BUSTER_TEST(arguments, module.rejected_function_count == 1);
            BUSTER_TEST(arguments, module.functions[0].state == IR_FUNCTION_REJECTED);
        }
        for (u32 function_index = 0; function_index < module.function_count; function_index += 1)
        {
            IrFunction* function = module.functions + function_index;
            if (function->state == IR_FUNCTION_LOWERED)
            {
                BUSTER_TEST(arguments, function->block_count >= 2);
                BUSTER_TEST(arguments, function->instruction_count > 0);
                BUSTER_TEST(arguments, function->value_count > 0 || analysis_type_from_id(&analysis, function->type)->as.function.argument_count == 0);
            }
            else if (function->state == IR_FUNCTION_REJECTED)
            {
                BUSTER_TEST(arguments, ir_entity_has_diagnostic(&analysis, function->entity));
                BUSTER_TEST(arguments, function->instruction_count == 0);
            }
        }
        String8 printed = ir_print_module(arguments->arena, &analysis, &module);
        BUSTER_TEST(arguments, printed.length > 0);
        BUSTER_TEST(arguments, string_starts_with_sequence(printed, S8("module ir-fixture")));
        file_map_unmap(source_file);
        arena_set_position(fixture_temporary.arena, fixture_temporary.position);
    }

    AnalysisProgram loaded_program = analysis_program_load(arguments->arena, expression_arena,
                                                           (AnalysisProgramOptions){
                                                               .root_path = S8("tests/basic_import.bbb"),
                                                               .root_module_name = S8("app"),
                                                               .module_root = S8("tests/modules"),
                                                               .pointer_size = 8,
                                                               .pointer_alignment = 8,
                                                           });
    BUSTER_TEST(arguments, !loaded_program.load_failed);
    BUSTER_TEST(arguments, loaded_program.analysis_diagnostic_count == 0);
    IrProgram generated_program = ir_generate_program(arguments->arena, &loaded_program);
    BUSTER_TEST(arguments, generated_program.module_count == 3);
    BUSTER_TEST(arguments, generated_program.lowered_function_count == 3);
    BUSTER_TEST(arguments, generated_program.rejected_function_count == 0);
    BUSTER_TEST(arguments, generated_program.types.count > 0);
    BUSTER_TEST(arguments, generated_program.symbols.count >= 3);
    BUSTER_TEST(arguments, generated_program.sources.count == 3);
    BUSTER_TEST(arguments, ir_type_from_id(&generated_program.types, IR_TYPE_ID_INVALID) == 0);
    BUSTER_TEST(arguments, ir_symbol_from_id(&generated_program.symbols, IR_SYMBOL_ID_INVALID) == 0);
    for (u32 module_index = 0; module_index < loaded_program.module_count; module_index += 1)
    {
        AnalysisResult* analysis = loaded_program.module_results[module_index];
        if (analysis)
        {
            IrModule* generated_module = generated_program.modules + module_index;
            BUSTER_TEST(arguments, generated_module->frontend_type_count == analysis->types.count);
            BUSTER_TEST(arguments, generated_module->frontend_symbol_count == analysis->module.entity_count);
            BUSTER_TEST(arguments, generated_module->frontend_source_count == analysis->module.source_count);
            for (u32 function_index = 0; function_index < generated_module->function_count; function_index += 1)
            {
                IrFunction* function = generated_module->functions + function_index;
                BUSTER_TEST(arguments, function->canonical_type.value != IR_ID_UNDERLYING_INVALID);
                BUSTER_TEST(arguments, ir_type_from_id(&generated_program.types, function->canonical_type) != 0);
                BUSTER_TEST(arguments, ir_symbol_from_id(&generated_program.symbols, function->symbol) != 0);
                if (function->instantiation.value == ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    IrSymbol* symbol = ir_symbol_from_id(&generated_program.symbols, function->symbol);
                    BUSTER_TEST(arguments, symbol != 0);
                    BUSTER_TEST(arguments, !symbol || symbol->kind == IR_SYMBOL_FUNCTION);
                    BUSTER_TEST(arguments, !symbol || symbol->type.value != IR_ID_UNDERLYING_INVALID);
                }
                if (function->state == IR_FUNCTION_LOWERED)
                {
                    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
                    {
                        BUSTER_TEST(arguments, function->values[value_index].canonical_type.value != IR_ID_UNDERLYING_INVALID);
                    }
                    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                    {
                        BUSTER_TEST(arguments, function->instructions[instruction_index].canonical_type.value != IR_ID_UNDERLYING_INVALID);
                        BUSTER_TEST(arguments, ir_instruction_canonical_source(function, function->instructions[instruction_index].id).source.value != IR_ID_UNDERLYING_INVALID);
                    }
                }
            }
            IrValidationResult validation = ir_validate_module(analysis, generated_module);
            BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
        }
    }
    analysis_program_unmap_sources(&loaded_program);
    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
