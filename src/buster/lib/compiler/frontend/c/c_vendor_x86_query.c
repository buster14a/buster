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
