// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// diagnose_if's and enable_if's conditions at calls, after checking (#1447, c_check_condition.c): a call whose callee names a
// function with a list evaluates the conditions that need its arguments, each argument's checked value bound to its
// parameter where it is a constant (an integer, a floating value or an absolute address), converted to the parameter's
// type. Included by c_check_warning_graph.c, whose pass over the checked calls calls check_input_condition_call: a unit
// without conditions pays a count test there.

// Whether `condition` holds at call `node` (`*known` false where it can't be evaluated: an argument isn't a constant, or an
// action faults). Its steps run in order on `values`, the scratch room of a value for each.
static bool check_condition_evaluate(TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node, const CheckCondition* condition,
                                     CheckValue* values, bool* known)
{
    const uint32_t begin = graph->argument_offsets[node];
    const uint32_t arguments = graph->argument_counts[node];
    bool valid = true;
    for (uint32_t s = 0; valid && s < condition->step_count; s += 1)
    {
        const CheckConditionStep* step = condition->steps + s;
        if (step->op == BUSTER_CHECK_CONDITION_CONSTANT)
        {
            values[s] = condition->constants[step->index];
        }
        else if (step->op == BUSTER_CHECK_CONDITION_PARAMETER)
        {
            valid = step->index < arguments;
            CheckValue value = valid ? check_stream_value(stream, graph->argument_nodes[begin + step->index]) : (CheckValue){ 0 };
            valid &= value.fault == CHECK_FAULT_NONE && (value.constant_kind == CHECK_CONSTANT_INTEGER || value.constant_kind == CHECK_CONSTANT_FLOATING ||
                                                         (value.constant_kind == CHECK_CONSTANT_ADDRESS && value.object == 0));
            if (valid)
            {
                // A parameter's value: neither an lvalue nor an integer constant expression.
                value.category = CHECK_RVALUE;
                value.flags = 0;
                check_action_convert_value(types, &value, step->type, node);
                values[s] = value;
                valid = value.fault == CHECK_FAULT_NONE && value.constant_kind != CHECK_CONSTANT_NONE;
            }
        }
        else
        {
            CheckAction action = { .op = (CheckOp)step->op, .type = step->type, .node = node, .scalar = true };
            action.context.constant_evaluated = true;
            action.context.extensions = graph->extensions;
            action.operands[0] = values[step->operands[0]];
            action.operands[1] = step->op == CHECK_OP_CAST || step->op == CHECK_OP_NEGATIVE || step->op == CHECK_OP_POSITIVE || step->op == CHECK_OP_NOT ||
                                 step->op == CHECK_OP_BIT_NOT ? (CheckValue){ 0 } : values[step->operands[1]];
            action.operands[2] = step->op == CHECK_OP_CONDITIONAL ? values[step->operands[2]] : (CheckValue){ 0 };
            CheckActionResult result = { 0 };
            check_action_scalar(types, &action, &result);
            values[s] = result.value;
            valid = result.diagnostic == CHECK_DIAG_NONE;
        }
    }
    bool truth = false;
    const CheckValue* last = condition->step_count != 0 ? values + condition->step_count - 1 : NULL;
    *known = valid && last != NULL && last->constant_kind != CHECK_CONSTANT_NONE && check_action_truth(types, last, &truth);
    return *known && truth;
}

// The conditions of call `node`'s callee, where it names a function with a list: an enable_if that doesn't hold, the first
// in the order clang checks them (the latest declaration's first), is "no matching function" at the callee's name; else
// the diagnose_if conditions that need the arguments and hold are diagnosed at the call's `)`.
static void check_input_condition_call(CheckInputReader* reader, const CheckStream* stream, TypeTable* types, uint32_t node)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t callee = graph->operand0[node];
    const uint32_t name = callee < graph->count && graph->ops[callee] == CHECK_OP_VALUE ? graph->tokens[callee] : count;
    const uint32_t declaration = name < count && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER ? input->declarations[name] : count;
    const CheckConditionDeclaration* entry = declaration < count ? check_condition_declaration(input, declaration) : NULL;
    const uint64_t mark = reader->scratch->length;
    uint32_t steps = 0;
    for (uint32_t i = 0; entry != NULL && entry->dependent && i < entry->count; i += 1)
    {
        const uint32_t step_count = input->conditions[entry->conditions[i]].step_count;
        steps = step_count > steps ? step_count : steps;
    }
    CheckValue* values = entry != NULL && entry->dependent ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, (uint64_t)steps + 1) : NULL;
    bool* holds = values != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, bool, entry->count) : NULL;
    reader->output->resource |= entry != NULL && entry->dependent && (values == NULL || holds == NULL);
    uint32_t disabled = BUSTER_CHECK_NONE;
    // The enable_if conditions, each declaration's group from the latest.
    uint32_t group_end = holds != NULL ? entry->count : 0;
    while (disabled == BUSTER_CHECK_NONE && group_end != 0)
    {
        const uint32_t group_declaration = input->conditions[entry->conditions[group_end - 1]].declaration;
        uint32_t group_first = group_end - 1;
        while (group_first > 0 && input->conditions[entry->conditions[group_first - 1]].declaration == group_declaration)
        {
            group_first -= 1;
        }
        for (uint32_t i = group_first; disabled == BUSTER_CHECK_NONE && i < group_end; i += 1)
        {
            const CheckCondition* condition = input->conditions + entry->conditions[i];
            bool known = false;
            const bool admits = condition->kind != CHECK_CONDITION_ENABLE || condition->state == CHECK_CONDITION_TRUE || condition->state == CHECK_CONDITION_OPAQUE ||
                                (condition->state == CHECK_CONDITION_DEPENDENT && check_condition_evaluate(types, graph, stream, node, condition, values, &known));
            disabled = admits ? BUSTER_CHECK_NONE : entry->conditions[i];
        }
        group_end = group_first;
    }
    if (disabled != BUSTER_CHECK_NONE)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, name, CHECK_INPUT_ENABLE_IF_CALL);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].note_token = declaration;
            reader->output->reports[before].detail = input->conditions[disabled].message + 1;
        }
    }
    bool any = false;
    for (uint32_t i = 0; holds != NULL && disabled == BUSTER_CHECK_NONE && i < entry->count; i += 1)
    {
        const CheckCondition* condition = input->conditions + entry->conditions[i];
        bool known = false;
        holds[i] = condition->kind != CHECK_CONDITION_ENABLE && condition->state == CHECK_CONDITION_DEPENDENT &&
                   check_condition_evaluate(types, graph, stream, node, condition, values, &known);
        any |= holds[i];
    }
    const uint32_t open = graph->tokens[node];
    const uint32_t close = open < count && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN ? parser->tree.parents[open] : open;
    if (any && close < count)
    {
        check_condition_diagnose(reader, entry, close, holds);
    }
    arena_pop_to(reader->scratch, mark);
}
