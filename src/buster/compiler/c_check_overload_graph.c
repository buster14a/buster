// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Context flows into a function designator only at its consuming edge. Keep each selected object on that edge;
// changing the shared source leaf would select the wrong function when a graph reuses it in another context.

static bool check_graph_overload_capacity(const CheckGraph* graph, Arena* scratch, uint32_t* capacity, uint64_t* scratch_peak)
{
    *capacity = 0;
    const uint64_t mark = scratch->length;
    uint32_t* heads = graph->overload_candidate_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->count) : 0;
    bool valid = graph->overload_candidate_count == 0 || graph->count == 0 || heads != 0;
    uint64_t count = 0;
    for (uint32_t node = 0; valid && heads != 0 && node < graph->count; node += 1)
    {
        const CheckOp op = (CheckOp)graph->ops[node];
        const uint32_t first = graph->operand0[node];
        const bool preserve = op == CHECK_OP_VALUE || op == CHECK_OP_SELECT || op == CHECK_OP_ADDRESS;
        heads[node] = op == CHECK_OP_VALUE ? graph->leaves[node].overload_head :
                      preserve && first != BUSTER_CHECK_NONE ? heads[first] : 0;
        valid = heads[node] <= graph->overload_candidate_count;
        if (!preserve)
        {
            const uint32_t operands[] = { first, graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = op == CHECK_OP_CALL ? 1 : 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                count += operands[i] != BUSTER_CHECK_NONE && heads[operands[i]] != 0;
            }
            if (check_graph_has_arguments(op))
            {
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    count += heads[graph->argument_nodes[graph->argument_offsets[node] + i]] != 0;
                }
            }
        }
        valid &= count <= UINT32_MAX;
    }
    if (valid)
    {
        *capacity = (uint32_t)count;
    }
    if (scratch_peak != NULL && *scratch_peak < scratch->length)
    {
        *scratch_peak = scratch->length;
    }
    arena_pop_to(scratch, mark);
    return valid;
}

static CheckOverloadContext check_graph_overload_context(const CheckAction* action, CheckStream* stream)
{
    const CheckOverloadContext context = {
        .tag_completions = action->tag_completions,
        .transparent_unions = action->transparent_unions,
        .tag_completion_count = action->tag_completion_count,
        .source_position = action->source_position,
        .extensions = action->extensions,
        .scratch_peak = &stream->statistics.scratch_bytes,
    };
    return context;
}

static bool check_graph_overload_address_append(CheckStream* stream, uint32_t node, uint32_t operand, CheckOverloadResult selected)
{
    const bool valid = stream->overload_address_count < stream->overload_address_capacity;
    if (valid)
    {
        stream->overload_addresses[stream->overload_address_count++] = (CheckOverloadAddressEffect){
            .node = node,
            .operand = operand,
            .selected = selected,
        };
    }
    return valid;
}

static CheckDiagnostic check_graph_overload_operands(TypeTable* types, const CheckGraph* graph, CheckStream* stream, CheckAction* action, Arena* scratch)
{
    CheckDiagnostic diagnostic = CHECK_DIAG_NONE;
    const bool preserve = action->op == CHECK_OP_VALUE || action->op == CHECK_OP_SELECT || action->op == CHECK_OP_ADDRESS;
    const CheckOverloadContext context = check_graph_overload_context(action, stream);
    if (action->op == CHECK_OP_VALUE && action->operands[0].overload_head != 0)
    {
        // Availability belongs to the original use, even when a surrounding generic association is unchosen.
        // Multiple candidates remain unresolved until a consuming edge supplies its actual expected type.
        const CheckOverloadResult selected = check_overload_address(types, graph->overload_candidates, graph->overload_candidate_count,
                                                                    action->operands[0].overload_head - 1, 0, &context, scratch);
        if (selected.status != CHECK_OVERLOAD_OK && selected.status != CHECK_OVERLOAD_AMBIGUOUS)
        {
            diagnostic = selected.status == CHECK_OVERLOAD_FULL ? CHECK_DIAG_RESOURCE : CHECK_DIAG_INCOMPATIBLE;
        }
    }
    const uint32_t count = check_action_operand_count(action);
    for (uint32_t i = 0; !preserve && diagnostic == CHECK_DIAG_NONE && i < count; i += 1)
    {
        if (action->operands[i].overload_head != 0)
        {
            const TypeId target = i == 0 && (action->op == CHECK_OP_CAST || action->op == CHECK_OP_INITIALIZE || action->op == CHECK_OP_CONVERT) ? action->type :
                                  i == 1 && action->op == CHECK_OP_ASSIGN ? action->operands[0].type : 0;
            CheckValue value = { 0 };
            const CheckOverloadResult selected = check_overload_argument(types, action->operands + i, target, graph->overload_candidates,
                                                                         graph->overload_candidate_count, &context, scratch, &value);
            if (selected.status != CHECK_OVERLOAD_OK)
            {
                diagnostic = selected.status == CHECK_OVERLOAD_FULL ? CHECK_DIAG_RESOURCE : CHECK_DIAG_INCOMPATIBLE;
            }
            else if (!check_graph_overload_address_append(stream, action->node, i, selected))
            {
                diagnostic = CHECK_DIAG_RESOURCE;
            }
            else
            {
                action->operands[i] = value;
            }
        }
    }
    for (uint32_t i = 0; diagnostic == CHECK_DIAG_NONE && action->arguments != 0 && i < action->argument_count; i += 1)
    {
        if (action->arguments[i].overload_head != 0)
        {
            CheckValue value = { 0 };
            const CheckOverloadResult selected = check_overload_argument(types, action->arguments + i, 0, graph->overload_candidates,
                                                                         graph->overload_candidate_count, &context, scratch, &value);
            if (selected.status != CHECK_OVERLOAD_OK)
            {
                diagnostic = selected.status == CHECK_OVERLOAD_FULL ? CHECK_DIAG_RESOURCE : CHECK_DIAG_INCOMPATIBLE;
            }
            else if (!check_graph_overload_address_append(stream, action->node, i + 3, selected))
            {
                diagnostic = CHECK_DIAG_RESOURCE;
            }
            else
            {
                // Graph action arguments are the caller's bounded scratch gather, never the immutable input.
                ((CheckValue*)action->arguments)[i] = value;
            }
        }
    }
    return diagnostic;
}

static CheckOverloadResult check_graph_overload_call(TypeTable* types, const CheckGraph* graph, CheckStream* stream, CheckAction* action,
                                                     uint32_t head, Arena* scratch, bool scalar, CheckActionResult* result)
{
    const uint64_t mark = scratch->length;
    bool pending = false;
    for (uint32_t i = 0; i < action->argument_count; i += 1)
    {
        pending |= action->arguments[i].overload_head != 0;
    }
    CheckOverloadResult* arguments = pending ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckOverloadResult, action->argument_count) : 0;
    CheckOverloadResult selected = check_overload_result(CHECK_OVERLOAD_FULL);
    if (!pending || arguments != 0)
    {
        selected = check_overload_call(types, action, graph->overload_candidates, graph->overload_candidate_count,
                                      head, scratch, scalar, result, arguments, &stream->statistics.scratch_bytes);
        for (uint32_t i = 0; arguments != 0 && i < action->argument_count; i += 1)
        {
            if (arguments[i].status == CHECK_OVERLOAD_OK && arguments[i].candidate != BUSTER_CHECK_NONE &&
                !check_graph_overload_address_append(stream, action->node, i + 3, arguments[i]))
            {
                selected.status = CHECK_OVERLOAD_FULL;
                result->diagnostic = CHECK_DIAG_RESOURCE;
            }
        }
    }
    else
    {
        *result = (CheckActionResult){ .diagnostic = CHECK_DIAG_RESOURCE };
    }
    if (scratch->length > stream->statistics.scratch_bytes)
    {
        stream->statistics.scratch_bytes = scratch->length;
    }
    arena_pop_to(scratch, mark);
    return selected;
}
