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

// An operator's node, with what the walk's action gives every operator's: the type the token's query asked for, and a
// static initializer's context.
static uint32_t check_input_expression_operator(CheckInputReader* reader, CheckOp op, uint32_t token, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t node = check_input_add_node(reader, op, token, a, b, c);
    if (node != BUSTER_CHECK_NONE)
    {
        CheckGraph* graph = &reader->output->graph;
        graph->types[node] = reader->query_types[token];
        graph->contexts[node] = reader->static_initializers[token] ? BUSTER_CHECK_CONTEXT_STATIC : 0;
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

_Static_assert(sizeof(CheckValue) == 96 && offsetof(CheckValue, category) == 4 && offsetof(CheckValue, flags) == 8 &&
               offsetof(CheckValue, constant_kind) == 12 && offsetof(CheckValue, integer) == 16 && offsetof(CheckValue, floating_storage) == 32 &&
               offsetof(CheckValue, imaginary_storage) == 48 && offsetof(CheckValue, object) == 64 && offsetof(CheckValue, overload_head) == 68 &&
               offsetof(CheckValue, offset) == 72 && offsetof(CheckValue, object_bit_offset) == 92 && sizeof(CheckCategory) == 4 &&
               sizeof(CheckConstantKind) == 4,
               "an identifier's leaf is written as twelve words");

// An identifier's leaf, into `leaf`: the twelve words of its type, category, flags, constant kind, value and object, all
// else zero (no x87 store of the zero floating parts), as the walk's action makes it of the identifier's declaration.
static void check_input_identifier_leaf(const CheckInputReader* reader, TypeId type, uint32_t declaration, CheckValue* leaf)
{
    const bool enumeration = reader->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
    const uint64_t category = enumeration ? CHECK_RVALUE : type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
    const uint64_t flags = (enumeration ? BUSTER_CHECK_ICE : 0) |
                           (reader->static_objects[declaration] && !reader->output->thread_local[declaration] ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                           (reader->registers[declaration] ? BUSTER_CHECK_REGISTER : 0);
    const uint64_t kind = enumeration ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS;
    const unsigned __int128 integer = enumeration ? reader->enum_values[declaration] : 0;
    const uint64_t words[12] =
    {
        (uint64_t)type | category << 32,
        flags | kind << 32,
        (uint64_t)integer,
        (uint64_t)(integer >> 64),
        0,
        0,
        0,
        0,
        reader->objects[declaration],
        0,
        0,
        0,
    };
    memcpy(leaf, words, sizeof(words));
}

// An identifier operand: a VALUE node of its declaration and its leaf, or nothing for one without a declared type. A K&R
// unit's identifiers, whose types a K&R function's designator interns, and a constexpr object's, which reads its value,
// are the walk's.
static bool check_input_expression_identifier(CheckInputReader* reader, uint32_t token)
{
    const TypeId type = reader->declared_types[token];
    const uint32_t declaration = type != 0 ? reader->output->declarations[token] : 0;
    const bool acted = reader->output->kr_parameter_count == 0 && (type == 0 || (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_CONSTEXPR) == 0);
    if (acted && type != 0)
    {
        const uint32_t node = check_input_add_node(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            check_input_identifier_leaf(reader, type, declaration, check_graph_leaf_of(&reader->output->graph, node));
            check_input_expression_value(reader, token, node);
        }
    }
    return acted;
}

// A number or a character operand: its literal's VALUE node, or the report of an invalid one. A function's name is the
// walk's, as its tests take it after the literals.
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
                *check_graph_leaf_of(&reader->output->graph, node) = check_input_literal_value(reader, token, literal);
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
    const bool acted = op != CHECK_OP_COUNT && values.count == 1 && reader->builtins[token] == 0;
    if (acted)
    {
        check_input_expression_value(reader, token, check_input_expression_operator(reader, op, token, values.first, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE));
    }
    return acted;
}

// A binary operator, or a comma operator: two values, but an initializer's assignment.
static bool check_input_expression_binary(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    const CheckOp op = check_input_binary(reader->parser->tokens.kinds[token]);
    const bool acted = op != CHECK_OP_COUNT && values.count == 2 && reader->initializers[token] == 0 && reader->builtins[token] == 0;
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
    const bool acted = (values.count == 3 || values.count == 2) && reader->builtins[token] == 0;
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
    const bool acted = values.count == 1 && token + 1 < parser->tokens.count && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER && reader->builtins[token] == 0;
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
    const bool acted = values.count == wanted && reader->builtins[token] == 0;
    if (acted)
    {
        const uint32_t second = wanted == 2 ? reader->value_next[values.first] : BUSTER_CHECK_NONE;
        check_input_expression_value(reader, token, check_input_expression_operator(reader, op, token, values.first, second, BUSTER_CHECK_NONE));
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
    const bool acted = values.count == 1 && reader->builtins[token] == 0;
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
// (check_input_library_call, #148). A builtin's, an intrinsic's and a K&R unit's calls are the walk's.
static bool check_input_expression_call(CheckInputReader* reader, uint32_t token, CheckInputValues values)
{
    CheckInput* input = reader->output;
    const bool acted = values.count != 0 && reader->builtins[token] == 0 && (input->intrinsic_ids == 0 || input->intrinsic_ids[token] == 0) &&
                       input->kr_parameter_count == 0;
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
static void check_input_expression_coded(CheckInputReader* reader, uint32_t token, uint8_t classes, bool coded)
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
                acted = check_input_expression_identifier(reader, token);
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
                if (reader->expression_commas[token])
                {
                    acted = check_input_expression_binary(reader, token, values);
                }
                else
                {
                    check_input_expression_list(reader, token, values);
                }
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
// reader's setup wrote it), so the compaction leaves its tokens out. A unit with expressions past its limit or deferred,
// or with overload tables (CheckInput.overload_heads: a unit that spells `overloadable` or `__overloadable__`, whose
// every function declaration is a candidate whose head the walk's action carries through its nodes to its calls), is
// acted at by the walk's visit throughout, though still compacted.
static void check_input_expressions_coded(CheckInputReader* reader)
{
    const CheckInput* input = reader->output;
    const uint32_t count = reader->parser->tokens.count;
    // Every token's expression ends by the count, so that within these limits none is past them.
    const bool coded = input->diagnostic_limits == NULL && input->deferred_tokens == NULL && reader->expression_limit >= count && input->token_count >= count &&
                       input->overload_heads == NULL;
    const uint32_t dropped = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_DROP << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    uint32_t tokens[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t classes[BUSTER_CHECK_EXPRESSION_CHUNK];
    for (uint32_t first = 0; first < count && !reader->output->resource; first += BUSTER_CHECK_EXPRESSION_CHUNK)
    {
        const uint32_t end = count - first < BUSTER_CHECK_EXPRESSION_CHUNK ? count : first + BUSTER_CHECK_EXPRESSION_CHUNK;
        const uint32_t acting = check_input_expression_compact(reader, first, end, tokens, classes, coded ? dropped : UINT32_MAX, false);
        for (uint32_t i = 0; i < acting && !reader->output->resource; i += 1)
        {
            check_input_expression_coded(reader, tokens[i], (uint8_t)classes[i], coded);
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
