// Exact x86 state queries use the same canonical fixed-register inline-asm
// contract as GNU source assembly. XGETBV reads actual processor/OS state;
// callers own its runtime OSXSAVE/XCR availability guard. No target feature
// is added here, and the backend retains its instruction-admission policy.
// Contract: LLVM 21.1.8 BuiltinsX86.td; Intel SDM XGETBV, ECX selector and
// EDX:EAX result. The existing baseline-asm policy is owned by #1487/#2258.

BUSTER_C_INTERNAL IrValueId c_ir_emit_vendor_x86_query(CIntegerIrBuilder* builder, String8 name, IrValueId const* args,
                                                       u32 count, CToken token)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    bool known = string_equal(name, S8("__builtin_ia32_xgetbv"));
    bool valid = known && builder->target.cpu_arch == CPU_ARCH_X86_64 && args && count == 1 &&
                 args[0].value < builder->function->value_count;
    IrSourceRange source = c_ir_token_source_range(builder, token);
    IrTypeId word_type = IR_TYPE_ID_INVALID;
    IrTypeId result_type = IR_TYPE_ID_INVALID;
    if (valid)
    {
        IrValue* operand = builder->function->values + args[0].value;
        IrType* type = ir_type_from_id(&builder->program->types, operand->canonical_type);
        valid = operand->category == IR_VALUE_VALUE && type &&
                (type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_ENUM || type->kind == IR_TYPE_BOOLEAN ||
                 type->kind == IR_TYPE_FLOAT || type->is_complex);
    }
    if (valid)
    {
        // Preserve the builtin's unsigned int -> unsigned long long signature,
        // including the result's C rank on both LP64 and LLP64 targets.
        word_type = c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_INT);
        result_type = c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_LONG_LONG);
        IrType* word = ir_type_from_id(&builder->program->types, word_type);
        IrType* wide = ir_type_from_id(&builder->program->types, result_type);
        valid = word && wide && word->kind == IR_TYPE_INTEGER && wide->kind == IR_TYPE_INTEGER &&
                word->bit_width == 32 && wide->bit_width == 64 && !word->is_signed && !wide->is_signed;
    }
    IrValueId selector = IR_VALUE_ID_INVALID;
    IrValueId low_place = IR_VALUE_ID_INVALID;
    IrValueId high_place = IR_VALUE_ID_INVALID;
    if (valid)
    {
        selector = c_ir_emit_cast(builder, args[0], word_type, source);
        valid = selector.value != IR_ID_UNDERLYING_INVALID;
    }
    if (valid)
    {
        low_place = c_ir_emit_temporary(builder, word_type, source);
        high_place = c_ir_emit_temporary(builder, word_type, source);
        valid = low_place.value != IR_ID_UNDERLYING_INVALID && high_place.value != IR_ID_UNDERLYING_INVALID;
    }
    if (valid)
    {
        IrInstruction assembly = c_ir_instruction_initialize(IR_OPCODE_INLINE_ASSEMBLY, builder->void_type);
        assembly.volatile_access = true;
        assembly.operands = arena_allocate(builder->arena, IrValueId, 3);
        assembly.operands[0] = low_place;
        assembly.operands[1] = high_place;
        assembly.operands[2] = selector;
        assembly.operand_count = 3;
        assembly.immediates = arena_allocate(builder->arena, u64, 3);
        assembly.immediates[0] = IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_A;
        assembly.immediates[1] = IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_D;
        assembly.immediates[2] = IR_INLINE_ASSEMBLY_CONSTRAINT_C;
        assembly.immediate_count = 3;
        IrInstructionId instruction = c_ir_append_instruction(builder, assembly, source);
        valid = instruction.value != IR_ID_UNDERLYING_INVALID;
        if (valid)
        {
            IrInstructionExtra* extra = ir_instruction_extra_ensure(builder->arena, builder->function, instruction);
            extra->literal = S8("xgetbv");
            extra->operand_names = arena_allocate(builder->arena, String8, 3);
            extra->operand_name_count = 3;
            for (u32 operand = 0; operand < 3; operand += 1)
            {
                extra->operand_names[operand] = (String8){0};
            }
            // Both outputs are places, exactly as in "=a"(lo), "=d"(hi).
            // The inline-asm barrier makes direct SSA restore these locals
            // and their subsequent loads to memory before final validation.
            // ECX is a value input and is not changed by this instruction.
        }
    }
    if (valid)
    {
        IrValueId low = c_ir_emit_load_place_raw(builder, low_place, word_type, source);
        IrValueId high = c_ir_emit_load_place_raw(builder, high_place, word_type, source);
        valid = low.value != IR_ID_UNDERLYING_INVALID && high.value != IR_ID_UNDERLYING_INVALID;
        if (valid)
        {
            low = c_ir_emit_cast(builder, low, result_type, source);
            high = c_ir_emit_cast(builder, high, result_type, source);
            valid = low.value != IR_ID_UNDERLYING_INVALID && high.value != IR_ID_UNDERLYING_INVALID;
        }
        if (valid)
        {
            IrValueId shift = c_ir_emit_integer_value_at(builder, 32, false, source, result_type);
            high = c_ir_emit_binary_value(builder, high, shift, result_type, IR_BINARY_SHIFT_LEFT, source);
            valid = high.value != IR_ID_UNDERLYING_INVALID;
            if (valid)
            {
                result = c_ir_emit_binary_value(builder, high, low, result_type, IR_BINARY_INTEGER_BITWISE_OR, source);
            }
        }
    }
    if (result.value == IR_ID_UNDERLYING_INVALID)
    {
        builder->failure_message = string_format(builder->arena, S8("unsupported or invalid x86 query builtin '{S8}'"), name);
    }
    return result;
}

// Microsoft x64's __cpuidex uses the same canonical fixed-register CPUID row
// as constrained GNU assembly. The EAX and ECX inputs tie to their respective
// outputs; four 32-bit output places are then copied to the caller's int array.
BUSTER_C_INTERNAL IrValueId c_ir_emit_vendor_cpuidex(CIntegerIrBuilder* builder, String8 name, IrValueId const* args,
                                                     u32 count, CToken token)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    bool valid = string_equal(name, S8("__cpuidex")) && builder->target.cpu_arch == CPU_ARCH_X86_64 &&
                 builder->target.os == OPERATING_SYSTEM_WINDOWS && args && count == 3;
    IrSourceRange source = c_ir_token_source_range(builder, token);
    IrTypeId int_type = builder->s32_type;
    if (valid)
    {
        for (u32 argument = 0; valid && argument < count; argument += 1)
        {
            valid = args[argument].value < builder->function->value_count;
        }
    }
    if (valid)
    {
        IrValue* output = builder->function->values + args[0].value;
        IrType* pointer = ir_type_from_id(&builder->program->types, output->canonical_type);
        IrType* element = pointer && pointer->kind == IR_TYPE_POINTER
                              ? ir_type_from_id(&builder->program->types, pointer->element_type)
                              : 0;
        valid = output->category == IR_VALUE_VALUE && pointer && pointer->kind == IR_TYPE_POINTER &&
                element && element->kind == IR_TYPE_INTEGER && element->bit_width == 32 && element->is_signed;
    }
    for (u32 argument = 1; valid && argument < count; argument += 1)
    {
        IrValue* input = builder->function->values + args[argument].value;
        IrType* type = ir_type_from_id(&builder->program->types, input->canonical_type);
        valid = input->category == IR_VALUE_VALUE && type && type->kind == IR_TYPE_INTEGER &&
                type->bit_width == 32 && type->is_signed;
    }
    IrValueId outputs[4];
    if (valid)
    {
        for (u32 output = 0; valid && output < BUSTER_ARRAY_LENGTH(outputs); output += 1)
        {
            outputs[output] = c_ir_emit_temporary(builder, int_type, source);
            valid = outputs[output].value != IR_ID_UNDERLYING_INVALID;
        }
    }
    if (valid)
    {
        IrInstruction assembly = c_ir_instruction_initialize(IR_OPCODE_INLINE_ASSEMBLY, builder->void_type);
        assembly.volatile_access = true;
        assembly.operands = arena_allocate(builder->arena, IrValueId, 6);
        for (u32 output = 0; output < BUSTER_ARRAY_LENGTH(outputs); output += 1)
        {
            assembly.operands[output] = outputs[output];
        }
        assembly.operands[4] = args[1];
        assembly.operands[5] = args[2];
        assembly.operand_count = 6;
        assembly.immediates = arena_allocate(builder->arena, u64, 6);
        assembly.immediates[0] = IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_A;
        assembly.immediates[1] = IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_B;
        assembly.immediates[2] = IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_C;
        assembly.immediates[3] = IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_D;
        assembly.immediates[4] = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH | IR_INLINE_ASSEMBLY_CONSTRAINT_A;
        assembly.immediates[5] = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH | IR_INLINE_ASSEMBLY_CONSTRAINT_C |
                                  ((u64)2 << IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_SHIFT);
        assembly.immediate_count = 6;
        IrInstructionId instruction = c_ir_append_instruction(builder, assembly, source);
        valid = instruction.value != IR_ID_UNDERLYING_INVALID;
        if (valid)
        {
            IrInstructionExtra* extra = ir_instruction_extra_ensure(builder->arena, builder->function, instruction);
            extra->literal = S8("cpuid");
            extra->operand_names = arena_allocate(builder->arena, String8, 6);
            extra->operand_name_count = 6;
            for (u32 operand = 0; operand < 6; operand += 1)
            {
                extra->operand_names[operand] = (String8){0};
            }
        }
    }
    for (u32 output = 0; valid && output < BUSTER_ARRAY_LENGTH(outputs); output += 1)
    {
        IrValueId value = c_ir_emit_load_place_raw(builder, outputs[output], int_type, source);
        IrValueId index = c_ir_emit_integer_value_at(builder, output, false, source, int_type);
        IrValueId place = c_ir_emit_index_place(builder, args[0], index, source);
        valid = value.value != IR_ID_UNDERLYING_INVALID && index.value != IR_ID_UNDERLYING_INVALID &&
                place.value != IR_ID_UNDERLYING_INVALID;
        if (valid)
        {
            valid = c_ir_emit_store_place(builder, place, int_type, value, source);
        }
    }
    if (valid)
    {
        result = c_ir_emit_integer_value_at(builder, 0, false, source, int_type);
    }
    if (result.value == IR_ID_UNDERLYING_INVALID && !builder->failure_message.length)
    {
        builder->failure_message = S8("unsupported or invalid Microsoft __cpuidex builtin");
    }
    return result;
}
