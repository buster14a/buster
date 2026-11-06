// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A late redefine_extname keeps an earlier name only where Clang's emission already referred to it. Reuse the
// target-feature check's emission walk: its source labels, loops, statement expressions and dead-end calls differ
// from runtime reachability. This cold pass exists only for a late rename and walks only eager candidate bodies.

static void check_program_symbol_reference(const CheckProgram* program, uint32_t* first, uint32_t object, uint32_t point)
{
    if (object < program->object_count && first[object] != 0 && point < first[object])
    {
        first[object] = point;
    }
}

// An overloaded initializer's selected address is on its retained write, rather than the unresolved graph leaf.
static void check_program_symbol_initializer(const CheckProgram* program, const CheckInitializer* plan, uint32_t* first, uint32_t point)
{
    for (uint32_t part = 0; part < plan->count; part += 1)
    {
        const CheckInitPart* write = plan->parts + part;
        if (write->kind == CHECK_INIT_VALUE && write->node < program->input->graph.count && check_stream_overload_head(program->stream, write->node) != 0)
        {
            check_program_symbol_reference(program, first, write->value.object, point);
        }
    }
}

static void check_program_symbol_expression(const CheckProgram* program, uint32_t node, const uint8_t* emitted, uint32_t* first, uint32_t point)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    if (op == CHECK_OP_VALUE)
    {
        const CheckValue value = *check_graph_leaf_of(graph, node);
        if (value.overload_head == 0)
        {
            check_program_symbol_reference(program, first, value.object, point);
        }
        if (value.object != 0 && value.object < program->object_count && check_input_bit(program->input->declaration_bits, value.object - 1, BUSTER_CHECK_BITS_COMPOUND) &&
            !check_input_bit(program->input->declaration_bits, value.object - 1, BUSTER_CHECK_BITS_STATIC))
        {
            const uint32_t selected = program->initializers != NULL ? check_initializers_plan_index(program->initializers, value.object - 1) : 0;
            if (selected != 0 && selected <= program->initializers->count)
            {
                check_program_symbol_initializer(program, program->initializers->plans + selected - 1, first, point);
            }
        }
    }
    if (op == CHECK_OP_CALL && program->stream->overload_count != 0)
    {
        const uint32_t overload = check_program_builtin_index(program->stream->overloads, program->stream->overload_count, sizeof(CheckOverloadEffect), node);
        if (overload != BUSTER_CHECK_NONE)
        {
            check_program_symbol_reference(program, first, program->stream->overloads[overload].selected.object, point);
        }
    }
    if (program->stream->overload_address_count != 0 && op != CHECK_OP_VALUE && op != CHECK_OP_SIZEOF && op != CHECK_OP_ALIGNOF)
    {
        const uint32_t operands = 3 + (check_graph_has_arguments(op) ? graph->argument_counts[node] : 0);
        for (uint32_t operand = 0; operand < operands; operand += 1)
        {
            const uint32_t child = operand == 0 ? graph->operand0[node] : operand == 1 ? graph->operand1[node] : operand == 2 ? graph->operand2[node] :
                                   graph->argument_nodes[graph->argument_offsets[node] + operand - 3];
            const CheckOverloadResult* selected = child < graph->count && emitted[child] != 0 ? check_program_overload_address(program, node, operand) : NULL;
            if (selected != NULL)
            {
                check_program_symbol_reference(program, first, selected->object, point);
            }
        }
    }
}

static bool check_program_symbol_bodies(CheckProgram* program, uint32_t* first, Arena* scratch)
{
    const uint64_t temporary = scratch->length;
    const CheckInput* input = program->input;
    const uint64_t items = (uint64_t)input->graph.count + program->control_count;
    CheckX86DiagnosticWalk walk = { .program = program, .calls = scratch };
    CheckTargetFeatureEmission emission = { 0 };
    bool valid = items <= UINT32_MAX;
    for (uint32_t b = 0; valid && b < program->body_count; b += 1)
    {
        const CheckProgramBody* body = program->bodies + b;
        bool candidate = false;
        for (uint32_t r = 0; program->body_eager[b] && !candidate && r < body->reference_count; r += 1)
        {
            const uint32_t object = program->references[body->reference_first + r];
            candidate = object < program->object_count && first[object] != 0;
        }
        if (candidate && walk.seen == NULL)
        {
            walk.seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, items);
            walk.stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, items);
            walk.cleanup_tags = input->cleanup_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, input->cleanup_count) : NULL;
            emission.bits = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, input->graph.count);
            emission.capacity = (uint64_t)input->graph.count * 2;
            emission.stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, emission.capacity);
            emission.regions = program->region_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, program->region_count) : NULL;
            emission.options = program->parser->preprocessor != NULL ? &program->parser->preprocessor->options : NULL;
            valid = walk.seen != NULL && walk.stack != NULL && (input->cleanup_count == 0 || walk.cleanup_tags != NULL) &&
                    emission.bits != NULL && emission.stack != NULL && (program->region_count == 0 || emission.regions != NULL);
            if (valid)
            {
                memset(walk.seen, 0, items);
                memset(emission.bits, 0, input->graph.count);
                if (walk.cleanup_tags != NULL)
                {
                    memset(walk.cleanup_tags, 0, (uint64_t)input->cleanup_count * sizeof(uint32_t));
                }
                if (emission.regions != NULL)
                {
                    memset(emission.regions, 0, program->region_count);
                }
                valid = check_target_feature_columns(program, &emission, scratch);
            }
        }
        if (valid && candidate)
        {
            const uint64_t body_mark = scratch->length;
            const uint32_t point = program->parser->tree.parents[body->opening] + 1;
            walk.body = body;
            walk.tag = 0;
            walk.call_count = 0;
            walk.call_room = true;
            check_x86_diagnostic_push(&walk, 0, true);
            valid = check_target_feature_emission_prepare(&walk, &emission, scratch);
            while (valid && walk.call_room && walk.depth != 0 && program->status == CHECK_PROGRAM_OK)
            {
                const uint64_t entry = walk.stack[--walk.depth];
                const uint32_t item = (uint32_t)entry;
                const uint32_t control = item >= input->graph.count ? item - input->graph.count - body->first : BUSTER_CHECK_NONE;
                walk.tag = control < body->entry_count ? control + 1 : (uint32_t)(entry >> 32);
                if (item < input->graph.count)
                {
                    check_target_feature_expression(&walk, &emission, item);
                    check_program_symbol_expression(program, item, walk.seen, first, point);
                }
                else
                {
                    const CheckControl* current = program->controls + body->first + control;
                    if (current->kind == CHECK_CONTROL_INITIALIZE)
                    {
                        check_program_symbol_initializer(program, program->initializers->plans + current->target, first, point);
                    }
                    check_target_feature_control(&walk, &emission, control);
                }
            }
            valid = valid && walk.call_room && program->status == CHECK_PROGRAM_OK;
            for (uint32_t c = 0; valid && walk.cleanup_count != 0 && c < body->count; c += 1)
            {
                const CheckControl* control = program->controls + body->first + c;
                const uint32_t record = control->kind == CHECK_CONTROL_CLEANUP ? control->target : BUSTER_CHECK_NONE;
                if (record < input->cleanup_count && walk.cleanup_tags[record] != 0)
                {
                    if (check_target_feature_cleanup_emitted(&walk, &emission, record))
                    {
                        const uint32_t declaration = input->cleanups[record].function;
                        const uint32_t object = declaration < input->token_count ? input->objects[declaration] : 0;
                        check_program_symbol_reference(program, first, object, point);
                    }
                    walk.cleanup_tags[record] = 0;
                    walk.cleanup_count -= 1;
                }
            }
            valid = valid && walk.cleanup_count == 0;
            walk.depth = 0;
            walk.tag = 0;
            arena_pop_to(scratch, body_mark);
        }
    }
    arena_pop_to(scratch, temporary);
    return valid;
}
