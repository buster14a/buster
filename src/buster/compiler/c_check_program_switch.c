// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Constant-switch emission (#1192), included after check_control_drop. This is an original, nonrecursive source
// collector, not an interpreter: it proves that the switch can be replaced by a list of statements before changing
// the writer. The existing expression proof, case conversion and dropped-range consumers remain authoritative.
// Frames describe compounds on the selected case's path. A complete retained compound keeps its own scope, while
// a terminating break uses the ordinary writer's cleanup, stack-restore and lifetime-exit machinery.

typedef struct CheckSwitchFrame CheckSwitchFrame;
struct CheckSwitchFrame
{
    uint32_t token;
    uint32_t next;
    uint32_t first;
    bool entered;
    bool skipped_declaration;
    bool live_declaration;
};

// Case labels are handled by the collector, not by ContainsLabel's ordinary-label veto.
static bool check_control_switch_labeled(const CheckControlWriter* writer, uint32_t statement)
{
    uint32_t low = 0;
    uint32_t high = 0;
    check_control_extent(writer, statement, &low, &high);
    const uint8_t* roles = writer->parser->tree.roles;
    bool labeled = false;
    for (uint32_t t = low; !labeled && t <= high; t += 1)
    {
        labeled = roles[t] == PARSE_ROLE_LABEL;
    }
    return labeled;
}

// A declaration this statement adds to its surrounding scope. Declarations behind a compound, loop, condition or
// statement-expression boundary belong to that boundary instead; case/ordinary labels and attributes are transparent.
static bool check_control_switch_declares(const CheckControlWriter* writer, uint32_t statement)
{
    const Parser* parser = writer->parser;
    const CheckInput* input = writer->program->input;
    const uint8_t* roles = parser->tree.roles;
    const uint32_t* parents = parser->tree.parents;
    const uint32_t count = parser->tokens.count;
    const uint8_t role = roles[statement];
    const bool transparent = role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
    uint32_t low = 0;
    uint32_t high = 0;
    check_control_extent(writer, statement, &low, &high);
    bool declares = false;
    for (uint32_t t = low; transparent && !declares && t <= high; t += 1)
    {
        if (input->declaration_kinds[t] != 0)
        {
            uint32_t ancestor = parents[t];
            bool terminated = false;
            bool same = true;
            uint32_t steps = 0;
            while (same && ancestor < count && ancestor != statement && steps < count)
            {
                const uint8_t ancestor_role = roles[ancestor];
                same = !terminated || ancestor_role == PARSE_ROLE_TERMINATOR || ancestor_role == PARSE_ROLE_CLOSE_CASE ||
                       ancestor_role == PARSE_ROLE_LABEL || ancestor_role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
                terminated |= ancestor_role == PARSE_ROLE_TERMINATOR;
                ancestor = parents[ancestor];
                steps += 1;
            }
            declares = same && ancestor == statement;
        }
    }
    return declares;
}

// A loop or nested switch owns its own breaks; an if, attributed statement or ordinary label does not.
static bool check_control_switch_break_scope(const CheckControlWriter* writer, uint32_t token)
{
    const Parser* parser = writer->parser;
    const uint8_t role = parser->tree.roles[token];
    bool scope = role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_DO;
    if (role == PARSE_ROLE_CLOSE_CONDITION)
    {
        const uint32_t* next = writer->next;
        for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE && child < token; child = next[child])
        {
            const uint8_t kind = parser->tokens.kinds[child];
            scope |= parser->tree.roles[child] == PARSE_ROLE_LEAF_RIGHT && (kind == PARSE_KIND_WHILE || kind == PARSE_KIND_SWITCH);
        }
    }
    return scope;
}

// Whether keeping this opaque statement would leave a break aimed at the switch being removed. This is syntactic:
// an if(0)'s break still prevents collection, while a nested loop's break does not.
static bool check_control_switch_breaks(const CheckControlWriter* writer, uint32_t statement)
{
    const Parser* parser = writer->parser;
    const uint8_t* roles = parser->tree.roles;
    const uint8_t* kinds = parser->tokens.kinds;
    const uint32_t* parents = parser->tree.parents;
    const uint32_t count = parser->tokens.count;
    uint32_t low = 0;
    uint32_t high = 0;
    check_control_extent(writer, statement, &low, &high);
    bool escapes = false;
    for (uint32_t t = low; !escapes && t <= high; t += 1)
    {
        if (roles[t] == PARSE_ROLE_RETURN && kinds[t] == PARSE_KIND_BREAK)
        {
            uint32_t ancestor = parents[t];
            uint32_t steps = 0;
            bool owned = false;
            while (!owned && ancestor < count && steps < count)
            {
                owned = check_control_switch_break_scope(writer, ancestor);
                if (ancestor == statement)
                {
                    break;
                }
                ancestor = parents[ancestor];
                steps += 1;
            }
            escapes = !owned;
        }
    }
    return escapes;
}

static bool check_control_switch_direct_break(const CheckControlWriter* writer, uint32_t statement)
{
    const Parser* parser = writer->parser;
    const uint32_t* next = writer->next;
    bool stops = false;
    for (uint32_t child = parser->tree.roles[statement] == PARSE_ROLE_TERMINATOR ? writer->first[statement] : BUSTER_CHECK_NONE;
         child != BUSTER_CHECK_NONE; child = next[child])
    {
        stops |= parser->tree.roles[child] == PARSE_ROLE_RETURN && parser->tokens.kinds[child] == PARSE_KIND_BREAK;
    }
    return stops;
}

// Each source statement is visited once. There is at most one frame per token and two kept tasks per token (a source
// statement and, for a fully entered compound, its optional lifetime start). Storage is supplied only for a known
// constant selector and is restored before returning. No writer state changes on a failed collection.
static bool check_control_switch_collect(const CheckControlWriter* writer, uint32_t body, uint32_t selected, CheckSwitchFrame* frames,
                                          CheckControlTask* kept, uint32_t* kept_count)
{
    const Parser* parser = writer->parser;
    const uint8_t* roles = parser->tree.roles;
    const uint32_t* next = writer->next;
    uint32_t depth = 0;
    uint32_t count = 0;
    uint32_t current = body;
    bool found = false;
    bool stopped = false;
    bool valid = true;
    while (valid && (current != BUSTER_CHECK_NONE || depth != 0))
    {
        if (current != BUSTER_CHECK_NONE)
        {
            const uint32_t statement = current;
            const uint8_t role = roles[statement];
            current = BUSTER_CHECK_NONE;
            if (stopped)
            {
                valid = !check_control_switch_labeled(writer, statement);
            }
            else if (role == PARSE_ROLE_CLOSE_CASE)
            {
                found |= statement == selected;
                for (uint32_t child = writer->first[statement]; child != BUSTER_CHECK_NONE; child = next[child])
                {
                    current = child > statement ? child : current;
                }
            }
            else if (role == PARSE_ROLE_CLOSE_BLOCK)
            {
                frames[depth++] = (CheckSwitchFrame)
                {
                    .token = statement,
                    .next = writer->first[statement],
                    .first = count,
                    .entered = found,
                };
                // A compound entered through a case already has no lifetime markers (check_program_lifetime_entered).
                // A fully entered one ending in a break still needs its start; the ordinary break emits its end.
                const uint32_t* lifetimes = writer->program->lifetime_first;
                if (found && lifetimes != NULL && lifetimes[statement] != BUSTER_CHECK_NONE)
                {
                    kept[count++] = (CheckControlTask)
                    {
                        .kind = CHECK_CONTROL_TASK_ENTRY,
                        .entry = { .node = 0, .token = statement, .target = statement, .kind = CHECK_CONTROL_LIFETIME },
                        .token = statement,
                    };
                }
            }
            else if (!found)
            {
                valid = !check_control_switch_labeled(writer, statement);
            }
            else
            {
                const bool direct = check_control_switch_direct_break(writer, statement);
                valid = direct || !check_control_switch_breaks(writer, statement);
                if (valid)
                {
                    kept[count++] = (CheckControlTask){ .kind = CHECK_CONTROL_TASK_STATEMENT, .token = statement };
                    stopped = direct;
                }
            }
        }
        else
        {
            CheckSwitchFrame* frame = frames + depth - 1;
            valid = !found || !frame->skipped_declaration;
            uint32_t child = frame->next;
            while (child != BUSTER_CHECK_NONE && roles[child] == PARSE_ROLE_OPEN_BLOCK)
            {
                child = next[child];
            }
            if (valid && child != BUSTER_CHECK_NONE)
            {
                frame->next = next[child];
                if (!stopped)
                {
                    const bool declares = check_control_switch_declares(writer, child);
                    frame->skipped_declaration |= !found && declares;
                    frame->live_declaration |= found && declares;
                }
                current = child;
            }
            else
            {
                if (valid && found && !stopped && frame->live_declaration)
                {
                    // Keeping a complete compound preserves declarations' scope exits. A partially entered one with
                    // declarations and no terminating break cannot be flattened, so retain the original switch.
                    valid = frame->entered;
                    if (valid)
                    {
                        count = frame->first;
                        kept[count++] = (CheckControlTask){ .kind = CHECK_CONTROL_TASK_STATEMENT, .token = frame->token };
                    }
                }
                depth -= 1;
            }
        }
    }
    *kept_count = count;
    return valid && found;
}

static bool check_control_switch_prune(CheckControlWriter* writer, uint32_t keyword, uint32_t token, uint32_t body, uint32_t condition, uint32_t end, uint32_t repeat)
{
    CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const uint32_t expression = graph->ops[condition] == CHECK_OP_CONDITION ? graph->operand0[condition] : condition;
    const CheckValue value = expression < graph->count ? check_stream_value(program->stream, expression) : (CheckValue){ 0 };
    bool folded = value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE && (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                  check_control_foldable(writer, expression) && !check_control_labeled(writer, keyword + 1, token);
    uint32_t selected = BUSTER_CHECK_NONE;
    uint32_t fallback = BUSTER_CHECK_NONE;
    uint32_t range = 0;
    const uint32_t* case_next = writer->case_next;
    for (uint32_t c = folded ? writer->case_first[token] : BUSTER_CHECK_NONE; c != BUSTER_CHECK_NONE; c = case_next[c])
    {
        const CheckProgramCase* item = program->cases + c;
        if (item->fallback)
        {
            fallback = item->token;
        }
        else if (item->range)
        {
            range = item->token > range ? item->token : range;
        }
        else
        {
            unsigned __int128 integer = 0;
            const bool matches = check_program_case_value(program, expression, item->type, &integer, NULL) && integer == item->lower;
            selected = matches && (selected == BUSTER_CHECK_NONE || item->token > selected) ? item->token : selected;
        }
    }
    // Clang scans its reverse-source-order list only up to the first match. A range encountered before that match
    // refuses the shortcut, even when the range cannot match. This is independent of our linked list's order.
    folded = folded && (range == 0 || (selected != BUSTER_CHECK_NONE && selected > range));
    selected = selected != BUSTER_CHECK_NONE ? selected : fallback;
    if (folded)
    {
        uint32_t low = 0;
        uint32_t high = 0;
        check_control_extent(writer, body, &low, &high);
        Arena* scratch = writer->switch_scratch;
        const uint64_t mark = scratch->length;
        const uint64_t extent = (uint64_t)high - low + 1;
        CheckSwitchFrame* frames = selected != BUSTER_CHECK_NONE ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckSwitchFrame, extent) : NULL;
        CheckControlTask* kept = selected != BUSTER_CHECK_NONE ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckControlTask, 2 * extent) : NULL;
        const bool room = selected == BUSTER_CHECK_NONE || (frames != NULL && kept != NULL && extent <= UINT32_MAX / 2);
        uint32_t count = 0;
        folded = room && (selected == BUSTER_CHECK_NONE ? !check_control_switch_labeled(writer, body) :
                          check_control_switch_collect(writer, body, selected, frames, kept, &count));
        if (!room)
        {
            check_program_fail(program, CHECK_PROGRAM_RESOURCE, token);
        }
        if (folded)
        {
            check_control_mark(writer, end);
            check_control_drop(writer, keyword + 1, token);
            // Retained statements' disjoint extents are in source order. Drop their complement, including stripped
            // case wrappers, so locals, statics, references and statement-expression regions share this decision.
            uint32_t cursor = low;
            for (uint32_t i = 0; i < count; i += 1)
            {
                if (kept[i].kind == CHECK_CONTROL_TASK_STATEMENT)
                {
                    uint32_t first = 0;
                    uint32_t last = 0;
                    check_control_extent(writer, kept[i].token, &first, &last);
                    if (cursor < first)
                    {
                        check_control_drop(writer, cursor, first - 1);
                    }
                    cursor = last + 1;
                }
            }
            if (cursor <= high)
            {
                check_control_drop(writer, cursor, high);
            }
            for (uint32_t i = count; i-- > 0;)
            {
                if (kept[i].kind == CHECK_CONTROL_TASK_STATEMENT)
                {
                    check_control_statement(writer, kept[i].token, end, repeat);
                }
                else
                {
                    check_control_push(writer, kept[i]);
                }
            }
        }
        arena_pop_to(scratch, mark);
    }
    return folded;
}

// Keep the unfolder's dispatch unchanged: the same case controls, fallback edge and jump-table admission.
static void check_control_switch(CheckControlWriter* writer, uint32_t keyword, uint32_t token, uint32_t body, uint32_t condition, uint32_t end, uint32_t repeat)
{
    writer->stops[token] = end;
    if (!check_program_switch_type(writer->program->types, check_stream_type(writer->program->stream, condition)))
    {
        check_program_fail(writer->program, CHECK_PROGRAM_INVALID, token);
    }
    if (writer->program->status == CHECK_PROGRAM_OK && !check_control_switch_prune(writer, keyword, token, body, condition, end, repeat))
    {
        uint32_t fallback = end;
        for (uint32_t c = writer->case_first[token]; c != BUSTER_CHECK_NONE; c = writer->case_next[c])
        {
            const CheckProgramCase* item = writer->program->cases + c;
            if (item->fallback)
            {
                fallback = writer->source_labels[item->token];
            }
        }
        check_control_mark(writer, end);
        check_control_statement(writer, body, end, repeat);
        check_control_entry(writer, CHECK_CONTROL_JUMP, token, BUSTER_CHECK_NONE, fallback);
        uint32_t cases = 0;
        for (uint32_t c = writer->case_first[token]; c != BUSTER_CHECK_NONE; c = writer->case_next[c])
        {
            if (!writer->program->cases[c].fallback)
            {
                check_control_entry(writer, CHECK_CONTROL_CASE, writer->program->cases[c].token, BUSTER_CHECK_NONE, c);
                cases += 1;
            }
        }
        check_control_entry(writer, CHECK_CONTROL_SWITCH, token, condition, BUSTER_CHECK_NONE);
        if (cases >= 4 && writer->switch_count < writer->switch_capacity)
        {
            writer->switches[writer->switch_count++] = writer->count;
        }
    }
}
