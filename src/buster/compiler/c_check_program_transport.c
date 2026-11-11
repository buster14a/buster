// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Computing with transport vectors (#137): the vectors Tier 0 has no VECTOR width for (check_lower_transport_type), a
// vector of bool, one of 1 or 2 bytes, or one of more than 64 bytes, whose value is its object's address. An operation
// writes its result into a frame object of the result's type (check_lower_transport_object), whose address is its
// value, as clang -O0's code computes it:
//   - a vector of 1 or 2 bytes, of one or two elements of 1 or 2 bytes, an element at a time, each in its promoted type
//     (int, or float for a half-precision element) as C's operators compute it, and converted back, as LLVM's
//     element-wise operations give it (check_lower_transport_elements); clang's code widens such a vector into a
//     register, whose other lanes no result reads;
//   - a vector of more than 64 bytes, a piece at a time, each the width of the target's widest vector registers, by the
//     ordinary vectors' rows at that width (check_vector_lower_element_row), as LLVM's legalization splits it: what
//     those rows compute, and refuse by name, is the ordinary vectors' (check_lower_transport_pieces);
//   - a vector of bool, its bits as clang's <N x i1> keeps them in memory: a bit per element from bit 0 of byte 0, and
//     the bits past the last element zero, as clang's stores of its masks leave them. Its bitwise operators and its
//     compares (false below true, as i1's unsigned compares order them) are bitwise operations on its bytes: in an
//     integer of its bytes where it has at most 8, or as unsigned chars' vector rows a piece at a time where it has more
//     (check_lower_transport_bools).
// A scalar operand is splat, converted to the element's type first, as an ordinary vector's is. Every entry is pushed
// last first, as the tasks run. Included by c_check_program_lower.c after check_lower_vector_operation.

// A frame object for a transport vector's result, of its type's bytes and alignment (at most 64 bytes'), whose
// address is its value.
static uint32_t check_lower_transport_object(CheckLowerWriter* writer, TypeId type)
{
    const TypeTable* types = writer->program->types;
    const TypeId base = type_conversion_operand(types, type);
    const uint32_t alignment = type_alignment_log2(types, base);
    return check_lower_object(writer, type_size(types, base), alignment < 6 ? alignment : 6);
}

// A frame object that LOCAL and SET_LOCAL of VECTOR read and write `bytes` of: a scalar operand's splat, which every
// piece of a wide transport vector's operation reads.
static uint32_t check_lower_transport_splat_temporary(CheckLowerWriter* writer, uint32_t bytes)
{
    const uint32_t local = check_lower_object(writer, bytes, bytes >= 64 ? 6 : bytes >= 32 ? 5 : 4);
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->value_classes[local] = BUSTER_TYPED_CLASS_VECTOR;
    }
    return local;
}

// A vector type's element type, after qualifiers and aligned typedefs.
static TypeId check_lower_transport_element(const TypeTable* types, TypeId vector)
{
    return type_unqualified(type_strip_aligned(types, type_element(types, type_conversion_operand(types, vector))));
}

// Whether a type is a vector (of any width).
static bool check_lower_transport_is_vector(const TypeTable* types, TypeId type)
{
    const uint8_t kind = type_kind(types, type_conversion_operand(types, type));
    return kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
}

// The child of `node` that is its operand `operand` (0 or 1).
static uint32_t check_lower_transport_child(const CheckLowerWriter* writer, uint32_t node, uint32_t operand)
{
    const CheckGraph* graph = &writer->program->input->graph;
    return operand == 0 ? graph->operand0[node] : graph->operand1[node];
}

// Whether operand `operand` of `node` is a scalar, which the operation splats.
static bool check_lower_transport_scalar(const CheckLowerWriter* writer, uint32_t node, uint32_t operand)
{
    const CheckProgram* program = writer->program;
    return !check_lower_transport_is_vector(program->types, check_stream_type(program->stream, check_lower_transport_child(writer, node, operand)));
}

// The address of byte `offset` of the object whose address local `local` (U64) holds.
static void check_lower_transport_at(CheckLowerWriter* writer, uint32_t local, uint64_t offset)
{
    if (offset != 0)
    {
        check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, offset);
    }
    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
}

// Operand `operand` of `node`, a vector's, its object's address set into local `local` (U64).
static void check_lower_transport_vector_operand(CheckLowerWriter* writer, uint32_t node, uint32_t operand, uint32_t local)
{
    const CheckProgram* program = writer->program;
    const TypeId from = check_stream_type(program->stream, check_lower_transport_child(writer, node, operand));
    const TypeId converted = program->stream->conversions[node].operands[operand].type;
    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
    check_lower_operand(writer, node, operand, false, check_lower_transport_is_vector(program->types, converted) ? converted : from);
}

// Operand `operand` of `node`, a scalar's, converted to the element's type `element` and then to `computed`, which
// what follows takes, on the stack.
static void check_lower_transport_scalar_operand(CheckLowerWriter* writer, uint32_t node, uint32_t operand, TypeId element, TypeId computed)
{
    const TypeId from = check_stream_type(writer->program->stream, check_lower_transport_child(writer, node, operand));
    check_lower_convert(writer, element, computed);
    check_lower_convert(writer, from, element);
    check_lower_operand(writer, node, operand, false, from);
}

// The type an element of a vector of 1 or 2 bytes computes in, an element at a time: int for an integer element (each
// is narrower), float for a half-precision one, as C promotes and clang's excess precision computes them; 0 for an
// element neither takes.
static TypeId check_lower_transport_promoted(const CheckLowerWriter* writer, TypeId element)
{
    const TypeTable* types = writer->program->types;
    const uint8_t class = check_lower_class(writer, element);
    return check_lower_class_half(class) != CHECK_HALF_NONE ? BUSTER_TYPE_FLOAT :
           check_action_is_integer(types, element) && type_size(types, element) < 4 && element != BUSTER_TYPE_BOOL ? BUSTER_TYPE_INT :
                                                                                                                 0;
}

// An operator on transport vectors of 1 or 2 bytes, an element at a time: each operand's element read and promoted
// (check_lower_transport_promoted), C's operator in the promoted type, and the result converted to the result's element
// and set at its place in the result's object `result`. A compare's result is -1 or 0 in the result's signed elements;
// a half-precision element's negation flips its sign bit, as clang's fneg does, and its other operators are its
// arithmetic and compares, in float (one operator's float result rounds to it as the element's own operation does,
// check_lower_vector_half_chain's argument). The operands are set into temporaries first, a vector's address or a
// scalar's promoted value.
static void check_lower_transport_elements(CheckLowerWriter* writer, uint32_t node, CheckOp op, TypeId vector, TypeId result_type, uint32_t result,
                                           uint32_t left)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_transport_element(types, vector);
    const TypeId result_element = check_lower_transport_element(types, result_type);
    const TypeId promoted = check_lower_transport_promoted(writer, element);
    const uint32_t size = (uint32_t)type_size(types, element);
    const uint32_t count = size != 0 ? (uint32_t)type_size(types, type_conversion_operand(types, vector)) / size : 0;
    const bool unary = op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
    const bool compare = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
    const bool half = check_lower_class_half(check_lower_class(writer, element)) != CHECK_HALF_NONE;
    const uint8_t kind = unary ? (op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT) : check_lower_binary(op);
    if (promoted == 0 || count == 0)
    {
        check_lower_unsupported(writer, "vector operations on elements of this type");
    }
    else if (half && (op == CHECK_OP_BIT_NOT || (op >= CHECK_OP_REMAINDER && op <= CHECK_OP_SHIFT_RIGHT)))
    {
        // What clang refuses of floating elements (Sema's invalid operands), which no checked program has.
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const uint8_t promoted_class = check_lower_class(writer, promoted);
        const uint8_t element_class = check_lower_class(writer, element);
        const uint8_t result_class = check_lower_class(writer, result_element);
        const uint32_t operands = unary ? 1 : 2;
        bool scalars[2] = { false, false };
        uint32_t locals[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        for (uint32_t j = 0; j < operands; j += 1)
        {
            scalars[j] = !(j == 0 && left != BUSTER_CHECK_NONE) && check_lower_transport_scalar(writer, node, j);
            locals[j] = j == 0 && left != BUSTER_CHECK_NONE ? left : check_lower_temporary(writer, scalars[j] ? promoted_class : BUSTER_TYPED_CLASS_U64);
        }
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        for (uint32_t i = count; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const uint64_t offset = (uint64_t)i * size;
            if (half && op == CHECK_OP_NEGATIVE)
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U16, 0);
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U16, 0);
                check_lower_instruction(writer, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U32, 0);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 0x8000);
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U16, 0);
                check_lower_transport_at(writer, locals[0], offset);
            }
            else
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET, result_class, 0);
                if (compare)
                {
                    check_lower_convert(writer, BUSTER_TYPE_INT, result_element);
                    check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I32, 0);
                    check_lower_instruction(writer, kind, BUSTER_TYPED_CLASS_I32, 0);
                }
                else
                {
                    check_lower_convert(writer, promoted, result_element);
                    check_lower_instruction(writer, kind, promoted_class, 0);
                }
                for (uint32_t j = operands; j-- > 0;)
                {
                    if (scalars[j])
                    {
                        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, promoted_class, locals[j]);
                    }
                    else
                    {
                        check_lower_convert(writer, element, promoted);
                        check_lower_instruction(writer, BUSTER_TYPED_LOAD, element_class, 0);
                        check_lower_transport_at(writer, locals[j], offset);
                    }
                }
            }
            check_lower_element_address(writer, result, (uint32_t)offset);
        }
        for (uint32_t j = operands; j-- > 0;)
        {
            if (j == 0 && left != BUSTER_CHECK_NONE)
            {
            }
            else if (scalars[j])
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, promoted_class, locals[j]);
                check_lower_transport_scalar_operand(writer, node, j, element, promoted);
            }
            else
            {
                check_lower_transport_vector_operand(writer, node, j, locals[j]);
            }
        }
    }
}

// The width of a piece of a wide transport vector's operation: the target's widest vector registers' (64 bytes with
// AVX-512F and AVX-512VL, which the rows' EVEX forms take, 32 with AVX, else 16), as LLVM legalizes the operation.
static uint32_t check_lower_transport_width(const CheckLowerWriter* writer)
{
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    return (writer->target.features & evex) == evex ? 64 : (writer->target.features & BUSTER_X86_FEATURE(AVX)) != 0 ? 32 : 16;
}

// A row of `op` on `width` bytes of `element`s, its index among the function's operations, or BUSTER_CHECK_NONE where
// it is refused (by its reason's name).
static uint32_t check_lower_transport_row(CheckLowerWriter* writer, uint32_t op, CheckVectorElement element, uint32_t width, uint8_t scalar_class,
                                          Tier0Operation* row)
{
    const bool excess = (writer->program->target.extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0;
    const char* reason = check_vector_lower_element_row(&writer->target, excess, op, element, width, scalar_class, row);
    uint32_t index = BUSTER_CHECK_NONE;
    if (reason != NULL)
    {
        check_lower_unsupported(writer, reason);
    }
    else
    {
        index = check_lower_operation_index(writer, row);
        writer->vectors = true;
    }
    return index;
}

// A wide bf16 vector uses the ordinary bf16 rows on each piece. Under excess precision the pieces have at most 32
// bytes, so their widened floats fit one register and the row never needs the ordinary 64-byte vector's join.
static uint32_t check_lower_transport_brain_row(CheckLowerWriter* writer, uint32_t op, uint32_t width)
{
    Tier0Operation rows[3];
    uint32_t count = 0;
    const bool excess = (writer->program->target.extensions & BUSTER_X86_EXTENSION(AVX10_2)) == 0;
    const char* reason = check_vector_lower_brain_rows(&writer->target, excess, op, width, 0, rows, &count);
    uint32_t index = BUSTER_CHECK_NONE;
    if (reason != NULL)
    {
        check_lower_unsupported(writer, reason);
    }
    else if (count != 1)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        index = check_lower_operation_index(writer, rows);
        writer->vectors = true;
    }
    return index;
}

// A scalar's splat into a `width` bytes' vector of `element` (of type `element_type`), the scalar on the stack
// converted to the element's type already, as check_lower_vector_splat makes it: an element of 1 or 2 bytes from an
// int's of its signedness, a half-precision one from its bits zero-extended; then set into the splat's temporary
// `local`.
static void check_lower_transport_splat(CheckLowerWriter* writer, CheckVectorElement element, TypeId element_type, uint32_t width, uint32_t local)
{
    const uint8_t element_class = check_lower_storage_class(writer, element_type);
    const bool half = check_lower_half_of(writer, element_type) != CHECK_HALF_NONE;
    const bool narrow = !half && element_class >= BUSTER_TYPED_CLASS_I8 && element_class <= BUSTER_TYPED_CLASS_U16;
    const TypeId wide = element_class == BUSTER_TYPED_CLASS_U8 || element_class == BUSTER_TYPED_CLASS_U16 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT;
    Tier0Operation row;
    const uint32_t index = check_lower_transport_row(writer, BUSTER_CHECK_VECTOR_SPLAT, element, width, half ? BUSTER_TYPED_CLASS_U32 : narrow ? check_lower_class(writer, wide) : element_class,
                                                     &row);
    if (index != BUSTER_CHECK_NONE)
    {
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, local);
        check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, index);
        if (half)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        }
        else if (narrow)
        {
            check_lower_convert(writer, element_type, wide);
        }
    }
}

// The steps of a vector of bool's operator, on its bytes: its operands' complements first (`not_a`, `not_b`), the
// bitwise operation (`kind`: AND, OR or XOR, or NONE for a unary one), and the result's complement (`not_result`).
typedef struct CheckLowerTransportBits CheckLowerTransportBits;
struct CheckLowerTransportBits
{
    uint32_t op;
    bool not_a;
    bool not_b;
    bool not_result;
};

static bool check_lower_transport_bits(CheckOp op, CheckLowerTransportBits* bits)
{
    bool valid = true;
    *bits = (CheckLowerTransportBits){ .op = CHECK_OP_BIT_AND };
    switch (op)
    {
        case CHECK_OP_BIT_AND: case CHECK_OP_BIT_AND_ASSIGN: break;
        case CHECK_OP_BIT_OR: case CHECK_OP_BIT_OR_ASSIGN: bits->op = CHECK_OP_BIT_OR; break;
        case CHECK_OP_BIT_XOR: case CHECK_OP_BIT_XOR_ASSIGN: case CHECK_OP_NOT_EQUAL: bits->op = CHECK_OP_BIT_XOR; break;
        case CHECK_OP_EQUAL: bits->op = CHECK_OP_BIT_XOR; bits->not_result = true; break;
        case CHECK_OP_LESS: bits->not_a = true; break;
        case CHECK_OP_LESS_EQUAL: bits->op = CHECK_OP_BIT_OR; bits->not_a = true; break;
        case CHECK_OP_GREATER: bits->not_b = true; break;
        case CHECK_OP_GREATER_EQUAL: bits->op = CHECK_OP_BIT_OR; bits->not_b = true; break;
        case CHECK_OP_BIT_NOT: case CHECK_OP_NOT: bits->op = CHECK_OP_COUNT; bits->not_a = true; break;
        default: valid = false; break;
    }
    return valid;
}

// The pieces of a wide transport vector's operation, or of a vector of bool's of more than 8 bytes, each `width`
// bytes (or the vector's, where it has fewer) of `element`s: each piece's operands loaded at its offset (a scalar's
// splat whole), the rows run and the result set at its offset in the result's object `result`. A vector of bool's
// rows are `bits`' (with `bits` non-NULL), its operator's own (`op`) otherwise, a floating negation's with its sign bit.
static void check_lower_transport_rows(CheckLowerWriter* writer, uint32_t node, uint32_t op, TypeId vector, CheckVectorElement element, uint32_t width,
                                       const CheckLowerTransportBits* bits, uint32_t result, uint32_t left)
{
    const TypeTable* types = writer->program->types;
    const TypeId element_type = check_lower_transport_element(types, vector);
    const uint32_t bytes = (uint32_t)type_size(types, type_conversion_operand(types, vector));
    const uint32_t piece = bytes < width ? bytes : width;
    const bool unary = op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT || op == CHECK_OP_NOT;
    const bool brain = element_type == BUSTER_TYPE_BF16;
    const bool negation = op == CHECK_OP_NEGATIVE && element.floating && !brain;
    const uint32_t operands = unary ? 1 : 2;
    Tier0Operation rows[2];
    uint32_t main_row = BUSTER_CHECK_NONE;
    uint32_t not_row = BUSTER_CHECK_NONE;
    if (bits != NULL)
    {
        if (bits->op != CHECK_OP_COUNT)
        {
            main_row = check_lower_transport_row(writer, bits->op, element, piece, 0, rows);
        }
        if (bits->not_a || bits->not_b || bits->not_result)
        {
            not_row = check_lower_transport_row(writer, CHECK_OP_BIT_NOT, element, piece, 0, rows + 1);
        }
    }
    else
    {
        // `<=` and `>=` of qwords without SSE4.2 are their complements' rows and a complement (#1834).
        const uint32_t complement = brain ? 0 : check_vector_baseline_complement(&writer->target, op, element);
        main_row = brain ? check_lower_transport_brain_row(writer, op, piece) : check_lower_transport_row(writer, complement != 0 ? complement : op, element, piece, 0, rows);
        if (complement != 0)
        {
            not_row = check_lower_transport_row(writer, CHECK_OP_BIT_NOT, element, piece, 0, rows + 1);
        }
    }
    const bool not_result = bits != NULL ? bits->not_result : not_row != BUSTER_CHECK_NONE;
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        bool scalars[2] = { false, false };
        uint32_t locals[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        for (uint32_t j = 0; j < operands; j += 1)
        {
            scalars[j] = !(j == 0 && left != BUSTER_CHECK_NONE) && check_lower_transport_scalar(writer, node, j);
            locals[j] = j == 0 && left != BUSTER_CHECK_NONE ? left :
                        scalars[j] ? check_lower_transport_splat_temporary(writer, piece) : check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        }
        for (uint32_t p = bytes / piece; p-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const uint64_t offset = (uint64_t)p * piece;
            check_lower_instruction(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, piece);
            if (not_result)
            {
                check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, not_row);
            }
            if (main_row != BUSTER_CHECK_NONE)
            {
                check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, main_row);
            }
            if (negation)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, element.bytes == 8 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32,
                                         element.bytes == 2 ? UINT64_C(0x8000) : element.bytes == 4 ? UINT64_C(0x80000000) : UINT64_C(0x8000000000000000));
            }
            for (uint32_t j = operands; j-- > 0;)
            {
                if (bits != NULL && (j == 0 ? bits->not_a : bits->not_b))
                {
                    check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, not_row);
                }
                if (scalars[j])
                {
                    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, locals[j]);
                }
                else
                {
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, piece);
                    check_lower_transport_at(writer, locals[j], offset);
                }
            }
            check_lower_element_address(writer, result, (uint32_t)offset);
        }
        for (uint32_t j = operands; j-- > 0;)
        {
            if (j == 0 && left != BUSTER_CHECK_NONE)
            {
            }
            else if (scalars[j] && bits != NULL)
            {
                // A scalar's truth in every element: its _Bool's 0 or 1 negated, all of a byte's bits or none.
                check_lower_transport_splat(writer, element, BUSTER_TYPE_UCHAR, piece, locals[j]);
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
                check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I32, 0);
                check_lower_transport_scalar_operand(writer, node, j, BUSTER_TYPE_BOOL, BUSTER_TYPE_INT);
            }
            else if (scalars[j])
            {
                check_lower_transport_splat(writer, element, element_type, piece, locals[j]);
                check_lower_transport_scalar_operand(writer, node, j, element_type, element_type);
            }
            else
            {
                check_lower_transport_vector_operand(writer, node, j, locals[j]);
            }
        }
    }
}

// An operator on wide transport vectors, a piece at a time (check_lower_transport_rows): the ordinary vectors' rows at
// the target's widest registers' width, whose refusals are theirs. bf16 elements' arithmetic computes in float where
// the unit's target hasn't AVX10.2, with pieces of at most 32 bytes so their floats fit one register.
static void check_lower_transport_pieces(CheckLowerWriter* writer, uint32_t node, CheckOp op, TypeId vector, uint32_t result, uint32_t left)
{
    const TypeTable* types = writer->program->types;
    const bool brain = check_vector_lower_brain(types, vector);
    const CheckVectorElement element = brain ? (CheckVectorElement){ .bytes = 2, .floating = true, .is_signed = true } : check_vector_lower_element(types, vector);
    if (element.bytes == 0)
    {
        check_lower_unsupported(writer, "vector operations on elements of this type");
    }
    else
    {
        const CheckGraph* graph = &writer->program->input->graph;
        const bool unary = op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
        check_lower_vector_half_chain(writer, vector, op, left != BUSTER_CHECK_NONE ? BUSTER_CHECK_NONE : graph->operand0[node],
                                      unary ? BUSTER_CHECK_NONE : graph->operand1[node]);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        const uint32_t width = check_lower_transport_width(writer);
        const bool excess = brain && (writer->program->target.extensions & BUSTER_X86_EXTENSION(AVX10_2)) == 0;
        check_lower_transport_rows(writer, node, op, vector, element, excess && width > 32 ? 32 : width, NULL, result, left);
    }
}

// The bits of a vector of bool `vector` past its last element, in its object `result`, cleared: the last byte's high
// bits and the bytes past it. Pushed to run after what is pushed before it (that is, pushed first).
static void check_lower_transport_clear(CheckLowerWriter* writer, TypeId vector, uint32_t result)
{
    const TypeTable* types = writer->program->types;
    const uint32_t lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, vector));
    const uint32_t bytes = (uint32_t)type_size(types, type_conversion_operand(types, vector));
    for (uint32_t k = bytes; k-- > lanes / 8;)
    {
        check_lower_instruction(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U8, 0);
        if (k == lanes / 8 && lanes % 8 != 0)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
            check_lower_instruction(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, (UINT32_C(1) << (lanes % 8)) - 1);
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
            check_lower_element_address(writer, result, k);
        }
        else
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U8, 0);
        }
        check_lower_element_address(writer, result, k);
    }
}

// An operator on vectors of bool (check_lower_transport_bits' steps on their bytes): with at most 8 bytes, in an
// integer of 32 bits (or 64, for 8 bytes) loaded from each operand's object, a scalar operand's truth its _Bool's 0 or
// 1 negated, the result masked to its elements' bits and set as an integer of its bytes; with more, a piece at a time
// as vectors of unsigned chars (check_lower_transport_rows), and the bits past the elements cleared after.
static void check_lower_transport_bools(CheckLowerWriter* writer, uint32_t node, CheckOp op, TypeId vector, uint32_t result, uint32_t left)
{
    const TypeTable* types = writer->program->types;
    const uint32_t bytes = (uint32_t)type_size(types, type_conversion_operand(types, vector));
    const uint32_t lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, vector));
    CheckLowerTransportBits bits;
    if (!check_lower_transport_bits(op, &bits))
    {
        check_lower_unsupported(writer, check_lower_transport_refusal(types, vector, CHECK_LOWER_TRANSPORT_ARITHMETIC));
    }
    else if (bytes > 8)
    {
        const CheckVectorElement element = { .bytes = 1 };
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        if (lanes < 8 * bytes)
        {
            check_lower_transport_clear(writer, vector, result);
        }
        check_lower_transport_rows(writer, node, op, vector, element, check_lower_transport_width(writer), &bits, result, left);
    }
    else
    {
        // The integer of the vector's bytes, and the one it computes in.
        const uint8_t stored = bytes == 1 ? BUSTER_TYPED_CLASS_U8 : bytes == 2 ? BUSTER_TYPED_CLASS_U16 : bytes == 4 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
        const uint8_t computed = bytes == 8 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
        const TypeId computed_type = bytes == 8 ? BUSTER_TYPE_ULONG : BUSTER_TYPE_UINT;
        const bool unary = bits.op == CHECK_OP_COUNT;
        const uint32_t operands = unary ? 1 : 2;
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        check_lower_instruction(writer, BUSTER_TYPED_SET, stored, 0);
        if (stored != computed)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, stored, 0);
        }
        if (lanes < 8 * bytes)
        {
            check_lower_instruction(writer, BUSTER_TYPED_AND, computed, 0);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, computed, (UINT64_C(1) << lanes) - 1);
        }
        if (bits.not_result)
        {
            check_lower_instruction(writer, BUSTER_TYPED_NOT, computed, 0);
        }
        if (!unary)
        {
            check_lower_instruction(writer, check_lower_binary((CheckOp)bits.op), computed, 0);
        }
        for (uint32_t j = operands; j-- > 0;)
        {
            if (j == 0 ? bits.not_a : bits.not_b)
            {
                check_lower_instruction(writer, BUSTER_TYPED_NOT, computed, 0);
            }
            if (j == 0 && left != BUSTER_CHECK_NONE)
            {
                if (stored != computed)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computed, 0);
                }
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, left);
            }
            else if (check_lower_transport_scalar(writer, node, j))
            {
                check_lower_instruction(writer, BUSTER_TYPED_NEG, computed, 0);
                check_lower_transport_scalar_operand(writer, node, j, BUSTER_TYPE_BOOL, computed_type);
            }
            else
            {
                const TypeId from = check_stream_type(writer->program->stream, check_lower_transport_child(writer, node, j));
                if (stored != computed)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computed, 0);
                }
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored, 0);
                check_lower_operand(writer, node, j, false, from);
            }
        }
        check_lower_element_address(writer, result, 0);
    }
}

// An operator on transport vectors (#137), whose result is written into a frame object of its type: a vector of bool's
// (check_lower_transport_bools), one of 1 or 2 bytes' an element at a time (check_lower_transport_elements), a wider
// one's a piece at a time (check_lower_transport_pieces). Logical operators and vector conditions use
// check_lower_vector_logical; a vector of bool's ! is its ~ and uses the packed-bit path here too.
static __attribute__((noinline)) void check_lower_transport_operation(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    const CheckOp op = program->input->graph.ops[node];
    const TypeId result_type = check_stream_type(program->stream, node);
    const TypeId converted = program->stream->conversions[node].operands[0].type;
    const TypeId vector = check_lower_transport_is_vector(types, converted) ? converted : result_type;
    const bool logical = op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL;
    if (check_lower_bool_vector(types, vector) && check_lower_bool_vector(types, result_type) && op != CHECK_OP_LOGICAL_AND && op != CHECK_OP_LOGICAL_OR &&
        op != CHECK_OP_CONDITIONAL)
    {
        check_lower_transport_bools(writer, node, op, vector, check_lower_transport_object(writer, result_type), BUSTER_CHECK_NONE);
    }
    else if (logical || check_lower_bool_vector(types, vector) || check_lower_bool_vector(types, result_type))
    {
        check_lower_unsupported(writer, "vector logical operators and conditions");
    }
    else if (type_size(types, type_conversion_operand(types, vector)) <= 2)
    {
        check_lower_transport_elements(writer, node, op, vector, result_type, check_lower_transport_object(writer, result_type), BUSTER_CHECK_NONE);
    }
    else
    {
        check_lower_transport_pieces(writer, node, op, vector, check_lower_transport_object(writer, result_type), BUSTER_CHECK_NONE);
    }
}

// A compound assignment to a transport vector (#137), `a op= b`: the destination's address, evaluated once, is the
// operation's left operand (check_lower_transport_operation's computations with `left`), whose result's bytes are copied
// into the destination; the destination's address is its value. An increment is refused as an ordinary vector's is.
static __attribute__((noinline)) void check_lower_transport_update(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    const CheckOp op = program->input->graph.ops[node];
    const TypeId stored = check_stream_type(program->stream, program->input->graph.operand0[node]);
    const TypeId calculated = program->stream->conversions[node].operands[0].type;
    const TypeId vector = check_lower_transport_is_vector(types, calculated) ? calculated : stored;
    const CheckOp binary = op <= CHECK_OP_SHIFT_RIGHT_ASSIGN ? (CheckOp)(CHECK_OP_ADD + (op - CHECK_OP_ADD_ASSIGN)) : (CheckOp)(CHECK_OP_BIT_AND + (op - CHECK_OP_BIT_AND_ASSIGN));
    if (op <= CHECK_OP_POST_DECREMENT)
    {
        check_lower_unsupported(writer, "increments of vectors");
    }
    else if (type_conversion_operand(types, vector) != type_conversion_operand(types, stored) || !check_lower_transport_is_vector(types, stored))
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const uint32_t destination = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t result = check_lower_transport_object(writer, stored);
        // The destination's address, its value, after the copy of the result into it.
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = type_size(types, type_conversion_operand(types, stored)), .address = false });
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        // The computation's own value, the result's address, dropped.
        check_lower_instruction(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        if (check_lower_bool_vector(types, vector))
        {
            check_lower_transport_bools(writer, node, binary, vector, result, destination);
        }
        else if (type_size(types, type_conversion_operand(types, vector)) <= 2)
        {
            check_lower_transport_elements(writer, node, binary, vector, stored, result, destination);
        }
        else
        {
            check_lower_transport_pieces(writer, node, binary, vector, result, destination);
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, destination);
        check_lower_operand(writer, node, 0, true, 0);
    }
}

// The value of element `i` of the vector of type `vector` whose address local `source` (U64) holds, converted to `to`:
// a vector of bool's, its bit (as an unsigned int's 0 or 1, converted), any other's, its element loaded.
static void check_lower_transport_read(CheckLowerWriter* writer, uint32_t source, TypeId vector, uint32_t i, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_transport_element(types, vector);
    if (check_lower_bool_vector(types, vector))
    {
        check_lower_convert(writer, BUSTER_TYPE_UINT, to);
        check_lower_instruction(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 1);
        if (i % 8 != 0)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U32, 0);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, i % 8);
        }
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
        check_lower_transport_at(writer, source, i / 8);
    }
    else
    {
        check_lower_convert(writer, element, to);
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, check_lower_class(writer, element), 0);
        check_lower_transport_at(writer, source, (uint64_t)i * type_size(types, element));
    }
}

// The elements of a vector of type `result` written into its object `object` (a frame object's local): element i is
// element indices[i] of the two sources (each of type `source`, whose addresses locals sources[0] and sources[1]
// hold, the second's elements after the first's), converted to the result's element type, or 0 where indices[i] is
// BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED. A vector of bool's bits are packed a byte at a time, its bytes past its
// elements cleared.
static void check_lower_transport_gather(CheckLowerWriter* writer, const uint32_t sources[2], TypeId source, TypeId result, uint32_t object, const uint32_t* indices)
{
    const TypeTable* types = writer->program->types;
    const uint32_t in_lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, source));
    const uint32_t lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, result));
    const TypeId element = check_lower_transport_element(types, result);
    if (check_lower_bool_vector(types, result))
    {
        const uint32_t bytes = (uint32_t)type_size(types, type_conversion_operand(types, result));
        for (uint32_t k = bytes; k-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const uint32_t count = 8 * k >= lanes ? 0 : lanes - 8 * k < 8 ? lanes - 8 * k : 8;
            check_lower_instruction(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U8, 0);
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
            if (count == 0)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 0);
            }
            for (uint32_t j = count; j-- > 0;)
            {
                const uint32_t index = indices[8 * k + j];
                if (j != 0)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U32, 0);
                    check_lower_instruction(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U32, 0);
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, j);
                }
                if (index == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 0);
                }
                else
                {
                    // Its truth, 0 or 1, as an unsigned int.
                    check_lower_convert(writer, BUSTER_TYPE_BOOL, BUSTER_TYPE_UINT);
                    check_lower_transport_read(writer, sources[index / in_lanes], source, index % in_lanes, BUSTER_TYPE_BOOL);
                }
            }
            check_lower_element_address(writer, object, k);
        }
    }
    else
    {
        const uint32_t size = (uint32_t)type_size(types, element);
        for (uint32_t i = lanes; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const uint32_t index = indices[i];
            check_lower_instruction(writer, BUSTER_TYPED_SET, check_lower_class(writer, element), 0);
            if (index == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED)
            {
                check_lower_convert(writer, BUSTER_TYPE_INT, element);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
            }
            else
            {
                check_lower_transport_read(writer, sources[index / in_lanes], source, index % in_lanes, element);
            }
            check_lower_element_address(writer, object, i * size);
        }
    }
}

// Whether a vector type's value is an X86 row's VECTOR (an ordinary vector's), not its object's address.
static bool check_lower_transport_register(const CheckLowerWriter* writer, TypeId type)
{
    return check_lower_vector_bytes(writer->program->types, type) != 0;
}

// The vector of type `type` on top of the stack, its object's address set into local `local` (U64): a transport
// vector's value already is it, an ordinary vector's is set into a temporary of its bytes first.
static void check_lower_transport_source(CheckLowerWriter* writer, TypeId type, uint32_t local)
{
    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
    if (check_lower_transport_register(writer, type))
    {
        const uint32_t object = check_lower_temporary_of(writer, type);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, object);
    }
}

// A frame object for a result of type `type`, a transport vector's or an ordinary one's, and the result's value
// pushed from it: the transport vector's address, the ordinary vector's register value.
static uint32_t check_lower_transport_result(CheckLowerWriter* writer, TypeId type)
{
    uint32_t object = BUSTER_CHECK_NONE;
    if (check_lower_transport_register(writer, type))
    {
        object = check_lower_temporary_of(writer, type);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, object);
    }
    else
    {
        object = check_lower_transport_object(writer, type);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    }
    return object;
}

// A builtin row's source: ordinary and wide vectors use their objects, but a 1- or 2-byte vector is zero-extended
// into an addressed word before a minimum-width VECTOR load. Neither the load nor the result store touches bytes
// outside the caller's vector. The padding belongs to this lowering alone.
static void check_lower_transport_builtin_source(CheckLowerWriter* writer, TypeId type, uint32_t local)
{
    const uint32_t bytes = (uint32_t)type_size(writer->program->types, type_conversion_operand(writer->program->types, type));
    if (bytes >= 4)
    {
        check_lower_transport_source(writer, type, local);
    }
    else
    {
        const uint32_t object = check_lower_addressed_temporary(writer, BUSTER_TYPED_CLASS_U32);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U32, object);
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, bytes == 1 ? BUSTER_TYPED_CLASS_U8 : BUSTER_TYPED_CLASS_U16, 0);
    }
}

// A minimum-width builtin row's result written at its actual width. The destination address is already below the
// row's operands on the stack, as for SET of an ordinary VECTOR; a small result first goes through a private word.
static void check_lower_transport_builtin_store(CheckLowerWriter* writer, uint32_t bytes)
{
    if (bytes >= 4)
    {
        check_lower_instruction(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, bytes);
    }
    else
    {
        const uint8_t class = bytes == 1 ? BUSTER_TYPED_CLASS_U8 : BUSTER_TYPED_CLASS_U16;
        const uint32_t object = check_lower_transport_splat_temporary(writer, 4);
        check_lower_instruction(writer, BUSTER_TYPED_SET, class, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, object);
    }
}

// Keep a vector operand's value before evaluating the next operand. An ordinary value is already a register and a
// tiny value is read into its padded word; a wider transport value needs its own object because its address alone
// would observe a later operand's writes. This is also the sequence point between a vector condition and its arms.
static void check_lower_vector_logical_source(CheckLowerWriter* writer, TypeId type, uint32_t local)
{
    const uint32_t bytes = (uint32_t)type_size(writer->program->types, type_conversion_operand(writer->program->types, type));
    if (bytes <= 2 || check_lower_transport_register(writer, type))
    {
        check_lower_transport_builtin_source(writer, type, local);
    }
    else
    {
        const uint32_t object = check_lower_transport_object(writer, type);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = bytes });
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
    }
}

// One piece's mask. Logical truth is nonzero, including either NaN, with both signed zeros false. Floating compares
// retain the ordinary rows' exception behavior, including FE_INVALID for a signaling NaN.
// A vector conditional instead tests the sign bit, as clang's signed less-than-zero comparison does even for an
// unsigned condition. All true lanes have every bit set, so the same integer rows blend every arm's representation.
static void check_lower_vector_logical_mask(CheckLowerWriter* writer, uint32_t source, uint32_t offset, uint32_t bytes, uint32_t compare, uint32_t zero)
{
    check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, compare);
    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, zero);
    check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
    check_lower_transport_at(writer, source, offset);
}

// Half-precision truth uses the scalar conversion and comparison rules where the ordinary packed comparison has
// no matching target form (bf16, or binary16 without AVX512-FP16). Converting each loaded lane also preserves the
// comparison's floating-point exceptions; inspecting its representation alone would miss signaling NaNs.
static void check_lower_vector_half_logical(CheckLowerWriter* writer, CheckOp op, TypeId input, TypeId result, const uint32_t sources[3], uint32_t object)
{
    const TypeTable* types = writer->program->types;
    const TypeId element = check_lower_transport_element(types, input);
    const TypeId output = check_lower_transport_element(types, result);
    const uint32_t lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, result));
    const uint32_t operands = op == CHECK_OP_NOT ? 1 : 2;
    for (uint32_t lane = lanes; lane-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
    {
        check_lower_instruction(writer, BUSTER_TYPED_SET, check_lower_class(writer, output), 0);
        check_lower_convert(writer, BUSTER_TYPE_INT, output);
        check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I32, 0);
        if (operands == 2)
        {
            check_lower_instruction(writer, op == CHECK_OP_LOGICAL_AND ? BUSTER_TYPED_AND : BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_I32, 0);
        }
        for (uint32_t a = operands; a-- > 0;)
        {
            check_lower_truth(writer, element, op == CHECK_OP_NOT);
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, check_lower_class(writer, element), 0);
            check_lower_transport_at(writer, sources[a], lane * 2);
        }
        check_lower_element_address(writer, object, lane * 2);
    }
}

// Read an arm's element as unsigned bits, promoted to at least a word. The conditional blends representations,
// preserving signed zero and NaN payloads and avoiding a floating conversion of either selected or unselected arm.
static void check_lower_vector_logical_bits(CheckLowerWriter* writer, uint32_t source, uint32_t offset, uint32_t size)
{
    const uint8_t class = size == 1 ? BUSTER_TYPED_CLASS_U8 : size == 2 ? BUSTER_TYPED_CLASS_U16 :
                          size == 4 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
    if (size < 4)
    {
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
    }
    check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
    check_lower_transport_at(writer, source, offset);
}

// A packed boolean condition's bits select whole elements. Boolean arms stay packed and blend a byte at a time;
// other arms expand only the condition bit, at the same bounded lane count as the existing bool conversions.
static void check_lower_vector_bool_conditional(CheckLowerWriter* writer, TypeId condition, TypeId result, const uint32_t sources[3], uint32_t object)
{
    const TypeTable* types = writer->program->types;
    const bool packed = check_lower_bool_vector(types, result);
    const uint32_t size = packed ? 1 : (uint32_t)type_size(types, check_lower_transport_element(types, result));
    const uint32_t lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, result));
    const uint32_t count = packed ? (lanes + 7) / 8 : lanes;
    const uint8_t class = size <= 4 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
    const uint8_t stored = size == 1 ? BUSTER_TYPED_CLASS_U8 : size == 2 ? BUSTER_TYPED_CLASS_U16 : class;
    if (packed)
    {
        check_lower_transport_clear(writer, result, object);
    }
    for (uint32_t i = count; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
    {
        const uint32_t offset = i * size;
        check_lower_instruction(writer, BUSTER_TYPED_SET, stored, 0);
        if (size < 4)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONVERT, stored, 0);
        }
        check_lower_instruction(writer, BUSTER_TYPED_XOR, class, 0);
        check_lower_vector_logical_bits(writer, sources[2], offset, size);
        check_lower_instruction(writer, BUSTER_TYPED_AND, class, 0);
        if (packed)
        {
            check_lower_vector_logical_bits(writer, sources[0], i, 1);
        }
        else
        {
            check_lower_instruction(writer, BUSTER_TYPED_NEG, class, 0);
            if (class != BUSTER_TYPED_CLASS_U32)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, class, 0);
            }
            check_lower_transport_read(writer, sources[0], condition, i, BUSTER_TYPE_UINT);
        }
        check_lower_instruction(writer, BUSTER_TYPED_XOR, class, 0);
        check_lower_vector_logical_bits(writer, sources[2], offset, size);
        check_lower_vector_logical_bits(writer, sources[1], offset, size);
        check_lower_element_address(writer, object, offset);
    }
}

static void check_lower_transport_splat_value(CheckLowerWriter* writer, TypeId from, TypeId to);

// Extended vectors' !, &&, || and vector-condition ?: share register-width masks for ordinary and transport
// values. The only per-element case is unpacking a boolean condition for non-boolean arms. Scalar-condition ?: is
// still the ordinary short-circuit control-flow path. Boolean && and || are rejected by Sema, as clang rejects them.
static __attribute__((noinline)) void check_lower_vector_logical(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const TypeId result = check_stream_type(stream, node);
    const CheckConversion* conversions = stream->conversions[node].operands;
    const bool conditional = op == CHECK_OP_CONDITIONAL;
    const uint32_t operands = conditional ? 3 : op == CHECK_OP_NOT ? 1 : 2;
    TypeId inputs[3] = { conversions[0].type, conversions[1].type, conversions[2].type };
    const bool packed = check_lower_bool_vector(types, inputs[0]);
    const TypeId element_type = check_lower_transport_element(types, inputs[0]);
    const uint32_t element_bytes = (uint32_t)type_size(types, element_type);
    const uint32_t bytes = (uint32_t)type_size(types, type_conversion_operand(types, result));
    const uint32_t lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, result));
    const bool half = !conditional && check_lower_half(types, element_type) != CHECK_HALF_NONE;
    const bool half_scalar = half && (element_type == BUSTER_TYPE_BF16 || (writer->target.extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0);
    if (op == CHECK_OP_NOT && packed)
    {
        check_lower_transport_bools(writer, node, op, inputs[0], check_lower_transport_object(writer, result), BUSTER_CHECK_NONE);
    }
    else if (element_bytes == 0 || element_bytes > 8 || type_size(types, check_lower_transport_element(types, result)) > 8)
    {
        check_lower_unsupported(writer, "vector logical operators on elements of this size");
    }
    else if (lanes == 0 || lanes > BUSTER_CHECK_LOWER_TRANSPORT_LANES)
    {
        check_lower_unsupported(writer, "logical operations of vectors of more than 1024 elements");
    }
    else
    {
        const bool reuse = conditional && graph->operand0[node] == graph->operand1[node];
        uint32_t sources[3] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        for (uint32_t a = 0; a < operands; a += 1)
        {
            sources[a] = a == 1 && reuse ? sources[0] : check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        }
        const uint32_t object = check_lower_transport_result(writer, result);
        if (conditional && packed)
        {
            check_lower_vector_bool_conditional(writer, inputs[0], result, sources, object);
        }
        else if (half_scalar)
        {
            check_lower_vector_half_logical(writer, op, inputs[0], result, sources, object);
        }
        else
        {
            const uint32_t width = check_lower_transport_width(writer);
            const uint32_t piece_bytes = bytes < 4 ? 4 : bytes < width ? bytes : width;
            const CheckVectorElement element = { .bytes = (uint8_t)element_bytes, .is_signed = true };
            CheckVectorElement compared = element;
            compared.floating = !conditional && check_action_kind(types, element_type) == BUSTER_TYPE_KIND_FLOATING;
            const uint32_t predicate = conditional ? CHECK_OP_LESS : op == CHECK_OP_NOT ? CHECK_OP_EQUAL : CHECK_OP_NOT_EQUAL;
            Tier0Operation row;
            const uint32_t compare = check_lower_transport_row(writer, predicate, compared, piece_bytes, 0, &row);
            const uint32_t bit_and = check_lower_transport_row(writer, CHECK_OP_BIT_AND, element, piece_bytes, 0, &row);
            const uint32_t combine = check_lower_transport_row(writer, conditional ? CHECK_OP_BIT_XOR :
                                                                      op == CHECK_OP_LOGICAL_OR ? CHECK_OP_BIT_OR : CHECK_OP_BIT_AND,
                                                               element, piece_bytes, 0, &row);
            uint64_t words[8] = { 0 };
            const uint32_t zero = check_lower_vector_constant(writer, words, piece_bytes);
            for (uint32_t end = bytes; end != 0 && writer->output->status == CHECK_PROGRAM_OK;)
            {
                const uint32_t part = end < piece_bytes ? end : piece_bytes;
                const uint32_t offset = end - part;
                check_lower_transport_builtin_store(writer, part);
                if (conditional)
                {
                    // false ^ ((true ^ false) & mask).
                    check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, combine);
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, piece_bytes);
                    check_lower_transport_at(writer, sources[2], offset);
                    check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, bit_and);
                    check_lower_vector_logical_mask(writer, sources[0], offset, piece_bytes, compare, zero);
                    check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, combine);
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, piece_bytes);
                    check_lower_transport_at(writer, sources[2], offset);
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, piece_bytes);
                    check_lower_transport_at(writer, sources[1], offset);
                }
                else
                {
                    if (operands == 2)
                    {
                        check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, combine);
                        check_lower_vector_logical_mask(writer, sources[1], offset, piece_bytes, compare, zero);
                    }
                    check_lower_vector_logical_mask(writer, sources[0], offset, piece_bytes, compare, zero);
                }
                check_lower_element_address(writer, object, offset);
                end = offset;
            }
        }
        for (uint32_t a = operands; a-- > 0;)
        {
            if (a != 1 || !reuse)
            {
                const uint32_t child = a == 0 ? graph->operand0[node] : a == 1 ? graph->operand1[node] : graph->operand2[node];
                const TypeId from = check_stream_type(stream, child);
                check_lower_vector_logical_source(writer, inputs[a], sources[a]);
                if (!check_lower_transport_is_vector(types, from))
                {
                    // An operator splats its scalar, including a GNU vector arm whose cast would instead use its bits.
                    if (check_lower_transport_register(writer, inputs[a]))
                    {
                        check_lower_vector_splat(writer, from, inputs[a]);
                    }
                    else
                    {
                        check_lower_transport_splat_value(writer, from, inputs[a]);
                    }
                    check_lower_operand(writer, node, a, false, from);
                }
                else
                {
                    check_lower_operand(writer, node, a, false, inputs[a]);
                }
            }
        }
        writer->vectors = true;
    }
}

// Integer elementwise builtins on <N x i1> are operations on packed bits: unsigned saturation is OR or A & ~B,
// popcount and bitreverse are identities, and a one-bit funnel shift has a zero modulo-one count (left A, right B).
// Process a register's bits together instead of unpacking lanes. Every argument is evaluated once, including the
// unused arguments of a funnel shift, and the result's padding is cleared as for ordinary boolean-vector operators.
// Clang 23's elementwise CLZG/CTZG of bools emit an invalid scalar-to-vector truncation, so retain a named refusal.
static __attribute__((noinline)) void check_lower_transport_boolean_builtin(CheckLowerWriter* writer, uint32_t node, CBuiltinId builtin, uint32_t arguments)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeId result = check_stream_type(stream, node);
    const uint32_t first = graph->argument_offsets[node];
    const uint32_t bytes = (uint32_t)type_size(types, type_conversion_operand(types, result));
    CheckLowerTransportBits bits = { .op = CHECK_OP_COUNT };
    bool valid = arguments >= 1 && arguments <= 3 && arguments <= graph->argument_counts[node];
    switch (builtin)
    {
        case CBUILTIN_ELEMENTWISE_MAX: case CBUILTIN_ELEMENTWISE_ADD_SAT: bits.op = CHECK_OP_BIT_OR; break;
        case CBUILTIN_ELEMENTWISE_MIN: case CBUILTIN_ELEMENTWISE_CLMUL:
        case CBUILTIN_ELEMENTWISE_PDEP: case CBUILTIN_ELEMENTWISE_PEXT: bits.op = CHECK_OP_BIT_AND; break;
        case CBUILTIN_ELEMENTWISE_SUB_SAT: bits.op = CHECK_OP_BIT_AND; bits.not_b = true; break;
        case CBUILTIN_ELEMENTWISE_POPCOUNT: case CBUILTIN_ELEMENTWISE_BITREVERSE:
        case CBUILTIN_ELEMENTWISE_FSHL: case CBUILTIN_ELEMENTWISE_FSHR: break;
        default: valid = false; break;
    }
    if (builtin == CBUILTIN_ELEMENTWISE_CLZG || builtin == CBUILTIN_ELEMENTWISE_CTZG)
    {
        check_lower_unsupported(writer, "elementwise leading/trailing zero counts of boolean vectors, which clang 23 miscompiles");
    }
    else if (!valid)
    {
        check_lower_unsupported(writer, "elementwise builtins of boolean vectors");
    }
    else
    {
        const uint32_t operands = bits.op == CHECK_OP_COUNT ? 1 : 2;
        const uint32_t operand_start = builtin == CBUILTIN_ELEMENTWISE_FSHR ? 1 : 0;
        const uint32_t width = check_lower_transport_width(writer);
        const uint32_t piece = bytes < width ? bytes : width;
        const bool vector = bytes > 8;
        const uint8_t stored = vector ? BUSTER_TYPED_CLASS_VECTOR : bytes == 1 ? BUSTER_TYPED_CLASS_U8 : bytes == 2 ? BUSTER_TYPED_CLASS_U16 :
                                                                                    bytes == 4 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
        const uint8_t computed = bytes <= 4 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
        uint32_t sources[3] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        for (uint32_t a = 0; a < arguments; a += 1)
        {
            sources[a] = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        }
        uint32_t combine = BUSTER_CHECK_NONE;
        uint32_t negate = BUSTER_CHECK_NONE;
        if (vector)
        {
            const CheckVectorElement element = { .bytes = 1 };
            Tier0Operation row;
            if (operands == 2)
            {
                combine = check_lower_transport_row(writer, bits.op, element, piece, 0, &row);
            }
            if (bits.not_a || bits.not_b)
            {
                negate = check_lower_transport_row(writer, CHECK_OP_BIT_NOT, element, piece, 0, &row);
            }
        }
        const uint32_t object = check_lower_transport_result(writer, result);
        check_lower_transport_clear(writer, result, object);
        for (uint32_t p = bytes / piece; p-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const uint32_t offset = p * piece;
            check_lower_instruction(writer, BUSTER_TYPED_SET, stored, vector ? piece : 0);
            if (!vector && stored != computed)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, stored, 0);
            }
            if (operands == 2)
            {
                check_lower_instruction(writer, vector ? BUSTER_TYPED_X86 : check_lower_binary((CheckOp)bits.op),
                                         vector ? BUSTER_TYPED_CLASS_VECTOR : computed, vector ? combine : 0);
            }
            for (uint32_t a = operands; a-- > 0;)
            {
                if (a == 0 ? bits.not_a : bits.not_b)
                {
                    check_lower_instruction(writer, vector ? BUSTER_TYPED_X86 : BUSTER_TYPED_NOT, vector ? BUSTER_TYPED_CLASS_VECTOR : computed, vector ? negate : 0);
                }
                if (!vector && stored != computed)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, computed, 0);
                }
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored, vector ? piece : 0);
                check_lower_transport_at(writer, sources[a + operand_start], offset);
            }
            check_lower_element_address(writer, object, offset);
        }
        for (uint32_t a = arguments; a-- > 0;)
        {
            const TypeId type = stream->argument_conversions[first + a].type;
            check_lower_vector_logical_source(writer, type, sources[a]);
            check_lower_operand(writer, node, a + 3, false, type);
        }
        writer->vectors |= vector;
    }
}

// An elementwise builtin of transport vectors uses the ordinary row on disjoint register-width pieces. ldexp's
// exponent can have another element width: the piece's lane count is bounded by the widest argument, and every
// argument advances by its own element width. No type is interned while a checked body's immutable table is read.
static __attribute__((noinline)) void check_lower_transport_elementwise(CheckLowerWriter* writer, uint32_t node, CBuiltinId builtin, uint32_t arguments)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeId result = check_stream_type(stream, node);
    const uint32_t first = graph->argument_offsets[node];
    const CheckVectorShape shape = check_vector_builtin_shape(types, result);
    const bool ldexp = builtin == CBUILTIN_ELEMENTWISE_LDEXP;
    const bool floating = check_vector_floating_instructions(builtin);
    CheckVectorShape inputs[3] = { 0 };
    uint32_t widest = shape.element;
    bool valid = arguments >= 1 && arguments <= 3 && arguments <= graph->argument_counts[node] && shape.element != 0 && shape.lanes != 0;
    for (uint32_t a = 0; valid && a < arguments; a += 1)
    {
        inputs[a] = check_vector_builtin_shape(types, stream->argument_conversions[first + a].type);
        valid = inputs[a].element != 0 && inputs[a].lanes == shape.lanes;
        widest = inputs[a].element > widest ? inputs[a].element : widest;
    }
    if (!valid)
    {
        check_lower_unsupported(writer, "elementwise vector builtins of elements of this size");
    }
    else if (shape.lanes > BUSTER_CHECK_LOWER_TRANSPORT_LANES)
    {
        check_lower_unsupported(writer, "elementwise vector builtins of more than 1024 elements");
    }
    else
    {
        const uint32_t width = check_lower_transport_width(writer);
        const uint32_t capacity = shape.bytes / shape.element;
        const uint32_t lanes = capacity < width / widest ? capacity : width / widest;
        const uint32_t bytes = lanes * shape.element;
        CheckVectorShape piece = shape;
        piece.bytes = bytes < 4 ? 4 : bytes;
        piece.lanes = piece.bytes / piece.element;
        CheckVectorShape exponent = inputs[arguments > 1 ? 1 : 0];
        exponent.bytes = lanes * exponent.element;
        exponent.bytes = exponent.bytes < 4 ? 4 : exponent.bytes;
        exponent.lanes = piece.lanes;
        const uint32_t negated = builtin == CBUILTIN_ELEMENTWISE_FMA && shape.bfloat && arguments == 3 ? check_lower_brain_negations(program, first, result) : 0;
        Tier0Operation row;
        // Qwords' minimum and maximum without SSE4.2: a mask's row and a blend's, each piece's operands loaded for both
        // (check_vector_baseline_extreme_pair, #1834).
        Tier0Operation pair_rows[2];
        const bool pair = arguments == 2 && !ldexp && !floating && check_vector_baseline_extreme_pair(&writer->target, builtin, piece, pair_rows);
        CheckVectorConstants constants = { 0 };
        const char* reason = pair ? NULL :
                             ldexp ? check_vector_ldexp_shape_row(&writer->target, piece, exponent, &row) :
                             floating ? check_vector_floating_shape_row(builtin, piece, &row, &constants) :
                                        check_vector_elementwise_shape_row(&writer->target, builtin, piece, arguments, negated, &row, &constants);
        if (pair)
        {
            row = pair_rows[1];
        }
        if (reason != NULL)
        {
            check_lower_unsupported(writer, reason);
        }
        else
        {
            const uint32_t operation = check_lower_operation_index(writer, &row);
            const uint32_t mask_operation = pair ? check_lower_operation_index(writer, pair_rows) : BUSTER_CHECK_NONE;
            uint32_t sources[3] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
            for (uint32_t a = 0; a < arguments; a += 1)
            {
                sources[a] = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            }
            const uint32_t object = check_lower_transport_result(writer, result);
            for (uint32_t p = capacity / lanes; p-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
            {
                check_lower_transport_builtin_store(writer, bytes);
                check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, operation);
                check_lower_row_constants(writer, &row, &constants, arguments);
                for (uint32_t pass = pair ? 2 : 1; pass-- > 0;)
                {
                    for (uint32_t a = arguments; a-- > 0;)
                    {
                        check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, pair ? piece.bytes : row.operand_bytes[a]);
                        check_lower_transport_at(writer, sources[a], (uint64_t)p * lanes * inputs[a].element);
                    }
                    if (pass == 1)
                    {
                        check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, mask_operation);
                    }
                }
                check_lower_element_address(writer, object, p * bytes);
            }
            for (uint32_t a = arguments; a-- > 0;)
            {
                const TypeId type = stream->argument_conversions[first + a].type;
                const bool folded = (negated >> a & 1) != 0;
                check_lower_transport_builtin_source(writer, type, sources[a]);
                check_lower_operand(writer, folded ? graph->argument_nodes[first + a] : node, folded ? 0 : a + 3, false, type);
            }
            writer->vectors = true;
        }
    }
}

// An element-wise conversion of the vector on top of the stack, from `from` to `to`, of as many elements, at least one
// of them a transport vector (#137): __builtin_convertvector's, and the implicit conversion of an extended vector to
// one of bool, whose elements are its elements' truths, as clang's icmp ne 0. Each element converts as C converts a
// scalar, which clang's conversions of vectors do element by element (fptosi, fptrunc, sitofp, zext of an i1...).
static __attribute__((noinline)) void check_lower_transport_convert(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const uint32_t lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, to));
    if (lanes == 0 || lanes != check_builtin_vector_lanes(types, type_conversion_operand(types, from)))
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (lanes > BUSTER_CHECK_LOWER_TRANSPORT_LANES)
    {
        check_lower_unsupported(writer, "element-wise conversions of vectors of more than 1024 elements");
    }
    else
    {
        uint32_t indices[BUSTER_CHECK_LOWER_TRANSPORT_LANES];
        for (uint32_t i = 0; i < lanes; i += 1)
        {
            indices[i] = i;
        }
        const uint32_t source = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t sources[2] = { source, source };
        const uint32_t object = check_lower_transport_result(writer, to);
        check_lower_transport_gather(writer, sources, from, to, object, indices);
        check_lower_transport_source(writer, from, source);
    }
}

// A scalar's splat into a transport vector of type `to` (#137), of the value of type `from` on top of the stack,
// written into a frame object whose address is the result: an ext_vector_type vector's cast of a scalar, or a scalar's
// conversion to the vector. The scalar converts to the element's type first, as an ordinary vector's splat does
// (check_lower_vector_splat); a vector of bool's elements are all its truth, every byte's bits or none, its bits past
// its elements cleared; a wide vector's pieces are a splat row's at the target's widest registers' width.
static void check_lower_transport_splat_value(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const TypeId base = type_conversion_operand(types, to);
    const uint32_t bytes = (uint32_t)type_size(types, base);
    const TypeId element = check_lower_transport_element(types, to);
    const uint32_t result = check_lower_transport_object(writer, to);
    check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    if (check_lower_bool_vector(types, to))
    {
        const uint32_t local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t chunk = bytes < 8 ? bytes : 8;
        const uint8_t stored = chunk == 1 ? BUSTER_TYPED_CLASS_U8 : chunk == 2 ? BUSTER_TYPED_CLASS_U16 : chunk == 4 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
        if (check_builtin_vector_lanes(types, base) < 8 * bytes)
        {
            check_lower_transport_clear(writer, to, result);
        }
        for (uint32_t k = bytes / chunk; k-- > 0;)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET, stored, 0);
            if (stored != BUSTER_TYPED_CLASS_U64)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, stored, 0);
            }
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
            check_lower_element_address(writer, result, k * chunk);
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_convert(writer, BUSTER_TYPE_BOOL, BUSTER_TYPE_ULONG);
        check_lower_convert(writer, from, BUSTER_TYPE_BOOL);
    }
    else if (bytes <= 2)
    {
        const uint8_t class = check_lower_class(writer, element);
        const uint32_t size = (uint32_t)type_size(types, element);
        const uint32_t local = check_lower_temporary(writer, class);
        for (uint32_t i = size != 0 ? bytes / size : 0; i-- > 0;)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET, class, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, local);
            check_lower_element_address(writer, result, i * size);
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
        check_lower_convert(writer, from, element);
    }
    else
    {
        const CheckVectorElement shape = check_vector_lower_brain(types, to) ? (CheckVectorElement){ .bytes = 2, .floating = true, .is_signed = true } :
                                                                             check_vector_lower_element(types, to);
        const uint32_t width = check_lower_transport_width(writer);
        const uint32_t piece = bytes < width ? bytes : width;
        const uint32_t local = check_lower_transport_splat_temporary(writer, piece);
        for (uint32_t p = bytes / piece; p-- > 0;)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, piece);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, local);
            check_lower_element_address(writer, result, p * piece);
        }
        if (shape.bytes == 0)
        {
            check_lower_unsupported(writer, "splats of this type into vectors");
        }
        else
        {
            check_lower_transport_splat(writer, shape, element, piece, local);
            check_lower_convert(writer, from, element);
        }
    }
}

// A conversion of the value on top of the stack between `from` and `to`, one of them a transport vector (#137,
// check_lower_aggregate_conversion's), as clang's casts and conversions compute it:
//   - between vectors of the same bytes, their bits: nothing between two transport vectors, an ordinary vector's
//     loaded from a transport vector's object or set into one;
//   - between vectors of as many elements and other bytes (an extended vector's conversion to one of bool), element by
//     element (check_lower_transport_convert);
//   - from a transport vector to an integer of its bytes, its bits, loaded from its object;
//   - from a scalar, a GNU C vector's cast of an integer of its bytes, its bits, set into a temporary whose address is
//     the result; any other (an ext_vector_type vector's cast, a conversion), its splat (check_lower_transport_splat_value).
// Others are refused as an ordinary vector's are: casts between vectors and integers or vectors of other sizes.
static __attribute__((noinline)) void check_lower_transport_cast(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const TypeId source = type_conversion_operand(types, from);
    const TypeId target = type_conversion_operand(types, to);
    const bool from_vector = check_lower_transport_is_vector(types, source);
    const bool to_vector = check_lower_transport_is_vector(types, target);
    const uint64_t from_size = type_size(types, source);
    const uint64_t to_size = type_size(types, target);
    if (from_vector && to_vector && from_size == to_size && !check_lower_transport_register(writer, source) && !check_lower_transport_register(writer, target))
    {
    }
    else if (from_vector && to_vector && from_size == to_size && check_lower_transport_register(writer, target))
    {
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, to_size);
    }
    else if (from_vector && to_vector && from_size == to_size)
    {
        const uint32_t object = check_lower_transport_splat_temporary(writer, (uint32_t)from_size);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, object);
    }
    else if (from_vector && to_vector && check_builtin_vector_lanes(types, source) == check_builtin_vector_lanes(types, target) &&
             check_lower_bool_vector(types, target) && type_kind(types, target) == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        check_lower_transport_convert(writer, from, to);
    }
    else if (from_vector && !to_vector && check_action_is_integer(types, target) && to_size == from_size && !check_lower_bool_vector(types, source))
    {
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, check_lower_class(writer, target), 0);
    }
    else if (!from_vector && to_vector && type_kind(types, target) == BUSTER_TYPE_KIND_VECTOR && check_action_is_integer(types, source) && to_size == from_size &&
             !check_lower_bool_vector(types, target))
    {
        const uint8_t class = check_lower_class(writer, source);
        const uint32_t local = check_lower_addressed_temporary(writer, class);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
    }
    else if (!from_vector && to_vector && check_lower_storage_class(writer, source) < BUSTER_TYPED_CLASS_F80 &&
             (check_action_is_integer(types, source) || check_lower_class(writer, source) == BUSTER_TYPED_CLASS_F32 ||
              check_lower_class(writer, source) == BUSTER_TYPED_CLASS_F64 || check_lower_half(types, source) != CHECK_HALF_NONE))
    {
        check_lower_transport_splat_value(writer, from, to);
    }
    else if (!from_vector && to_vector)
    {
        check_lower_unsupported(writer, "splats of this type into vectors");
    }
    else
    {
        check_lower_unsupported(writer, "casts between vectors and integers or vectors of other sizes");
    }
}

// A transport vector's constant (#137): an integer's bits the checker folded (a cast of an integer constant), written
// into a frame object whose address is its value. Emitted in place, as check_lower_constant's entries are.
static void check_lower_transport_constant(CheckLowerWriter* writer, const CheckValue* value)
{
    const TypeTable* types = writer->program->types;
    const uint32_t bytes = (uint32_t)type_size(types, type_conversion_operand(types, value->type));
    if (value->constant_kind != CHECK_CONSTANT_INTEGER || bytes > 16)
    {
        check_lower_unsupported(writer, check_lower_transport_refusal(types, value->type, CHECK_LOWER_TRANSPORT_CONSTANT));
    }
    else
    {
        const uint32_t result = check_lower_transport_object(writer, value->type);
        const uint32_t chunk = bytes < 8 ? bytes : 8;
        const uint8_t class = chunk == 1 ? BUSTER_TYPED_CLASS_U8 : chunk == 2 ? BUSTER_TYPED_CLASS_U16 : chunk == 4 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
        const unsigned __int128 integer = (unsigned __int128)value->integer;
        for (uint32_t k = 0; k < bytes / chunk; k += 1)
        {
            const uint64_t word = (uint64_t)(integer >> (64 * k));
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
            if (k != 0)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 8 * k);
                check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            }
            check_lower_emit(writer, BUSTER_TYPED_CONST, class, chunk == 8 ? word : word & ((UINT64_C(1) << (8 * chunk)) - 1));
            check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
    }
}

// __builtin_shufflevector of transport vectors, or into one (#137): the result's elements gathered from the two
// vectors' by the constant indices (check_lower_transport_gather), the second vector left unevaluated where it is
// __builtin_nondeterministic_value's, whose elements are then undefined (0), as the ordinary rows take it.
static __attribute__((noinline)) void check_lower_transport_shuffle(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const uint32_t first = graph->argument_offsets[node];
    const uint32_t lanes = graph->argument_counts[node] - 2;
    const TypeId source = stream->argument_conversions[first].type;
    const TypeId result = check_stream_type(stream, node);
    const uint32_t in_lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, source));
    const bool b_undefined = check_lower_nondeterministic(program, graph->argument_nodes[first + 1]);
    uint32_t indices[BUSTER_CHECK_LOWER_TRANSPORT_LANES];
    bool constant = lanes <= BUSTER_CHECK_LOWER_TRANSPORT_LANES && lanes == check_builtin_vector_lanes(types, type_conversion_operand(types, result));
    for (uint32_t i = 0; constant && i < lanes; i += 1)
    {
        CheckValue value = check_stream_value(stream, graph->argument_nodes[first + 2 + i]);
        uint32_t width = 0;
        bool is_signed = false;
        constant = value.constant_kind == CHECK_CONSTANT_INTEGER && check_action_integer_type(types, value.type, &width, &is_signed);
        const unsigned __int128 bits = constant ? value.integer & check_action_mask(width) : 0;
        const bool undefined = is_signed && bits == check_action_mask(width);
        indices[i] = undefined || (b_undefined && bits >= in_lanes) ? BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED : (uint32_t)bits;
        constant &= undefined || bits < (unsigned __int128)2 * in_lanes;
    }
    if (lanes > BUSTER_CHECK_LOWER_TRANSPORT_LANES)
    {
        check_lower_unsupported(writer, "vector shuffles of more than 1024 elements");
    }
    else if (!constant || in_lanes == 0)
    {
        check_lower_unsupported(writer, "vector builtins of operands the checker doesn't fold");
    }
    else
    {
        const uint32_t sources[2] = { check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64), check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64) };
        const uint32_t object = check_lower_transport_result(writer, result);
        check_lower_transport_gather(writer, sources, source, result, object, indices);
        for (uint32_t a = 2; a-- > 0;)
        {
            if (a == 0 || !b_undefined)
            {
                const TypeId type = stream->argument_conversions[first + a].type;
                check_lower_transport_source(writer, type, sources[a]);
                check_lower_operand(writer, node, 3 + a, false, type);
            }
        }
    }
}

// __builtin_convertvector of a transport vector, or into one (#137): element by element (check_lower_transport_convert).
static __attribute__((noinline)) void check_lower_transport_convertvector(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const TypeId source = program->stream->argument_conversions[program->input->graph.argument_offsets[node]].type;
    check_lower_transport_convert(writer, source, check_stream_type(program->stream, node));
    check_lower_operand(writer, node, 3, false, source);
}

// A floating reduction of a wide vector first pairs its low and high halves, repeatedly, in register-width pieces.
// The last piece uses the ordinary reduction rows. This preserves the full vector's reduction tree rather than
// reducing independent pieces and adding their scalar results, which changes roundings and NaN selection.
static __attribute__((noinline)) void check_lower_transport_reduce_floating(CheckLowerWriter* writer, uint32_t node, CBuiltinId builtin)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    const CheckStream* stream = program->stream;
    const CheckGraph* graph = &program->input->graph;
    const uint32_t first = graph->argument_offsets[node];
    const TypeId source = stream->argument_conversions[first].type;
    const CheckVectorShape shape = check_vector_builtin_shape(types, source);
    const bool sum = builtin == CBUILTIN_REDUCE_ASSOC_FADD;
    const bool number = builtin == CBUILTIN_REDUCE_MAX || builtin == CBUILTIN_REDUCE_MIN;
    const bool maximum = builtin == CBUILTIN_REDUCE_MAXIMUM || builtin == CBUILTIN_REDUCE_MAX;
    if (!shape.floating || shape.bfloat || shape.element == 2)
    {
        check_lower_unsupported(writer, "vector reductions of half-precision or bf16 elements");
    }
    else if (shape.lanes == 0 || shape.lanes > BUSTER_CHECK_LOWER_TRANSPORT_LANES)
    {
        check_lower_unsupported(writer, "vector reductions of more than 1024 elements");
    }
    else if (!sum && shape.lanes * shape.element != shape.bytes)
    {
        check_lower_unsupported(writer, "floating maximum and minimum reductions of vectors their elements don't fill");
    }
    else
    {
        const uint32_t width = check_lower_transport_width(writer);
        const uint32_t bytes = shape.bytes < width ? shape.bytes : width;
        CheckVectorShape piece = shape;
        piece.bytes = bytes;
        piece.lanes = bytes / shape.element;
        Tier0Operation tail[BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS];
        uint32_t count = 0;
        const uint8_t class = shape.element == 8 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
        Tier0Operation scalar;
        Tier0Operation scalar_tail;
        CheckVectorConstants scalar_constants = { 0 };
        // The lanes' fold is LLVM's where its legal vectors are AVX-512's; a narrower target's legalization isn't modelled.
        const char* reason = number && (writer->target.features & BUSTER_X86_FEATURE(AVX512F)) == 0 ? "elementwise builtins of scalars without AVX-512F" :
                             number ? check_vector_floating_scalar_row(&writer->target, maximum ? CBUILTIN_ELEMENTWISE_MAXNUM : CBUILTIN_ELEMENTWISE_MINNUM,
                                                                       class, &scalar, &scalar_constants, &scalar_tail) :
                                      check_vector_reduce_floating_shape_rows(builtin, piece, tail, &count);
        Tier0Operation combine;
        CheckVectorConstants constants = { 0 };
        if (reason == NULL && shape.bytes > bytes)
        {
            if (sum)
            {
                const CheckVectorElement element = { .bytes = shape.element, .floating = true, .is_signed = true };
                reason = check_vector_lower_element_row(&writer->target, false, CHECK_OP_ADD, element, bytes, 0, &combine);
            }
            else
            {
                const CBuiltinId operation = number ? maximum ? CBUILTIN_ELEMENTWISE_MAXNUM : CBUILTIN_ELEMENTWISE_MINNUM :
                                                     maximum ? CBUILTIN_ELEMENTWISE_MAXIMUM : CBUILTIN_ELEMENTWISE_MINIMUM;
                reason = check_vector_floating_shape_row(operation, piece, &combine, &constants);
            }
        }
        if (reason != NULL)
        {
            check_lower_unsupported(writer, reason);
        }
        else
        {
            const uint64_t sign = check_vector_floating_sign(shape.element == 8);
            const uint32_t object = check_lower_transport_object(writer, source);
            const uint32_t local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            const bool explicit_start = sum && graph->argument_counts[node] == 2;
            const uint32_t start = explicit_start ? check_lower_temporary(writer, class) : BUSTER_CHECK_NONE;
            const uint32_t operation = shape.bytes > bytes ? check_lower_operation_index(writer, &combine) : BUSTER_CHECK_NONE;
            if (number)
            {
                // LLVM's fmax/fmin reduction legalizes wide vectors by pairing their halves, then folds the legal
                // vector's lanes in source order. In particular, equal signed zeros keep that fold's first number.
                const uint32_t accumulator = check_lower_temporary(writer, class);
                const uint32_t step = check_lower_operation_index(writer, &scalar);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, accumulator);
                for (uint32_t lane = piece.lanes; lane-- > 1;)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, accumulator);
                    check_lower_instruction(writer, BUSTER_TYPED_X86, class, step);
                    check_lower_row_constants(writer, &scalar, &scalar_constants, 2);
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
                    check_lower_element_address(writer, object, lane * shape.element);
                    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, accumulator);
                }
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, accumulator);
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
                check_lower_element_address(writer, object, 0);
            }
            for (uint32_t r = count; r-- > 0;)
            {
                check_lower_instruction(writer, BUSTER_TYPED_X86, tail[r].result_class, check_lower_operation_index(writer, tail + r));
                if (!sum && !number)
                {
                    uint64_t words[8] = { 0 };
                    const uint64_t mask = maximum ? sign - 1 : sign;
                    for (uint32_t w = 0; w < tail[r].operand_bytes[1] / 8; w += 1)
                    {
                        words[w] = shape.element == 8 ? mask : mask | mask << 32;
                    }
                    if (tail[r].operand_bytes[1] == 4)
                    {
                        words[0] = mask;
                    }
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR,
                                             check_lower_vector_constant(writer, words, tail[r].operand_bytes[1]));
                }
                else if (sum && r == count - 1 && explicit_start)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, start);
                }
                else if (sum && r == count - 1)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, class, sign);
                }
            }
            if (!number)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                check_lower_element_address(writer, object, 0);
            }
            // Pushed from the last halving to the first, so the widest runs first and no unread half is overwritten.
            for (uint32_t span = bytes * 2; span <= shape.bytes; span *= 2)
            {
                const uint32_t half = span / 2;
                for (uint32_t p = half / bytes; p-- > 0;)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, bytes);
                    check_lower_instruction(writer, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, operation);
                    check_lower_row_constants(writer, &combine, &constants, 2);
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                    check_lower_element_address(writer, object, half + p * bytes);
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                    check_lower_element_address(writer, object, p * bytes);
                    check_lower_element_address(writer, object, p * bytes);
                }
            }
            // An associative sum's padding is its -0.0 identity. It never reads unspecified padding as data.
            for (uint32_t lane = shape.bytes / shape.element; lane-- > shape.lanes;)
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET, class, 0);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, class, sign);
                check_lower_element_address(writer, object, lane * shape.element);
            }
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = shape.bytes, .address = false });
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
            if (explicit_start)
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, start);
                check_lower_operand(writer, node, 4, false, stream->argument_conversions[first + 1].type);
            }
            check_lower_transport_source(writer, source, local);
            check_lower_operand(writer, node, 3, false, source);
            writer->vectors = true;
        }
    }
}

// An integer reduction of a transport vector (#137, __builtin_reduce_add, _mul, _and, _or, _xor, _max and _min): its
// elements folded first to last in their promoted type (int, or the element's own type from 4 bytes; a vector of
// bool's in unsigned int, its elements 0 or 1, where a sum is an exclusive or and a product a conjunction, as i1's
// arithmetic wraps), the result converted to the element's type, then to the call's. A maximum or minimum keeps the
// greater or lesser by a mask: b ^ ((a ^ b) & -(a > b)). Floating elements are refused by name.
static __attribute__((noinline)) void check_lower_transport_reduce(CheckLowerWriter* writer, uint32_t node, CBuiltinId builtin)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    const TypeId source = program->stream->argument_conversions[program->input->graph.argument_offsets[node]].type;
    const TypeId result = check_stream_type(program->stream, node);
    const TypeId element = check_lower_transport_element(types, source);
    const uint32_t lanes = check_builtin_vector_lanes(types, type_conversion_operand(types, source));
    const bool boolean = check_lower_bool_vector(types, source);
    const TypeId promoted = boolean ? BUSTER_TYPE_UINT : check_action_is_integer(types, element) ? type_size(types, element) < 4 ? BUSTER_TYPE_INT : element : 0;
    if (promoted == 0)
    {
        check_lower_unsupported(writer, check_lower_transport_refusal(types, source, CHECK_LOWER_TRANSPORT_BUILTIN));
    }
    else if (lanes == 0 || lanes > BUSTER_CHECK_LOWER_TRANSPORT_LANES)
    {
        check_lower_unsupported(writer, "vector reductions of more than 1024 elements");
    }
    else
    {
        const uint8_t class = check_lower_class(writer, promoted);
        const bool extreme = builtin == CBUILTIN_REDUCE_MAX || builtin == CBUILTIN_REDUCE_MIN;
        const uint8_t kind = builtin == CBUILTIN_REDUCE_ADD ? (boolean ? BUSTER_TYPED_XOR : BUSTER_TYPED_ADD) :
                             builtin == CBUILTIN_REDUCE_MUL ? (boolean ? BUSTER_TYPED_AND : BUSTER_TYPED_MUL) :
                             builtin == CBUILTIN_REDUCE_AND ? BUSTER_TYPED_AND :
                             builtin == CBUILTIN_REDUCE_OR ? BUSTER_TYPED_OR :
                             builtin == CBUILTIN_REDUCE_XOR ? BUSTER_TYPED_XOR :
                                                              BUSTER_TYPED_NONE;
        const uint32_t local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        // The running value and the next element, for a maximum's or minimum's mask.
        const uint32_t running = extreme ? check_lower_temporary(writer, class) : BUSTER_CHECK_NONE;
        const uint32_t next = extreme ? check_lower_temporary(writer, class) : BUSTER_CHECK_NONE;
        check_lower_convert(writer, boolean ? BUSTER_TYPE_BOOL : element, result);
        check_lower_convert(writer, promoted, boolean ? BUSTER_TYPE_BOOL : element);
        for (uint32_t i = lanes; i-- > 1 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            if (extreme)
            {
                // running' = next ^ ((running ^ next) & -(running > next)), for a maximum (< for a minimum).
                check_lower_instruction(writer, BUSTER_TYPED_XOR, class, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, next);
                check_lower_instruction(writer, BUSTER_TYPED_AND, class, 0);
                check_lower_instruction(writer, BUSTER_TYPED_NEG, class, 0);
                if (class != BUSTER_TYPED_CLASS_I32)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, class, 0);
                }
                check_lower_instruction(writer, builtin == CBUILTIN_REDUCE_MAX ? BUSTER_TYPED_GT : BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, next);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, running);
                check_lower_instruction(writer, BUSTER_TYPED_XOR, class, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, next);
                check_lower_instruction(writer, BUSTER_TYPED_STORE_LOCAL, class, running);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, next);
            }
            else
            {
                check_lower_instruction(writer, kind, class, 0);
            }
            if (boolean)
            {
                check_lower_convert(writer, BUSTER_TYPE_BOOL, promoted);
            }
            check_lower_transport_read(writer, local, source, i, boolean ? BUSTER_TYPE_BOOL : promoted);
        }
        if (boolean)
        {
            check_lower_convert(writer, BUSTER_TYPE_BOOL, promoted);
        }
        check_lower_transport_read(writer, local, source, 0, boolean ? BUSTER_TYPE_BOOL : promoted);
        check_lower_transport_source(writer, source, local);
        check_lower_operand(writer, node, 3, false, source);
    }
}

// The address of the byte that holds element `index` (local `index_local`, I64) of the vector of bool whose address
// local `vector_local` (U64) holds, and the element's bit's count in it (index & 7, I64), pushed as entries in this
// order to run.
static void check_lower_transport_bit_byte(CheckLowerWriter* writer, uint32_t vector_local, uint32_t index_local)
{
    check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    check_lower_instruction(writer, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_I64, 0);
    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 3);
    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, index_local);
    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, vector_local);
}

static void check_lower_transport_bit_count(CheckLowerWriter* writer, uint32_t index_local)
{
    check_lower_instruction(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I64, 0);
    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7);
    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, index_local);
}

// The vector of bool and the index of `subscript` (an element of one, operand 0 the vector, operand 1 the index), the
// vector's address (an lvalue's place, or an rvalue's object) set into `vector_local` and the index, as a long, into
// `index_local`, pushed to run first.
static void check_lower_transport_bit_operands(CheckLowerWriter* writer, uint32_t subscript, uint32_t vector_local, uint32_t index_local)
{
    const CheckStream* stream = writer->program->stream;
    const uint32_t base = writer->program->input->graph.operand0[subscript];
    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, index_local);
    check_lower_operand(writer, subscript, 1, false, BUSTER_TYPE_LONG);
    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, vector_local);
    const bool lvalue = check_stream_category(stream, base) == CHECK_LVALUE;
    check_lower_operand(writer, subscript, 0, lvalue, lvalue ? 0 : check_stream_type(stream, base));
}

// An element of a vector of bool (#137), the bit of its index's place: the byte that holds it loaded, shifted right by
// the bit's count in it and masked to its low bit, an unsigned int's 0 or 1 converted to the element's type, as clang's
// extractelement of an <N x i1> reads it.
static __attribute__((noinline)) void check_lower_transport_bit_read(CheckLowerWriter* writer, uint32_t node)
{
    const uint32_t vector_local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    const uint32_t index_local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I64);
    check_lower_convert(writer, BUSTER_TYPE_UINT, check_stream_type(writer->program->stream, node));
    check_lower_instruction(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U32, 0);
    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 1);
    check_lower_instruction(writer, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U32, 0);
    check_lower_transport_bit_count(writer, index_local);
    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
    check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
    check_lower_transport_bit_byte(writer, vector_local, index_local);
    check_lower_transport_bit_operands(writer, node, vector_local, index_local);
}

// An assignment to an element of a vector of bool (#137): its byte's other bits kept and its own set to the right
// operand's truth, as clang's insertelement of an <N x i1> and the store of the whole vector leave them; the
// expression's value is the element's new value, at the expression's type. A compound assignment or an increment of
// one is refused by name: clang 23's code for it extracts the element from the vector's memory integer (an
// extractelement of an i8, invalid IR), which crashes or computes no element's value.
static __attribute__((noinline)) void check_lower_transport_bit_store(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckStream* stream = program->stream;
    const uint32_t subscript = program->input->graph.operand0[node];
    if (program->input->graph.ops[node] != CHECK_OP_ASSIGN)
    {
        check_lower_unsupported(writer, "compound assignments and increments of elements of vectors of bool, which clang 23 miscompiles");
    }
    else
    {
        const TypeId right = check_stream_type(stream, program->input->graph.operand1[node]);
        const uint32_t vector_local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t index_local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I64);
        const uint32_t byte_local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t new_local = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U32);
        // The value, the new element.
        check_lower_convert(writer, BUSTER_TYPE_UINT, check_stream_type(stream, node));
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U32, new_local);
        // The byte stored: (byte & ~(1 << count)) | (new << count).
        check_lower_instruction(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U8, 0);
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
        check_lower_instruction(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_transport_bit_count(writer, index_local);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U32, new_local);
        check_lower_instruction(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_NOT, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_transport_bit_count(writer, index_local);
        check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 1);
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, byte_local);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, byte_local);
        // The new element's truth, 0 or 1, after the element's place, as the operands of an ordinary assignment run.
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U32, new_local);
        check_lower_convert(writer, BUSTER_TYPE_BOOL, BUSTER_TYPE_UINT);
        check_lower_convert(writer, right, BUSTER_TYPE_BOOL);
        check_lower_operand(writer, node, 1, false, right);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, byte_local);
        check_lower_transport_bit_byte(writer, vector_local, index_local);
        check_lower_transport_bit_operands(writer, subscript, vector_local, index_local);
    }
}
