// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Physically replace closed arithmetic subexpressions of integer literals with their checked value.
// An ordinary parent still owns its conversions, source location, native code and diagnostics.

typedef struct CheckIntegerWindowRange
{
    uint16_t first;
    uint16_t end;
    uint16_t literal;
} CheckIntegerWindowRange;

typedef struct CheckIntegerWindowSpan
{
    uint32_t value;
    uint16_t first;
    uint16_t literals;
    uint16_t nodes;
} CheckIntegerWindowSpan;

_Static_assert(BUSTER_CHECK_EXPRESSION_CHUNK <= UINT16_MAX, "a chunk-relative window position fits in 16 bits");

// A nontrivial closed literal tree starts with a literal followed by another literal, a prefix or a group.
// Keep only uninterrupted runs of admissible action classes containing such a start. The rank of its
// first literal indexes the chunk's already decoded numbers even when the run crosses a SIMD group.
static uint32_t check_integer_window_ranges(const uint32_t* classes, uint32_t acting, CheckIntegerWindowRange* ranges)
{
    uint16_t literals[BUSTER_CHECK_EXPRESSION_CHUNK / 16 + 1];
    uint16_t continuation[BUSTER_CHECK_EXPRESSION_CHUNK / 16 + 1];
    uint16_t allowed[BUSTER_CHECK_EXPRESSION_CHUNK / 16 + 1];
    uint16_t ranks[BUSTER_CHECK_EXPRESSION_CHUNK / 16 + 1];
    const uint32_t groups = (acting + 15) / 16;
    uint32_t literal = 0;
    for (uint32_t group = 0; group < groups; ++group)
    {
        const uint32_t first = 16 * group;
        const uint16_t lanes = (uint16_t)_bzhi_u32(UINT32_MAX, acting - first < 16 ? acting - first : 16);
        const __m512i fields = _mm512_maskz_loadu_epi32(lanes, classes + first);
        const __m512i codes = _mm512_and_si512(fields, _mm512_set1_epi32(BUSTER_CHECK_EXPRESSION_CODE_MASK));
#define BUSTER_INTEGER_WINDOW_CLASS(code) _mm512_mask_cmpeq_epi32_mask(lanes, codes, _mm512_set1_epi32((code) << BUSTER_CHECK_EXPRESSION_CODE_SHIFT))
        literals[group] = BUSTER_INTEGER_WINDOW_CLASS(CHECK_INPUT_CODE_LITERAL);
        continuation[group] = literals[group] | BUSTER_INTEGER_WINDOW_CLASS(CHECK_INPUT_CODE_PREFIX) | BUSTER_INTEGER_WINDOW_CLASS(CHECK_INPUT_CODE_GROUP);
        allowed[group] = continuation[group] | BUSTER_INTEGER_WINDOW_CLASS(CHECK_INPUT_CODE_BINARY);
#undef BUSTER_INTEGER_WINDOW_CLASS
        ranks[group] = (uint16_t)literal;
        literal += (uint32_t)__builtin_popcount(literals[group]);
    }
    continuation[groups] = 0;
    uint32_t count = 0;
    uint32_t end = 0;
    for (uint32_t group = 0; group < groups; ++group)
    {
        const uint32_t following = continuation[group] | (uint32_t)continuation[group + 1] << 16;
        uint32_t starts = literals[group] & (following >> 1);
        while (starts != 0)
        {
            const uint32_t bit = (uint32_t)__builtin_ctz(starts);
            const uint32_t first = 16 * group + bit;
            if (first >= end)
            {
                uint32_t stop = group;
                uint32_t outside = (uint16_t)(~allowed[stop] & (UINT32_MAX << bit));
                while (outside == 0 && ++stop < groups)
                {
                    outside = (uint16_t)~allowed[stop];
                }
                end = outside != 0 ? 16 * stop + (uint32_t)__builtin_ctz(outside) : acting;
                const uint32_t prior = (uint32_t)__builtin_popcount(literals[group] & ((1u << bit) - 1));
                ranges[count++] = (CheckIntegerWindowRange){ (uint16_t)first, (uint16_t)end, (uint16_t)(ranks[group] + prior) };
            }
            if (end >= 16 * group + 16)
            {
                break;
            }
            starts &= UINT32_MAX << (end - 16 * group);
        }
    }
    return count;
}

// Ordinary boolean and division diagnostics can inspect an operator and the literal beneath a unary minus.
// Keep the written operands, through parentheses, where a shift, multiplication, division or conditional owns them.
static bool check_integer_window_observed(const Parser* parser, uint32_t token, CheckOp op)
{
    uint32_t parent = parser->tree.parents[token];
    while (parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_GROUP)
    {
        parent = parser->tree.parents[parent];
    }
    const uint32_t role = parent < parser->tokens.count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
    return op == CHECK_OP_SHIFT_LEFT || role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_MULTIPLICATIVE ||
           role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_CLOSE_TERNARY;
}

// Let the ordinary integer decoder type nondecimal spellings too. A bounded number cannot request
// wide integer storage. All literals outside this domain take the ordinary parsing and diagnostic path.
static uint32_t check_integer_window_literal(CheckInputReader* reader, uint32_t token, uint32_t value)
{
    const uint32_t source = reader->parser->tokens.sources[token];
    const PPTokens* pp = &reader->pp->output;
    if (value == BUSTER_CHECK_NONE && reader->parser->tokens.names[token] == 0 && pp->kinds[source] == C_TOKEN_NUMERIC_CONSTANT &&
        pp->values[source] - pp->locations[source] <= 16)
    {
        uint8_t buffer[17];
        const uint8_t* bytes = NULL;
        const uint32_t length = pp_spelling(reader->pp, pp->kinds[source], pp->values[source], pp->locations[source], buffer, &bytes);
        CheckLiteral literal = { 0 };
        if (length <= 16 && bytes != NULL)
        {
            check_literal_integer(bytes, length, &literal);
        }
        if (literal.valid && literal.type == BUSTER_TYPE_INT && !literal.real && !literal.imaginary && literal.bit_width == 0)
        {
            value = (uint32_t)literal.integer;
        }
    }
    return value;
}

static __attribute__((noinline)) uint32_t check_integer_window_actions(CheckInputReader* reader, CheckInputBatch* batch, const uint32_t* tokens,
    const uint32_t* classes, uint32_t acting, const CheckInputIdentifiers* identifiers, const CheckInputLiterals* literals, uint64_t* visited)
{
    const Parser* parser = reader->parser;
    CheckIntegerWindowRange ranges[BUSTER_CHECK_EXPRESSION_CHUNK / 2 + 1];
    CheckIntegerWindowSpan spans[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint16_t ends[BUSTER_CHECK_EXPRESSION_CHUNK] = { 0 };
    uint16_t selected[BUSTER_CHECK_EXPRESSION_CHUNK / 2 + 1];
    const uint32_t count = check_integer_window_ranges(classes, acting, ranges);
    uint32_t selections = 0;
    for (uint32_t r = 0; r < count; ++r)
    {
        const CheckIntegerWindowRange range = ranges[r];
        uint32_t literal = range.literal;
        for (uint32_t i = range.first; i < range.end; ++i)
        {
            const uint32_t code = check_input_window_code(classes, i);
            const uint32_t token = tokens[i];
            const uint8_t kind = parser->tokens.kinds[token];
            spans[i].nodes = 0;
            if (code == CHECK_INPUT_CODE_LITERAL)
            {
                const uint32_t value = kind == PARSE_KIND_NUMBER ? check_integer_window_literal(reader, token, literals->values[literal]) : BUSTER_CHECK_NONE;
                ++literal;
                if (value != BUSTER_CHECK_NONE)
                {
                    spans[i] = (CheckIntegerWindowSpan){ value, (uint16_t)i, 1, 1 };
                }
                continue;
            }
            if (i == range.first || spans[i - 1].nodes == 0 || parser->tree.parents[tokens[i - 1]] != token)
            {
                continue;
            }
            const CheckIntegerWindowSpan right = spans[i - 1];
            if (code == CHECK_INPUT_CODE_GROUP)
            {
                spans[i] = right;
                continue;
            }
            const bool binary = code == CHECK_INPUT_CODE_BINARY;
            CheckIntegerWindowSpan left = right;
            if (binary)
            {
                if (right.first == range.first || spans[right.first - 1].nodes == 0 || parser->tree.parents[tokens[right.first - 1]] != token)
                {
                    continue;
                }
                left = spans[right.first - 1];
            }
            const CheckOp op = binary ? check_input_binary(kind) : kind == PARSE_KIND_PLUS ? CHECK_OP_POSITIVE : kind == PARSE_KIND_MINUS ?
                               CHECK_OP_NEGATIVE : kind == PARSE_KIND_TILDE ? CHECK_OP_BIT_NOT : CHECK_OP_COUNT;
            uint32_t value;
            if (op < CHECK_OP_COUNT && (reader->constant_contexts[token] & BUSTER_CHECK_CONSTANT_ASSERTION_CONTEXT) == 0 &&
                !check_integer_window_observed(parser, token, op) && check_node_window_fold_int(op, left.value, right.value, binary, &value))
            {
                spans[i] = (CheckIntegerWindowSpan){ value, left.first, (uint16_t)(left.literals + (binary ? right.literals : 0)),
                                                     (uint16_t)(1 + left.nodes + (binary ? right.nodes : 0)) };
                ends[left.first] = (uint16_t)(i + 1);
            }
        }
        // Spans with a common first operand grow from the inside out. Take maximal disjoint spans in source postorder.
        for (uint32_t i = range.first; i < range.end;)
        {
            if (ends[i] > i)
            {
                selected[selections++] = (uint16_t)i;
                i = ends[i];
            }
            else
            {
                ++i;
            }
        }
    }
    selected[selections] = (uint16_t)acting;
    uint32_t identifier = 0;
    uint32_t literal = 0;
    uint32_t cursor = 0;
    for (uint32_t k = 0; k <= selections && !reader->output->resource; ++k)
    {
        const uint32_t first = selected[k];
        while (cursor < first && !reader->output->resource)
        {
            check_input_expression_coded(reader, batch, tokens[cursor], (uint8_t)classes[cursor], true, identifiers, &identifier, literals, &literal, visited);
            ++cursor;
        }
        if (k < selections && !reader->output->resource)
        {
            const uint32_t end = ends[first];
            const uint32_t token = tokens[end - 1];
            const CheckIntegerWindowSpan span = spans[end - 1];
            check_input_expression_room(reader, batch, 1);
            const uint32_t node = check_input_batch_add(reader, batch, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, CHECK_INPUT_FORM_LEAF);
            if (node == BUSTER_CHECK_NONE)
            {
                break;
            }
            const CheckValue value = { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_ICE,
                                       .constant_kind = CHECK_CONSTANT_INTEGER, .integer = span.value };
            // A later statement-expression can query a skipped action's value chain.
            for (uint32_t i = first; i < end; ++i)
            {
                reader->first_value[tokens[i]] = BUSTER_CHECK_NONE;
            }
            check_input_batch_set_leaf(reader, batch, node, &value);
            check_input_expression_value(reader, token, node);
            reader->output->integer_window_count += 1;
            reader->output->integer_window_removed += span.nodes - 1u;
            literal += span.literals;
            cursor = end;
        }
    }
    return cursor;
}
