// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Statements clang -O0 does not emit (#2987). Included after check_control_enterable, check_control_drop and the
// constant-switch collector. A block is walked in source order. A statement with no insertion point and no label or
// outer case (check_control_enterable, clang's ContainsLabel) is left out with the constant if's dropped ranges, so
// its calls and statics are not selected. What follows a return, break, continue, goto or
// __builtin_unreachable() has no insertion point. An if whose taken arms both end that way, a while whose condition
// is constantly true, and a `for (;;)` that no break leaves, end the same way. A for that has a condition does
// not: clang still branches to its exit. __builtin_trap() does not, and a do's
// following statement stays: clang -O0 still names a function only that statement calls. An automatic local the
// dropped statement declares keeps its place when an emitted statement names it; its initializer is not emitted.
// A block-scope static it declares is still emitted, as clang emits that declaration with no insertion point.
// The walk is ordered because each statement's insertion point is the one before it. The statement tree is an explicit stack
// (BUSTER_CHECK_REACH_DEPTH frames): past that, the following statements stay, which keeps a reachable call.

#define BUSTER_CHECK_REACH_DEPTH 128u

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
    const uint32_t first = check_control_statement_head(writer, token);
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
                          check_control_statement_head(writer, token);
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

// A loop or switch is a leaf here: its body is emitted as a separate task with its own jump targets.
// The blocks, labels and if arms visited together by check_control_completes therefore share stop and repeat.
static bool check_control_reach_leaf(const CheckControlWriter* writer, uint32_t token, uint32_t stop, uint32_t repeat, bool* completes)
{
    const Parser* parser = writer->parser;
    const uint8_t role = parser->tree.roles[token];
    bool leaf = true;
    bool result = true;
    if (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO ||
        role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE)
    {
        leaf = false;
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
            result = stop == BUSTER_CHECK_NONE;
        }
        else if (kind == PARSE_KIND_CONTINUE)
        {
            result = repeat == BUSTER_CHECK_NONE;
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
            leaf = false;
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
        // Clang's EmitForStmt branches to the exit whenever the for has a condition, even `for (; 1;)`. Only
        // `for (;;)` leaves that block unused, so only then is there no insertion point after the loop.
        const bool absent = condition != BUSTER_CHECK_NONE && check_control_statement_head(writer, condition) == BUSTER_CHECK_NONE &&
                            (condition >= writer->program->input->token_count || writer->program->input->token_nodes[condition] == BUSTER_CHECK_NONE);
        const bool breaks = check_control_reach_breaks(writer, token, body);
        result = !absent || breaks;
    }
    *completes = result;
    return leaf;
}

// Whether an if folds, and which arm it keeps. A missing arm falls through (`if (0)` with no else).
static bool check_control_reach_if(const CheckControlWriter* writer, uint32_t token, bool* folded, uint32_t* first, uint32_t* second)
{
    const Parser* parser = writer->parser;
    const CheckInput* input = writer->program->input;
    const CheckGraph* graph = &input->graph;
    uint32_t keyword = BUSTER_CHECK_NONE;
    uint32_t body = BUSTER_CHECK_NONE;
    uint32_t alternate = BUSTER_CHECK_NONE;
    check_control_reach_condition_children(writer, token, &keyword, &body, &alternate);
    const uint32_t condition = token < input->token_count ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    const uint32_t expression = condition < graph->count && graph->ops[condition] == CHECK_OP_CONDITION ? graph->operand0[condition] : condition;
    bool truth = false;
    const bool folds = keyword != BUSTER_CHECK_NONE && parser->tokens.kinds[keyword] == PARSE_KIND_IF && body != BUSTER_CHECK_NONE &&
                       check_control_fold_truth(writer, expression, &truth) && !check_control_labeled(writer, keyword + 1, token) &&
                       !check_control_enterable(writer, truth ? alternate : body);
    *folded = folds;
    *first = folds ? (truth ? body : alternate) : body;
    *second = folds || alternate == BUSTER_CHECK_NONE ? BUSTER_CHECK_NONE : alternate;
    return alternate == BUSTER_CHECK_NONE && !folds;
}

typedef enum CheckReachStep
{
    CHECK_REACH_STATEMENT,
    CHECK_REACH_SEQUENCE,
    CHECK_REACH_IF_THEN,
    CHECK_REACH_IF_ELSE,
} CheckReachStep;

typedef struct CheckReachFrame CheckReachFrame;
struct CheckReachFrame
{
    uint32_t token;
    uint32_t child;
    uint32_t after;
    uint32_t alternate;
    CheckReachStep step;
    bool entered;
    bool ip;
    bool skip_final;
    bool then_ip;
};

// Whether control leaves `token` for the next statement. `entered` is false when the statement was reached only
// through a label inside it. An explicit stack: the call graph has no cycle here.
static bool check_control_completes(CheckControlWriter* writer, uint32_t token, bool entered, uint32_t stop, uint32_t repeat)
{
    CheckReachFrame stack[BUSTER_CHECK_REACH_DEPTH];
    uint32_t depth = 0;
    bool result = true;
    bool have = false;
    bool overflow = token == BUSTER_CHECK_NONE;
    if (!overflow)
    {
        stack[0] = (CheckReachFrame){ .token = token, .step = CHECK_REACH_STATEMENT, .entered = entered };
        depth = 1;
    }
    while (depth != 0 && !overflow)
    {
        CheckReachFrame* frame = stack + depth - 1;
        if (have && frame->step == CHECK_REACH_SEQUENCE)
        {
            frame->ip = result;
            frame->child = writer->next[frame->child];
            have = false;
        }
        else if (have && frame->step == CHECK_REACH_IF_THEN)
        {
            frame->then_ip = result;
            frame->step = CHECK_REACH_IF_ELSE;
            have = false;
            if (depth >= BUSTER_CHECK_REACH_DEPTH)
            {
                overflow = true;
            }
            else
            {
                stack[depth++] = (CheckReachFrame){ .token = frame->alternate, .step = CHECK_REACH_STATEMENT, .entered = true };
            }
        }
        else if (have && frame->step == CHECK_REACH_IF_ELSE)
        {
            result = frame->then_ip || result;
            depth -= 1;
        }
        else if (frame->step == CHECK_REACH_STATEMENT)
        {
            const uint8_t role = writer->parser->tree.roles[frame->token];
            bool leaf_result = true;
            if (check_control_reach_leaf(writer, frame->token, stop, repeat, &leaf_result))
            {
                result = leaf_result;
                have = true;
                depth -= 1;
            }
            else if (role == PARSE_ROLE_CLOSE_CONDITION)
            {
                bool folded = false;
                uint32_t first = BUSTER_CHECK_NONE;
                uint32_t second = BUSTER_CHECK_NONE;
                const bool falls = check_control_reach_if(writer, frame->token, &folded, &first, &second);
                if (falls || (folded && first == BUSTER_CHECK_NONE))
                {
                    // No else, or `if (0)` with no else: control continues after the if.
                    result = true;
                    have = true;
                    depth -= 1;
                }
                else if (folded)
                {
                    frame->token = first;
                    frame->entered = true;
                }
                else if (first == BUSTER_CHECK_NONE)
                {
                    frame->then_ip = false;
                    frame->alternate = second;
                    frame->step = CHECK_REACH_IF_ELSE;
                    if (depth >= BUSTER_CHECK_REACH_DEPTH)
                    {
                        overflow = true;
                    }
                    else
                    {
                        stack[depth++] = (CheckReachFrame){ .token = second, .step = CHECK_REACH_STATEMENT, .entered = true };
                    }
                }
                else
                {
                    frame->alternate = second;
                    frame->step = CHECK_REACH_IF_THEN;
                    if (depth >= BUSTER_CHECK_REACH_DEPTH)
                    {
                        overflow = true;
                    }
                    else
                    {
                        stack[depth++] = (CheckReachFrame){ .token = first, .step = CHECK_REACH_STATEMENT, .entered = true };
                    }
                }
            }
            else
            {
                const bool label = role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE;
                frame->step = CHECK_REACH_SEQUENCE;
                frame->ip = label || frame->entered;
                frame->after = label ? frame->token : writer->opening;
                frame->skip_final = label;
                frame->child = writer->first[frame->token];
            }
        }
        else
        {
            bool pushed = false;
            while (frame->child != BUSTER_CHECK_NONE && !pushed && !overflow)
            {
                const uint32_t child = frame->child;
                const bool kept = check_control_sequence_child(writer, child) && child > frame->after &&
                                  !(frame->skip_final && child == writer->region_final);
                if (kept && (frame->ip || check_control_enterable(writer, child)))
                {
                    if (depth >= BUSTER_CHECK_REACH_DEPTH)
                    {
                        overflow = true;
                    }
                    else
                    {
                        stack[depth++] = (CheckReachFrame){ .token = child, .step = CHECK_REACH_STATEMENT, .entered = frame->ip };
                        pushed = true;
                    }
                }
                else
                {
                    frame->child = writer->next[child];
                }
            }
            if (!pushed && !overflow)
            {
                result = frame->ip;
                have = true;
                depth -= 1;
            }
        }
    }
    if (overflow)
    {
        result = true;
    }
    return result;
}

// An automatic local (`check_lower_local_declaration`'s test).
static bool check_control_reach_local(const CheckControlWriter* writer, uint32_t token)
{
    const CheckInput* input = writer->program->input;
    const bool result = token < input->token_count && input->declarations != NULL && input->declarations[token] == token &&
                        check_sparse_get(input->declaration_bodies, token) < input->token_count &&
                        input->declaration_kinds[token] == C_BINDING_OBJECT &&
                        !check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC);
    return result;
}

// A block-scope static. Clang emits its declaration even with no insertion point (EmitDeclStmt is a simple statement),
// so the object and its initializer stay. A static in a constant if's dead arm is a different drop.
static bool check_control_reach_static(const CheckControlWriter* writer, uint32_t token)
{
    const CheckInput* input = writer->program->input;
    const bool result = token < input->token_count && input->declarations != NULL && input->declarations[token] == token &&
                        check_sparse_get(input->declaration_bodies, token) < input->token_count &&
                        input->declaration_kinds[token] == C_BINDING_OBJECT &&
                        check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC);
    return result;
}

// Whether `used` (declaration tokens an emitted statement names) holds `token`.
static bool check_control_reach_used(const uint32_t* used, uint32_t used_count, uint32_t token)
{
    bool found = false;
    for (uint32_t i = 0; !found && i < used_count; i += 1)
    {
        found = used[i] == token;
    }
    return found;
}

// Drops `low` through `high`. An automatic local in `used`, and every block-scope static, stays out of the range.
// The statement is not emitted. An automatic local's initializer does not run; a static's initializer is its object's.
static void check_control_drop_unreachable(CheckControlWriter* writer, uint32_t low, uint32_t high, const uint32_t* used, uint32_t used_count)
{
    uint32_t cursor = low;
    const uint32_t limit = writer->parser->tokens.count;
    for (uint32_t token = low; token <= high; token += 1)
    {
        const bool keep = check_control_reach_static(writer, token) ||
                          (check_control_reach_local(writer, token) && check_control_reach_used(used, used_count, token));
        if (keep)
        {
            if (cursor < token && writer->dropped_range_count < limit)
            {
                check_control_drop(writer, cursor, token - 1);
            }
            cursor = token + 1;
        }
    }
    if (cursor <= high && writer->dropped_range_count < limit)
    {
        check_control_drop(writer, cursor, high);
    }
}

// Declaration tokens of automatic locals that `child` names, appended to `used`.
static void check_control_reach_uses(const CheckControlWriter* writer, uint32_t child, uint32_t* used, uint32_t* used_count, uint32_t room)
{
    uint32_t low = 0;
    uint32_t high = 0;
    check_control_extent(writer, child, &low, &high);
    const CheckInput* input = writer->program->input;
    for (uint32_t token = low; token <= high && *used_count < room; token += 1)
    {
        const uint32_t declaration = token < input->token_count && input->declarations != NULL ? input->declarations[token] : BUSTER_CHECK_NONE;
        const bool named = declaration != token && declaration != BUSTER_CHECK_NONE && check_control_reach_local(writer, declaration);
        bool seen = false;
        for (uint32_t i = 0; named && !seen && i < *used_count; i += 1)
        {
            seen = used[i] == declaration;
        }
        if (named && !seen)
        {
            used[(*used_count)++] = declaration;
        }
    }
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
    uint32_t* dropped = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, room);
    bool ip = entered;
    uint32_t count = 0;
    uint32_t drop_count = 0;
    if (list == NULL || at == NULL || dropped == NULL)
    {
        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, parent);
    }
    for (uint32_t child = list != NULL && at != NULL && dropped != NULL ? writer->first[parent] : BUSTER_CHECK_NONE; child != BUSTER_CHECK_NONE; child = writer->next[child])
    {
        const bool kept = check_control_sequence_child(writer, child) && child > after && !(skip_final && child == writer->region_final);
        if (kept)
        {
            if (!ip && !check_control_enterable(writer, child))
            {
                if (drop_count < room)
                {
                    dropped[drop_count] = child;
                    drop_count += 1;
                }
            }
            else if (count < room)
            {
                list[count] = child;
                at[count] = ip ? 1 : 0;
                count += 1;
                ip = check_control_completes(writer, child, ip, stop, repeat);
            }
        }
    }
    // A local the dropped statement declares keeps a place when an emitted statement names it (a declaration before
    // the first case, or between a goto and its label). One nothing emitted names stays dropped, cleanup included.
    // A block-scope static stays either way: clang emits its declaration with no insertion point.
    uint32_t* used = drop_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, room) : NULL;
    uint32_t used_count = 0;
    if (drop_count != 0 && used == NULL)
    {
        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, parent);
    }
    for (uint32_t i = 0; used != NULL && i < count; i += 1)
    {
        check_control_reach_uses(writer, list[i], used, &used_count, room);
    }
    for (uint32_t i = 0; used != NULL && i < drop_count; i += 1)
    {
        uint32_t low = 0;
        uint32_t high = 0;
        check_control_extent(writer, dropped[i], &low, &high);
        if (low <= high)
        {
            check_control_drop_unreachable(writer, low, high, used, used_count);
        }
    }
    for (uint32_t i = count; i-- > 0;)
    {
        check_control_statement_at(writer, list[i], stop, repeat, at[i] != 0);
    }
    arena_pop_to(scratch, mark);
}
