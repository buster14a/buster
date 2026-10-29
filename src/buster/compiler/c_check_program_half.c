// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Half-precision scalars (#136, for #129's --lower-all of main.c): _Float16's and __fp16's IEEE binary16 and __bf16's
// bfloat16, as clang -O0's code computes them. Their types have classes of their own past Tier 0's
// (BUSTER_CHECK_LOWER_CLASS_BINARY16 and _BFLOAT16, check_lower_type_class), past VECTOR, so that what the tasks' loop
// does with any value does it with them and takes them to code that only they reach, out of line: an entry of one
// (check_lower_emit's class past the function's) is Tier 0's that check_lower_half_entry writes for it, an operator on one
// is check_lower_vector_operation's (which the loop's test of VECTOR takes them to), and so are their conversions
// (check_lower_vector_conversion's), their truth, their updates and their calls'. A value of one is its 16 bits on the
// integer stack (U16), as clang's IR keeps a `half` or a `bfloat`: its loads, stores, copies, members and vector elements
// are a 16-bit integer's, so that they keep every bit, a NaN's payload among them, and a local of one is a frame object of
// its 2 bytes, as clang -O0 keeps one. A parameter of one arrives in an XMM register's low 16 bits (the convention's SSE
// class), which the prologue stores into its object as a struct's, a call's result comes back there, and a return's and
// an argument's go there: those entries move the bits between the stacks (VMOVD where the function's target has AVX, or
// through a frame temporary, as an integer's bit cast to a float does). What computes with one converts it first:
//   - extended to float, exactly: bfloat16's bits shifted up by 16; binary16's by F16C's VCVTPH2PS where the function's
//     target has F16C, or else libgcc's __extendhfsf2 (to double and long double through float, also exact);
//   - a float rounded to one: VCVTPS2PH (after a VMOVD into the register has zeroed the other lanes, so that they raise
//     nothing), AVX512-BF16's or AVX-NE-CONVERT's VCVTNEPS2BF16, or libgcc's __truncsfhf2 and __truncsfbf2, as LLVM
//     selects them; a double and a long double are libgcc's __truncdfhf2, __truncdfbf2, __truncxfhf2 and __truncxfbf2,
//     as clang calls them (with AVX512-FP16, LLVM's VCVTSD2SH rounds the same), a constant's at run time too;
//   - an integer through float, as LLVM converts one: a binary16's from an integer's float is the integer's own
//     rounding (every integer below 65520 is a float's), and a bfloat16's is LLVM's double rounding; but a 128-bit
//     integer and binary16 through libgcc's __floattihf ... __fixunshfti, as clang's are; and a constant rounded once,
//     as clang folds it (check_lower_half_constant).
// In a function with FENV_ACCESS anywhere, clang's floating operations are all constrained intrinsics, which its -O0
// code folds none of and rounds a float to bfloat16 by __truncsfbf2: so are the lowering's (check_lower_half_strict).
// Arithmetic is clang's excess precision (-ffloat16-excess-precision and -fbfloat16-excess-precision, standard), where the
// unit's target has no native arithmetic of the type, AVX512-FP16 for binary16 and AVX10.2 for bfloat16 (clang's
// TargetInfo decides it by the unit's target, not a function's attribute): +, -, * and / and the unary - of a chain of
// them compute in float, and the chain's value rounds once, where it leaves the chain (a store, a cast, a call's
// argument, a conditional's or a comma's operand: check_lower_half_chain). With native arithmetic, each operator rounds:
// float's two roundings of one operation round as once, as its 24 bits are at least twice 11 and 2. A unary + alone is
// its operand, as LLVM folds its round trip and the lowering's casts are (check_lower_plain), in a function with
// FENV_ACCESS too, where clang's constrained round trip quiets a signaling NaN. Compares and truth extend their
// operands, which then compare exactly: the checker compares two half-precision
// operands as floats (check_action_arithmetic). Included by c_check_program_lower.c after its emission, temporaries and
// operands.

// Whether the unit computes a format's arithmetic in float (clang's excess precision): where its target has no native
// arithmetic of it.
static bool check_lower_half_excess(const CheckLowerWriter* writer, CheckHalf format)
{
    const uint64_t native = format == CHECK_HALF_IEEE ? BUSTER_X86_EXTENSION(AVX512FP16) : BUSTER_X86_EXTENSION(AVX10_2);
    return (writer->program->target.extensions & native) == 0;
}

// Whether the function's body has FENV_ACCESS anywhere, a pragma's in a block of it or one before it: clang then emits
// every floating operation of the function as a constrained intrinsic, outside the pragma's block too, which its -O0 code
// folds none of (a conversion of a constant, a unary chain's round trip). Its tokens are read where a fold asks, once a
// body (CheckLowerWriter.half_strict_body): only a half-precision fold does, in a unit with a floating pragma, so that
// the scan stays scalar.
static bool check_lower_half_strict(CheckLowerWriter* writer)
{
    const CheckProgram* program = writer->program;
    const uint8_t* environments = program->input->fp_environments;
    const uint32_t opening = writer->body->opening;
    if (environments != NULL && writer->half_strict_body != opening + 1)
    {
        const uint32_t closing = program->parser->tree.parents[opening];
        bool strict = false;
        for (uint32_t token = opening; !strict && token <= closing; token += 1)
        {
            strict = (environments[token] & BUSTER_CHECK_FP_ENV_ACCESS) != 0;
        }
        writer->half_strict_body = opening + 1;
        writer->half_strict = strict;
    }
    return environments != NULL && writer->half_strict;
}

// ---------------------------------------------------------------------------------------------------------------------
// Constants

// Whether node `node`'s value, converted to `target` of a half-precision format, is a constant the lowering rounds at
// compile time, as clang folds a conversion of a constant: an integer's or a floating one's, of a real type (a half-
// precision one's own too), evaluated without effects, in a function without FENV_ACCESS (check_lower_half_strict); its
// bits in `bits`.
static bool check_lower_half_fold(CheckLowerWriter* writer, uint32_t node, TypeId target, uint16_t* bits)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    const CheckValue* value = program->stream->values + node;
    const bool plain = (value->constant_kind == CHECK_CONSTANT_INTEGER || value->constant_kind == CHECK_CONSTANT_FLOATING) && value->category == CHECK_RVALUE &&
                       value->fault == CHECK_FAULT_NONE && (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 &&
                       value->overload_head == 0 && !check_lower_half_strict(writer);
    const CheckHalf format = plain ? check_lower_half_of(writer, target) : CHECK_HALF_NONE;
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = format != CHECK_HALF_NONE && value->constant_kind == CHECK_CONSTANT_INTEGER &&
                         check_action_integer_type(types, value->type, &width, &is_signed) && width <= 128;
    const bool floating = format != CHECK_HALF_NONE && value->constant_kind == CHECK_CONSTANT_FLOATING &&
                          check_action_kind(types, value->type) == BUSTER_TYPE_KIND_FLOATING;
    const bool constant = integer || floating;
    if (constant && integer)
    {
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 field = value->integer & mask;
        const bool negative = is_signed && width != 0 && (field >> (width - 1)) != 0;
        *bits = check_action_half_round(format, negative, negative ? (0 - field) & mask : field, 0);
    }
    else if (constant)
    {
        *bits = check_action_half_bits(format, value->floating);
    }
    return constant;
}

// ---------------------------------------------------------------------------------------------------------------------
// The value's recipes, written as the tasks that need them run: the operand is on top of the stack. They append their
// entries (check_lower_append), as check_lower_half_entry, which check_lower_emit calls, writes its through them.

// An X86 entry of one operand of `operand_class` and a result of `result_class`, the row's `count` records.
static void check_lower_half_row(CheckLowerWriter* writer, const X86Instruction* records, uint32_t count, uint8_t operand_class, uint8_t result_class)
{
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    for (uint32_t i = 0; i < count; i += 1)
    {
        row.records[i] = records[i];
    }
    row.record_count = (uint8_t)count;
    row.operand_count = 1;
    row.operand_classes[0] = operand_class;
    row.result_class = result_class;
    check_lower_append(writer, BUSTER_TYPED_X86, result_class, check_lower_operation_index(writer, &row));
    writer->vectors = true;
}

// The function's temporary `which` of CheckLowerWriter.half_locals, of `class`, addressed: a local of the class with no
// value class (VOID), which no other local is, so that one an earlier body left is told from the body's own, which it
// takes the first time.
static uint32_t check_lower_half_temporary(CheckLowerWriter* writer, uint32_t which, uint8_t class)
{
    uint32_t* local = &writer->half_locals[which];
    if (*local >= writer->local_count || writer->local_classes[*local] != class || writer->value_classes[*local] != BUSTER_TYPED_CLASS_VOID)
    {
        *local = check_lower_addressed_temporary(writer, class);
        if (*local < writer->local_count)
        {
            writer->value_classes[*local] = BUSTER_TYPED_CLASS_VOID;
        }
    }
    return *local;
}

// The bits on top of the stack moved into a float's register (`into`: a U32's, zero-extended, into F32's low bits) or
// out of one (F32's low 16 bits as U16): VMOVD where the function's target has AVX, or else through the function's frame
// temporary of the source's class (check_lower_half_temporary), written whole and read at its first bytes.
static void check_lower_half_move(CheckLowerWriter* writer, bool into)
{
    const uint8_t from = into ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_F32;
    const uint8_t to = into ? BUSTER_TYPED_CLASS_F32 : BUSTER_TYPED_CLASS_U16;
    if ((writer->target.features & BUSTER_X86_FEATURE(AVX)) != 0)
    {
        const X86Instruction move = into ? check_vector_lower_record(X86_FORM_VMOVD_X_RM32, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, BUSTER_TIER0_ROLE_INTEGER(0)) :
                                           check_vector_lower_record(X86_FORM_VMOVD_RM32_X, BUSTER_TIER0_ROLE_FLOATING(0), 0, BUSTER_TIER0_ROLE_INTEGER_RESULT);
        check_lower_half_row(writer, &move, 1, from, to);
    }
    else
    {
        const uint32_t local = check_lower_half_temporary(writer, into ? 0 : 1, from);
        check_lower_append(writer, BUSTER_TYPED_SET_LOCAL, from, local);
        check_lower_append(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        check_lower_append(writer, BUSTER_TYPED_LOAD, to, 0);
    }
}

// The value on top of the stack, of `format`, extended to float (F32), exactly.
static void check_lower_half_extend(CheckLowerWriter* writer, CheckHalf format)
{
    check_lower_append(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
    if (format == CHECK_HALF_BRAIN)
    {
        // bfloat16 is float's top half: LLVM's shll $16, then movd.
        check_lower_append(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 16);
        check_lower_append(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_half_move(writer, true);
    }
    else if ((writer->target.features & BUSTER_X86_FEATURE(F16C)) != 0)
    {
        // The halves past the first are zeros, as the bits were zero-extended, so VCVTPH2PS raises nothing for them.
        const X86Instruction records[2] = {
            check_vector_lower_record(X86_FORM_VMOVD_X_RM32, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, BUSTER_TIER0_ROLE_INTEGER(0)),
            check_vector_lower_record(X86_FORM_VCVTPH2PS_X_XM64, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, BUSTER_TIER0_ROLE_FLOATING_RESULT),
        };
        check_lower_half_row(writer, records, 2, BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_F32);
    }
    else
    {
        const TypeId argument = BUSTER_TYPE_FLOAT;
        check_lower_half_move(writer, true);
        const uint32_t call = check_lower_runtime_call(writer, CHECK_LOWER_RUNTIME_EXTENDHFSF2, BUSTER_TYPE_FLOAT, &argument, 1, BUSTER_CHECK_NONE);
        check_lower_append(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F32, call);
    }
}

// The float on top of the stack rounded to `format` (U16), at the current rounding: a row of F16C's VCVTPS2PH, AVX512-
// BF16's VCVTNEPS2BF16 (which, as AVX-NE-CONVERT's, rounds to nearest, flushing subnormals, and raises nothing) or
// AVX-NE-CONVERT's, or a call of libgcc's, as LLVM selects them for the function's target: in a function with
// FENV_ACCESS (check_lower_half_strict), __truncsfbf2 for bfloat16 whatever the target, as LLVM rounds a constrained one.
static void check_lower_half_round(CheckLowerWriter* writer, CheckHalf format)
{
    const uint64_t features = writer->target.features;
    const uint64_t bf16 = BUSTER_X86_FEATURE(AVX512BF16) | BUSTER_X86_FEATURE(AVX512VL);
    const bool neconvert = (writer->target.extensions & BUSTER_X86_EXTENSION(AVXNECONVERT)) != 0;
    const bool strict = format == CHECK_HALF_BRAIN && check_lower_half_strict(writer);
    if (format == CHECK_HALF_IEEE && (features & BUSTER_X86_FEATURE(F16C)) != 0)
    {
        // The float through a scratch register and back, whose VMOVD zeroes the lanes past it, which VCVTPS2PH (whose rm is
        // its destination) would round too.
        X86Instruction records[4] = {
            check_vector_lower_record(X86_FORM_VMOVD_RM32_X, BUSTER_TIER0_ROLE_FLOATING(0), 0, BUSTER_TIER0_ROLE_SCRATCH(0)),
            check_vector_lower_record(X86_FORM_VMOVD_X_RM32, BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_SCRATCH(0)),
            check_vector_lower_record(X86_FORM_VCVTPS2PH_XM64_X_IMM8, BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_VECTOR(0)),
            check_vector_lower_record(X86_FORM_VMOVD_RM32_X, BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_INTEGER_RESULT),
        };
        records[2].immediate = 4;
        check_lower_half_row(writer, records, 4, BUSTER_TYPED_CLASS_F32, BUSTER_TYPED_CLASS_U16);
    }
    else if (format == CHECK_HALF_BRAIN && !strict && ((features & bf16) == bf16 || neconvert))
    {
        const X86Instruction records[2] = {
            check_vector_lower_record((features & bf16) == bf16 ? X86_FORM_VCVTNEPS2BF16_X_XM128_EVEX : X86_FORM_VCVTNEPS2BF16_X_XM128, BUSTER_TIER0_ROLE_VECTOR(0), 0,
                                      BUSTER_TIER0_ROLE_FLOATING(0)),
            check_vector_lower_record(X86_FORM_VMOVD_RM32_X, BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_INTEGER_RESULT),
        };
        check_lower_half_row(writer, records, 2, BUSTER_TYPED_CLASS_F32, BUSTER_TYPED_CLASS_U16);
    }
    else if (format == CHECK_HALF_BRAIN && !strict && (features & BUSTER_X86_FEATURE(AVX512BF16)) != 0)
    {
        // LLVM's would be the 64-byte form's, which no target of AVX-512BF16 needs: every one has AVX-512VL.
        check_lower_unsupported(writer, "conversions to __bf16 with AVX512-BF16 but not AVX-512VL");
    }
    else
    {
        const TypeId argument = BUSTER_TYPE_FLOAT;
        const CheckLowerRuntime runtime = format == CHECK_HALF_IEEE ? CHECK_LOWER_RUNTIME_TRUNCSFHF2 : CHECK_LOWER_RUNTIME_TRUNCSFBF2;
        const uint32_t call = check_lower_runtime_call(writer, runtime, BUSTER_TYPE_FLOAT, &argument, 1, BUSTER_CHECK_NONE);
        check_lower_append(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F32, call);
        check_lower_half_move(writer, false);
    }
}

// A 128-bit integer of class `wide` and binary16, as clang converts them: libgcc's __floattihf and __floatuntihf (`into`
// the half-precision type: the integer from a frame object of its own, by its address, its result's bits from the float's
// register they come back in), or __fixhfti and __fixunshfti (the half-precision value's bits in a float's register, its
// result from a frame object of its own), whose infinities and NaNs saturate as __fixsfti's don't.
static void check_lower_half_wide(CheckLowerWriter* writer, uint8_t wide, bool into)
{
    const bool is_signed = wide == BUSTER_TYPED_CLASS_I128;
    const TypeId integer = is_signed ? BUSTER_TYPE_INT128 : BUSTER_TYPE_UINT128;
    const TypeId floating = BUSTER_TYPE_FLOAT;
    const uint32_t local = check_lower_temporary(writer, wide);
    if (into)
    {
        const CheckLowerRuntime runtime = is_signed ? CHECK_LOWER_RUNTIME_FLOATTIHF : CHECK_LOWER_RUNTIME_FLOATUNTIHF;
        const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ? check_lower_runtime_call(writer, runtime, BUSTER_TYPE_FLOAT, &integer, 1, BUSTER_CHECK_NONE) : 0;
        check_lower_append(writer, BUSTER_TYPED_SET_LOCAL, wide, local);
        check_lower_append(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        check_lower_append(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F32, call);
        check_lower_half_move(writer, false);
    }
    else
    {
        const CheckLowerRuntime runtime = is_signed ? CHECK_LOWER_RUNTIME_FIXHFTI : CHECK_LOWER_RUNTIME_FIXUNSHFTI;
        const uint32_t call = writer->output->status == CHECK_PROGRAM_OK ? check_lower_runtime_call(writer, runtime, integer, &floating, 1, local) : 0;
        check_lower_append(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_half_move(writer, true);
        check_lower_append(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
        check_lower_append(writer, BUSTER_TYPED_LOCAL, wide, local);
    }
}

// The value on top of the stack, of class `from` (F64 or F80), rounded to `format` once: libgcc's __truncdfhf2 ...
// __truncxfbf2, as clang calls them, the result's bits from the float's register they come back in.
static void check_lower_half_truncate(CheckLowerWriter* writer, uint8_t from, CheckHalf format)
{
    const bool x87 = from == BUSTER_TYPED_CLASS_F80;
    const TypeId argument = x87 ? BUSTER_TYPE_LONG_DOUBLE : BUSTER_TYPE_DOUBLE;
    const CheckLowerRuntime runtime = format == CHECK_HALF_IEEE ? x87 ? CHECK_LOWER_RUNTIME_TRUNCXFHF2 : CHECK_LOWER_RUNTIME_TRUNCDFHF2 :
                                                                       x87 ? CHECK_LOWER_RUNTIME_TRUNCXFBF2 : CHECK_LOWER_RUNTIME_TRUNCDFBF2;
    const uint32_t call = check_lower_runtime_call(writer, runtime, BUSTER_TYPE_FLOAT, &argument, 1, BUSTER_CHECK_NONE);
    check_lower_append(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F32, call);
    check_lower_half_move(writer, false);
}

// Whether the value on top of the stack, of `class` (I8 to U64), is a constant that clang's -O0 code converts to `format`
// as it compiles: the entry before is its CONST, which no branch joins (a conditional's value is clang's at run time too),
// in a function without FENV_ACCESS (check_lower_half_strict); its bits in `bits`, the value rounded once
// (check_action_half_round), as clang rounds it, where a conversion at run time rounds it to float first. A bfloat16's
// two roundings can differ from one; a binary16's can't (every integer past float's 24 bits overflows it).
static bool check_lower_half_constant(CheckLowerWriter* writer, uint8_t class, CheckHalf format, uint16_t* bits)
{
    const uint32_t count = writer->count;
    bool constant = count != 0 && writer->kinds[count - 1] == BUSTER_TYPED_CONST && writer->classes[count - 1] == class && !check_lower_half_strict(writer);
    for (uint32_t label = 0; constant && label < writer->label_count; label += 1)
    {
        constant = writer->labels[label] != count;
    }
    if (constant)
    {
        // A CONST's value is its class's bits, sign-extended for a signed class (check_lower_constant's).
        const uint64_t value = writer->values[count - 1];
        const bool negative = (class & 1) != 0 && (int64_t)value < 0;
        *bits = check_action_half_round(format, negative, negative ? 0 - value : value, 0);
    }
    return constant;
}

// A conversion of the value on top of the stack from `from` to `to`, one of them half-precision (check_lower_scalar_
// conversion's): to void, a DISCARD; between two formats, through float (one of them's the same format: nothing); from
// one, its float's conversion; to one, a double's and a long double's rounding, an integer constant's rounding as clang
// folds it (check_lower_half_constant: its CONST discarded for the bits'), and any other real value's through its float. A
// complex value, a vector or an aggregate is refused by name.
static void check_lower_half_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const CheckHalf source = check_lower_half(types, from);
    const CheckHalf target = check_lower_half(types, to);
    const uint8_t from_class = check_lower_value_class(writer, from);
    const uint8_t to_class = check_lower_value_class(writer, to);
    const bool to_bool = (check_lower_facts(writer, to) & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0;
    const uint8_t other = source != CHECK_HALF_NONE ? to_class : from_class;
    const TypeId other_type = source != CHECK_HALF_NONE ? to : from;
    uint16_t bits = 0;
    if ((source == CHECK_HALF_NONE || target == CHECK_HALF_NONE) && (other >= BUSTER_TYPED_CLASS_VECTOR || check_lower_aggregate(writer, other_type) ||
                                                                               check_lower_complex(writer, other_type)))
    {
        check_lower_unsupported(writer, other >= BUSTER_TYPED_CLASSES ? check_lower_type_feature(types, other_type) :
                                        "conversions between half-precision values and vectors, aggregates or complex values");
    }
    else if (source != CHECK_HALF_NONE && to_class == BUSTER_TYPED_CLASS_VOID)
    {
        check_lower_append(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U16, 0);
    }
    else if (source != CHECK_HALF_NONE && target != CHECK_HALF_NONE)
    {
        if (source != target)
        {
            check_lower_half_extend(writer, source);
            check_lower_half_round(writer, target);
        }
    }
    else if (source == CHECK_HALF_IEEE && check_lower_wide_class(to_class))
    {
        check_lower_half_wide(writer, to_class, false);
    }
    else if (source != CHECK_HALF_NONE)
    {
        check_lower_half_extend(writer, source);
        if (to_bool)
        {
            // NaN is true and -0.0 false, as a float's.
            check_lower_append(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F32, 0);
            check_lower_append(writer, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_append(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
        }
        else if (check_lower_wide_class(to_class))
        {
            check_lower_wide_from_floating(writer, BUSTER_TYPED_CLASS_F32, to_class);
        }
        else if (to_class != BUSTER_TYPED_CLASS_F32)
        {
            check_lower_append(writer, BUSTER_TYPED_CONVERT, to_class, 0);
        }
    }
    else if (from_class == BUSTER_TYPED_CLASS_F64 || from_class == BUSTER_TYPED_CLASS_F80)
    {
        check_lower_half_truncate(writer, from_class, target);
    }
    else if (target == CHECK_HALF_IEEE && check_lower_wide_class(from_class))
    {
        check_lower_half_wide(writer, from_class, true);
    }
    else if (from_class >= BUSTER_TYPED_CLASS_I8 && from_class <= BUSTER_TYPED_CLASS_U64 && check_lower_half_constant(writer, from_class, target, &bits))
    {
        check_lower_append(writer, BUSTER_TYPED_DISCARD, from_class, 0);
        check_lower_append(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U16, bits);
    }
    else
    {
        if (check_lower_wide_class(from_class))
        {
            check_lower_wide_to_floating(writer, from_class, BUSTER_TYPED_CLASS_F32);
        }
        else if (from_class != BUSTER_TYPED_CLASS_F32)
        {
            check_lower_append(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F32, 0);
        }
        check_lower_half_round(writer, target);
    }
}

// An entry of a half-precision class (check_lower_emit's, through check_lower_emit_classless), written as Tier 0's. One
// that moves a value moves its bits, a U16's: a local of one is a frame object of its 2 bytes (check_lower_temporary's),
// which LOCAL, STORE_LOCAL and SET_LOCAL of U16 read and write at its first bytes. A call's result comes back in xmm0's
// low 16 bits and a return's goes there, and a CONVERT moves the bits into a float's register, a call's argument's
// (check_lower_call_halves). A compare of the class (check_lower_count's) compares its operands' floats: the right one
// set into the function's U16 temporary (CheckLowerWriter.half_locals' third), which leaves the left one the next entry's
// operand, as a value under a store of its class is; the left one extended; the right one read back and extended.
// Any other entry would compute with the value's bits, which only this file's recipes do, in float: refused.
static void check_lower_half_entry(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    const bool move = kind == BUSTER_TYPED_CONST || kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_GLOBAL || kind == BUSTER_TYPED_DUP ||
                      kind == BUSTER_TYPED_LOAD || (kind >= BUSTER_TYPED_STORE && kind <= BUSTER_TYPED_SET_GLOBAL) || kind == BUSTER_TYPED_DISCARD ||
                      kind == BUSTER_TYPED_JUMP_VALUE;
    if (move)
    {
        check_lower_append(writer, kind, BUSTER_TYPED_CLASS_U16, value);
    }
    else if (kind == BUSTER_TYPED_CALL)
    {
        check_lower_append(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F32, value);
        check_lower_half_move(writer, false);
    }
    else if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)
    {
        const uint32_t right = check_lower_half_temporary(writer, 2, BUSTER_TYPED_CLASS_U16);
        check_lower_append(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U16, right);
        check_lower_half_extend(writer, check_lower_class_half(class));
        check_lower_append(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U16, right);
        check_lower_half_extend(writer, check_lower_class_half(class));
        check_lower_append(writer, kind, BUSTER_TYPED_CLASS_I32, value);
    }
    else if (kind == BUSTER_TYPED_RETURN || kind == BUSTER_TYPED_CONVERT)
    {
        check_lower_append(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_half_move(writer, true);
        if (kind == BUSTER_TYPED_RETURN)
        {
            check_lower_append(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F32, value);
        }
    }
    else
    {
        check_lower_unsupported(writer, "this operation on half-precision values");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Expressions, as tasks (check_lower_expand's): pushed in reverse, as the lowering's stack runs them.

// Whether node `node` continues a chain of `format`'s arithmetic (clang's EmitPromoted): +, -, *, / or a unary + or -,
// of a type of the format.
static bool check_lower_half_link(const CheckLowerWriter* writer, uint32_t node, CheckHalf format)
{
    const CheckProgram* program = writer->program;
    const CheckOp op = node < program->stream->count ? program->input->graph.ops[node] : CHECK_OP_VALUE;
    const bool arithmetic = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE || op == CHECK_OP_NEGATIVE ||
                            op == CHECK_OP_POSITIVE;
    return arithmetic && check_lower_half(program->types, program->stream->values[node].type) == format;
}

// Operand `operand` of node `node` (a chain's link, or an update's right operand) onto the stack as a float, as tasks: in a chain under
// excess precision (`chained`), a link's own computation, unrounded (CHECK_LOWER_HALF); any other operand's value
// converted to `type`, its type in the operation (a half-precision type, which rounds it), then extended, or for a
// constant, the float of its bits, folded.
static void check_lower_half_operand(CheckLowerWriter* writer, uint32_t node, uint32_t operand, TypeId type, bool chained)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckHalf format = check_lower_half(program->types, type);
    const uint32_t child = operand == 0 ? graph->operand0[node] : graph->operand1[node];
    uint16_t bits = 0;
    if (chained && check_lower_half_link(writer, child, format) && program->stream->values[child].type == type &&
        check_program_overload_address(program, node, operand) == NULL)
    {
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_HALF, .node = child });
    }
    else if (child < program->stream->count && check_program_overload_address(program, node, operand) == NULL && check_lower_half_fold(writer, child, type, &bits))
    {
        const float value = (float)check_action_half_value(format, bits);
        uint32_t word = 0;
        memcpy(&word, &value, sizeof(word));
        check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F32, word);
    }
    else
    {
        check_lower_convert(writer, type, BUSTER_TYPE_FLOAT);
        check_lower_operand(writer, node, operand, false, type);
    }
}

// Chain link `node` of `format` computed in float, without its own rounding, as tasks: its operator's F32 entry over its
// operands' floats (check_lower_half_operand), whose links continue the chain where `chained`.
static void check_lower_half_chain(CheckLowerWriter* writer, uint32_t node, bool chained)
{
    const CheckProgram* program = writer->program;
    const CheckOp op = program->input->graph.ops[node];
    const CheckNodeConversions* conversion = program->stream->conversions + node;
    if (op == CHECK_OP_NEGATIVE)
    {
        check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_F32, 0);
        check_lower_half_operand(writer, node, 0, conversion->operands[0].type, chained);
    }
    else if (op == CHECK_OP_POSITIVE)
    {
        check_lower_half_operand(writer, node, 0, program->stream->values[node].type, chained);
    }
    else
    {
        check_lower_instruction(writer, check_lower_binary(op), BUSTER_TYPED_CLASS_F32, 0);
        check_lower_half_operand(writer, node, 1, conversion->operands[1].type, chained);
        check_lower_half_operand(writer, node, 0, conversion->operands[0].type, chained);
    }
}

// Expression `node`, half-precision arithmetic (+, -, *, / or a unary - of a half-precision type, which
// check_lower_vector_operation takes from check_lower_expand), its type's format `format`: its chain's value
// (check_lower_half_chain) rounded once, or with native arithmetic its own operation's (a negation flipping the sign
// bit, as LLVM's does).
static void check_lower_half_expression(CheckLowerWriter* writer, uint32_t node, CheckHalf format)
{
    const CheckProgram* program = writer->program;
    const CheckOp op = program->input->graph.ops[node];
    const TypeId type = program->stream->values[node].type;
    const CheckNodeConversions* conversion = program->stream->conversions + node;
    if (!check_lower_half_excess(writer, format) && format == CHECK_HALF_BRAIN)
    {
        check_lower_unsupported(writer, "__bf16 arithmetic where the unit's target has AVX10.2");
    }
    else if (!check_lower_half_excess(writer, format) && op == CHECK_OP_NEGATIVE)
    {
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U16, 0);
        check_lower_instruction(writer, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 0x8000);
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
        check_lower_operand(writer, node, 0, false, conversion->operands[0].type);
    }
    else
    {
        // A chain of unary + and - alone, over an operand that isn't a link, is its operand's sign flipped as often as it
        // has -: LLVM folds the float's round trip away (an IEEE negation's, which keeps a signaling NaN's bits). A
        // bfloat16's negation it doesn't fold: one of an odd count of - extends, negates and rounds, which flushes a
        // subnormal where the rounding is VCVTNEPS2BF16's (an even count folds away). In a function with FENV_ACCESS
        // (check_lower_half_strict) it folds neither, the round trip being constrained intrinsics': the chain extends and
        // rounds, which quiets a signaling NaN and raises FE_INVALID. `link` walks down to the last of them.
        const CheckGraph* graph = &program->input->graph;
        const bool unary = (op == CHECK_OP_NEGATIVE || op == CHECK_OP_POSITIVE) && !check_lower_half_strict(writer);
        uint32_t link = node;
        uint32_t negations = 0;
        bool walking = unary;
        while (walking)
        {
            const uint32_t child = graph->operand0[link];
            negations += graph->ops[link] == CHECK_OP_NEGATIVE ? 1 : 0;
            walking = check_lower_half_link(writer, child, format) && program->stream->values[child].type == program->stream->values[link].type &&
                      (graph->ops[child] == CHECK_OP_NEGATIVE || graph->ops[child] == CHECK_OP_POSITIVE);
            link = walking ? child : link;
        }
        const uint32_t operand = graph->operand0[link];
        const bool sign = unary && !(check_lower_half_link(writer, operand, format) && program->stream->values[operand].type == program->stream->values[link].type) &&
                          (format == CHECK_HALF_IEEE || negations % 2 == 0);
        if (sign)
        {
            if (negations % 2 != 0)
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U16, 0);
                check_lower_instruction(writer, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U32, 0);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 0x8000);
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
            }
            check_lower_operand(writer, link, 0, false, program->stream->values[link].type);
        }
        else
        {
            check_lower_convert(writer, BUSTER_TYPE_FLOAT, type);
            check_lower_half_chain(writer, node, check_lower_half_excess(writer, format));
        }
    }
}

// A half-precision value's truth (check_lower_truth's): its float compared with +0.0, so that NaN is true and -0.0
// false.
static void check_lower_half_truth(CheckLowerWriter* writer, TypeId type, bool negate)
{
    check_lower_instruction(writer, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F32, 0);
    check_lower_convert(writer, type, BUSTER_TYPE_FLOAT);
}

// A CHECK_LOWER_HALF task's: chain link `node` computed in float, its entries the link's token's for -g's rows, as an
// expression's are (check_lower_task_source).
static void check_lower_half_task(CheckLowerWriter* writer, CheckLowerTask task)
{
    check_lower_source(writer, writer->program->input->graph.tokens[task.node]);
    check_lower_half_chain(writer, task.node, true);
}
