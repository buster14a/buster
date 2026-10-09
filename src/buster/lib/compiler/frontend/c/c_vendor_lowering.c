// Typed vendor builtin semantic expansion. Included by c_gen.c after its
// scalar, vector, place and CFG helpers; it is not a separate translation unit.
// Entry points: c_ir_emit_vendor_builtin, the bounded per-call reservation in
// c_ir_vendor_builtin_budget, and c_semantic_vendor_* admission queries.
// The lane helpers also serve c_vendor_sha.c. Every argument is already an
// evaluated canonical value. Software expansions preserve the Intel operation
// semantics without introducing target-specific IR or widening memory reads.
// Contracts: LLVM 21.1.8 BuiltinsX86.td/SemaX86.cpp and Intel instruction
// semantics. No upstream implementation or vendor header is copied here.

typedef struct CVendorBuiltinBudget CVendorBuiltinBudget;
struct CVendorBuiltinBudget
{
    u32 instructions;
    u32 values;
    u32 blocks;
};

typedef enum CIrVendorOperation
{
    C_IR_VENDOR_NONE,
    C_IR_VENDOR_MOVEMASK_128,
    C_IR_VENDOR_MOVEMASK_256,
    C_IR_VENDOR_TEST_ZERO_256,
    C_IR_VENDOR_COMPARE_UNSIGNED_BYTE_512,
    C_IR_VENDOR_SELECT_BYTE_512,
    C_IR_VENDOR_LOAD_BYTE_MASKED_512,
    C_IR_VENDOR_PERMUTE_BYTE_512,
    C_IR_VENDOR_ZERO_HIGH_32,
    C_IR_VENDOR_ZERO_HIGH_64,
    C_IR_VENDOR_COUNT_TRAILING_32,
    C_IR_VENDOR_COUNT_TRAILING_64,
    C_IR_VENDOR_MASK_COPY,
    C_IR_VENDOR_MASK_TEST_ZERO,
    C_IR_VENDOR_SHUFFLE_DWORD,
    C_IR_VENDOR_SHUFFLE_HALFWORD_HIGH,
    C_IR_VENDOR_SHUFFLE_HALFWORD_LOW,
    C_IR_VENDOR_SHUFFLE_FLOAT,
    C_IR_VENDOR_BLEND_WORD,
    C_IR_VENDOR_SHIFT_BYTES_LEFT,
    C_IR_VENDOR_SHIFT_BYTES_RIGHT,
    C_IR_VENDOR_SHUFFLE_BYTE,
    C_IR_VENDOR_ALIGN_BYTE,
    C_IR_VENDOR_INSERT_128,
    C_IR_VENDOR_MULTIPLY_UNSIGNED_DWORD,
} CIrVendorOperation;

typedef struct CIrVendorRule CIrVendorRule;
struct CIrVendorRule
{
    String8 name;
    CVendorBuiltinBudget budget;
    u8 operation;
    u8 arguments;
    u8 immediate_argument_plus_one;
    u16 immediate_limit;
};

// Reservations count emitted rows/values rather than source tokens. A lane
// extraction costs at most three rows, an equal-size retype eight, and scalar
// selection eight. The 64-lane mask load adds exactly two blocks per lane;
// its value reservation includes the 64 result-temporary SSA parameters.
// The caller additionally reserves added blocks times eligible ambient named
// locals/parameters for incomplete SSA parameters; this table cannot bound
// those without the enclosing function's local count.
// The separate SHA module's largest body emits 104 scalar rows, with at most
// three retypes, twelve extracts, four final casts and one constructor.
BUSTER_GLOBAL_LOCAL CIrVendorRule const c_ir_vendor_rules[] = {
    {S8_INITIALIZER("__builtin_ia32_pmovmskb128"), {192, 192, 0}, C_IR_VENDOR_MOVEMASK_128, 1, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_pmovmskb256"), {352, 352, 0}, C_IR_VENDOR_MOVEMASK_256, 1, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_ptestz256"), {80, 80, 0}, C_IR_VENDOR_TEST_ZERO_256, 2, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_ucmpb512_mask"), {1024, 1024, 0}, C_IR_VENDOR_COMPARE_UNSIGNED_BYTE_512, 4, 3, 8},
    {S8_INITIALIZER("__builtin_ia32_selectb_512"), {1536, 1536, 0}, C_IR_VENDOR_SELECT_BYTE_512, 3, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_loaddquqi512_mask"), {2048, 2048, 128}, C_IR_VENDOR_LOAD_BYTE_MASKED_512, 3, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_vpermi2varqi512"), {2048, 2048, 0}, C_IR_VENDOR_PERMUTE_BYTE_512, 3, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_bzhi_si"), {48, 48, 0}, C_IR_VENDOR_ZERO_HIGH_32, 2, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_bzhi_di"), {48, 48, 0}, C_IR_VENDOR_ZERO_HIGH_64, 2, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_tzcnt_u32"), {32, 32, 0}, C_IR_VENDOR_COUNT_TRAILING_32, 1, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_tzcnt_u64"), {32, 32, 0}, C_IR_VENDOR_COUNT_TRAILING_64, 1, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_kmovq"), {2, 2, 0}, C_IR_VENDOR_MASK_COPY, 1, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_kortestzdi"), {8, 8, 0}, C_IR_VENDOR_MASK_TEST_ZERO, 2, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_pshufd"), {32, 32, 0}, C_IR_VENDOR_SHUFFLE_DWORD, 2, 2, 256},
    {S8_INITIALIZER("__builtin_ia32_pshufhw"), {64, 64, 0}, C_IR_VENDOR_SHUFFLE_HALFWORD_HIGH, 2, 2, 256},
    {S8_INITIALIZER("__builtin_ia32_pshuflw"), {64, 64, 0}, C_IR_VENDOR_SHUFFLE_HALFWORD_LOW, 2, 2, 256},
    {S8_INITIALIZER("__builtin_ia32_shufps"), {80, 80, 0}, C_IR_VENDOR_SHUFFLE_FLOAT, 3, 3, 256},
    {S8_INITIALIZER("__builtin_ia32_pblendw128"), {64, 64, 0}, C_IR_VENDOR_BLEND_WORD, 3, 3, 256},
    {S8_INITIALIZER("__builtin_ia32_pslldqi128_byteshift"), {96, 96, 0}, C_IR_VENDOR_SHIFT_BYTES_LEFT, 2, 2, 256},
    {S8_INITIALIZER("__builtin_ia32_psrldqi128_byteshift"), {96, 96, 0}, C_IR_VENDOR_SHIFT_BYTES_RIGHT, 2, 2, 256},
    {S8_INITIALIZER("__builtin_ia32_pshufb128"), {448, 448, 0}, C_IR_VENDOR_SHUFFLE_BYTE, 2, 0, 0},
    {S8_INITIALIZER("__builtin_ia32_palignr128"), {80, 80, 0}, C_IR_VENDOR_ALIGN_BYTE, 3, 3, 256},
    {S8_INITIALIZER("__builtin_ia32_insert128i256"), {40, 40, 0}, C_IR_VENDOR_INSERT_128, 3, 3, 2},
    {S8_INITIALIZER("__builtin_ia32_pmuludq128"), {64, 64, 0}, C_IR_VENDOR_MULTIPLY_UNSIGNED_DWORD, 2, 0, 0},
};

BUSTER_C_INTERNAL CIrVendorRule const* c_ir_vendor_rule(String8 name)
{
    CIrVendorRule const* result = 0;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ir_vendor_rules) && !result; index += 1)
    {
        if (string_equal(name, c_ir_vendor_rules[index].name))
        {
            result = c_ir_vendor_rules + index;
        }
    }
    return result;
}

BUSTER_C_INTERNAL bool c_ir_vendor_sha_name(String8 name)
{
    static String8 const names[] = {
        S8_INITIALIZER("__builtin_ia32_sha1rnds4"), S8_INITIALIZER("__builtin_ia32_sha1nexte"),
        S8_INITIALIZER("__builtin_ia32_sha1msg1"), S8_INITIALIZER("__builtin_ia32_sha1msg2"),
        S8_INITIALIZER("__builtin_ia32_sha256rnds2"), S8_INITIALIZER("__builtin_ia32_sha256msg1"),
        S8_INITIALIZER("__builtin_ia32_sha256msg2"),
    };
    bool result = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names) && !result; index += 1)
    {
        result = string_equal(name, names[index]);
    }
    return result;
}

BUSTER_C_INTERNAL CVendorBuiltinBudget c_ir_vendor_builtin_budget(String8 name)
{
    CIrVendorRule const* rule = c_ir_vendor_rule(name);
    CVendorBuiltinBudget result = rule ? rule->budget : (CVendorBuiltinBudget){0};
    if (c_ir_vendor_sha_name(name))
    {
        result = (CVendorBuiltinBudget){512, 512, 0};
    }
    if (string_equal(name, S8("__builtin_ia32_xgetbv")))
    {
        result = (CVendorBuiltinBudget){32, 32, 0};
    }
    CVendorBuiltinMicrosoftOperation microsoft_operation = c_vendor_builtin_microsoft_operation(name);
    if (microsoft_operation >= C_VENDOR_BUILTIN_MICROSOFT_MOVSB &&
        microsoft_operation <= C_VENDOR_BUILTIN_MICROSOFT_STOSQ)
    {
        result = (CVendorBuiltinBudget){16, 16, 0};
    }
    if (microsoft_operation == C_VENDOR_BUILTIN_MICROSOFT_CPUIDEX)
    {
        result = (CVendorBuiltinBudget){64, 64, 0};
    }
    if (microsoft_operation == C_VENDOR_BUILTIN_MICROSOFT_POPCNT ||
        microsoft_operation == C_VENDOR_BUILTIN_MICROSOFT_POPCNT64)
    {
        // The feature-absent SWAR path emits eight constants and twelve binary
        // rows; retain room for operand/result conversions without new blocks.
        result = (CVendorBuiltinBudget){64, 64, 0};
    }
    return result;
}

BUSTER_C_INTERNAL bool c_ir_vendor_generic_supported(Target target, String8 name);

BUSTER_C_SHARED bool c_semantic_vendor_builtin_supported(Target target, String8 name)
{
    CVendorBuiltinMicrosoftOperation microsoft_operation = c_vendor_builtin_microsoft_operation(name);
    bool microsoft_target_builtin = microsoft_operation != C_VENDOR_BUILTIN_MICROSOFT_NONE &&
                                    target.cpu_arch == CPU_ARCH_X86_64 && target.os == OPERATING_SYSTEM_WINDOWS;
    bool result = microsoft_target_builtin ||
                  (target.cpu_arch == CPU_ARCH_X86_64 &&
                   (c_ir_vendor_rule(name) || c_ir_vendor_sha_name(name) || string_equal(name, S8("__builtin_ia32_xgetbv")))) ||
                  c_ir_vendor_generic_supported(target, name);
    return result;
}

BUSTER_C_SHARED u64 c_semantic_vendor_immediate_limit(String8 name, u32 argument)
{
    CIrVendorRule const* rule = c_ir_vendor_rule(name);
    u64 result = rule && rule->immediate_argument_plus_one == argument + 1 ? rule->immediate_limit : 0;
    if (string_equal(name, S8("__builtin_ia32_sha1rnds4")) && argument == 2)
    {
        result = 4;
    }
    return result;
}

BUSTER_C_INTERNAL IrTypeId c_ir_vendor_unsigned_type(CIntegerIrBuilder* builder, u32 width)
{
    IrTypeId result = width == 8 ? c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_CHAR)
                      : width == 16 ? c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_SHORT)
                      : width == 32 ? c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_INT)
                      : width == 64 ? c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_LONG_LONG)
                                    : IR_TYPE_ID_INVALID;
    return result;
}

BUSTER_C_INTERNAL IrTypeId c_ir_vendor_vector_type(CIntegerIrBuilder* builder, IrTypeId element, u32 count)
{
    IrType* scalar = ir_type_from_id(&builder->program->types, element);
    IrTypeId result = IR_TYPE_ID_INVALID;
    bool valid = scalar && scalar->kind == IR_TYPE_INTEGER && scalar->layout.resolved &&
                 scalar->layout.size && count && count <= 64 && scalar->layout.size <= 64 / count;
    u64 size = valid ? scalar->layout.size * count : 0;
    for (u32 index = 0; valid && index < builder->program->types.count && result.value == IR_ID_UNDERLYING_INVALID; index += 1)
    {
        IrType* candidate = builder->program->types.types + index;
        if (candidate->kind == IR_TYPE_VECTOR && candidate->element_type.value == element.value && candidate->element_count == count &&
            candidate->layout.resolved && candidate->layout.size == size)
        {
            result = candidate->id;
        }
    }
    if (valid && result.value == IR_ID_UNDERLYING_INVALID)
    {
        u32 alignment = 1;
        while (alignment < size)
        {
            alignment *= 2;
        }
        result = ir_program_add_type(builder->program, (IrType){
            .name = S8("GNU vector"), .element_type = element, .return_type = IR_TYPE_ID_INVALID,
            .layout = {.size = size, .alignment = alignment, .resolved = true},
            .kind = IR_TYPE_VECTOR, .element_count = count, .bit_width = (u32)(size * 8),
        });
    }
    return result;
}

BUSTER_C_INTERNAL IrType* c_ir_vendor_value_type(CIntegerIrBuilder* builder, IrValueId value)
{
    IrType* result = value.value < builder->function->value_count
                         ? ir_type_from_id(&builder->program->types, builder->function->values[value.value].canonical_type) : 0;
    return result;
}

BUSTER_C_INTERNAL bool c_ir_vendor_vector_shape(CIntegerIrBuilder* builder, IrValueId value, u32 width, u32 count)
{
    IrType* vector = c_ir_vendor_value_type(builder, value);
    IrType* element = vector ? ir_type_from_id(&builder->program->types, vector->element_type) : 0;
    bool result = vector && vector->kind == IR_TYPE_VECTOR && vector->layout.resolved && vector->element_count == count &&
                  vector->layout.size == (u64)width * count / 8 && element && element->kind == IR_TYPE_INTEGER && element->bit_width == width;
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_reinterpret(CIntegerIrBuilder* builder, IrValueId value, IrTypeId element, u32 count, IrSourceRange source)
{
    IrType* original = c_ir_vendor_value_type(builder, value);
    bool valid = original && original->kind == IR_TYPE_VECTOR && original->layout.resolved;
    IrTypeId original_id = valid ? original->id : IR_TYPE_ID_INVALID;
    u64 original_size = valid ? original->layout.size : 0;
    IrTypeId type = valid ? c_ir_vendor_vector_type(builder, element, count) : IR_TYPE_ID_INVALID;
    IrType* target = ir_type_from_id(&builder->program->types, type);
    IrValueId result = IR_VALUE_ID_INVALID;
    if (valid && target && original_size == target->layout.size)
    {
        result = original_id.value == type.value ? value : c_ir_emit_representation_alias_conversion(builder, value, type, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_extract_index(CIntegerIrBuilder* builder, IrValueId vector, IrValueId index, IrSourceRange source)
{
    IrType* type = c_ir_vendor_value_type(builder, vector);
    IrValueId result = IR_VALUE_ID_INVALID;
    if (type && type->kind == IR_TYPE_VECTOR && index.value < builder->function->value_count)
    {
        IrTypeId element = type->element_type;
        IrValueId place = c_ir_emit_index_place(builder, vector, index, source);
        if (place.value != IR_ID_UNDERLYING_INVALID)
        {
            result = c_ir_emit_load_place_raw(builder, place, element, source);
        }
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_extract(CIntegerIrBuilder* builder, IrValueId vector, u32 lane, IrSourceRange source)
{
    IrType* type = c_ir_vendor_value_type(builder, vector);
    IrValueId result = IR_VALUE_ID_INVALID;
    if (type && type->kind == IR_TYPE_VECTOR && lane < type->element_count)
    {
        IrValueId index = c_ir_emit_integer_value_at(builder, lane, false, source, builder->s32_type);
        result = c_ir_vendor_extract_index(builder, vector, index, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_construct(CIntegerIrBuilder* builder, IrTypeId type, IrValueId const* lanes, u32 count, IrSourceRange source)
{
    IrType* vector = ir_type_from_id(&builder->program->types, type);
    IrValueId result = IR_VALUE_ID_INVALID;
    bool valid = vector && vector->kind == IR_TYPE_VECTOR && vector->element_count == count && count && count <= 64;
    IrTypeId element = valid ? vector->element_type : IR_TYPE_ID_INVALID;
    IrValueId* operands = valid ? arena_allocate(builder->arena, IrValueId, count) : 0;
    for (u32 index = 0; valid && index < count; index += 1)
    {
        valid = lanes[index].value < builder->function->value_count;
        if (valid)
        {
            operands[index] = c_ir_emit_cast(builder, lanes[index], element, source);
            valid = operands[index].value != IR_ID_UNDERLYING_INVALID;
        }
    }
    if (valid)
    {
        result = c_ir_add_result(builder, type);
        IrInstruction instruction = c_ir_instruction_initialize(IR_OPCODE_ARRAY, type);
        instruction.operands = operands;
        instruction.operand_count = count;
        instruction.result = result;
        if (c_ir_append_instruction(builder, instruction, source).value == IR_ID_UNDERLYING_INVALID)
        {
            result = IR_VALUE_ID_INVALID;
        }
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_binary(CIntegerIrBuilder* builder, IrValueId left, IrValueId right, IrTypeId type,
                                             IrBinaryOperation operation, IrSourceRange source)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    if (left.value < builder->function->value_count && right.value < builder->function->value_count && type.value != IR_ID_UNDERLYING_INVALID)
    {
        result = c_ir_emit_binary_value(builder, left, right, type, operation, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_cast(CIntegerIrBuilder* builder, IrValueId value, IrTypeId type, IrSourceRange source)
{
    IrValueId result = value.value < builder->function->value_count && type.value != IR_ID_UNDERLYING_INVALID
                           ? c_ir_emit_cast(builder, value, type, source) : IR_VALUE_ID_INVALID;
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_constant(CIntegerIrBuilder* builder, u64 value, IrTypeId type, IrSourceRange source)
{
    IrValueId result = type.value != IR_ID_UNDERLYING_INVALID ? c_ir_emit_integer_value_at(builder, value, false, source, type) : IR_VALUE_ID_INVALID;
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_select(CIntegerIrBuilder* builder, IrValueId condition, IrValueId yes, IrValueId no,
                                             IrTypeId type, IrSourceRange source)
{
    IrValueId truth = c_ir_vendor_cast(builder, condition, type, source);
    IrValueId zero = c_ir_vendor_constant(builder, 0, type, source);
    IrValueId mask = c_ir_vendor_binary(builder, zero, truth, type, IR_BINARY_INTEGER_SUBTRACT, source);
    IrValueId inverse = mask.value < builder->function->value_count
                             ? c_ir_emit_unary_value(builder, mask, type, IR_UNARY_INTEGER_BITWISE_NOT, source) : IR_VALUE_ID_INVALID;
    IrValueId chosen = c_ir_vendor_binary(builder, yes, mask, type, IR_BINARY_INTEGER_BITWISE_AND, source);
    IrValueId other = c_ir_vendor_binary(builder, no, inverse, type, IR_BINARY_INTEGER_BITWISE_AND, source);
    IrValueId result = c_ir_vendor_binary(builder, chosen, other, type, IR_BINARY_INTEGER_BITWISE_OR, source);
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_mask_bit(CIntegerIrBuilder* builder, IrValueId mask, u32 lane, IrSourceRange source)
{
    IrTypeId u64_type = c_ir_vendor_unsigned_type(builder, 64);
    IrValueId count = c_ir_vendor_constant(builder, lane, u64_type, source);
    IrValueId shifted = c_ir_vendor_binary(builder, mask, count, u64_type, IR_BINARY_UNSIGNED_SHIFT_RIGHT, source);
    IrValueId one = c_ir_vendor_constant(builder, 1, u64_type, source);
    IrValueId bit = c_ir_vendor_binary(builder, shifted, one, u64_type, IR_BINARY_INTEGER_BITWISE_AND, source);
    IrValueId zero = c_ir_vendor_constant(builder, 0, u64_type, source);
    IrValueId result = c_ir_vendor_binary(builder, bit, zero, builder->bool_type, IR_BINARY_INTEGER_NOT_EQUAL, source);
    return result;
}

BUSTER_C_INTERNAL bool c_ir_vendor_immediate(CIntegerIrBuilder* builder, IrValueId value, u32 limit, u32* output)
{
    u64 number = 0;
    bool result = value.value < builder->function->value_count && c_ir_value_integer_constant_evaluate(builder, value, &number) && number < limit;
    if (result)
    {
        *output = (u32)number;
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_movemask(CIntegerIrBuilder* builder, IrValueId input, u32 count, IrSourceRange source)
{
    IrTypeId u8_type = c_ir_vendor_unsigned_type(builder, 8);
    IrTypeId u32_type = c_ir_vendor_unsigned_type(builder, 32);
    IrValueId bytes = c_ir_vendor_vector_shape(builder, input, 8, count)
                          ? c_ir_vendor_reinterpret(builder, input, u8_type, count, source) : IR_VALUE_ID_INVALID;
    IrValueId result = IR_VALUE_ID_INVALID;
    if (bytes.value < builder->function->value_count)
    {
        IrValueId mask = c_ir_vendor_constant(builder, 0, u32_type, source);
        IrValueId seven = c_ir_vendor_constant(builder, 7, u32_type, source);
        for (u32 lane = 0; lane < count; lane += 1)
        {
            IrValueId byte = c_ir_vendor_extract(builder, bytes, lane, source);
            IrValueId wide = c_ir_vendor_cast(builder, byte, u32_type, source);
            IrValueId bit = c_ir_vendor_binary(builder, wide, seven, u32_type, IR_BINARY_UNSIGNED_SHIFT_RIGHT, source);
            IrValueId number = c_ir_vendor_constant(builder, lane, u32_type, source);
            bit = c_ir_vendor_binary(builder, bit, number, u32_type, IR_BINARY_SHIFT_LEFT, source);
            mask = c_ir_vendor_binary(builder, mask, bit, u32_type, IR_BINARY_INTEGER_BITWISE_OR, source);
        }
        result = c_ir_vendor_cast(builder, mask, builder->s32_type, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_test_zero(CIntegerIrBuilder* builder, IrValueId first, IrValueId second, IrSourceRange source)
{
    IrTypeId type = c_ir_vendor_unsigned_type(builder, 64);
    bool valid = c_ir_vendor_vector_shape(builder, first, 64, 4) && c_ir_vendor_vector_shape(builder, second, 64, 4);
    IrValueId a = valid ? c_ir_vendor_reinterpret(builder, first, type, 4, source) : IR_VALUE_ID_INVALID;
    IrValueId b = valid ? c_ir_vendor_reinterpret(builder, second, type, 4, source) : IR_VALUE_ID_INVALID;
    IrValueId result = IR_VALUE_ID_INVALID;
    if (a.value < builder->function->value_count && b.value < builder->function->value_count)
    {
        IrValueId zero = c_ir_vendor_constant(builder, 0, type, source);
        IrValueId accumulated = zero;
        for (u32 lane = 0; lane < 4; lane += 1)
        {
            IrValueId left = c_ir_vendor_extract(builder, a, lane, source);
            IrValueId right = c_ir_vendor_extract(builder, b, lane, source);
            IrValueId both = c_ir_vendor_binary(builder, left, right, type, IR_BINARY_INTEGER_BITWISE_AND, source);
            accumulated = c_ir_vendor_binary(builder, accumulated, both, type, IR_BINARY_INTEGER_BITWISE_OR, source);
        }
        IrValueId empty = c_ir_vendor_binary(builder, accumulated, zero, builder->bool_type, IR_BINARY_INTEGER_EQUAL, source);
        result = c_ir_vendor_cast(builder, empty, builder->s32_type, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_compare_mask(CIntegerIrBuilder* builder, IrValueId const* arguments, u32 predicate, IrSourceRange source)
{
    static IrBinaryOperation const operations[] = {
        IR_BINARY_INTEGER_EQUAL, IR_BINARY_UNSIGNED_LESS, IR_BINARY_UNSIGNED_LESS_EQUAL, IR_BINARY_INTEGER_NOT_EQUAL,
        IR_BINARY_INTEGER_NOT_EQUAL, IR_BINARY_UNSIGNED_GREATER_EQUAL, IR_BINARY_UNSIGNED_GREATER, IR_BINARY_INTEGER_EQUAL,
    };
    IrTypeId u8_type = c_ir_vendor_unsigned_type(builder, 8);
    IrTypeId u64_type = c_ir_vendor_unsigned_type(builder, 64);
    bool valid = predicate < BUSTER_ARRAY_LENGTH(operations) && c_ir_vendor_vector_shape(builder, arguments[0], 8, 64) &&
                 c_ir_vendor_vector_shape(builder, arguments[1], 8, 64);
    IrValueId a = valid ? c_ir_vendor_reinterpret(builder, arguments[0], u8_type, 64, source) : IR_VALUE_ID_INVALID;
    IrValueId b = valid ? c_ir_vendor_reinterpret(builder, arguments[1], u8_type, 64, source) : IR_VALUE_ID_INVALID;
    IrValueId input_mask = valid ? c_ir_vendor_cast(builder, arguments[3], u64_type, source) : IR_VALUE_ID_INVALID;
    IrValueId result = IR_VALUE_ID_INVALID;
    if (a.value < builder->function->value_count && b.value < builder->function->value_count && input_mask.value < builder->function->value_count)
    {
        IrValueId mask = c_ir_vendor_constant(builder, 0, u64_type, source);
        for (u32 lane = 0; lane < 64; lane += 1)
        {
            IrValueId left = c_ir_vendor_extract(builder, a, lane, source);
            IrValueId right = c_ir_vendor_extract(builder, b, lane, source);
            // Predicate 3 is FALSE and predicate 7 TRUE, independent of data.
            IrValueId compared = c_ir_vendor_binary(builder, left, predicate == 3 || predicate == 7 ? left : right,
                                                     builder->bool_type, operations[predicate], source);
            IrValueId bit = c_ir_vendor_cast(builder, compared, u64_type, source);
            IrValueId number = c_ir_vendor_constant(builder, lane, u64_type, source);
            bit = c_ir_vendor_binary(builder, bit, number, u64_type, IR_BINARY_SHIFT_LEFT, source);
            mask = c_ir_vendor_binary(builder, mask, bit, u64_type, IR_BINARY_INTEGER_BITWISE_OR, source);
        }
        result = c_ir_vendor_binary(builder, mask, input_mask, u64_type, IR_BINARY_INTEGER_BITWISE_AND, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_select_bytes(CIntegerIrBuilder* builder, IrValueId const* arguments, IrSourceRange source)
{
    IrTypeId u8_type = c_ir_vendor_unsigned_type(builder, 8);
    IrTypeId u64_type = c_ir_vendor_unsigned_type(builder, 64);
    bool valid = c_ir_vendor_vector_shape(builder, arguments[1], 8, 64) && c_ir_vendor_vector_shape(builder, arguments[2], 8, 64);
    IrTypeId output_type = valid ? builder->function->values[arguments[1].value].canonical_type : IR_TYPE_ID_INVALID;
    IrValueId mask = valid ? c_ir_vendor_cast(builder, arguments[0], u64_type, source) : IR_VALUE_ID_INVALID;
    IrValueId a = valid ? c_ir_vendor_reinterpret(builder, arguments[1], u8_type, 64, source) : IR_VALUE_ID_INVALID;
    IrValueId b = valid ? c_ir_vendor_reinterpret(builder, arguments[2], u8_type, 64, source) : IR_VALUE_ID_INVALID;
    IrValueId result = IR_VALUE_ID_INVALID;
    IrValueId lanes[64];
    valid = valid && mask.value < builder->function->value_count && a.value < builder->function->value_count && b.value < builder->function->value_count;
    for (u32 lane = 0; valid && lane < 64; lane += 1)
    {
        IrValueId condition = c_ir_vendor_mask_bit(builder, mask, lane, source);
        IrValueId yes = c_ir_vendor_extract(builder, a, lane, source);
        IrValueId no = c_ir_vendor_extract(builder, b, lane, source);
        lanes[lane] = c_ir_vendor_select(builder, condition, yes, no, u8_type, source);
        valid = lanes[lane].value < builder->function->value_count;
    }
    if (valid)
    {
        result = c_ir_vendor_construct(builder, output_type, lanes, 64, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_masked_byte_load(CIntegerIrBuilder* builder, IrValueId const* arguments, IrSourceRange source)
{
    IrType* merge_type = c_ir_vendor_value_type(builder, arguments[1]);
    IrType* pointer_type = c_ir_vendor_value_type(builder, arguments[0]);
    bool valid = c_ir_vendor_vector_shape(builder, arguments[1], 8, 64) && pointer_type && pointer_type->kind == IR_TYPE_POINTER;
    IrTypeId output_type = valid ? merge_type->id : IR_TYPE_ID_INVALID;
    IrTypeId element_type = valid ? merge_type->element_type : IR_TYPE_ID_INVALID;
    IrTypeId byte_pointer = valid ? c_ir_add_pointer_type(builder->program, builder->pointer_types, element_type) : IR_TYPE_ID_INVALID;
    IrValueId pointer = valid ? c_ir_vendor_cast(builder, arguments[0], byte_pointer, source) : IR_VALUE_ID_INVALID;
    IrValueId mask = valid ? c_ir_vendor_cast(builder, arguments[2], c_ir_vendor_unsigned_type(builder, 64), source) : IR_VALUE_ID_INVALID;
    IrValueId lanes[64];
    IrValueId result = IR_VALUE_ID_INVALID;
    valid = valid && pointer.value < builder->function->value_count && mask.value < builder->function->value_count;
    for (u32 lane = 0; valid && lane < 64; lane += 1)
    {
        IrValueId fallback = c_ir_vendor_extract(builder, arguments[1], lane, source);
        IrValueId temporary = c_ir_emit_temporary(builder, element_type, source);
        IrValueId active = c_ir_vendor_mask_bit(builder, mask, lane, source);
        IrBlockId load_block = c_ir_block_create(builder);
        IrBlockId continuation = c_ir_block_create(builder);
        IrBlockId targets[2] = {load_block, continuation};
        valid = fallback.value < builder->function->value_count && temporary.value < builder->function->value_count &&
                active.value < builder->function->value_count && load_block.value != IR_ID_UNDERLYING_INVALID &&
                continuation.value != IR_ID_UNDERLYING_INVALID &&
                c_ir_emit_store_place(builder, temporary, element_type, fallback, source) &&
                c_ir_terminate(builder, IR_OPCODE_BRANCH_IF, &active, 1, targets, 2, source) &&
                c_ir_switch_block(builder, load_block);
        if (valid)
        {
            // Forming the address and accessing the byte both occur only in
            // the active successor. Inactive lanes never touch the pointer.
            IrValueId index = c_ir_vendor_constant(builder, lane, builder->s32_type, source);
            IrValueId place = c_ir_emit_index_place(builder, pointer, index, source);
            IrValueId loaded = place.value < builder->function->value_count
                                   ? c_ir_emit_load_place_raw(builder, place, element_type, source) : IR_VALUE_ID_INVALID;
            valid = loaded.value < builder->function->value_count && c_ir_emit_store_place(builder, temporary, element_type, loaded, source) &&
                    c_ir_terminate(builder, IR_OPCODE_BRANCH, 0, 0, &continuation, 1, source) &&
                    c_ir_switch_block(builder, continuation);
        }
        if (valid)
        {
            lanes[lane] = c_ir_emit_load_place_raw(builder, temporary, element_type, source);
            valid = lanes[lane].value < builder->function->value_count;
        }
    }
    if (valid)
    {
        result = c_ir_vendor_construct(builder, output_type, lanes, 64, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_permute_bytes(CIntegerIrBuilder* builder, IrValueId const* arguments, IrSourceRange source)
{
    IrTypeId u8_type = c_ir_vendor_unsigned_type(builder, 8);
    IrTypeId u32_type = c_ir_vendor_unsigned_type(builder, 32);
    bool valid = c_ir_vendor_vector_shape(builder, arguments[0], 8, 64) && c_ir_vendor_vector_shape(builder, arguments[1], 8, 64) &&
                 c_ir_vendor_vector_shape(builder, arguments[2], 8, 64);
    IrTypeId output_type = valid ? builder->function->values[arguments[0].value].canonical_type : IR_TYPE_ID_INVALID;
    IrValueId low = valid ? c_ir_vendor_reinterpret(builder, arguments[0], u8_type, 64, source) : IR_VALUE_ID_INVALID;
    IrValueId indices = valid ? c_ir_vendor_reinterpret(builder, arguments[1], u8_type, 64, source) : IR_VALUE_ID_INVALID;
    IrValueId high = valid ? c_ir_vendor_reinterpret(builder, arguments[2], u8_type, 64, source) : IR_VALUE_ID_INVALID;
    IrValueId lanes[64];
    IrValueId result = IR_VALUE_ID_INVALID;
    valid = valid && low.value < builder->function->value_count && indices.value < builder->function->value_count &&
            high.value < builder->function->value_count;
    if (valid)
    {
        IrValueId table_mask = c_ir_vendor_constant(builder, 63, u32_type, source);
        IrValueId table_bit = c_ir_vendor_constant(builder, 64, u32_type, source);
        IrValueId zero = c_ir_vendor_constant(builder, 0, u32_type, source);
        for (u32 lane = 0; valid && lane < 64; lane += 1)
        {
            IrValueId control = c_ir_vendor_extract(builder, indices, lane, source);
            IrValueId wide = c_ir_vendor_cast(builder, control, u32_type, source);
            IrValueId index = c_ir_vendor_binary(builder, wide, table_mask, u32_type, IR_BINARY_INTEGER_BITWISE_AND, source);
            IrValueId which = c_ir_vendor_binary(builder, wide, table_bit, u32_type, IR_BINARY_INTEGER_BITWISE_AND, source);
            IrValueId upper = c_ir_vendor_binary(builder, which, zero, builder->bool_type, IR_BINARY_INTEGER_NOT_EQUAL, source);
            IrValueId a = c_ir_vendor_extract_index(builder, low, index, source);
            IrValueId b = c_ir_vendor_extract_index(builder, high, index, source);
            lanes[lane] = c_ir_vendor_select(builder, upper, b, a, u8_type, source);
            valid = lanes[lane].value < builder->function->value_count;
        }
    }
    if (valid)
    {
        result = c_ir_vendor_construct(builder, output_type, lanes, 64, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_zero_high(CIntegerIrBuilder* builder, IrValueId const* arguments, u32 width, IrSourceRange source)
{
    IrTypeId type = c_ir_vendor_unsigned_type(builder, width);
    IrValueId input = c_ir_vendor_cast(builder, arguments[0], type, source);
    IrValueId count = c_ir_vendor_cast(builder, arguments[1], type, source);
    IrValueId byte_mask = c_ir_vendor_constant(builder, 255, type, source);
    count = c_ir_vendor_binary(builder, count, byte_mask, type, IR_BINARY_INTEGER_BITWISE_AND, source);
    IrValueId bits = c_ir_vendor_constant(builder, width, type, source);
    IrValueId within = c_ir_vendor_binary(builder, count, bits, builder->bool_type, IR_BINARY_UNSIGNED_LESS, source);
    IrValueId count_mask = c_ir_vendor_constant(builder, width - 1, type, source);
    IrValueId safe_count = c_ir_vendor_binary(builder, count, count_mask, type, IR_BINARY_INTEGER_BITWISE_AND, source);
    IrValueId one = c_ir_vendor_constant(builder, 1, type, source);
    IrValueId shifted = c_ir_vendor_binary(builder, one, safe_count, type, IR_BINARY_SHIFT_LEFT, source);
    IrValueId low_mask = c_ir_vendor_binary(builder, shifted, one, type, IR_BINARY_INTEGER_SUBTRACT, source);
    IrValueId masked = c_ir_vendor_binary(builder, input, low_mask, type, IR_BINARY_INTEGER_BITWISE_AND, source);
    IrValueId result = c_ir_vendor_select(builder, within, masked, input, type, source);
    return result;
}

// The canonical count operation is given a nonzero operand. Intel TZCNT
// returns the operand width for zero, unlike the C __builtin_ctz contract.
BUSTER_C_INTERNAL IrValueId c_ir_vendor_count_trailing(CIntegerIrBuilder* builder, IrValueId input, u32 width, IrSourceRange source)
{
    IrTypeId type = c_ir_vendor_unsigned_type(builder, width);
    IrValueId operand = c_ir_vendor_cast(builder, input, type, source);
    IrValueId zero = c_ir_vendor_constant(builder, 0, type, source);
    IrValueId is_zero = c_ir_vendor_binary(builder, operand, zero, builder->bool_type, IR_BINARY_INTEGER_EQUAL, source);
    IrValueId zero_bit = c_ir_vendor_cast(builder, is_zero, type, source);
    IrValueId safe_operand = c_ir_vendor_binary(builder, operand, zero_bit, type, IR_BINARY_INTEGER_BITWISE_OR, source);
    IrValueId trailing = safe_operand.value < builder->function->value_count
                             ? c_ir_emit_unary_value(builder, safe_operand, type, IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS, source)
                             : IR_VALUE_ID_INVALID;
    IrValueId bits = c_ir_vendor_constant(builder, width, type, source);
    IrValueId empty_count = c_ir_vendor_binary(builder, zero_bit, bits, type, IR_BINARY_INTEGER_MULTIPLY, source);
    IrValueId result = c_ir_vendor_binary(builder, trailing, empty_count, type, IR_BINARY_INTEGER_ADD, source);
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_shuffle_bytes(CIntegerIrBuilder* builder, IrValueId const* arguments, IrSourceRange source)
{
    IrTypeId u8_type = c_ir_vendor_unsigned_type(builder, 8);
    IrTypeId u32_type = c_ir_vendor_unsigned_type(builder, 32);
    bool valid = c_ir_vendor_vector_shape(builder, arguments[0], 8, 16) && c_ir_vendor_vector_shape(builder, arguments[1], 8, 16);
    IrTypeId output_type = valid ? builder->function->values[arguments[0].value].canonical_type : IR_TYPE_ID_INVALID;
    IrValueId input = valid ? c_ir_vendor_reinterpret(builder, arguments[0], u8_type, 16, source) : IR_VALUE_ID_INVALID;
    IrValueId controls = valid ? c_ir_vendor_reinterpret(builder, arguments[1], u8_type, 16, source) : IR_VALUE_ID_INVALID;
    IrValueId lanes[16];
    IrValueId result = IR_VALUE_ID_INVALID;
    valid = valid && input.value < builder->function->value_count && controls.value < builder->function->value_count;
    if (valid)
    {
        IrValueId index_mask = c_ir_vendor_constant(builder, 15, u32_type, source);
        IrValueId high_bit = c_ir_vendor_constant(builder, 128, u32_type, source);
        IrValueId zero = c_ir_vendor_constant(builder, 0, u32_type, source);
        IrValueId zero_byte = c_ir_vendor_constant(builder, 0, u8_type, source);
        for (u32 lane = 0; valid && lane < 16; lane += 1)
        {
            IrValueId control = c_ir_vendor_extract(builder, controls, lane, source);
            IrValueId wide = c_ir_vendor_cast(builder, control, u32_type, source);
            IrValueId index = c_ir_vendor_binary(builder, wide, index_mask, u32_type, IR_BINARY_INTEGER_BITWISE_AND, source);
            IrValueId nonzero = c_ir_vendor_binary(builder, wide, high_bit, u32_type, IR_BINARY_INTEGER_BITWISE_AND, source);
            IrValueId cleared = c_ir_vendor_binary(builder, nonzero, zero, builder->bool_type, IR_BINARY_INTEGER_EQUAL, source);
            IrValueId chosen = c_ir_vendor_extract_index(builder, input, index, source);
            lanes[lane] = c_ir_vendor_select(builder, cleared, chosen, zero_byte, u8_type, source);
            valid = lanes[lane].value < builder->function->value_count;
        }
    }
    if (valid)
    {
        result = c_ir_vendor_construct(builder, output_type, lanes, 16, source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_fixed_shuffle(CIntegerIrBuilder* builder, IrValueId const* arguments, CIrVendorOperation operation,
                                                     u32 immediate, IrSourceRange source)
{
    bool halfword_shuffle = operation == C_IR_VENDOR_SHUFFLE_HALFWORD_HIGH || operation == C_IR_VENDOR_SHUFFLE_HALFWORD_LOW;
    bool one_vector = operation == C_IR_VENDOR_SHUFFLE_DWORD || halfword_shuffle;
    u32 width = operation == C_IR_VENDOR_SHUFFLE_DWORD || operation == C_IR_VENDOR_SHUFFLE_FLOAT ? 32 :
                operation == C_IR_VENDOR_BLEND_WORD || halfword_shuffle ? 16 :
                operation == C_IR_VENDOR_INSERT_128 ? 64 : 8;
    u32 count = operation == C_IR_VENDOR_ALIGN_BYTE ? 16 :
                operation == C_IR_VENDOR_BLEND_WORD || halfword_shuffle ? 8 : 4;
    bool valid = c_ir_vendor_vector_shape(builder, arguments[0], width, count) &&
                 (one_vector ||
                  c_ir_vendor_vector_shape(builder, arguments[1], width, operation == C_IR_VENDOR_INSERT_128 ? 2 : count));
    IrTypeId output_type = valid ? builder->function->values[arguments[0].value].canonical_type : IR_TYPE_ID_INVALID;
    IrType* vector = ir_type_from_id(&builder->program->types, output_type);
    IrTypeId element = vector ? vector->element_type : IR_TYPE_ID_INVALID;
    IrValueId result = IR_VALUE_ID_INVALID;
    IrValueId lanes[16];
    for (u32 lane = 0; valid && lane < count; lane += 1)
    {
        if (operation == C_IR_VENDOR_SHUFFLE_DWORD)
        {
            lanes[lane] = c_ir_vendor_extract(builder, arguments[0], (immediate >> (2 * lane)) & 3, source);
        }
        else if (halfword_shuffle)
        {
            u32 input_lane = lane;
            if (operation == C_IR_VENDOR_SHUFFLE_HALFWORD_HIGH && lane >= 4)
            {
                input_lane = 4 + ((immediate >> (2 * (lane - 4))) & 3);
            }
            else if (operation == C_IR_VENDOR_SHUFFLE_HALFWORD_LOW && lane < 4)
            {
                input_lane = (immediate >> (2 * lane)) & 3;
            }
            lanes[lane] = c_ir_vendor_extract(builder, arguments[0], input_lane, source);
        }
        else if (operation == C_IR_VENDOR_SHUFFLE_FLOAT)
        {
            lanes[lane] = c_ir_vendor_extract(builder, arguments[lane < 2 ? 0 : 1], (immediate >> (2 * lane)) & 3, source);
        }
        else if (operation == C_IR_VENDOR_BLEND_WORD)
        {
            lanes[lane] = c_ir_vendor_extract(builder, arguments[(immediate >> lane) & 1], lane, source);
        }
        else if (operation == C_IR_VENDOR_INSERT_128)
        {
            u32 first = immediate * 2;
            lanes[lane] = lane >= first && lane < first + 2 ? c_ir_vendor_extract(builder, arguments[1], lane - first, source)
                                                           : c_ir_vendor_extract(builder, arguments[0], lane, source);
        }
        else
        {
            u32 index = immediate + lane;
            // PALIGNR shifts the 32-byte concatenation (second, first).
            lanes[lane] = index < 16 ? c_ir_vendor_extract(builder, arguments[1], index, source)
                            : index < 32 ? c_ir_vendor_extract(builder, arguments[0], index - 16, source)
                                         : c_ir_vendor_constant(builder, 0, element, source);
        }
        valid = lanes[lane].value < builder->function->value_count;
    }
    if (valid)
    {
        result = c_ir_vendor_construct(builder, output_type, lanes, count, source);
    }
    return result;
}

// PSLLDQ/PSRLDQ move the whole 128-bit representation, rather than shifting
// each 64-bit lane. Literal byte counts at or above sixteen clear every byte.
BUSTER_C_INTERNAL IrValueId c_ir_vendor_shift_bytes(CIntegerIrBuilder* builder, IrValueId input, bool left, u32 immediate,
                                                   IrSourceRange source)
{
    IrTypeId element = c_ir_vendor_unsigned_type(builder, 8);
    bool valid = c_ir_vendor_vector_shape(builder, input, 64, 2) ||
                 c_ir_vendor_vector_shape(builder, input, 8, 16);
    IrTypeId output_type = valid ? builder->function->values[input.value].canonical_type : IR_TYPE_ID_INVALID;
    IrValueId bytes = valid ? c_ir_vendor_reinterpret(builder, input, element, 16, source) : IR_VALUE_ID_INVALID;
    IrValueId result = IR_VALUE_ID_INVALID;
    IrValueId lanes[16];
    valid = valid && bytes.value < builder->function->value_count;
    for (u32 lane = 0; valid && lane < 16; lane += 1)
    {
        // Unsigned underflow on the left selects the zero case, so no
        // out-of-range extraction or scalar shift is emitted.
        u32 index = left ? lane - immediate : lane + immediate;
        lanes[lane] = index < 16 ? c_ir_vendor_extract(builder, bytes, index, source)
                                : c_ir_vendor_constant(builder, 0, element, source);
        valid = lanes[lane].value < builder->function->value_count;
    }
    if (valid)
    {
        IrTypeId bytes_type = builder->function->values[bytes.value].canonical_type;
        result = c_ir_vendor_construct(builder, bytes_type, lanes, 16, source);
        if (result.value < builder->function->value_count)
        {
            result = c_ir_emit_representation_alias_conversion(builder, result, output_type, source);
        }
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_multiply_unsigned_dwords(CIntegerIrBuilder* builder, IrValueId const* arguments, IrSourceRange source)
{
    IrTypeId u32_type = c_ir_vendor_unsigned_type(builder, 32);
    IrTypeId u64_type = c_ir_vendor_unsigned_type(builder, 64);
    IrTypeId result_element = c_ir_builder_scalar_type(builder, C_TYPE_LONG_LONG);
    IrTypeId result_type = c_ir_vendor_vector_type(builder, result_element, 2);
    bool valid = c_ir_vendor_vector_shape(builder, arguments[0], 32, 4) && c_ir_vendor_vector_shape(builder, arguments[1], 32, 4);
    IrValueId a = valid ? c_ir_vendor_reinterpret(builder, arguments[0], u32_type, 4, source) : IR_VALUE_ID_INVALID;
    IrValueId b = valid ? c_ir_vendor_reinterpret(builder, arguments[1], u32_type, 4, source) : IR_VALUE_ID_INVALID;
    IrValueId lanes[2];
    IrValueId result = IR_VALUE_ID_INVALID;
    valid = valid && a.value < builder->function->value_count && b.value < builder->function->value_count;
    for (u32 lane = 0; valid && lane < 2; lane += 1)
    {
        IrValueId left = c_ir_vendor_extract(builder, a, lane * 2, source);
        IrValueId right = c_ir_vendor_extract(builder, b, lane * 2, source);
        left = c_ir_vendor_cast(builder, left, u64_type, source);
        right = c_ir_vendor_cast(builder, right, u64_type, source);
        lanes[lane] = c_ir_vendor_binary(builder, left, right, u64_type, IR_BINARY_INTEGER_MULTIPLY, source);
        valid = lanes[lane].value < builder->function->value_count;
    }
    if (valid)
    {
        result = c_ir_vendor_construct(builder, result_type, lanes, 2, source);
    }
    return result;
}

// The additional modules share the lane helpers above. These declarations let
// the textual includes follow this file without adding compiler callbacks.
BUSTER_C_INTERNAL IrValueId c_ir_emit_vendor_sha(CIntegerIrBuilder* builder, String8 name, IrValueId const* arguments, u32 count, CToken token);
BUSTER_C_INTERNAL IrValueId c_ir_emit_vendor_x86_query(CIntegerIrBuilder* builder, String8 name, IrValueId const* arguments, u32 count, CToken token);
BUSTER_C_INTERNAL IrValueId c_ir_emit_vendor_cpuidex(CIntegerIrBuilder* builder, String8 name, IrValueId const* arguments, u32 count, CToken token);
BUSTER_C_INTERNAL IrValueId c_ir_emit_vendor_microsoft_memory(CIntegerIrBuilder* builder, String8 name,
                                                              IrValueId const* arguments, u32 count, CToken token);

BUSTER_C_INTERNAL IrValueId c_ir_emit_vendor_builtin(CIntegerIrBuilder* builder, String8 name, IrValueId const* arguments, u32 count, CToken token)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    CIrVendorRule const* rule = c_ir_vendor_rule(name);
    IrSourceRange source = c_ir_token_source_range(builder, token);
    bool valid = builder->target.cpu_arch == CPU_ARCH_X86_64 && arguments && rule && count == rule->arguments;
    u32 immediate = 0;
    for (u32 argument = 0; valid && argument < count; argument += 1)
    {
        valid = arguments[argument].value < builder->function->value_count;
    }
    if (valid && rule->immediate_argument_plus_one)
    {
        valid = c_ir_vendor_immediate(builder, arguments[rule->immediate_argument_plus_one - 1], rule->immediate_limit, &immediate);
    }
    if (valid)
    {
        switch ((CIrVendorOperation)rule->operation)
        {
        case C_IR_VENDOR_MOVEMASK_128:
        case C_IR_VENDOR_MOVEMASK_256:
            result = c_ir_vendor_movemask(builder, arguments[0], rule->operation == C_IR_VENDOR_MOVEMASK_128 ? 16 : 32, source);
            break;
        case C_IR_VENDOR_TEST_ZERO_256:
            result = c_ir_vendor_test_zero(builder, arguments[0], arguments[1], source);
            break;
        case C_IR_VENDOR_COMPARE_UNSIGNED_BYTE_512:
            result = c_ir_vendor_compare_mask(builder, arguments, immediate, source);
            break;
        case C_IR_VENDOR_SELECT_BYTE_512:
            result = c_ir_vendor_select_bytes(builder, arguments, source);
            break;
        case C_IR_VENDOR_LOAD_BYTE_MASKED_512:
            result = c_ir_vendor_masked_byte_load(builder, arguments, source);
            break;
        case C_IR_VENDOR_PERMUTE_BYTE_512:
            result = c_ir_vendor_permute_bytes(builder, arguments, source);
            break;
        case C_IR_VENDOR_ZERO_HIGH_32:
        case C_IR_VENDOR_ZERO_HIGH_64:
            result = c_ir_vendor_zero_high(builder, arguments, rule->operation == C_IR_VENDOR_ZERO_HIGH_32 ? 32 : 64, source);
            break;
        case C_IR_VENDOR_COUNT_TRAILING_32:
        case C_IR_VENDOR_COUNT_TRAILING_64:
            result = c_ir_vendor_count_trailing(builder, arguments[0], rule->operation == C_IR_VENDOR_COUNT_TRAILING_32 ? 32 : 64, source);
            break;
        case C_IR_VENDOR_MASK_COPY:
            result = c_ir_vendor_cast(builder, arguments[0], c_ir_vendor_unsigned_type(builder, 64), source);
            break;
        case C_IR_VENDOR_MASK_TEST_ZERO:
        {
            IrTypeId type = c_ir_vendor_unsigned_type(builder, 64);
            IrValueId left = c_ir_vendor_cast(builder, arguments[0], type, source);
            IrValueId right = c_ir_vendor_cast(builder, arguments[1], type, source);
            IrValueId bits = c_ir_vendor_binary(builder, left, right, type, IR_BINARY_INTEGER_BITWISE_OR, source);
            IrValueId zero = c_ir_vendor_constant(builder, 0, type, source);
            IrValueId empty = c_ir_vendor_binary(builder, bits, zero, builder->bool_type, IR_BINARY_INTEGER_EQUAL, source);
            result = c_ir_vendor_cast(builder, empty, builder->s32_type, source);
            break;
        }
        case C_IR_VENDOR_SHUFFLE_BYTE:
            result = c_ir_vendor_shuffle_bytes(builder, arguments, source);
            break;
        case C_IR_VENDOR_SHIFT_BYTES_LEFT:
        case C_IR_VENDOR_SHIFT_BYTES_RIGHT:
            result = c_ir_vendor_shift_bytes(builder, arguments[0], rule->operation == C_IR_VENDOR_SHIFT_BYTES_LEFT, immediate, source);
            break;
        case C_IR_VENDOR_SHUFFLE_FLOAT:
        {
            // SHUFPS moves lane representations, including signaling NaNs.
            // Select integer views, then restore the original float-vector type.
            IrTypeId type = builder->function->values[arguments[0].value].canonical_type;
            IrTypeId element = c_ir_vendor_unsigned_type(builder, 32);
            IrValueId views[] = {c_ir_vendor_reinterpret(builder, arguments[0], element, 4, source),
                                 c_ir_vendor_reinterpret(builder, arguments[1], element, 4, source)};
            result = c_ir_vendor_fixed_shuffle(builder, views, C_IR_VENDOR_SHUFFLE_FLOAT, immediate, source);
            if (result.value < builder->function->value_count)
            {
                result = c_ir_emit_representation_alias_conversion(builder, result, type, source);
            }
            break;
        }
        case C_IR_VENDOR_SHUFFLE_DWORD:
        case C_IR_VENDOR_SHUFFLE_HALFWORD_HIGH:
        case C_IR_VENDOR_SHUFFLE_HALFWORD_LOW:
        case C_IR_VENDOR_BLEND_WORD:
        case C_IR_VENDOR_ALIGN_BYTE:
        case C_IR_VENDOR_INSERT_128:
            result = c_ir_vendor_fixed_shuffle(builder, arguments, (CIrVendorOperation)rule->operation, immediate, source);
            break;
        case C_IR_VENDOR_MULTIPLY_UNSIGNED_DWORD:
            result = c_ir_vendor_multiply_unsigned_dwords(builder, arguments, source);
            break;
        case C_IR_VENDOR_NONE:
            break;
        }
    }
    else if (builder->target.cpu_arch == CPU_ARCH_X86_64 && c_ir_vendor_sha_name(name))
    {
        result = c_ir_emit_vendor_sha(builder, name, arguments, count, token);
    }
    else if (builder->target.cpu_arch == CPU_ARCH_X86_64 && string_equal(name, S8("__builtin_ia32_xgetbv")))
    {
        result = c_ir_emit_vendor_x86_query(builder, name, arguments, count, token);
    }
    else if (builder->target.cpu_arch == CPU_ARCH_X86_64 && builder->target.os == OPERATING_SYSTEM_WINDOWS &&
             string_equal(name, S8("__cpuidex")))
    {
        result = c_ir_emit_vendor_cpuidex(builder, name, arguments, count, token);
    }
    else if (builder->target.cpu_arch == CPU_ARCH_X86_64 && builder->target.os == OPERATING_SYSTEM_WINDOWS &&
             c_vendor_builtin_microsoft_operation(name) >= C_VENDOR_BUILTIN_MICROSOFT_MOVSB &&
             c_vendor_builtin_microsoft_operation(name) <= C_VENDOR_BUILTIN_MICROSOFT_STOSQ)
    {
        result = c_ir_emit_vendor_microsoft_memory(builder, name, arguments, count, token);
    }
    else if (builder->target.cpu_arch == CPU_ARCH_X86_64 && builder->target.os == OPERATING_SYSTEM_WINDOWS &&
             arguments && count == 1 && arguments[0].value < builder->function->value_count &&
             (c_vendor_builtin_microsoft_operation(name) == C_VENDOR_BUILTIN_MICROSOFT_POPCNT ||
              c_vendor_builtin_microsoft_operation(name) == C_VENDOR_BUILTIN_MICROSOFT_POPCNT64))
    {
        u32 width = c_vendor_builtin_microsoft_operation(name) == C_VENDOR_BUILTIN_MICROSOFT_POPCNT ? 32 : 64;
        // The prepared operand already has the signature conversion. Reuse the
        // GNU count expansion, including its software path without POPCNT.
        result = c_ir_emit_population_count(builder, arguments[0], c_ir_vendor_unsigned_type(builder, width), token, source);
    }
    if (result.value == IR_ID_UNDERLYING_INVALID && !builder->failure_message.length)
    {
        builder->failure_message = string_format(builder->arena, S8("vendor builtin '{S8}' has no canonical implementation for these operands"), name);
    }
    return result;
}
