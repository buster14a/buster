// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Statements clang -O0 does not emit (#2987). Included after check_control_enterable, check_control_drop and the
// constant-switch collector. A block is walked in source order. A statement with no insertion point and no label or
// outer case (check_control_enterable, clang's ContainsLabel) is left out with the constant if's dropped ranges, so
// its calls, statics and locals are not selected. What follows a return, break, continue, goto or
// __builtin_unreachable() has no insertion point. An if whose taken arms both end that way, and a while or for whose
// condition is constantly true and that no break leaves, end the same way. __builtin_trap() does not, and a do's
// following statement stays: clang -O0 still names a function only that statement calls. The walk is ordered because
// each statement's insertion point is the one before it. Nesting past BUSTER_CHECK_REACH_DEPTH keeps the following
// statements.

#define BUSTER_CHECK_REACH_DEPTH 64u

static bool check_control_completes(CheckControlWriter* writer, uint32_t token, bool entered);

static bool check_control_sequence_child(const CheckControlWriter* writer, uint32_t child)
{
    const uint8_t role = writer->parser->tree.roles[child];
    const bool result = role != PARSE_ROLE_OPEN_BODY && role != PARSE_ROLE_OPEN_BLOCK;
    return result;
}

// The return, break, continue or goto of statement `token`, through a chain of attribute specifiers, or none.
static uint32_t check_control_reach_jump(const CheckControlWriter* writer, uint32_t token)
{
    const Parser* parser = writer->parser;
    uint32_t jump = BUSTER_CHECK_NONE;
    for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE; child = writer->next[child])
    {
        const uint8_t child_role = parser->tree.roles[child];
        const uint32_t attributed = child_role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ? check_control_attributed(writer, child) : BUSTER_CHECK_NONE;
        if (attributed != BUSTER_CHECK_NONE)
        {
            jump = attributed;
        }
        else if (child_role == PARSE_ROLE_RETURN)
        {
            jump = child;
        }
    }
    return jump;
}

// Whether `token` sits in a loop or switch a break leaves, or (when `loops_only`) in a loop a continue repeats.
static bool check_control_reach_scope(const CheckControlWriter* writer, uint32_t token, bool loops_only)
{
    const Parser* parser = writer->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = token < count ? parser->tree.parents[token] : count;
    bool found = false;
    for (uint32_t steps = 0; !found && ancestor < count && steps < count; steps += 1)
    {
        const bool scope = check_control_switch_break_scope(writer, ancestor);
        const uint8_t role = parser->tree.roles[ancestor];
        bool loop = role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_DO;
        if (role == PARSE_ROLE_CLOSE_CONDITION)
        {
            for (uint32_t child = writer->first[ancestor]; child != BUSTER_CHECK_NONE && child < ancestor; child = writer->next[child])
            {
                loop |= parser->tree.roles[child] == PARSE_ROLE_LEAF_RIGHT && parser->tokens.kinds[child] == PARSE_KIND_WHILE;
            }
        }
        found = loops_only ? loop : scope;
        ancestor = parser->tree.parents[ancestor];
    }
    return found;
}

// A break in `body` whose nearest break scope is `loop`. An inner loop's or switch's break is not one.
static bool check_control_reach_breaks(const CheckControlWriter* writer, uint32_t loop, uint32_t body)
{
    const Parser* parser = writer->parser;
    uint32_t low = 0;
    uint32_t high = 0;
    bool found = false;
    if (body != BUSTER_CHECK_NONE)
    {
        check_control_extent(writer, body, &low, &high);
    }
    for (uint32_t token = body != BUSTER_CHECK_NONE ? low : 1; !found && token <= high && body != BUSTER_CHECK_NONE; token += 1)
    {
        if (parser->tree.roles[token] == PARSE_ROLE_RETURN && parser->tokens.kinds[token] == PARSE_KIND_BREAK)
        {
            uint32_t ancestor = parser->tree.parents[token];
            bool owned = false;
            for (uint32_t steps = 0; !owned && ancestor < parser->tokens.count && steps < parser->tokens.count; steps += 1)
            {
                if (check_control_switch_break_scope(writer, ancestor))
                {
                    found = ancestor == loop;
                    owned = true;
                }
                ancestor = parser->tree.parents[ancestor];
            }
        }
    }
    return found;
}

// The condition expression of a while or for, or none when a for has no condition (`for (;;)`).
static uint32_t check_control_reach_condition(const CheckControlWriter* writer, uint32_t token)
{
    const CheckInput* input = writer->program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t node = token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    const uint32_t first = writer->token_first[token];
    const uint32_t chosen = node != BUSTER_CHECK_NONE ? node : first;
    const uint32_t expression = chosen < graph->count && graph->ops[chosen] == CHECK_OP_CONDITION ? graph->operand0[chosen] : chosen;
    return expression;
}

static bool check_control_reach_always(const CheckControlWriter* writer, uint32_t expression, bool absent)
{
    bool truth = false;
    const bool result = absent || (check_control_fold_truth(writer, expression, &truth) && truth);
    return result;
}

// Whether `node`, through casts and both sides of a comma, is __builtin_unreachable. A trap is not one.
static bool check_control_reach_unreachable(const CheckControlWriter* writer, uint32_t node)
{
    const CheckGraph* graph = &writer->program->input->graph;
    bool found = false;
    uint32_t cursor = node;
    for (uint32_t steps = 0; cursor < graph->count && steps < graph->count && !found; steps += 1)
    {
        const CheckOp op = graph->ops[cursor];
        if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT)
        {
            cursor = graph->operand0[cursor];
        }
        else if (op == CHECK_OP_COMMA)
        {
            const uint32_t right = graph->operand1[cursor];
            found = right < graph->count && graph->ops[right] == CHECK_OP_BUILTIN_MISC && graph->members[right] == CBUILTIN_UNREACHABLE;
            cursor = found ? graph->count : graph->operand0[cursor];
        }
        else
        {
            found = op == CHECK_OP_BUILTIN_MISC && graph->members[cursor] == CBUILTIN_UNREACHABLE;
            cursor = graph->count;
        }
    }
    return found;
}

static bool check_control_reach_statement_unreachable(const CheckControlWriter* writer, uint32_t token)
{
    const CheckInput* input = writer->program->input;
    const uint32_t node = token < input->token_count && input->token_nodes[token] != BUSTER_CHECK_NONE ? input->token_nodes[token] :
                          writer->token_first[token];
    const bool result = check_control_reach_unreachable(writer, node);
    return result;
}

// The if, while or switch keyword of a CLOSE_CONDITION, and its body and else.
static void check_control_reach_condition_children(const CheckControlWriter* writer, uint32_t token, uint32_t* keyword, uint32_t* body, uint32_t* alternate)
{
    const Parser* parser = writer->parser;
    *keyword = BUSTER_CHECK_NONE;
    *body = BUSTER_CHECK_NONE;
    *alternate = BUSTER_CHECK_NONE;
    for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE; child = writer->next[child])
    {
        if (child < token && parser->tree.roles[child] == PARSE_ROLE_LEAF_RIGHT)
        {
            *keyword = child;
        }
        else if (child > token && parser->tree.roles[child] == PARSE_ROLE_ELSE)
        {
            *alternate = child;
        }
        else if (child > token)
        {
            *body = child;
        }
    }
}

static bool check_control_completes_sequence(CheckControlWriter* writer, uint32_t token, bool entered, uint32_t after, bool skip_final)
{
    bool ip = entered;
    for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE; child = writer->next[child])
    {
        const bool kept = check_control_sequence_child(writer, child) && child > after && !(skip_final && child == writer->region_final);
        if (kept && (ip || check_control_enterable(writer, child)))
        {
            ip = check_control_completes(writer, child, ip);
        }
    }
    return ip;
}

static bool check_control_completes_if(CheckControlWriter* writer, uint32_t token, uint32_t keyword, uint32_t body, uint32_t alternate)
{
    const CheckInput* input = writer->program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t condition = input->token_nodes[token];
    const uint32_t expression = condition < graph->count && graph->ops[condition] == CHECK_OP_CONDITION ? graph->operand0[condition] : condition;
    bool truth = false;
    const bool folded = keyword != BUSTER_CHECK_NONE && body != BUSTER_CHECK_NONE && check_control_fold_truth(writer, expression, &truth) &&
                        !check_control_labeled(writer, keyword + 1, token) && !check_control_enterable(writer, truth ? alternate : body);
    bool result = true;
    if (folded)
    {
        const uint32_t executed = truth ? body : alternate;
        // No taken arm (`if (0)` without an else) falls through. A taken return does not.
        result = executed == BUSTER_CHECK_NONE || check_control_completes(writer, executed, true);
    }
    else if (alternate == BUSTER_CHECK_NONE)
    {
        result = true;
    }
    else
    {
        const bool then_ip = body != BUSTER_CHECK_NONE && check_control_completes(writer, body, true);
        const bool else_ip = check_control_completes(writer, alternate, true);
        result = then_ip || else_ip;
    }
    return result;
}

static bool check_control_completes_body(CheckControlWriter* writer, uint32_t token, bool entered)
{
    const Parser* parser = writer->parser;
    const uint8_t role = parser->tree.roles[token];
    bool result = true;
    if (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO)
    {
        result = check_control_completes_sequence(writer, token, entered, writer->opening, false);
    }
    else if (role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE)
    {
        // The label or case creates the insertion point. Statements after it are its children, and a return among
        // them leaves out the rest, the same way a block does. The region's value is not one of those statements.
        result = check_control_completes_sequence(writer, token, true, token, true);
    }
    else if (role == PARSE_ROLE_TERMINATOR)
    {
        const uint32_t jump = check_control_reach_jump(writer, token);
        const uint8_t kind = jump != BUSTER_CHECK_NONE ? parser->tokens.kinds[jump] : 0;
        uint32_t do_body = BUSTER_CHECK_NONE;
        for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            do_body = parser->tree.roles[child] == PARSE_ROLE_DO ? child : do_body;
        }
        if (kind == PARSE_KIND_RETURN || kind == PARSE_KIND_GOTO)
        {
            result = false;
        }
        else if (kind == PARSE_KIND_BREAK)
        {
            result = !check_control_reach_scope(writer, jump, false);
        }
        else if (kind == PARSE_KIND_CONTINUE)
        {
            result = !check_control_reach_scope(writer, jump, true);
        }
        else if (do_body != BUSTER_CHECK_NONE)
        {
            // clang -O0 still emits the statement after a do, including `do {} while (1)`, so a function only that
            // statement calls stays selected. Statements inside the body are this walk's when the body is emitted.
            result = true;
        }
        else
        {
            result = !check_control_reach_statement_unreachable(writer, token);
        }
    }
    else if (role == PARSE_ROLE_CLOSE_CONDITION)
    {
        uint32_t keyword = BUSTER_CHECK_NONE;
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t alternate = BUSTER_CHECK_NONE;
        check_control_reach_condition_children(writer, token, &keyword, &body, &alternate);
        const uint8_t kind = keyword != BUSTER_CHECK_NONE ? parser->tokens.kinds[keyword] : 0;
        if (kind == PARSE_KIND_IF)
        {
            result = check_control_completes_if(writer, token, keyword, body, alternate);
        }
        else if (kind == PARSE_KIND_WHILE)
        {
            const uint32_t expression = check_control_reach_condition(writer, token);
            const bool always = check_control_reach_always(writer, expression, false);
            const bool breaks = check_control_reach_breaks(writer, token, body);
            result = !always || breaks;
        }
        else
        {
            // A switch falls through when no case matches. Its body is emitted with no insertion point, so a
            // statement before the first case is left out there.
            result = true;
        }
    }
    else if (role == PARSE_ROLE_CLOSE_FOR)
    {
        uint32_t first = BUSTER_CHECK_NONE;
        uint32_t condition = BUSTER_CHECK_NONE;
        uint32_t body = BUSTER_CHECK_NONE;
        for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            const bool semi = child < token && parser->tokens.kinds[child] == PARSE_KIND_SEMI;
            condition = semi && first != BUSTER_CHECK_NONE ? child : condition;
            first = semi && first == BUSTER_CHECK_NONE ? child : first;
            body = child > token ? child : body;
        }
        const uint32_t expression = condition != BUSTER_CHECK_NONE ? check_control_reach_condition(writer, condition) : BUSTER_CHECK_NONE;
        const bool absent = condition != BUSTER_CHECK_NONE && writer->token_first[condition] == BUSTER_CHECK_NONE &&
                            (condition >= writer->program->input->token_count || writer->program->input->token_nodes[condition] == BUSTER_CHECK_NONE);
        const bool always = check_control_reach_always(writer, expression, absent);
        const bool breaks = check_control_reach_breaks(writer, token, body);
        result = !always || breaks;
    }
    return result;
}

static bool check_control_completes(CheckControlWriter* writer, uint32_t token, bool entered)
{
    bool result = true;
    if (writer->reach_depth < BUSTER_CHECK_REACH_DEPTH && token != BUSTER_CHECK_NONE)
    {
        writer->reach_depth += 1;
        result = check_control_completes_body(writer, token, entered);
        writer->reach_depth -= 1;
    }
    return result;
}

// The statements of `parent` after `after`, in source order. One with no insertion point and no label or outer case
// is dropped. The ones that stay are pushed last-to-first, which is the order the writer's stack emits them.
static void check_control_emit_sequence(CheckControlWriter* writer, uint32_t parent, uint32_t stop, uint32_t repeat, bool entered, uint32_t after, bool skip_final)
{
    Arena* scratch = writer->switch_scratch;
    const uint64_t mark = scratch->length;
    const uint32_t room = writer->parser->tokens.count + 1;
    uint32_t* list = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, room);
    uint8_t* at = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, room);
    bool ip = entered;
    uint32_t count = 0;
    if (list == NULL || at == NULL)
    {
        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, parent);
    }
    for (uint32_t child = list != NULL && at != NULL ? writer->first[parent] : BUSTER_CHECK_NONE; child != BUSTER_CHECK_NONE; child = writer->next[child])
    {
        const bool kept = check_control_sequence_child(writer, child) && child > after && !(skip_final && child == writer->region_final);
        if (kept)
        {
            if (!ip && !check_control_enterable(writer, child))
            {
                uint32_t low = 0;
                uint32_t high = 0;
                check_control_extent(writer, child, &low, &high);
                if (low <= high && writer->dropped_range_count < writer->parser->tokens.count)
                {
                    check_control_drop(writer, low, high);
                }
            }
            else if (count < room)
            {
                list[count] = child;
                at[count] = ip ? 1 : 0;
                count += 1;
                ip = check_control_completes(writer, child, ip);
            }
        }
    }
    for (uint32_t i = count; i-- > 0;)
    {
        check_control_statement_at(writer, list[i], stop, repeat, at[i] != 0);
    }
    arena_pop_to(scratch, mark);
}
