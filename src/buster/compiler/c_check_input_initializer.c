// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Initializer syntax stays separate from expression actions. Resolve designator ICEs after the declaration's
// activation point, retain their value for the final initializer planner, and finish an outer array at the end of
// its initializer. Structural preparation emits no future binding events; the source pass executes each bound
// job at its actual opener. The token scan skips expression groups and uses no recursive descent.

static bool check_input_initializer_prepare(CheckInputReader* reader, uint32_t assignment)
{
    const Parser* parser = reader->parser;
    const uint32_t declaration = reader->initializer_declarations[assignment];
    const uint32_t begin = reader->output->initializer_begins[declaration];
    const uint32_t end = reader->output->initializer_ends[declaration];
    TypeId type = reader->declared_types[declaration];
    bool valid = begin < end && end <= parser->tokens.count;
    bool item = true;
    for (uint32_t token = begin; valid && token < end;)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const uint8_t role = parser->tree.roles[token];
        if (role == PARSE_ROLE_OPEN_INITIALIZER || role == PARSE_ROLE_CLOSE_INITIALIZER ||
            (role == PARSE_ROLE_OPEN_COMPOUND_BODY && token == begin) || (role == PARSE_ROLE_CLOSE_COMPOUND_BODY && token + 1 == end) || kind == PARSE_KIND_COMMA)
        {
            item = true;
            token += 1;
        }
        else if (item && kind == PARSE_KIND_PERIOD)
        {
            valid = token + 1 < end && parser->tokens.kinds[token + 1] == PARSE_KIND_IDENTIFIER;
            reader->ignored[token] = true;
            if (valid)
            {
                reader->ignored[token + 1] = true;
                token += 2;
            }
        }
        else if (item && kind == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = parser->tree.parents[token];
            uint32_t range = close;
            for (uint32_t at = token + 1; at < close && close < end; at += 1)
            {
                if (parser->tokens.kinds[at] == PARSE_KIND_ELLIPSIS)
                {
                    range = at;
                    break;
                }
                if (parser->tokens.kinds[at] == PARSE_KIND_L_PAREN || parser->tokens.kinds[at] == PARSE_KIND_L_SQUARE)
                {
                    at = parser->tree.parents[at];
                }
            }
            valid = close > token && close < end;
            if (valid)
            {
                reader->designator_indices[token] = close;
                reader->designator_valid[token] = 2;
                check_input_visit(reader, token, BUSTER_CHECK_RESOLVE_QUERY);
                reader->designator_ranges[token] = range;
                memset(reader->ignored + token, 1, close - token + 1);
                token = close + 1;
            }
        }
        else if (item && kind == PARSE_KIND_EQUAL)
        {
            reader->initializers[token] = 2;
            item = false;
            token += 1;
        }
        else
        {
            item = false;
            if (kind == PARSE_KIND_STRING)
            {
                valid = check_input_string(reader, token) != 0;
            }
            if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
            {
                const uint32_t close = parser->tree.parents[token];
                valid = close > token && close < end;
                token = valid ? close + 1 : end;
            }
            else
            {
                token += 1;
            }
        }
    }
    const bool incomplete = type_kind(reader->types, type) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
    if (valid && (incomplete || check_const_eligible(reader->types, type)))
    {
        reader->incomplete_array_types[declaration] = incomplete ? type : 0;
        reader->initializer_finish_next[declaration] = reader->initializer_finish_heads[end];
        reader->initializer_finish_heads[end] = declaration;
        check_input_visit(reader, end, BUSTER_CHECK_RESOLVE_FINISH);
        reader->inference_end = end > reader->inference_end ? end : reader->inference_end;
    }
    if (!valid)
    {
        check_input_report(reader, assignment, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static bool check_input_initializer_designator(CheckInputReader* reader, uint32_t token)
{
    const uint32_t close = (uint32_t)reader->designator_indices[token];
    const uint32_t range = reader->designator_ranges[token];
    int64_t index = 0;
    int64_t high = 0;
    bool valid = check_input_constant(reader, token + 1, range, &index) && index >= 0;
    valid = valid && (range == close || (check_input_constant(reader, range + 1, close, &high) && high >= index));
    if (valid)
    {
        reader->designator_indices[token] = (uint64_t)index;
        reader->designator_valid[token] = 1;
        reader->designator_tokens[range] = token + 1;
        if (range != close)
        {
            reader->designator_indices[range] = (uint64_t)high;
            reader->designator_valid[range] = 1;
            reader->designator_tokens[close] = range + 1;
        }
    }
    else
    {
        check_input_report(reader, token + 1, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

#include "c_check_input_const_values.c"

static bool check_input_initializer_finish(CheckInputReader* reader, uint32_t declaration)
{
    const Parser* parser = reader->parser;
    const uint32_t begin = reader->output->initializer_begins[declaration];
    const uint32_t end = reader->output->initializer_ends[declaration];
    const uint32_t assignment = reader->output->compound_objects[declaration] ? declaration : begin - 1;
    TypeId type = reader->declared_types[declaration];
    bool valid = begin < end && end <= parser->tokens.count;
    if (valid && type_kind(reader->types, type) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
    {
        uint32_t string_token = begin;
        string_token += parser->tree.roles[begin] == PARSE_ROLE_OPEN_INITIALIZER || parser->tree.roles[begin] == PARSE_ROLE_OPEN_COMPOUND_BODY;
        const TypeId element = type_unqualified(type_strip_aligned(reader->types, type_element(reader->types, type)));
        const CheckInitString* string = string_token < end && parser->tokens.kinds[string_token] == PARSE_KIND_STRING ? check_input_string(reader, string_token) : 0;
        const TypeId string_element = string != 0 ? type_element(reader->types, string->type) : 0;
        const bool character = element == BUSTER_TYPE_CHAR || element == BUSTER_TYPE_SCHAR || element == BUSTER_TYPE_UCHAR;
        string = string != 0 && (element == string_element || (character && string_element == BUSTER_TYPE_CHAR)) ? string : 0;
        if (string != 0)
        {
            type = check_input_type_result(reader, type_array(reader->types, type_element(reader->types, type), reader->types->types.extent[type_index(string->type)]));
            valid = type != 0;
        }
        else
        {
            uint32_t failed = begin;
            CheckInitStatus status = CHECK_INIT_OK;
            // The casts in the initializer have their types already, at their `)`s: it finishes at its end.
            valid = check_initializer_infer_array(reader->types, parser, reader->pp->table, type, begin, end, reader->designator_indices,
                                                  reader->designator_valid, reader->declared_types, reader->query_types, reader->scratch, &type,
                                                  &failed, &status);
            reader->output->resource |= status == CHECK_INIT_RESOURCE;
            if (!valid)
            {
                check_input_report(reader, failed, CHECK_INPUT_DECLARATION);
            }
        }
        if (valid)
        {
            reader->declared_types[declaration] = type;
            reader->query_types[assignment] = type;
            const uint32_t name = parser->tokens.names[declaration];
            if (reader->linkage[declaration] != 0)
            {
                reader->linked_types[name] = type;
            }
        }
    }
    if (valid && check_const_eligible(reader->types, type))
    {
        check_input_initializer_const(reader, declaration, begin, end, type);
    }
    if (!valid && reader->output->report_count == 0)
    {
        check_input_report(reader, assignment, CHECK_INPUT_DECLARATION);
    }
    return valid;
}
