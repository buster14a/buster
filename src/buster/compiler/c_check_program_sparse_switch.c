// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Large sparse-switch dispatch for Tier 1 (#1371), included after check_lower_sort_cases. Preparation retains the
// checked, sorted interval slice before any lane runs. Emission uses ordinary typed branches before IR/SSA, with the
// selector already evaluated once in its switch local. Case bodies, source labels and cleanup/lifetime controls stay
// in their original order. No pointer tree, interval expansion or input-dependent recursion is needed.

// Trees are in function/control order, including separate plans for target-clone bodies sharing the same controls.
static const CheckLowerSwitchTree* check_lower_switch_tree_of(const CheckLowerSwitchTrees* trees, uint32_t function, uint32_t control)
{
    const CheckLowerSwitchTree* items = trees->items;
    uint32_t first = 0;
    uint32_t end = trees->count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const bool before = items[middle].function < function || (items[middle].function == function && items[middle].control < control);
        first = before ? middle + 1 : first;
        end = before ? end : middle;
    }
    return first < trees->count && items[first].function == function && items[first].control == control ? items + first : NULL;
}

typedef struct CheckLowerSwitchFrame CheckLowerSwitchFrame;
struct CheckLowerSwitchFrame
{
    uint32_t first;
    uint32_t end;
    uint32_t label;
};

// Split on the first value of the upper half. Every lower interval ends below it: the checker rejected overlaps,
// and preparation removed empty intervals. Each partition takes one comparison; a leaf uses at most four existing
// equality/range checks. The uint32_t interval count bounds the explicit stack by 32, independent of numeric gaps.
static void check_lower_switch_tree(CheckLowerWriter* writer, const CheckLowerSwitchTree* tree, uint32_t token)
{
    CheckLowerSwitchFrame stack[BUSTER_CHECK_LOWER_TREE_DEPTH];
    const CheckProgramCase* items = writer->program->cases;
    const CheckLowerCaseRange* cases = tree->cases;
    uint32_t first = 0;
    uint32_t end = tree->count;
    uint32_t depth = 0;
    bool more = true;
    check_lower_source(writer, token);
    while (more && writer->output->status == CHECK_PROGRAM_OK)
    {
        while (end - first > BUSTER_CHECK_LOWER_TREE_LEAF_CASES && writer->output->status == CHECK_PROGRAM_OK)
        {
            const uint32_t middle = first + (end - first) / 2;
            const CheckProgramCase* pivot = items + cases[middle].item;
            const uint8_t own = check_lower_class(writer, pivot->type);
            const uint8_t class = check_lower_computed(own);
            const uint32_t local = writer->switch_locals[pivot->owner];
            const uint32_t label = check_lower_label(writer);
            if (depth >= BUSTER_CHECK_LOWER_TREE_DEPTH || local == BUSTER_CHECK_NONE || class == UINT8_MAX)
            {
                check_lower_fail(writer, CHECK_PROGRAM_INVALID);
            }
            else
            {
                stack[depth++] = (CheckLowerSwitchFrame){ .first = first, .end = middle, .label = label };
                check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, local);
                const CheckValue lower = { .type = pivot->type, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = pivot->lower };
                check_lower_constant(writer, &lower);
                if (class != own)
                {
                    check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
                }
                check_lower_emit(writer, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
                check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, label);
                first = middle;
            }
        }
        for (uint32_t i = first; i < end && writer->output->status == CHECK_PROGRAM_OK; i += 1)
        {
            check_lower_case(writer, items + cases[i].item);
        }
        check_lower_emit(writer, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, tree->fallback);
        more = depth != 0;
        if (more && writer->output->status == CHECK_PROGRAM_OK)
        {
            const CheckLowerSwitchFrame frame = stack[--depth];
            check_lower_place(writer, frame.label);
            first = frame.first;
            end = frame.end;
        }
    }
}
