// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-time C overload sets. Include after c_check_actions.c and c_check_builtin_library.c. Candidates are immutable declaration snapshots:
// previous is an earlier dense index in the same lexical scope, and every use retains its own head. Redeclarations
// keep their canonical declaration/object, with inherited attributes, so later declarations never alter an earlier
// call. The caller owns name lookup, linkage and publication. This module checks signatures and selects by the
// partial order of per-argument conversions; it never sums ranks. enable_if conditions (#1447, c_check_condition_eval.c)
// remove the candidates they don't admit and break ties of equal conversions, as clang's CheckEnableIf and
// compareEnableIfAttrs do. The matrix is bounded by candidates * arguments,
// and scalar/masked-vector comparisons consume the same rows without padding. All scratch is restored on exit.
// Original implementation and fixtures. Clang 23.1.1 SemaOverload.cpp and AttributeReference describe behavior;
// installed Clang is an external oracle. No upstream implementation or tests are incorporated.
#ifndef BUSTER_C_CHECK_OVERLOAD_INCLUDED
#define BUSTER_C_CHECK_OVERLOAD_INCLUDED

#define BUSTER_CHECK_OVERLOADABLE 0x01u
#define BUSTER_CHECK_OVERLOAD_UNAVAILABLE 0x02u
#define BUSTER_CHECK_OVERLOAD_DEFINED 0x04u

typedef struct CheckOverloadCandidate CheckOverloadCandidate;
struct CheckOverloadCandidate
{
    TypeId type;
    uint32_t declaration;
    uint32_t canonical;
    uint32_t object;
    uint32_t previous;
    uint32_t scope;
    uint32_t position;
    uint32_t flags;
    // Immutable parameter modes retained by the source adapter; no hidden arguments in the conservative ABI.
    const uint8_t* object_sizes;
    uint32_t object_size_count;
    // A plain library fold's mark belongs to this ordinary declaration, never to an overloadable one.
    uint32_t library_call;
};

typedef struct CheckOverloadContext CheckOverloadContext;
struct CheckOverloadContext
{
    CheckContext semantic;
    uint64_t* scratch_peak;
    // The unit's enable_if and diagnose_if conditions (#1447, c_check_condition_eval.c), null or empty for none: an
    // enable_if that doesn't admit a call makes its candidate not viable, and one that isn't always true makes its
    // candidate's address ineligible. A call's arguments that are addresses never null (`&x`), one byte each, or null.
    const CheckConditionTable* conditions;
    const uint8_t* argument_nonnull;
};

typedef enum CheckOverloadStatus
{
    CHECK_OVERLOAD_OK,
    CHECK_OVERLOAD_INVALID,
    CHECK_OVERLOAD_PROTOTYPE,
    CHECK_OVERLOAD_CONFLICT,
    CHECK_OVERLOAD_ATTRIBUTE,
    CHECK_OVERLOAD_REDEFINITION,
    CHECK_OVERLOAD_NO_MATCH,
    CHECK_OVERLOAD_AMBIGUOUS,
    CHECK_OVERLOAD_UNAVAILABLE,
    CHECK_OVERLOAD_FULL,
} CheckOverloadStatus;

typedef struct CheckOverloadResult CheckOverloadResult;
struct CheckOverloadResult
{
    CheckOverloadStatus status;
    uint32_t candidate;
    uint32_t declaration;
    uint32_t canonical;
    uint32_t object;
    TypeId type;
    uint32_t flags;
};

// Why one candidate of a call isn't selected, for clang's notes of a call none or several are viable for (a diagnosis's
// cold path, check_overload_explain).
typedef enum CheckOverloadFailure
{
    CHECK_OVERLOAD_FAILURE_NONE,
    CHECK_OVERLOAD_FAILURE_FEW_ARGUMENTS,
    CHECK_OVERLOAD_FAILURE_MANY_ARGUMENTS,
    CHECK_OVERLOAD_FAILURE_CONVERSION,
    CHECK_OVERLOAD_FAILURE_DISABLED,
} CheckOverloadFailure;

typedef struct CheckOverloadExplained CheckOverloadExplained;
struct CheckOverloadExplained
{
    // The candidate (each distinct function's latest visible declaration), its enable_if that disables it (an index into
    // the unit's conditions) and whether it is among an ambiguous call's best (clang's OverloadCandidate.Best).
    uint32_t candidate;
    uint32_t condition;
    // A conversion failure's argument (from 0): the first that converts to its parameter by no conversion (#2384).
    uint32_t argument;
    uint8_t failure;
    bool best;
};

typedef struct CheckOverloadRequest CheckOverloadRequest;
struct CheckOverloadRequest
{
    const CheckOverloadCandidate* candidates;
    uint32_t count;
    uint32_t head;
    const CheckValue* arguments;
    uint32_t argument_count;
    CheckOverloadContext context;
    bool scalar;
    // Where not null, each distinct candidate's outcome, in the walk's order (room for every candidate), and its count.
    CheckOverloadExplained* explained;
    uint32_t* explained_count;
};

typedef enum CheckOverloadRank
{
    CHECK_OVERLOAD_EXACT,
    CHECK_OVERLOAD_PROMOTION,
    CHECK_OVERLOAD_CONVERSION,
    CHECK_OVERLOAD_COMPLEX_REAL,
    CHECK_OVERLOAD_C_CONVERSION,
    CHECK_OVERLOAD_BAD_POINTER,
    CHECK_OVERLOAD_ELLIPSIS,
    CHECK_OVERLOAD_IMPOSSIBLE = 255,
} CheckOverloadRank;

static CheckOverloadResult check_overload_result(CheckOverloadStatus status)
{
    const CheckOverloadResult result = {
        .status = status,
        .candidate = UINT32_MAX,
        .declaration = UINT32_MAX,
        .canonical = UINT32_MAX,
    };
    return result;
}

static bool check_overload_candidate_valid(const TypeTable* table, const CheckOverloadCandidate* candidate)
{
    const TypeId type = candidate->type;
    const uint32_t flags = candidate->flags;
    const uint32_t library = candidate->library_call;
    const uint32_t fold = library & ~BUSTER_CHECK_LIBRARY_CALL;
    bool valid = type_is_valid(table, type) && check_action_kind(table, type) == BUSTER_TYPE_KIND_FUNCTION &&
                 candidate->declaration != UINT32_MAX && candidate->canonical != UINT32_MAX &&
                 (flags & ~(BUSTER_CHECK_OVERLOADABLE | BUSTER_CHECK_OVERLOAD_UNAVAILABLE | BUSTER_CHECK_OVERLOAD_DEFINED)) == 0;
    if (valid && library != 0)
    {
        valid = (library & BUSTER_CHECK_LIBRARY_CALL) != 0 && fold < BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS &&
                (flags & BUSTER_CHECK_OVERLOADABLE) == 0 && check_library_plain_prototype(table, type, fold);
    }
    if (valid && candidate->object_sizes != NULL)
    {
        uint32_t count = 0;
        type_parameters(table, type_strip_aligned(table, type), &count);
        valid = count != 0 && count == candidate->object_size_count;
        for (uint32_t p = 0; valid && p < count; p += 1)
        {
            valid = candidate->object_sizes[p] <= 8;
        }
    }
    else
    {
        valid &= candidate->object_size_count == 0;
    }
    return valid;
}

static TypeAnswer check_overload_compatible(const TypeTable* table, TypeId left, TypeId right, const CheckOverloadContext* context)
{
    return left == right ? TYPE_ANSWER_YES : type_compatible_at(table, left, right, context->semantic.transparent_unions, context->semantic.tag_completion_count, context->semantic.source_position);
}

// Parameter canonical identity distinguishes an enum from its compatible integer type, and a prototyped function
// pointer from a compatible old-style pointer. Return types are intentionally absent from this signature relation.
static TypeAnswer check_overload_signature(const TypeTable* table, TypeId left, TypeId right)
{
    const TypeId l = type_strip_aligned(table, left);
    const TypeId r = type_strip_aligned(table, right);
    uint32_t lc = 0;
    uint32_t rc = 0;
    const TypeId* lp = type_parameters(table, l, &lc);
    const TypeId* rp = type_parameters(table, r, &rc);
    const uint8_t mask = BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC;
    TypeAnswer answer = lc == rc && (table->types.flags[type_index(l)] & mask) == (table->types.flags[type_index(r)] & mask) ? TYPE_ANSWER_YES : TYPE_ANSWER_NO;
    for (uint32_t i = 0; answer == TYPE_ANSWER_YES && i < lc; i += 1)
    {
        answer = type_same_canonical(table, type_unqualified(lp[i]), type_unqualified(rp[i]));
    }
    return answer;
}

static TypeAnswer check_overload_candidate_signature(const TypeTable* table, const CheckOverloadCandidate* left, const CheckOverloadCandidate* right)
{
    return (left->object_sizes != NULL) == (right->object_sizes != NULL) ? check_overload_signature(table, left->type, right->type) : TYPE_ANSWER_NO;
}

static bool check_overload_object_sizes_same(const CheckOverloadCandidate* left, const CheckOverloadCandidate* right)
{
    return left->object_size_count == right->object_size_count &&
           (left->object_size_count == 0 || (left->object_sizes != NULL && right->object_sizes != NULL &&
                                            memcmp(left->object_sizes, right->object_sizes, left->object_size_count) == 0));
}

// Whether two declarations of one signature are one function by their enable_if conditions (#1447): clang's IsOverload
// makes overloadable ones whose enable_ifs differ (check_condition_compare_enable) two overloads.
static bool check_overload_enables_same(const CheckOverloadContext* context, const CheckOverloadCandidate* left, const CheckOverloadCandidate* right)
{
    const CheckConditionTable* conditions = context != NULL ? context->conditions : NULL;
    const bool compared = conditions != NULL && conditions->declaration_count != 0 && (left->flags & right->flags & BUSTER_CHECK_OVERLOADABLE) != 0;
    return !compared || check_condition_compare_enable(conditions, check_condition_find(conditions, left->declaration),
                                                       check_condition_find(conditions, right->declaration)) == 0;
}

static CheckOverloadResult check_overload_declare_chain(const TypeTable* table, const CheckOverloadCandidate* candidates, uint32_t count, uint32_t head, const CheckOverloadCandidate* candidate, const CheckOverloadContext* context, const uint32_t* next, const uint8_t* checked, uint32_t unmarked)
{
    CheckOverloadResult result = check_overload_result(CHECK_OVERLOAD_OK);
    const bool valid = candidate != 0 && context != 0 && (count == 0 || candidates != 0) && (head == UINT32_MAX || head < count) &&
                       check_overload_candidate_valid(table, candidate);
    if (!valid)
    {
        result.status = CHECK_OVERLOAD_INVALID;
    }
    else
    {
        result.candidate = count;
        result.declaration = candidate->declaration;
        result.canonical = candidate->canonical;
        result.object = candidate->object;
        result.type = candidate->type;
        result.flags = candidate->flags;
        const TypeId function = type_strip_aligned(table, candidate->type);
        if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0 && (table->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0)
        {
            result.status = CHECK_OVERLOAD_PROTOTYPE;
        }
        bool matched = false;
        bool complete = false;
        for (uint32_t current = head; !complete && result.status == CHECK_OVERLOAD_OK && current != UINT32_MAX; current = next != NULL ? next[current] : candidates[current].previous)
        {
            const CheckOverloadCandidate* prior = &candidates[current];
            if (!check_overload_candidate_valid(table, prior) || prior->scope != candidate->scope ||
                (prior->previous != UINT32_MAX && prior->previous >= current) ||
                (next != NULL && next[current] != UINT32_MAX && next[current] >= current))
            {
                result.status = CHECK_OVERLOAD_INVALID;
            }
            else
            {
                const TypeAnswer compared = check_overload_candidate_signature(table, candidate, prior);
                const TypeAnswer signature = compared == TYPE_ANSWER_YES && !check_overload_enables_same(context, candidate, prior) ? TYPE_ANSWER_NO : compared;
                if (signature == TYPE_ANSWER_FULL)
                {
                    result.status = CHECK_OVERLOAD_FULL;
                }
                else if (signature == TYPE_ANSWER_YES)
                {
                    const TypeAnswer returns = check_overload_compatible(table, type_return(table, candidate->type), type_return(table, prior->type), context);
                    if (type_function_abi_kind(table, candidate->type) != type_function_abi_kind(table, prior->type))
                    {
                        result.status = CHECK_OVERLOAD_CONFLICT;
                    }
                    else if (returns != TYPE_ANSWER_YES)
                    {
                        result.status = returns == TYPE_ANSWER_FULL ? CHECK_OVERLOAD_FULL : CHECK_OVERLOAD_CONFLICT;
                    }
                    else if (((candidate->flags ^ prior->flags) & BUSTER_CHECK_OVERLOADABLE) != 0 || !check_overload_object_sizes_same(candidate, prior))
                    {
                        result.status = CHECK_OVERLOAD_ATTRIBUTE;
                    }
                    else if ((candidate->flags & prior->flags & BUSTER_CHECK_OVERLOAD_DEFINED) != 0)
                    {
                        result.status = CHECK_OVERLOAD_REDEFINITION;
                    }
                    else
                    {
                        if (!matched)
                        {
                            result.candidate = current;
                            result.canonical = prior->canonical;
                            result.object = prior->object;
                        }
                        matched = true;
                        result.flags |= prior->flags;
                        // A canonically identical return type has already passed every older declaration of this
                        // signature. Its accumulated flags retain definitions and unavailable attributes. Merely
                        // compatible returns cannot stop the walk: old-style function pointers are not transitive.
                        if (next != NULL && checked[current])
                        {
                            const TypeAnswer same = type_same_canonical(table, type_return(table, candidate->type), type_return(table, prior->type));
                            result.status = same == TYPE_ANSWER_FULL ? CHECK_OVERLOAD_FULL : CHECK_OVERLOAD_OK;
                            complete = same == TYPE_ANSWER_YES;
                        }
                    }
                }
                if ((prior->flags & BUSTER_CHECK_OVERLOADABLE) == 0)
                {
                    unmarked = prior->canonical;
                }
            }
        }
        if (result.status == CHECK_OVERLOAD_OK && (candidate->flags & BUSTER_CHECK_OVERLOADABLE) == 0 && unmarked != UINT32_MAX && unmarked != result.canonical)
        {
            result.status = CHECK_OVERLOAD_ATTRIBUTE;
        }
    }
    return result;
}

static __attribute__((unused)) CheckOverloadResult check_overload_declare(const TypeTable* table, const CheckOverloadCandidate* candidates, uint32_t count, uint32_t head, const CheckOverloadCandidate* candidate, const CheckOverloadContext* context)
{
    return check_overload_declare_chain(table, candidates, count, head, candidate, context, NULL, NULL, UINT32_MAX);
}

#include "c_check_overload_index.c"
#include "c_check_overload_address.c"

// C overloads use the standard multilevel qualification relation. A deeper qualification addition requires const
// at every intervening destination pointer; int ** -> const int ** therefore fails, while const int *const * works.
static TypeAnswer check_overload_qualification(const TypeTable* table, TypeId source, TypeId target)
{
    TypeId from = type_unqualified(type_strip_aligned(table, source));
    TypeId to = type_unqualified(type_strip_aligned(table, target));
    bool valid = true;
    bool enclosing_const = true;
    uint32_t depth = 0;
    while (valid && check_action_kind(table, from) == BUSTER_TYPE_KIND_POINTER && check_action_kind(table, to) == BUSTER_TYPE_KIND_POINTER)
    {
        from = type_strip_aligned(table, type_element(table, from));
        to = type_strip_aligned(table, type_element(table, to));
        const uint32_t fq = type_qualifiers(from);
        const uint32_t tq = type_qualifiers(to);
        valid = (fq & ~tq) == 0 && (fq == tq || depth == 0 || enclosing_const);
        enclosing_const = enclosing_const && (tq & BUSTER_TYPE_CONST) != 0;
        depth += 1;
    }
    const TypeAnswer answer = valid ? type_same_canonical(table, type_unqualified(from), type_unqualified(to)) : TYPE_ANSWER_NO;
    return answer;
}

static bool check_overload_float_promotion(TypeId source, TypeId target)
{
    return (source == BUSTER_TYPE_FLOAT && target == BUSTER_TYPE_DOUBLE) ||
           ((source == BUSTER_TYPE_FLOAT || source == BUSTER_TYPE_DOUBLE) && (target == BUSTER_TYPE_LONG_DOUBLE || target == BUSTER_TYPE_FLOAT128)) ||
           (source == BUSTER_TYPE_HALF && target == BUSTER_TYPE_FLOAT);
}

static CheckAction check_overload_action(CheckOverloadContext context)
{
    const CheckAction action = { .context = context.semantic };
    return action;
}

// An overloaded argument is a pending function designator, not the latest declaration's convertible type.
// Each candidate supplies its own formal type. This pure resolution leaves the original value and immutable
// overload set untouched; callers publish the returned identity only after the surrounding selection wins.
static CheckOverloadResult check_overload_argument(TypeTable* table, const CheckValue* source, TypeId target, const CheckOverloadCandidate* candidates, uint32_t count, const CheckOverloadContext* context, Arena* scratch, CheckValue* resolved)
{
    CheckOverloadResult selected = check_overload_result(CHECK_OVERLOAD_OK);
    *resolved = *source;
    if (source->overload_head != 0)
    {
        selected = check_overload_address(table, candidates, count, source->overload_head - 1, target, context, scratch);
        if (selected.status == CHECK_OVERLOAD_OK)
        {
            resolved->type = source->category == CHECK_FUNCTION ? selected.type : type_pointer(table, selected.type);
            resolved->object = selected.object;
            resolved->overload_head = 0;
            if (resolved->type == 0)
            {
                selected.status = table->last_error == TYPE_ERROR_FULL ? CHECK_OVERLOAD_FULL : CHECK_OVERLOAD_INVALID;
            }
        }
    }
    return selected;
}

static uint8_t check_overload_conversion(const TypeTable* table, const CheckValue* value, TypeId parameter, const CheckOverloadContext* context, bool* full)
{
    const TypeId source = type_unqualified(type_strip_aligned(table, value->type));
    const TypeId target = type_unqualified(type_strip_aligned(table, parameter));
    const uint8_t sk = check_action_kind(table, source);
    const uint8_t tk = check_action_kind(table, target);
    const TypeAnswer same = type_same_canonical(table, source, target);
    *full |= same == TYPE_ANSWER_FULL;
    uint8_t rank = same == TYPE_ANSWER_YES ? CHECK_OVERLOAD_EXACT : CHECK_OVERLOAD_IMPOSSIBLE;
    if (rank == CHECK_OVERLOAD_IMPOSSIBLE && !*full)
    {
        const TypeId promoted = (value->flags & BUSTER_CHECK_BIT_FIELD) != 0 ? type_promote_bit_field(table, source, value->bit_width) : type_promote(table, source);
        if ((check_action_is_integer(table, source) && target == promoted && source != target) || check_overload_float_promotion(source, target))
        {
            rank = CHECK_OVERLOAD_PROMOTION;
        }
        else if (sk == BUSTER_TYPE_KIND_COMPLEX && tk == BUSTER_TYPE_KIND_COMPLEX)
        {
            const TypeId element = type_element(table, source);
            const TypeId destination = type_element(table, target);
            const bool promotion = check_overload_float_promotion(element, destination) ||
                                   (check_action_is_integer(table, element) && type_promote(table, element) == destination && element != destination);
            rank = promotion ? CHECK_OVERLOAD_PROMOTION : CHECK_OVERLOAD_CONVERSION;
        }
        else if (target == BUSTER_TYPE_BOOL && check_action_is_scalar(table, source))
        {
            rank = CHECK_OVERLOAD_CONVERSION;
        }
        else if (check_action_is_arithmetic(table, source) && check_action_is_arithmetic(table, target))
        {
            rank = sk == BUSTER_TYPE_KIND_COMPLEX || tk == BUSTER_TYPE_KIND_COMPLEX ? CHECK_OVERLOAD_COMPLEX_REAL : CHECK_OVERLOAD_CONVERSION;
        }
        else if (tk == BUSTER_TYPE_KIND_POINTER && check_action_null(table, value, context->semantic.extensions))
        {
            rank = CHECK_OVERLOAD_CONVERSION;
        }
        else if (sk == BUSTER_TYPE_KIND_POINTER && tk == BUSTER_TYPE_KIND_POINTER)
        {
            const TypeAnswer qualified = check_overload_qualification(table, source, target);
            *full |= qualified == TYPE_ANSWER_FULL;
            if (qualified == TYPE_ANSWER_YES)
            {
                rank = CHECK_OVERLOAD_EXACT;
            }
            else if (!*full)
            {
                const TypeId from = type_strip_aligned(table, type_element(table, source));
                const TypeId to = type_strip_aligned(table, type_element(table, target));
                const bool qualifiers = (type_qualifiers(from) & ~type_qualifiers(to)) == 0;
                const TypeAnswer compatible = check_overload_compatible(table, type_unqualified(from), type_unqualified(to), context);
                *full |= compatible == TYPE_ANSWER_FULL;
                if (qualifiers && compatible == TYPE_ANSWER_YES)
                {
                    rank = CHECK_OVERLOAD_CONVERSION;
                }
                else if (qualifiers && check_action_kind(table, to) == BUSTER_TYPE_KIND_VOID && check_action_kind(table, from) != BUSTER_TYPE_KIND_FUNCTION)
                {
                    rank = CHECK_OVERLOAD_CONVERSION;
                }
                else if (qualifiers && check_action_kind(table, from) == BUSTER_TYPE_KIND_VOID && check_action_kind(table, to) != BUSTER_TYPE_KIND_FUNCTION)
                {
                    rank = CHECK_OVERLOAD_C_CONVERSION;
                }
                else
                {
                    rank = CHECK_OVERLOAD_BAD_POINTER;
                }
            }
        }
        else if (check_vector_type(table, source) || check_vector_type(table, target))
        {
            CheckActionResult converted = { 0 };
            if (check_vector_assignable(table, target, value, &converted))
            {
                rank = CHECK_OVERLOAD_CONVERSION;
            }
            *full |= converted.diagnostic == CHECK_DIAG_RESOURCE;
        }
        else
        {
            const TypeAnswer compatible = check_overload_compatible(table, source, target, context);
            *full |= compatible == TYPE_ANSWER_FULL;
            if (compatible == TYPE_ANSWER_YES)
            {
                rank = CHECK_OVERLOAD_CONVERSION;
            }
            else if (!*full && tk == BUSTER_TYPE_KIND_UNION)
            {
                CheckAction action = check_overload_action(*context);
                CheckActionResult converted = { 0 };
                if (check_action_transparent_parameter(table, target, value, &action, &converted) != 0)
                {
                    rank = CHECK_OVERLOAD_CONVERSION;
                }
                *full |= converted.diagnostic == CHECK_DIAG_RESOURCE;
            }
        }
    }
    return rank;
}

// Rows first compare their sets of incompatible-pointer arguments. An otherwise preferable exact match cannot
// compensate for introducing that extension on an argument; incomparable bad-pointer sets stay incomparable.
static uint32_t check_overload_rank_order(const uint8_t* left, const uint8_t* right, uint32_t count, bool scalar)
{
    (void)scalar;

    uint32_t bad_order = 0;
    uint32_t rank_order = 0;
    uint32_t i = 0;
    if (!BUSTER_SCALAR_PATH(scalar))
    {
        const __m512i bad = _mm512_set1_epi8(CHECK_OVERLOAD_BAD_POINTER);
        while (i < count)
        {
            const uint32_t lanes = count - i < 64 ? count - i : 64;
            const __mmask64 active = lanes == 64 ? UINT64_MAX : (UINT64_C(1) << lanes) - 1;
            const __m512i l = _mm512_maskz_loadu_epi8(active, left + i);
            const __m512i r = _mm512_maskz_loadu_epi8(active, right + i);
            const uint64_t lb = _mm512_mask_cmpeq_epi8_mask(active, l, bad);
            const uint64_t rb = _mm512_mask_cmpeq_epi8_mask(active, r, bad);
            bad_order |= (rb & ~lb) != 0 ? 1u : 0u;
            bad_order |= (lb & ~rb) != 0 ? 2u : 0u;
            rank_order |= _mm512_mask_cmplt_epu8_mask(active, l, r) != 0 ? 1u : 0u;
            rank_order |= _mm512_mask_cmpgt_epu8_mask(active, l, r) != 0 ? 2u : 0u;
            i += lanes;
        }
    }
    for (; i < count; i += 1)
    {
        bad_order |= left[i] != CHECK_OVERLOAD_BAD_POINTER && right[i] == CHECK_OVERLOAD_BAD_POINTER ? 1u : 0u;
        bad_order |= left[i] == CHECK_OVERLOAD_BAD_POINTER && right[i] != CHECK_OVERLOAD_BAD_POINTER ? 2u : 0u;
        rank_order |= left[i] < right[i] ? 1u : 0u;
        rank_order |= left[i] > right[i] ? 2u : 0u;
    }
    return bad_order != 0 ? bad_order | 4u : rank_order;
}

// Whether the call's arguments are admitted by `candidate`'s enable_if conditions (#1447), clang's CheckEnableIf: each of
// its parameters bound to its argument's checked value (as the call's, before its own conversion) converted to the
// parameter's type (check_condition_bind), the
// conditions checked in clang's order (check_condition_disabled). A unit without conditions doesn't call it.
static bool check_overload_enabled(TypeTable* table, const CheckOverloadRequest* request, const CheckOverloadCandidate* candidate, const TypeId* parameters,
                                   uint32_t parameter_count, Arena* scratch, bool* full, uint32_t* disabled)
{
    const CheckConditionTable* conditions = request->context.conditions;
    const CheckConditionDeclaration* entry = check_condition_find(conditions, candidate->declaration);
    bool enabled = true;
    if (entry != NULL && entry->restricted)
    {
        const uint64_t mark = scratch->length;
        const uint32_t bound_count = parameter_count < request->argument_count ? parameter_count : request->argument_count;
        CheckValue* bound = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckValue, (uint64_t)bound_count + 1);
        CheckValue* values = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckValue, (uint64_t)check_condition_steps_needed(conditions, entry) + 1);
        *full |= bound == NULL || values == NULL;
        for (uint32_t p = 0; !*full && p < bound_count; p += 1)
        {
            // An object's or a function's address is never null, as clang's evaluation knows: `&x`, an array or a function
            // that decays, an address constant the checker marked so.
            const CheckValue* original = request->arguments + p;
            const bool pointer = check_action_kind(table, type_strip_aligned(table, parameters[p])) == BUSTER_TYPE_KIND_POINTER;
            const bool nonnull = pointer && original->fault == CHECK_FAULT_NONE &&
                                 ((request->context.argument_nonnull != NULL && request->context.argument_nonnull[p] != 0) || original->category == CHECK_FUNCTION ||
                                  original->overload_head != 0 ||
                                  (original->category == CHECK_LVALUE && type_kind_is_array(check_action_kind(table, type_strip_aligned(table, original->type)))) ||
                                  (original->constant_kind == CHECK_CONSTANT_ADDRESS && (original->flags & BUSTER_CHECK_NONNULL_ADDRESS) != 0 &&
                                   original->category != CHECK_LVALUE));
            bound[p] = check_condition_bind(table, *original, nonnull, parameters[p], 0);
        }
        *disabled = *full ? UINT32_MAX : check_condition_disabled(table, conditions, entry, bound, bound_count, request->context.semantic.extensions, 0, values);
        enabled = *disabled == UINT32_MAX;
        arena_pop_to(scratch, mark);
    }
    return enabled;
}

static uint32_t check_overload_better(const TypeTable* table, const CheckOverloadRequest* request, const CheckValue* arguments, const uint8_t* left, const uint8_t* right, uint32_t li, uint32_t ri, bool* full)
{
    uint32_t order = check_overload_rank_order(left, right, request->argument_count, BUSTER_SCALAR_PATH(request->scalar));
    if ((order & 4u) == 0)
    {
        uint32_t lc = 0;
        uint32_t rc = 0;
        const TypeId* lp = type_parameters(table, request->candidates[li].type, &lc);
        const TypeId* rp = type_parameters(table, request->candidates[ri].type, &rc);
        for (uint32_t i = 0; !*full && order != 3 && i < request->argument_count && i < lc && i < rc; i += 1)
        {
            if (left[i] == right[i])
            {
                const TypeId l = type_unqualified(type_strip_aligned(table, lp[i]));
                const TypeId r = type_unqualified(type_strip_aligned(table, rp[i]));
                const bool pointer = arguments[i].overload_head != 0 || check_action_kind(table, arguments[i].type) == BUSTER_TYPE_KIND_POINTER;
                if (pointer && left[i] == CHECK_OVERLOAD_CONVERSION && (l == BUSTER_TYPE_BOOL) != (r == BUSTER_TYPE_BOOL))
                {
                    order |= l == BUSTER_TYPE_BOOL ? 2u : 1u;
                }
                else if (pointer && left[i] == CHECK_OVERLOAD_CONVERSION && check_action_kind(table, l) == BUSTER_TYPE_KIND_POINTER &&
                         check_action_kind(table, r) == BUSTER_TYPE_KIND_POINTER)
                {
                    const bool lv = check_action_kind(table, type_element(table, l)) == BUSTER_TYPE_KIND_VOID;
                    const bool rv = check_action_kind(table, type_element(table, r)) == BUSTER_TYPE_KIND_VOID;
                    if (lv != rv)
                    {
                        order |= lv ? 2u : 1u;
                    }
                }
                else if (left[i] == CHECK_OVERLOAD_EXACT && l != r && check_action_kind(table, l) == BUSTER_TYPE_KIND_POINTER && check_action_kind(table, r) == BUSTER_TYPE_KIND_POINTER)
                {
                    const TypeAnswer lr = check_overload_qualification(table, l, r);
                    const TypeAnswer rl = check_overload_qualification(table, r, l);
                    *full |= lr == TYPE_ANSWER_FULL || rl == TYPE_ANSWER_FULL;
                    if ((lr == TYPE_ANSWER_YES) != (rl == TYPE_ANSWER_YES))
                    {
                        order |= lr == TYPE_ANSWER_YES ? 1u : 2u;
                    }
                }
            }
        }
    }
    // Then clang's enable_if comparison (compareEnableIfAttrs): the candidate whose enable_ifs are the other's and more
    // is better, and ones that differ are neither; then pass_object_size's.
    const CheckConditionTable* conditions = request->context.conditions;
    if (order == 0 && conditions != NULL && conditions->declaration_count != 0)
    {
        order = check_condition_compare_enable(conditions, check_condition_find(conditions, request->candidates[li].declaration),
                                               check_condition_find(conditions, request->candidates[ri].declaration));
    }
    if (order == 0 && (request->candidates[li].object_sizes != NULL) != (request->candidates[ri].object_sizes != NULL))
    {
        order = request->candidates[li].object_sizes != NULL ? 1 : 2;
    }
    return order & 3u;
}

static CheckOverloadResult check_overload_select(TypeTable* table, const CheckOverloadRequest* request, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    CheckOverloadResult result = check_overload_result(CHECK_OVERLOAD_NO_MATCH);
    bool valid = request != 0 && (request->count == 0 || request->candidates != 0) &&
                 (request->argument_count == 0 || request->arguments != 0) && (request->head == UINT32_MAX || request->head < request->count);
    uint32_t length = 0;
    for (uint32_t current = valid ? request->head : UINT32_MAX; valid && current != UINT32_MAX; current = request->candidates[current].previous)
    {
        const CheckOverloadCandidate* candidate = &request->candidates[current];
        valid = check_overload_candidate_valid(table, candidate) && candidate->scope == request->candidates[request->head].scope &&
                (candidate->previous == UINT32_MAX || candidate->previous < current) &&
                (request->context.semantic.source_position == 0 || candidate->position <= request->context.semantic.source_position);
        if (valid && (candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0)
        {
            valid = (table->types.flags[type_index(type_strip_aligned(table, candidate->type))] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        }
        length += 1;
    }
    bool full = false;
    uint32_t* indices = 0;
    uint32_t* canonical = 0;
    uint8_t* ranks = 0;
    CheckValue* arguments = 0;
    uint64_t hash_capacity = 1;
    while (hash_capacity < (uint64_t)length * 2)
    {
        hash_capacity *= 2;
    }
    if (valid && length != 0)
    {
        indices = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, length);
        canonical = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, hash_capacity);
        ranks = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)length * request->argument_count + 1);
        arguments = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckValue, (uint64_t)request->argument_count + 1);
        full = indices == 0 || canonical == 0 || ranks == 0 || arguments == 0;
        if (!full)
        {
            memset(canonical, 0xff, hash_capacity * sizeof(*canonical));
        }
    }
    for (uint32_t i = 0; valid && !full && length != 0 && i < request->argument_count; i += 1)
    {
        valid = type_is_valid(table, request->arguments[i].type);
        if (valid && request->arguments[i].overload_head != 0)
        {
            arguments[i] = request->arguments[i];
        }
        else if (valid)
        {
            CheckAction action = check_overload_action(request->context);
            CheckActionResult converted = { 0 };
            action.operands[0] = request->arguments[i];
            arguments[i] = check_action_operand(table, &action, &converted, 0);
            full = converted.diagnostic == CHECK_DIAG_RESOURCE;
            valid = converted.diagnostic == CHECK_DIAG_NONE || full;
        }
    }
    uint32_t active_count = 0;
    for (uint32_t current = valid && !full ? request->head : UINT32_MAX; current != UINT32_MAX; current = request->candidates[current].previous)
    {
        const CheckOverloadCandidate* candidate = &request->candidates[current];
        uint64_t bucket = ((uint64_t)candidate->canonical * UINT64_C(11400714819323198485)) & (hash_capacity - 1);
        while (canonical[bucket] != UINT32_MAX && canonical[bucket] != candidate->canonical)
        {
            bucket = (bucket + 1) & (hash_capacity - 1);
        }
        if (canonical[bucket] == UINT32_MAX)
        {
            canonical[bucket] = candidate->canonical;
            uint32_t parameter_count = 0;
            const TypeId function = type_strip_aligned(table, candidate->type);
            const TypeId* parameters = type_parameters(table, function, &parameter_count);
            const bool prototype = (table->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
            const bool variadic = (table->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
            bool viable = !prototype || (request->argument_count >= parameter_count && (variadic || request->argument_count == parameter_count));
            uint8_t* row = ranks + (uint64_t)active_count * request->argument_count;
            uint32_t failed_argument = UINT32_MAX;
            for (uint32_t i = 0; viable && !full && i < request->argument_count; i += 1)
            {
                CheckValue argument = arguments[i];
                const bool fixed = prototype && i < parameter_count;
                if (argument.overload_head != 0)
                {
                    const CheckOverloadResult selected = check_overload_argument(table, arguments + i, fixed ? parameters[i] : 0,
                        request->candidates, request->count, &request->context, scratch, &argument);
                    full = selected.status == CHECK_OVERLOAD_FULL;
                    viable = selected.status == CHECK_OVERLOAD_OK;
                    if (viable)
                    {
                        CheckAction action = check_overload_action(request->context);
                        CheckActionResult converted = { 0 };
                        action.operands[0] = argument;
                        argument = check_action_operand(table, &action, &converted, 0);
                        full = converted.diagnostic == CHECK_DIAG_RESOURCE;
                        viable = converted.diagnostic == CHECK_DIAG_NONE;
                    }
                }
                row[i] = viable ? (fixed ? check_overload_conversion(table, &argument, parameters[i], &request->context, &full) : CHECK_OVERLOAD_ELLIPSIS) : CHECK_OVERLOAD_IMPOSSIBLE;
                viable = row[i] != CHECK_OVERLOAD_IMPOSSIBLE;
                failed_argument = viable ? failed_argument : i;
            }
            const uint8_t arity = prototype && request->argument_count < parameter_count ? CHECK_OVERLOAD_FAILURE_FEW_ARGUMENTS :
                                  prototype && !variadic && request->argument_count > parameter_count ? CHECK_OVERLOAD_FAILURE_MANY_ARGUMENTS :
                                  CHECK_OVERLOAD_FAILURE_NONE;
            const bool converted = viable;
            uint32_t disabled = UINT32_MAX;
            if (viable && !full && request->context.conditions != NULL && request->context.conditions->declaration_count != 0)
            {
                viable = check_overload_enabled(table, request, candidate, parameters, parameter_count, scratch, &full, &disabled);
            }
            if (request->explained != NULL)
            {
                request->explained[*request->explained_count] = (CheckOverloadExplained){
                    .candidate = current,
                    .condition = disabled,
                    .argument = failed_argument,
                    .failure = arity != CHECK_OVERLOAD_FAILURE_NONE ? arity : !converted ? CHECK_OVERLOAD_FAILURE_CONVERSION :
                               disabled != UINT32_MAX ? CHECK_OVERLOAD_FAILURE_DISABLED : CHECK_OVERLOAD_FAILURE_NONE,
                };
                *request->explained_count += 1;
            }
            if (viable && !full)
            {
                indices[active_count++] = current;
            }
        }
    }
    uint32_t best_row = active_count != 0 ? 0 : UINT32_MAX;
    for (uint32_t i = 1; !full && i < active_count; i += 1)
    {
        const uint8_t* left = ranks + (uint64_t)i * request->argument_count;
        const uint8_t* right = ranks + (uint64_t)best_row * request->argument_count;
        if (check_overload_better(table, request, arguments, left, right, indices[i], indices[best_row], &full) == 1)
        {
            best_row = i;
        }
    }
    bool wins = best_row != UINT32_MAX;
    for (uint32_t i = 0; wins && !full && i < active_count; i += 1)
    {
        if (i != best_row)
        {
            const uint8_t* left = ranks + (uint64_t)best_row * request->argument_count;
            const uint8_t* right = ranks + (uint64_t)i * request->argument_count;
            wins = check_overload_better(table, request, arguments, left, right, indices[best_row], indices[i], &full) == 1;
        }
    }
    // An ambiguous call's best, for its notes: clang's closure from the first best of the candidates the ones in it aren't
    // better than (BestViableFunction's PendingBest).
    uint8_t* marked = request->explained != NULL && !wins && active_count != 0 && !full ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, active_count) : 0;
    uint32_t* pending = marked != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, active_count) : 0;
    full |= marked != 0 && pending == 0;
    uint32_t pending_count = 0;
    if (pending != 0)
    {
        memset(marked, 0, active_count);
        marked[best_row] = 1;
        pending[pending_count++] = best_row;
    }
    while (!full && pending_count != 0)
    {
        const uint32_t current = pending[--pending_count];
        for (uint32_t i = 0; !full && i < active_count; i += 1)
        {
            const uint8_t* left = ranks + (uint64_t)current * request->argument_count;
            const uint8_t* right = ranks + (uint64_t)i * request->argument_count;
            if (marked[i] == 0 && check_overload_better(table, request, arguments, left, right, indices[current], indices[i], &full) != 1)
            {
                marked[i] = 1;
                pending[pending_count++] = i;
            }
        }
    }
    for (uint32_t i = 0; pending != 0 && !full && i < *request->explained_count; i += 1)
    {
        for (uint32_t j = 0; j < active_count; j += 1)
        {
            request->explained[i].best |= marked[j] != 0 && indices[j] == request->explained[i].candidate;
        }
    }
    const uint32_t best = wins ? indices[best_row] : UINT32_MAX;
    result.status = full ? CHECK_OVERLOAD_FULL : !valid ? CHECK_OVERLOAD_INVALID : best == UINT32_MAX ?
                    (active_count == 0 ? CHECK_OVERLOAD_NO_MATCH : CHECK_OVERLOAD_AMBIGUOUS) : CHECK_OVERLOAD_OK;
    if (result.status == CHECK_OVERLOAD_OK)
    {
        const CheckOverloadCandidate* selected = &request->candidates[best];
        result.candidate = best;
        result.declaration = selected->declaration;
        result.canonical = selected->canonical;
        result.object = selected->object;
        result.type = selected->type;
        result.flags = selected->flags;
        result.status = (selected->flags & BUSTER_CHECK_OVERLOAD_UNAVAILABLE) != 0 ? CHECK_OVERLOAD_UNAVAILABLE : CHECK_OVERLOAD_OK;
    }
    if (request != 0 && request->context.scratch_peak != 0 && scratch->length > *request->context.scratch_peak)
    {
        *request->context.scratch_peak = scratch->length;
    }
    arena_pop_to(scratch, mark);
    return result;
}

// The selected call uses ordinary CALL constraints and conversions. Only certified standard multilevel pointer
// qualifications need an adjusted argument view; their original lvalue/volatile/effect conversion is retained. The
// candidates' enable_if conditions are `conditions` (null for none), the arguments that are addresses never null
// `argument_nonnull` (null for none known).
static CheckOverloadResult check_overload_call_with(TypeTable* table, const CheckAction* action, const CheckOverloadCandidate* candidates, uint32_t count, uint32_t head,
                                                    Arena* scratch, bool scalar, CheckActionResult* output, CheckOverloadResult* argument_selected,
                                                    uint64_t* scratch_peak, const CheckConditionTable* conditions, const uint8_t* argument_nonnull)
{
    (void)scalar;

    const CheckOverloadRequest request = {
        .candidates = candidates,
        .count = count,
        .head = head,
        .arguments = action->arguments,
        .argument_count = action->argument_count,
        .context = {
            .semantic = action->context,
            .scratch_peak = scratch_peak,
            .conditions = conditions,
            .argument_nonnull = argument_nonnull,
        },
        .scalar = BUSTER_SCALAR_PATH(scalar),
    };
    CheckOverloadResult selected = head != UINT32_MAX ? check_overload_select(table, &request, scratch) : check_overload_result(CHECK_OVERLOAD_OK);
    if (head == UINT32_MAX)
    {
        TypeId function = action->operands[0].type;
        if (type_is_valid(table, function) && check_action_kind(table, function) == BUSTER_TYPE_KIND_POINTER)
        {
            function = type_element(table, function);
        }
        selected.type = function;
        selected.status = type_is_valid(table, function) && check_action_kind(table, function) == BUSTER_TYPE_KIND_FUNCTION &&
                          (action->argument_count == 0 || action->arguments != 0) ? CHECK_OVERLOAD_OK : CHECK_OVERLOAD_INVALID;
    }
    for (uint32_t i = 0; argument_selected != 0 && i < action->argument_count; i += 1)
    {
        argument_selected[i] = check_overload_result(CHECK_OVERLOAD_INVALID);
    }
    *output = (CheckActionResult){ 0 };
    const uint64_t mark = scratch->length;
    if (selected.status == CHECK_OVERLOAD_OK)
    {
        CheckAction call = *action;
        call.op = CHECK_OP_CALL;
        if (head != UINT32_MAX)
        {
            call.operands[0].type = selected.type;
            call.operands[0].category = CHECK_FUNCTION;
            call.operands[0].object = selected.object;
            call.operands[0].overload_head = 0;
        }
        CheckValue* arguments = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckValue, (uint64_t)call.argument_count + 1);
        CheckConversion* before = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckConversion, (uint64_t)call.argument_count + 1);
        uint8_t* adjusted = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)call.argument_count + 1);
        CheckOverloadResult* identities = argument_selected != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckOverloadResult, (uint64_t)call.argument_count + 1) : 0;
        if (arguments == 0 || before == 0 || adjusted == 0 || (argument_selected != 0 && identities == 0))
        {
            selected.status = CHECK_OVERLOAD_FULL;
        }
        else
        {
            uint32_t parameter_count = 0;
            const TypeId* parameters = type_parameters(table, selected.type, &parameter_count);
            memset(adjusted, 0, (uint64_t)call.argument_count + 1);
            for (uint32_t i = 0; selected.status == CHECK_OVERLOAD_OK && i < call.argument_count; i += 1)
            {
                const CheckOverloadResult resolved = check_overload_argument(table, action->arguments + i, i < parameter_count ? parameters[i] : 0,
                    candidates, count, &request.context, scratch, arguments + i);
                selected.status = resolved.status;
                if (identities != 0)
                {
                    identities[i] = resolved;
                }
                if (selected.status == CHECK_OVERLOAD_OK && i < parameter_count)
                {
                    CheckAction operand = check_overload_action(request.context);
                    CheckActionResult converted = { 0 };
                    operand.operands[0] = arguments[i];
                    CheckValue value = check_action_operand(table, &operand, &converted, 0);
                    before[i] = converted.conversions[0];
                    if (converted.diagnostic != CHECK_DIAG_NONE)
                    {
                        selected.status = converted.diagnostic == CHECK_DIAG_RESOURCE ? CHECK_OVERLOAD_FULL : CHECK_OVERLOAD_INVALID;
                    }
                    else if (check_action_kind(table, value.type) == BUSTER_TYPE_KIND_POINTER && check_action_kind(table, parameters[i]) == BUSTER_TYPE_KIND_POINTER)
                    {
                        const TypeAnswer qualification = check_overload_qualification(table, value.type, parameters[i]);
                        if (qualification == TYPE_ANSWER_FULL)
                        {
                            selected.status = CHECK_OVERLOAD_FULL;
                        }
                        else if (qualification == TYPE_ANSWER_YES)
                        {
                            value.type = parameters[i];
                            arguments[i] = value;
                            adjusted[i] = 1;
                        }
                    }
                }
            }
            if (selected.status == CHECK_OVERLOAD_OK)
            {
                call.arguments = arguments;
                check_action_scalar(table, &call, output);
                for (uint32_t i = 0; i < call.argument_count; i += 1)
                {
                    if (adjusted[i] && action->argument_conversions != 0)
                    {
                        action->argument_conversions[i].flags |= before[i].flags;
                    }
                    if (output->diagnostic == CHECK_DIAG_NONE && argument_selected != 0 && action->arguments[i].overload_head != 0)
                    {
                        argument_selected[i] = identities[i];
                    }
                }
                if (head != UINT32_MAX)
                {
                    // The selected declaration owns the fold permission, even when a newer overload shares its name.
                    check_builtin_library_fold_plain(table, candidates[selected.candidate].library_call, &call, output);
                }
            }
        }
    }
    if (selected.status != CHECK_OVERLOAD_OK)
    {
        output->diagnostic = selected.status == CHECK_OVERLOAD_FULL ? CHECK_DIAG_RESOURCE : CHECK_DIAG_INCOMPATIBLE;
    }
    if (scratch_peak != 0 && scratch->length > *scratch_peak)
    {
        *scratch_peak = scratch->length;
    }
    arena_pop_to(scratch, mark);
    return selected;
}

// A call's candidates' outcomes (CheckOverloadExplained), for the notes of a call that no candidate or several are viable
// for: the selection again, with the checked arguments `arguments`, into `explained` (room for `count`), giving how many.
// A diagnosis's cold path: the call was checked with these values, so the conversions find the types they made then.
static uint32_t check_overload_explain(TypeTable* table, const CheckOverloadCandidate* candidates, uint32_t count, uint32_t head, const CheckValue* arguments,
                                       uint32_t argument_count, const CheckOverloadContext* context, Arena* scratch, CheckOverloadExplained* explained, bool scalar)
{
    uint32_t explained_count = 0;
    const CheckOverloadRequest request = {
        .candidates = candidates,
        .count = count,
        .head = head,
        .arguments = arguments,
        .argument_count = argument_count,
        .context = *context,
        .scalar = BUSTER_SCALAR_PATH(scalar),
        .explained = explained,
        .explained_count = &explained_count,
    };
    const CheckOverloadResult result = check_overload_select(table, &request, scratch);
    return result.status == CHECK_OVERLOAD_NO_MATCH || result.status == CHECK_OVERLOAD_AMBIGUOUS ? explained_count : 0;
}

// check_overload_call_with without conditions (the tests' entry; the checker's calls pass the unit's conditions).
static __attribute__((unused)) CheckOverloadResult check_overload_call(TypeTable* table, const CheckAction* action, const CheckOverloadCandidate* candidates, uint32_t count, uint32_t head,
                                               Arena* scratch, bool scalar, CheckActionResult* output, CheckOverloadResult* argument_selected, uint64_t* scratch_peak)
{
    return check_overload_call_with(table, action, candidates, count, head, scratch, scalar, output, argument_selected, scratch_peak, NULL, NULL);
}

#endif
