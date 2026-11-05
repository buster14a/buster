// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source spelling for shared conversion facts. Context and publication belong to the caller; this constructor
// allocates nothing and can be used while initializer parts must remain contiguous in their output arena.

// clang's Expr::getExprLoc of the expression from `first` to `end` (#147), where some handlers report an argument: a
// binary operator's (an assignment's and a comma's too) at its operator, a member access at the member's name, a postfix
// increment or decrement at its operator, a subscript at its base's, and any other (a conditional, a call, a cast, a
// prefix operator, a parenthesized expression, a primary one) at its first token. Every subtree is contiguous, so
// the first token's parent chain reaches its root. A subscript keeps its base's location along that chain; following
// each link once avoids rescanning the same prefix for every subscript in a deep a[0][0]... expression. This is the
// written syntax; check_warning_expression_location accounts for a checked subscript's implicit base conversion.
static uint32_t check_input_expression_root_location(const Parser* parser, uint32_t first, uint32_t end, uint32_t* expression)
{
    uint32_t result = first;
    uint32_t root = first;
    if (first < end && end <= parser->tokens.count)
    {
        for (uint32_t steps = 0; steps < end - first; steps += 1)
        {
            const uint8_t role = parser->tree.roles[root];
            if (role != PARSE_ROLE_CLOSE_SUBSCRIPT)
            {
                const bool binary = role == PARSE_ROLE_MULTIPLICATIVE || role == PARSE_ROLE_ADDITIVE || role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_RELATIONAL ||
                                    role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_BITWISE_AND || role == PARSE_ROLE_BITWISE_XOR || role == PARSE_ROLE_BITWISE_OR ||
                                    role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR || role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_COMMA;
                result = binary || role == PARSE_ROLE_POSTFIX ? root : role == PARSE_ROLE_MEMBER_OPERATOR && root + 1 < end ? root + 1 : first;
            }
            const uint32_t parent = parser->tree.parents[root];
            if (parent < first || parent >= end || parent == root)
            {
                break;
            }
            root = parent;
        }
    }
    if (expression != NULL)
    {
        *expression = root;
    }
    return result;
}

static uint32_t check_input_expression_location(const Parser* parser, uint32_t first, uint32_t end)
{
    return check_input_expression_root_location(parser, first, end, NULL);
}

// A checked subscript uses its pointer operand after the default lvalue/array conversion. Clang's implicit cast
// reports that operand's beginning, while a value such as p++ or get().pointer keeps its own expression location.
// Use the existing checked categories to distinguish them, including the reversed spelling 0[p]. An enclosing
// written group keeps its own location. At most one root walk, one backwards opener search and one operand walk
// visit the source span; deep subscripts never rescan their prefixes or allocate source-depth storage.
static uint32_t check_warning_expression_location(const Parser* parser, const TypeTable* types, const CheckStream* stream,
                                                  uint32_t node, uint32_t first, uint32_t end)
{
    uint32_t root = first;
    uint32_t result = check_input_expression_root_location(parser, first, end, &root);
    const CheckGraph* graph = stream->graph;
    if (graph != NULL && node < graph->count && node < stream->count && graph->ops[node] == CHECK_OP_SUBSCRIPT &&
        root < parser->tokens.count && parser->tree.roles[root] == PARSE_ROLE_CLOSE_SUBSCRIPT)
    {
        const uint32_t left = graph->operand0[node];
        const uint8_t left_kind = left < node ?
            type_kind(types, type_strip_aligned(types, type_conversion_operand(types, check_stream_type(stream, left)))) : BUSTER_TYPE_KIND_NONE;
        const uint32_t base = left_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(left_kind) ? left : graph->operand1[node];
        if (base < node)
        {
            uint32_t begin = first;
            if (base != left)
            {
                for (uint32_t open = root; open > first;)
                {
                    open -= 1;
                    if (parser->tree.roles[open] == PARSE_ROLE_OPEN_SUBSCRIPT && parser->tree.parents[open] == root)
                    {
                        begin = open + 1;
                        result = check_input_expression_location(parser, begin, root);
                        break;
                    }
                }
            }
            const uint8_t kind = type_kind(types, type_strip_aligned(types, check_stream_type(stream, base)));
            if (check_stream_category(stream, base) == CHECK_LVALUE || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ATOMIC)
            {
                result = begin;
            }
        }
    }
    return result;
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_init_value.
static inline __attribute__((always_inline)) bool check_warning_null_event(const Parser* parser, const TypeTable* types, TypeId destination, const CheckValue* value,
                                      uint32_t first, uint32_t end, uint32_t completion, CheckWarningEvent* event)
{
    bool result = check_warning_null_integer(types, destination, value) && (value->flags & BUSTER_CHECK_LITERAL_ZERO) == 0 &&
                  first < end && end <= parser->tokens.count;
    if (result)
    {
        const uint32_t original_first = first;
        const uint32_t original_end = end;
        const bool parentheses = first + 1 < end && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN && parser->tree.parents[first] == end - 1;
        while (first + 1 < end && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN && parser->tree.parents[first] == end - 1)
        {
            first += 1;
            end -= 1;
        }
        const bool literal = first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_NUMBER && parser->tokens.names[first] == 0;
        result = !literal;
        if (result)
        {
            uint32_t token = first;
            for (uint32_t steps = 0; steps < end - first; steps += 1)
            {
                const uint32_t parent = parser->tree.parents[token];
                if (parent < first || parent >= end)
                {
                    break;
                }
                token = parent;
            }
            if (parentheses || parser->tree.roles[token] == PARSE_ROLE_CLOSE_CAST || parser->tokens.names[original_first] == C_NAME_GENERIC ||
                parser->tokens.names[original_first] == C_NAME_BUILTIN_CHOOSE_EXPR)
            {
                token = original_first;
            }
            else if (parser->tree.roles[token] == PARSE_ROLE_CLOSE_TERNARY || parser->tree.roles[token] == PARSE_ROLE_OPEN_TERNARY)
            {
                token = original_first;
                for (uint32_t steps = 0; steps < original_end - original_first; steps += 1)
                {
                    const uint32_t parent = parser->tree.parents[token];
                    if (parent < original_first || parent >= original_end || parser->tree.roles[parent] == PARSE_ROLE_OPEN_TERNARY || parser->tree.roles[parent] == PARSE_ROLE_CLOSE_TERNARY)
                    {
                        break;
                    }
                    token = parent;
                }
                if (parser->tree.roles[token] == PARSE_ROLE_CLOSE_GROUP)
                {
                    token = original_first;
                }
            }
            *event = (CheckWarningEvent){
                .id = CHECK_WARNING_ID_NON_LITERAL_NULL,
                .kind = CHECK_WARNING_EVENT_NON_LITERAL_NULL,
                .token = token,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = completion,
                .payload = { .first_type = destination, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
        }
    }
    return result;
}
