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
// Always inlined (#880), as profile-guided builds do; hot caller: the expression reader, once per operator.
static inline __attribute__((always_inline)) uint32_t check_input_expression_operator(CheckInputReader* reader, CheckOp op, uint32_t token, uint32_t a, uint32_t b, uint32_t c)
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
                           (type != 0 && check_input_bit(reader->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) && !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL) ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                           (type != 0 && check_input_bit(reader->declaration_bits, declaration, BUSTER_CHECK_BITS_REGISTER) ? BUSTER_CHECK_REGISTER : 0);
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
        const __m512i bits = check_input_gather_bytes(typed, declaration, reader->declaration_bits);
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
        const __mmask16 static_object = _mm512_mask_test_epi32_mask(typed, bits, _mm512_set1_epi32(BUSTER_CHECK_BITS_STATIC)) &
                                        _mm512_mask_testn_epi32_mask(typed, bits, _mm512_set1_epi32(BUSTER_CHECK_BITS_THREAD_LOCAL));
        const __mmask16 in_register = _mm512_mask_test_epi32_mask(typed, bits, _mm512_set1_epi32(BUSTER_CHECK_BITS_REGISTER));
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

// A chunk's number literals' values, found before its actions (#1038). A literal whose spelling is a plain decimal, one
// to nine digits without a leading zero but for 0 itself, is an int of that value: what check_input_literal makes of it
// (check_literal_integer's first case), its spelling its source's bytes, among which no splice, trigraph, digit
// separator or suffix can be; and so is an integer the preprocessor made (BUSTER_PP_KIND_INTEGER: __LINE__'s, say) up to
// INT_MAX, spelled in decimal. The chunk's literals are read at once, their spellings' bytes by gathers of the aligned
// words that hold them, and each literal's action takes its value from here rather than spell and parse it as the walk
// reaches it. It parses every other literal (check_input_literal).
typedef struct CheckInputLiterals CheckInputLiterals;
struct CheckInputLiterals
{
    // Per literal of the chunk, in its order: its value, or BUSTER_CHECK_NONE for a literal of another form.
    uint32_t values[BUSTER_CHECK_EXPRESSION_CHUNK];
};

// The most digits a plain decimal has here: nine, whose largest value is an int.
#define BUSTER_CHECK_LITERAL_PLAIN_DIGITS 9u

// The range of `pp`'s source that holds `location`: `cursor`'s if it does, else the one pp_find_range finds. A chunk's
// literals, in source order but for macros' bodies, are mostly in the range of the one before.
static uint32_t check_input_literal_range(const Preprocessor* pp, uint32_t location, uint32_t cursor)
{
    const bool held = cursor < pp->range_count && location - pp->ranges[cursor].base < pp->ranges[cursor].length;
    return held ? cursor : pp_find_range(pp, location);
}

// The literal `token`'s value where its spelling is a plain decimal or the preprocessor's int (CheckInputLiterals), else
// BUSTER_CHECK_NONE, with `*cursor` the range of its spelling's source after: the scalar twin of
// check_input_expression_literals' vector path.
static uint32_t check_input_literal_plain(const CheckInputReader* reader, uint32_t token, uint32_t* cursor)
{
    const Parser* parser = reader->parser;
    const Preprocessor* pp = reader->pp;
    const uint32_t source = parser->tokens.sources[token];
    const uint8_t kind = pp->output.kinds[source];
    const uint32_t length = pp->output.values[source] - pp->output.locations[source];
    const bool unnamed = parser->tokens.names[token] == 0;
    const bool number = unnamed && kind == C_TOKEN_NUMERIC_CONSTANT && length - 1 < BUSTER_CHECK_LITERAL_PLAIN_DIGITS;
    const uint32_t range = number ? check_input_literal_range(pp, pp->output.locations[source], *cursor) : BUSTER_PP_NONE;
    *cursor = range < pp->range_count ? range : *cursor;
    const uint8_t* bytes = range < pp->range_count ? pp->ranges[range].bytes + (pp->output.locations[source] - pp->ranges[range].base) : NULL;
    bool plain = bytes != NULL && (bytes[0] != '0' || length == 1);
    uint32_t value = 0;
    for (uint32_t i = 0; plain && i < length; i += 1)
    {
        const uint32_t digit = (uint32_t)bytes[i] - '0';
        plain = digit < 10;
        value = value * 10 + digit;
    }
    const bool integer = unnamed && kind == BUSTER_PP_KIND_INTEGER && pp->output.values[source] <= (uint32_t)INT32_MAX;
    return integer ? pp->output.values[source] : plain ? value : BUSTER_CHECK_NONE;
}

// The spellings of four tokens of 1 to 9 digits, one a 128-bit lane: each lane's two words hold its token's bytes from
// its byte `offsets`, for `lengths` bytes (both the lane's in each of its dwords). Returns each spelling's value in its
// lane's first dword, and in `*bad` the bytes of the lanes whose spellings are no plain decimal: a byte no digit, or a
// leading zero of more than one digit. One shuffle right-aligns each spelling; its digits are multiplied out in pairs,
// then quads, then the lane's four quads (of which the first is a 9-digit spelling's zero).
static inline __m512i check_input_literal_digits(__m512i words, __m512i offsets, __m512i lengths, uint64_t* bad)
{
    const __m512i iota = _mm512_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
                                         15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i splat = _mm512_set1_epi32(0x01010101);
    // In every byte of its lane: where the spelling's first digit goes (16 - length), the shift from its bytes' place to
    // there (offset + length - 16, from -15 to 0), and its length.
    const __m512i first = _mm512_mullo_epi32(_mm512_sub_epi32(_mm512_set1_epi32(16), lengths), splat);
    const __m512i shift = _mm512_mullo_epi32(_mm512_and_si512(_mm512_sub_epi32(_mm512_add_epi32(offsets, lengths), _mm512_set1_epi32(16)), _mm512_set1_epi32(0xff)),
                                             splat);
    const __m512i length = _mm512_mullo_epi32(lengths, splat);
    const __mmask64 before = _mm512_cmplt_epu8_mask(iota, first);
    const __m512i control = _mm512_mask_mov_epi8(_mm512_add_epi8(iota, shift), before, _mm512_set1_epi8((char)0x80));
    const __m512i aligned = _mm512_shuffle_epi8(words, control);
    const __mmask64 spelled = ~before;
    const __m512i digits = _mm512_mask_sub_epi8(_mm512_setzero_si512(), spelled, aligned, _mm512_set1_epi8('0'));
    const __mmask64 leading = _mm512_cmpeq_epi8_mask(iota, first) & _mm512_cmpgt_epu8_mask(length, _mm512_set1_epi8(1));
    *bad = _mm512_mask_cmpgt_epu8_mask(spelled, digits, _mm512_set1_epi8(9)) | _mm512_mask_cmpeq_epi8_mask(leading, aligned, _mm512_set1_epi8('0'));
    const __m512i pairs = _mm512_maddubs_epi16(digits, _mm512_set1_epi16(0x010a));
    const __m512i quads = _mm512_madd_epi16(pairs, _mm512_set1_epi32(0x00010064));
    const __m512i scaled = _mm512_mullo_epi32(quads, _mm512_set_epi32(1, 10000, 100000000, 0, 1, 10000, 100000000, 0, 1, 10000, 100000000, 0, 1, 10000, 100000000, 0));
    const __m512i halves = _mm512_add_epi32(scaled, _mm512_shuffle_epi32(scaled, _MM_SHUFFLE(1, 0, 3, 2)));
    return _mm512_add_epi32(halves, _mm512_shuffle_epi32(halves, _MM_SHUFFLE(2, 3, 0, 1)));
}

// The entries of the chunk's literals, the `acting` tokens and classes whose class is CHECK_INPUT_CODE_LITERAL, in their
// order into `literals`; returns how many. The vector path compresses the literals 16 tokens at a time, then reads them in
// stages, each over all of the chunk's literals 16 at a time, so that the loads of one stage, which mostly miss the
// cache (a chunk's literals are far apart in the preprocessor's columns and the source), wait together rather than in
// one chain a literal: their names and sources; the sources' kinds, locations and ends; the numbers' ranges, which are
// pp_find_range's (one of the two the last numbers were in, else found by a search of the ranges' bases that all lanes
// take in step), and the spellings' addresses where their ranges hold them; then the two aligned words that hold each spelling's bytes, four tokens a vector, and their digits
// (check_input_literal_digits). The preprocessor's ints take their values. The scalar path, the twin, takes
// check_input_literal_plain for every literal. An aligned word holds a byte of the spelling, so is on its page.
static uint32_t check_input_expression_literals(const CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes, uint32_t acting,
                                                CheckInputLiterals* literals, bool scalar)
{
    const Parser* parser = reader->parser;
    const Preprocessor* pp = reader->pp;
    const uint32_t literal = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_LITERAL << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    // Gathers index by signed 32-bit offsets: the tokens' (the compaction checked them), the preprocessor's tokens' and
    // the ranges' bytes.
    const bool indexed = parser->tokens.count <= (uint32_t)INT32_MAX - 4 && pp->output.count <= (uint32_t)INT32_MAX - 4 &&
                         (uint64_t)pp->range_count * sizeof(PPRange) <= (uint64_t)INT32_MAX;
    const uint8_t* table = (const uint8_t*)pp->ranges;
    // The stages' columns, a literal each: its token then its source, its spelling's location then its address, its
    // spelling's length, and its end (the preprocessor's int's value); and a group's numbers, the preprocessor's ints and
    // the numbers whose ranges hold them.
    uint32_t found[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t places[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t lengths[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t ends[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint64_t spellings[BUSTER_CHECK_EXPRESSION_CHUNK];
    __mmask16 numbers[BUSTER_CHECK_EXPRESSION_CHUNK / 16];
    __mmask16 integers[BUSTER_CHECK_EXPRESSION_CHUNK / 16];
    __mmask16 inside[BUSTER_CHECK_EXPRESSION_CHUNK / 16];
    uint32_t count = 0;
    uint32_t position = 0;
    uint32_t cursor = BUSTER_PP_NONE;
    for (; !BUSTER_SCALAR_PATH(scalar) && indexed && position < acting; position += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, acting - position < 16 ? acting - position : 16);
        const __mmask16 matched = _mm512_mask_cmpeq_epi32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, classes + position), _mm512_set1_epi32((int32_t)literal));
        _mm512_mask_compressstoreu_epi32(found + count, matched, _mm512_maskz_loadu_epi32(lanes, tokens + position));
        count += (uint32_t)__builtin_popcount(matched);
    }
    const __m512i zero = _mm512_setzero_si512();
    // The names and the sources: an unnamed token's source in `found`, a named one's none of the numbers' or ints'.
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, count - at < 16 ? count - at : 16);
        const __m512i token = _mm512_maskz_loadu_epi32(lanes, found + at);
        const __mmask16 unnamed = _mm512_mask_testn_epi32_mask(lanes, _mm512_mask_i32gather_epi32(zero, lanes, token, parser->tokens.names, 4),
                                                              _mm512_set1_epi32(-1));
        _mm512_mask_storeu_epi32(found + at, lanes, _mm512_mask_i32gather_epi32(zero, lanes, token, parser->tokens.sources, 4));
        numbers[at / 16] = unnamed;
    }
    // The sources' kinds, locations and ends: the numbers and the ints.
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 unnamed = numbers[at / 16];
        const __m512i source = _mm512_maskz_loadu_epi32(unnamed, found + at);
        const __m512i kinds = check_input_gather_bytes(unnamed, source, pp->output.kinds);
        const __m512i location = _mm512_mask_i32gather_epi32(zero, unnamed, source, pp->output.locations, 4);
        const __m512i end = _mm512_mask_i32gather_epi32(zero, unnamed, source, pp->output.values, 4);
        const __m512i length = _mm512_sub_epi32(end, location);
        numbers[at / 16] = _mm512_mask_cmpeq_epi32_mask(unnamed, kinds, _mm512_set1_epi32(C_TOKEN_NUMERIC_CONSTANT)) &
                           _mm512_cmplt_epu32_mask(_mm512_sub_epi32(length, _mm512_set1_epi32(1)), _mm512_set1_epi32((int32_t)BUSTER_CHECK_LITERAL_PLAIN_DIGITS));
        integers[at / 16] = _mm512_mask_cmpeq_epi32_mask(unnamed, kinds, _mm512_set1_epi32(BUSTER_PP_KIND_INTEGER)) &
                            _mm512_cmple_epu32_mask(end, _mm512_set1_epi32(INT32_MAX));
        _mm512_storeu_si512(places + at, location);
        _mm512_storeu_si512(lengths + at, length);
        _mm512_storeu_si512(ends + at, end);
    }
    // The numbers' ranges: one of the two the last numbers were in, where it holds the location (most of a chunk's numbers
    // are in one range or two), else the last whose base is at most the location, found by halving the ranges in step,
    // which is then the first of the two. Where the range holds the location, the spelling's address.
    const __m512i stride = _mm512_set1_epi32((int32_t)sizeof(PPRange));
    uint32_t cursors[2] = { BUSTER_PP_NONE, BUSTER_PP_NONE };
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 ranged = pp->range_count != 0 ? numbers[at / 16] : 0;
        const __m512i location = _mm512_loadu_si512(places + at);
        __m512i addresses[2] = { zero, zero };
        __mmask16 held = 0;
        for (uint32_t c = 0; c < 2; c += 1)
        {
            const PPRange* range = cursors[c] < pp->range_count ? &pp->ranges[cursors[c]] : NULL;
            const __m512i place = _mm512_sub_epi32(location, _mm512_set1_epi32(range != NULL ? (int32_t)range->base : 0));
            const __mmask16 in = range != NULL ? _mm512_mask_cmplt_epu32_mask(ranged & ~held, place, _mm512_set1_epi32((int32_t)range->length)) : 0;
            const __m512i bytes = _mm512_set1_epi64(range != NULL ? (int64_t)(uintptr_t)range->bytes : 0);
            addresses[0] = _mm512_mask_add_epi64(addresses[0], (__mmask8)in, bytes, _mm512_cvtepu32_epi64(_mm512_castsi512_si256(place)));
            addresses[1] = _mm512_mask_add_epi64(addresses[1], (__mmask8)(in >> 8), bytes, _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(place, 1)));
            held |= in;
        }
        const __mmask16 others = ranged & ~held;
        if (others != 0)
        {
            __m512i range = zero;
            for (uint32_t size = pp->range_count; size > 1; size -= size / 2)
            {
                const __m512i probe = _mm512_add_epi32(range, _mm512_set1_epi32((int32_t)(size / 2)));
                const __m512i bases = _mm512_mask_i32gather_epi32(zero, others, _mm512_mullo_epi32(probe, stride), table + offsetof(PPRange, base), 1);
                range = _mm512_mask_mov_epi32(range, _mm512_mask_cmple_epu32_mask(others, bases, location), probe);
            }
            const __m512i entry = _mm512_mullo_epi32(range, stride);
            const __m512i place = _mm512_sub_epi32(location, _mm512_mask_i32gather_epi32(zero, others, entry, table + offsetof(PPRange, base), 1));
            const __mmask16 found_in = _mm512_mask_cmplt_epu32_mask(others, place, _mm512_mask_i32gather_epi32(zero, others, entry, table + offsetof(PPRange, length), 1));
            addresses[0] = _mm512_mask_add_epi64(addresses[0], (__mmask8)found_in,
                                                 _mm512_mask_i32gather_epi64(zero, (__mmask8)found_in, _mm512_castsi512_si256(entry), table + offsetof(PPRange, bytes), 1),
                                                 _mm512_cvtepu32_epi64(_mm512_castsi512_si256(place)));
            addresses[1] = _mm512_mask_add_epi64(addresses[1], (__mmask8)(found_in >> 8),
                                                 _mm512_mask_i32gather_epi64(zero, (__mmask8)(found_in >> 8), _mm512_extracti64x4_epi64(entry, 1), table + offsetof(PPRange, bytes), 1),
                                                 _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(place, 1)));
            held |= found_in;
            uint32_t ranges[16];
            _mm512_storeu_si512(ranges, range);
            cursors[1] = found_in != 0 ? cursors[0] : cursors[1];
            cursors[0] = found_in != 0 ? ranges[__builtin_ctz(found_in)] : cursors[0];
        }
        inside[at / 16] = held;
        _mm512_storeu_si512(spellings + at, addresses[0]);
        _mm512_storeu_si512(spellings + at + 8, addresses[1]);
    }
    // The spellings' digits.
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, count - at < 16 ? count - at : 16);
        const __mmask16 held = inside[at / 16];
        __m512i values = _mm512_mask_mov_epi32(_mm512_set1_epi32((int32_t)BUSTER_CHECK_NONE), integers[at / 16], _mm512_loadu_si512(ends + at));
        if (held != 0)
        {
            const __m512i addresses[2] = {
                _mm512_loadu_si512(spellings + at),
                _mm512_loadu_si512(spellings + at + 8),
            };
            const __m512i offsets = _mm512_and_si512(_mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi64_epi32(addresses[0])), _mm512_cvtepi64_epi32(addresses[1]), 1),
                                                     _mm512_set1_epi32(7));
            const __m512i length = _mm512_mask_mov_epi32(_mm512_set1_epi32(1), held, _mm512_loadu_si512(lengths + at));
            // A token's second word holds its spelling's bytes past the first's eight.
            const __mmask16 seconds = _mm512_mask_cmpgt_epu32_mask(held, _mm512_add_epi32(offsets, length), _mm512_set1_epi32(8));
            __m512i groups[4];
            uint32_t bad = 0;
            for (uint32_t group = 0; group < 4; group += 1)
            {
                const int64_t b = (int64_t)(group & 1) * 4;
                const __m512i pick = _mm512_set_epi64(b + 3, b + 3, b + 2, b + 2, b + 1, b + 1, b, b);
                const __m512i words = _mm512_add_epi64(_mm512_andnot_si512(_mm512_set1_epi64(7), _mm512_permutexvar_epi64(pick, addresses[group >> 1])),
                                                       _mm512_set_epi64(8, 0, 8, 0, 8, 0, 8, 0));
                const __mmask8 loaded = (__mmask8)(_pdep_u32((held >> (group * 4)) & 0xf, 0x55) | _pdep_u32((seconds >> (group * 4)) & 0xf, 0xaa));
                const __m512i gathered = _mm512_mask_i64gather_epi64(zero, loaded, words, (const void*)0, 1);
                const int32_t g = (int32_t)group * 4;
                const __m512i lane = _mm512_set_epi32(g + 3, g + 3, g + 3, g + 3, g + 2, g + 2, g + 2, g + 2, g + 1, g + 1, g + 1, g + 1, g, g, g, g);
                uint64_t wrong = 0;
                groups[group] = check_input_literal_digits(gathered, _mm512_permutexvar_epi32(lane, offsets), _mm512_permutexvar_epi32(lane, length), &wrong);
                // A lane's bit where any byte of its 16 is wrong.
                const uint64_t nibbles = wrong | wrong >> 1 | wrong >> 2 | wrong >> 3;
                const uint64_t sixteens = nibbles | nibbles >> 4 | nibbles >> 8 | nibbles >> 12;
                bad |= (uint32_t)_pext_u64(sixteens, 0x0001000100010001ull) << (group * 4);
            }
            // Each lane's value from its group's first dwords.
            const __m512i firsts = _mm512_set_epi32(0, 0, 0, 0, 0, 0, 0, 0, 28, 24, 20, 16, 12, 8, 4, 0);
            const __m512i low = _mm512_permutex2var_epi32(groups[0], firsts, groups[1]);
            const __m512i high = _mm512_permutex2var_epi32(groups[2], firsts, groups[3]);
            const __m512i read = _mm512_permutex2var_epi32(low, _mm512_set_epi32(23, 22, 21, 20, 19, 18, 17, 16, 7, 6, 5, 4, 3, 2, 1, 0), high);
            values = _mm512_mask_mov_epi32(values, (__mmask16)(held & ~bad), read);
        }
        _mm512_mask_storeu_epi32(literals->values + at, lanes, values);
    }
    for (; position < acting; position += 1)
    {
        if (classes[position] == literal)
        {
            literals->values[count] = check_input_literal_plain(reader, tokens[position], &cursor);
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
        const uint32_t node = check_input_add_leaf(reader, token);
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

// A number or a character operand, the chunk's literal `at`: its literal's VALUE node, or the report of an invalid one; in
// a unit with overload tables, its token's head is its leaf's, none. A plain decimal's value the chunk's literals hold
// (CheckInputLiterals); every other literal is parsed here. A function's name is the walk's, as its tests take it after
// the literals.
static bool check_input_expression_literal(CheckInputReader* reader, uint32_t token, const CheckInputLiterals* literals, uint32_t at)
{
    const bool acted = !check_input_function_name_token(reader->parser, token);
    if (acted)
    {
        CheckLiteral literal = { .type = BUSTER_TYPE_INT, .integer = literals->values[at], .valid = true };
        if (literals->values[at] == BUSTER_CHECK_NONE)
        {
            check_input_literal(reader, token, &literal);
        }
        if (!literal.valid)
        {
            check_input_report(reader, token, check_input_literal_report(&literal));
        }
        else
        {
            const uint32_t node = check_input_add_leaf(reader, token);
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
// Always inlined (#880), as profile-guided builds do; hot caller: check_input_expression_coded, once per binary
// operator.
static inline __attribute__((always_inline)) bool check_input_expression_binary(CheckInputReader* reader, uint32_t token, CheckInputValues values)
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
        const uint32_t node = check_input_add_leaf(reader, token);
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
// in a condition's context, which may hold one value at most. In a unit with statement expressions, the last statement of
// a statement expression's region (CheckInputRegion.final_statement, found through its parent, or a label's) gives the
// region its one value as its result, and the discards are chained from the token for the region's walk
// (CheckInputReader.region_roots): what the walk's action does, in the same order. A region's last statement without
// exactly one value is the walk's, which refuses it.
static bool check_input_expression_terminator(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    CheckInput* input = reader->output;
    const bool condition = reader->conditions[token] != 0;
    CheckInputRegion* region = NULL;
    if (input->region_count != 0)
    {
        const Parser* parser = reader->parser;
        const uint32_t parent = parser->tree.parents[token];
        uint32_t selected = parent < parser->tokens.count ? input->expression_regions[parent] : 0;
        if (selected == 0 && parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_LABEL)
        {
            selected = check_input_labeled_region(reader, parent);
        }
        region = selected != 0 && input->regions[selected - 1].final_statement == token ? &input->regions[selected - 1] : NULL;
    }
    const bool acted = (!condition || values.count <= 1) && (region == NULL || values.count == 1);
    if (acted && region != NULL)
    {
        region->result = values.first;
    }
    uint32_t last = BUSTER_CHECK_NONE;
    for (uint32_t value = acted ? values.first : BUSTER_CHECK_NONE; value != BUSTER_CHECK_NONE; value = reader->value_next[value])
    {
        const uint32_t discard = check_input_add_node(reader, condition ? CHECK_OP_CONDITION : CHECK_OP_CAST, token, value, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (discard != BUSTER_CHECK_NONE)
        {
            input->graph.types[discard] = BUSTER_TYPE_VOID;
            if (reader->region_roots != NULL)
            {
                if (last == BUSTER_CHECK_NONE)
                {
                    reader->region_roots[token] = discard;
                }
                else
                {
                    reader->value_next[last] = discard;
                }
                last = discard;
            }
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
// both of which the walk's visit writes again as they were. In a coded unit, the visit's ticks are added to `*visited`
// (BUSTER_CHECK_EXPRESSION_PART_VISITS) unless it is null; in another, the caller times its whole chunk as visits.
static void check_input_expression_coded(CheckInputReader* reader, uint32_t token, uint8_t classes, bool coded, const CheckInputIdentifiers* identifiers,
                                         uint32_t* identifier, const CheckInputLiterals* literals, uint32_t* literal, uint64_t* visited)
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
                acted = check_input_expression_literal(reader, token, literals, *literal);
                *literal += 1;
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
    // Left tokens are counted in units the coded walk takes alone: in another, the walk's visit takes every token. The
    // tokens acted at by class are each class's tokens less those it left (check_input_expressions_coded's count).
    // Not hinted, though 98% of sqlite3.c's tokens act: with the visit's counters and clock in this branch, a hint
    // that the visit is rare took 19 thousand more taken branches (#1175).
    if (!acted)
    {
        reader->output->expression_left[code] += coded;
        if (reader->expression_declined != NULL)
        {
            reader->expression_declined[token] = 1;
        }
        const bool clocked = coded && visited != NULL;
        const uint64_t start = clocked ? __rdtsc() : 0;
        check_input_expression_visit(reader, token, classes);
        if (clocked)
        {
            *visited += __rdtsc() - start;
        }
    }
}

// Whether `token` passes its children's values on to its parent in the graph by passes' numbering: a LIST, a GROUP or a
// SEPARATOR the walk's visit didn't take.
static bool check_input_numbering_passes(const CheckInputReader* reader, uint32_t token)
{
    const uint32_t code = (reader->visits[token] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    return (code == CHECK_INPUT_CODE_LIST || code == CHECK_INPUT_CODE_GROUP || code == CHECK_INPUT_CODE_SEPARATOR) && reader->expression_declined[token] == 0;
}

// Appends node `value` to the list of `consumer`'s values in the graph by passes' numbering.
static void check_input_numbering_append(uint32_t* first, uint32_t* last, uint32_t* counts, uint32_t* next, uint32_t consumer, uint32_t value)
{
    if (counts[consumer] != 0)
    {
        next[last[consumer]] = value;
    }
    else
    {
        first[consumer] = value;
    }
    last[consumer] = value;
    next[value] = BUSTER_CHECK_NONE;
    counts[consumer] += 1;
}

// The graph by passes' numbering, measured (#1038's stages 2 and 4, under PPOptions.expression_numbering): after the coded
// walk, its acting tokens numbered again in postorder from `base` (the graph's count before the walk), as such a pass
// would number them before writing any node. Each value goes to its consumer's list: the nearest ancestor that doesn't pass
// values on (check_input_numbering_passes). A token the walk's visit took (the scalar class, and those their classes'
// actions left, CheckInputReader.expression_declined) is a barrier: it takes the walk's count of nodes at it and the walk's
// chain of its values. A terminator makes a discard a value of its list; an identifier, a literal or a string a node where
// the walk made one; a return one where it has a value; and every other class one, whose first operand is its list's
// first. A condition's and a return's nodes are no values. The numbering is timed as written plainly, from its columns'
// allocation and clearing and the walk's counts at each token to its last token, the chunks compacted again
// (CheckInput.expression_numbering_ticks). Then each node a class numbered is checked against the walk's: its token, and
// but for a VALUE its first operand; the barriers' nodes it only counts. The caller pops the scratch.
static void check_input_expression_numbering(CheckInputReader* reader, uint32_t base, uint32_t* tokens, uint32_t* classes)
{
    CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const uint32_t count = reader->parser->tokens.count;
    const uint32_t* parents = reader->parser->tree.parents;
    const uint32_t nodes = graph->count;
    const uint32_t dropped = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_DROP << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    Arena* scratch = reader->scratch;
    // The numbering's whole cost, from its columns' allocation and clearing to its last token; the check is outside it.
    const uint64_t start = __rdtsc();
    // Per token: its list's first and last values and their count, and the nodes the walk made at it. Per node: the next
    // value of its list, and the token and first operand the numbering gives it.
    uint32_t* first = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* last = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* counts = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* made = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* next = arena_push_array(scratch, (uint64_t)nodes + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* numbered = arena_push_array(scratch, (uint64_t)nodes + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* operands = arena_push_array(scratch, (uint64_t)nodes + 1, sizeof(uint32_t), _Alignof(uint32_t));
    if (first != NULL && last != NULL && counts != NULL && made != NULL && next != NULL && numbered != NULL && operands != NULL)
    {
        memset(counts, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(made, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(numbered, 0xff, ((uint64_t)nodes + 1) * sizeof(uint32_t));
        for (uint32_t n = base; n < nodes; n += 1)
        {
            made[graph->tokens[n] < count ? graph->tokens[n] : count] += 1;
        }
        uint32_t id = base;
        for (uint32_t chunk = 0; chunk < count; chunk += BUSTER_CHECK_EXPRESSION_CHUNK)
        {
            const uint32_t end = count - chunk < BUSTER_CHECK_EXPRESSION_CHUNK ? count : chunk + BUSTER_CHECK_EXPRESSION_CHUNK;
            const uint32_t acting = check_input_expression_compact(reader, chunk, end, tokens, classes, dropped, false);
            for (uint32_t i = 0; i < acting; i += 1)
            {
                const uint32_t token = tokens[i];
                const uint32_t code = (classes[i] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
                const bool barrier = code == CHECK_INPUT_CODE_SCALAR || reader->expression_declined[token] != 0;
                if (barrier || !check_input_numbering_passes(reader, token))
                {
                    uint32_t consumer = parents[token];
                    while (consumer < count && check_input_numbering_passes(reader, consumer))
                    {
                        consumer = parents[consumer];
                    }
                    uint32_t value = BUSTER_CHECK_NONE;
                    uint32_t chain = BUSTER_CHECK_NONE;
                    uint32_t chained = 0;
                    if (barrier)
                    {
                        id += made[token];
                        chain = reader->first_value[token];
                        chained = reader->value_counts[token];
                    }
                    else if (code == CHECK_INPUT_CODE_TERMINATOR)
                    {
                        uint32_t operand = first[token];
                        for (uint32_t k = 0; k < counts[token]; k += 1)
                        {
                            numbered[id < nodes ? id : nodes] = token;
                            operands[id < nodes ? id : nodes] = operand;
                            id += 1;
                            operand = next[operand];
                        }
                    }
                    else if (code == CHECK_INPUT_CODE_IDENTIFIER || code == CHECK_INPUT_CODE_LITERAL || code == CHECK_INPUT_CODE_STRING)
                    {
                        if (input->token_nodes[token] != BUSTER_CHECK_NONE)
                        {
                            numbered[id < nodes ? id : nodes] = token;
                            operands[id < nodes ? id : nodes] = BUSTER_CHECK_NONE;
                            value = id;
                            id += 1;
                        }
                    }
                    else if (code != CHECK_INPUT_CODE_RETURN || counts[token] != 0)
                    {
                        numbered[id < nodes ? id : nodes] = token;
                        operands[id < nodes ? id : nodes] = counts[token] != 0 ? first[token] : BUSTER_CHECK_NONE;
                        value = code == CHECK_INPUT_CODE_CONDITION || code == CHECK_INPUT_CODE_RETURN ? BUSTER_CHECK_NONE : id;
                        id += 1;
                    }
                    if (consumer < count && value < nodes)
                    {
                        check_input_numbering_append(first, last, counts, next, consumer, value);
                    }
                    for (uint32_t k = 0; consumer < count && k < chained && chain < nodes; k += 1)
                    {
                        const uint32_t after = reader->value_next[chain];
                        check_input_numbering_append(first, last, counts, next, consumer, chain);
                        chain = after;
                    }
                }
            }
        }
        input->expression_numbering_ticks = __rdtsc() - start;
        input->expression_numbering_nodes = id - base;
        uint32_t checked = 0;
        uint32_t mismatches = id != nodes;
        for (uint32_t n = base; n < nodes && n < id; n += 1)
        {
            if (numbered[n] != BUSTER_CHECK_NONE)
            {
                checked += 1;
                mismatches += graph->tokens[n] != numbered[n] || (graph->ops[n] != CHECK_OP_VALUE && graph->operand0[n] != operands[n]);
            }
        }
        input->expression_numbering_checked = checked;
        input->expression_numbering_mismatches = mismatches;
    }
}

// The coded walk: the postorder a chunk at a time, compacted to the tokens the walk acts at, each acted at by its class.
// A DROP's action is nothing but its children's chains joined, which no parent reads (its first value is NONE, as the
// reader's setup wrote it), so the compaction leaves its tokens out. A unit with expressions past its limit or deferred
// is acted at by the walk's visit throughout, though still compacted. A unit with overload tables (CheckInput.
// overload_heads: one that names the overloadable attribute in an attribute list, whose every function declaration is a
// candidate) is coded too: the class actions carry the heads through their nodes to their calls as the walk's does.
static void check_input_expressions_coded_range(CheckInputReader* reader, uint32_t begin, uint32_t stop)
{
    const CheckInput* input = reader->output;
    const uint32_t count = reader->parser->tokens.count;
    // Every token's expression ends by the count, so that within these limits none is past them.
    const bool coded = input->diagnostic_limits == NULL && input->deferred_tokens == NULL && reader->expression_limit >= count && input->token_count >= count;
    const uint32_t dropped = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_DROP << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    uint32_t tokens[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t classes[BUSTER_CHECK_EXPRESSION_CHUNK];
    CheckInputIdentifiers identifiers;
    CheckInputLiterals literals;
    // The graph's nodes before the walk, from which the numbering's check counts (PPOptions.expression_numbering), and the
    // tokens the classes' actions leave, which it takes as the walk's visits.
    const uint32_t base = reader->output->graph.count;
    const uint64_t scratch_mark = reader->scratch->length;
    reader->expression_declined = coded && begin == 0 && stop == count && reader->pp->options.expression_numbering ? arena_push_array(reader->scratch, (uint64_t)count + 1, 1, 1) : NULL;
    if (reader->expression_declined != NULL)
    {
        memset(reader->expression_declined, 0, (uint64_t)count + 1);
    }
    // The parts' ticks (CheckInput.expression_part_ticks): three TSC reads a chunk, and two a visit of a coded unit's,
    // which are the actions' less their visits'. And the tokens of each class the actions reached, in four histograms
    // that a chunk's run of one class doesn't make a chain of: those the classes' actions acted at are the rest of the
    // ones their actions left (CheckInput.expression_acted).
    uint64_t prepared = 0;
    uint64_t acted = 0;
    uint64_t visited = 0;
    // A coded unit's actions timed by class where the options ask (CheckInput.expression_class_ticks).
    const bool timed = coded && reader->pp->options.expression_timing;
    uint32_t reached[4][BUSTER_CHECK_EXPRESSION_CODES] = { 0 };
    for (uint32_t first = begin; first < stop && !reader->output->resource; first += BUSTER_CHECK_EXPRESSION_CHUNK)
    {
        const uint64_t start = __rdtsc();
        const uint32_t end = stop - first < BUSTER_CHECK_EXPRESSION_CHUNK ? stop : first + BUSTER_CHECK_EXPRESSION_CHUNK;
        const uint32_t acting = check_input_expression_compact(reader, first, end, tokens, classes, coded ? dropped : UINT32_MAX, false);
        if (coded)
        {
            check_input_expression_identifiers(reader, tokens, classes, acting, &identifiers, false);
            check_input_expression_literals(reader, tokens, classes, acting, &literals, false);
        }
        const uint64_t ready = __rdtsc();
        uint32_t identifier = 0;
        uint32_t literal = 0;
        uint32_t i = 0;
        if (!timed)
        {
            for (; i < acting && !reader->output->resource; i += 1)
            {
                check_input_expression_coded(reader, tokens[i], (uint8_t)classes[i], coded, &identifiers, &identifier, &literals, &literal, &visited);
            }
        }
        else
        {
            // Each token's action timed by its class, and a pair of reads after it, whose ticks are the timing's own. The
            // visits aren't timed apart: their reads would be in their classes' ticks, which only these runs pay.
            for (; i < acting && !reader->output->resource; i += 1)
            {
                const uint64_t before = __rdtsc();
                check_input_expression_coded(reader, tokens[i], (uint8_t)classes[i], coded, &identifiers, &identifier, &literals, &literal, NULL);
                const uint64_t after = __rdtsc();
                reader->output->expression_class_ticks[(classes[i] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT] += after - before;
                reader->output->expression_class_overhead += __rdtsc() - after;
            }
        }
        for (uint32_t k = 0; coded && k < i; k += 1)
        {
            reached[k & 3][(classes[k] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT] += 1;
        }
        const uint64_t done = __rdtsc();
        prepared += ready - start;
        acted += done - ready;
    }
    // A coded unit's visits lie within its chunks' actions; another's actions are all visits.
    visited = coded ? (visited < acted ? visited : acted) : acted;
    reader->output->expression_part_ticks[BUSTER_CHECK_EXPRESSION_PART_PREPARE] = prepared;
    reader->output->expression_part_ticks[BUSTER_CHECK_EXPRESSION_PART_ACTIONS] = acted - visited;
    reader->output->expression_part_ticks[BUSTER_CHECK_EXPRESSION_PART_VISITS] = visited;
    for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
    {
        const uint32_t tokens_reached = reached[0][code] + reached[1][code] + reached[2][code] + reached[3][code];
        reader->output->expression_acted[code] = tokens_reached - reader->output->expression_left[code];
    }
    if (reader->expression_declined != NULL && !reader->output->resource)
    {
        check_input_expression_numbering(reader, base, tokens, classes);
    }
    reader->expression_declined = NULL;
    arena_pop_to(reader->scratch, scratch_mark);
}

static void check_input_expressions_coded(CheckInputReader* reader)
{
    check_input_expressions_coded_range(reader, 0, reader->parser->tokens.count);
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
