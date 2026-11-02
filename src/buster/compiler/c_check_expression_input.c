// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The coded walk (#1038): the vector path of the expressions' walk, which builds the expression graph from the tokens in
// postorder. The walk's action at a token (check_input_expression_act) tests the token's role, kind and columns through
// a chain of about 25 branches before it acts, and the scalar path takes it at every token, the oracle. The coded walk
// takes the class each token's visits byte holds (CheckInputExpressionCode, which check_input_expression_classes gives
// 64 tokens at a time from the role and kind tables), compacts the tokens the walk acts at into chunks in postorder with
// their classes, and acts at each by its class's own action: an identifier's leaf, a literal's, an operator's node, a list
// of values passed on, or nothing. The class's action tests what the class can't say (the values the children have, the
// columns the walk writes as it goes, the unit's rare features) and leaves every other case to the walk's visit
// (check_input_expression_visit), so that the graph, its leaves, the reports and the reader's columns are the walk's, node
// for node. Included by c_check_input.c after the walk's action.

// The postorder positions the coded walk compacts at a time: the tokens the walk acts at among them and their classes, in
// two arrays on the stack that stay in the first-level cache.
#define BUSTER_CHECK_EXPRESSION_CHUNK 1024u

// The tokens the walk acts at among postorder positions [first, end) into `tokens`, in postorder, and each one's visits
// byte into `classes`, but those whose byte is `skipped` (a class whose action is nothing, or a value no byte has);
// returns how many. `end - first` is at most BUSTER_CHECK_EXPRESSION_CHUNK, the arrays' room. The vector path gathers 16
// tokens' bytes from CheckInputReader.visits (four bytes at a token, which its padding allows) and compresses the tokens
// it keeps; the scalar path, its twin, takes a token at a time, writing each and counting the ones it keeps.
static uint32_t check_input_expression_compact(const CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t* tokens, uint32_t* classes,
                                               uint32_t skipped, bool scalar)
{
    const uint8_t* visits = reader->visits;
    const uint32_t* postorder = reader->postorder;
    // Gathers index by signed 32-bit offsets.
    const bool indexed = reader->parser->tokens.count <= (uint32_t)INT32_MAX;
    uint32_t result = 0;
    uint32_t position = first;
    for (; !BUSTER_SCALAR_PATH(scalar) && indexed && position < end; position += 16)
    {
        const uint32_t length = end - position < 16 ? end - position : 16;
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, length);
        const __m512i order = _mm512_maskz_loadu_epi32(lanes, postorder + position);
        const __m512i bytes = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, order, visits, 1);
        const __m512i visit = _mm512_and_si512(bytes, _mm512_set1_epi32(0xff));
        const __mmask16 acting = _mm512_mask_test_epi32_mask(lanes, visit, _mm512_set1_epi32(BUSTER_CHECK_EXPRESSION_ACTS)) &
                                 _mm512_cmpneq_epu32_mask(visit, _mm512_set1_epi32((int32_t)skipped));
        _mm512_mask_compressstoreu_epi32(tokens + result, acting, order);
        _mm512_mask_compressstoreu_epi32(classes + result, acting, visit);
        result += (uint32_t)__builtin_popcount(acting);
    }
    for (; position < end; position += 1)
    {
        const uint32_t token = postorder[position];
        const uint8_t visit = visits[token];
        // The compress store a token at a time, without a branch: every token is written at `result`, and the next one
        // writes over it unless counting keeps it. `result` never passes the positions read, so the room holds both.
        tokens[result] = token;
        classes[result] = visit;
        result += (visit & BUSTER_CHECK_EXPRESSION_ACTS) != 0 && visit != skipped;
    }
    return result;
}

typedef struct CheckInputValues CheckInputValues;
struct CheckInputValues
{
    uint32_t first;
    uint32_t last;
    uint32_t count;
};

// The values of `token`'s children, chained as the walk's action chains them: each child's chain after the one before
// it, through value_next, and their counts added (CheckInputReader.value_counts).
static inline CheckInputValues check_input_expression_values(CheckInputReader* reader, uint32_t token)
{
    CheckInputValues values = { .first = BUSTER_CHECK_NONE, .last = BUSTER_CHECK_NONE, .count = 0 };
    for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
    {
        const uint32_t begin = reader->first_value[child];
        if (begin != BUSTER_CHECK_NONE)
        {
            if (values.last != BUSTER_CHECK_NONE)
            {
                reader->value_next[values.last] = begin;
            }
            else
            {
                values.first = begin;
            }
            values.last = reader->last_value[child];
            values.count += reader->value_counts[child];
        }
    }
    return values;
}

// An operator's node, with what the walk's action gives every operator's: the type the token's query asked for, a static
// initializer's context, and in a unit with overload tables the token's overload head, which an address of an operand
// (ADDRESS) and __extension__ (SELECT) take from their operand's token and every other operator clears, so that a call
// finds its callee's set through them (the walk's action's last step).
static uint32_t check_input_expression_operator(CheckInputReader* reader, CheckOp op, uint32_t token, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t node = check_input_add_node(reader, op, token, a, b, c);
    if (node != BUSTER_CHECK_NONE)
    {
        CheckInput* input = reader->output;
        CheckGraph* graph = &input->graph;
        graph->types[node] = reader->query_types[token];
        graph->contexts[node] = reader->static_initializers[token] ? BUSTER_CHECK_CONTEXT_STATIC : 0;
        if (input->overload_heads != NULL)
        {
            const uint32_t count = reader->parser->tokens.count;
            const uint32_t operand = (op == CHECK_OP_ADDRESS || op == CHECK_OP_SELECT) && a != BUSTER_CHECK_NONE ? graph->tokens[a] : count;
            input->overload_heads[token] = operand < count ? input->overload_heads[operand] : 0;
        }
    }
    return node;
}

// The token's value is its node, where it made one.
static void check_input_expression_value(CheckInputReader* reader, uint32_t token, uint32_t node)
{
    if (node != BUSTER_CHECK_NONE)
    {
        reader->output->token_nodes[token] = node;
        reader->first_value[token] = node;
        reader->last_value[token] = node;
        reader->value_counts[token] = 1;
    }
}

// The token's values are its children's.
static void check_input_expression_list(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    reader->output->token_nodes[token] = values.count == 1 ? values.first : BUSTER_CHECK_NONE;
    reader->first_value[token] = values.first;
    reader->last_value[token] = values.last;
    reader->value_counts[token] = values.count;
    if (reader->static_initializers[token])
    {
        // A braced static initializer's elements are constant expressions, which initializer planning diagnoses:
        // the same constant evaluation its scalar form's INITIALIZE gets (check_input_expression_act's list).
        for (uint32_t value = values.first; value != BUSTER_CHECK_NONE; value = reader->value_next[value])
        {
            reader->output->graph.contexts[value] |= BUSTER_CHECK_CONTEXT_VALUE;
        }
    }
}

// A chunk's identifiers' leaves, made before its actions (#1038). An identifier's leaf is a function of columns at its
// token (its declared type, declaration and overload head) and at its declaration (kind, storage, flags, object,
// enumerator's value) and of its type's kind, which no action writes: the reader's run wrote them all before the walk.
// So the chunk's identifiers' leaves are made at once, by gathers whose loads don't wait for the walk's serial order, and
// each identifier's action writes its leaf from them, rather than load at its declaration as the walk reaches it.
typedef struct CheckInputIdentifiers CheckInputIdentifiers;
struct CheckInputIdentifiers
{
    // Per identifier of the chunk, in its order: the leaf's first two words (type and category; flags and constant kind),
    // its ninth (object and overload head), the declaration, whose enumerator's value an enumeration's leaf holds, and
    // the identifier's status (BUSTER_CHECK_IDENTIFIER_*).
    uint64_t types[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint64_t flags[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint64_t objects[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t declarations[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t status[BUSTER_CHECK_EXPRESSION_CHUNK];
};

// An identifier's status: its class's action takes it (not a K&R unit's, not a constexpr object's use), it makes a node
// (it has a declared type), and its declaration is an enumerator.
#define BUSTER_CHECK_IDENTIFIER_ACTED 0x01u
#define BUSTER_CHECK_IDENTIFIER_NODE 0x02u
#define BUSTER_CHECK_IDENTIFIER_ENUMERATOR 0x04u

_Static_assert(sizeof(CheckValue) == 96 && offsetof(CheckValue, category) == 4 && offsetof(CheckValue, flags) == 8 &&
               offsetof(CheckValue, constant_kind) == 12 && offsetof(CheckValue, integer) == 16 && offsetof(CheckValue, floating_storage) == 32 &&
               offsetof(CheckValue, imaginary_storage) == 48 && offsetof(CheckValue, object) == 64 && offsetof(CheckValue, overload_head) == 68 &&
               offsetof(CheckValue, offset) == 72 && offsetof(CheckValue, object_bit_offset) == 92 && sizeof(CheckCategory) == 4 &&
               sizeof(CheckConstantKind) == 4 && BUSTER_CHECK_ICE < 256 && BUSTER_CHECK_STATIC_OBJECT < 256 && BUSTER_CHECK_REGISTER < 256,
               "an identifier's leaf is written as twelve words, of which three vary but for an enumerator's value");

// The identifier `token`'s entry `at` in `identifiers`, as the walk's action makes its leaf: the scalar twin of
// check_input_expression_identifiers' vector path.
static void check_input_identifier_entry(const CheckInputReader* reader, uint32_t token, CheckInputIdentifiers* identifiers, uint32_t at)
{
    const CheckInput* input = reader->output;
    const TypeId type = reader->declared_types[token];
    const uint32_t declaration = type != 0 ? input->declarations[token] : 0;
    const bool acted = input->kr_parameter_count == 0 && (type == 0 || (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_CONSTEXPR) == 0);
    const bool enumeration = type != 0 && reader->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
    const uint64_t category = enumeration ? CHECK_RVALUE : type != 0 && type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
    const uint64_t flags = (enumeration ? BUSTER_CHECK_ICE : 0) |
                           (type != 0 && reader->static_objects[declaration] && !input->thread_local[declaration] ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                           (type != 0 && reader->registers[declaration] ? BUSTER_CHECK_REGISTER : 0);
    const uint64_t kind = enumeration ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS;
    const uint64_t head = input->overload_heads != NULL ? input->overload_heads[token] : 0;
    identifiers->types[at] = (uint64_t)type | category << 32;
    identifiers->flags[at] = flags | kind << 32;
    identifiers->objects[at] = (type != 0 ? reader->objects[declaration] : 0) | head << 32;
    identifiers->declarations[at] = declaration;
    identifiers->status[at] = (acted ? BUSTER_CHECK_IDENTIFIER_ACTED : 0) | (acted && type != 0 ? BUSTER_CHECK_IDENTIFIER_NODE : 0) |
                              (enumeration ? BUSTER_CHECK_IDENTIFIER_ENUMERATOR : 0);
}

// The bytes of `column` at the 16 `indices` in `lanes`, a dword each: the aligned dword that holds each byte, which never
// crosses a page the column's bytes aren't on (the column's start rounded down to four bytes), shifted down to the byte.
static inline __m512i check_input_gather_bytes(__mmask16 lanes, __m512i indices, const uint8_t* column)
{
    const uintptr_t start = (uintptr_t)column & ~(uintptr_t)3;
    const __m512i offsets = _mm512_add_epi32(indices, _mm512_set1_epi32((int32_t)((uintptr_t)column - start)));
    const __m512i aligned = _mm512_andnot_si512(_mm512_set1_epi32(3), offsets);
    const __m512i shifts = _mm512_slli_epi32(_mm512_and_si512(offsets, _mm512_set1_epi32(3)), 3);
    const __m512i dwords = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, aligned, (const void*)start, 1);
    return _mm512_and_si512(_mm512_srlv_epi32(dwords, shifts), _mm512_set1_epi32(0xff));
}

// The entries of the chunk's identifiers, the `acting` tokens and classes whose class is CHECK_INPUT_CODE_IDENTIFIER, in
// their order into `identifiers`; returns how many. The vector path compresses the identifiers 16 tokens at a time, then
// gathers 16 identifiers' columns at once: their types and declarations, then their declarations' columns and their
// types' kinds; the scalar path, its twin, makes each entry by check_input_identifier_entry.
static uint32_t check_input_expression_identifiers(const CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes, uint32_t acting,
                                                   CheckInputIdentifiers* identifiers, bool scalar)
{
    const CheckInput* input = reader->output;
    const uint32_t identifier = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_IDENTIFIER << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    // Gathers index by signed 32-bit offsets: the tokens' and the types' (the compaction checked the tokens').
    const bool indexed = reader->parser->tokens.count <= (uint32_t)INT32_MAX - 4 && reader->types->types.count <= (uint32_t)INT32_MAX - 4;
    uint32_t found[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t count = 0;
    uint32_t position = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && indexed && position < acting; position += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, acting - position < 16 ? acting - position : 16);
        const __mmask16 matched = _mm512_mask_cmpeq_epi32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, classes + position), _mm512_set1_epi32((int32_t)identifier));
        _mm512_mask_compressstoreu_epi32(found + count, matched, _mm512_maskz_loadu_epi32(lanes, tokens + position));
        count += (uint32_t)__builtin_popcount(matched);
    }
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, count - at < 16 ? count - at : 16);
        const __m512i zero = _mm512_setzero_si512();
        const __m512i token = _mm512_maskz_loadu_epi32(lanes, found + at);
        const __m512i type = _mm512_mask_i32gather_epi32(zero, lanes, token, reader->declared_types, 4);
        const __mmask16 typed = _mm512_mask_test_epi32_mask(lanes, type, type);
        const __m512i declaration = _mm512_mask_i32gather_epi32(zero, typed, token, input->declarations, 4);
        const __m512i kinds = check_input_gather_bytes(typed, declaration, reader->declaration_kinds);
        const __m512i statics = check_input_gather_bytes(typed, declaration, reader->static_objects);
        const __m512i locals = check_input_gather_bytes(typed, declaration, input->thread_local);
        const __m512i registers = check_input_gather_bytes(typed, declaration, reader->registers);
        const __m512i declared = check_input_gather_bytes(typed, declaration, reader->declaration_flags);
        const __m512i type_kinds = check_input_gather_bytes(typed, _mm512_srli_epi32(type, BUSTER_TYPE_INDEX_SHIFT), reader->types->types.kind);
        const __m512i objects = _mm512_mask_i32gather_epi32(zero, typed, declaration, reader->objects, 4);
        const __m512i heads = input->overload_heads != NULL ? _mm512_mask_i32gather_epi32(zero, lanes, token, input->overload_heads, 4) : zero;
        const __mmask16 enumeration = _mm512_mask_cmpeq_epi32_mask(typed, kinds, _mm512_set1_epi32(C_BINDING_ENUMERATOR));
        const __mmask16 function = _mm512_mask_cmpeq_epi32_mask(typed, type_kinds, _mm512_set1_epi32(BUSTER_TYPE_KIND_FUNCTION));
        const __mmask16 constexprs = _mm512_mask_test_epi32_mask(typed, declared, _mm512_set1_epi32(BUSTER_CHECK_DECLARATION_CONSTEXPR));
        const __mmask16 acted = input->kr_parameter_count == 0 ? (__mmask16)(lanes & ~constexprs) : 0;
        __m512i category = _mm512_mask_mov_epi32(_mm512_set1_epi32(CHECK_LVALUE), function, _mm512_set1_epi32(CHECK_FUNCTION));
        category = _mm512_mask_mov_epi32(category, enumeration, _mm512_set1_epi32(CHECK_RVALUE));
        const __mmask16 static_object = _mm512_mask_test_epi32_mask(typed, statics, statics) & _mm512_mask_testn_epi32_mask(typed, locals, locals);
        const __mmask16 in_register = _mm512_mask_test_epi32_mask(typed, registers, registers);
        const __m512i flags = _mm512_or_si512(_mm512_or_si512(_mm512_maskz_mov_epi32(enumeration, _mm512_set1_epi32(BUSTER_CHECK_ICE)),
                                                              _mm512_maskz_mov_epi32(static_object, _mm512_set1_epi32(BUSTER_CHECK_STATIC_OBJECT))),
                                              _mm512_maskz_mov_epi32(in_register, _mm512_set1_epi32(BUSTER_CHECK_REGISTER)));
        const __m512i constant = _mm512_mask_mov_epi32(_mm512_set1_epi32(CHECK_CONSTANT_ADDRESS), enumeration, _mm512_set1_epi32(CHECK_CONSTANT_INTEGER));
        const __m512i status = _mm512_or_si512(_mm512_or_si512(_mm512_maskz_mov_epi32(acted, _mm512_set1_epi32(BUSTER_CHECK_IDENTIFIER_ACTED)),
                                                               _mm512_maskz_mov_epi32(acted & typed, _mm512_set1_epi32(BUSTER_CHECK_IDENTIFIER_NODE))),
                                               _mm512_maskz_mov_epi32(enumeration, _mm512_set1_epi32(BUSTER_CHECK_IDENTIFIER_ENUMERATOR)));
        // The 64-bit words, the low lanes' then the high lanes': each a low dword and a high one.
        const __m512i pairs[3][2] =
        {
            { type, category },
            { flags, constant },
            { objects, heads },
        };
        uint64_t* words[3] =
        {
            identifiers->types + at,
            identifiers->flags + at,
            identifiers->objects + at,
        };
        for (uint32_t w = 0; w < 3; w += 1)
        {
            const __m512i low = _mm512_or_si512(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(pairs[w][0])),
                                                _mm512_slli_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(pairs[w][1])), 32));
            const __m512i high = _mm512_or_si512(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(pairs[w][0], 1)),
                                                 _mm512_slli_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(pairs[w][1], 1)), 32));
            _mm512_mask_storeu_epi64(words[w], (__mmask8)lanes, low);
            _mm512_mask_storeu_epi64(words[w] + 8, (__mmask8)(lanes >> 8), high);
        }
        _mm512_mask_storeu_epi32(identifiers->declarations + at, lanes, declaration);
        _mm512_mask_storeu_epi32(identifiers->status + at, lanes, status);
    }
    for (; position < acting; position += 1)
    {
        if (classes[position] == identifier)
        {
            check_input_identifier_entry(reader, tokens[position], identifiers, count);
            count += 1;
        }
    }
    return count;
}

// An identifier operand, the chunk's identifier `at`: a VALUE node of its declaration and its leaf, or nothing for one
// without a declared type. In a unit with overload tables the leaf holds the use's overload head, which the reader's run
// wrote at the token, and which the walk's action writes back there unchanged. A K&R unit's identifiers, whose types a
// K&R function's designator interns, and a constexpr object's, which reads its value, are the walk's.
static bool check_input_expression_identifier(CheckInputReader* reader, uint32_t token, const CheckInputIdentifiers* identifiers, uint32_t at)
{
    const uint32_t status = identifiers->status[at];
    if ((status & BUSTER_CHECK_IDENTIFIER_NODE) != 0)
    {
        const uint32_t node = check_input_add_node(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            const unsigned __int128 integer = (status & BUSTER_CHECK_IDENTIFIER_ENUMERATOR) != 0 ? reader->enum_values[identifiers->declarations[at]] : 0;
            // The twelve words, all else zero (no x87 store of the zero floating parts).
            const uint64_t words[12] =
            {
                identifiers->types[at],
                identifiers->flags[at],
                (uint64_t)integer,
                (uint64_t)(integer >> 64),
                0,
                0,
                0,
                0,
                identifiers->objects[at],
                0,
                0,
                0,
            };
            memcpy(check_graph_leaf_of(&reader->output->graph, node), words, sizeof(words));
            check_input_expression_value(reader, token, node);
        }
    }
    return (status & BUSTER_CHECK_IDENTIFIER_ACTED) != 0;
}

// A number or a character operand: its literal's VALUE node, or the report of an invalid one; in a unit with overload
// tables, its token's head is its leaf's, none. A function's name is the walk's, as its tests take it after the literals.
static bool check_input_expression_literal(CheckInputReader* reader, uint32_t token)
{
    const bool acted = !check_input_function_name_token(reader->parser, token);
    if (acted)
    {
        CheckLiteral literal;
        check_input_literal(reader, token, &literal);
        if (!literal.valid)
        {
            check_input_report(reader, token, check_input_literal_report(&literal));
        }
        else
        {
            const uint32_t node = check_input_add_node(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            if (node != BUSTER_CHECK_NONE)
            {
                CheckValue* leaf = check_graph_leaf_of(&reader->output->graph, node);
                *leaf = check_input_literal_value(reader, token, literal);
                if (reader->output->overload_heads != NULL)
                {
                    reader->output->overload_heads[token] = leaf->overload_head;
                }
                check_input_expression_value(reader, token, node);
            }
        }
    }
    return acted;
}

// A prefix operator of one operand, but the address of a label, sizeof and alignof, and __real__ and __imag__.
static bool check_input_expression_prefix(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    CheckOp op = CHECK_OP_COUNT;
    switch (reader->parser->tokens.kinds[token])
    {
        case PARSE_KIND_PLUS: op = CHECK_OP_POSITIVE; break;
        case PARSE_KIND_EXTENSION: op = CHECK_OP_SELECT; break;
        case PARSE_KIND_MINUS: op = CHECK_OP_NEGATIVE; break;
        case PARSE_KIND_EXCLAIM: op = CHECK_OP_NOT; break;
        case PARSE_KIND_TILDE: op = CHECK_OP_BIT_NOT; break;
        case PARSE_KIND_AMP: op = CHECK_OP_ADDRESS; break;
        case PARSE_KIND_STAR: op = CHECK_OP_DEREFERENCE; break;
        case PARSE_KIND_PLUSPLUS: op = CHECK_OP_PRE_INCREMENT; break;
        case PARSE_KIND_MINUSMINUS: op = CHECK_OP_PRE_DECREMENT; break;
        default: break;
    }
    const bool acted = op != CHECK_OP_COUNT && values.count == 1;
    if (acted)
    {
        check_input_expression_value(reader, token, check_input_expression_operator(reader, op, token, values.first, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE));
    }
    return acted;
}

// A binary operator, or a comma operator: two values. An initializer's assignment is the walk's class.
static bool check_input_expression_binary(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    const CheckOp op = check_input_binary(reader->parser->tokens.kinds[token]);
    const bool acted = op != CHECK_OP_COUNT && values.count == 2;
    if (acted)
    {
        const uint32_t second = reader->value_next[values.first];
        check_input_expression_value(reader, token, check_input_expression_operator(reader, op, token, values.first, second, BUSTER_CHECK_NONE));
    }
    return acted;
}

// A conditional operator of three values, or of two with the middle one omitted (x ?: y), whose first is both.
static bool check_input_expression_ternary(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    const bool acted = values.count == 3 || values.count == 2;
    if (acted)
    {
        const bool omitted = values.count == 2;
        const uint32_t second = reader->value_next[values.first];
        const uint32_t third = omitted ? BUSTER_CHECK_NONE : reader->value_next[second];
        check_input_expression_value(reader, token, check_input_expression_operator(reader, CHECK_OP_CONDITIONAL, token, values.first, omitted ? values.first : second,
                                                                                    omitted ? second : third));
    }
    return acted;
}

// A member access: one value and the member's name after the operator, a vector's member for `.`.
static bool check_input_expression_member(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    const Parser* parser = reader->parser;
    const bool acted = values.count == 1 && token + 1 < parser->tokens.count && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER;
    if (acted)
    {
        const bool arrow = parser->tokens.kinds[token] == PARSE_KIND_ARROW;
        const uint32_t node = check_input_expression_operator(reader, arrow ? CHECK_OP_ARROW : CHECK_OP_MEMBER, token, values.first, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            reader->output->graph.members[node] = parser->tokens.names[token + 1];
            if (!arrow)
            {
                check_input_vector_member(reader, node, token + 1);
            }
        }
        check_input_expression_value(reader, token, node);
    }
    return acted;
}

// An operator of `values.count` values (one, or two for a subscript), where the class asks for `wanted`.
static bool check_input_expression_fixed(CheckInputReader* reader, uint32_t token, CheckInputValues values, CheckOp op, uint32_t wanted)
{
    const bool acted = values.count == wanted;
    if (acted)
    {
        const uint32_t second = wanted == 2 ? reader->value_next[values.first] : BUSTER_CHECK_NONE;
        check_input_expression_value(reader, token, check_input_expression_operator(reader, op, token, values.first, second, BUSTER_CHECK_NONE));
    }
    return acted;
}

// A string literal operand: a VALUE node of its string's object (check_input_string, which makes the object the first time
// a string is met), or nothing where it has none; in a unit with overload tables, its token's head is its leaf's, none.
static bool check_input_expression_string(CheckInputReader* reader, uint32_t token)
{
    const CheckInitString* string = check_input_string(reader, token);
    if (string != 0)
    {
        const uint32_t node = check_input_add_node(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            CheckValue* leaf = check_graph_leaf_of(&reader->output->graph, node);
            *leaf = (CheckValue)
            {
                .type = string->type,
                .category = CHECK_LVALUE,
                .flags = BUSTER_CHECK_STATIC_OBJECT,
                .constant_kind = CHECK_CONSTANT_ADDRESS,
                .object = string->object,
            };
            if (reader->output->overload_heads != NULL)
            {
                reader->output->overload_heads[token] = leaf->overload_head;
            }
            check_input_expression_value(reader, token, node);
        }
    }
    return true;
}

// A return statement: its one value converted to the function's return type (a CONVERT node, the token's node but no value
// of an operand's), or without one a report where the function's type isn't void, but in a system header, as clang
// warns (-Wreturn-mismatch). A return outside a function's body, or of more values, is the walk's.
static bool check_input_expression_return(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    const TypeId type = reader->return_types[token];
    const bool acted = type != 0 && values.count <= 1;
    if (acted && values.count == 1)
    {
        reader->query_types[token] = type;
        const uint32_t node = check_input_expression_operator(reader, CHECK_OP_CONVERT, token, values.first, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            reader->output->token_nodes[token] = node;
        }
    }
    else if (acted && check_action_kind(reader->types, type) != BUSTER_TYPE_KIND_VOID && !check_graph_system_token(&reader->output->graph, token))
    {
        check_input_report(reader, token, CHECK_INPUT_RETURN);
    }
    return acted;
}

// A statement's terminator, or a for's closing parenthesis: each of its values discarded, a CAST to void, or a CONDITION
// in a condition's context, which may hold one value at most. A unit with statement expressions, whose last statements'
// values are their results and whose discards the regions chain, is the walk's.
static bool check_input_expression_terminator(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    const bool condition = reader->conditions[token] != 0;
    const bool acted = reader->output->region_count == 0 && (!condition || values.count <= 1);
    for (uint32_t value = acted ? values.first : BUSTER_CHECK_NONE; value != BUSTER_CHECK_NONE; value = reader->value_next[value])
    {
        const uint32_t discard = check_input_add_node(reader, condition ? CHECK_OP_CONDITION : CHECK_OP_CAST, token, value, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (discard != BUSTER_CHECK_NONE)
        {
            reader->output->graph.types[discard] = BUSTER_TYPE_VOID;
        }
    }
    return acted;
}

// An if's, a while's or a switch's condition: its one value tested (CONDITION), or a switch's promoted (POSITIVE). The
// condition's node is the token's, but no value of an operand's.
static bool check_input_expression_condition(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    const Parser* parser = reader->parser;
    const bool acted = values.count == 1;
    if (acted)
    {
        const uint32_t keyword = reader->first_child[token];
        const bool switched = keyword < parser->tokens.count && parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH;
        const uint32_t second = switched ? reader->value_next[values.first] : BUSTER_CHECK_NONE;
        const uint32_t node = check_input_expression_operator(reader, switched ? CHECK_OP_POSITIVE : CHECK_OP_CONDITION, token, values.first, second, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            reader->output->token_nodes[token] = node;
        }
    }
    return acted;
}

// A call of a function: the callee's value its operand, and the arguments' values its own slice of argument_nodes, each
// with its first token; a call of a plain library builtin's declaration that clang folds marked in its members
// (check_input_library_call, #148); and in a unit with overload tables, a call whose callee's token names an overload set
// recorded with the set's head (CheckGraph.overload_calls), in node order as the walk records them. A K&R unit's calls are
// the walk's, and so are a builtin's and an intrinsic's, by their class (check_input_expression_fold).
static bool check_input_expression_call(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    CheckInput* input = reader->output;
    const bool acted = values.count != 0 && input->kr_parameter_count == 0;
    if (acted)
    {
        CheckGraph* graph = &input->graph;
        const uint32_t callee = values.first;
        const uint32_t first_argument = reader->value_next[callee];
        const uint32_t node = check_input_expression_operator(reader, CHECK_OP_CALL, token, callee, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            if (graph->ops[callee] == CHECK_OP_VALUE || graph->ops[callee] == CHECK_OP_ADDRESS)
            {
                const uint32_t designator = graph->ops[callee] == CHECK_OP_ADDRESS ? graph->operand0[callee] : callee;
                const uint32_t use = designator < node && graph->ops[designator] == CHECK_OP_VALUE ? graph->tokens[designator] : BUSTER_CHECK_NONE;
                const uint32_t library = use != BUSTER_CHECK_NONE ? check_input_library_call(reader, use) : 0;
                graph->members[node] = library != 0 ? library : graph->members[node];
            }
            const uint32_t callee_token = input->overload_heads != NULL ? graph->tokens[callee] : reader->parser->tokens.count;
            const uint32_t head = callee_token < reader->parser->tokens.count ? input->overload_heads[callee_token] : 0;
            if (head != 0)
            {
                if (graph->overload_count < graph->overload_capacity)
                {
                    graph->overload_calls[graph->overload_count++] = (CheckOverloadCall){ .node = node, .head = head - 1 };
                }
                else
                {
                    input->resource = true;
                }
            }
            graph->argument_offsets[node] = graph->argument_count;
            graph->argument_counts[node] = values.count - 1;
            for (uint32_t argument = first_argument; argument != BUSTER_CHECK_NONE; argument = reader->value_next[argument])
            {
                if (graph->argument_count < graph->argument_capacity)
                {
                    graph->argument_tokens[graph->argument_count] = check_input_first_token(reader, graph->tokens[argument]);
                    graph->argument_nodes[graph->argument_count++] = argument;
                }
                else
                {
                    input->resource = true;
                }
            }
        }
        check_input_expression_value(reader, token, node);
    }
    return acted;
}

// The coded walk's action at `token` of visits byte `classes`, by its class where `coded` (the unit has none of the
// features every class leaves to the walk), else and where the class's action doesn't take it, the walk's visit. A
// class's action that leaves the token has written only its first value, NONE, and its children's chains (value_next),
// both of which the walk's visit writes again as they were.
static void check_input_expression_coded(CheckInputReader* reader, uint32_t token, uint8_t classes, bool coded, const CheckInputIdentifiers* identifiers,
                                         uint32_t* identifier)
{
    const uint32_t code = (classes & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    bool acted = coded && code != CHECK_INPUT_CODE_SCALAR;
    if (acted)
    {
        reader->first_value[token] = BUSTER_CHECK_NONE;
        const CheckInputValues values = check_input_expression_values(reader, token);
        switch (code)
        {
            case CHECK_INPUT_CODE_LIST:
                check_input_expression_list(reader, token, values);
                break;
            case CHECK_INPUT_CODE_DROP:
                break;
            case CHECK_INPUT_CODE_IDENTIFIER:
                acted = check_input_expression_identifier(reader, token, identifiers, *identifier);
                *identifier += 1;
                break;
            case CHECK_INPUT_CODE_LITERAL:
                acted = check_input_expression_literal(reader, token);
                break;
            case CHECK_INPUT_CODE_PREFIX:
                acted = check_input_expression_prefix(reader, token, values);
                break;
            case CHECK_INPUT_CODE_BINARY:
                acted = check_input_expression_binary(reader, token, values);
                break;
            case CHECK_INPUT_CODE_SEPARATOR:
                check_input_expression_list(reader, token, values);
                break;
            case CHECK_INPUT_CODE_GROUP:
                acted = values.count == 1;
                if (acted)
                {
                    check_input_expression_list(reader, token, values);
                }
                break;
            case CHECK_INPUT_CODE_CAST:
                acted = reader->query_types[token] != 0 && check_input_expression_fixed(reader, token, values, CHECK_OP_CAST, 1);
                break;
            case CHECK_INPUT_CODE_TERNARY:
                acted = check_input_expression_ternary(reader, token, values);
                break;
            case CHECK_INPUT_CODE_SUBSCRIPT:
                acted = check_input_expression_fixed(reader, token, values, CHECK_OP_SUBSCRIPT, 2);
                break;
            case CHECK_INPUT_CODE_MEMBER:
                acted = check_input_expression_member(reader, token, values);
                break;
            case CHECK_INPUT_CODE_TERMINATOR:
                acted = check_input_expression_terminator(reader, token, values);
                break;
            case CHECK_INPUT_CODE_CONDITION:
                acted = check_input_expression_condition(reader, token, values);
                break;
            case CHECK_INPUT_CODE_CALL:
                acted = check_input_expression_call(reader, token, values);
                break;
            case CHECK_INPUT_CODE_STRING:
                acted = check_input_expression_string(reader, token);
                break;
            case CHECK_INPUT_CODE_RETURN:
                acted = check_input_expression_return(reader, token, values);
                break;
            case CHECK_INPUT_CODE_POSTFIX:
            {
                const uint8_t kind = reader->parser->tokens.kinds[token];
                acted = (kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS) &&
                        check_input_expression_fixed(reader, token, values, kind == PARSE_KIND_PLUSPLUS ? CHECK_OP_POST_INCREMENT : CHECK_OP_POST_DECREMENT, 1);
                break;
            }
            default:
                acted = false;
                break;
        }
    }
    // Counted in units the coded walk takes alone: in another, the walk's visit takes every token.
    reader->output->expression_acted[code] += acted;
    reader->output->expression_left[code] += coded && !acted;
    if (!acted)
    {
        check_input_expression_visit(reader, token, classes);
    }
}

// The coded walk: the postorder a chunk at a time, compacted to the tokens the walk acts at, each acted at by its class.
// A DROP's action is nothing but its children's chains joined, which no parent reads (its first value is NONE, as the
// reader's setup wrote it), so the compaction leaves its tokens out. A unit with expressions past its limit or deferred
// is acted at by the walk's visit throughout, though still compacted. A unit with overload tables (CheckInput.
// overload_heads: one that names the overloadable attribute in an attribute list, whose every function declaration is a
// candidate) is coded too: the class actions carry the heads through their nodes to their calls as the walk's does.
static void check_input_expressions_coded(CheckInputReader* reader)
{
    const CheckInput* input = reader->output;
    const uint32_t count = reader->parser->tokens.count;
    // Every token's expression ends by the count, so that within these limits none is past them.
    const bool coded = input->diagnostic_limits == NULL && input->deferred_tokens == NULL && reader->expression_limit >= count && input->token_count >= count;
    const uint32_t dropped = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_DROP << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    uint32_t tokens[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t classes[BUSTER_CHECK_EXPRESSION_CHUNK];
    CheckInputIdentifiers identifiers;
    for (uint32_t first = 0; first < count && !reader->output->resource; first += BUSTER_CHECK_EXPRESSION_CHUNK)
    {
        const uint32_t end = count - first < BUSTER_CHECK_EXPRESSION_CHUNK ? count : first + BUSTER_CHECK_EXPRESSION_CHUNK;
        const uint32_t acting = check_input_expression_compact(reader, first, end, tokens, classes, coded ? dropped : UINT32_MAX, false);
        if (coded)
        {
            check_input_expression_identifiers(reader, tokens, classes, acting, &identifiers, false);
        }
        uint32_t identifier = 0;
        for (uint32_t i = 0; i < acting && !reader->output->resource; i += 1)
        {
            check_input_expression_coded(reader, tokens[i], (uint8_t)classes[i], coded, &identifiers, &identifier);
        }
    }
}

// The tokens the coded walk acted at by their classes' own actions: CheckInput.expression_acted's sum over the classes.
static __attribute__((unused)) uint32_t check_input_expression_classed(const CheckInput* input)
{
    uint32_t total = 0;
    for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
    {
        total += input->expression_acted[code];
    }
    return total;
}

// The tokens the coded walk's class actions declined and left to the walk's visit: CheckInput.expression_left's sum over
// the classes but CHECK_INPUT_CODE_SCALAR, whose tokens no class's action takes.
static __attribute__((unused)) uint32_t check_input_expression_declined(const CheckInput* input)
{
    uint32_t total = 0;
    for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
    {
        total += code != CHECK_INPUT_CODE_SCALAR ? input->expression_left[code] : 0;
    }
    return total;
}
