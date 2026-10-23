// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Coverage is derived from the ordinary parser's tree, never from the sink's expression/emitted flags or decoded
// identities. The structural decoder is a separate check: it proves that the required nodes have the right edges.
// This file proves which nodes are required. It is intentionally a scalar, iterative test oracle, not a producer.
// Declaration names/pointers, statement punctuation, attributes and asm syntax are explicit grammar exclusions.
// Bounds, initializers, static-assert conditions and asm operand expressions are still checked. Unknown roles fail
// closed. Type/designator leaves name ordinary-parser subtrees; this is syntax coverage, not semantic or SSA proof.

typedef enum CFragmentCoverageContext
{
    C_FRAGMENT_COVER_ITEM,
    C_FRAGMENT_COVER_DECLARATION,
    C_FRAGMENT_COVER_EXPRESSION,
    C_FRAGMENT_COVER_KEYWORD,
    C_FRAGMENT_COVER_DESIGNATOR,
    C_FRAGMENT_COVER_ASSERTION,
    C_FRAGMENT_COVER_ASM,
    C_FRAGMENT_COVER_GOTO,
    C_FRAGMENT_COVER_EXCLUDED,
    C_FRAGMENT_COVER_UNSET,
} CFragmentCoverageContext;

typedef struct CFragmentCoverageNode CFragmentCoverageNode;
struct CFragmentCoverageNode
{
    uint32_t first;
    uint32_t last;
    uint32_t next;
    uint32_t opener;
    uint32_t keyword;
    uint32_t source;
    uint32_t auxiliary;
    uint32_t type_root;
    uint32_t children;
    uint8_t context;
    uint8_t kind;
    bool transparent;
    bool visited;
};

static bool c_fragment_coverage_type(uint8_t role)
{
    return role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG || role == PARSE_ROLE_CLOSE_SPECIFIER ||
           role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
}

// These mappings deliberately do not call the producer's classification helpers or use its arity/fixity tables.
static uint8_t c_fragment_coverage_operator(uint8_t role, uint8_t kind, IdentId name)
{
    uint8_t result = C_FRAGMENT_KIND_COUNT;
    if (role == PARSE_ROLE_PREFIX)
    {
        switch (kind)
        {
            case PARSE_KIND_PLUS: result = C_FRAGMENT_POSITIVE; break;
            case PARSE_KIND_MINUS: result = C_FRAGMENT_NEGATIVE; break;
            case PARSE_KIND_STAR: result = C_FRAGMENT_DEREFERENCE; break;
            case PARSE_KIND_AMP: result = C_FRAGMENT_ADDRESS; break;
            case PARSE_KIND_EXCLAIM: result = C_FRAGMENT_NOT; break;
            case PARSE_KIND_TILDE: result = C_FRAGMENT_COMPLEMENT; break;
            case PARSE_KIND_PLUSPLUS: result = C_FRAGMENT_PRE_INCREMENT; break;
            case PARSE_KIND_MINUSMINUS: result = C_FRAGMENT_PRE_DECREMENT; break;
            case PARSE_KIND_AMPAMP: result = C_FRAGMENT_LABEL_ADDRESS; break;
            case PARSE_KIND_EXTENSION: result = C_FRAGMENT_EXTENSION; break;
            case PARSE_KIND_REAL_IMAG:
                result = name == C_NAME_GNU_IMAG || name == C_NAME_GNU_IMAG2 ? C_FRAGMENT_IMAG : C_FRAGMENT_REAL;
                break;
            case PARSE_KIND_SIZEOF:
                result = name == C_NAME_COUNTOF ? C_FRAGMENT_COUNTOF :
                         name == C_NAME_ALIGNOF || name == C_NAME_ALIGNOF_C11 || name == C_NAME_GNU_ALIGNOF || name == C_NAME_GNU_ALIGNOF2 ? C_FRAGMENT_ALIGNOF : C_FRAGMENT_SIZEOF;
                break;
            default: break;
        }
    }
    else if (role == PARSE_ROLE_POSTFIX)
    {
        result = kind == PARSE_KIND_PLUSPLUS ? C_FRAGMENT_POST_INCREMENT : kind == PARSE_KIND_MINUSMINUS ? C_FRAGMENT_POST_DECREMENT : C_FRAGMENT_KIND_COUNT;
    }
    else
    {
        switch (kind)
        {
            case PARSE_KIND_STAR: result = C_FRAGMENT_MULTIPLY; break;
            case PARSE_KIND_SLASH: result = C_FRAGMENT_DIVIDE; break;
            case PARSE_KIND_PERCENT: result = C_FRAGMENT_REMAINDER; break;
            case PARSE_KIND_PLUS: result = C_FRAGMENT_ADD; break;
            case PARSE_KIND_MINUS: result = C_FRAGMENT_SUBTRACT; break;
            case PARSE_KIND_LESSLESS: result = C_FRAGMENT_SHIFT_LEFT; break;
            case PARSE_KIND_GREATERGREATER: result = C_FRAGMENT_SHIFT_RIGHT; break;
            case PARSE_KIND_LESS: result = C_FRAGMENT_LESS; break;
            case PARSE_KIND_LESSEQUAL: result = C_FRAGMENT_LESS_EQUAL; break;
            case PARSE_KIND_GREATER: result = C_FRAGMENT_GREATER; break;
            case PARSE_KIND_GREATEREQUAL: result = C_FRAGMENT_GREATER_EQUAL; break;
            case PARSE_KIND_EQUALEQUAL: result = C_FRAGMENT_EQUAL; break;
            case PARSE_KIND_EXCLAIMEQUAL: result = C_FRAGMENT_NOT_EQUAL; break;
            case PARSE_KIND_AMP: result = C_FRAGMENT_BIT_AND; break;
            case PARSE_KIND_CARET: result = C_FRAGMENT_BIT_XOR; break;
            case PARSE_KIND_PIPE: result = C_FRAGMENT_BIT_OR; break;
            case PARSE_KIND_AMPAMP: result = C_FRAGMENT_LOGICAL_AND; break;
            case PARSE_KIND_PIPEPIPE: result = C_FRAGMENT_LOGICAL_OR; break;
            case PARSE_KIND_EQUAL: result = C_FRAGMENT_ASSIGN; break;
            case PARSE_KIND_PLUSEQUAL: result = C_FRAGMENT_ADD_ASSIGN; break;
            case PARSE_KIND_MINUSEQUAL: result = C_FRAGMENT_SUBTRACT_ASSIGN; break;
            case PARSE_KIND_STAREQUAL: result = C_FRAGMENT_MULTIPLY_ASSIGN; break;
            case PARSE_KIND_SLASHEQUAL: result = C_FRAGMENT_DIVIDE_ASSIGN; break;
            case PARSE_KIND_PERCENTEQUAL: result = C_FRAGMENT_REMAINDER_ASSIGN; break;
            case PARSE_KIND_LESSLESSEQUAL: result = C_FRAGMENT_LEFT_ASSIGN; break;
            case PARSE_KIND_GREATERGREATEREQUAL: result = C_FRAGMENT_RIGHT_ASSIGN; break;
            case PARSE_KIND_AMPEQUAL: result = C_FRAGMENT_AND_ASSIGN; break;
            case PARSE_KIND_CARETEQUAL: result = C_FRAGMENT_XOR_ASSIGN; break;
            case PARSE_KIND_PIPEEQUAL: result = C_FRAGMENT_OR_ASSIGN; break;
            case PARSE_KIND_PERIOD: result = C_FRAGMENT_MEMBER; break;
            case PARSE_KIND_ARROW: result = C_FRAGMENT_POINTER_MEMBER; break;
            case PARSE_KIND_COMMA: result = C_FRAGMENT_COMMA; break;
            case PARSE_KIND_ELLIPSIS: result = C_FRAGMENT_RANGE; break;
            default: break;
        }
    }
    return result;
}

static bool c_fragment_test_coverage(const Parser* parser, const CFragmentSink* sink, Arena* arena)
{
    const uint32_t count = parser->tokens.count;
    CFragmentCoverageNode* nodes = BUSTER_ARENA_PUSH_ARRAY(arena, CFragmentCoverageNode, (uint64_t)count + 1);
    bool result = nodes != 0 && count != UINT32_MAX && count == sink->token_count && parser->postorder != 0;
    uint32_t failed = BUSTER_C_FRAGMENT_NONE;
    const char* reason = "allocation or input";
    if (result)
    {
        for (uint32_t k = 0; k <= count; k += 1)
        {
            nodes[k] = (CFragmentCoverageNode){
                .first = BUSTER_C_FRAGMENT_NONE,
                .last = BUSTER_C_FRAGMENT_NONE,
                .next = BUSTER_C_FRAGMENT_NONE,
                .opener = BUSTER_C_FRAGMENT_NONE,
                .keyword = BUSTER_C_FRAGMENT_NONE,
                .source = k,
                .auxiliary = BUSTER_C_FRAGMENT_NONE,
                .type_root = BUSTER_C_FRAGMENT_NONE,
                .context = C_FRAGMENT_COVER_UNSET,
                .kind = C_FRAGMENT_KIND_COUNT,
            };
        }
        for (uint32_t k = 0; result && k < count; k += 1)
        {
            const uint32_t raw_parent = parser->tree.parents[k];
            const uint32_t parent = raw_parent == BUSTER_PARSE_ROOT ? count : raw_parent;
            result = parent <= count && parent != k && parser->tree.roles[k] > PARSE_ROLE_ERROR && parser->tree.roles[k] < PARSE_ROLE_COUNT;
            if (result)
            {
                if (nodes[parent].last == BUSTER_C_FRAGMENT_NONE)
                {
                    nodes[parent].first = k;
                }
                else
                {
                    nodes[nodes[parent].last].next = k;
                }
                nodes[parent].last = k;
                nodes[k].context = parent == count ? C_FRAGMENT_COVER_ITEM : C_FRAGMENT_COVER_UNSET;
                const uint8_t role = parser->tree.roles[k];
                if (role >= PARSE_ROLE_OPEN_GROUP && role <= PARSE_ROLE_OPEN_CASE)
                {
                    result = parent < count && parent > k && nodes[parent].opener == BUSTER_C_FRAGMENT_NONE &&
                             parser->tree.roles[parent] >= PARSE_ROLE_CLOSE_GROUP && parser->tree.roles[parent] <= PARSE_ROLE_CLOSE_CASE;
                    if (result)
                    {
                        nodes[parent].opener = k;
                    }
                }
            }
            if (!result)
            {
                failed = k;
                reason = "ordinary tree";
            }
        }
    }
    // Reverse postorder visits every parent before its children. Context comes solely from that parent and from
    // source positions relative to its opener/operator, not from the recognizer's transient mode or sink metadata.
    for (uint32_t position = count; result && position != 0; position -= 1)
    {
        const uint32_t token = parser->postorder[position - 1];
        result = token < count && !nodes[token].visited && nodes[token].context != C_FRAGMENT_COVER_UNSET;
        if (result)
        {
            CFragmentCoverageNode* node = &nodes[token];
            node->visited = true;
            const uint8_t role = parser->tree.roles[token];
            const uint8_t input = parser->tokens.kinds[token];
            uint8_t context = node->context;
            uint8_t expected = C_FRAGMENT_KIND_COUNT;
            uint32_t keyword = node->keyword;
            const uint32_t opener = node->opener;
            uint32_t left_end = opener;
            uint32_t right_begin = token;
            uint8_t left = context;
            uint8_t middle = context;
            uint8_t right = context;
            bool opaque = false;
            const bool opening = role >= PARSE_ROLE_OPEN_GROUP && role <= PARSE_ROLE_OPEN_CASE;
            const bool closing = role >= PARSE_ROLE_CLOSE_GROUP && role <= PARSE_ROLE_CLOSE_CASE;
            result = !closing || opener < token;
            if (opening || role == PARSE_ROLE_LEAF_RIGHT || role == PARSE_ROLE_QUALIFIER_LEFT)
            {
                context = C_FRAGMENT_COVER_EXCLUDED;
            }
            if (context == C_FRAGMENT_COVER_KEYWORD && role != PARSE_ROLE_SEPARATOR && role != PARSE_ROLE_ASSOCIATION)
            {
                if (c_fragment_coverage_type(role))
                {
                    expected = C_FRAGMENT_TYPE_NAME;
                    context = C_FRAGMENT_COVER_DECLARATION;
                    opaque = true;
                }
                else if (input == PARSE_KIND_DEFAULT)
                {
                    expected = C_FRAGMENT_TYPE_DEFAULT;
                    context = C_FRAGMENT_COVER_EXCLUDED;
                    opaque = true;
                }
                else if (keyword < count && parser->tokens.names[keyword] == C_NAME_BUILTIN_OFFSETOF)
                {
                    expected = C_FRAGMENT_DESIGNATOR;
                    context = C_FRAGMENT_COVER_DESIGNATOR;
                    opaque = true;
                }
                else
                {
                    context = C_FRAGMENT_COVER_EXPRESSION;
                }
                node->type_root = opaque ? token : BUSTER_C_FRAGMENT_NONE;
            }
            if (context == C_FRAGMENT_COVER_ASSERTION && role != PARSE_ROLE_SEPARATOR)
            {
                context = C_FRAGMENT_COVER_EXPRESSION;
            }
            left = context;
            middle = context;
            right = context;
            if (context == C_FRAGMENT_COVER_EXCLUDED)
            {
                // Deliberately opaque attribute/message/keyword punctuation, not an unknown expression fallback.
            }
            else if (context == C_FRAGMENT_COVER_GOTO)
            {
                result = role == PARSE_ROLE_OPERAND || (role == PARSE_ROLE_PREFIX && input == PARSE_KIND_STAR);
                left = middle = right = role == PARSE_ROLE_PREFIX ? C_FRAGMENT_COVER_EXPRESSION : C_FRAGMENT_COVER_EXCLUDED;
            }
            else if (role >= PARSE_ROLE_CLOSE_ATTRIBUTE && role <= PARSE_ROLE_CLOSE_ATTRIBUTE_LIST)
            {
                // Attribute closers can also own the surrounding declarator; exclude only their inner syntax.
                middle = C_FRAGMENT_COVER_EXCLUDED;
                left = right = context == C_FRAGMENT_COVER_ITEM ? C_FRAGMENT_COVER_DECLARATION : context;
            }
            else if (role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO)
            {
                left = middle = right = C_FRAGMENT_COVER_ITEM;
            }
            else if (role == PARSE_ROLE_LABEL)
            {
                left_end = right_begin = token;
                left = C_FRAGMENT_COVER_EXCLUDED;
                right = C_FRAGMENT_COVER_ITEM;
            }
            else if (role == PARSE_ROLE_RETURN)
            {
                left = middle = right = input == PARSE_KIND_RETURN ? C_FRAGMENT_COVER_EXPRESSION :
                                       input == PARSE_KIND_GOTO ? C_FRAGMENT_COVER_GOTO : C_FRAGMENT_COVER_EXCLUDED;
            }
            else if (role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG)
            {
                result = context != C_FRAGMENT_COVER_EXPRESSION;
                left = middle = right = C_FRAGMENT_COVER_DECLARATION;
            }
            else if (role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_FUNCTION)
            {
                left = right = C_FRAGMENT_COVER_DECLARATION;
                middle = role == PARSE_ROLE_CLOSE_BODY ? C_FRAGMENT_COVER_ITEM : C_FRAGMENT_COVER_DECLARATION;
            }
            else if (role == PARSE_ROLE_CLOSE_BLOCK)
            {
                if (context == C_FRAGMENT_COVER_EXPRESSION)
                {
                    expected = C_FRAGMENT_STATEMENT_EXPRESSION;
                    node->source = opener;
                    node->type_root = token;
                }
                left = middle = right = C_FRAGMENT_COVER_ITEM;
            }
            else if (role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_CLOSE_CASE)
            {
                left = C_FRAGMENT_COVER_EXCLUDED;
                middle = role == PARSE_ROLE_CLOSE_FOR ? C_FRAGMENT_COVER_ITEM : C_FRAGMENT_COVER_EXPRESSION;
                right = C_FRAGMENT_COVER_ITEM;
            }
            else if (role == PARSE_ROLE_CLOSE_SPECIFIER)
            {
                left = right = C_FRAGMENT_COVER_DECLARATION;
                middle = C_FRAGMENT_COVER_ITEM;
            }
            else if (role == PARSE_ROLE_CLOSE_ASM)
            {
                left = middle = right = C_FRAGMENT_COVER_ASM;
            }
            else if (context == C_FRAGMENT_COVER_ASM)
            {
                // GNU asm templates, constraints, clobbers and labels are grammar, not expression leaves. The
                // parser gives each operand's parenthesized expression a CLOSE_CALL beneath that grammar.
                if (role == PARSE_ROLE_CLOSE_CALL)
                {
                    middle = C_FRAGMENT_COVER_EXPRESSION;
                }
                else
                {
                    result = role == PARSE_ROLE_ASM_COLON || role == PARSE_ROLE_SEPARATOR || role == PARSE_ROLE_COMMA ||
                             role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_STRING_NEXT || role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_MEMBER;
                }
            }
            else if (role == PARSE_ROLE_CLOSE_KEYWORD)
            {
                result = opener != 0;
                keyword = opener - (opener != 0);
                if (result && parser->tokens.kinds[keyword] == PARSE_KIND_STATIC_ASSERT)
                {
                    left = right = C_FRAGMENT_COVER_EXCLUDED;
                    middle = C_FRAGMENT_COVER_ASSERTION;
                }
                else if (result)
                {
                    expected = parser->tokens.kinds[keyword] == PARSE_KIND_GENERIC ? C_FRAGMENT_GENERIC : C_FRAGMENT_BUILTIN;
                    node->source = keyword;
                    left = middle = right = C_FRAGMENT_COVER_KEYWORD;
                }
            }
            else if (role == PARSE_ROLE_SEPARATOR && context == C_FRAGMENT_COVER_ASSERTION)
            {
                left_end = right_begin = token;
                left = C_FRAGMENT_COVER_EXPRESSION;
                right = C_FRAGMENT_COVER_EXCLUDED;
            }
            else if (role == PARSE_ROLE_SEPARATOR && context == C_FRAGMENT_COVER_KEYWORD)
            {
                expected = C_FRAGMENT_SEPARATOR;
            }
            else if (role == PARSE_ROLE_ASSOCIATION && context == C_FRAGMENT_COVER_KEYWORD)
            {
                expected = C_FRAGMENT_ASSOCIATION;
                left_end = right_begin = token;
                left = C_FRAGMENT_COVER_KEYWORD;
                right = C_FRAGMENT_COVER_EXPRESSION;
            }
            else if (context == C_FRAGMENT_COVER_DECLARATION && (role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_BITFIELD))
            {
                left_end = right_begin = token;
                right = C_FRAGMENT_COVER_EXPRESSION;
            }
            else if (role == PARSE_ROLE_CLOSE_SUBSCRIPT && (context == C_FRAGMENT_COVER_DECLARATION || context == C_FRAGMENT_COVER_DESIGNATOR))
            {
                middle = C_FRAGMENT_COVER_ITEM;
            }
            else if (context == C_FRAGMENT_COVER_DECLARATION || context == C_FRAGMENT_COVER_DESIGNATOR)
            {
                // Declarator spelling (including pointer '*') is not an arithmetic expression. Descend so that
                // nested function declarators and array bounds are still discovered by the branches above.
                result = role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_MEMBER || role == PARSE_ROLE_PREFIX ||
                         role == PARSE_ROLE_MEMBER_OPERATOR || role == PARSE_ROLE_COMMA || role == PARSE_ROLE_SEPARATOR || role == PARSE_ROLE_CLOSE_GROUP;
            }
            else if (role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_SIZEOF)
            {
                bool type = false;
                for (uint32_t child = node->first; child != BUSTER_C_FRAGMENT_NONE; child = nodes[child].next)
                {
                    type |= child > opener && child < token && c_fragment_coverage_type(parser->tree.roles[child]);
                }
                if (role == PARSE_ROLE_CLOSE_SIZEOF && type)
                {
                    expected = C_FRAGMENT_TYPE_NAME;
                    node->source = opener;
                    node->type_root = token;
                    middle = C_FRAGMENT_COVER_DECLARATION;
                }
                else
                {
                    node->transparent = true;
                    left = middle = right = C_FRAGMENT_COVER_EXPRESSION;
                }
            }
            else if (role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_CLOSE_COMPOUND)
            {
                expected = role == PARSE_ROLE_CLOSE_CAST ? C_FRAGMENT_CAST : C_FRAGMENT_TYPE_NAME;
                node->source = opener;
                node->type_root = role == PARSE_ROLE_CLOSE_COMPOUND ? token : BUSTER_C_FRAGMENT_NONE;
                left = right = C_FRAGMENT_COVER_EXPRESSION;
                middle = C_FRAGMENT_COVER_DECLARATION;
                if (role == PARSE_ROLE_CLOSE_CAST)
                {
                    for (uint32_t child = node->first; result && child != BUSTER_C_FRAGMENT_NONE; child = nodes[child].next)
                    {
                        if (child > opener && child < token && parser->tree.roles[child] != PARSE_ROLE_LEAF_RIGHT)
                        {
                            result = node->type_root == BUSTER_C_FRAGMENT_NONE && c_fragment_coverage_type(parser->tree.roles[child]);
                            node->type_root = child;
                        }
                    }
                    result &= node->type_root != BUSTER_C_FRAGMENT_NONE;
                }
            }
            else if (role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_CLOSE_INITIALIZER ||
                     role == PARSE_ROLE_CLOSE_COMPOUND_BODY || role == PARSE_ROLE_CLOSE_TERNARY)
            {
                expected = role == PARSE_ROLE_CLOSE_CALL ? C_FRAGMENT_CALL : role == PARSE_ROLE_CLOSE_SUBSCRIPT ? C_FRAGMENT_SUBSCRIPT :
                           role == PARSE_ROLE_CLOSE_INITIALIZER ? C_FRAGMENT_INITIALIZER : role == PARSE_ROLE_CLOSE_COMPOUND_BODY ? C_FRAGMENT_COMPOUND_LITERAL :
                           opener + 1 == token ? C_FRAGMENT_OMITTED_CONDITIONAL : C_FRAGMENT_CONDITIONAL;
                node->source = opener;
                node->auxiliary = role == PARSE_ROLE_CLOSE_TERNARY ? token : BUSTER_C_FRAGMENT_NONE;
                left = middle = right = C_FRAGMENT_COVER_EXPRESSION;
            }
            else if (role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_MEMBER || role == PARSE_ROLE_STRING_NEXT)
            {
                expected = input == PARSE_KIND_NUMBER ? C_FRAGMENT_NUMBER : input == PARSE_KIND_CHARACTER ? C_FRAGMENT_CHARACTER :
                           input == PARSE_KIND_STRING ? C_FRAGMENT_STRING : input == PARSE_KIND_IDENTIFIER ? C_FRAGMENT_IDENTIFIER : C_FRAGMENT_KIND_COUNT;
                result = expected != C_FRAGMENT_KIND_COUNT;
                left = middle = right = C_FRAGMENT_COVER_EXPRESSION;
            }
            else if (role == PARSE_ROLE_SEPARATOR)
            {
                expected = C_FRAGMENT_SEPARATOR;
                left = middle = right = C_FRAGMENT_COVER_EXPRESSION;
            }
            else if (role >= PARSE_ROLE_PREFIX && role <= PARSE_ROLE_COMMA && role != PARSE_ROLE_BITFIELD)
            {
                expected = c_fragment_coverage_operator(role, input, parser->tokens.names[token]);
                result = expected != C_FRAGMENT_KIND_COUNT;
                left = middle = right = C_FRAGMENT_COVER_EXPRESSION;
            }
            else
            {
                result = false;
            }
            node->kind = expected;
            // Every edge gets an independently chosen context, even through excluded grammar. The ordinary tree's
            // complete postorder must account for every token exactly once; omissions cannot reduce this walk.
            for (uint32_t child = node->first; result && child != BUSTER_C_FRAGMENT_NONE; child = nodes[child].next)
            {
                uint8_t child_context = child < left_end ? left : child > right_begin ? right : middle;
                const uint8_t child_role = parser->tree.roles[child];
                if (child_role >= PARSE_ROLE_OPEN_GROUP && child_role <= PARSE_ROLE_OPEN_CASE)
                {
                    child_context = C_FRAGMENT_COVER_EXCLUDED;
                }
                if (role == PARSE_ROLE_CLOSE_SUBSCRIPT && context == C_FRAGMENT_COVER_DECLARATION && child > opener && child < token &&
                    ((parser->tokens.kinds[child] == PARSE_KIND_STAR && child + 1 == token) || child_role == PARSE_ROLE_SPECIFIER))
                {
                    child_context = C_FRAGMENT_COVER_DECLARATION;
                }
                result = nodes[child].context == C_FRAGMENT_COVER_UNSET;
                nodes[child].context = child_context;
                nodes[child].keyword = keyword;
            }
        }
        if (!result)
        {
            failed = token;
            reason = "unsupported ordinary-tree context";
        }
    }
    // Count canonical children from the independently required nodes. Transparent parentheses propagate their
    // single child; statement/declaration/type grammar is a barrier. No sink child counts participate here.
    for (uint32_t position = 0; result && position < count; position += 1)
    {
        const uint32_t token = parser->postorder[position];
        CFragmentCoverageNode* node = &nodes[token];
        uint32_t children = 0;
        for (uint32_t child = node->first; child != BUSTER_C_FRAGMENT_NONE; child = nodes[child].next)
        {
            const uint32_t add = nodes[child].kind != C_FRAGMENT_KIND_COUNT ? 1 : nodes[child].transparent ? nodes[child].children : 0;
            result &= children <= count - add;
            children += add;
        }
        node->children = children;
        if (node->kind == C_FRAGMENT_STRING && children != 0)
        {
            node->kind = C_FRAGMENT_STRING_SEQUENCE;
        }
        if (node->kind == C_FRAGMENT_MEMBER && children == 1)
        {
            node->kind = C_FRAGMENT_FIELD_DESIGNATOR;
        }
        if (node->kind == C_FRAGMENT_SUBSCRIPT && children == 1)
        {
            node->kind = C_FRAGMENT_INDEX_DESIGNATOR;
        }
        if (node->kind == C_FRAGMENT_SEPARATOR && children == 1)
        {
            node->kind = C_FRAGMENT_TRAILING_SEPARATOR;
        }
        const CFragmentSyntax* actual = &sink->syntax[token];
        const bool emitted = node->kind != C_FRAGMENT_KIND_COUNT;
        result &= node->visited && (!node->transparent || children == 1) && actual->emitted == emitted && actual->transparent == node->transparent;
        if (result && emitted)
        {
            result = sink->kinds[token] == node->kind && actual->source == node->source && actual->auxiliary == node->auxiliary && actual->type_root == node->type_root &&
                     sink->sources[token] == parser->tokens.sources[token];
        }
        if (!result)
        {
            failed = token;
            reason = "required kind, identity, reference or transparency";
            fprintf(stderr, "fragment coverage token=%u expected_kind=%u actual_kind=%u expected_emitted=%u actual_emitted=%u expected_transparent=%u actual_transparent=%u\n",
                    token, node->kind, sink->kinds[token], emitted, actual->emitted, node->transparent, actual->transparent);
        }
    }
    if (!result)
    {
        fprintf(stderr, "fragment coverage: %s at token %u role=%s\n", reason, failed,
                failed < count && parser->tree.roles[failed] < PARSE_ROLE_COUNT ? parse_role_names[parser->tree.roles[failed]] : "none");
    }
    return result;
}

typedef struct CFragmentCoverageFixtureNode CFragmentCoverageFixtureNode;
struct CFragmentCoverageFixtureNode
{
    uint8_t token;
    uint8_t kind;
    uint8_t source;
    uint8_t type_root;
    uint8_t auxiliary;
};

#define BUSTER_FRAGMENT_EXPECT(token, kind) { token, C_FRAGMENT_##kind, token, UINT8_MAX, UINT8_MAX }
#define BUSTER_FRAGMENT_EXPECT_GROUP(token, kind, source) { token, C_FRAGMENT_##kind, source, UINT8_MAX, UINT8_MAX }
#define BUSTER_FRAGMENT_EXPECT_TYPE(token, kind, source, type) { token, C_FRAGMENT_##kind, source, type, UINT8_MAX }
#define BUSTER_FRAGMENT_EXPECT_TERNARY(token, kind, source) { token, C_FRAGMENT_##kind, source, UINT8_MAX, token }

// These are externally specified token positions in the literal fixtures in c_fragments_test.c, relative to the
// return operand except fixture 14 (whole file). They do not query parser roles to decide what should be emitted.
// Full required-node counts exclude declarations and parentheses, so extra output cannot compensate for omissions.
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_0[] = {
    BUSTER_FRAGMENT_EXPECT(0, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(1, ADD), BUSTER_FRAGMENT_EXPECT(2, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT(3, MULTIPLY), BUSTER_FRAGMENT_EXPECT(4, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(5, SUBTRACT),
    BUSTER_FRAGMENT_EXPECT(6, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(7, DIVIDE), BUSTER_FRAGMENT_EXPECT(8, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT(9, REMAINDER), BUSTER_FRAGMENT_EXPECT(10, IDENTIFIER),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_1[] = {
    BUSTER_FRAGMENT_EXPECT(0, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(1, ASSIGN), BUSTER_FRAGMENT_EXPECT(2, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT(3, ASSIGN), BUSTER_FRAGMENT_EXPECT(4, IDENTIFIER),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_2[] = {
    BUSTER_FRAGMENT_EXPECT(0, POSITIVE), BUSTER_FRAGMENT_EXPECT(1, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(2, ADD),
    BUSTER_FRAGMENT_EXPECT(3, NEGATIVE), BUSTER_FRAGMENT_EXPECT(4, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(5, ADD),
    BUSTER_FRAGMENT_EXPECT(6, NOT), BUSTER_FRAGMENT_EXPECT(7, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(8, ADD),
    BUSTER_FRAGMENT_EXPECT(9, COMPLEMENT), BUSTER_FRAGMENT_EXPECT(10, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(11, ADD),
    BUSTER_FRAGMENT_EXPECT(12, PRE_INCREMENT), BUSTER_FRAGMENT_EXPECT(13, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(14, ADD),
    BUSTER_FRAGMENT_EXPECT(15, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(16, POST_DECREMENT),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_3[] = {
    BUSTER_FRAGMENT_EXPECT(3, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(4, ADD), BUSTER_FRAGMENT_EXPECT(5, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT(7, ADD), BUSTER_FRAGMENT_EXPECT(9, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(10, ADD),
    BUSTER_FRAGMENT_EXPECT(11, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(16, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(17, ADD),
    BUSTER_FRAGMENT_EXPECT(18, IDENTIFIER), BUSTER_FRAGMENT_EXPECT_TERNARY(19, CONDITIONAL, 15),
    BUSTER_FRAGMENT_EXPECT(20, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(21, ADD), BUSTER_FRAGMENT_EXPECT(22, IDENTIFIER),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_4[] = {
    BUSTER_FRAGMENT_EXPECT(0, IDENTIFIER), BUSTER_FRAGMENT_EXPECT_TERNARY(2, OMITTED_CONDITIONAL, 1), BUSTER_FRAGMENT_EXPECT(3, IDENTIFIER),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_5[] = {
    BUSTER_FRAGMENT_EXPECT(0, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(2, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(3, SEPARATOR),
    BUSTER_FRAGMENT_EXPECT(4, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(5, SEPARATOR), BUSTER_FRAGMENT_EXPECT(6, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT(7, ADD), BUSTER_FRAGMENT_EXPECT(8, IDENTIFIER), BUSTER_FRAGMENT_EXPECT_GROUP(9, CALL, 1),
    BUSTER_FRAGMENT_EXPECT(10, ADD), BUSTER_FRAGMENT_EXPECT(11, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(13, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT_GROUP(14, CALL, 12),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_6[] = {
    BUSTER_FRAGMENT_EXPECT(0, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(1, POINTER_MEMBER), BUSTER_FRAGMENT_EXPECT(2, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT(3, ADD), BUSTER_FRAGMENT_EXPECT(4, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(6, NUMBER),
    BUSTER_FRAGMENT_EXPECT_GROUP(7, SUBSCRIPT, 5),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_7[] = {
    BUSTER_FRAGMENT_EXPECT_TYPE(2, CAST, 0, 1), BUSTER_FRAGMENT_EXPECT(3, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(4, ADD),
    BUSTER_FRAGMENT_EXPECT(5, SIZEOF), BUSTER_FRAGMENT_EXPECT_TYPE(8, TYPE_NAME, 6, 8), BUSTER_FRAGMENT_EXPECT(9, ADD),
    BUSTER_FRAGMENT_EXPECT(10, SIZEOF), BUSTER_FRAGMENT_EXPECT(12, IDENTIFIER),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_8[] = {
    BUSTER_FRAGMENT_EXPECT(2, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(3, SEPARATOR), BUSTER_FRAGMENT_EXPECT_TYPE(4, TYPE_NAME, 4, 4),
    BUSTER_FRAGMENT_EXPECT(5, ASSOCIATION), BUSTER_FRAGMENT_EXPECT(6, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(7, ADD),
    BUSTER_FRAGMENT_EXPECT(8, NUMBER), BUSTER_FRAGMENT_EXPECT(9, SEPARATOR), BUSTER_FRAGMENT_EXPECT_TYPE(10, TYPE_DEFAULT, 10, 10),
    BUSTER_FRAGMENT_EXPECT(11, ASSOCIATION), BUSTER_FRAGMENT_EXPECT(12, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(13, SUBTRACT),
    BUSTER_FRAGMENT_EXPECT(14, NUMBER), BUSTER_FRAGMENT_EXPECT_GROUP(15, GENERIC, 0),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_9[] = {
    BUSTER_FRAGMENT_EXPECT_TYPE(5, TYPE_NAME, 1, 5), BUSTER_FRAGMENT_EXPECT(7, NUMBER), BUSTER_FRAGMENT_EXPECT(8, SEPARATOR),
    BUSTER_FRAGMENT_EXPECT(9, NUMBER), BUSTER_FRAGMENT_EXPECT(10, SEPARATOR), BUSTER_FRAGMENT_EXPECT(11, NUMBER),
    BUSTER_FRAGMENT_EXPECT_GROUP(12, COMPOUND_LITERAL, 6), BUSTER_FRAGMENT_EXPECT(15, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT_GROUP(16, SUBSCRIPT, 14),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_10[] = {
    BUSTER_FRAGMENT_EXPECT(5, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(6, ADD), BUSTER_FRAGMENT_EXPECT(7, NUMBER),
    BUSTER_FRAGMENT_EXPECT(9, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(10, ADD), BUSTER_FRAGMENT_EXPECT(11, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT_TYPE(13, STATEMENT_EXPRESSION, 1, 13),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_11[] = {
    BUSTER_FRAGMENT_EXPECT(0, STRING_SEQUENCE), BUSTER_FRAGMENT_EXPECT(1, STRING_SEQUENCE), BUSTER_FRAGMENT_EXPECT(2, STRING),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_12[] = {
    BUSTER_FRAGMENT_EXPECT(2, NUMBER), BUSTER_FRAGMENT_EXPECT(3, SEPARATOR), BUSTER_FRAGMENT_EXPECT(4, IDENTIFIER),
    BUSTER_FRAGMENT_EXPECT(5, SEPARATOR), BUSTER_FRAGMENT_EXPECT(6, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(7, ADD),
    BUSTER_FRAGMENT_EXPECT(8, NUMBER), BUSTER_FRAGMENT_EXPECT_GROUP(9, BUILTIN, 0),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_13[] = {
    BUSTER_FRAGMENT_EXPECT_TYPE(2, TYPE_NAME, 2, 2), BUSTER_FRAGMENT_EXPECT(3, SEPARATOR),
    BUSTER_FRAGMENT_EXPECT_TYPE(4, TYPE_NAME, 4, 4), BUSTER_FRAGMENT_EXPECT_GROUP(5, BUILTIN, 0),
};
static const CFragmentCoverageFixtureNode c_fragment_coverage_fixture_14[] = {
    BUSTER_FRAGMENT_EXPECT(3, NUMBER), BUSTER_FRAGMENT_EXPECT(7, NUMBER), BUSTER_FRAGMENT_EXPECT(8, SEPARATOR),
    BUSTER_FRAGMENT_EXPECT(9, NUMBER), BUSTER_FRAGMENT_EXPECT(10, SEPARATOR), BUSTER_FRAGMENT_EXPECT(11, NUMBER),
    BUSTER_FRAGMENT_EXPECT_GROUP(12, INITIALIZER, 6), BUSTER_FRAGMENT_EXPECT(21, IDENTIFIER), BUSTER_FRAGMENT_EXPECT(23, NUMBER),
    BUSTER_FRAGMENT_EXPECT_GROUP(24, SUBSCRIPT, 22),
};

#undef BUSTER_FRAGMENT_EXPECT
#undef BUSTER_FRAGMENT_EXPECT_GROUP
#undef BUSTER_FRAGMENT_EXPECT_TYPE
#undef BUSTER_FRAGMENT_EXPECT_TERNARY

static bool c_fragment_coverage_fixture_matches(const CFragmentSink* sink, const CFragmentCoverageFixtureNode* expected, uint32_t length, uint32_t base)
{
    uint32_t emitted = 0;
    bool result = sink->expression_count != 0;
    for (uint32_t token = 0; token < sink->token_count; token += 1)
    {
        emitted += sink->syntax[token].emitted;
    }
    result &= emitted == length;
    for (uint32_t k = 0; result && k < length; k += 1)
    {
        const CFragmentCoverageFixtureNode* wanted = &expected[k];
        const uint64_t token = (uint64_t)base + wanted->token;
        result = token < sink->token_count;
        if (result)
        {
            const CFragmentSyntax* actual = &sink->syntax[token];
            const uint32_t type = wanted->type_root == UINT8_MAX ? BUSTER_C_FRAGMENT_NONE : base + wanted->type_root;
            const uint32_t auxiliary = wanted->auxiliary == UINT8_MAX ? BUSTER_C_FRAGMENT_NONE : base + wanted->auxiliary;
            result = actual->emitted && sink->kinds[token] == wanted->kind && actual->source == base + wanted->source &&
                     actual->type_root == type && actual->auxiliary == auxiliary;
        }
    }
    return result;
}

static bool c_fragment_test_coverage_fixture(const Parser* parser, const CFragmentSink* sink, int fixture)
{
    static const struct
    {
        const CFragmentCoverageFixtureNode* nodes;
        uint32_t count;
    } fixtures[] = {
#define BUSTER_FRAGMENT_FIXTURE(number) { c_fragment_coverage_fixture_##number, sizeof(c_fragment_coverage_fixture_##number) / sizeof(c_fragment_coverage_fixture_##number[0]) }
        BUSTER_FRAGMENT_FIXTURE(0),
        BUSTER_FRAGMENT_FIXTURE(1),
        BUSTER_FRAGMENT_FIXTURE(2),
        BUSTER_FRAGMENT_FIXTURE(3),
        BUSTER_FRAGMENT_FIXTURE(4),
        BUSTER_FRAGMENT_FIXTURE(5),
        BUSTER_FRAGMENT_FIXTURE(6),
        BUSTER_FRAGMENT_FIXTURE(7),
        BUSTER_FRAGMENT_FIXTURE(8),
        BUSTER_FRAGMENT_FIXTURE(9),
        BUSTER_FRAGMENT_FIXTURE(10),
        BUSTER_FRAGMENT_FIXTURE(11),
        BUSTER_FRAGMENT_FIXTURE(12),
        BUSTER_FRAGMENT_FIXTURE(13),
        BUSTER_FRAGMENT_FIXTURE(14),
#undef BUSTER_FRAGMENT_FIXTURE
    };
    bool result = true;
    if (fixture >= 0 && (uint32_t)fixture < sizeof(fixtures) / sizeof(fixtures[0]))
    {
        uint32_t base = fixture == 14 ? 0 : BUSTER_C_FRAGMENT_NONE;
        for (uint32_t k = 0; fixture != 14 && k < parser->tokens.count; k += 1)
        {
            if (parser->tokens.kinds[k] == PARSE_KIND_RETURN)
            {
                base = k + 1;
            }
        }
        const CFragmentCoverageFixtureNode* expected = fixtures[fixture].nodes;
        const uint32_t length = fixtures[fixture].count;
        result = base != BUSTER_C_FRAGMENT_NONE && c_fragment_coverage_fixture_matches(sink, expected, length, base);
        // Negative controls run on copies: an empty stream, an all-false emitted set, and each whole node omitted.
        // Keeping the real expression_count in the latter cases prevents that count alone from detecting the fault.
        CFragmentSyntax changed[128];
        result &= sink->token_count <= sizeof(changed) / sizeof(changed[0]);
        if (result)
        {
            CFragmentSink altered = *sink;
            altered.expression_count = 0;
            result = !c_fragment_coverage_fixture_matches(&altered, expected, length, base);
            altered.expression_count = sink->expression_count;
            altered.syntax = changed;
            memset(changed, 0, sizeof(changed));
            result &= !c_fragment_coverage_fixture_matches(&altered, expected, length, base);
            for (uint32_t k = 0; result && k < length; k += 1)
            {
                memcpy(changed, sink->syntax, (uint64_t)sink->token_count * sizeof(*changed));
                changed[base + expected[k].token].emitted = false;
                result = !c_fragment_coverage_fixture_matches(&altered, expected, length, base);
            }
        }
        if (!result)
        {
            fprintf(stderr, "fragment exact coverage fixture %d failed (base=%u expected_nodes=%u)\n", fixture, base, length);
        }
    }
    // Corpus/generated cases and newer fixtures still receive the full independent ordinary-tree coverage pass.
    return result;
}
