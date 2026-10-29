// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Resolve pending runtime range diagnostics on the checked control/expression graph, before lowering. This cold
// worklist exists only for an out-of-range immediate. Each control/node enters it once, with no recursion, token
// reparsing or dependence on target features. Unknown conditions retain both edges. Scratch has exactly one byte
// and one u64 slot per control/node; no padding is read. Failure publishes no partially resolved events.
// Code generation's check of target features walks the same way (#775, c_check_target_feature.c): with `calls`, the
// walk also appends each call it reaches to that arena, a uint64_t each of its node in the low half and the walk's `tag`
// in the high, `call_count` of them, in a run nothing else pushes into while it walks; `call_room` turns false if one had
// no room. Each item pushed carries `tag` in its high half too, for the check's loop to set it from (the statement whose
// control reached an expression); this walk's is 0, so that its items are the bare indices.

typedef struct CheckX86DiagnosticWalk CheckX86DiagnosticWalk;
struct CheckX86DiagnosticWalk
{
    CheckProgram* program;
    const CheckProgramBody* body;
    uint8_t* seen;
    uint64_t* stack;
    uint64_t depth;
    Arena* calls;
    uint32_t call_count;
    bool call_room;
    uint32_t tag;
};

static void check_x86_diagnostic_push(CheckX86DiagnosticWalk* walk, uint32_t index, bool control)
{
    const uint32_t nodes = walk->program->input->graph.count;
    const bool valid = control ? index < walk->body->count : index < nodes;
    const uint64_t item = control ? (uint64_t)nodes + walk->body->first + index : index;
    if (valid && walk->seen[item] == 0)
    {
        walk->seen[item] = 1;
        walk->stack[walk->depth++] = item | (uint64_t)walk->tag << 32;
    }
}

static void check_x86_diagnostic_switch(CheckX86DiagnosticWalk* walk, uint32_t index)
{
    const CheckProgram* program = walk->program;
    const CheckControl* controls = program->controls + walk->body->first;
    const uint32_t node = controls[index].node;
    uint32_t next = index + 1;
    uint32_t selected = BUSTER_CHECK_NONE;
    unsigned __int128 value = 0;
    const bool known = check_program_case_value(program, node, program->stream->values[node].type, &value);
    while (next < walk->body->count && controls[next].kind == CHECK_CONTROL_CASE)
    {
        const CheckProgramCase* item = program->cases + controls[next].target;
        uint32_t width = 0;
        bool sign = false;
        check_action_integer_type(program->types, item->type, &width, &sign);
        const unsigned __int128 ordered = value ^ (sign ? (unsigned __int128)1 << (width - 1) : 0);
        const bool matches = ordered >= check_program_case_order(program, item, false) &&
                             ordered <= check_program_case_order(program, item, true);
        if (!known || matches)
        {
            check_x86_diagnostic_push(walk, item->target, true);
            selected = item->target;
        }
        next += 1;
    }
    // The control after the case table jumps to the default label or the switch's end.
    if (!known || selected == BUSTER_CHECK_NONE)
    {
        check_x86_diagnostic_push(walk, next, true);
    }
}

static void check_x86_diagnostic_initializer(CheckX86DiagnosticWalk* walk, const CheckInitializer* plan)
{
    for (uint32_t part = 0; part < plan->count; part += 1)
    {
        if (plan->parts[part].kind == CHECK_INIT_VALUE)
        {
            check_x86_diagnostic_push(walk, plan->parts[part].node, false);
        }
    }
}

static void check_x86_diagnostic_control(CheckX86DiagnosticWalk* walk, uint32_t index)
{
    const CheckProgram* program = walk->program;
    const CheckInput* input = program->input;
    const CheckControl* entry = program->controls + walk->body->first + index;
    check_x86_diagnostic_push(walk, entry->node, false);
    if (entry->kind == CHECK_CONTROL_INITIALIZE)
    {
        const CheckInitializer* plan = program->initializers->plans + entry->target;
        check_x86_diagnostic_initializer(walk, plan);
    }
    if (entry->kind == CHECK_CONTROL_ASM)
    {
        const CheckInputAsm* assembly = input->asm_statements + entry->target;
        for (uint32_t i = 0; i < assembly->output_count + assembly->input_count; i += 1)
        {
            check_x86_diagnostic_push(walk, assembly->operands[i].node, false);
        }
        for (uint32_t i = program->asm_offsets[entry->target]; i < program->asm_offsets[entry->target + 1]; i += 1)
        {
            check_x86_diagnostic_push(walk, program->asm_targets[i], true);
        }
    }
    bool falls = entry->kind != CHECK_CONTROL_RETURN && entry->kind != CHECK_CONTROL_END;
    if (entry->kind == CHECK_CONTROL_SWITCH)
    {
        check_x86_diagnostic_switch(walk, index);
        falls = false;
    }
    else if (entry->kind == CHECK_CONTROL_JUMP_INDIRECT)
    {
        for (uint32_t i = 0; i < walk->body->count; i += 1)
        {
            if (program->controls[walk->body->first + i].kind == CHECK_CONTROL_LABEL)
            {
                check_x86_diagnostic_push(walk, i, true);
            }
        }
        falls = false;
    }
    else if (entry->kind >= CHECK_CONTROL_BRANCH_FALSE)
    {
        bool truth = false;
        const bool jump = entry->kind == CHECK_CONTROL_JUMP;
        const bool known = !jump && check_program_runtime_truth(program, entry->node, entry->node, &truth);
        const bool taken = truth == (entry->kind == CHECK_CONTROL_BRANCH_TRUE);
        if (jump || !known || taken)
        {
            check_x86_diagnostic_push(walk, entry->target, true);
        }
        falls = !jump && (!known || !taken);
    }
    if (entry->kind == CHECK_CONTROL_END && entry->token < input->token_count)
    {
        const uint32_t region = input->expression_regions[entry->token];
        if (region != 0 && region <= program->region_count)
        {
            check_x86_diagnostic_push(walk, program->regions[region - 1].result, false);
        }
    }
    if (falls)
    {
        check_x86_diagnostic_push(walk, index + 1, true);
    }
}

static void check_x86_diagnostic_expression(CheckX86DiagnosticWalk* walk, uint32_t node)
{
    CheckProgram* program = walk->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    if (op == CHECK_OP_VALUE)
    {
        // Automatic compound literals execute an initializer plan, outside the value leaf's operand columns.
        const CheckValue value = *check_graph_leaf_of(graph, node);
        if (value.object != 0 && value.object < program->object_count && program->input->compound_objects[value.object - 1] &&
            !program->input->static_objects[value.object - 1])
        {
            const uint32_t selected = program->initializers != NULL ? check_initializers_plan_index(program->initializers, value.object - 1) : 0;
            if (selected == 0 || selected > program->initializers->count)
            {
                check_program_fail(program, CHECK_PROGRAM_INVALID, value.object - 1);
            }
            else
            {
                check_x86_diagnostic_initializer(walk, program->initializers->plans + selected - 1);
            }
        }
    }
    else if (op == CHECK_OP_STATEMENT_VALUE)
    {
        const uint32_t region = program->input->expression_regions[graph->tokens[node]];
        if (region != 0 && region <= program->region_count)
        {
            check_x86_diagnostic_push(walk, program->regions[region - 1].first, true);
        }
    }
    else if (op != CHECK_OP_SIZEOF && op != CHECK_OP_ALIGNOF)
    {
        if (op == CHECK_OP_CALL && walk->calls != NULL)
        {
            uint64_t* call = walk->call_room ? BUSTER_ARENA_PUSH_ARRAY(walk->calls, uint64_t, 1) : NULL;
            walk->call_room = call != NULL;
            if (call != NULL)
            {
                *call = (uint64_t)walk->tag << 32 | node;
                walk->call_count += 1;
            }
        }
        uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        bool truth = false;
        if ((op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL) &&
            check_program_runtime_truth(program, node, operands[0], &truth))
        {
            if (op == CHECK_OP_CONDITIONAL)
            {
                operands[truth ? 2 : 1] = BUSTER_CHECK_NONE;
            }
            else if (truth == (op == CHECK_OP_LOGICAL_OR))
            {
                operands[1] = BUSTER_CHECK_NONE;
            }
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
        {
            check_x86_diagnostic_push(walk, operands[i], false);
        }
        if (check_graph_has_arguments(op))
        {
            const uint32_t mask = check_program_argument_mask(program, node);
            for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
            {
                if (op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86 || (i < 32 && (mask & (1u << i)) != 0))
                {
                    check_x86_diagnostic_push(walk, graph->argument_nodes[graph->argument_offsets[node] + i], false);
                }
            }
        }
    }
}

static bool check_program_x86_diagnostics(CheckProgram* program, CheckStream* stream, Arena* scratch)
{
    bool pending = false;
    for (uint32_t i = 0; !pending && i < stream->x86_count; i += 1)
    {
        for (uint32_t e = 0; e < stream->x86[i].range_event_count; e += 1)
        {
            pending |= stream->x86[i].range_events[e].disposition == CHECK_BUILTIN_X86_DEFERRED;
        }
    }
    const uint64_t mark = scratch->length;
    const uint64_t count = (uint64_t)program->input->graph.count + program->control_count;
    CheckX86DiagnosticWalk walk = { .program = program };
    if (pending)
    {
        walk.seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count);
        walk.stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
    }
    const bool valid = !pending || (walk.seen != NULL && walk.stack != NULL);
    if (pending && valid)
    {
        memset(walk.seen, 0, count);
        for (uint32_t b = 0; program->status == CHECK_PROGRAM_OK && b < program->body_count; b += 1)
        {
            walk.body = program->bodies + b;
            check_x86_diagnostic_push(&walk, 0, true);
            while (walk.depth != 0 && program->status == CHECK_PROGRAM_OK)
            {
                const uint64_t item = walk.stack[--walk.depth];
                if (item < program->input->graph.count)
                {
                    check_x86_diagnostic_expression(&walk, (uint32_t)item);
                }
                else
                {
                    check_x86_diagnostic_control(&walk, (uint32_t)(item - program->input->graph.count) - walk.body->first);
                }
            }
        }
        for (uint32_t i = 0; program->status == CHECK_PROGRAM_OK && i < stream->x86_count; i += 1)
        {
            CheckBuiltinX86Effect* effect = stream->x86 + i;
            for (uint32_t e = 0; e < effect->range_event_count; e += 1)
            {
                CheckBuiltinX86RangeEvent* event = effect->range_events + e;
                if (event->disposition == CHECK_BUILTIN_X86_DEFERRED)
                {
                    event->disposition = walk.seen[effect->node] != 0 ? CHECK_BUILTIN_X86_READY : CHECK_BUILTIN_X86_SUPPRESSED;
                }
            }
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}
