// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-constant eligibility and provenance transfers (#1058). Include after CheckValue/CheckOp and the basic
// action type queries. These helpers do not evaluate an operand, choose a branch or grant receipt access. The
// caller makes those decisions first, then transfers the facts of precisely the operands that were evaluated.
//
// CheckConstantKind describes a known payload, not source eligibility. ICE/GNU forms describe admissible syntax;
// effects and deferred faults remain independent. LITERAL_ZERO is a spelling witness, not an integer comparison.
// A builtin may explicitly certify its own folded call as an ICE, but a conversion or receipt read may not infer
// that certification from known bits. No helper allocates, owns a history, or extends the per-node value record.
#ifndef BUSTER_C_CHECK_PROVENANCE_INCLUDED
#define BUSTER_C_CHECK_PROVENANCE_INCLUDED

// Shared prerequisite, not a complete rule for every consumer: integer contexts still require an admissible
// integer form, static initializers still require representable/static addresses, and vector writers still check
// their lane's representation. FP policy is applied by the action, not guessed from this value's payload.
static bool check_value_evaluated_constant(const CheckValue* value)
{
    const bool eligible = value->constant_kind != CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_NONE &&
                          (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_DISCARDED_UNEVALUABLE)) == 0;
    return eligible;
}

static bool check_value_static_initializer(const CheckValue* value, bool bit_field)
{
    const bool address = value->constant_kind == CHECK_CONSTANT_ADDRESS && value->object != 0;
    const bool eligible = check_value_evaluated_constant(value) &&
                          (!address || ((value->flags & BUSTER_CHECK_STATIC_OBJECT) != 0 && !bit_field));
    return eligible;
}

// Only source-transparent VALUE/SELECT nodes preserve literal spelling. Parentheses and selected generic
// associations use those nodes; arithmetic, casts, calls, conditional expressions and receipt reads do not.
static uint32_t check_value_source_flags(CheckOp op, uint32_t flags)
{
    const uint32_t result = op == CHECK_OP_VALUE || op == CHECK_OP_SELECT ? flags : flags & ~BUSTER_CHECK_LITERAL_ZERO;
    return result;
}

// A receipt proves the initialized value, never a source literal, null-pointer syntax, or standard ICE.
// Symbolic pointer identity and its later-read restrictions survive; an integer payload inherits none of them.
static uint32_t check_value_receipt_flags(const CheckValue* value)
{
    const uint32_t pointer = value->constant_kind == CHECK_CONSTANT_ADDRESS ?
                             value->flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED) : 0;
    const uint32_t flags = (value->flags & (BUSTER_CHECK_GNU_DIVISION | BUSTER_CHECK_GNU_FLOAT_RANGE)) | pointer |
                           BUSTER_CHECK_GNU_OBJECT_READ | BUSTER_CHECK_GNU_CONSTANT;
    return flags;
}

// The family must first certify that its builtin permits this fold as an ICE. This is not a general "known means
// ICE" rule. Evaluated operands have already supplied their effects/faults/provenance through check_action_inherit.
static void check_action_builtin_ice(CheckValue* value)
{
    value->flags |= (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 ? BUSTER_CHECK_ICE : 0;
}

static void check_action_set_fault(CheckValue* value, CheckFault fault, uint32_t node)
{
    if (value->fault == CHECK_FAULT_NONE)
    {
        value->fault = fault;
        value->fault_node = node;
    }
    value->constant_kind = CHECK_CONSTANT_NONE;
    value->integer = 0;
    value->floating = 0;
    value->imaginary = 0;
}

static void check_action_inherit(CheckValue* value, const CheckValue* operand)
{
    value->flags |= operand->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_FP_ENVIRONMENT | BUSTER_CHECK_GNU_DIVISION |
                                      BUSTER_CHECK_GNU_FLOAT_RANGE | BUSTER_CHECK_GNU_OBJECT_READ | BUSTER_CHECK_INACTIVE_UNION_READ |
                                      BUSTER_CHECK_MODIFICATION | BUSTER_CHECK_DISCARDED_UNEVALUABLE);
    if (value->fault == CHECK_FAULT_NONE && operand->fault != CHECK_FAULT_NONE)
    {
        value->fault = operand->fault;
        value->fault_node = operand->fault_node;
    }
}

// Whether clang's evaluator evaluates a discarded operand (a comma's left one, a cast to void's): a constant, or a void
// expression without an operand it can't evaluate, and no modification (BUSTER_CHECK_DISCARDED_UNEVALUABLE).
static bool check_action_discard_evaluable(const TypeTable* table, const CheckValue* value)
{
    const bool known = value->constant_kind != CHECK_CONSTANT_NONE ||
                       (check_action_kind(table, value->type) == BUSTER_TYPE_KIND_VOID &&
                        (value->flags & BUSTER_CHECK_DISCARDED_UNEVALUABLE) == 0);
    return known && (value->flags & BUSTER_CHECK_MODIFICATION) == 0;
}

static bool check_action_integer_constant(const CheckValue* value, uint32_t extensions)
{
    return check_value_evaluated_constant(value) && value->constant_kind == CHECK_CONSTANT_INTEGER &&
           ((value->flags & BUSTER_CHECK_ICE) != 0 || (extensions & BUSTER_CHECK_EXTENSION_GNU_CONSTANT) != 0);
}

// The caller has already selected and converted the arms' common type. Never inherit the unselected arm's
// deferred fault/effects merely because it was type-checked; its admissible unevaluated syntax is still consulted.
static void check_action_conditional_provenance(const TypeTable* table, CheckValue* value, TypeId common,
                                                const CheckValue* condition, const CheckValue* left, const CheckValue* right,
                                                bool known, bool truth)
{
    if (known)
    {
        *value = truth ? *left : *right;
    }
    else
    {
        value->type = common;
        check_action_inherit(value, left);
        check_action_inherit(value, right);
    }
    value->category = CHECK_RVALUE;
    value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER);
    const bool left_ice = (left->flags & BUSTER_CHECK_ICE) != 0 ||
                          (known && !truth && (left->flags & (BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM)) != 0);
    const bool right_ice = (right->flags & BUSTER_CHECK_ICE) != 0 ||
                           (known && truth && (right->flags & (BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM)) != 0);
    const bool ice = (condition->flags & BUSTER_CHECK_ICE) != 0 && left_ice && right_ice;
    const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
    const bool left_form = (left->flags & forms) != 0 || (known && !truth && (left->flags & BUSTER_CHECK_ICE_UNEVALUATED) != 0);
    const bool right_form = (right->flags & forms) != 0 || (known && truth && (right->flags & BUSTER_CHECK_ICE_UNEVALUATED) != 0);
    const bool gnu_ice = (condition->flags & forms) != 0 && left_form && right_form;
    value->flags |= check_action_is_integer(table, common) ? (ice ? BUSTER_CHECK_ICE : (gnu_ice ? BUSTER_CHECK_GNU_ICE_FORM : 0)) : 0;
    check_action_inherit(value, condition);
}

// A comma's value is its right operand, but both operands are evaluated. Source evaluation records an unevaluable
// discarded operand and keeps only the right's modification provenance; runtime evaluation keeps both effects.
static void check_action_comma_provenance(const TypeTable* table, CheckValue* value, const CheckValue* left,
                                          const CheckValue* right, bool constant_evaluated)
{
    *value = *right;
    value->category = CHECK_RVALUE;
    value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
    const uint32_t allowed = BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM;
    value->flags |= (left->flags & allowed) != 0 && (right->flags & allowed) != 0 ? BUSTER_CHECK_ICE_UNEVALUATED : 0;
    if (left->fault != CHECK_FAULT_NONE)
    {
        value->fault = left->fault;
        value->fault_node = left->fault_node;
    }
    check_action_inherit(value, left);
    if (constant_evaluated)
    {
        value->flags = (value->flags & ~BUSTER_CHECK_MODIFICATION) | (right->flags & BUSTER_CHECK_MODIFICATION) |
                       (check_action_discard_evaluable(table, left) ? 0 : BUSTER_CHECK_DISCARDED_UNEVALUABLE);
    }
}

// Type/representation conversion has finished. The producer computed form witnesses from the original operand,
// before conversion erased its type; saturation still vetoes an otherwise valid integer-form witness.
static void check_action_cast_provenance(CheckValue* value, bool ice, bool gnu_ice, bool null, bool is_void, bool discarded)
{
    value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
    const bool saturated = (value->flags & BUSTER_CHECK_GNU_FLOAT_RANGE) != 0;
    value->flags |= (saturated ? 0 : ice ? BUSTER_CHECK_ICE : (gnu_ice ? BUSTER_CHECK_GNU_ICE_FORM : 0)) | (null ? BUSTER_CHECK_NULL_POINTER : 0);
    if (is_void)
    {
        value->constant_kind = CHECK_CONSTANT_NONE;
        value->flags = (value->flags & ~BUSTER_CHECK_MODIFICATION) | (discarded ? 0 : BUSTER_CHECK_DISCARDED_UNEVALUABLE);
    }
}

// Taking an inactive member's address is not reading that member. Dereferencing it restores the read restriction;
// neither transition clears a restricted pointer path, nor certifies that an automatic address is a static one.
static void check_action_address_provenance(CheckValue* value, bool constant_evaluated)
{
    value->flags |= (value->flags & BUSTER_CHECK_INACTIVE_UNION_READ) != 0 ? BUSTER_CHECK_INACTIVE_UNION_ADDRESS : 0;
    value->flags &= ~(BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL |
                      BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_VOLATILE_PART);
    if (!constant_evaluated && (value->flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && value->constant_kind == CHECK_CONSTANT_ADDRESS && value->object != 0)
    {
        value->constant_kind = CHECK_CONSTANT_NONE;
    }
}

static void check_action_dereference_provenance(CheckValue* value)
{
    value->flags |= (value->flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
    value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
}

#endif
