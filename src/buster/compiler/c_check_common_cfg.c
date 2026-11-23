// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// -Wtautological-bitwise-compare, -Wtautological-overlap-compare and -Wtautological-negation-compare (#2990): what clang's
// CFG builder's observer (AnalysisBasedWarnings' LogicalErrorHandler) hears as it builds a function's CFG, in the order
// it builds it. Its rules are check_cfg_try's (c_check_program_cfg.c); this walk calls it where CFGBuilder calls
// tryEvaluateBool, with one cache a unit, as clang's is one a build: an equality's and a logical operator's are decided
// once, a `|`'s each time (so `x | 1 || y` reports twice). Include within c_check_common_warnings.c.
//
// The builder visits a body's statements last first, each expression from its root down, an operator's right operand
// before its left (but an assignment's left first), an equality's and a relational comparison's own tryEvaluateBool
// before its operands, a `!`'s operand's before it visits it, and a logical operator's as VisitLogicalOperator does: its
// right operand's (and then its own, where that one's is unknown) before it visits the right operand, then the left
// operand's after. A condition is an if's and a conditional's before it is visited and a loop's after. The walk takes
// the unit's expression roots by their nodes, last first, which is that order but for a for statement's increment,
// which the builder takes before the body, and a statement expression's statements, which it takes where the
// expression stands. The warnings are published at the body's end before its queued runtime warnings
// (PP_DIAGNOSTIC_CFG): clang builds the CFG before it gives them. A body clang ends after the unit's first error
// (`flushed`) has no CFG, and so none.

typedef enum CheckCommonCfgAction
{
    CHECK_COMMON_CFG_VISIT,
    CHECK_COMMON_CFG_TRY,
    CHECK_COMMON_CFG_TRY_BARE,
    CHECK_COMMON_CFG_TRY_RIGHT,
    CHECK_COMMON_CFG_LOGICAL,
} CheckCommonCfgAction;

#define BUSTER_CHECK_COMMON_CFG_ACTIONS 512u

// Whether any token of the expression at `node` comes from a macro (clang's HasMacroID): its nodes' tokens, from the
// first to the last, and the parentheses and casts that open before the first.
static bool check_common_cfg_from_macro(const Parser* parser, const Preprocessor* pp, const CheckGraph* graph, uint32_t node)
{
    const uint32_t count = parser->tokens.count;
    uint32_t stack[64];
    uint32_t top = 1;
    stack[0] = node;
    uint32_t low = UINT32_MAX;
    uint32_t high = 0;
    bool complete = true;
    while (top != 0)
    {
        top -= 1;
        const uint32_t at = stack[top];
        const uint32_t token = graph->tokens[at];
        low = token < low ? token : low;
        high = token < count && token > high ? token : high;
        const uint32_t operands[3] = { graph->operand0[at], graph->operand1[at], graph->operand2[at] };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            const bool valid = operands[i] < graph->count;
            complete &= !valid || top < BUSTER_ARRAY_LENGTH(stack);
            if (valid && top < BUSTER_ARRAY_LENGTH(stack))
            {
                stack[top] = operands[i];
                top += 1;
            }
        }
    }
    while (low < count && low > 0 && (parser->tree.roles[low - 1] == PARSE_ROLE_OPEN_GROUP || parser->tree.roles[low - 1] == PARSE_ROLE_OPEN_CAST) &&
           parser->tree.parents[low - 1] > low - 1 && parser->tree.parents[low - 1] <= high + 1)
    {
        low -= 1;
    }
    // (An expression too deep to walk is taken as a macro's: its warning is dropped, not given wrongly.)
    bool result = !complete;
    for (uint32_t token = low; !result && token <= high && token < count; token += 1)
    {
        result = check_common_from_macro(parser, pp, token);
    }
    return result;
}

// The observer's events since the last flush, published in order at their bodies' ends.
static void check_common_cfg_flush(const Parser* parser, Preprocessor* pp, const CheckInput* input, CheckCfgObserver* observer, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    for (uint32_t i = 0; i < observer->count && !pp->fatal; i += 1)
    {
        const CheckCfgEvent* event = observer->events + i;
        const uint32_t token = graph->tokens[event->node];
        const uint32_t id = event->kind == CHECK_CFG_EVENT_OVERLAP ? CHECK_WARNING_ID_TAUTOLOGICAL_OVERLAP_COMPARE :
                            event->kind == CHECK_CFG_EVENT_NEGATION ? CHECK_WARNING_ID_TAUTOLOGICAL_NEGATION_COMPARE : CHECK_WARNING_ID_TAUTOLOGICAL_BITWISE_COMPARE;
        const char* message = event->kind == CHECK_CFG_EVENT_BITWISE_OR ? "bitwise or with non-zero value always evaluates to true" :
                              event->kind == CHECK_CFG_EVENT_BITWISE_EQUALITY ?
                                  (event->truth ? "bitwise comparison always evaluates to true" : "bitwise comparison always evaluates to false") :
                              event->kind == CHECK_CFG_EVENT_OVERLAP ?
                                  (event->truth ? "overlapping comparisons always evaluate to true" : "non-overlapping comparisons always evaluate to false") :
                              event->truth ? "'||' of a value and its negation always evaluates to true" :
                                             "'&&' of a value and its negation always evaluates to false";
        if (token < parser->tokens.count && check_common_severity(pp, parser, input, token, id, true) != CHECK_WARNING_IGNORE &&
            !check_common_cfg_from_macro(parser, pp, graph, event->node))
        {
            // The operator's expression highlighted (clang's B->getSourceRange()), its own parentheses left out.
            const uint32_t publication = check_common_publication_source(parser, pp, check_common_function_boundary(parser, token));
            check_semantic_range(parser, pp, token, false);
            check_common_report_published_phase(parser, pp, input, token, publication, PP_DIAGNOSTIC_CFG, id, true, message, NULL, 0, scratch);
            pp_diagnostic_ranges_clear(pp);
        }
    }
    observer->count = 0;
}

// What kind of statement condition CONDITION node `node` is: 0 for a switch's (no tryEvaluateBool), 1 for an if's (before
// its visit), 2 for a while's or a for's (after it), 3 for a do's (after it, and a logical operator's as written too).
static uint32_t check_common_cfg_condition_kind(const Parser* parser, const CheckProgram* program, uint32_t node)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t token = program->input->graph.tokens[node];
    const uint8_t role = token < count ? parser->tree.roles[token] : PARSE_ROLE_NONE;
    const uint32_t opener = role == PARSE_ROLE_CLOSE_CONDITION ? check_common_opener(parser, token) : BUSTER_CHECK_NONE;
    const uint8_t keyword = opener != BUSTER_CHECK_NONE && opener > 0 ? parser->tokens.kinds[opener - 1] : 0;
    return keyword == PARSE_KIND_SWITCH ? 0u : keyword == PARSE_KIND_IF ? 1u : check_cfg_do_condition(program, node) ? 3u : 2u;
}

// The builder's visit of expression root `root`, its tryEvaluateBool calls observed in order (the file's comment). An
// explicit stack of actions, each a node and what to do with it; past BUSTER_CHECK_COMMON_CFG_ACTIONS the rest of the
// root is left unvisited.
static void check_common_cfg_visit(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckProgram* program, CheckCfgObserver* observer,
                                   uint32_t root, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    uint32_t nodes[BUSTER_CHECK_COMMON_CFG_ACTIONS];
    uint8_t actions[BUSTER_CHECK_COMMON_CFG_ACTIONS];
    uint32_t top = 1;
    nodes[0] = root;
    actions[0] = CHECK_COMMON_CFG_VISIT;
    while (top != 0 && !pp->fatal)
    {
        top -= 1;
        const uint32_t node = nodes[top];
        const uint8_t action = actions[top];
        // The actions this one makes, in the order they run (pushed last first).
        uint32_t next_nodes[8];
        uint8_t next_actions[8];
        uint32_t next = 0;
        const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
        if (action == CHECK_COMMON_CFG_TRY || action == CHECK_COMMON_CFG_TRY_BARE)
        {
            (void)check_cfg_try(program, node, node, action == CHECK_COMMON_CFG_TRY_BARE || !check_cfg_parenthesized(program, node), observer);
            check_common_cfg_flush(parser, pp, input, observer, scratch);
        }
        else if (action == CHECK_COMMON_CFG_TRY_RIGHT)
        {
            // A logical operator's right operand's, then its own where that one's is unknown.
            const uint8_t value = check_cfg_try(program, node, graph->operand1[node], true, observer);
            if (value == BUSTER_CHECK_CFG_UNKNOWN)
            {
                (void)check_cfg_try(program, node, node, true, observer);
            }
            check_common_cfg_flush(parser, pp, input, observer, scratch);
        }
        else if (action == CHECK_COMMON_CFG_LOGICAL)
        {
            const uint32_t right = graph->operand1[node];
            const uint32_t left = graph->operand0[node];
            if (check_cfg_logical(program, right))
            {
                next_nodes[next] = right;
                next_actions[next] = CHECK_COMMON_CFG_LOGICAL;
                next += 1;
            }
            else
            {
                next_nodes[next] = node;
                next_actions[next] = CHECK_COMMON_CFG_TRY_RIGHT;
                next_nodes[next + 1] = right;
                next_actions[next + 1] = CHECK_COMMON_CFG_VISIT;
                next += 2;
            }
            if (check_cfg_logical(program, left))
            {
                next_nodes[next] = left;
                next_actions[next] = CHECK_COMMON_CFG_LOGICAL;
                next += 1;
            }
            else
            {
                next_nodes[next] = left;
                next_actions[next] = CHECK_COMMON_CFG_VISIT;
                next_nodes[next + 1] = left;
                next_actions[next + 1] = CHECK_COMMON_CFG_TRY_BARE;
                next += 2;
            }
        }
        else if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR)
        {
            next_nodes[next] = node;
            next_actions[next] = CHECK_COMMON_CFG_LOGICAL;
            next += 1;
        }
        else if (op == CHECK_OP_CONDITIONAL && graph->operand1[node] == graph->operand0[node])
        {
            // GNU's `a ?: b`: its false arm, then its common operand, whose OpaqueValueExpr's tryEvaluateBool hears nothing.
            next_nodes[next] = graph->operand2[node];
            next_actions[next] = CHECK_COMMON_CFG_VISIT;
            next_nodes[next + 1] = graph->operand0[node];
            next_actions[next + 1] = CHECK_COMMON_CFG_VISIT;
            next += 2;
        }
        else if (op == CHECK_OP_CONDITION || (op == CHECK_OP_CONDITIONAL && graph->operand1[node] < graph->count))
        {
            // A conditional's arms, its false one first; then a condition, as its statement takes it.
            const uint32_t condition = graph->operand0[node];
            const uint32_t kind = op == CHECK_OP_CONDITION ? check_common_cfg_condition_kind(parser, program, node) : 1u;
            if (op == CHECK_OP_CONDITIONAL)
            {
                next_nodes[next] = graph->operand2[node];
                next_actions[next] = CHECK_COMMON_CFG_VISIT;
                next_nodes[next + 1] = graph->operand1[node];
                next_actions[next + 1] = CHECK_COMMON_CFG_VISIT;
                next += 2;
            }
            const bool logical = kind != 0 && kind != 3 && check_cfg_logical(program, condition);
            next_nodes[next] = condition;
            next_actions[next] = logical ? CHECK_COMMON_CFG_LOGICAL : kind == 1 ? CHECK_COMMON_CFG_TRY : CHECK_COMMON_CFG_VISIT;
            next_nodes[next + 1] = condition;
            next_actions[next + 1] = kind == 1 ? CHECK_COMMON_CFG_VISIT : CHECK_COMMON_CFG_TRY;
            next += logical || kind == 0 ? 1 : 2;
        }
        else if (check_cfg_comparison(op))
        {
            next_nodes[next] = node;
            next_actions[next] = CHECK_COMMON_CFG_TRY_BARE;
            next_nodes[next + 1] = graph->operand1[node];
            next_actions[next + 1] = CHECK_COMMON_CFG_VISIT;
            next_nodes[next + 2] = graph->operand0[node];
            next_actions[next + 2] = CHECK_COMMON_CFG_VISIT;
            next += 3;
        }
        else if (op == CHECK_OP_NOT)
        {
            next_nodes[next] = graph->operand0[node];
            next_actions[next] = CHECK_COMMON_CFG_TRY_BARE;
            next_nodes[next + 1] = graph->operand0[node];
            next_actions[next + 1] = CHECK_COMMON_CFG_VISIT;
            next += 2;
        }
        else if (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN))
        {
            next_nodes[next] = graph->operand0[node];
            next_actions[next] = CHECK_COMMON_CFG_VISIT;
            next_nodes[next + 1] = graph->operand1[node];
            next_actions[next + 1] = CHECK_COMMON_CFG_VISIT;
            next += 2;
        }
        else if (op != CHECK_OP_SIZEOF && op != CHECK_OP_ALIGNOF && op < CHECK_OP_COUNT)
        {
            // Any other node's operands, its last first; a call's arguments, its last first, between its other operands and
            // its callee (its first operand). Pushed here directly, in reverse: the callee, the arguments, the rest.
            if (graph->operand0[node] < graph->count && top < BUSTER_CHECK_COMMON_CFG_ACTIONS)
            {
                nodes[top] = graph->operand0[node];
                actions[top] = CHECK_COMMON_CFG_VISIT;
                top += 1;
            }
            const bool arguments = op != CHECK_OP_VARARGS && check_graph_has_arguments(op) && graph->argument_offsets != NULL && graph->argument_counts != NULL;
            for (uint32_t i = 0; arguments && i < graph->argument_counts[node] && top < BUSTER_CHECK_COMMON_CFG_ACTIONS; i += 1)
            {
                nodes[top] = graph->argument_nodes[graph->argument_offsets[node] + i];
                actions[top] = CHECK_COMMON_CFG_VISIT;
                top += 1;
            }
            next_nodes[next] = graph->operand2[node];
            next_actions[next] = CHECK_COMMON_CFG_VISIT;
            next_nodes[next + 1] = graph->operand1[node];
            next_actions[next + 1] = CHECK_COMMON_CFG_VISIT;
            next += 2;
        }
        for (uint32_t i = next; i-- > 0;)
        {
            if (next_nodes[i] < graph->count && top < BUSTER_CHECK_COMMON_CFG_ACTIONS)
            {
                nodes[top] = next_nodes[i];
                actions[top] = next_actions[i];
                top += 1;
            }
        }
    }
}

static void check_common_cfg_tautologies(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckProgram* program, const uint8_t* flushed,
                                         const uint32_t* parents, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const bool enabled = check_common_enabled(pp, input, CHECK_WARNING_ID_TAUTOLOGICAL_BITWISE_COMPARE, true) ||
                         check_common_enabled(pp, input, CHECK_WARNING_ID_TAUTOLOGICAL_OVERLAP_COMPARE, true) ||
                         check_common_enabled(pp, input, CHECK_WARNING_ID_TAUTOLOGICAL_NEGATION_COMPARE, true);
    if (enabled && program != NULL && parents != NULL && graph->count != 0 && !pp->fatal)
    {
        const uint64_t mark = scratch->length;
        CheckCfgObserver observer = { .cache = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count) };
        if (observer.cache == NULL)
        {
            check_common_exhausted(pp);
        }
        else
        {
            memset(observer.cache, 0, graph->count);
            for (uint32_t root = graph->count; root-- > 0 && !pp->fatal;)
            {
                const uint32_t token = graph->tokens[root];
                const bool body = parents[root] == BUSTER_CHECK_NONE && token < parser->tokens.count && (flushed == NULL || flushed[token] == 0) &&
                                  check_common_function_boundary(parser, token) != token;
                if (body)
                {
                    check_common_cfg_visit(parser, pp, input, program, &observer, root, scratch);
                }
            }
        }
        arena_pop_to(scratch, mark);
    }
}
