// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// diagnose_if's and enable_if's conditions at calls, after checking (#1447, c_check_condition.c): a call whose callee names a
// function with a list evaluates the conditions that need its arguments, each argument's checked value bound to its
// parameter where it is a constant (an integer, a floating value or an absolute address), converted to the parameter's
// type (c_check_condition_eval.c). Included by c_check_warning_graph.c, whose pass over the checked calls calls check_input_condition_call: a unit
// without conditions pays a count test there.

// The parameters' values of call `node` (check_condition_bind), into `bound`, room for `count`, each of the type its
// steps in `entry`'s conditions read it by: each argument's checked value converted to that type where it is a constant;
// an object's or a function's address (`&x`, an array or a function that decays, an address constant the checker marked
// so) is an address that is never null, as clang's evaluation knows. A parameter no step reads stays unknown.
static void check_condition_bind_call(TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node, const CheckConditionTable* table,
                                      const CheckConditionDeclaration* entry, uint32_t count, CheckValue* bound)
{
    const uint32_t begin = graph->argument_offsets[node];
    for (uint32_t p = 0; p < count; p += 1)
    {
        bound[p] = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE };
    }
    for (uint32_t i = 0; i < entry->count; i += 1)
    {
        const CheckCondition* condition = table->conditions + entry->conditions[i];
        for (uint32_t s = 0; s < condition->step_count; s += 1)
        {
            const CheckConditionStep* step = condition->steps + s;
            if (step->op == BUSTER_CHECK_CONDITION_PARAMETER && step->index < count)
            {
                const uint32_t argument = graph->argument_nodes[begin + step->index];
                const CheckValue value = check_stream_value(stream, argument);
                const bool pointer = type_kind(types, type_strip_aligned(types, step->type)) == BUSTER_TYPE_KIND_POINTER;
                const bool nonnull = pointer && value.fault == CHECK_FAULT_NONE &&
                                     (graph->ops[argument] == CHECK_OP_ADDRESS || value.category == CHECK_FUNCTION ||
                                      (value.category == CHECK_LVALUE && type_kind_is_array(type_kind(types, type_strip_aligned(types, value.type)))) ||
                                      (value.constant_kind == CHECK_CONSTANT_ADDRESS && (value.flags & BUSTER_CHECK_NONNULL_ADDRESS) != 0 &&
                                       value.category != CHECK_LVALUE));
                bound[step->index] = check_condition_bind(types, value, nonnull, step->type, node);
            }
        }
    }
}

// The overload call `node`'s selected candidate (CheckStream.overloads, in node order), or NULL where it isn't one or
// none was selected.
static const CheckOverloadResult* check_condition_selected(const CheckStream* stream, uint32_t node)
{
    uint32_t low = 0;
    uint32_t high = stream->overload_count;
    const CheckOverloadResult* result = NULL;
    while (result == NULL && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const uint32_t at = stream->overloads[middle].node;
        result = at == node ? &stream->overloads[middle].selected : NULL;
        high = node < at ? middle : high;
        low = node > at ? middle + 1 : low;
    }
    return result != NULL && result->status == CHECK_OVERLOAD_OK ? result : NULL;
}

// The conditions of call `node`'s callee, where it names a function with a list: an enable_if that doesn't hold, the first
// in the order clang checks them (the latest declaration's first), is "no matching function" at the callee's name; else
// the diagnose_if conditions that need the arguments and hold are diagnosed at the call's `)`. An overload call's callee is
// its selected candidate, whose enable_ifs overload resolution has checked (c_check_overload.c): its diagnose_ifs that
// name no parameter are diagnosed here too, at the callee's name, as clang's use of the selected function gives them.
static void check_input_condition_call(CheckInputReader* reader, const CheckStream* stream, TypeTable* types, uint32_t node)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t callee = graph->operand0[node];
    const uint32_t name = callee < graph->count && graph->ops[callee] == CHECK_OP_VALUE ? graph->tokens[callee] : count;
    const CheckOverloadResult* selected = stream->overload_count != 0 ? check_condition_selected(stream, node) : NULL;
    const uint32_t declaration = selected != NULL ? selected->declaration :
                                 name < count && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER ? input->declarations[name] : count;
    const CheckConditionDeclaration* entry = declaration < count ? check_condition_declaration(input, declaration) : NULL;
    const CheckConditionTable table = check_condition_table(input);
    const uint64_t mark = reader->scratch->length;
    const bool evaluated = entry != NULL && (entry->dependent || (selected != NULL && entry->independent));
    const uint32_t bound_count = graph->argument_counts[node];
    CheckValue* values = evaluated ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, (uint64_t)check_condition_steps_needed(&table, entry) + 1) : NULL;
    CheckValue* bound = values != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, (uint64_t)bound_count + 1) : NULL;
    bool* holds = bound != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, bool, entry->count) : NULL;
    reader->output->resource |= evaluated && holds == NULL;
    if (holds != NULL)
    {
        check_condition_bind_call(types, graph, stream, node, &table, entry, bound_count, bound);
    }
    const uint32_t disabled = holds != NULL && selected == NULL ?
                              check_condition_disabled(types, &table, entry, bound, bound_count, graph->extensions, node, values) : BUSTER_CHECK_NONE;
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
    // An overload call's selected candidate's diagnose_ifs that name no parameter, at the callee's name.
    bool any = false;
    for (uint32_t i = 0; holds != NULL && selected != NULL && i < entry->count; i += 1)
    {
        const CheckCondition* condition = input->conditions + entry->conditions[i];
        holds[i] = condition->kind != CHECK_CONDITION_ENABLE && condition->state == CHECK_CONDITION_TRUE;
        any |= holds[i];
    }
    if (any && name < count)
    {
        check_condition_diagnose(reader, entry, name, holds);
    }
    any = false;
    for (uint32_t i = 0; holds != NULL && disabled == BUSTER_CHECK_NONE && i < entry->count; i += 1)
    {
        const CheckCondition* condition = input->conditions + entry->conditions[i];
        bool known = false;
        holds[i] = condition->kind != CHECK_CONDITION_ENABLE && condition->state == CHECK_CONDITION_DEPENDENT &&
                   check_condition_run(types, condition, bound, bound_count, graph->extensions, node, values, &known);
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
