// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Semantic unevaluation for rare null-conversion candidates. One lazy source scan retains only change points of
// the innermost query interval. Point queries then use a lower bound; no token-sized suppression column is added.
// A lane retains this immutable, type-independent index in its results arena across repeated bodies.

static uint32_t check_constant_punctuation(const Parser* parser, uint32_t first, uint32_t end, uint8_t punctuation);

// A tree-only parse omits derived ends, and a fixed-bound query can have no retained graph node. Its operand
// still occupies a contiguous parser subtree. Inspect that exceptional subtree's parent paths without retaining
// a token-sized end column; the normal reader and graph paths supply their already proven exclusive end.
static uint32_t check_warning_query_end(const Parser* parser, uint32_t query)
{
    uint32_t end = query + 1;
    while (end < parser->tokens.count)
    {
        uint32_t parent = end;
        for (uint32_t steps = 0; parent < parser->tokens.count && parent != query && steps < parser->tokens.count; steps += 1)
        {
            const uint32_t next = parser->tree.parents[parent];
            if (next == parent)
            {
                break;
            }
            parent = next;
        }
        if (parent != query)
        {
            break;
        }
        end += 1;
    }
    return end;
}

static void check_warning_query_point(CheckWarningQueryPoint* points, uint32_t* count, uint32_t token, uint32_t query)
{
    if (*count != 0 && points[*count - 1].token == token)
    {
        points[*count - 1].query = query;
    }
    else
    {
        points[(*count)++] = (CheckWarningQueryPoint){ .token = token, .query = query };
    }
}

static bool check_input_warning_queries(CheckInputReader* reader)
{
    bool valid = true;
    if (!reader->warning_queries.prepared)
    {
        const Parser* parser = reader->parser;
        const uint32_t count = parser->tokens.count;
        uint32_t room = 0;
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint8_t kind = parser->tokens.kinds[token];
            room += kind == PARSE_KIND_SIZEOF || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_GENERIC;
        }
        Arena* output = reader->warning_query_arena;
        const uint64_t output_mark = output->length;
        const uint64_t mark = reader->scratch->length;
        CheckWarningQueryPoint* points = room != 0 && room < UINT32_MAX / 2 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckWarningQueryPoint, (uint64_t)room * 2) : NULL;
        CheckWarningQueryFrame* stack = points != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckWarningQueryFrame, room) : NULL;
        valid = room == 0 || stack != NULL;
        uint32_t written = 0;
        uint32_t depth = 0;
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            const uint8_t kind = parser->tokens.kinds[token];
            const uint32_t name = parser->tokens.names[token];
            uint32_t first = count;
            uint32_t end = count;
            if (kind == PARSE_KIND_SIZEOF)
            {
                first = token + 1;
                const uint32_t node = reader->output->token_nodes != NULL ? reader->output->token_nodes[token] : BUSTER_CHECK_NONE;
                end = reader->ends != NULL ? reader->ends[token] + 1 : node < reader->output->graph.count ? reader->output->graph.source_positions[node] :
                      check_warning_query_end(parser, token);
            }
            else if (kind == PARSE_KIND_TYPEOF && token + 1 < count &&
                     (name == C_NAME_TYPEOF || name == C_NAME_GNU_TYPEOF || name == C_NAME_GNU_TYPEOF2 ||
                      name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2))
            {
                first = token + 2;
                end = parser->tree.parents[token + 1];
            }
            else if (kind == PARSE_KIND_GENERIC && name == C_NAME_GENERIC && token + 1 < count)
            {
                first = token + 2;
                const uint32_t close = parser->tree.parents[token + 1];
                end = close < count ? check_constant_punctuation(parser, first, close, PARSE_KIND_COMMA) : first;
            }
            if (first < end && end <= count)
            {
                while (depth != 0 && stack[depth - 1].end <= first)
                {
                    const uint32_t finish = stack[--depth].end;
                    check_warning_query_point(points, &written, finish, depth != 0 ? stack[depth - 1].first : BUSTER_CHECK_NONE);
                }
                valid = depth == 0 || end <= stack[depth - 1].end;
                if (valid)
                {
                    stack[depth++] = (CheckWarningQueryFrame){ .first = first, .end = end };
                    check_warning_query_point(points, &written, first, first);
                }
            }
        }
        while (valid && depth != 0)
        {
            const uint32_t finish = stack[--depth].end;
            check_warning_query_point(points, &written, finish, depth != 0 ? stack[depth - 1].first : BUSTER_CHECK_NONE);
        }
        if (valid)
        {
            reader->warning_queries = (CheckWarningQueries){ .points = points, .count = written, .prepared = true };
        }
        else
        {
            arena_pop_to(output, output_mark);
            reader->output->resource = true;
        }
        arena_pop_to(reader->scratch, mark);
    }
    return valid;
}

static bool check_input_warning_unevaluated(CheckInputReader* reader, uint32_t token)
{
    const bool ready = check_input_warning_queries(reader);
    const CheckWarningQueries* queries = &reader->warning_queries;
    uint32_t low = 0;
    uint32_t high = ready ? queries->count : 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (queries->points[middle].token <= token)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const uint32_t query = low != 0 ? queries->points[low - 1].query : BUSTER_CHECK_NONE;
    const bool result = query != BUSTER_CHECK_NONE && (reader->warning_bound_open == BUSTER_CHECK_NONE || query > reader->warning_bound_open);
    return result;
}
