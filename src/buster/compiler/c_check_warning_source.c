// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source spelling for shared conversion facts. Context and publication belong to the caller; this constructor
// allocates nothing and can be used while initializer parts must remain contiguous in their output arena.

static bool check_warning_null_event(const Parser* parser, const TypeTable* types, TypeId destination, const CheckValue* value,
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
