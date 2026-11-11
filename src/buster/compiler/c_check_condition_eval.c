// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// diagnose_if's and enable_if's conditions as data, and their evaluation with a call's arguments (#1447). The reader
// builds them (c_check_condition.c); the calls' pass after checking (c_check_condition_graph.c) and overload resolution
// during it (c_check_overload.c, which clang's enable_if takes part in) evaluate them, the same steps over values bound
// to the parameters. Include after c_check_actions.c and before c_check_overload.c. Clang is the external oracle of the
// behavior; no clang code is incorporated.
#ifndef BUSTER_C_CHECK_CONDITION_EVAL_INCLUDED
#define BUSTER_C_CHECK_CONDITION_EVAL_INCLUDED

#define BUSTER_CHECK_CONDITION_CONSTANT 0xffffffffu
#define BUSTER_CHECK_CONDITION_PARAMETER 0xfffffffeu
#define BUSTER_CHECK_CONDITION_COMMA 0xfffffffdu

typedef enum CheckConditionKind
{
    CHECK_CONDITION_WARNING,
    CHECK_CONDITION_ERROR,
    CHECK_CONDITION_ENABLE,
} CheckConditionKind;

typedef enum CheckConditionState
{
    // A condition that names no parameter, false or true once and for all.
    CHECK_CONDITION_FALSE,
    CHECK_CONDITION_TRUE,
    // A condition each call evaluates with its arguments (CheckCondition.steps).
    CHECK_CONDITION_DEPENDENT,
    // A system header's condition whose shape the steps don't take: it decides nothing.
    CHECK_CONDITION_OPAQUE,
} CheckConditionState;

// One step of a condition's program, in postorder: a constant (`index` into CheckCondition.constants), a parameter (its
// place `index`, of type `type`) or a CheckOp on earlier steps' values (`operands`, a cast's destination `type`).
typedef struct CheckConditionStep CheckConditionStep;
struct CheckConditionStep
{
    uint32_t op;
    uint32_t operands[3];
    uint32_t index;
    TypeId type;
};

typedef struct CheckCondition CheckCondition;
struct CheckCondition
{
    const CheckConditionStep* steps;
    const CheckValue* constants;
    uint32_t step_count;
    // The attribute's first token (its note's place), the message's first string token and the declaration's name.
    uint32_t attribute;
    uint32_t message;
    uint32_t declaration;
    // The condition's tokens [first, end), the place among its function's parameters of the one each names
    // (UINT32_MAX for a token that names none, null for a condition of none) and its function's type: what the reader
    // compares conditions by (check_condition_profile).
    uint32_t first;
    uint32_t end;
    const uint32_t* places;
    TypeId function;
    // An overloadable function's enable_if's argument in its Itanium name (c_check_condition_mangle.c), null where its
    // shape has none, or for another condition.
    const char* mangled;
    uint32_t mangled_length;
    // The first condition of the unit (its index in CheckInput.conditions) that clang's profile of the expression makes
    // the same as this one: the same tree, literals by value and type, parameters by place and type (compareEnableIfAttrs).
    uint32_t profile;
    // diagnose_if's warning group (CheckInputAttribute.warning_group): its index in pp_features_warning plus one, or 0.
    uint16_t group;
    uint8_t kind;
    uint8_t state;
};

typedef struct CheckConditionDeclaration CheckConditionDeclaration;
struct CheckConditionDeclaration
{
    uint32_t declaration;
    uint32_t count;
    const uint32_t* conditions;
    // Whether some diagnose_if holds at every use, some enable_if isn't always true, and some condition needs a call's
    // arguments.
    bool independent;
    bool restricted;
    bool dependent;
};

// The unit's conditions and its declarations' lists, in ascending order of the declarations' name tokens, as the reader
// has them (CheckInput's, mirrored in CheckGraph.conditions for the checker's overload resolution).
typedef struct CheckConditionTable CheckConditionTable;
struct CheckConditionTable
{
    const CheckCondition* conditions;
    const CheckConditionDeclaration* declarations;
    uint32_t declaration_count;
};

// The list of declaration `declaration` (by binary search), or NULL.
static const CheckConditionDeclaration* check_condition_find(const CheckConditionTable* table, uint32_t declaration)
{
    uint32_t low = 0;
    uint32_t high = table != NULL ? table->declaration_count : 0;
    const CheckConditionDeclaration* result = NULL;
    while (result == NULL && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const uint32_t at = table->declarations[middle].declaration;
        result = at == declaration ? table->declarations + middle : NULL;
        high = declaration < at ? middle : high;
        low = declaration > at ? middle + 1 : low;
    }
    return result;
}

// Whether `value` is an address that is never null (an object's or a function's), and whether it is a null pointer: an
// integer zero or a null address.
static bool check_condition_nonnull(const CheckValue* value)
{
    return value->constant_kind == CHECK_CONSTANT_ADDRESS && (value->flags & BUSTER_CHECK_NONNULL_ADDRESS) != 0 && value->category != CHECK_LVALUE &&
           value->fault == CHECK_FAULT_NONE;
}

static bool check_condition_null(const TypeTable* types, const CheckValue* value)
{
    bool truth = true;
    const bool zero = value->constant_kind == CHECK_CONSTANT_INTEGER && check_action_truth(types, value, &truth) && !truth;
    const bool address = value->constant_kind == CHECK_CONSTANT_ADDRESS && value->object == 0 && value->offset == 0 &&
                         (value->flags & BUSTER_CHECK_NONNULL_ADDRESS) == 0 && value->fault == CHECK_FAULT_NONE;
    return zero || address;
}

// An argument's checked value `value` bound to a parameter of type `type`: converted where it is a constant (an integer, a
// floating value or an absolute address, not an lvalue), an address that is never null where `nonnull` says it is one (an object's or a
// function's, whose value is otherwise unknown), else an unknown value.
static CheckValue check_condition_bind(TypeTable* types, CheckValue value, bool nonnull, TypeId type, uint32_t node)
{
    // (An lvalue's value is no constant: a pointer variable's address constant is its own object's, not its value.)
    const bool constant = !nonnull && value.fault == CHECK_FAULT_NONE && value.category != CHECK_LVALUE &&
                          (value.constant_kind == CHECK_CONSTANT_INTEGER || value.constant_kind == CHECK_CONSTANT_FLOATING ||
                           (value.constant_kind == CHECK_CONSTANT_ADDRESS && value.object == 0));
    if (constant)
    {
        // A parameter's value: neither an lvalue nor an integer constant expression.
        value.category = CHECK_RVALUE;
        value.flags = 0;
        check_action_convert_value(types, &value, type, node);
    }
    // Where it isn't a constant, its value is unknown: an operator that doesn't read it (a `&&` whose left operand is
    // false) still gives one.
    return constant && value.fault == CHECK_FAULT_NONE ? value :
           (CheckValue){ .type = type, .category = CHECK_RVALUE, .constant_kind = nonnull ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_NONE,
                         .flags = nonnull ? BUSTER_CHECK_NONNULL_ADDRESS : 0 };
}

// Whether `condition` holds with the parameters' values `bound` (`count` of them, check_condition_bind's), `*known` false
// where it can't be evaluated: a value it reads is unknown, or an action faults. Its steps run in order on `values`, the
// scratch room of a value for each; the actions are checked with the call's `extensions` at its `node`.
static bool check_condition_run(TypeTable* types, const CheckCondition* condition, const CheckValue* bound, uint32_t count, uint32_t extensions, uint32_t node,
                                CheckValue* values, bool* known)
{
    bool valid = true;
    for (uint32_t s = 0; valid && s < condition->step_count; s += 1)
    {
        const CheckConditionStep* step = condition->steps + s;
        if (step->op == BUSTER_CHECK_CONDITION_CONSTANT)
        {
            values[s] = condition->constants[step->index];
        }
        else if (step->op == BUSTER_CHECK_CONDITION_COMMA)
        {
            // The right operand's value, where the left one's is known.
            values[s] = values[step->operands[0]].constant_kind != CHECK_CONSTANT_NONE ? values[step->operands[1]] :
                        (CheckValue){ .type = values[step->operands[1]].type, .category = CHECK_RVALUE };
        }
        else if (step->op == BUSTER_CHECK_CONDITION_PARAMETER)
        {
            valid = step->index < count;
            values[s] = valid ? bound[step->index] : (CheckValue){ 0 };
        }
        else
        {
            CheckAction action = { .op = (CheckOp)step->op, .type = step->type, .node = node, .scalar = true };
            action.context.constant_evaluated = true;
            action.context.extensions = extensions;
            const bool unary = step->op == CHECK_OP_CAST || step->op == CHECK_OP_NEGATIVE || step->op == CHECK_OP_POSITIVE || step->op == CHECK_OP_NOT ||
                               step->op == CHECK_OP_BIT_NOT;
            action.operands[0] = values[step->operands[0]];
            action.operands[1] = unary ? (CheckValue){ 0 } : values[step->operands[1]];
            action.operands[2] = step->op == CHECK_OP_CONDITIONAL ? values[step->operands[2]] : (CheckValue){ 0 };
            // An address that is never null against a null pointer, which the actions can't tell apart from a null address
            // (the unknown address has no object): decided here.
            const bool equality = step->op == CHECK_OP_EQUAL || step->op == CHECK_OP_NOT_EQUAL;
            const bool decided = equality && ((check_condition_nonnull(&action.operands[0]) && check_condition_null(types, &action.operands[1])) ||
                                              (check_condition_nonnull(&action.operands[1]) && check_condition_null(types, &action.operands[0])));
            // Any other action on such an address but its truth's (`!`, `&&`, `||`, a conditional's condition) and a cast to
            // another pointer has no known value.
            const bool logical = step->op == CHECK_OP_NOT || step->op == CHECK_OP_LOGICAL_AND || step->op == CHECK_OP_LOGICAL_OR || step->op == CHECK_OP_CONDITIONAL;
            const bool addressed = check_condition_nonnull(&action.operands[0]) || (!unary && check_condition_nonnull(&action.operands[1])) ||
                                   (step->op == CHECK_OP_CONDITIONAL && check_condition_nonnull(&action.operands[2]));
            const bool pointer_cast = step->op == CHECK_OP_CAST && type_kind(types, type_strip_aligned(types, step->type)) == BUSTER_TYPE_KIND_POINTER;
            CheckActionResult result = { 0 };
            if (decided)
            {
                result.value = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER,
                                             .integer = step->op == CHECK_OP_NOT_EQUAL };
            }
            else if (addressed && pointer_cast)
            {
                result.value = action.operands[0];
                result.value.type = step->type;
            }
            else if (addressed && !logical)
            {
                result.value = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE };
            }
            else
            {
                check_action_scalar(types, &action, &result);
            }
            values[s] = result.value;
            valid = result.diagnostic == CHECK_DIAG_NONE;
        }
    }
    bool truth = false;
    const CheckValue* last = condition->step_count != 0 ? values + condition->step_count - 1 : NULL;
    *known = valid && last != NULL && last->constant_kind != CHECK_CONSTANT_NONE && check_action_truth(types, last, &truth);
    return *known && truth;
}

// The steps' room a list's conditions need at most (the longest program), for check_condition_run's `values`.
static uint32_t check_condition_steps_needed(const CheckConditionTable* table, const CheckConditionDeclaration* entry)
{
    uint32_t steps = 0;
    for (uint32_t i = 0; i < entry->count; i += 1)
    {
        const uint32_t step_count = table->conditions[entry->conditions[i]].step_count;
        steps = step_count > steps ? step_count : steps;
    }
    return steps;
}

// The first of `entry`'s enable_if conditions that doesn't admit a call whose parameters' values are `bound`, in the order
// clang checks them (the latest declaration's first, each declaration's in source order): its index in the unit's
// conditions, or UINT32_MAX where all admit it. An unknown condition doesn't admit it, as clang's
// EvaluateWithSubstitution's failure doesn't; an opaque one (a system header's) does.
static uint32_t check_condition_disabled(TypeTable* types, const CheckConditionTable* table, const CheckConditionDeclaration* entry, const CheckValue* bound,
                                         uint32_t count, uint32_t extensions, uint32_t node, CheckValue* values)
{
    const CheckCondition* conditions = table->conditions;
    uint32_t disabled = UINT32_MAX;
    uint32_t group_end = entry->restricted ? entry->count : 0;
    while (disabled == UINT32_MAX && group_end != 0)
    {
        const uint32_t group_declaration = conditions[entry->conditions[group_end - 1]].declaration;
        uint32_t group_first = group_end - 1;
        while (group_first > 0 && conditions[entry->conditions[group_first - 1]].declaration == group_declaration)
        {
            group_first -= 1;
        }
        for (uint32_t i = group_first; disabled == UINT32_MAX && i < group_end; i += 1)
        {
            const CheckCondition* condition = conditions + entry->conditions[i];
            bool known = false;
            const bool admits = condition->kind != CHECK_CONDITION_ENABLE || condition->state == CHECK_CONDITION_TRUE || condition->state == CHECK_CONDITION_OPAQUE ||
                                (condition->state == CHECK_CONDITION_DEPENDENT && check_condition_run(types, condition, bound, count, extensions, node, values, &known));
            disabled = admits ? UINT32_MAX : entry->conditions[i];
        }
        group_end = group_first;
    }
    return disabled;
}

// clang's compareEnableIfAttrs of two candidates' lists (NULL for none): 1 where `left`'s enable_ifs are `right`'s in
// declaration order with more after them, 2 where `right`'s are `left`'s with more, 3 where they differ, 0 where they are
// the same. Each list holds its redeclarations' conditions in source order, which is clang's declaration order.
static uint32_t check_condition_compare_enable(const CheckConditionTable* table, const CheckConditionDeclaration* left, const CheckConditionDeclaration* right)
{
    uint32_t li = 0;
    uint32_t ri = 0;
    const uint32_t lc = left != NULL ? left->count : 0;
    const uint32_t rc = right != NULL ? right->count : 0;
    uint32_t order = 0;
    bool walking = true;
    while (walking)
    {
        while (li < lc && table->conditions[left->conditions[li]].kind != CHECK_CONDITION_ENABLE)
        {
            li += 1;
        }
        while (ri < rc && table->conditions[right->conditions[ri]].kind != CHECK_CONDITION_ENABLE)
        {
            ri += 1;
        }
        const bool lp = li < lc;
        const bool rp = ri < rc;
        walking = lp && rp && table->conditions[left->conditions[li]].profile == table->conditions[right->conditions[ri]].profile;
        order = lp && rp && !walking ? 3u : lp && !rp ? 1u : !lp && rp ? 2u : order;
        li += 1;
        ri += 1;
    }
    return order;
}

#endif
