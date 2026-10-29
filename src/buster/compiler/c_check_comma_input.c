// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Comma contexts for the source reader. Non-separators have a context fixed by their role; only separators inherit
// a parent's byte. Reverse source blocks write contiguous bytes and visit only separators and for terminators.
// A separator whose separator parent is earlier in source order falls back to the unchanged reverse-postorder
// oracle. This also covers prefix/repair traversals without retaining another column or allocation. Input columns
// have count logical elements; output is disjoint reader scratch. No sentinel or vector padding is accessed.
// count is parser->tokens.count, exactly check_input_run's original loop bound. A diagnostic view's postorder is a
// permutation of its own complete token view, including repair closers; original_end limits publication, not this pass.

static bool check_input_expression_comma_seed(const Parser* parser, uint32_t token)
{
    const uint8_t role = parser->tree.roles[token];
    const uint32_t parent = parser->tree.parents[token];
    return role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_SIZEOF || role == PARSE_ROLE_CLOSE_CONDITION ||
           role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_CLOSE_TERNARY || role == PARSE_ROLE_CLOSE_FOR ||
           (role == PARSE_ROLE_TERMINATOR && parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_FOR);
}

// The original pass, kept as an independent scalar oracle and the dependency-order fallback.
static void check_input_expression_commas_scalar(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    for (uint32_t k = count; k-- > 0;)
    {
        const uint32_t token = reader->postorder[k];
        const uint32_t parent = parser->tree.parents[token];
        const uint8_t role = parser->tree.roles[token];
        reader->expression_commas[token] = role == PARSE_ROLE_SEPARATOR && parent < count ? reader->expression_commas[parent] :
                                          role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_SIZEOF || role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_CLOSE_TERNARY ||
                                          role == PARSE_ROLE_CLOSE_FOR || (role == PARSE_ROLE_TERMINATOR && parent < count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_FOR);
    }
}

// False asks the caller to replay the scalar oracle: earlier separator parents require its dependency order.
static bool check_input_expression_commas_vector(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    bool complete = true;
    for (uint32_t end = count; complete && end != 0;)
    {
        const uint32_t first = end > 64 ? end - 64 : 0;
        const uint32_t length = end - first;
        const __mmask64 active = length == 64 ? UINT64_MAX : (UINT64_C(1) << length) - 1;
        const __m512i roles = _mm512_maskz_loadu_epi8(active, parser->tree.roles + first);
        const __mmask64 seeds = _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_GROUP)) |
                                _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_SIZEOF)) |
                                _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_CONDITION)) |
                                _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_SUBSCRIPT)) |
                                _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_TERNARY)) |
                                _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_FOR));
        _mm512_mask_storeu_epi8(reader->expression_commas + first, active, _mm512_maskz_set1_epi8(seeds, 1));
        uint64_t terminators = active & _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_TERMINATOR));
        for (; terminators != 0; terminators &= terminators - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(terminators);
            const uint32_t parent = parser->tree.parents[token];
            reader->expression_commas[token] = parent < count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_FOR;
        }
        uint64_t separators = active & _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_SEPARATOR));
        while (complete && separators != 0)
        {
            const uint32_t lane = 63u - (uint32_t)__builtin_clzll(separators);
            const uint32_t token = first + lane;
            const uint32_t parent = parser->tree.parents[token];
            separators ^= UINT64_C(1) << lane;
            if (parent < count)
            {
                if (parent > token)
                {
                    reader->expression_commas[token] = reader->expression_commas[parent];
                }
                else if (parser->tree.roles[parent] == PARSE_ROLE_SEPARATOR)
                {
                    complete = false;
                }
                else
                {
                    reader->expression_commas[token] = check_input_expression_comma_seed(parser, parent);
                }
            }
        }
        end = first;
    }
    return complete;
}

static void check_input_expression_commas(CheckInputReader* reader)
{
    if (BUSTER_SCALAR_PATH(reader->parser->options.scalar) || !check_input_expression_commas_vector(reader))
    {
        check_input_expression_commas_scalar(reader);
    }
}
