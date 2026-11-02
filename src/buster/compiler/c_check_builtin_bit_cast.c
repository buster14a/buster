// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's C __builtin_bit_cast action. Include after c_types.c, c_builtin_catalog.c and c_check_actions.c.
// The parser supplies the destination type and ONE raw operand: no decay, lvalue conversion, atomic load or
// half promotion precedes this action. Complete, equally sized, trivially copyable C types pass independently
// of the bounded constant representation supported here. Operand arrays retain their raw type and category.
//
// source_proof describes the entire source bit-cast expression, not local operand flags. CONSTANT certifies
// constant evaluation; ICE additionally certifies C integer-constant-expression provenance. Exact source_bytes
// are independently known object-representation bytes in Linux/x86-64 little-endian order, including every
// byte of storage. Their presence alone never proves a source constant, initialized padding or a valid value.
// NULL/0 omits this certificate; only ordinary full-width integer CheckValues can then supply exact bits.
// A supplied nonempty slice must match the source type's exact static storage size. No program pointer is read.
//
// Raw HALF/FLOAT16/F32/F64 result bits are retained without touching CheckValue.floating. This includes sNaNs:
// even copying an x87 floating lvalue could raise or quiet one. A representation-aware consumer can emit the
// exact raw constant; CHECK_VALUE_PAYLOAD explicitly says ordinary CheckValue lacks its FP representation.
// Other formats, pointers, aggregates, padding and invalid bool encodings remain explicit requirements.
// Qualified array spellings can have distinct Clang result qualifiers but one canonical TypeId. RESULT_TYPE
// reports this lost distinction with action.value.type=0; the valid representation_type remains available.
// runtime_required means operand work/bit reinterpretation remains; evaluated_operands is always one after
// successful type validation, even for a folded constant. The caller retains actual effects and faults.
//
// All slices have ordinary C alignment, exact lengths and no readable padding, are disjoint from outputs and
// mutable TypeTable storage. Batches allocate nothing, preserve order and permit null slices at count zero.
// This module grants no source/lowering capability. Rules audited from exact primary sources, not imported:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaCast.cpp
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/AST/ExprConstant.cpp
#ifndef BUSTER_C_CHECK_BUILTIN_BIT_CAST_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_BIT_CAST_INCLUDED

#define BUSTER_CHECK_BUILTIN_BIT_CAST_BYTES 16u
#define BUSTER_CHECK_BUILTIN_BIT_CAST_SIZE 0x01u
#define BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION 0x02u
#define BUSTER_CHECK_BUILTIN_BIT_CAST_CHECK_VALUE_PAYLOAD 0x04u
#define BUSTER_CHECK_BUILTIN_BIT_CAST_INVALID_REPRESENTATION 0x08u
#define BUSTER_CHECK_BUILTIN_BIT_CAST_RESULT_TYPE 0x10u

typedef enum CheckBuiltinBitCastProof
{
    CHECK_BUILTIN_BIT_CAST_PROOF_NONE,
    CHECK_BUILTIN_BIT_CAST_PROOF_CONSTANT,
    CHECK_BUILTIN_BIT_CAST_PROOF_ICE,
} CheckBuiltinBitCastProof;

typedef struct CheckBuiltinBitCastAction CheckBuiltinBitCastAction;
struct CheckBuiltinBitCastAction
{
    CBuiltinId id;
    TypeId destination_type;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint32_t node;
    CheckContext context;
    CheckBuiltinBitCastProof source_proof;
    const uint8_t* source_bytes;
    uint32_t source_byte_count;
};

typedef struct CheckBuiltinBitCastResult CheckBuiltinBitCastResult;
struct CheckBuiltinBitCastResult
{
    // diagnostic_operand 0 denotes the value operand, 1 the separate destination type.
    CheckActionResult action;
    TypeId source_type;
    // Sema's destination for constraints; records/arrays retain CV, other destinations drop top atomic/CV.
    TypeId representation_type;
    uint64_t byte_count;
    uint32_t evaluated_operands;
    uint32_t unsupported;
    CBuiltinId runtime_builtin;
    uint8_t bytes[BUSTER_CHECK_BUILTIN_BIT_CAST_BYTES];
    uint32_t payload_count;
    bool representation_known;
    bool folded;
    bool runtime_required;
    bool volatile_read;
};

static bool check_builtin_bit_cast_complete(const TypeTable* table, TypeId type, const CheckBuiltinBitCastAction* input, bool* variable, bool* trivial)
{
    bool complete = true;
    bool more = true;
    *variable = false;
    *trivial = true;
    for (uint32_t depth = 0; complete && more && depth < table->types.count; depth += 1)
    {
        type = type_strip_aligned(table, type);
        const uint8_t kind = type_kind(table, type);
        // Functions are not incomplete in Clang; they fail the later trivially-copyable constraint.
        complete = kind == BUSTER_TYPE_KIND_FUNCTION || type_is_complete(table, type);
        complete = complete && check_context_tag_visible(table, type, &input->context);
        *variable |= kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
        more = type_kind_is_array(kind);
        *trivial &= kind != BUSTER_TYPE_KIND_ATOMIC && kind != BUSTER_TYPE_KIND_FUNCTION && kind != BUSTER_TYPE_KIND_VOID;
        if (more)
        {
            type = type_element(table, type);
        }
    }
    complete &= !more;
    return complete;
}

static bool check_builtin_bit_cast_integer(const TypeTable* table, TypeId type, uint32_t* width)
{
    bool is_signed = false;
    const bool integer = check_action_integer_type(table, type, width, &is_signed);
    const uint64_t bytes = type_size(table, type);
    // _Bool has a one-bit value and an eight-bit object representation. Other padding is not modeled.
    const bool result = integer && (type_unqualified(type_strip_aligned(table, type)) == BUSTER_TYPE_BOOL ||
        (*width == bytes * 8 && bytes <= BUSTER_CHECK_BUILTIN_BIT_CAST_BYTES));
    return result;
}

static bool check_builtin_bit_cast_float(TypeId type)
{
    const bool result = type == BUSTER_TYPE_HALF || type == BUSTER_TYPE_FLOAT16 || type == BUSTER_TYPE_FLOAT || type == BUSTER_TYPE_DOUBLE;
    return result;
}

static __attribute__((unused)) void check_builtin_bit_cast_one(const TypeTable* table, const CheckBuiltinBitCastAction* input, CheckBuiltinBitCastResult* output)
{
    memset(output, 0, sizeof(*output));
    if (input->id != CBUILTIN_BIT_CAST)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != 1)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (input->arguments == NULL || !type_is_valid(table, input->destination_type) ||
        (unsigned)input->source_proof > CHECK_BUILTIN_BIT_CAST_PROOF_ICE ||
        (input->source_bytes == NULL && input->source_byte_count != 0) ||
        (input->context.tag_completions == NULL && input->context.tag_completion_count != 0))
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        // Read only non-FP fields. In particular, do not copy an input CheckValue containing an sNaN.
        const CheckValue* source = input->arguments;
        if (!type_is_valid(table, source->type) || (unsigned)source->category > CHECK_FUNCTION ||
            (unsigned)source->constant_kind > CHECK_CONSTANT_COMPLEX || (unsigned)source->fault > CHECK_FAULT_UNSUPPORTED_CONSTANT)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
        }
        else
        {
            const TypeId result_type = type_unqualified(input->destination_type);
            TypeId destination = type_strip_aligned(table, input->destination_type);
            const uint8_t destination_kind = type_kind(table, destination);
            if (destination_kind != BUSTER_TYPE_KIND_STRUCT && destination_kind != BUSTER_TYPE_KIND_UNION && !type_kind_is_array(destination_kind))
            {
                destination = type_unqualified(destination);
                if (destination_kind == BUSTER_TYPE_KIND_ATOMIC)
                {
                    destination = type_element(table, destination);
                }
            }
            bool source_variable = false;
            bool destination_variable = false;
            bool source_trivial = false;
            bool destination_trivial = false;
            const bool destination_complete = check_builtin_bit_cast_complete(table, destination, input, &destination_variable, &destination_trivial);
            const bool source_complete = check_builtin_bit_cast_complete(table, source->type, input, &source_variable, &source_trivial);
            const bool variable = source_variable || destination_variable;
            const uint64_t bytes = type_size(table, source->type);
            if (!destination_complete || !source_complete)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, !destination_complete ? 1 : 0);
            }
            else if (type_size(table, destination) != bytes)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
            }
            else if (!destination_trivial || !source_trivial)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, !destination_trivial ? 1 : 0);
            }
            else if (input->source_bytes != NULL && (variable || input->source_byte_count != bytes))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
            }
            else
            {
                output->source_type = source->type;
                output->representation_type = destination;
                output->byte_count = variable ? 0 : bytes;
                output->evaluated_operands = 1;
                output->runtime_builtin = input->id;
                output->runtime_required = true;
                output->volatile_read = source->category == CHECK_LVALUE && (type_qualifiers(type_strip_aligned(table, source->type)) & BUSTER_TYPE_VOLATILE) != 0;
                output->action.value.type = result_type;
                output->action.value.category = CHECK_RVALUE;
                output->action.value.flags = (source->flags & BUSTER_CHECK_SIDE_EFFECT) | (output->volatile_read ? BUSTER_CHECK_SIDE_EFFECT : 0);
                output->action.value.fault = source->fault;
                output->action.value.fault_node = source->fault_node;
                uint32_t source_width = 0;
                uint32_t destination_width = 0;
                const bool source_integer = check_builtin_bit_cast_integer(table, source->type, &source_width);
                const bool destination_integer = check_builtin_bit_cast_integer(table, destination, &destination_width);
                const bool source_float = check_builtin_bit_cast_float(type_unqualified(type_strip_aligned(table, source->type)));
                const bool destination_float = check_builtin_bit_cast_float(destination);
                const bool supported = !variable && bytes > 0 && bytes <= BUSTER_CHECK_BUILTIN_BIT_CAST_BYTES &&
                    (source_integer || source_float) && (destination_integer || destination_float) &&
                    type_kind(table, type_strip_aligned(table, result_type)) != BUSTER_TYPE_KIND_ATOMIC &&
                    (source->flags & BUSTER_CHECK_BIT_FIELD) == 0;
                if (input->source_proof == CHECK_BUILTIN_BIT_CAST_PROOF_ICE && (!check_action_is_integer(table, source->type) || !check_action_is_integer(table, destination) ||
                    type_kind(table, type_strip_aligned(table, result_type)) == BUSTER_TYPE_KIND_ATOMIC))
                {
                    check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
                }
                else if (variable)
                {
                    output->unsupported = BUSTER_CHECK_BUILTIN_BIT_CAST_SIZE;
                }
                else if (!supported)
                {
                    output->unsupported = BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION;
                }
                else
                {
                    unsigned __int128 bits = 0;
                    bool known = input->source_bytes != NULL;
                    if (known)
                    {
                        for (uint32_t i = 0; i < (uint32_t)bytes; i += 1)
                        {
                            bits |= (unsigned __int128)input->source_bytes[i] << (i * 8);
                        }
                    }
                    else if (source_integer && source->constant_kind == CHECK_CONSTANT_INTEGER)
                    {
                        const bool source_bool = type_unqualified(type_strip_aligned(table, source->type)) == BUSTER_TYPE_BOOL;
                        bits = source->integer & check_action_mask(source_bool ? 8 : source_width);
                        known = true;
                    }
                    const bool source_bool = type_unqualified(type_strip_aligned(table, source->type)) == BUSTER_TYPE_BOOL;
                    const bool destination_bool = destination == BUSTER_TYPE_BOOL;
                    if (known && (source_bool || destination_bool) && bits > 1)
                    {
                        output->unsupported = BUSTER_CHECK_BUILTIN_BIT_CAST_INVALID_REPRESENTATION;
                    }
                    else if (known)
                    {
                        output->representation_known = true;
                        output->payload_count = (uint32_t)bytes;
                        for (uint32_t i = 0; i < output->payload_count; i += 1)
                        {
                            output->bytes[i] = (uint8_t)(bits >> (i * 8));
                        }
                        const bool clean = output->action.value.flags == 0 && source->fault == CHECK_FAULT_NONE &&
                            (type_qualifiers(type_strip_aligned(table, source->type)) & BUSTER_TYPE_VOLATILE) == 0;
                        output->folded = input->source_proof != CHECK_BUILTIN_BIT_CAST_PROOF_NONE && clean;
                        output->runtime_required = !output->folded;
                        output->runtime_builtin = output->folded ? CBUILTIN_NONE : input->id;
                        if (output->folded && destination_integer)
                        {
                            output->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
                            output->action.value.integer = bits & check_action_mask(destination_width);
                            output->action.value.flags = input->source_proof == CHECK_BUILTIN_BIT_CAST_PROOF_ICE ? BUSTER_CHECK_ICE : 0;
                        }
                        if (destination_float)
                        {
                            output->unsupported |= BUSTER_CHECK_BUILTIN_BIT_CAST_CHECK_VALUE_PAYLOAD;
                        }
                    }
                    else
                    {
                        output->unsupported = BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION;
                    }
                }
                if (output->action.diagnostic == CHECK_DIAG_NONE && type_kind_is_array(destination_kind) && type_qualifiers(destination) != 0)
                {
                    output->action.value.type = 0;
                    output->unsupported |= BUSTER_CHECK_BUILTIN_BIT_CAST_RESULT_TYPE;
                }
            }
        }
    }
}

static __attribute__((unused)) void check_builtin_bit_cast(const TypeTable* table, const CheckBuiltinBitCastAction* actions, CheckBuiltinBitCastResult* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_bit_cast_one(table, actions + i, results + i);
    }
}

#endif
