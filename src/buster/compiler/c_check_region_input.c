// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Compact proof schedules for GNU statement expressions, built from the parser's existing statement tree and
// the reader's complete typed expression roots. The executable program retains authority over runtime control.
// These schedules describe the same selections to the graph's bounded source/speculative/runtime proof lanes;
// they neither interpret local storage nor infer names or types. Include after check_input_add and before the
// expression pass. Nested regions are atomic expression roots, so retained steps are linear in covered tokens.

typedef enum CheckRegionInputTaskKind
{
    CHECK_REGION_INPUT_STATEMENT,
    CHECK_REGION_INPUT_TOKEN,
    CHECK_REGION_INPUT_STEP,
    CHECK_REGION_INPUT_LABEL,
} CheckRegionInputTaskKind;

typedef struct CheckRegionInputTask CheckRegionInputTask;
struct CheckRegionInputTask
{
    CheckRegionInputTaskKind kind;
    uint32_t token;
    uint32_t stop;
    uint32_t repeat;
    CheckRegionProofStep step;
};

typedef struct CheckRegionInput CheckRegionInput;
struct CheckRegionInput
{
    CheckInputReader* reader;
    const CheckInputRegion* region;
    CheckRegionInputTask* tasks;
    uint32_t* labels;
    uint32_t* source_labels;
    uint32_t* case_first;
    uint32_t* case_next;
    uint32_t first;
    uint32_t depth;
    uint32_t label_count;
    uint32_t capacity;
    bool unsupported;
};

static bool check_input_region_initialize(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    CheckGraph* graph = &input->graph;
    uint64_t covered = 0;
    uint64_t end = 0;
    bool valid = true;
    for (uint32_t i = 0; valid && i < input->region_count; i += 1)
    {
        const CheckInputRegion* region = input->regions + i;
        valid = region->opening < region->closing && region->closing < input->token_count &&
                (i == 0 || input->regions[i - 1].opening < region->opening);
        if (valid && (uint64_t)region->closing + 1 > end)
        {
            const uint64_t begin = region->opening > end ? region->opening : end;
            covered += (uint64_t)region->closing + 1 - begin;
            end = (uint64_t)region->closing + 1;
        }
    }
    const uint64_t capacity = covered * 8 + input->region_count;
    valid &= capacity <= UINT32_MAX;
    if (valid && input->region_count != 0)
    {
        graph->region_proof_capacity = input->region_count;
        graph->region_step_capacity = (uint32_t)capacity;
        graph->region_proofs = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckRegionProof, graph->region_proof_capacity);
        graph->region_steps = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckRegionProofStep, graph->region_step_capacity);
        reader->region_roots = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)input->token_count + 1);
        valid = graph->region_proofs != NULL && graph->region_steps != NULL && reader->region_roots != NULL;
        if (valid)
        {
            memset(reader->region_roots, 0xff, ((uint64_t)input->token_count + 1) * sizeof(uint32_t));
        }
    }
    input->resource |= !valid;
    return valid;
}

static void check_region_input_push(CheckRegionInput* writer, CheckRegionInputTask task)
{
    if (writer->depth < writer->capacity)
    {
        writer->tasks[writer->depth++] = task;
    }
    else
    {
        writer->reader->output->resource = true;
    }
}

static uint32_t check_region_input_label(CheckRegionInput* writer)
{
    const uint32_t label = writer->label_count;
    if (label < writer->capacity)
    {
        writer->labels[label] = BUSTER_CHECK_NONE;
        writer->label_count += 1;
    }
    else
    {
        writer->reader->output->resource = true;
    }
    return label;
}

static void check_region_input_mark(CheckRegionInput* writer, uint32_t label)
{
    check_region_input_push(writer, (CheckRegionInputTask){ .kind = CHECK_REGION_INPUT_LABEL, .token = label });
}

static void check_region_input_step(CheckRegionInput* writer, CheckRegionProofOp op, uint32_t node, uint32_t operand, uint32_t target)
{
    check_region_input_push(writer, (CheckRegionInputTask)
    {
        .kind = CHECK_REGION_INPUT_STEP,
        .step = { .op = op, .node = node, .operand = operand, .target = target },
    });
}

static void check_region_input_statement(CheckRegionInput* writer, uint32_t token, uint32_t stop, uint32_t repeat)
{
    if (token != BUSTER_CHECK_NONE)
    {
        check_region_input_push(writer, (CheckRegionInputTask){ .kind = CHECK_REGION_INPUT_STATEMENT, .token = token, .stop = stop, .repeat = repeat });
    }
}

static void check_region_input_token(CheckRegionInput* writer, uint32_t token)
{
    check_region_input_push(writer, (CheckRegionInputTask){ .kind = CHECK_REGION_INPUT_TOKEN, .token = token });
}

static void check_region_input_children(CheckRegionInput* writer, uint32_t token, uint32_t minimum, uint32_t stop, uint32_t repeat)
{
    const CheckInputReader* reader = writer->reader;
    const uint32_t first = writer->depth;
    for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
    {
        const uint8_t role = reader->parser->tree.roles[child];
        if ((minimum == BUSTER_CHECK_NONE || child > minimum) && role != PARSE_ROLE_OPEN_BLOCK && role != PARSE_ROLE_OPEN_BODY)
        {
            check_region_input_statement(writer, child, stop, repeat);
        }
    }
    // The reader retains forward siblings. Reverse only this newly appended task slice for source-order pop.
    for (uint32_t a = first, b = writer->depth; a < b && a < --b; a += 1)
    {
        const CheckRegionInputTask saved = writer->tasks[a];
        writer->tasks[a] = writer->tasks[b];
        writer->tasks[b] = saved;
    }
}

static uint32_t check_region_input_keyword(const CheckRegionInput* writer, uint32_t token)
{
    const CheckInputReader* reader = writer->reader;
    uint32_t keyword = BUSTER_CHECK_NONE;
    for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
    {
        if (child < token && reader->parser->tree.roles[child] == PARSE_ROLE_LEAF_RIGHT)
        {
            keyword = child;
        }
    }
    return keyword;
}

// Discover case labels with the same statement traversal used below. Arrays indexed by source position are
// only read for visited switches/cases, and are initialized at those visits; nested expression spans are not scanned.
static void check_region_input_cases(CheckRegionInput* writer)
{
    CheckInputReader* reader = writer->reader;
    const Parser* parser = reader->parser;
    const uint32_t opening = writer->region->opening;
    check_region_input_statement(writer, writer->region->block, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
    while (writer->depth != 0 && !reader->output->resource)
    {
        const uint32_t token = writer->tasks[--writer->depth].token;
        const uint8_t role = parser->tree.roles[token];
        if (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO)
        {
            check_region_input_children(writer, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
        else if (role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR)
        {
            if (role == PARSE_ROLE_CLOSE_CONDITION)
            {
                const uint32_t keyword = check_region_input_keyword(writer, token);
                if (keyword != BUSTER_CHECK_NONE && parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH)
                {
                    writer->case_first[token - opening] = BUSTER_CHECK_NONE;
                }
            }
            check_region_input_children(writer, token, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
        else if (role == PARSE_ROLE_TERMINATOR)
        {
            for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                if (parser->tree.roles[child] == PARSE_ROLE_DO)
                {
                    check_region_input_statement(writer, child, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                }
            }
        }
        else if (role == PARSE_ROLE_CLOSE_CASE)
        {
            const uint32_t owner = reader->output->control_owners[token];
            if (owner <= opening || owner >= writer->region->closing || reader->output->case_ranges[token] != BUSTER_CHECK_NONE)
            {
                writer->unsupported = true;
            }
            else
            {
                writer->source_labels[token - opening] = check_region_input_label(writer);
                writer->case_next[token - opening] = writer->case_first[owner - opening];
                writer->case_first[owner - opening] = token;
            }
            check_region_input_children(writer, token, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
        else if (role == PARSE_ROLE_LABEL)
        {
            check_region_input_children(writer, token, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
    }
}

static void check_region_input_expand(CheckRegionInput* writer, CheckRegionInputTask task)
{
    CheckInputReader* reader = writer->reader;
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const uint32_t token = task.token;
    const uint8_t role = parser->tree.roles[token];
    const uint32_t none = BUSTER_CHECK_NONE;
    if (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO)
    {
        check_region_input_children(writer, token, none, task.stop, task.repeat);
    }
    else if (role == PARSE_ROLE_CLOSE_CONDITION)
    {
        uint32_t body = none;
        uint32_t alternate = none;
        for (uint32_t child = reader->first_child[token]; child != none; child = reader->next_child[child])
        {
            if (child > token)
            {
                if (parser->tree.roles[child] == PARSE_ROLE_ELSE)
                {
                    alternate = child;
                }
                else
                {
                    body = child;
                }
            }
        }
        const uint32_t keyword = check_region_input_keyword(writer, token);
        const uint32_t condition = input->token_nodes[token];
        const uint32_t end = check_region_input_label(writer);
        if (keyword == none || condition == none || body == none)
        {
            writer->unsupported = true;
        }
        else if (parser->tokens.kinds[keyword] == PARSE_KIND_IF)
        {
            const uint32_t otherwise = alternate != none ? check_region_input_label(writer) : end;
            check_region_input_mark(writer, end);
            if (alternate != none)
            {
                check_region_input_statement(writer, alternate, task.stop, task.repeat);
                check_region_input_mark(writer, otherwise);
                check_region_input_step(writer, CHECK_REGION_JUMP, none, none, end);
            }
            check_region_input_statement(writer, body, task.stop, task.repeat);
            check_region_input_step(writer, CHECK_REGION_BRANCH, condition, none, otherwise);
        }
        else if (parser->tokens.kinds[keyword] == PARSE_KIND_WHILE)
        {
            const uint32_t start = check_region_input_label(writer);
            check_region_input_mark(writer, end);
            check_region_input_step(writer, CHECK_REGION_JUMP, none, none, start);
            check_region_input_statement(writer, body, end, start);
            check_region_input_step(writer, CHECK_REGION_BRANCH, condition, none, end);
            check_region_input_mark(writer, start);
        }
        else if (parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH)
        {
            const uint32_t opening = writer->region->opening;
            uint32_t fallback = end;
            for (uint32_t item = writer->case_first[token - opening]; item != none; item = writer->case_next[item - opening])
            {
                if (input->token_nodes[item] == none)
                {
                    fallback = writer->source_labels[item - opening];
                }
            }
            check_region_input_mark(writer, end);
            check_region_input_statement(writer, body, end, task.repeat);
            check_region_input_step(writer, CHECK_REGION_JUMP, none, none, fallback);
            for (uint32_t item = writer->case_first[token - opening]; item != none; item = writer->case_next[item - opening])
            {
                const uint32_t value = input->token_nodes[item];
                if (value != none)
                {
                    const uint32_t next = check_region_input_label(writer);
                    check_region_input_mark(writer, next);
                    check_region_input_step(writer, CHECK_REGION_JUMP, none, none, writer->source_labels[item - opening]);
                    check_region_input_step(writer, CHECK_REGION_CASE, condition, value, next);
                }
            }
            check_region_input_step(writer, CHECK_REGION_EVAL, condition, none, none);
        }
        else
        {
            writer->unsupported = true;
        }
    }
    else if (role == PARSE_ROLE_CLOSE_FOR)
    {
        uint32_t first = none;
        uint32_t condition = none;
        uint32_t body = none;
        for (uint32_t child = reader->first_child[token]; child != none; child = reader->next_child[child])
        {
            if (child < token && parser->tokens.kinds[child] == PARSE_KIND_SEMI)
            {
                if (first == none)
                {
                    first = child;
                }
                else
                {
                    condition = child;
                }
            }
            else if (child > token)
            {
                body = child;
            }
        }
        if (first == none || condition == none || body == none)
        {
            writer->unsupported = true;
        }
        else
        {
            const uint32_t start = check_region_input_label(writer);
            const uint32_t update = check_region_input_label(writer);
            const uint32_t end = check_region_input_label(writer);
            check_region_input_mark(writer, end);
            check_region_input_step(writer, CHECK_REGION_JUMP, none, none, start);
            check_region_input_token(writer, token);
            check_region_input_mark(writer, update);
            check_region_input_statement(writer, body, end, update);
            const uint32_t node = reader->region_roots[condition];
            if (node != none)
            {
                check_region_input_step(writer, CHECK_REGION_BRANCH, node, none, end);
            }
            check_region_input_mark(writer, start);
            check_region_input_token(writer, first);
        }
    }
    else if (role == PARSE_ROLE_TERMINATOR)
    {
        uint32_t jump = none;
        uint32_t body = none;
        uint32_t condition = none;
        for (uint32_t child = reader->first_child[token]; child != none; child = reader->next_child[child])
        {
            const uint8_t child_role = parser->tree.roles[child];
            jump = child_role == PARSE_ROLE_RETURN ? child : jump;
            body = child_role == PARSE_ROLE_DO ? child : body;
            condition = child_role == PARSE_ROLE_CLOSE_CONDITION ? child : condition;
            writer->unsupported |= child_role == PARSE_ROLE_CLOSE_ASM;
        }
        if (jump != none)
        {
            const uint8_t kind = parser->tokens.kinds[jump];
            if ((kind == PARSE_KIND_BREAK && task.stop != none) || (kind == PARSE_KIND_CONTINUE && task.repeat != none))
            {
                check_region_input_step(writer, CHECK_REGION_JUMP, none, none, kind == PARSE_KIND_BREAK ? task.stop : task.repeat);
            }
            else
            {
                writer->unsupported = true;
            }
        }
        else if (body != none && condition != none)
        {
            const uint32_t start = check_region_input_label(writer);
            const uint32_t test = check_region_input_label(writer);
            const uint32_t end = check_region_input_label(writer);
            check_region_input_mark(writer, end);
            check_region_input_step(writer, CHECK_REGION_JUMP, none, none, start);
            check_region_input_step(writer, CHECK_REGION_BRANCH, input->token_nodes[condition], none, end);
            check_region_input_mark(writer, test);
            check_region_input_statement(writer, body, end, test);
            check_region_input_mark(writer, start);
        }
        else
        {
            check_region_input_token(writer, token);
        }
    }
    else if (role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_LABEL)
    {
        check_region_input_children(writer, token, token, task.stop, task.repeat);
        if (role == PARSE_ROLE_CLOSE_CASE)
        {
            check_region_input_mark(writer, writer->source_labels[token - writer->region->opening]);
        }
    }
}

static uint32_t check_input_region_proof(CheckInputReader* reader, const CheckInputRegion* region)
{
    CheckGraph* graph = &reader->output->graph;
    const uint64_t mark = reader->scratch->length;
    const uint64_t span = (uint64_t)region->closing - region->opening + 1;
    const uint64_t capacity = 8 * span + 1;
    CheckRegionInput writer = { .reader = reader, .region = region, .first = graph->region_step_count };
    bool ready = region->opening < region->closing && region->closing < reader->output->token_count && capacity <= UINT32_MAX &&
                 reader->region_roots != NULL && graph->region_proof_count < graph->region_proof_capacity;
    writer.capacity = ready ? (uint32_t)capacity : 0;
    writer.tasks = ready ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckRegionInputTask, capacity) : NULL;
    writer.labels = ready ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, capacity) : NULL;
    writer.source_labels = ready ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, span) : NULL;
    writer.case_first = ready ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, span) : NULL;
    writer.case_next = ready ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, span) : NULL;
    ready &= writer.tasks != NULL && writer.labels != NULL && writer.source_labels != NULL && writer.case_first != NULL && writer.case_next != NULL;
    reader->output->resource |= !ready;
    if (ready)
    {
        check_region_input_cases(&writer);
        check_region_input_step(&writer, CHECK_REGION_END, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        check_region_input_statement(&writer, region->block, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
    }
    while (writer.depth != 0 && !writer.unsupported && !reader->output->resource)
    {
        const CheckRegionInputTask task = writer.tasks[--writer.depth];
        if (task.kind == CHECK_REGION_INPUT_LABEL)
        {
            writer.labels[task.token] = graph->region_step_count - writer.first;
        }
        else if (task.kind == CHECK_REGION_INPUT_STEP)
        {
            if (graph->region_step_count < graph->region_step_capacity)
            {
                graph->region_steps[graph->region_step_count++] = task.step;
            }
            else
            {
                reader->output->resource = true;
            }
        }
        else if (task.kind == CHECK_REGION_INPUT_TOKEN && task.token != region->final_statement)
        {
            const uint32_t first = writer.depth;
            for (uint32_t node = reader->region_roots[task.token]; node != BUSTER_CHECK_NONE; node = reader->value_next[node])
            {
                uint32_t value = node;
                while (graph->ops[value] == CHECK_OP_CAST || graph->ops[value] == CHECK_OP_CONVERT)
                {
                    value = graph->operand0[value];
                }
                const bool initializer = graph->ops[value] == CHECK_OP_INITIALIZE;
                const uint32_t declaration = initializer ? reader->initializer_declarations[graph->tokens[value]] : BUSTER_CHECK_NONE;
                if (!initializer || declaration == BUSTER_CHECK_NONE || !reader->static_objects[declaration])
                {
                    // Keep the initializer's checked destination conversion visible, including a volatile
                    // automatic store. The surrounding void cast does not add an initialization effect.
                    check_region_input_step(&writer, CHECK_REGION_EVAL, initializer ? value : node, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                }
            }
            for (uint32_t a = first, b = writer.depth; a < b && a < --b; a += 1)
            {
                const CheckRegionInputTask saved = writer.tasks[a];
                writer.tasks[a] = writer.tasks[b];
                writer.tasks[b] = saved;
            }
        }
        else if (task.kind == CHECK_REGION_INPUT_STATEMENT)
        {
            check_region_input_expand(&writer, task);
        }
    }
    if (writer.unsupported && !reader->output->resource)
    {
        graph->region_step_count = writer.first;
        if (graph->region_step_count < graph->region_step_capacity)
        {
            graph->region_steps[graph->region_step_count++] = (CheckRegionProofStep)
            {
                .op = CHECK_REGION_END,
                .node = BUSTER_CHECK_NONE,
                .operand = BUSTER_CHECK_NONE,
                .target = BUSTER_CHECK_NONE,
            };
        }
        else
        {
            reader->output->resource = true;
        }
    }
    for (uint32_t i = writer.first; !reader->output->resource && i < graph->region_step_count; i += 1)
    {
        CheckRegionProofStep* step = graph->region_steps + i;
        if (step->target != BUSTER_CHECK_NONE)
        {
            if (step->target >= writer.label_count || writer.labels[step->target] == BUSTER_CHECK_NONE)
            {
                reader->output->resource = true;
            }
            else
            {
                step->target = writer.labels[step->target];
            }
        }
    }
    uint32_t result = BUSTER_CHECK_NONE;
    if (!reader->output->resource)
    {
        result = graph->region_proof_count++;
        graph->region_proofs[result] = (CheckRegionProof)
        {
            .node = BUSTER_CHECK_NONE,
            .first = writer.first,
            .count = graph->region_step_count - writer.first,
            .flags = writer.unsupported ? CHECK_REGION_UNSUPPORTED : 0,
        };
    }
    arena_pop_to(reader->scratch, mark);
    return result;
}
