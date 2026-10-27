// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Selection syntax is discovered before its association types enter the declaration reader. Each type is
// captured at its own source position. Selection runs after binding replay, using only those exact cached
// declarations; it never appends a binding event after scopes have closed. Both selected and unselected
// expression subtrees remain in the ordinary semantic graph.

static bool check_input_builtin_constant_prepare(CheckInputReader* reader, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    const uint32_t name = parser->tokens.names[keyword];
    const uint32_t open = keyword + 1;
    const uint32_t close = open < parser->tokens.count ? parser->tree.parents[open] : parser->tokens.count;
    CheckValue value = { 0 };
    const bool valid = (name == C_NAME_BUILTIN_TYPES_COMPATIBLE_P || name == C_NAME_BUILTIN_OFFSETOF) &&
                       open < close && close < parser->tokens.count && parser->tree.roles[open] == PARSE_ROLE_OPEN_KEYWORD &&
                       check_input_evaluate(reader, keyword, close + 1, &value) && check_input_integer_constant(reader, &value);
    if (valid)
    {
        const uint32_t index = reader->cached_constant_count++;
        reader->cached_constants[index] = value;
        reader->constant_present[close] = index + 1;
        for (uint32_t token = keyword; token <= close; token += 1)
        {
            reader->ignored[token] = true;
        }
    }
    else
    {
        check_input_report(reader, keyword, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static bool check_input_selection_prepare(CheckInputReader* reader, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    const uint32_t name = parser->tokens.names[keyword];
    const uint32_t open = keyword + 1;
    const uint32_t close = open < parser->tokens.count ? parser->tree.parents[open] : parser->tokens.count;
    bool valid = open < close && close < parser->tokens.count && parser->tree.roles[open] == PARSE_ROLE_OPEN_KEYWORD;
    const uint32_t separator = valid ? check_constant_punctuation(parser, open + 1, close, PARSE_KIND_COMMA) : close;
    valid &= open + 1 < separator && separator < close;
    if (valid && name == C_NAME_GENERIC)
    {
        uint32_t association = separator + 1;
        while (valid && association < close)
        {
            const uint32_t colon = check_constant_punctuation(parser, association, close, PARSE_KIND_COLON);
            const uint32_t after = colon < close ? check_constant_punctuation(parser, colon + 1, close, PARSE_KIND_COMMA) : close;
            valid = association < colon && colon < close && colon + 1 < after;
            if (valid)
            {
                const bool fallback = parser->tokens.kinds[association] == PARSE_KIND_DEFAULT;
                valid = !fallback || association + 1 == colon;
                reader->selection_type_ends[association] = fallback ? 0 : colon;
                check_input_visit(reader, association, BUSTER_CHECK_RESOLVE_QUERY);
                for (uint32_t token = association; token < colon; token += 1)
                {
                    reader->ignored[token] = true;
                }
                association = after + 1;
            }
        }
    }
    else if (valid && name == C_NAME_BUILTIN_CHOOSE_EXPR)
    {
        const uint32_t next = check_constant_punctuation(parser, separator + 1, close, PARSE_KIND_COMMA);
        valid = separator + 1 < next && next + 1 < close &&
                check_constant_punctuation(parser, next + 1, close, PARSE_KIND_COMMA) == close;
    }
    else
    {
        valid = false;
    }
    if (valid)
    {
        reader->ignored[keyword] = true;
    }
    else
    {
        check_input_report(reader, keyword, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static bool check_input_selection_type(CheckInputReader* reader, uint32_t first)
{
    const uint32_t end = reader->selection_type_ends[first];
    const TypeId type = end > first ? check_input_constant_type(reader, first, end) : 0;
    const uint8_t kind = type_kind(reader->types, type_strip_aligned(reader->types, type));
    const bool valid = type != 0 && type_is_complete(reader->types, type) && kind != BUSTER_TYPE_KIND_VOID &&
                       kind != BUSTER_TYPE_KIND_FUNCTION && (type_inherited_flags(reader->types, type) & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) == 0;
    if (valid)
    {
        reader->selection_types[end] = type;
    }
    else
    {
        check_input_report(reader, first, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static uint32_t check_input_selection_root(const Parser* parser, uint32_t first, uint32_t end)
{
    uint32_t root = BUSTER_CHECK_NONE;
    uint32_t count = 0;
    for (uint32_t token = first; token < end; token += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        if (parent < first || parent >= end)
        {
            root = token;
            count += 1;
        }
    }
    return count == 1 ? root : BUSTER_CHECK_NONE;
}

static bool check_input_selection_generic(CheckInputReader* reader, uint32_t separator, uint32_t close, const CheckValue* control)
{
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const TypeId control_type = check_constant_control_type(reader, control);
    TypeId* seen = arena_push_array(reader->scratch, close - separator, sizeof(TypeId), 4);
    reader->output->resource |= seen == 0;
    bool valid = seen != 0 && control_type != 0;
    uint32_t seen_count = 0;
    uint32_t chosen = BUSTER_CHECK_NONE;
    uint32_t fallback = BUSTER_CHECK_NONE;
    uint32_t association = separator + 1;
    while (valid && association < close)
    {
        const uint32_t colon = check_constant_punctuation(parser, association, close, PARSE_KIND_COLON);
        const uint32_t after = colon < close ? check_constant_punctuation(parser, colon + 1, close, PARSE_KIND_COMMA) : close;
        valid = association < colon && colon < close && colon + 1 < after;
        const uint32_t root = valid ? check_input_selection_root(parser, colon + 1, after) : BUSTER_CHECK_NONE;
        valid &= root != BUSTER_CHECK_NONE;
        if (valid && parser->tokens.kinds[association] == PARSE_KIND_DEFAULT)
        {
            valid = association + 1 == colon && fallback == BUSTER_CHECK_NONE;
            fallback = root;
        }
        else if (valid)
        {
            const TypeId type = reader->selection_types[colon];
            valid = type != 0;
            for (uint32_t prior = 0; valid && prior < seen_count; prior += 1)
            {
                const TypeAnswer answer = type_compatible_at(reader->types, type, seen[prior], reader->transparent_unions, reader->types->tags.count, close + 1);
                reader->output->resource |= answer == TYPE_ANSWER_FULL;
                valid = answer == TYPE_ANSWER_NO;
            }
            if (valid)
            {
                seen[seen_count++] = type;
                const TypeAnswer answer = type_compatible_at(reader->types, type, control_type, reader->transparent_unions, reader->types->tags.count, close + 1);
                reader->output->resource |= answer == TYPE_ANSWER_FULL;
                valid = answer != TYPE_ANSWER_FULL;
                if (answer == TYPE_ANSWER_YES)
                {
                    valid &= chosen == BUSTER_CHECK_NONE;
                    chosen = root;
                }
            }
        }
        association = after + 1;
    }
    chosen = chosen != BUSTER_CHECK_NONE ? chosen : fallback;
    valid &= chosen != BUSTER_CHECK_NONE;
    if (valid)
    {
        reader->selected_tokens[close] = chosen;
        association = separator + 1;
        while (association < close)
        {
            const uint32_t colon = check_constant_punctuation(parser, association, close, PARSE_KIND_COLON);
            const uint32_t after = check_constant_punctuation(parser, colon + 1, close, PARSE_KIND_COMMA);
            if (chosen < colon + 1 || chosen >= after)
            {
                check_input_warning_suppress(reader, colon + 1, after, CHECK_WARNING_INTEGER_OVERFLOW);
            }
            association = after + 1;
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

static bool check_input_selections(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    bool valid = true;
    for (uint32_t position = 0; position < parser->tokens.count; position += 1)
    {
        const uint32_t close = parser->postorder[position];
        const uint32_t keyword = reader->first_child[close];
        const uint32_t name = keyword < parser->tokens.count ? parser->tokens.names[keyword] : 0;
        if (reader->ends[close] < reader->expression_limit && !reader->ignored[close] && parser->tree.roles[close] == PARSE_ROLE_CLOSE_KEYWORD &&
            (name == C_NAME_GENERIC || name == C_NAME_BUILTIN_CHOOSE_EXPR))
        {
            const uint32_t first = keyword + 2;
            const uint32_t separator = check_constant_punctuation(parser, first, close, PARSE_KIND_COMMA);
            CheckValue control = { 0 };
            bool selected = first < separator && separator < close && check_input_evaluate_cached(reader, first, separator, &control);
            if (selected && name == C_NAME_GENERIC)
            {
                selected = check_input_selection_generic(reader, separator, close, &control);
                check_input_warning_suppress(reader, first, separator, check_constant_unevaluated_warnings());
            }
            else if (selected)
            {
                const uint32_t next = check_constant_punctuation(parser, separator + 1, close, PARSE_KIND_COMMA);
                bool condition = false;
                selected = next < close && check_input_integer_ice(reader, &control) && check_action_truth(reader->types, &control, &condition);
                const uint32_t root = selected ? check_input_selection_root(parser, condition ? separator + 1 : next + 1, condition ? next : close) : BUSTER_CHECK_NONE;
                selected &= root != BUSTER_CHECK_NONE;
                if (selected)
                {
                    reader->selected_tokens[close] = root;
                    check_input_constant_fold_warning(reader, first, separator, &control, true);
                    check_input_warning_suppress(reader, condition ? next + 1 : separator + 1, condition ? close : next, CHECK_WARNING_INTEGER_OVERFLOW);
                }
            }
            if (!selected)
            {
                check_input_report(reader, keyword, CHECK_INPUT_DECLARATION);
            }
            valid &= selected;
        }
    }
    return valid;
}
