// The seven legacy x86 SHA builtins have fixed, bit-exact semantics even on
// targets without SHA instructions. Expand them into canonical u32 operations
// and the shared vector lane helpers; no target-specific IR reaches a backend.
// Lane zero is bits 31:0. SHA-1 packs its message/state words high-to-low;
// SHA-256 message words are low-to-high, with its state packed as CDGH/ABEF.
// Contracts: LLVM 21.1.8 BuiltinsX86.td and SemaX86.cpp; Intel SDM Volume 2B,
// SHA1MSG1 through SHA256MSG2. This implementation is independently written.

typedef struct CIrVendorShaBuilder CIrVendorShaBuilder;
struct CIrVendorShaBuilder
{
    CIntegerIrBuilder* builder;
    IrTypeId type;
    IrSourceRange source;
};

typedef enum CIrVendorShaOperation
{
    C_IR_VENDOR_SHA_NONE,
    C_IR_VENDOR_SHA1_MESSAGE_FIRST,
    C_IR_VENDOR_SHA1_MESSAGE_LAST,
    C_IR_VENDOR_SHA1_NEXT_E,
    C_IR_VENDOR_SHA1_ROUNDS,
    C_IR_VENDOR_SHA256_MESSAGE_FIRST,
    C_IR_VENDOR_SHA256_MESSAGE_LAST,
    C_IR_VENDOR_SHA256_ROUNDS,
} CIrVendorShaOperation;

BUSTER_C_INTERNAL IrValueId c_ir_vendor_sha_constant(CIrVendorShaBuilder* sha, u32 value)
{
    IrValueId result = c_ir_emit_integer_value_at(sha->builder, value, false, sha->source, sha->type);
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_sha_binary(CIrVendorShaBuilder* sha, IrValueId left, IrValueId right,
                                                  IrBinaryOperation operation)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    if (left.value < sha->builder->function->value_count && right.value < sha->builder->function->value_count)
    {
        result = c_ir_emit_binary_value(sha->builder, left, right, sha->type, operation, sha->source);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_sha_shift(CIrVendorShaBuilder* sha, IrValueId value, u32 amount, bool left)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    if (amount < 32)
    {
        IrValueId count = c_ir_vendor_sha_constant(sha, amount);
        result = c_ir_vendor_sha_binary(sha, value, count, left ? IR_BINARY_SHIFT_LEFT : IR_BINARY_UNSIGNED_SHIFT_RIGHT);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_sha_rotate(CIrVendorShaBuilder* sha, IrValueId value, u32 amount, bool left)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    if (amount && amount < 32)
    {
        IrValueId first = c_ir_vendor_sha_shift(sha, value, amount, left);
        IrValueId second = c_ir_vendor_sha_shift(sha, value, 32 - amount, !left);
        result = c_ir_vendor_sha_binary(sha, first, second, IR_BINARY_INTEGER_BITWISE_OR);
    }
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_sha_xor_three(CIrVendorShaBuilder* sha, IrValueId first, IrValueId second, IrValueId third)
{
    IrValueId pair = c_ir_vendor_sha_binary(sha, first, second, IR_BINARY_INTEGER_BITWISE_XOR);
    IrValueId result = c_ir_vendor_sha_binary(sha, pair, third, IR_BINARY_INTEGER_BITWISE_XOR);
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_sha_choice(CIrVendorShaBuilder* sha, IrValueId first, IrValueId second, IrValueId third)
{
    IrValueId positive = c_ir_vendor_sha_binary(sha, first, second, IR_BINARY_INTEGER_BITWISE_AND);
    IrValueId mask = c_ir_vendor_sha_constant(sha, UINT32_MAX);
    IrValueId inverse = c_ir_vendor_sha_binary(sha, first, mask, IR_BINARY_INTEGER_BITWISE_XOR);
    IrValueId negative = c_ir_vendor_sha_binary(sha, inverse, third, IR_BINARY_INTEGER_BITWISE_AND);
    IrValueId result = c_ir_vendor_sha_binary(sha, positive, negative, IR_BINARY_INTEGER_BITWISE_XOR);
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_sha_majority(CIrVendorShaBuilder* sha, IrValueId first, IrValueId second, IrValueId third)
{
    IrValueId first_second = c_ir_vendor_sha_binary(sha, first, second, IR_BINARY_INTEGER_BITWISE_AND);
    IrValueId first_third = c_ir_vendor_sha_binary(sha, first, third, IR_BINARY_INTEGER_BITWISE_AND);
    IrValueId second_third = c_ir_vendor_sha_binary(sha, second, third, IR_BINARY_INTEGER_BITWISE_AND);
    IrValueId result = c_ir_vendor_sha_xor_three(sha, first_second, first_third, second_third);
    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_vendor_sha_sigma(CIrVendorShaBuilder* sha, IrValueId value, u32 first_count, u32 second_count,
                                                 u32 third_count, bool third_shift)
{
    IrValueId first = c_ir_vendor_sha_rotate(sha, value, first_count, false);
    IrValueId second = c_ir_vendor_sha_rotate(sha, value, second_count, false);
    IrValueId third = third_shift ? c_ir_vendor_sha_shift(sha, value, third_count, false)
                                 : c_ir_vendor_sha_rotate(sha, value, third_count, false);
    IrValueId result = c_ir_vendor_sha_xor_three(sha, first, second, third);
    return result;
}

BUSTER_C_INTERNAL bool c_ir_vendor_sha1_rounds(CIrVendorShaBuilder* sha, IrValueId const* first, IrValueId const* second,
                                               u32 selector, IrValueId* output)
{
    static u32 const constants[] = {UINT32_C(0x5a827999), UINT32_C(0x6ed9eba1), UINT32_C(0x8f1bbcdc), UINT32_C(0xca62c1d6)};
    bool valid = selector < 4;
    if (valid)
    {
        IrValueId a = first[3];
        IrValueId b = first[2];
        IrValueId c = first[1];
        IrValueId d = first[0];
        IrValueId e = IR_VALUE_ID_INVALID;
        IrValueId constant = c_ir_vendor_sha_constant(sha, constants[selector]);
        for (u32 round = 0; round < 4; round += 1)
        {
            IrValueId logic = selector == 0 ? c_ir_vendor_sha_choice(sha, b, c, d)
                                : selector == 2 ? c_ir_vendor_sha_majority(sha, b, c, d)
                                                : c_ir_vendor_sha_xor_three(sha, b, c, d);
            IrValueId rotated = c_ir_vendor_sha_rotate(sha, a, 5, true);
            IrValueId next = c_ir_vendor_sha_binary(sha, logic, rotated, IR_BINARY_INTEGER_ADD);
            next = c_ir_vendor_sha_binary(sha, next, second[3 - round], IR_BINARY_INTEGER_ADD);
            next = c_ir_vendor_sha_binary(sha, next, constant, IR_BINARY_INTEGER_ADD);
            // The first source message word already includes E. Later rounds
            // consume the preceding D directly, including round one.
            if (round)
            {
                next = c_ir_vendor_sha_binary(sha, next, e, IR_BINARY_INTEGER_ADD);
            }
            e = d;
            d = c;
            c = c_ir_vendor_sha_rotate(sha, b, 30, true);
            b = a;
            a = next;
        }
        output[0] = d;
        output[1] = c;
        output[2] = b;
        output[3] = a;
        valid = a.value != IR_ID_UNDERLYING_INVALID && b.value != IR_ID_UNDERLYING_INVALID &&
                c.value != IR_ID_UNDERLYING_INVALID && d.value != IR_ID_UNDERLYING_INVALID;
    }
    return valid;
}

BUSTER_C_INTERNAL bool c_ir_vendor_sha256_rounds(CIrVendorShaBuilder* sha, IrValueId const* first, IrValueId const* second,
                                                 IrValueId const* message, IrValueId* output)
{
    IrValueId a = second[3];
    IrValueId b = second[2];
    IrValueId c = first[3];
    IrValueId d = first[2];
    IrValueId e = second[1];
    IrValueId f = second[0];
    IrValueId g = first[1];
    IrValueId h = first[0];
    for (u32 round = 0; round < 2; round += 1)
    {
        IrValueId choice = c_ir_vendor_sha_choice(sha, e, f, g);
        IrValueId sigma_e = c_ir_vendor_sha_sigma(sha, e, 6, 11, 25, false);
        IrValueId sigma_a = c_ir_vendor_sha_sigma(sha, a, 2, 13, 22, false);
        IrValueId majority = c_ir_vendor_sha_majority(sha, a, b, c);
        IrValueId sum = c_ir_vendor_sha_binary(sha, choice, sigma_e, IR_BINARY_INTEGER_ADD);
        sum = c_ir_vendor_sha_binary(sha, sum, message[round], IR_BINARY_INTEGER_ADD);
        sum = c_ir_vendor_sha_binary(sha, sum, h, IR_BINARY_INTEGER_ADD);
        IrValueId next_a = c_ir_vendor_sha_binary(sha, sum, sigma_a, IR_BINARY_INTEGER_ADD);
        next_a = c_ir_vendor_sha_binary(sha, next_a, majority, IR_BINARY_INTEGER_ADD);
        IrValueId next_e = c_ir_vendor_sha_binary(sha, sum, d, IR_BINARY_INTEGER_ADD);
        h = g;
        g = f;
        f = e;
        e = next_e;
        d = c;
        c = b;
        b = a;
        a = next_a;
    }
    output[0] = f;
    output[1] = e;
    output[2] = b;
    output[3] = a;
    bool valid = a.value != IR_ID_UNDERLYING_INVALID && b.value != IR_ID_UNDERLYING_INVALID &&
                 e.value != IR_ID_UNDERLYING_INVALID && f.value != IR_ID_UNDERLYING_INVALID;
    return valid;
}

BUSTER_C_INTERNAL IrValueId c_ir_emit_vendor_sha(CIntegerIrBuilder* builder, String8 name, IrValueId const* args, u32 count,
                                                CToken token)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    CIrVendorShaOperation operation = C_IR_VENDOR_SHA_NONE;
    if (string_equal(name, S8("__builtin_ia32_sha1msg1")))
        operation = C_IR_VENDOR_SHA1_MESSAGE_FIRST;
    else if (string_equal(name, S8("__builtin_ia32_sha1msg2")))
        operation = C_IR_VENDOR_SHA1_MESSAGE_LAST;
    else if (string_equal(name, S8("__builtin_ia32_sha1nexte")))
        operation = C_IR_VENDOR_SHA1_NEXT_E;
    else if (string_equal(name, S8("__builtin_ia32_sha1rnds4")))
        operation = C_IR_VENDOR_SHA1_ROUNDS;
    else if (string_equal(name, S8("__builtin_ia32_sha256msg1")))
        operation = C_IR_VENDOR_SHA256_MESSAGE_FIRST;
    else if (string_equal(name, S8("__builtin_ia32_sha256msg2")))
        operation = C_IR_VENDOR_SHA256_MESSAGE_LAST;
    else if (string_equal(name, S8("__builtin_ia32_sha256rnds2")))
        operation = C_IR_VENDOR_SHA256_ROUNDS;

    u32 expected_count = operation == C_IR_VENDOR_SHA1_ROUNDS || operation == C_IR_VENDOR_SHA256_ROUNDS ? 3 : 2;
    bool valid = operation != C_IR_VENDOR_SHA_NONE && args && count == expected_count;
    u32 vector_count = operation == C_IR_VENDOR_SHA256_ROUNDS ? 3 : 2;
    IrTypeId result_type = IR_TYPE_ID_INVALID;
    IrTypeId result_element = IR_TYPE_ID_INVALID;
    for (u32 argument = 0; valid && argument < vector_count; argument += 1)
    {
        IrType* vector = args[argument].value < builder->function->value_count
                             ? ir_type_from_id(&builder->program->types, builder->function->values[args[argument].value].canonical_type)
                             : 0;
        IrType* element = vector ? ir_type_from_id(&builder->program->types, vector->element_type) : 0;
        valid = vector && vector->kind == IR_TYPE_VECTOR && vector->element_count == 4 && vector->layout.resolved &&
                vector->layout.size == 16 && element && element->kind == IR_TYPE_INTEGER && element->bit_width == 32 &&
                element->is_signed;
        if (valid && !argument)
        {
            result_type = builder->function->values[args[argument].value].canonical_type;
            result_element = vector->element_type;
        }
    }
    u64 selector = 0;
    if (valid && operation == C_IR_VENDOR_SHA1_ROUNDS)
    {
        // Source ICE checking happens before call preparation. The emitted
        // value check also rejects nonconstant or out-of-range direct callers.
        valid = c_ir_value_integer_constant_evaluate(builder, args[2], &selector) && selector <= 3;
    }
    CIrVendorShaBuilder sha = {.builder = builder, .type = IR_TYPE_ID_INVALID, .source = c_ir_token_source_range(builder, token)};
    IrValueId lanes[3][4] = {0};
    IrValueId output[4] = {IR_VALUE_ID_INVALID, IR_VALUE_ID_INVALID, IR_VALUE_ID_INVALID, IR_VALUE_ID_INVALID};
    if (valid)
    {
        sha.type = c_ir_vendor_unsigned_type(builder, 32);
        valid = sha.type.value != IR_ID_UNDERLYING_INVALID;
    }
    for (u32 argument = 0; valid && argument < vector_count; argument += 1)
    {
        IrValueId vector = c_ir_vendor_reinterpret(builder, args[argument], sha.type, 4, sha.source);
        valid = vector.value != IR_ID_UNDERLYING_INVALID;
        for (u32 lane = 0; valid && lane < 4; lane += 1)
        {
            lanes[argument][lane] = c_ir_vendor_extract(builder, vector, lane, sha.source);
            valid = lanes[argument][lane].value != IR_ID_UNDERLYING_INVALID;
        }
    }
    if (valid && operation == C_IR_VENDOR_SHA1_MESSAGE_FIRST)
    {
        output[0] = c_ir_vendor_sha_binary(&sha, lanes[0][0], lanes[1][2], IR_BINARY_INTEGER_BITWISE_XOR);
        output[1] = c_ir_vendor_sha_binary(&sha, lanes[0][1], lanes[1][3], IR_BINARY_INTEGER_BITWISE_XOR);
        output[2] = c_ir_vendor_sha_binary(&sha, lanes[0][2], lanes[0][0], IR_BINARY_INTEGER_BITWISE_XOR);
        output[3] = c_ir_vendor_sha_binary(&sha, lanes[0][3], lanes[0][1], IR_BINARY_INTEGER_BITWISE_XOR);
    }
    else if (valid && operation == C_IR_VENDOR_SHA1_MESSAGE_LAST)
    {
        for (u32 word = 0; word < 4; word += 1)
        {
            IrValueId previous = word == 3 ? output[3] : lanes[1][2 - word];
            IrValueId combined = c_ir_vendor_sha_binary(&sha, lanes[0][3 - word], previous, IR_BINARY_INTEGER_BITWISE_XOR);
            output[3 - word] = c_ir_vendor_sha_rotate(&sha, combined, 1, true);
        }
    }
    else if (valid && operation == C_IR_VENDOR_SHA1_NEXT_E)
    {
        output[0] = lanes[1][0];
        output[1] = lanes[1][1];
        output[2] = lanes[1][2];
        IrValueId next = c_ir_vendor_sha_rotate(&sha, lanes[0][3], 30, true);
        output[3] = c_ir_vendor_sha_binary(&sha, lanes[1][3], next, IR_BINARY_INTEGER_ADD);
    }
    else if (valid && operation == C_IR_VENDOR_SHA1_ROUNDS)
    {
        valid = c_ir_vendor_sha1_rounds(&sha, lanes[0], lanes[1], (u32)selector, output);
    }
    else if (valid && operation == C_IR_VENDOR_SHA256_MESSAGE_FIRST)
    {
        for (u32 lane = 0; lane < 4; lane += 1)
        {
            IrValueId next = lane == 3 ? lanes[1][0] : lanes[0][lane + 1];
            IrValueId sigma = c_ir_vendor_sha_sigma(&sha, next, 7, 18, 3, true);
            output[lane] = c_ir_vendor_sha_binary(&sha, lanes[0][lane], sigma, IR_BINARY_INTEGER_ADD);
        }
    }
    else if (valid && operation == C_IR_VENDOR_SHA256_MESSAGE_LAST)
    {
        for (u32 lane = 0; lane < 4; lane += 1)
        {
            IrValueId previous = lane < 2 ? lanes[1][lane + 2] : output[lane - 2];
            IrValueId sigma = c_ir_vendor_sha_sigma(&sha, previous, 17, 19, 10, true);
            output[lane] = c_ir_vendor_sha_binary(&sha, lanes[0][lane], sigma, IR_BINARY_INTEGER_ADD);
        }
    }
    else if (valid && operation == C_IR_VENDOR_SHA256_ROUNDS)
    {
        valid = c_ir_vendor_sha256_rounds(&sha, lanes[0], lanes[1], lanes[2], output);
    }
    for (u32 lane = 0; valid && lane < 4; lane += 1)
    {
        valid = output[lane].value != IR_ID_UNDERLYING_INVALID;
        if (valid)
        {
            output[lane] = c_ir_emit_cast(builder, output[lane], result_element, sha.source);
            valid = output[lane].value != IR_ID_UNDERLYING_INVALID;
        }
    }
    if (valid)
    {
        result = c_ir_vendor_construct(builder, result_type, output, 4, sha.source);
    }
    if (result.value == IR_ID_UNDERLYING_INVALID)
    {
        builder->failure_message = string_format(builder->arena, S8("unsupported or invalid SHA builtin '{S8}'"), name);
    }
    return result;
}
