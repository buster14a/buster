// Generic Clang vector operations use canonical lane extraction, scalar
// conversion, aggregate construction, and private representation aliases.
// The parser supplies the result type and checks source ICE shuffle indices.
// Contracts: LLVM 21.1.8 LanguageExtensions.rst, SemaChecking.cpp and
// CGBuiltin.cpp. Arguments are already evaluated once, except the explicitly
// unevaluated nondeterministic-value operand, which is never passed here.
//
// Zero is one valid choice for a nondeterministic value or a shuffle -1 lane.
// It is a value of the requested type, never an uninitialized stack read.
// No CPU instruction or target-specific IR is introduced by this module.

BUSTER_C_INTERNAL IrValueId c_ir_emit_zero_value(CIntegerIrBuilder* builder, IrTypeId root_type, CToken token);

typedef enum CIrVendorGenericOperation
{
    C_IR_VENDOR_GENERIC_NONE,
    C_IR_VENDOR_GENERIC_BIT_CAST,
    C_IR_VENDOR_GENERIC_CONVERT_VECTOR,
    C_IR_VENDOR_GENERIC_SHUFFLE_VECTOR,
    C_IR_VENDOR_GENERIC_NONDETERMINISTIC_VALUE,
} CIrVendorGenericOperation;

BUSTER_C_INTERNAL bool c_ir_vendor_generic_supported(Target target, String8 name)
{
    bool result = (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64) &&
                  (string_equal(name, S8("__builtin_bit_cast")) || string_equal(name, S8("__builtin_convertvector")) ||
                   string_equal(name, S8("__builtin_shufflevector")) || string_equal(name, S8("__builtin_nondeterministic_value")));
    return result;
}

BUSTER_C_INTERNAL bool c_ir_vendor_generic_scalar(IrType const* type)
{
    bool result = type && type->layout.resolved && type->layout.size && !type->is_complex &&
                  (type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_FLOAT);
    return result;
}

BUSTER_C_INTERNAL bool c_ir_vendor_generic_vector(CIntegerIrBuilder* builder, IrTypeId type_id, IrTypeId* element_out, u32* count_out)
{
    IrType* vector = ir_type_from_id(&builder->program->types, type_id);
    IrType* element = vector ? ir_type_from_id(&builder->program->types, vector->element_type) : 0;
    bool result = vector && vector->kind == IR_TYPE_VECTOR && vector->layout.resolved &&
                  vector->element_count && vector->element_count <= UINT32_MAX && c_ir_vendor_generic_scalar(element) &&
                  element->layout.size <= UINT32_MAX / vector->element_count;
    if (result)
    {
        u64 logical_size = element->layout.size * vector->element_count;
        u64 expected_count = 0;
        u64 expected_size = 0;
        u32 expected_alignment = 0;
        // Non-power-of-two lane counts retain their logical lanes while
        // canonical object storage rounds to the target vector ABI size.
        result = c_vector_type_layout(builder->target, element->layout.size, (u32)logical_size,
                                      &expected_count, &expected_size, &expected_alignment) &&
                 vector->element_count == expected_count && vector->layout.size == expected_size;
    }
    if (result)
    {
        *element_out = vector->element_type;
        *count_out = (u32)vector->element_count;
    }
    return result;
}

// Result types already exist in the parser's canonical type map. Construct
// their exact lane count rather than imposing the vendor helper's 64-lane
// storage bound. The caller casts lanes before this function, so every ARRAY
// operand has the exact canonical element type.
BUSTER_C_INTERNAL IrValueId c_ir_vendor_generic_construct(CIntegerIrBuilder* builder, IrTypeId type, IrValueId* lanes,
                                                          u32 count, IrSourceRange source)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    IrTypeId element = IR_TYPE_ID_INVALID;
    u32 expected = 0;
    bool valid = lanes && c_ir_vendor_generic_vector(builder, type, &element, &expected) && expected == count;
    for (u32 index = 0; valid && index < count; index += 1)
    {
        valid = lanes[index].value < builder->function->value_count &&
                builder->function->values[lanes[index].value].category == IR_VALUE_VALUE &&
                builder->function->values[lanes[index].value].canonical_type.value == element.value;
    }
    if (valid)
    {
        result = c_ir_add_result(builder, type);
        IrInstruction instruction = c_ir_instruction_initialize(IR_OPCODE_ARRAY, type);
        instruction.operands = lanes;
        instruction.operand_count = count;
        instruction.result = result;
        if (c_ir_append_instruction(builder, instruction, source).value == IR_ID_UNDERLYING_INVALID)
        {
            result = IR_VALUE_ID_INVALID;
        }
    }
    return result;
}

// Use an unsigned pointer-width index so canonical vectors do not acquire
// an accidental signed-int lane bound. There is no dynamic memory access:
// INDEX names a lane in an already evaluated vector value.
BUSTER_C_INTERNAL IrValueId c_ir_vendor_generic_extract(CIntegerIrBuilder* builder, IrValueId vector, u32 lane,
                                                        IrTypeId index_type, IrSourceRange source)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    IrTypeId element = IR_TYPE_ID_INVALID;
    u32 count = 0;
    bool valid = vector.value < builder->function->value_count &&
                 c_ir_vendor_generic_vector(builder, builder->function->values[vector.value].canonical_type, &element, &count) &&
                 lane < count && index_type.value != IR_ID_UNDERLYING_INVALID;
    if (valid)
    {
        IrValueId index = c_ir_emit_integer_value_at(builder, lane, false, source, index_type);
        IrValueId place = c_ir_emit_index_place(builder, vector, index, source);
        if (place.value != IR_ID_UNDERLYING_INVALID)
        {
            result = c_ir_emit_load_place_raw(builder, place, element, source);
        }
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_emit_vendor_generic(CIntegerIrBuilder* builder, String8 name, IrTypeId result_type,
                                                     IrValueId const* args, u32 count, CToken token)
{
    CIrVendorGenericOperation operation = C_IR_VENDOR_GENERIC_NONE;
    if (string_equal(name, S8("__builtin_bit_cast")))
    {
        operation = C_IR_VENDOR_GENERIC_BIT_CAST;
    }
    else if (string_equal(name, S8("__builtin_convertvector")))
    {
        operation = C_IR_VENDOR_GENERIC_CONVERT_VECTOR;
    }
    else if (string_equal(name, S8("__builtin_shufflevector")))
    {
        operation = C_IR_VENDOR_GENERIC_SHUFFLE_VECTOR;
    }
    else if (string_equal(name, S8("__builtin_nondeterministic_value")))
    {
        operation = C_IR_VENDOR_GENERIC_NONDETERMINISTIC_VALUE;
    }

    IrValueId result = IR_VALUE_ID_INVALID;
    IrSourceRange source = c_ir_token_source_range(builder, token);
    IrType* output = ir_type_from_id(&builder->program->types, result_type);
    bool valid = operation != C_IR_VENDOR_GENERIC_NONE && output && output->layout.resolved && output->layout.size &&
                 (builder->target.cpu_arch == CPU_ARCH_X86_64 || builder->target.cpu_arch == CPU_ARCH_AARCH64);
    u64 output_size = valid ? output->layout.size : 0;
    bool output_is_scalar = valid && c_ir_vendor_generic_scalar(output);
    IrTypeId output_element = IR_TYPE_ID_INVALID;
    u32 output_count = 0;
    bool output_is_vector = valid && c_ir_vendor_generic_vector(builder, result_type, &output_element, &output_count);
    // Snapshot IDs and layout before emission can grow the program type table.
    IrTypeId first_type = IR_TYPE_ID_INVALID;
    u64 first_size = 0;
    IrTypeId first_element = IR_TYPE_ID_INVALID;
    u32 first_count = 0;
    bool first_is_vector = false;
    if (valid && count && args && args[0].value < builder->function->value_count &&
        builder->function->values[args[0].value].category == IR_VALUE_VALUE)
    {
        first_type = builder->function->values[args[0].value].canonical_type;
        IrType* first = ir_type_from_id(&builder->program->types, first_type);
        first_size = first && first->layout.resolved ? first->layout.size : 0;
        first_is_vector = c_ir_vendor_generic_vector(builder, first_type, &first_element, &first_count);
    }

    if (valid && operation == C_IR_VENDOR_GENERIC_BIT_CAST)
    {
        valid = count == 1 && first_size && first_size == output_size;
        if (valid)
        {
            result = first_type.value == result_type.value ? args[0]
                         : c_ir_emit_representation_alias_conversion(builder, args[0], result_type, source);
        }
    }
    else if (valid && operation == C_IR_VENDOR_GENERIC_NONDETERMINISTIC_VALUE)
    {
        valid = count == 0 && (output_is_scalar || output_is_vector);
        if (valid)
        {
            IrTypeId scalar_type = output_is_vector ? output_element : result_type;
            IrValueId zero = c_ir_emit_zero_value(builder, scalar_type, token);
            valid = zero.value != IR_ID_UNDERLYING_INVALID;
            if (valid)
            {
                zero = c_ir_emit_cast(builder, zero, scalar_type, source);
                valid = zero.value != IR_ID_UNDERLYING_INVALID;
            }
            if (valid && output_is_vector)
            {
                IrValueId* lanes = arena_allocate(builder->arena, IrValueId, output_count);
                for (u32 lane = 0; lane < output_count; lane += 1)
                {
                    lanes[lane] = zero;
                }
                result = c_ir_vendor_generic_construct(builder, result_type, lanes, output_count, source);
            }
            else if (valid)
            {
                result = zero;
            }
        }
    }
    else if (valid && (operation == C_IR_VENDOR_GENERIC_CONVERT_VECTOR || operation == C_IR_VENDOR_GENERIC_SHUFFLE_VECTOR))
    {
        bool shuffle = operation == C_IR_VENDOR_GENERIC_SHUFFLE_VECTOR;
        valid = output_is_vector && first_is_vector && (shuffle ? count >= 3 && count - 2 == output_count
                                                                : count == 1 && first_count == output_count);
        if (valid && shuffle)
        {
            IrTypeId second_type = args[1].value < builder->function->value_count
                                       ? builder->function->values[args[1].value].canonical_type : IR_TYPE_ID_INVALID;
            IrTypeId second_element = IR_TYPE_ID_INVALID;
            u32 second_count = 0;
            valid = args[1].value < builder->function->value_count &&
                    builder->function->values[args[1].value].category == IR_VALUE_VALUE &&
                    c_ir_vendor_generic_vector(builder, second_type, &second_element, &second_count) &&
                    first_element.value == second_element.value && first_count == second_count &&
                    output_element.value == first_element.value;
        }
        IrTypeId index_type = valid ? c_ir_vendor_unsigned_type(builder, 64) : IR_TYPE_ID_INVALID;
        IrValueId* lanes = valid ? arena_allocate(builder->arena, IrValueId, output_count) : 0;
        IrValueId zero = IR_VALUE_ID_INVALID;
        for (u32 lane = 0; valid && lane < output_count; lane += 1)
        {
            u64 selected = lane;
            bool unused = false;
            if (shuffle)
            {
                IrValueId selector = args[lane + 2];
                IrType* selector_type = selector.value < builder->function->value_count
                                           ? ir_type_from_id(&builder->program->types, builder->function->values[selector.value].canonical_type) : 0;
                u32 width = selector_type ? ir_integer_type_width(selector_type) : 0;
                valid = selector_type && width && width <= 64 &&
                        c_ir_value_integer_constant_evaluate(builder, selector, &selected);
                if (valid)
                {
                    u64 all_ones = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
                    unused = selector_type->is_signed && selected == all_ones;
                    bool negative = selector_type->is_signed && (selected & (UINT64_C(1) << (width - 1))) != 0;
                    valid = unused || (!negative && selected < (u64)first_count * 2);
                }
            }
            if (valid && unused)
            {
                if (zero.value == IR_ID_UNDERLYING_INVALID)
                {
                    zero = c_ir_emit_zero_value(builder, output_element, token);
                    if (zero.value != IR_ID_UNDERLYING_INVALID)
                    {
                        zero = c_ir_emit_cast(builder, zero, output_element, source);
                    }
                }
                lanes[lane] = zero;
            }
            else if (valid)
            {
                u32 input = shuffle && selected >= first_count;
                u32 input_lane = (u32)(selected - (input ? first_count : 0));
                lanes[lane] = c_ir_vendor_generic_extract(builder, args[input], input_lane, index_type, source);
                if (lanes[lane].value != IR_ID_UNDERLYING_INVALID)
                {
                    lanes[lane] = c_ir_emit_cast(builder, lanes[lane], output_element, source);
                }
            }
            valid = valid && lanes[lane].value != IR_ID_UNDERLYING_INVALID;
        }
        if (valid)
        {
            result = c_ir_vendor_generic_construct(builder, result_type, lanes, output_count, source);
        }
    }
    if (result.value == IR_ID_UNDERLYING_INVALID)
    {
        builder->failure_message = string_format(builder->arena, S8("unsupported or invalid generic builtin '{S8}'"), name);
    }
    return result;
}
