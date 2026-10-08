// Runtime-count semantics for the five existing SSE2 scalar-count shifts.
// Included by c_gen.c after c_vendor_lowering.c supplies its lane helpers.
// These BuiltinsX86.td entries take ordinary int, unlike byte-shift immediate
// builtins. Every shift count below is in range before a scalar shift is emitted.

BUSTER_C_INTERNAL CVendorBuiltinBudget c_ir_sse2_runtime_shift_budget(String8 name)
{
    bool supported = c_ir_sse2_immediate_shift_builtin(name) != C_IR_SSE2_IMMEDIATE_SHIFT_NONE;
    CVendorBuiltinBudget result = supported ? (CVendorBuiltinBudget){96, 96, 0} : (CVendorBuiltinBudget){0};
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_emit_sse2_runtime_shift(CIntegerIrBuilder* builder,
                                                        CIrSse2ImmediateShiftBuiltin entry,
                                                        IrValueId input, IrValueId count, CToken token)
{
    IrSourceRange source = c_ir_token_source_range(builder, token);
    IrValueId result = IR_VALUE_ID_INVALID;
    bool arithmetic = entry.operation == IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT;
    bool logical = entry.operation == IR_BINARY_VECTOR_SHIFT_LEFT || entry.operation == IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT;
    bool shape = (entry.lane_width == 32 && entry.lane_count == 4) ||
                 (entry.lane_width == 64 && entry.lane_count == 2);
    IrType* count_record = c_ir_vendor_value_type(builder, count);
    bool scalar_count = count_record && (count_record->kind == IR_TYPE_BOOLEAN || count_record->kind == IR_TYPE_INTEGER ||
                                         count_record->kind == IR_TYPE_ENUM || count_record->kind == IR_TYPE_FLOAT || count_record->is_complex);
    bool valid = shape && (logical || (arithmetic && entry.lane_width == 32)) && scalar_count &&
                 c_ir_vendor_vector_shape(builder, input, entry.lane_width, entry.lane_count);
    if (valid)
    {
        IrTypeId result_element = c_ir_builder_scalar_type(builder, entry.lane_width == 32 ? C_TYPE_INT : C_TYPE_LONG_LONG);
        IrTypeId result_type = c_ir_vendor_vector_type(builder, result_element, entry.lane_count);
        IrTypeId u32_type = c_ir_vendor_unsigned_type(builder, 32);
        IrTypeId lane_type = arithmetic ? builder->s32_type : c_ir_vendor_unsigned_type(builder, entry.lane_width);
        // Prototype conversion comes first: wide integers and floating values
        // become ordinary int before the instruction interprets the count.
        IrValueId signed_count = c_ir_vendor_cast(builder, count, builder->s32_type, source);
        IrValueId unsigned_count = c_ir_vendor_cast(builder, signed_count, u32_type, source);
        IrValueId width = c_ir_vendor_constant(builder, entry.lane_width, u32_type, source);
        IrValueId inside = c_ir_vendor_binary(builder, unsigned_count, width, builder->bool_type,
                                              IR_BINARY_UNSIGNED_LESS, source);
        IrValueId maximum = c_ir_vendor_constant(builder, entry.lane_width - 1, u32_type, source);
        IrValueId safe_count = arithmetic
                                   ? c_ir_vendor_select(builder, inside, unsigned_count, maximum, u32_type, source)
                                   : c_ir_vendor_binary(builder, unsigned_count, maximum, u32_type,
                                                        IR_BINARY_INTEGER_BITWISE_AND, source);
        IrValueId lane_count = c_ir_vendor_cast(builder, safe_count, lane_type, source);
        IrValueId lane_mask = IR_VALUE_ID_INVALID;
        if (logical)
        {
            IrValueId truth = c_ir_vendor_cast(builder, inside, lane_type, source);
            IrValueId zero = c_ir_vendor_constant(builder, 0, lane_type, source);
            lane_mask = c_ir_vendor_binary(builder, zero, truth, lane_type, IR_BINARY_INTEGER_SUBTRACT, source);
        }
        IrValueId vector = c_ir_vendor_reinterpret(builder, input, lane_type, entry.lane_count, source);
        IrBinaryOperation operation = arithmetic ? IR_BINARY_SIGNED_SHIFT_RIGHT :
            entry.operation == IR_BINARY_VECTOR_SHIFT_LEFT ? IR_BINARY_SHIFT_LEFT : IR_BINARY_UNSIGNED_SHIFT_RIGHT;
        IrValueId lanes[4] = {IR_VALUE_ID_INVALID, IR_VALUE_ID_INVALID, IR_VALUE_ID_INVALID, IR_VALUE_ID_INVALID};
        valid = vector.value < builder->function->value_count && lane_count.value < builder->function->value_count &&
                (arithmetic || lane_mask.value < builder->function->value_count);
        for (u32 lane = 0; valid && lane < entry.lane_count; lane += 1)
        {
            IrValueId value = c_ir_vendor_extract(builder, vector, lane, source);
            IrValueId shifted = c_ir_vendor_binary(builder, value, lane_count, lane_type, operation, source);
            lanes[lane] = arithmetic ? shifted : c_ir_vendor_binary(builder, shifted, lane_mask, lane_type,
                                                                    IR_BINARY_INTEGER_BITWISE_AND, source);
            valid = lanes[lane].value < builder->function->value_count;
        }
        if (valid) result = c_ir_vendor_construct(builder, result_type, lanes, entry.lane_count, source);
    }
    if (result.value == IR_ID_UNDERLYING_INVALID)
        builder->failure_message = string_format(builder->arena, S8("could not lower runtime count of {S8}"), entry.name);
    return result;
}
