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
        check_input_rare_claim(reader, &reader->constant_present[close]);
        reader->constant_present[close] = index + 1;
        check_input_sparse_mark(reader, close, true);
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
                check_input_rare_claim(reader, &reader->selection_type_ends[association]);
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
    // An incomplete type, void's too, is a C2y extension clang takes (-Wc2y-extensions): it matches no control.
    const bool valid = type != 0 && kind != BUSTER_TYPE_KIND_FUNCTION && (type_inherited_flags(reader->types, type) & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) == 0;
    if (valid)
    {
        check_input_rare_claim(reader, &reader->selection_types[end]);
        reader->selection_types[end] = type;
    }
    else
    {
        check_input_report(reader, first, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static bool check_input_selection_generic(CheckInputReader* reader, uint32_t separator, uint32_t close, const CheckValue* control)
{
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const TypeId control_type = check_constant_control_type(reader, control);
    CheckGenericSet seen;
    bool valid = control_type != 0 && check_generic_initialize(reader, &seen, close - separator, control_type, close + 1);
    uint32_t chosen = BUSTER_CHECK_NONE;
    uint32_t fallback = BUSTER_CHECK_NONE;
    uint32_t association = separator + 1;
    while (valid && association < close)
    {
        const uint32_t colon = check_constant_punctuation(parser, association, close, PARSE_KIND_COLON);
        const uint32_t after = colon < close ? check_constant_punctuation(parser, colon + 1, close, PARSE_KIND_COMMA) : close;
        valid = association < colon && colon < close && colon + 1 < after;
        const uint32_t root = valid ? check_input_range_root(reader, colon + 1, after) : BUSTER_CHECK_NONE;
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
            bool matches = false;
            valid = valid && check_generic_association(reader, &seen, type, &matches);
            if (valid)
            {
                if (matches)
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
        check_input_sparse_mark(reader, close, chosen < reader->parser->tokens.count);
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

// A close of the postorder that the selections' pass reads, and whether it selected: one whose first child is a _Generic
// or __builtin_choose_expr token, and which the reader kept (in the expressions' limit, not ignored, a keyword's close).
// Others select nothing and are valid.
static bool check_input_selection(CheckInputReader* reader, uint32_t close)
{
    const Parser* parser = reader->parser;
    const uint32_t keyword = reader->first_child[close];
    const uint32_t name = keyword < parser->tokens.count ? parser->tokens.names[keyword] : 0;
    bool valid = true;
    if (reader->ends[close] < check_input_expression_limit(reader, close) && !reader->ignored[close] && parser->tree.roles[close] == PARSE_ROLE_CLOSE_KEYWORD &&
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
            const uint32_t root = selected ? check_input_range_root(reader, condition ? separator + 1 : next + 1, condition ? next : close) : BUSTER_CHECK_NONE;
            selected &= root != BUSTER_CHECK_NONE;
            if (selected)
            {
                reader->selected_tokens[close] = root;
                check_input_sparse_mark(reader, close, root < reader->parser->tokens.count);
                check_input_constant_fold_warning(reader, first, separator, &control, true, first);
                check_input_warning_suppress(reader, condition ? next + 1 : separator + 1, condition ? close : next, CHECK_WARNING_INTEGER_OVERFLOW);
            }
        }
        if (!selected)
        {
            check_input_report(reader, keyword, CHECK_INPUT_DECLARATION);
        }
        valid = selected;
    }
    return valid;
}

// The postorder positions of the closes whose first child is a _Generic or __builtin_choose_expr token, as bits of
// `positions` ((count + 63) / 64 words, zeroed), and how many there are (#877). AVX-512 compares 16 tokens' names at a
// time with the two names and maps each such token to its parent, of which it must be the first child: a unit has few
// of them, where the walk visited every token. The scalar path tests every close of the postorder as the walk does: the
// kernel's twin in test-checker, since check_input_selections' own scalar path walks instead.
static uint32_t check_input_selection_positions(const uint32_t* names, const uint32_t* parents, const uint32_t* first_child, const uint32_t* postorder,
                                                const uint32_t* postorder_positions, uint32_t count, uint64_t* positions, bool scalar)
{
    (void)scalar;

    uint32_t found = 0;
    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t position = 0; position < count; position += 1)
        {
            const uint32_t keyword = first_child[postorder[position]];
            const uint32_t name = keyword < count ? names[keyword] : 0;
            if (name == C_NAME_GENERIC || name == C_NAME_BUILTIN_CHOOSE_EXPR)
            {
                positions[position / 64] |= UINT64_C(1) << (position % 64);
                found += 1;
            }
        }
    }
    else
    {
        const __m512i generic = _mm512_set1_epi32((int32_t)C_NAME_GENERIC);
        const __m512i choose = _mm512_set1_epi32((int32_t)C_NAME_BUILTIN_CHOOSE_EXPR);
        for (uint32_t first = 0; first < count; first += 16)
        {
            const __mmask16 lanes = (__mmask16)check_input_scan_all(first, count);
            const __m512i spelled = _mm512_maskz_loadu_epi32(lanes, names + first);
            for (uint32_t keywords = (uint32_t)(_mm512_mask_cmpeq_epi32_mask(lanes, spelled, generic) | _mm512_mask_cmpeq_epi32_mask(lanes, spelled, choose));
                 keywords != 0; keywords &= keywords - 1)
            {
                const uint32_t keyword = first + (uint32_t)__builtin_ctz(keywords);
                const uint32_t close = parents[keyword];
                if (close < count && first_child[close] == keyword)
                {
                    const uint32_t position = postorder_positions[close];
                    positions[position / 64] |= UINT64_C(1) << (position % 64);
                    found += 1;
                }
            }
        }
    }
    return found;
}

// Each _Generic's and __builtin_choose_expr's selection, in postorder, so that a selection's operands are selected
// before it. The vector path visits only the closes check_input_selection_positions finds, in the order of their bits,
// the order the walk would visit them in; the scalar path, its twin, walks every token. Where the vector path has no
// room for its bits, it walks too: the same visits.
static bool check_input_selections(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool scalar = BUSTER_SCALAR_PATH(parser->options.scalar);
    const uint64_t mark = reader->scratch->length;
    const uint64_t words = ((uint64_t)count + 63) / 64;
    uint64_t* positions = scalar || words == 0 ? NULL : BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint64_t, words);
    bool valid = true;
    if (positions != NULL)
    {
        memset(positions, 0, words * sizeof(uint64_t));
        check_input_selection_positions(parser->tokens.names, parser->tree.parents, reader->first_child, reader->postorder, reader->postorder_positions, count,
                                        positions, false);
        for (uint64_t word = 0; word < words; word += 1)
        {
            for (uint64_t bits = positions[word]; bits != 0; bits &= bits - 1)
            {
                valid &= check_input_selection(reader, reader->postorder[word * 64 + (uint64_t)__builtin_ctzll(bits)]);
            }
        }
    }
    else
    {
        for (uint32_t position = 0; position < count; position += 1)
        {
            valid &= check_input_selection(reader, reader->postorder[position]);
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}
