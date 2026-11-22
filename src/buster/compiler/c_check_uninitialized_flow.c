// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The cold definite-write proof for -Wuninitialized's graph-order candidates (#1391). Include from
// c_check_common_warnings.c after its expression/evaluation helpers. The retained CheckProgram supplies execution
// order, including for increments and continues; expression nodes keep their original postorder identities.
//
// Up to 64 candidate scalar objects share one set of read/write masks. An expression's read mask names the objects
// it can read before definitely writing them, and its write mask names the objects it writes on every completion.
// Control predecessors join the objects that may still be uninitialized by union. The monotone worklist converges
// across backedges, then a reverse expression pass selects the first genuinely unprotected read, which need not be
// the original graph-order candidate. A body with detached statement-expression controls or cleanups keeps the old
// diagnostic: those regions have no expression/control handoff in this proof, so they cannot certify a write.

typedef struct CheckCommonInitFlow CheckCommonInitFlow;
struct CheckCommonInitFlow
{
    const CheckProgram* program;
    const uint8_t* evaluated;
    const uint8_t* nonread;
    const uint8_t* slots;
    uint32_t first;
    uint32_t end;
    uint64_t* reads;
    uint64_t* writes;
    uint64_t* incoming;
    uint8_t* covered;
};

static bool check_common_init_candidate(const Parser* parser, const CheckInput* input, const TypeTable* types, const uint8_t* parameters,
                                        const uint8_t* addressed, const uint32_t* first_read, const uint32_t* first_write, uint32_t declaration)
{
    const bool local = input->declaration_kinds[declaration] == C_BINDING_OBJECT &&
                       check_common_sparse_get(input->declaration_bodies, declaration) < parser->tokens.count &&
                       !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) &&
                       !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL) &&
                       !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND);
    const TypeId type = local ? input->declared_types[declaration] : 0;
    const uint8_t kind = type != 0 ? type_kind(types, type_unqualified(type_strip_aligned(types, type))) : 0;
    const bool scalar = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_POINTER ||
                        kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_NULLPTR;
    const bool candidate = local && scalar && parameters[declaration] == 0 && addressed[declaration] == 0 &&
                           first_read[declaration] < first_write[declaration] && first_write[declaration] != BUSTER_CHECK_NONE &&
                           check_common_sparse_get(input->initializer_nodes, declaration) == BUSTER_CHECK_NONE &&
                           check_common_sparse_get(input->initializer_begins, declaration) == BUSTER_CHECK_NONE;
    return candidate;
}

static bool check_common_init_expression(CheckControlKind kind)
{
    const uint64_t expressions = UINT64_C(1) << CHECK_CONTROL_EVALUATE | UINT64_C(1) << CHECK_CONTROL_VLA |
                                 UINT64_C(1) << CHECK_CONTROL_RETURN | UINT64_C(1) << CHECK_CONTROL_SWITCH |
                                 UINT64_C(1) << CHECK_CONTROL_BRANCH_FALSE | UINT64_C(1) << CHECK_CONTROL_BRANCH_TRUE |
                                 UINT64_C(1) << CHECK_CONTROL_JUMP_INDIRECT | UINT64_C(1) << CHECK_CONTROL_RESULT;
    const bool result = (uint32_t)kind < 64 && (expressions >> kind & 1) != 0;
    return result;
}

// A scope's ancestry, rather than source order: a for's ')' precedes the body it owns. An object becomes uninitialized
// again on entry to its block. Keeping its bit set at controls outside that scope also handles loop reentry and gotos
// entering it without making a write from an earlier lifetime a proof for the next one.
static bool check_common_init_within(const Parser* parser, uint32_t token, uint32_t scope)
{
    bool result = false;
    const uint32_t count = parser->tokens.count;
    for (uint32_t steps = 0; !result && token < count && steps < count; steps += 1)
    {
        result = token == scope;
        const uint32_t parent = parser->tree.parents[token];
        token = parent != token ? parent : BUSTER_CHECK_NONE;
    }
    return result;
}

static uint32_t check_common_init_scope(const Parser* parser, uint32_t declaration)
{
    uint32_t scope = BUSTER_CHECK_NONE;
    const uint32_t count = parser->tokens.count;
    for (uint32_t token = declaration, steps = 0; scope == BUSTER_CHECK_NONE && token < count && steps < count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[token];
        scope = role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_COMPOUND_BODY ||
                role == PARSE_ROLE_CLOSE_FOR ? token : BUSTER_CHECK_NONE;
        const uint32_t parent = parser->tree.parents[token];
        token = parent != token ? parent : BUSTER_CHECK_NONE;
    }
    return scope;
}

static uint64_t check_common_init_slot(const CheckCommonInitFlow* flow, uint64_t object)
{
    const uint32_t count = flow->program->input->token_count;
    const uint32_t slot = object != 0 && object <= count ? flow->slots[object - 1] : 0;
    return slot != 0 ? UINT64_C(1) << (slot - 1) : 0;
}

static uint64_t check_common_init_object(const CheckCommonInitFlow* flow, uint32_t node)
{
    const CheckStream* stream = flow->program->stream;
    const uint32_t object = node < stream->count && check_stream_category(stream, node) == CHECK_LVALUE ? check_stream_object(stream, node) : 0;
    return check_common_init_slot(flow, object);
}

// The objects a VALUE reads while they are still in `incoming`, and in `writes` those it writes. A plain value reads its
// own lvalue unless that is not a read. A native window's summary (#2850) has no operand nodes: its rows are its
// accesses in evaluation order, so a row's read after an earlier row's write of the same object is safe.
static uint64_t check_common_init_value(const CheckCommonInitFlow* flow, uint32_t node, uint64_t incoming, uint64_t* writes)
{
    const CheckInput* input = flow->program->input;
    const CheckInputWindowSummary* summary = check_input_window_summary(input, node);
    uint64_t read = summary == NULL && flow->nonread[node] == 0 ? incoming & check_common_init_object(flow, node) : 0;
    uint64_t write = 0;
    for (uint32_t r = 0; summary != NULL && r < summary->count; r += 1)
    {
        const CheckInputWindowRow* row = input->window_rows + summary->first + r;
        const bool access = (row->flags & (BUSTER_CHECK_WINDOW_READ | BUSTER_CHECK_WINDOW_WRITE)) != 0;
        const uint64_t object = access ? check_common_init_slot(flow, row->value) : 0;
        read |= (row->flags & BUSTER_CHECK_WINDOW_READ) != 0 ? object & incoming & ~write : 0;
        write |= (row->flags & BUSTER_CHECK_WINDOW_WRITE) != 0 ? object : 0;
    }
    *writes = write;
    return read;
}

static uint64_t check_common_init_fact(const CheckCommonInitFlow* flow, const uint64_t* facts, uint32_t node)
{
    return node >= flow->first && node < flow->end ? facts[node - flow->first] : 0;
}

static void check_common_init_incoming(CheckCommonInitFlow* flow, uint32_t node, uint64_t incoming)
{
    if (node >= flow->first && node < flow->end)
    {
        const uint32_t at = node - flow->first;
        flow->incoming[at] |= incoming & flow->reads[at];
    }
}

// The fixed operands' execution mask is the warning pass's existing one. Builtin arguments may have an evaluation
// union instead of an execution schedule (fpclassify's selectors, queries). Their reads are conservative here and
// their writes certify nothing; ordinary call arguments, which all execute, can establish a write after the call.
static uint32_t check_common_init_mask(const CheckCommonInitFlow* flow, uint32_t node)
{
    const CheckProgram* program = flow->program;
    const CheckGraph* graph = &program->input->graph;
    uint32_t mask = check_common_operand_mask(graph, program->stream, program->types, program, node);
    if (graph->ops[node] == CHECK_OP_SELECT)
    {
        mask = 1;
    }
    else if (graph->ops[node] == CHECK_OP_VARARGS)
    {
        const uint32_t index = graph->argument_offsets != NULL ? graph->argument_offsets[node] : BUSTER_CHECK_NONE;
        mask = index < program->stream->varargs_count && program->stream->varargs[index].node == node ?
               program->stream->varargs[index].evaluated_operands : 0;
    }
    return mask;
}

static void check_common_init_reduce(CheckCommonInitFlow* flow, uint32_t node)
{
    const CheckProgram* program = flow->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    const uint32_t mask = check_common_init_mask(flow, node);
    const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    uint64_t reads[3] = { 0 };
    uint64_t writes[3] = { 0 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
    {
        if ((mask & (1u << i)) != 0)
        {
            reads[i] = check_common_init_fact(flow, flow->reads, operands[i]);
            writes[i] = check_common_init_fact(flow, flow->writes, operands[i]);
        }
    }
    uint64_t read = reads[0] | reads[1] | reads[2];
    uint64_t write = writes[0] | writes[1] | writes[2];
    if (op == CHECK_OP_COMMA)
    {
        read = reads[0] | (reads[1] & ~writes[0]);
    }
    else if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR)
    {
        read = reads[0] | (reads[1] & ~writes[0]);
        write = writes[0] | (mask == 3 ? writes[1] : 0);
    }
    else if (op == CHECK_OP_CONDITIONAL)
    {
        read = reads[0] | ((reads[1] | reads[2]) & ~writes[0]);
        write = writes[0] | (mask == 3 ? writes[1] : mask == 5 ? writes[2] : writes[1] & writes[2]);
    }
    if (check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
    {
        const uint32_t first = graph->argument_offsets[node];
        const uint32_t count = graph->argument_counts[node];
        for (uint32_t a = 0; a < count && first + a < graph->argument_count; a += 1)
        {
            const uint32_t argument = graph->argument_nodes[first + a];
            read |= check_common_init_fact(flow, flow->reads, argument);
            write |= op == CHECK_OP_CALL ? check_common_init_fact(flow, flow->writes, argument) : 0;
        }
    }
    if (check_common_builtin_op(op))
    {
        write = 0;
    }
    if (op == CHECK_OP_VALUE)
    {
        uint64_t rows = 0;
        read |= check_common_init_value(flow, node, UINT64_MAX, &rows);
        write |= rows;
    }
    if (op == CHECK_OP_ASSIGN || check_common_write_op(op))
    {
        write |= check_common_init_object(flow, operands[0]);
    }
    const bool evaluated = flow->evaluated[node] != 0;
    flow->reads[node - flow->first] = evaluated ? read : 0;
    flow->writes[node - flow->first] = evaluated ? write : 0;
}

// The read/gen masks are monotone summaries. This reverse pass sends the final incoming state only to operands that
// can execute, removing a preceding comma/condition's guaranteed writes. Unsequenced operands each see the original
// state: choosing one evaluation order must never make an uninitialized read disappear.
static void check_common_init_descend(CheckCommonInitFlow* flow, uint32_t node, uint32_t* unsafe)
{
    const CheckGraph* graph = &flow->program->input->graph;
    const CheckOp op = graph->ops[node];
    const uint64_t incoming = flow->incoming[node - flow->first];
    const uint32_t mask = check_common_init_mask(flow, node);
    const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    const bool sequenced = op == CHECK_OP_COMMA || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL;
    const uint64_t after = sequenced ? incoming & ~check_common_init_fact(flow, flow->writes, operands[0]) : incoming;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
    {
        if ((mask & (1u << i)) != 0)
        {
            check_common_init_incoming(flow, operands[i], i == 0 ? incoming : after);
        }
    }
    if (check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
    {
        const uint32_t first = graph->argument_offsets[node];
        const uint32_t count = graph->argument_counts[node];
        for (uint32_t a = 0; a < count && first + a < graph->argument_count; a += 1)
        {
            check_common_init_incoming(flow, graph->argument_nodes[first + a], incoming);
        }
    }
    uint64_t rows = 0;
    uint64_t read = op == CHECK_OP_VALUE ? check_common_init_value(flow, node, incoming, &rows) : 0;
    while (read != 0)
    {
        const uint32_t bit = (uint32_t)__builtin_ctzll(read);
        unsafe[bit] = node < unsafe[bit] ? node : unsafe[bit];
        read &= read - 1;
    }
}

static void check_common_init_offer(uint32_t target, uint32_t count, uint64_t incoming, uint64_t* states, uint8_t* seen,
                                    uint32_t* queue, uint32_t* tail, uint32_t* queued)
{
    if (target < count && ((seen[target] & 1) == 0 || (incoming & ~states[target]) != 0))
    {
        states[target] |= incoming;
        if ((seen[target] & 2) == 0)
        {
            queue[*tail] = target;
            *tail = *tail + 1 == count ? 0 : *tail + 1;
            *queued += 1;
        }
        seen[target] = 3;
    }
}

// The predecessor walk is check_common_body_reached's, with a union meet instead of a visited bit. Even an empty
// uninitialized mask still reaches its successors: a later scope entry may reset an object's lifetime.
static void check_common_init_controls(const CheckProgram* program, const CheckProgramBody* body, uint64_t all, const uint64_t* writes,
                                       const uint64_t* reset, uint64_t* states, uint8_t* seen, uint32_t* queue)
{
    const CheckControl* entries = program->controls + body->first;
    const uint32_t count = body->count;
    memset(states, 0, (uint64_t)count * sizeof(uint64_t));
    memset(seen, 0, count);
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t queued = 0;
    check_common_init_offer(0, count, all, states, seen, queue, &tail, &queued);
    while (queued != 0)
    {
        const uint32_t index = queue[head];
        head = head + 1 == count ? 0 : head + 1;
        queued -= 1;
        seen[index] &= 1;
        const CheckControl* entry = entries + index;
        const uint64_t outgoing = ((states[index] | reset[index]) & all) & ~writes[index];
        uint32_t target = BUSTER_CHECK_NONE;
        bool next = entry->kind != CHECK_CONTROL_RETURN && entry->kind != CHECK_CONTROL_END;
        if (entry->kind >= CHECK_CONTROL_BRANCH_FALSE)
        {
            bool truth = false;
            const bool jump = entry->kind == CHECK_CONTROL_JUMP;
            const bool known = !jump && check_program_branch_truth(program, entry->node, entry->node, &truth);
            const bool taken = truth == (entry->kind == CHECK_CONTROL_BRANCH_TRUE);
            target = jump || !known || taken ? entry->target : BUSTER_CHECK_NONE;
            next = !jump && (!known || !taken);
        }
        else if (entry->kind == CHECK_CONTROL_CASE)
        {
            const CheckProgramCase* item = program->cases + entry->target;
            const uint32_t condition = program->input->token_nodes[item->owner];
            CheckProgramCase selected = { .type = item->type };
            const bool known = condition != BUSTER_CHECK_NONE &&
                               check_program_case_value(program, condition, item->type, &selected.lower, &selected.wide_lower);
            const bool taken = known && check_program_case_compare(program, &selected, false, item, false) >= 0 &&
                               check_program_case_compare(program, &selected, false, item, true) <= 0;
            target = !known || taken ? item->target : BUSTER_CHECK_NONE;
            next = !known || !taken;
        }
        else if (entry->kind == CHECK_CONTROL_ASM || entry->kind == CHECK_CONTROL_JUMP_INDIRECT)
        {
            if (entry->kind == CHECK_CONTROL_ASM && program->asm_offsets != NULL && program->asm_targets != NULL)
            {
                for (uint32_t a = program->asm_offsets[entry->target]; a < program->asm_offsets[entry->target + 1]; a += 1)
                {
                    check_common_init_offer(program->asm_targets[a], count, outgoing, states, seen, queue, &tail, &queued);
                }
            }
            else
            {
                for (uint32_t label = 0; label < count; label += 1)
                {
                    if (entries[label].kind == CHECK_CONTROL_LABEL)
                    {
                        check_common_init_offer(label, count, outgoing, states, seen, queue, &tail, &queued);
                    }
                }
            }
            next = entry->kind != CHECK_CONTROL_JUMP_INDIRECT;
        }
        check_common_init_offer(target, count, outgoing, states, seen, queue, &tail, &queued);
        if (next && index + 1 < count)
        {
            check_common_init_offer(index + 1, count, outgoing, states, seen, queue, &tail, &queued);
        }
    }
}

static void check_common_init_root(CheckCommonInitFlow* flow, uint32_t root, uint32_t action, uint64_t incoming, uint64_t* writes)
{
    if (root >= flow->first && root < flow->end)
    {
        const uint32_t at = root - flow->first;
        if (action == 0)
        {
            flow->covered[at] = 1;
        }
        else if (action == 1)
        {
            *writes |= flow->writes[at];
        }
        else
        {
            flow->incoming[at] |= incoming & flow->reads[at];
        }
    }
}

// INITIALIZE's node is not an expression. Its original plan's value parts are, and every part executes before the
// next control. Reads of different parts remain unsequenced in this proof; their union still proves all final writes.
static void check_common_init_entry(CheckCommonInitFlow* flow, const CheckControl* entry, uint32_t action, uint64_t incoming, uint64_t* writes)
{
    const CheckProgram* program = flow->program;
    if (check_common_init_expression(entry->kind))
    {
        check_common_init_root(flow, entry->node, action, incoming, writes);
    }
    else if (entry->kind == CHECK_CONTROL_INITIALIZE && program->initializers != NULL && entry->target < program->initializers->count)
    {
        const CheckInitializer* plan = program->initializers->plans + entry->target;
        for (uint32_t part = 0; part < plan->count; part += 1)
        {
            if (plan->parts[part].kind == CHECK_INIT_VALUE)
            {
                check_common_init_root(flow, plan->parts[part].node, action, incoming, writes);
            }
        }
    }
}

// A body's token-to-node handoff bounds its own graph slice, including late wrappers named by its controls. A child
// outside it declines the proof instead of scanning other functions. Every evaluated read must belong to one of the
// control roots' closures; a detached graph root likewise keeps the original warning for the object it reads.
static bool check_common_init_body(const Parser* parser, const CheckProgram* program, const CheckProgramBody* body, const uint64_t* candidates,
                                   uint32_t candidate_count, const uint8_t* evaluated, const uint8_t* nonread, uint8_t* slots,
                                   uint32_t* first_read, uint32_t* first_write, Arena* scratch)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const CheckControl* entries = program->controls + body->first;
    const uint32_t closing = body->opening < parser->tokens.count ? parser->tree.parents[body->opening] : BUSTER_CHECK_NONE;
    uint32_t first = graph->count;
    uint32_t end = 0;
    for (uint32_t token = body->opening; token <= closing && token < input->token_count; token += 1)
    {
        const uint32_t node = input->token_nodes[token];
        first = node < first ? node : first;
        end = node < graph->count && node + 1 > end ? node + 1 : end;
    }
    for (uint32_t i = 0; i < body->count; i += 1)
    {
        const uint32_t node = check_common_init_expression(entries[i].kind) ? entries[i].node : BUSTER_CHECK_NONE;
        first = node < first ? node : first;
        end = node < graph->count && node + 1 > end ? node + 1 : end;
        if (entries[i].kind == CHECK_CONTROL_INITIALIZE && program->initializers != NULL && entries[i].target < program->initializers->count)
        {
            const CheckInitializer* plan = program->initializers->plans + entries[i].target;
            for (uint32_t part = 0; part < plan->count; part += 1)
            {
                const uint32_t value = plan->parts[part].kind == CHECK_INIT_VALUE ? plan->parts[part].node : BUSTER_CHECK_NONE;
                first = value < first ? value : first;
                end = value < graph->count && value + 1 > end ? value + 1 : end;
            }
        }
    }
    const uint32_t length = first < end ? end - first : 0;
    const uint64_t mark = scratch->length;
    CheckCommonInitFlow flow =
    {
        .program = program,
        .evaluated = evaluated,
        .nonread = nonread,
        .slots = slots,
        .first = first,
        .end = end,
    };
    flow.reads = length != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, length) : NULL;
    flow.writes = flow.reads != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, length) : NULL;
    flow.incoming = flow.writes != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, length) : NULL;
    flow.covered = flow.incoming != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, length) : NULL;
    uint64_t* writes = flow.covered != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, body->count) : NULL;
    uint64_t* reset = writes != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, body->count) : NULL;
    uint64_t* states = reset != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, body->count) : NULL;
    uint8_t* seen = states != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, body->count) : NULL;
    uint32_t* queue = seen != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, body->count) : NULL;
    const bool valid = length == 0 || queue != NULL;
    bool complete = length != 0 && valid;
    if (complete)
    {
        memset(flow.covered, 0, length);
        for (uint32_t i = 0; i < body->count; i += 1)
        {
            check_common_init_entry(&flow, entries + i, 0, 0, NULL);
        }
        for (uint32_t node = end; node-- > first;)
        {
            if (flow.covered[node - first] == 0)
            {
                continue;
            }
            complete &= graph->ops[node] != CHECK_OP_STATEMENT_VALUE || evaluated[node] == 0;
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                const uint32_t child = operands[i];
                if (child < graph->count)
                {
                    complete &= child >= first && child < node;
                    if (child >= first && child < node)
                    {
                        flow.covered[child - first] = 1;
                    }
                }
            }
            if (check_graph_has_arguments(graph->ops[node]) && graph->argument_offsets != NULL && graph->argument_counts != NULL)
            {
                const uint32_t offset = graph->argument_offsets[node];
                const uint32_t count = graph->argument_counts[node];
                for (uint32_t a = 0; a < count && offset + a < graph->argument_count; a += 1)
                {
                    const uint32_t child = graph->argument_nodes[offset + a];
                    complete &= child >= first && child < node;
                    if (child >= first && child < node)
                    {
                        flow.covered[child - first] = 1;
                    }
                }
            }
        }
    }
    for (uint32_t batch = 0; complete && batch < candidate_count;)
    {
        const uint32_t count = candidate_count - batch < 64 ? candidate_count - batch : 64;
        uint32_t declarations[64];
        uint32_t scopes[64];
        uint32_t unsafe[64];
        uint64_t eligible = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
        for (uint32_t bit = 0; bit < count; bit += 1)
        {
            const uint32_t declaration = (uint32_t)candidates[batch + bit];
            declarations[bit] = declaration;
            scopes[bit] = check_common_init_scope(parser, declaration);
            unsafe[bit] = BUSTER_CHECK_NONE;
            slots[declaration] = (uint8_t)(bit + 1);
            const uint32_t read = first_read[declaration];
            if (scopes[bit] >= parser->tokens.count || read < first || read >= end || flow.covered[read - first] == 0)
            {
                eligible &= ~(UINT64_C(1) << bit);
            }
        }
        memset(flow.incoming, 0, (uint64_t)length * sizeof(uint64_t));
        for (uint32_t node = first; node < end; node += 1)
        {
            if (flow.covered[node - first] != 0)
            {
                check_common_init_reduce(&flow, node);
            }
            else
            {
                flow.reads[node - first] = 0;
                flow.writes[node - first] = 0;
            }
            if (graph->ops[node] == CHECK_OP_VALUE && evaluated[node] != 0 && flow.covered[node - first] == 0)
            {
                uint64_t rows = 0;
                eligible &= ~check_common_init_value(&flow, node, UINT64_MAX, &rows);
            }
        }
        for (uint32_t i = 0; i < body->count; i += 1)
        {
            writes[i] = 0;
            reset[i] = 0;
            check_common_init_entry(&flow, entries + i, 1, 0, writes + i);
            for (uint32_t bit = 0; bit < count; bit += 1)
            {
                reset[i] |= !check_common_init_within(parser, entries[i].token, scopes[bit]) ? UINT64_C(1) << bit : 0;
            }
        }
        check_common_init_controls(program, body, eligible, writes, reset, states, seen, queue);
        for (uint32_t i = 0; i < body->count; i += 1)
        {
            if (seen[i] != 0)
            {
                check_common_init_entry(&flow, entries + i, 2, (states[i] | reset[i]) & eligible, NULL);
            }
        }
        for (uint32_t node = end; node-- > first;)
        {
            if (flow.incoming[node - first] != 0 && evaluated[node] != 0)
            {
                check_common_init_descend(&flow, node, unsafe);
            }
        }
        for (uint32_t bit = 0; bit < count; bit += 1)
        {
            const uint32_t declaration = declarations[bit];
            slots[declaration] = 0;
            if ((eligible & (UINT64_C(1) << bit)) != 0)
            {
                // The publication loop still owns spelling, policy and notes. After the proof these columns name
                // an actual unsafe read, or none, instead of comparing unrelated source-order minima.
                first_read[declaration] = unsafe[bit];
                first_write[declaration] = unsafe[bit] == BUSTER_CHECK_NONE ? 0 : BUSTER_CHECK_NONE;
            }
        }
        batch += count;
    }
    arena_pop_to(scratch, mark);
    return valid;
}

static bool check_common_uninitialized_flow(const Parser* parser, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                            const CheckProgram* program, const uint8_t* evaluated, const uint8_t* nonread, const uint8_t* parameters,
                                            const uint8_t* addressed, uint32_t* first_read, uint32_t* first_write, Arena* scratch)
{
    const uint32_t count = input->token_count;
    const bool available = program != NULL && program->status == CHECK_PROGRAM_OK && program->controls != NULL && program->bodies != NULL && program->body_count != 0 &&
                           program->input == input && program->stream == stream && input->token_nodes != NULL;
    uint32_t candidates_count = 0;
    for (uint32_t declaration = available ? check_common_next_entry(input->declaration_bodies, 0, count) : count; declaration < count;
         declaration = check_common_next_entry(input->declaration_bodies, declaration + 1, count))
    {
        candidates_count += check_common_init_candidate(parser, input, types, parameters, addressed, first_read, first_write, declaration) ? 1u : 0u;
    }
    const uint64_t mark = scratch->length;
    uint64_t* candidates = candidates_count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, candidates_count) : NULL;
    uint8_t* slots = candidates != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    bool valid = candidates_count == 0 || slots != NULL;
    if (valid && candidates_count != 0)
    {
        memset(slots, 0, count);
        uint32_t at = 0;
        bool ordered = true;
        for (uint32_t declaration = check_common_next_entry(input->declaration_bodies, 0, count); declaration < count;
             declaration = check_common_next_entry(input->declaration_bodies, declaration + 1, count))
        {
            if (check_common_init_candidate(parser, input, types, parameters, addressed, first_read, first_write, declaration))
            {
                const uint64_t key = (uint64_t)check_common_sparse_get(input->declaration_bodies, declaration) << 32 | declaration;
                ordered &= at == 0 || candidates[at - 1] < key;
                candidates[at++] = key;
            }
        }
        if (!ordered)
        {
            uint64_t* temporary = CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, candidates_count);
            valid = temporary != NULL;
            if (valid)
            {
                candidates = c_bindings_sort(candidates, temporary, candidates_count, true, UINT32_MAX);
            }
        }
        uint32_t body = 0;
        for (uint32_t begin = 0; valid && begin < candidates_count;)
        {
            const uint32_t opening = (uint32_t)(candidates[begin] >> 32);
            uint32_t end = begin + 1;
            while (end < candidates_count && (uint32_t)(candidates[end] >> 32) == opening)
            {
                end += 1;
            }
            while (body < program->body_count && program->bodies[body].opening < opening)
            {
                body += 1;
            }
            const CheckProgramBody* owner = body < program->body_count && program->bodies[body].opening == opening ? program->bodies + body : NULL;
            if (owner != NULL && owner->count != 0 && owner->entry_count == owner->count && owner->cleanup_count == 0)
            {
                valid = check_common_init_body(parser, program, owner, candidates + begin, end - begin, evaluated, nonread, slots, first_read, first_write, scratch);
            }
            begin = end;
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}
