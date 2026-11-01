// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-semantic context shared by ordinary and builtin actions (#1058). Include after c_types.c.
// The record borrows histories and initializer receipts; neither it nor an action owns their storage. The caller
// keeps those views live and unchanged for the action/batch. Copy the record at an action-family handoff, not a
// selected subset of its fields. No context is allocated per graph node and no history array is copied.
//
// source_position is an exclusive source end, never a diagnostic node identity. A null completion history selects
// the direct-action API's current-table contract. A supplied history is bounded, including when the table already
// contains tags completed later in the source. Type interning may grow the table, but not these borrowed histories.
//
// constant_evaluated authorizes source initializer-receipt reads only. It is NOT an operand-evaluation mask or proof
// that a known value is an ICE. Unevaluated/unselected operands, discarded-value evaluation, runtime effects and
// source provenance retain their separate graph/action contracts. In particular, copying a context must not turn
// a runtime atomic/volatile load into a constant or make a computed zero a literal zero.
#ifndef BUSTER_C_CHECK_CONTEXT_INCLUDED
#define BUSTER_C_CHECK_CONTEXT_INCLUDED

struct CheckConstValues;
typedef struct CheckContext CheckContext;
struct CheckContext
{
    const uint32_t* tag_completions;
    const uint32_t* transparent_unions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
    uint32_t fp_environment;
    const struct CheckConstValues* const_values;
    bool constant_evaluated;
};

// Visibility of this nominal type only. Callers walking array/atomic wrappers retain their own type constraints.
static bool check_context_tag_visible(const TypeTable* table, TypeId type, const CheckContext* context)
{
    const uint32_t* const completions = context->tag_completions;
    const TypeId base = type_strip_aligned(table, type);
    const uint8_t kind = type_kind(table, base);
    bool visible = true;
    if (completions != NULL && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM))
    {
        const uint32_t count = context->tag_completion_count;
        const uint32_t position = context->source_position;
        const uint32_t tag = table->types.index[type_index(base)];
        visible = tag < count && completions[tag] <= position;
    }
    return visible;
}

// Complete now AND at the expression's source end, including the nominal element of nested arrays.
static bool check_context_source_complete(const TypeTable* table, TypeId type, const CheckContext* context)
{
    bool complete = type_is_complete(table, type);
    if (complete && context->tag_completions != NULL)
    {
        TypeId base = type_strip_aligned(table, type);
        while (type_kind_is_array(type_kind(table, base)))
        {
            base = type_strip_aligned(table, type_element(table, base));
        }
        complete = check_context_tag_visible(table, base, context);
    }
    return complete;
}

#endif
