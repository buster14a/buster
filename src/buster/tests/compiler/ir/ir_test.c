#include <buster/tests/compiler/ir/ir_test.h>
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
    IrValidationResult result = {.error = IR_VALIDATION_INVALID_ID};
    if (global && floating.value != IR_ID_UNDERLYING_INVALID && symbol.value != IR_ID_UNDERLYING_INVALID)
    {
        result = ir_validate_canonical_module(&program, program.modules);
    }

    return result;
}

UnitTestResult ir_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};

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
        BUSTER_TEST(arguments, !ir_function_may_contain_opcodes(summary_function, IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)));
        ir_function_add_instruction(arguments->arena, summary_function,
                                    (IrInstruction){
                                        .result = IR_VALUE_ID_INVALID,
                                        .opcode = IR_OPCODE_INLINE_ASSEMBLY,
                                        .next = IR_INSTRUCTION_ID_INVALID,
                                    },
                                    (IrSourceRange){0});
        BUSTER_TEST(arguments, ir_function_may_contain_opcodes(summary_function, IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)));
        BUSTER_TEST(arguments, !ir_function_may_contain_opcodes(summary_function, IR_OPCODE_BIT(IR_OPCODE_ATOMIC_LOAD)));
    }
    // Rows written straight into `instructions` never reach the builder, so
    // the summary stays unknown and every query answers yes.
    IrFunction unbuilt_function = {0};
    BUSTER_TEST(arguments, ir_function_may_contain_opcodes(&unbuilt_function, IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)));
    return result;
}
#endif
