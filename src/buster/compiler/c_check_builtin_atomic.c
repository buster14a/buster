// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original typed GNU atomic actions (#148). Include after c_builtin_catalog.c and c_check_actions.c.
// This bounded slice checks load_n, store_n, synchronize, thread/signal fences and the GNU lock-free queries
// (__atomic_always_lock_free and __atomic_is_lock_free, constant where clang's evaluator answers them). Other identities
// are explicitly unsupported. The producer owns argument graph identities, evaluation order and address provenance. Every
// listed argument is evaluated exactly once, including dynamic memory orders, but for a query clang folds, which
// evaluates none; no action reads the source program's memory.
//
// alignment_known supplies the addressed object's alignment as clang's code generation gives its address (c_checker.c's
// check_graph_atomic_alignment): a pointer value's pointee type's, and a packed member's, a declaration's or an element's
// from an address's path. Unknown or insufficient alignment, for which clang calls libatomic, leaves valid source
// semantics intact and marks the native-helper path unsupported.
// The helper path requires naturally aligned 1/2/4/8-byte objects, preserves volatile accesses and must use true
// atomic helpers through unknown-effect calls (or atomic typed operations), never optimizer-visible ordinary loads.
//
// Actions/results and each arguments slice are ordinarily aligned, bounded and disjoint from output/type storage;
// no readable padding is required. Empty batches allow null slices. Batches preserve input order while interning
// decay types. There is no retained work, and legal batch partitions produce identical semantic results. These
// actions establish no catalog SOURCE/CONSTEXPR capability or runtime-consumer acceptance by themselves.
#ifndef BUSTER_C_CHECK_BUILTIN_ATOMIC_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_ATOMIC_INCLUDED

#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER 0x01u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ALIGNMENT 0x02u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_NULL 0x04u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS 0x08u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN 0x10u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_FUNCTION_POINTER 0x20u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ID 0x01u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH 0x02u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ALIGNMENT 0x04u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_UNKNOWN_ALIGNMENT 0x08u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION 0x10u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_FLOAT_FORMAT 0x20u
#define BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER 0x01u

typedef enum CheckBuiltinAtomicFenceScope
{
    CHECK_BUILTIN_ATOMIC_FENCE_NONE,
    CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM,
    CHECK_BUILTIN_ATOMIC_FENCE_SIGNAL,
} CheckBuiltinAtomicFenceScope;

typedef struct CheckBuiltinAtomicAction CheckBuiltinAtomicAction;
struct CheckBuiltinAtomicAction
{
    CBuiltinId id;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint32_t node;
    const uint32_t* tag_completions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
    uint64_t minimum_alignment;
    bool alignment_known;
    // A C11 operation on an object that isn't _Atomic: clang's __hip_atomic_* (check_builtin_atomic_family).
    bool plain_object;
};

typedef struct CheckBuiltinAtomicResult CheckBuiltinAtomicResult;
struct CheckBuiltinAtomicResult
{
    CheckActionResult action;
    // A scoped compare-exchange's seven arguments' (the scope's last).
    CheckConversion argument_conversions[7];
    // Converted pointer/value metadata, never an invented load result or an already executed store.
    CheckValue object_address;
    CheckValue store_value;
    TypeId object_type;
    TypeId value_type;
    uint64_t object_size;
    uint64_t required_alignment;
    uint64_t minimum_alignment;
    uint32_t evaluated_operands;
    uint32_t success_order;
    uint32_t failure_order;
    uint32_t known_orders;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t unsupported;
    CBuiltinId runtime_builtin;
    CheckBuiltinAtomicFenceScope fence_scope;
    bool alignment_known;
    bool volatile_access;
    bool reads_memory;
    bool writes_memory;
    bool rmw;
    // A conservative compiler ordering boundary is permitted for every supported request. Consumers may
    // strengthen valid orderings to sequential consistency while retaining all argument evaluations.
    bool compiler_barrier;
};

static bool check_builtin_atomic_source_complete(const TypeTable* table, TypeId type, const CheckBuiltinAtomicAction* input)
{
    const TypeId base = type_strip_aligned(table, type);
    bool complete = type_is_complete(table, base);
    const uint8_t kind = type_kind(table, base);
    if (complete && input->tag_completions != NULL &&
        (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM))
    {
        const uint32_t tag = table->types.index[type_index(base)];
        complete = tag < input->tag_completion_count && input->tag_completions[tag] <= input->source_position;
    }
    return complete;
}

static CheckValue check_builtin_atomic_argument(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output, uint32_t index)
{
    CheckValue value = input->arguments[index];
    CheckActionResult converted = { 0 };
    if (!type_is_valid(table, value.type) || value.category > CHECK_FUNCTION)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, index);
    }
    else
    {
        const TypeId base = type_strip_aligned(table, value.type);
        const uint8_t kind = type_kind(table, base);
        if ((value.category == CHECK_LVALUE || kind == BUSTER_TYPE_KIND_ENUM) && !type_kind_is_array(kind) &&
            !check_builtin_atomic_source_complete(table, base, input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, index);
        }
        else
        {
            const CheckAction action = {
                .operands = { value },
                .node = input->node,
                .tag_completions = input->tag_completions,
                .tag_completion_count = input->tag_completion_count,
                .source_position = input->source_position,
                .extensions = input->extensions,
            };
            value = check_action_operand(table, &action, &converted, 0);
            if (converted.diagnostic != CHECK_DIAG_NONE)
            {
                check_action_diagnose(&output->action, converted.diagnostic, index);
            }
        }
    }
    output->argument_conversions[index] = converted.conversions[0];
    return value;
}

static bool check_builtin_atomic_pointer_comparison_visible(const TypeTable* table, TypeId left, TypeId right, const CheckBuiltinAtomicAction* input, CheckActionResult* result, uint32_t operand)
{
    // Same identities need no enum definition. A cross-type comparison must not discover a future definition
    // through type_compatible, including inside pointer chains, array elements or function signatures. Walk the
    // same bounded inner-pair protocol as compatibility; enum visibility is checked before its shallow query.
    bool visible = true;
    if (input->tag_completions != NULL && left != right)
    {
        const Scratch scratch = scratch_begin(0);
        void* stack = NULL;
        uint64_t capacity = 0;
        uint64_t depth = 0;
        TypePairMemo met = {
            .arena = scratch.arena,
        };
        bool fits = type_grow_stack(scratch.arena, &stack, &capacity, 1, sizeof(TypePair));
        if (fits)
        {
            ((TypePair*)stack)[0] = (TypePair){ .left = left, .right = right };
            depth = 1;
        }
        while (visible && fits && depth != 0)
        {
            depth -= 1;
            const TypePair pair = ((TypePair*)stack)[depth];
            const TypeId l = type_strip_aligned(table, pair.left);
            const TypeId r = type_strip_aligned(table, pair.right);
            const uint8_t left_kind = type_kind(table, l);
            const uint8_t right_kind = type_kind(table, r);
            visible = l == r || ((left_kind != BUSTER_TYPE_KIND_ENUM || check_builtin_atomic_source_complete(table, l, input)) &&
                                 (right_kind != BUSTER_TYPE_KIND_ENUM || check_builtin_atomic_source_complete(table, r, input)));
            if (visible && l != r && left_kind == BUSTER_TYPE_KIND_FUNCTION && right_kind == BUSTER_TYPE_KIND_FUNCTION &&
                ((table->types.flags[type_index(l)] ^ table->types.flags[type_index(r)]) & BUSTER_TYPE_FLAG_PROTOTYPE) != 0)
            {
                // The prototype-versus-old-style shallow rule examines enum parameter promotions itself.
                const TypeId prototype = (table->types.flags[type_index(l)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ? l : r;
                uint32_t count = 0;
                const TypeId* parameters = type_parameters(table, prototype, &count);
                for (uint32_t i = 0; visible && i < count; i += 1)
                {
                    const TypeId parameter = type_unqualified(type_strip_aligned(table, parameters[i]));
                    visible = type_kind(table, parameter) != BUSTER_TYPE_KIND_ENUM || check_builtin_atomic_source_complete(table, parameter, input);
                }
            }
            bool leaf = true;
            if (visible && type_merge_shallow(table, pair.left, pair.right, &leaf) && !leaf)
            {
                fits = type_grow_stack(scratch.arena, &stack, &capacity, depth + type_merge_inner_count(table, pair.left), sizeof(TypePair));
                if (fits)
                {
                    TypePair* pairs = (TypePair*)stack + depth;
                    const uint32_t kept = type_pairs_unmet(&met, pairs, type_merge_inner(table, pair.left, pair.right, pairs));
                    fits = kept != UINT32_MAX;
                    depth += fits ? kept : 0;
                }
            }
        }
        if (!fits)
        {
            check_action_diagnose(result, CHECK_DIAG_RESOURCE, operand);
            visible = false;
        }
        scratch_end(scratch);
    }
    return visible;
}

static TypeId check_builtin_atomic_pointer_sign_type(const TypeTable* table, TypeId type, const CheckBuiltinAtomicAction* input)
{
    // Corresponding identities, not storage width: long and long long remain distinct on LP64.
    TypeId result = type;
    if (type_kind(table, type) == BUSTER_TYPE_KIND_ENUM && check_builtin_atomic_source_complete(table, type, input))
    {
        uint32_t width = 0;
        bool is_signed = false;
        if (check_action_integer_type(table, type, &width, &is_signed) && is_signed)
        {
            // Clang changes a signed enum through its underlying integer type. Unsigned enums retain identity.
            result = type_unqualified(type_strip_aligned(table, table->types.base[type_index(type)]));
        }
    }
    switch (result)
    {
        case BUSTER_TYPE_CHAR:
        case BUSTER_TYPE_SCHAR: result = BUSTER_TYPE_UCHAR; break;
        case BUSTER_TYPE_SHORT: result = BUSTER_TYPE_USHORT; break;
        case BUSTER_TYPE_INT: result = BUSTER_TYPE_UINT; break;
        case BUSTER_TYPE_LONG: result = BUSTER_TYPE_ULONG; break;
        case BUSTER_TYPE_LLONG: result = BUSTER_TYPE_ULLONG; break;
        case BUSTER_TYPE_INT128: result = BUSTER_TYPE_UINT128; break;
        default: break;
    }
    return result;
}

static void check_builtin_atomic_convert(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output, CheckValue* value, uint32_t index, TypeId destination)
{
    bool accepted = output->action.diagnostic == CHECK_DIAG_NONE;
    const uint8_t source_kind = check_action_kind(table, value->type);
    const uint8_t destination_kind = check_action_kind(table, destination);
    const bool unsupported_conversion = source_kind == BUSTER_TYPE_KIND_COMPLEX || destination_kind == BUSTER_TYPE_KIND_COMPLEX ||
                                        (source_kind == BUSTER_TYPE_KIND_FLOATING && !check_action_float_supported(table, value->type)) ||
                                        (destination_kind == BUSTER_TYPE_KIND_FLOATING && !check_action_float_supported(table, destination));
    if (accepted && destination_kind == BUSTER_TYPE_KIND_POINTER && source_kind == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId to = type_element(table, type_strip_aligned(table, destination));
        const TypeId from = type_element(table, type_strip_aligned(table, value->type));
        const TypeId to_base = type_unqualified(type_strip_aligned(table, to));
        const TypeId from_base = type_unqualified(type_strip_aligned(table, from));
        const uint8_t to_kind = check_action_kind(table, to);
        const uint8_t from_kind = check_action_kind(table, from);
        const bool void_pointer = to_kind == BUSTER_TYPE_KIND_VOID || from_kind == BUSTER_TYPE_KIND_VOID;
        const bool function_pointer = to_kind == BUSTER_TYPE_KIND_FUNCTION || from_kind == BUSTER_TYPE_KIND_FUNCTION;
        const bool visible = void_pointer || to_base == from_base || check_builtin_atomic_pointer_comparison_visible(table, to_base, from_base, input, &output->action, index);
        const bool compatible = void_pointer || to_base == from_base || (visible && check_action_compatible(table, to_base, from_base, &output->action));
        const bool sign_pair = !compatible && visible && output->action.diagnostic == CHECK_DIAG_NONE &&
                               check_builtin_atomic_pointer_sign_type(table, to_base, input) == check_builtin_atomic_pointer_sign_type(table, from_base, input);
        const bool loses_qualifiers = (type_qualifiers(from) & ~type_qualifiers(to)) != 0;
        accepted = output->action.diagnostic == CHECK_DIAG_NONE && (compatible || sign_pair);
        if (accepted)
        {
            output->warnings |= loses_qualifiers && !sign_pair ? BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS : 0;
            output->warnings |= void_pointer && function_pointer ? BUSTER_CHECK_BUILTIN_ATOMIC_WARN_FUNCTION_POINTER : 0;
            // Ordinary C qualifier loss remains compatible in Clang's IsAssignConvertCompatible; a corresponding
            // signedness pair therefore selects its pointer-sign diagnostic. Fatal address-space/lifetime cases
            // in Clang's preceding qualification check are not represented by this type table.
            output->warnings |= !void_pointer && sign_pair ? BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN : 0;
            if (loses_qualifiers || (void_pointer && function_pointer) || (!void_pointer && sign_pair))
            {
                output->warning_operands |= UINT32_C(1) << index;
            }
        }
    }
    else if (accepted)
    {
        const CheckAction context = {
            .node = input->node,
            .tag_completions = input->tag_completions,
            .tag_completion_count = input->tag_completion_count,
            .source_position = input->source_position,
            .extensions = input->extensions,
        };
        accepted = check_action_assignable(table, destination, value, &context, &output->action);
    }
    if (accepted)
    {
        if (unsupported_conversion)
        {
            // Source constraints are satisfied, but CheckValue has no exact complex/extended-format payload.
            // Publish the requested conversion without approximating it through host long double or reporting a
            // source constraint error solely because the checked runtime producer cannot yet emit the conversion.
            output->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION;
            output->argument_conversions[index].type = destination;
            output->argument_conversions[index].flags |= BUSTER_CHECK_CONVERT_ASSIGNMENT;
            value->type = destination;
            value->category = CHECK_RVALUE;
            value->constant_kind = CHECK_CONSTANT_NONE;
            value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_NULL_POINTER);
        }
        else
        {
            check_action_conversion(table, value, destination, BUSTER_CHECK_CONVERT_ASSIGNMENT, output->argument_conversions + index, input->node);
        }
    }
    else if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, index);
    }
}

static void check_builtin_atomic_object(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output, const CheckValue* pointer)
{
    if (check_action_kind(table, pointer->type) != BUSTER_TYPE_KIND_POINTER)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_POINTER_REQUIRED, 0);
    }
    else
    {
        const TypeId object = type_element(table, type_strip_aligned(table, pointer->type));
        const TypeId plain = type_strip_aligned(table, object);
        const uint8_t kind = type_kind(table, plain);
        const bool floating = kind == BUSTER_TYPE_KIND_FLOATING ||
                              (kind == BUSTER_TYPE_KIND_COMPLEX && check_action_kind(table, type_element(table, plain)) == BUSTER_TYPE_KIND_FLOATING);
        output->object_type = object;
        // Clang removes the inferred object's local const/volatile, preserving pointer restrict qualification.
        output->value_type = plain & ~(BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE);
        output->object_address = *pointer;
        output->volatile_access = (type_qualifiers(plain) & BUSTER_TYPE_VOLATILE) != 0;
        if (!check_builtin_atomic_source_complete(table, object, input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 0);
        }
        else if ((!check_action_is_integer(table, plain) && kind != BUSTER_TYPE_KIND_POINTER && !floating) ||
                 kind == BUSTER_TYPE_KIND_BIT_INT || type_unqualified(plain) == BUSTER_TYPE_LONG_DOUBLE)
        {
            // Clang 23.1.1 allows ordinary integer/pointer/floating objects, including floating complex types.
            // GNU _Atomic(T), records, _BitInt and direct x87 long double remain source constraint errors.
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (input->id == CBUILTIN_ATOMIC_STORE_N && (type_qualifiers(plain) & BUSTER_TYPE_CONST) != 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_MODIFIABLE_REQUIRED, 0);
        }
        else
        {
            output->object_size = type_size(table, object);
            output->required_alignment = output->object_size;
            output->alignment_known = input->alignment_known;
            output->minimum_alignment = input->alignment_known ? input->minimum_alignment : 0;
            if (floating && output->value_type != BUSTER_TYPE_FLOAT && output->value_type != BUSTER_TYPE_DOUBLE)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_FLOAT_FORMAT;
            }
            if (output->object_size != 1 && output->object_size != 2 && output->object_size != 4 && output->object_size != 8)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH;
            }
            if (!input->alignment_known)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_UNKNOWN_ALIGNMENT;
            }
            else if (input->minimum_alignment == 0 || (input->minimum_alignment & (input->minimum_alignment - 1)) != 0)
            {
                // Invalid producer metadata is distinct from valid-but-under-aligned source input.
                check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
            }
            else if (input->minimum_alignment < output->required_alignment)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ALIGNMENT;
                output->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ALIGNMENT;
                output->warning_operands |= 1;
            }
            if (pointer->constant_kind == CHECK_CONSTANT_ADDRESS && pointer->object == 0 && pointer->offset == 0)
            {
                output->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_NULL;
                output->warning_operands |= 1;
            }
        }
    }
}

static void check_builtin_atomic_order(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output, CheckValue* order, uint32_t index)
{
    check_builtin_atomic_convert(table, input, output, order, index, BUSTER_TYPE_INT);
    if (output->action.diagnostic == CHECK_DIAG_NONE && order->constant_kind == CHECK_CONSTANT_INTEGER && order->fault == CHECK_FAULT_NONE &&
        (order->flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
    {
        output->success_order = (uint32_t)order->integer;
        output->known_orders |= BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER;
        const bool load = input->id == CBUILTIN_ATOMIC_LOAD_N;
        const bool store = input->id == CBUILTIN_ATOMIC_STORE_N;
        const uint32_t value = output->success_order;
        const bool valid = value <= 5 && (!load || (value != 3 && value != 4)) && (!store || (value != 1 && value != 2 && value != 4));
        // Clang diagnoses invalid load/store orders only when getIntegerConstantExpr succeeds. Fences have
        // ordinary void(int) prototypes and no corresponding Sema warning; their raw order remains metadata.
        if ((load || store) && (order->flags & BUSTER_CHECK_ICE) != 0 && !valid)
        {
            output->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER;
            output->warning_operands |= UINT32_C(1) << index;
        }
    }
}

// clang's constant answer to a lock-free query of `size` bytes (ExprConstant's evaluation of __atomic_always_lock_free,
// __atomic_is_lock_free and __c11_atomic_is_lock_free): a size that is a power of two, 0 among them, up to the target's
// inline width (16 bytes where it has CMPXCHG16B, 8 otherwise) is lock-free for the C11 query, which takes an _Atomic
// object, and for one byte; for the GNU queries, also where `pointer`, their argument before its conversion to const
// volatile void *, is a constant address aligned to the size (0 alone for a size of 0), or points to a complete type of
// at least the size's alignment. 1 or 0, or -1 for no constant: __atomic_is_lock_free's runtime query of libatomic,
// which __atomic_always_lock_free answers 0.
static int32_t check_builtin_atomic_lock_free(const TypeTable* table, CBuiltinId id, uint64_t size, const CheckValue* pointer)
{
    const uint64_t inline_bytes = table->atomic_inline_16 ? 16 : 8;
    int32_t result = -1;
    if ((size & (size - 1)) == 0 && size <= inline_bytes)
    {
        const bool constant = pointer != NULL && pointer->fault == CHECK_FAULT_NONE && (pointer->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                              (pointer->constant_kind == CHECK_CONSTANT_INTEGER || (pointer->constant_kind == CHECK_CONSTANT_ADDRESS && pointer->object == 0));
        const uint64_t address = !constant ? 1 : pointer->constant_kind == CHECK_CONSTANT_INTEGER ? (uint64_t)pointer->integer : pointer->offset;
        const bool aligned = constant && (size == 0 ? address == 0 : (address & (size - 1)) == 0);
        const TypeId pointee = pointer != NULL && check_action_kind(table, pointer->type) == BUSTER_TYPE_KIND_POINTER ?
                               type_element(table, type_strip_aligned(table, pointer->type)) : 0;
        const bool typed = pointee != 0 && check_action_kind(table, pointee) != BUSTER_TYPE_KIND_VOID &&
                           check_action_kind(table, pointee) != BUSTER_TYPE_KIND_FUNCTION && type_is_complete(table, type_strip_aligned(table, pointee)) &&
                           type_alignment(table, pointee) >= size;
        result = id == CBUILTIN_C11_ATOMIC_IS_LOCK_FREE || size == 1 || aligned || typed ? 1 : -1;
    }
    return result < 0 && id == CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE ? 0 : result;
}

// The GNU lock-free queries, of clang's prototype bool (size_t, const volatile void *): constant where clang's evaluator
// answers (check_builtin_atomic_lock_free) a size that is a constant, its arguments then never evaluated, as clang folds
// the call; otherwise __atomic_is_lock_free evaluates both and asks libatomic at run time, and __atomic_always_lock_free
// is valid source that clang's code generation refuses ("cannot compile this builtin function yet"), as the lowering does.
static void check_builtin_atomic_query(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output)
{
    CheckValue values[2] = { 0 };
    if (input->argument_count != 2)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        output->runtime_builtin = input->id;
        output->action.value = (CheckValue){ .type = BUSTER_TYPE_BOOL, .category = CHECK_RVALUE };
        output->evaluated_operands = 3;
        for (uint32_t i = 0; i < 2 && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            values[i] = check_builtin_atomic_argument(table, input, output, i);
            check_action_inherit(&output->action.value, values + i);
        }
        const CheckValue pointer = values[1];
        const TypeId parameter = type_pointer(table, type_qualify(table, BUSTER_TYPE_VOID, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE));
        if (output->action.diagnostic == CHECK_DIAG_NONE && parameter == 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 1);
        }
        check_builtin_atomic_convert(table, input, output, values, 0, BUSTER_TYPE_ULONG);
        check_builtin_atomic_convert(table, input, output, values + 1, 1, parameter);
        const bool sized = output->action.diagnostic == CHECK_DIAG_NONE && values[0].constant_kind == CHECK_CONSTANT_INTEGER &&
                           values[0].fault == CHECK_FAULT_NONE && (values[0].flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
        const int32_t answer = sized ? check_builtin_atomic_lock_free(table, input->id, (uint64_t)values[0].integer, &pointer) : -1;
        if (answer >= 0)
        {
            output->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
            output->action.value.integer = answer;
            output->action.value.flags = (values[0].flags & BUSTER_CHECK_ICE) != 0 ? BUSTER_CHECK_ICE : 0;
            output->evaluated_operands = 0;
        }
    }
}

static __attribute__((unused)) void check_builtin_atomic_one(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output)
{
    *output = (CheckBuiltinAtomicResult){ 0 };
    const bool query = input->id == CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE || input->id == CBUILTIN_ATOMIC_IS_LOCK_FREE;
    const bool load = input->id == CBUILTIN_ATOMIC_LOAD_N;
    const bool store = input->id == CBUILTIN_ATOMIC_STORE_N;
    const bool synchronize = input->id == CBUILTIN_SYNC_SYNCHRONIZE;
    const bool fence = input->id == CBUILTIN_ATOMIC_THREAD_FENCE || input->id == CBUILTIN_ATOMIC_SIGNAL_FENCE;
    const uint32_t arity = load ? 2 : store ? 3 : fence ? 1 : 0;
    if (query)
    {
        check_builtin_atomic_query(table, input, output);
    }
    else if (!load && !store && !synchronize && !fence)
    {
        output->unsupported = BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ID;
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != arity)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (arity != 0 && input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        CheckValue values[3] = { 0 };
        output->action.value.type = BUSTER_TYPE_VOID;
        output->action.value.category = CHECK_RVALUE;
        output->action.value.flags = BUSTER_CHECK_SIDE_EFFECT;
        output->evaluated_operands = (UINT32_C(1) << arity) - 1;
        output->runtime_builtin = input->id;
        output->compiler_barrier = true;
        output->reads_memory = load;
        output->writes_memory = store;
        for (uint32_t i = 0; i < arity && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            values[i] = check_builtin_atomic_argument(table, input, output, i);
            check_action_inherit(&output->action.value, values + i);
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE && (load || store))
        {
            check_builtin_atomic_object(table, input, output, values);
            if (output->action.diagnostic == CHECK_DIAG_NONE && store)
            {
                check_builtin_atomic_convert(table, input, output, values + 1, 1, output->value_type);
                output->store_value = values[1];
                check_action_inherit(&output->action.value, values + 1);
            }
            if (output->action.diagnostic == CHECK_DIAG_NONE)
            {
                const uint32_t order = store ? 2 : 1;
                check_builtin_atomic_order(table, input, output, values + order, order);
                check_action_inherit(&output->action.value, values + order);
                output->action.value.type = load ? output->value_type : BUSTER_TYPE_VOID;
            }
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE && fence)
        {
            check_builtin_atomic_order(table, input, output, values, 0);
            check_action_inherit(&output->action.value, values);
            output->fence_scope = input->id == CBUILTIN_ATOMIC_SIGNAL_FENCE ? CHECK_BUILTIN_ATOMIC_FENCE_SIGNAL : CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM;
        }
        else if (synchronize)
        {
            output->success_order = 5;
            output->known_orders = BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER;
            output->fence_scope = CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM;
        }
    }
    // An atomic access is never a source constant expression, even if its address/store bits are known; a lock-free
    // query is one where clang's evaluator answers it.
    if (!query || output->action.value.constant_kind == CHECK_CONSTANT_NONE)
    {
        output->action.value.constant_kind = CHECK_CONSTANT_NONE;
        output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL);
    }
}

static __attribute__((unused)) void check_builtin_atomic(TypeTable* table, const CheckBuiltinAtomicAction* actions, CheckBuiltinAtomicResult* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_atomic_one(table, actions + i, results + i);
    }
}

#endif
