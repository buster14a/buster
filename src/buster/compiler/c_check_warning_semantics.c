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
