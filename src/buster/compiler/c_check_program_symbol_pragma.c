// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A late redefine_extname keeps an earlier name only where Clang's emission already referred to it. Reuse the
// target-feature check's emission walk: its source labels, loops, statement expressions and dead-end calls differ
// from runtime reachability. This cold pass exists only for a late rename and walks only eager candidate bodies.
//
// A late weak declaration (#1849) is the same question of clang's emission order: its code generation gives an
// undefined object's symbol the binding of the declaration its first emitted reference names, and keeps it, so a weak
// attribute or `#pragma weak` after that reference leaves the symbol global (GCC makes it weak; the owner's decision on
// #1447 follows clang). That walk takes the deferred bodies too, which clang emits at the unit's end.

// What the emission walk finds of the watched objects' references (CheckProgramUses): each one's earliest emission
// point among the eager references in `first` (BUSTER_CHECK_NONE before one, 0 for an object that isn't watched); and
// with weak's walk (#1849), the token of the first of the eager references at that point in `use`, and in `deferred`
// what the deferred ones found: 1 where a late weak declaration applies to one, 2 where none applies to another.
// With `records`, every reference to a watched object or to a deferred definition (`nodes`, its node plus one) is
// listed too, with its source's node (`source`: BUSTER_CHECK_NONE for eager code) and emission point, for weak's
// replay of clang's deferred emission (check_program_weak_deferred).
typedef struct CheckProgramUseRecord CheckProgramUseRecord;
struct CheckProgramUseRecord
{
    uint32_t source;
    uint32_t point;
    uint32_t token;
    uint32_t object;
};

typedef struct CheckProgramUses CheckProgramUses;
struct CheckProgramUses
{
    uint32_t* first;
    uint32_t* use;
    uint8_t* deferred;
    const uint32_t* nodes;
    CheckProgramUseRecord* records;
    uint32_t record_count;
    uint32_t record_capacity;
    uint32_t source;
    // A reference past `record_capacity`, which leaves the replay to the deferred bits.
    bool records_full;
};

static bool check_program_weak_named(const CheckProgram* program, uint32_t object, uint32_t use, uint32_t point);

// A reference to `object` at `token`, which clang emits at `point` (BUSTER_CHECK_NONE: at the unit's end).
static void check_program_symbol_reference(const CheckProgram* program, CheckProgramUses* uses, uint32_t object, uint32_t point, uint32_t token)
{
    uint32_t* first = uses->first;
    const bool listed = uses->records != NULL && object < program->object_count && (first[object] != 0 || uses->nodes[object] != 0);
    if (listed && uses->record_count < uses->record_capacity)
    {
        uses->records[uses->record_count++] = (CheckProgramUseRecord){ .source = uses->source, .point = point, .token = token, .object = object };
    }
    else if (listed)
    {
        uses->records_full = true;
    }
    if (object < program->object_count && first[object] != 0 && point != BUSTER_CHECK_NONE)
    {
        const bool earlier = point < first[object] || (uses->use != NULL && point == first[object] && token < uses->use[object]);
        if (earlier && uses->use != NULL)
        {
            uses->use[object] = token;
        }
        first[object] = earlier ? point : first[object];
    }
    else if (object < program->object_count && first[object] != 0 && uses->deferred != NULL)
    {
        uses->deferred[object] |= check_program_weak_named(program, object, token, BUSTER_CHECK_NONE) ? 1 : 2;
    }
}

// An overloaded initializer's selected address is on its retained write, rather than the unresolved graph leaf.
static void check_program_symbol_initializer(const CheckProgram* program, const CheckInitializer* plan, CheckProgramUses* uses, uint32_t point)
{
    for (uint32_t part = 0; part < plan->count; part += 1)
    {
        const CheckInitPart* write = plan->parts + part;
        if (write->kind == CHECK_INIT_VALUE && write->node < program->input->graph.count && check_stream_overload_head(program->stream, write->node) != 0)
        {
            check_program_symbol_reference(program, uses, write->value.object, point, program->input->graph.tokens[write->node]);
        }
    }
}

static void check_program_symbol_expression(const CheckProgram* program, uint32_t node, const uint8_t* emitted, CheckProgramUses* uses, uint32_t point)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    const uint32_t token = graph->tokens[node];
    if (op == CHECK_OP_VALUE)
    {
        const CheckValue value = check_graph_leaf_value(graph, node);
        if (value.overload_head == 0)
        {
            check_program_symbol_reference(program, uses, value.object, point, token);
        }
        if (value.object != 0 && value.object < program->object_count && check_input_bit(program->input->declaration_bits, value.object - 1, BUSTER_CHECK_BITS_COMPOUND) &&
            !check_input_bit(program->input->declaration_bits, value.object - 1, BUSTER_CHECK_BITS_STATIC))
        {
            const uint32_t selected = program->initializers != NULL ? check_initializers_plan_index(program->initializers, value.object - 1) : 0;
            if (selected != 0 && selected <= program->initializers->count)
            {
                check_program_symbol_initializer(program, program->initializers->plans + selected - 1, uses, point);
            }
        }
    }
    if (op == CHECK_OP_CALL && program->stream->overload_count != 0)
    {
        const uint32_t overload = check_program_builtin_index(program->stream->overloads, program->stream->overload_count, sizeof(CheckOverloadEffect), node);
        if (overload != BUSTER_CHECK_NONE)
        {
            check_program_symbol_reference(program, uses, program->stream->overloads[overload].selected.object, point, token);
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
                check_program_symbol_reference(program, uses, selected->object, point, token);
            }
        }
    }
}

// The selected bodies' references to the watched objects (CheckProgramUses): the eager ones', and with `uses->deferred`
// the deferred ones' too.
static bool check_program_symbol_bodies(CheckProgram* program, CheckProgramUses* uses, Arena* scratch)
{
    const uint32_t* first = uses->first;
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
        const bool eager = program->body_eager == NULL || program->body_eager[b] != 0;
        for (uint32_t r = 0; (eager || uses->deferred != NULL) && !candidate && r < body->reference_count; r += 1)
        {
            const uint32_t object = program->references[body->reference_first + r];
            candidate = object < program->object_count && (first[object] != 0 || (uses->nodes != NULL && uses->nodes[object] != 0));
        }
        if (candidate && walk.seen == NULL)
        {
            walk.seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, items);
            walk.stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, items);
            walk.cleanup_tags = input->cleanup_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, input->cleanup_count) : NULL;
            walk.cleanup_links = input->cleanup_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, input->cleanup_count) : NULL;
            emission.bits = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, input->graph.count);
            emission.capacity = (uint64_t)input->graph.count * 2;
            emission.stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, emission.capacity);
            emission.regions = program->region_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, program->region_count) : NULL;
            emission.options = program->parser->preprocessor != NULL ? &program->parser->preprocessor->options : NULL;
            valid = walk.seen != NULL && walk.stack != NULL && (input->cleanup_count == 0 || (walk.cleanup_tags != NULL && walk.cleanup_links != NULL)) &&
                    emission.bits != NULL && emission.stack != NULL && (program->region_count == 0 || emission.regions != NULL);
            if (valid)
            {
                memset(walk.seen, 0, items);
                memset(emission.bits, 0, input->graph.count);
                if (walk.cleanup_tags != NULL)
                {
                    memset(walk.cleanup_tags, 0, (uint64_t)input->cleanup_count * sizeof(uint32_t));
                    for (uint32_t record = 0; record < input->cleanup_count; record += 1)
                    {
                        walk.cleanup_links[record] = record;
                    }
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
            CheckTargetFeatureEmitted emitted = input->cleanup_count != 0 ? check_target_feature_emitted_reserve(scratch, body->count) : (CheckTargetFeatureEmitted){ 0 };
            const uint32_t point = eager ? program->parser->tree.parents[body->opening] + 1 : BUSTER_CHECK_NONE;
            uses->source = eager ? BUSTER_CHECK_NONE : b;
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
                    check_program_symbol_expression(program, item, walk.seen, uses, point);
                }
                else
                {
                    const CheckControl* current = program->controls + body->first + control;
                    if (current->kind == CHECK_CONTROL_INITIALIZE)
                    {
                        check_program_symbol_initializer(program, program->initializers->plans + current->target, uses, point);
                    }
                    check_target_feature_control(&walk, &emission, control);
                }
            }
            valid = valid && walk.call_room && program->status == CHECK_PROGRAM_OK && (walk.cleanup_count == 0 || emitted.ready);
            if (valid && walk.cleanup_count != 0)
            {
                check_target_feature_emitted_fill(&walk, &emission, &emitted);
            }
            for (uint32_t c = 0; valid && walk.cleanup_count != 0 && c < body->count; c += 1)
            {
                const CheckControl* control = program->controls + body->first + c;
                const uint32_t record = control->kind == CHECK_CONTROL_CLEANUP ? control->target : BUSTER_CHECK_NONE;
                if (record < input->cleanup_count && walk.cleanup_tags[record] != 0)
                {
                    if (check_target_feature_cleanup_emitted(&walk, &emission, &emitted, record))
                    {
                        const uint32_t declaration = input->cleanups[record].function;
                        const uint32_t object = declaration < input->token_count ? input->objects[declaration] : 0;
                        check_program_symbol_reference(program, uses, object, point, input->cleanups[record].argument);
                    }
                    walk.cleanup_tags[record] = 0;
                    walk.cleanup_links[record] = record;
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

// GNU's weak attribute and `#pragma weak` after a use (#1849, CheckProgram.weak_late_keys): of the unit's weak
// declarations (`events`, by object in token order), those of the objects none of whose weak declarations is its
// first declaration's own, applied there. A written attribute applies at its declaration, and `#pragma weak`'s (an
// attribute at the declaration's name, check_input_symbol_attribute) at the declaration or the earliest such pragma,
// whichever is later, as clang adds it to the declaration that was the latest at the pragma. Every reference to
// another object sees one of its weak declarations, as clang's do. Returns false without room.
static bool check_program_weak_late(CheckProgram* program, const CheckProgramTargetEvents* events, bool scalar, Arena* output, Arena* scratch)
{
    const uint64_t temporary = scratch->length;
    const CheckInput* input = program->input;
    const CheckInputSymbolPragmas* pragmas = input->symbol_pragmas;
    const uint32_t count = events->weak_count;
    uint64_t* sorting = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1);
    uint32_t* points = sorting != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : NULL;
    uint8_t* late = points != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : NULL;
    bool valid = late != NULL;
    const uint64_t* keys = valid && count > 1 ? c_bindings_sort(events->weak_keys, sorting, count, !scalar, UINT32_MAX) : events->weak_keys;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const uint32_t token = (uint32_t)keys[i];
        const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
        bool written = false;
        for (uint32_t a = 0; attributes != NULL && a < attributes->count; a += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + a;
            written |= !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_WEAK && attribute->name_token != token;
        }
        uint32_t pragma = BUSTER_CHECK_NONE;
        for (uint32_t p = 0; !written && pragmas != NULL && p < pragmas->count; p += 1)
        {
            const CheckInputSymbolPragma* record = pragmas->records + p;
            pragma = record->kind == CHECK_SYMBOL_PRAGMA_WEAK && record->declaration == token && record->token < pragma ? record->token : pragma;
        }
        points[i] = written || pragma == BUSTER_CHECK_NONE || pragma < token ? token : pragma;
    }
    uint32_t late_count = 0;
    for (uint32_t i = 0; valid && i < count;)
    {
        const uint32_t object = (uint32_t)(keys[i] >> 32);
        uint32_t end = i;
        bool early = false;
        while (end < count && (uint32_t)(keys[end] >> 32) == object)
        {
            early |= (uint32_t)keys[end] + 1 == object && points[end] == (uint32_t)keys[end];
            end += 1;
        }
        memset(late + i, early ? 0 : 1, end - i);
        late_count += early ? 0 : end - i;
        i = end;
    }
    program->weak_late_keys = valid && late_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, late_count) : NULL;
    program->weak_late_points = program->weak_late_keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, late_count) : NULL;
    valid = valid && (late_count == 0 || program->weak_late_points != NULL);
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        if (late[i] != 0)
        {
            program->weak_late_keys[program->weak_late_count] = keys[i];
            program->weak_late_points[program->weak_late_count] = points[i];
            program->weak_late_count += 1;
        }
    }
    arena_pop_to(scratch, temporary);
    return valid;
}

// Whether one of `object`'s late weak declarations (CheckProgram.weak_late_keys) applies to a reference that names the
// latest declaration before `use` and that clang emits at `point` (BUSTER_CHECK_NONE: the unit's end): one before `use`,
// applied before `point` (a pragma at the token a body's emission point is comes after the body, as the late rename's
// points have it). Static data names the latest one where clang emits it, so it passes its point as `use`.
static bool check_program_weak_named(const CheckProgram* program, uint32_t object, uint32_t use, uint32_t point)
{
    const uint64_t* keys = program->weak_late_keys;
    uint32_t low = 0;
    uint32_t high = program->weak_late_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = (uint32_t)(keys[middle] >> 32) < object;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    bool weak = false;
    for (uint32_t i = low; i < program->weak_late_count && (uint32_t)(keys[i] >> 32) == object; i += 1)
    {
        weak |= (uint32_t)keys[i] < use && program->weak_late_points[i] < point;
    }
    return weak;
}
