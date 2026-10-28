// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU syntax metadata (#147), included by c_parser.c before its entry point. The parser has already resolved
// grammar, brackets and attachment roles. This pass keeps each GNU attribute's name and bounded arguments, and
// distinguishes declarator asm labels from assembly statements. It does not apply attributes or rename symbols.
//
// gnu_syntax_collect discovers candidates 64 token kinds at a time, with masked tails and no input padding. Its
// scalar twin uses the same bounded group reader. Groups are in source order; each group's attributes occupy one
// contiguous range in source order. Nested groups have their own ranges, so attribute rows across groups need not
// be in token order. A semantic consumer follows syntax/declaration dependencies and orders diagnostics by token.
//
// Input: successful parser output, immutable for the call. Group opener -> matching closer is a tree invariant.
// Output: caller-owned disjoint arrays, naturally aligned, counts distinct from capacities, no padding. Tokens
// and IdentIds remain owned by Parser and InternTable; resolving sources also requires the original Preprocessor
// output and source storage to remain alive and unchanged. Token slices are half-open; arguments exclude their
// parentheses. A group's parent is its syntactic attachment, not a resolved declaration. Unknown names and empty lists are preserved.
// Collection does not allocate. Measurement uses the same traversal without output; gnu_syntax_build measures,
// reserves exact arrays in the caller's arena, then fills them. On failure no rows are valid; build rolls its arena
// back. Ordinary syntax-only parsing need not request this metadata before the semantic consumer exists.

typedef enum GnuSyntaxStatus
{
    GNU_SYNTAX_OK,
    GNU_SYNTAX_INVALID_TREE,
    GNU_SYNTAX_CAPACITY,
    GNU_SYNTAX_MEMORY,
} GnuSyntaxStatus;

typedef struct GnuAttributeGroup GnuAttributeGroup;
struct GnuAttributeGroup
{
    uint32_t keyword;
    uint32_t close;
    uint32_t parent;
    uint32_t attribute_begin;
    uint32_t attribute_count;
    uint8_t role;
};

typedef struct GnuAttribute GnuAttribute;
struct GnuAttribute
{
    uint32_t name;
    // BUSTER_PARSE_NONE for a bare name; equal bounds for an explicitly empty argument list.
    uint32_t argument_begin;
    uint32_t argument_end;
    uint32_t group;
};

typedef struct GnuAsmLabel GnuAsmLabel;
struct GnuAsmLabel
{
    uint32_t keyword;
    // A nonempty sequence of adjacent string tokens, retaining spelling and source through ParseTokens.sources.
    uint32_t string_begin;
    uint32_t string_end;
    uint32_t parent;
};

typedef struct GnuSyntax GnuSyntax;
struct GnuSyntax
{
    GnuAttributeGroup* groups;
    GnuAttribute* attributes;
    GnuAsmLabel* asm_labels;
    uint32_t group_count;
    uint32_t attribute_count;
    uint32_t asm_label_count;
    uint32_t group_capacity;
    uint32_t attribute_capacity;
    uint32_t asm_label_capacity;
};

// Only the parser's four GNU attribute attachment classes; C23 [[...]] groups are distinct.
static bool gnu_syntax_attribute_role(uint8_t role)
{
    return role == PARSE_ROLE_OPEN_ATTRIBUTE || role == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER ||
           role == PARSE_ROLE_OPEN_ATTRIBUTE_LEFT || role == PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT;
}

static bool gnu_syntax_parent_valid(uint32_t parent, uint32_t count)
{
    return parent == BUSTER_PARSE_ROOT || parent < count;
}

static bool gnu_syntax_paren(const ParseTokens* tokens, const ParseTree* tree, uint32_t open, uint32_t limit, uint32_t* close)
{
    bool result = open < limit && limit <= tokens->count && tokens->kinds[open] == PARSE_KIND_L_PAREN;
    if (result)
    {
        const uint32_t end = tree->parents[open];
        result = end > open && end < limit && tokens->kinds[end] == PARSE_KIND_R_PAREN;
        if (result)
        {
            *close = end;
        }
    }
    return result;
}

static GnuSyntaxStatus gnu_syntax_attribute(const ParseTokens* tokens, const ParseTree* tree, uint32_t keyword, GnuSyntax* syntax, bool measure)
{
    GnuSyntaxStatus result = GNU_SYNTAX_OK;
    uint32_t close = 0;
    uint32_t list_close = 0;
    const uint32_t list_open = keyword + 2;
    const bool valid = list_open < tokens->count && gnu_syntax_paren(tokens, tree, keyword + 1, tokens->count, &close) &&
                       gnu_syntax_paren(tokens, tree, list_open, close, &list_close) && list_close + 1 == close &&
                       tree->roles[list_open] == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && gnu_syntax_parent_valid(tree->parents[close], tokens->count);
    if (!valid)
    {
        result = GNU_SYNTAX_INVALID_TREE;
    }
    else if (!measure && syntax->group_count == syntax->group_capacity)
    {
        result = GNU_SYNTAX_CAPACITY;
    }
    else
    {
        const uint32_t group = syntax->group_count;
        const uint32_t first = syntax->attribute_count;
        uint32_t token = list_open + 1;
        while (result == GNU_SYNTAX_OK && token < list_close)
        {
            if (tokens->kinds[token] == PARSE_KIND_COMMA)
            {
                token += 1;
            }
            else
            {
                GnuAttribute attribute = {
                    .name = token,
                    .argument_begin = BUSTER_PARSE_NONE,
                    .argument_end = BUSTER_PARSE_NONE,
                    .group = group,
                };
                if (tokens->names[token] == 0)
                {
                    result = GNU_SYNTAX_INVALID_TREE;
                }
                token += 1;
                if (result == GNU_SYNTAX_OK && token < list_close && tokens->kinds[token] == PARSE_KIND_L_PAREN)
                {
                    uint32_t argument_close = 0;
                    if (gnu_syntax_paren(tokens, tree, token, list_close, &argument_close))
                    {
                        attribute.argument_begin = token + 1;
                        attribute.argument_end = argument_close;
                        token = argument_close + 1;
                    }
                    else
                    {
                        result = GNU_SYNTAX_INVALID_TREE;
                    }
                }
                if (result == GNU_SYNTAX_OK && token < list_close && tokens->kinds[token] != PARSE_KIND_COMMA)
                {
                    result = GNU_SYNTAX_INVALID_TREE;
                }
                if (result == GNU_SYNTAX_OK)
                {
                    if (!measure && syntax->attribute_count == syntax->attribute_capacity)
                    {
                        result = GNU_SYNTAX_CAPACITY;
                    }
                    else
                    {
                        if (!measure)
                        {
                            syntax->attributes[syntax->attribute_count] = attribute;
                        }
                        syntax->attribute_count += 1;
                    }
                }
            }
        }
        if (result == GNU_SYNTAX_OK)
        {
            if (!measure)
            {
                syntax->groups[group] = (GnuAttributeGroup){
                    .keyword = keyword,
                    .close = close,
                    .parent = tree->parents[close],
                    .attribute_begin = first,
                    .attribute_count = syntax->attribute_count - first,
                    .role = tree->roles[close],
                };
            }
            syntax->group_count += 1;
        }
    }
    return result;
}

static GnuSyntaxStatus gnu_syntax_asm_label(const ParseTokens* tokens, const ParseTree* tree, uint32_t keyword, GnuSyntax* syntax, bool measure)
{
    GnuSyntaxStatus result = GNU_SYNTAX_OK;
    uint32_t close = 0;
    const uint32_t begin = keyword + 2;
    bool valid = gnu_syntax_paren(tokens, tree, keyword + 1, tokens->count, &close) && begin < close &&
                 gnu_syntax_parent_valid(tree->parents[close], tokens->count);
    for (uint32_t token = begin; valid && token < close; token += 1)
    {
        valid = tokens->kinds[token] == PARSE_KIND_STRING;
    }
    if (!valid)
    {
        result = GNU_SYNTAX_INVALID_TREE;
    }
    else if (!measure && syntax->asm_label_count == syntax->asm_label_capacity)
    {
        result = GNU_SYNTAX_CAPACITY;
    }
    else
    {
        if (!measure)
        {
            syntax->asm_labels[syntax->asm_label_count] = (GnuAsmLabel){
                .keyword = keyword,
                .string_begin = begin,
                .string_end = close,
                .parent = tree->parents[close],
            };
        }
        syntax->asm_label_count += 1;
    }
    return result;
}

static uint64_t gnu_syntax_candidates_scalar(const uint8_t* kinds, uint32_t count)
{
    uint64_t result = 0;
    // Keep the reference and scalar-conditioning path scalar even when the compiler recognizes the reduction.
#pragma clang loop vectorize(disable) interleave(disable)
    for (uint32_t i = 0; i < count; i += 1)
    {
        result |= (uint64_t)(kinds[i] == PARSE_KIND_ATTRIBUTE || kinds[i] == PARSE_KIND_ASM) << i;
    }
    return result;
}

static uint64_t gnu_syntax_candidates(const uint8_t* kinds, uint32_t count)
{
#if !BUSTER_HOST_VECTOR
    return gnu_syntax_candidates_scalar(kinds, count);
#else
    const uint64_t active = count == 64 ? UINT64_MAX : (1ull << count) - 1;
    const __m512i values = _mm512_maskz_loadu_epi8(active, kinds);
    return active & (_mm512_cmpeq_epi8_mask(values, _mm512_set1_epi8(PARSE_KIND_ATTRIBUTE)) |
                     _mm512_cmpeq_epi8_mask(values, _mm512_set1_epi8(PARSE_KIND_ASM)));
#endif
}

// Resets counts, retaining the caller's arrays/capacities. On any failure all counts are zero, even when earlier
// rows were written. Measurement ignores arrays/capacities. The successful-parse input contract is required; the
// structural checks defend the slices this reader follows, not every invariant of an arbitrary foreign tree.
static GnuSyntaxStatus gnu_syntax_collect(const ParseTokens* tokens, const ParseTree* tree, GnuSyntax* syntax, bool scalar, bool measure)
{
    (void)scalar;

    syntax->group_count = 0;
    syntax->attribute_count = 0;
    syntax->asm_label_count = 0;
    GnuSyntaxStatus result = GNU_SYNTAX_OK;
    if (tokens->count > BUSTER_PARSE_MAX_TOKENS || (tokens->count != 0 &&
        (tokens->kinds == 0 || tokens->names == 0 || tree->roles == 0 || tree->parents == 0)))
    {
        result = GNU_SYNTAX_INVALID_TREE;
    }
    else if (!measure && ((syntax->group_capacity != 0 && syntax->groups == 0) ||
                          (syntax->attribute_capacity != 0 && syntax->attributes == 0) ||
                          (syntax->asm_label_capacity != 0 && syntax->asm_labels == 0)))
    {
        result = GNU_SYNTAX_CAPACITY;
    }
    for (uint32_t begin = 0; result == GNU_SYNTAX_OK && begin < tokens->count; begin += 64)
    {
        const uint32_t count = tokens->count - begin < 64 ? tokens->count - begin : 64;
        uint64_t candidates = BUSTER_SCALAR_PATH(scalar) ? gnu_syntax_candidates_scalar(tokens->kinds + begin, count) : gnu_syntax_candidates(tokens->kinds + begin, count);
        while (result == GNU_SYNTAX_OK && candidates != 0)
        {
            const uint32_t token = begin + (uint32_t)__builtin_ctzll(candidates);
            candidates &= candidates - 1;
            if (token + 1 < tokens->count && gnu_syntax_attribute_role(tree->roles[token + 1]))
            {
                // A keyword appearing as an attribute argument/name is not itself a group. Assembly statements
                // have OPEN_ASM, and never enter this path, even when they have no qualifiers or operands.
                if (tokens->kinds[token] == PARSE_KIND_ATTRIBUTE)
                {
                    result = gnu_syntax_attribute(tokens, tree, token, syntax, measure);
                }
                else
                {
                    result = gnu_syntax_asm_label(tokens, tree, token, syntax, measure);
                }
            }
        }
    }
    if (result != GNU_SYNTAX_OK)
    {
        syntax->group_count = 0;
        syntax->attribute_count = 0;
        syntax->asm_label_count = 0;
    }
    return result;
}

static GnuSyntaxStatus gnu_syntax_build(const ParseTokens* tokens, const ParseTree* tree, Arena* arena, GnuSyntax* syntax, bool scalar)
{
    (void)scalar;

    const uint64_t mark = arena->length;
    GnuSyntax result = { 0 };
    GnuSyntaxStatus status = gnu_syntax_collect(tokens, tree, &result, BUSTER_SCALAR_PATH(scalar), true);
    if (status == GNU_SYNTAX_OK)
    {
        result.group_capacity = result.group_count;
        result.attribute_capacity = result.attribute_count;
        result.asm_label_capacity = result.asm_label_count;
        result.groups = result.group_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, GnuAttributeGroup, result.group_count) : 0;
        result.attributes = result.attribute_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, GnuAttribute, result.attribute_count) : 0;
        result.asm_labels = result.asm_label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, GnuAsmLabel, result.asm_label_count) : 0;
        if ((result.group_count != 0 && result.groups == 0) || (result.attribute_count != 0 && result.attributes == 0) ||
            (result.asm_label_count != 0 && result.asm_labels == 0))
        {
            status = GNU_SYNTAX_MEMORY;
        }
        else
        {
            status = gnu_syntax_collect(tokens, tree, &result, BUSTER_SCALAR_PATH(scalar), false);
        }
    }
    if (status != GNU_SYNTAX_OK)
    {
        arena_pop_to(arena, mark);
        result = (GnuSyntax){ 0 };
    }
    *syntax = result;
    return status;
}
