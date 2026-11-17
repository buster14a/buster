// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Warning facts from already checked scalar contracts. These helpers neither choose source policy nor write a
// dense warning bit. The source adapter supplies expression spelling, unevaluated context and exact completion.

static bool check_warning_null_integer(const TypeTable* types, TypeId destination, const CheckValue* value)
{
    const bool result = type_is_valid(types, destination) && check_action_kind(types, destination) == BUSTER_TYPE_KIND_POINTER &&
                        type_is_valid(types, value->type) && check_action_is_integer(types, value->type) &&
                        type_unqualified(type_strip_aligned(types, value->type)) != BUSTER_TYPE_BOOL &&
                        value->constant_kind == CHECK_CONSTANT_INTEGER && value->integer == 0 && value->fault == CHECK_FAULT_NONE &&
                        (value->flags & BUSTER_CHECK_ICE) != 0;
    return result;
}

static bool check_warning_ordered_event(TypeTable* types, const CheckAction* action, const CheckActionResult* checked,
                                        uint32_t token, uint32_t completion, CheckWarningEvent* event)
{
    bool result = false;
    if (checked->diagnostic == CHECK_DIAG_NONE && action->op >= CHECK_OP_LESS && action->op <= CHECK_OP_GREATER_EQUAL)
    {
        CheckActionResult operands = { 0 };
        const CheckValue left = check_action_operand(types, action, &operands, 0);
        const CheckValue right = check_action_operand(types, action, &operands, 1);
        const bool lp = type_is_valid(types, left.type) && check_action_kind(types, left.type) == BUSTER_TYPE_KIND_POINTER;
        const bool rp = type_is_valid(types, right.type) && check_action_kind(types, right.type) == BUSTER_TYPE_KIND_POINTER;
        const CheckValue* integer = lp ? &right : &left;
        result = operands.diagnostic == CHECK_DIAG_NONE && lp != rp && check_action_is_integer(types, integer->type) &&
                 !check_action_null(types, integer, 0);
        if (result)
        {
            *event = (CheckWarningEvent){
                .id = CHECK_WARNING_ID_ORDERED_POINTER_INTEGER,
                .kind = CHECK_WARNING_EVENT_ORDERED_POINTER_INTEGER,
                .token = token,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = completion,
                .payload = { .first_type = left.type, .second_type = right.type, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
        }
    }
    return result;
}

// Construction-time logical diagnostics use the right operand's independently evaluable integer value. Runtime
// reachability does not suppress them. The adapter owns bare-enumerator and macro-operator spelling exclusions.
static __attribute__((unused)) bool check_warning_constant_logical_event(TypeTable* types, const CheckAction* action, const CheckActionResult* checked,
                                                  uint32_t token, uint32_t completion, CheckWarningEvent* event)
{
    bool result = false;
    if (checked->diagnostic == CHECK_DIAG_NONE && (action->op == CHECK_OP_LOGICAL_AND || action->op == CHECK_OP_LOGICAL_OR))
    {
        const TypeId left = action->operands[0].type;
        const TypeId right = action->operands[1].type;
        const bool integers = type_is_valid(types, left) && type_is_valid(types, right) &&
                              check_action_is_integer(types, left) && check_action_is_integer(types, right) &&
                              type_unqualified(type_strip_aligned(types, left)) != BUSTER_TYPE_BOOL;
        if (integers)
        {
            CheckAction probe = *action;
            probe.context.constant_evaluated = true;
            CheckActionResult converted = { 0 };
            const CheckValue value = check_action_operand(types, &probe, &converted, 1);
            bool boolean = value.integer == 0 || value.integer == 1;
            uint32_t width = 0;
            bool sign = false;
            if (value.constant_kind == CHECK_CONSTANT_INTEGER && check_wide_type(types, value.type, &width, &sign))
            {
                const uint32_t count = check_wide_count(width);
                const uint64_t* limbs = check_wide_limbs(&value, count);
                boolean = limbs == NULL || limbs[0] <= 1;
                for (uint32_t i = 1; limbs != NULL && boolean && i < count; i += 1)
                {
                    boolean = limbs[i] == 0;
                }
            }
            result = converted.diagnostic == CHECK_DIAG_NONE && value.constant_kind == CHECK_CONSTANT_INTEGER &&
                     check_value_evaluated_constant(&value) && (value.flags & BUSTER_CHECK_INVALID_CONSTANT) == 0 && !boolean;
            if (result)
            {
                *event = (CheckWarningEvent)
                {
                    .id = CHECK_WARNING_ID_CONSTANT_LOGICAL_OPERAND,
                    .kind = CHECK_WARNING_EVENT_CONSTANT_LOGICAL_OPERAND,
                    .token = token,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = completion,
                    // Building the logical expression precedes an enclosing assertion's failure at the same boundary.
                    .suborder = 1,
                    .payload =
                    {
                        .data = action->op == CHECK_OP_LOGICAL_OR,
                        .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                    },
                };
            }
        }
    }
    return result;
}
