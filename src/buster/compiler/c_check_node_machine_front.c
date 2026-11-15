// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The node machine's front (#2524, step 1b, docs/node-machine.md): the records the machine's T would type, made from the
// tokens the expressions' walk acts at, their classes and the reader's answers, without the walk's chains of values
// through the token columns and without its graph. In the shadow it runs beside the coded walk (PPOptions.node_front),
// which is its oracle: each record is compared with the node the walk made at the same token. No compile takes it.
//
// A record is a node's operator, its token, its operands as earlier records, each also as a byte, its distance back in
// the postorder (what #2850's layout asks of the front: an operand's facts are then a permute over the block of records
// before it, not a gather), and what the operator reads beside them: a leaf's value (the chunk's identifiers' and literals' entries, a string's object), a member's name, a call's arguments
// and its library mark, the type the token's query asked for. The operands come from a stack of the values not yet
// taken, in groups that share a consumer: a value's is its token's parent, and a token that passes its children's values
// on (a list, a group, a separator, a braced initializer) gives its group to its own parent, in one store however many
// values it holds. A token's values are then the top group's, where the token is its consumer, in source order, and a
// value whose consumer never acts is no entry at all. So a token costs its class's case, a load of its parent, and its
// record's stores, all in arrays a chunk long; nothing is written a token.
//
// A token the front has no case for (the walk's own visits, a class's action's rarer branches, a literal whose decoding
// reports) is left: its values are taken and it gives an unknown one, which leaves its consumer in turn. A full
// expression with such a token has records only for the subtrees below it, and the shadow counts its nodes as not the
// front's (NodeFrontShadow.nodes).
// Included by c_check_input.c after the coded walk.

// The records the front holds: a chunk's, and those a full expression that began in an earlier chunk still needs.
#define BUSTER_NODE_FRONT_RECORDS 4096u
// The most values a token takes here: a call's callee and arguments, a terminator's values.
#define BUSTER_NODE_FRONT_VALUES 64u
// A stack entry whose value a left token gave, and an operand a record doesn't have.
#define BUSTER_NODE_FRONT_UNKNOWN UINT32_MAX
// A leaf's value with more than a record's columns hold (a floating constant, a wide integer), by its place among them.
#define BUSTER_NODE_FRONT_WIDES 1024u
// An operand's distance where it doesn't fit its byte: 255 records back or more. And the distance within which an operand's
// facts are in the block of 64 records before its record's (#2850).
#define BUSTER_NODE_FRONT_FAR 255u
#define BUSTER_NODE_FRONT_NEAR 64u

// The differing records a file's report lists.
#define BUSTER_NODE_FRONT_LISTED 16u

// What the shadow reports of the front (CheckInput.node_front), in the output's arena.
typedef struct NodeFrontShadow NodeFrontShadow;
struct NodeFrontShadow
{
    // The TSC's ticks of the front's own pass over the chunks (the comparison is outside them), the acting tokens it
    // read and the records it made.
    uint64_t ticks;
    uint64_t tokens;
    uint64_t records;
    // The records compared with the walk's nodes, and those that differ: a difference is a failure.
    uint64_t checked;
    uint64_t differences;
    // The tokens left, by class, and the times a full expression outgrew the front's arrays.
    uint64_t left[BUSTER_CHECK_EXPRESSION_CODES];
    // Of the tokens left, the sizeof and _Alignof ones.
    uint64_t left_queries;
    uint64_t resets;
    // The values still waiting at the unit's end: none, where every value's consumer took it.
    uint64_t waiting;
    // The first records that differed: each one's token and field (BUSTER_NODE_FRONT_FIELD_*).
    uint32_t difference_tokens[BUSTER_NODE_FRONT_LISTED];
    uint32_t difference_fields[BUSTER_NODE_FRONT_LISTED];
    // A bit a graph node: the nodes whose records agreed.
    uint64_t* nodes;
    uint32_t node_capacity;
    // The operands of the records that are no full expression's root (a discard, a condition, a return's conversion, an
    // initialization, whose operand is the expression's value, a body away for a loop's or a switch's condition), by their
    // distance back: within BUSTER_NODE_FRONT_NEAR, further but in the byte, and BUSTER_NODE_FRONT_FAR or more. The
    // roots' operands, and those of them further than BUSTER_NODE_FRONT_NEAR. And the operands whose distance isn't the
    // difference of the walk's two nodes, which must be none where the front made every node between them.
    uint64_t operands;
    uint64_t operands_near;
    uint64_t operands_byte;
    uint64_t operands_far;
    uint64_t root_operands;
    uint64_t root_operands_past;
    // The calls' arguments, and those further back than BUSTER_NODE_FRONT_NEAR from their call.
    uint64_t arguments;
    uint64_t arguments_past;
    // A bit a graph node: the nodes with an operand further back than BUSTER_NODE_FRONT_NEAR, no root's.
    uint64_t* past;
    // A byte a graph node: the context the front gives the node's record, compared once the walk has acted at every
    // token (a list gives its values theirs when the walk reaches it), or BUSTER_NODE_FRONT_CONTEXT_UNKNOWN for a record
    // whose value a left token took.
    uint8_t* contexts;
};

#define BUSTER_NODE_FRONT_CONTEXT_UNKNOWN 0xffu

#define BUSTER_NODE_FRONT_FIELD_NODE 1u
#define BUSTER_NODE_FRONT_FIELD_OPERATOR 2u
#define BUSTER_NODE_FRONT_FIELD_OPERAND 3u
#define BUSTER_NODE_FRONT_FIELD_LEAF 4u
#define BUSTER_NODE_FRONT_FIELD_MEMBER 5u
#define BUSTER_NODE_FRONT_FIELD_ARGUMENTS 6u
#define BUSTER_NODE_FRONT_FIELD_TYPE 7u
#define BUSTER_NODE_FRONT_FIELD_CONTEXT 8u
#define BUSTER_NODE_FRONT_FIELD_DISTANCE 9u

typedef struct NodeFront NodeFront;
struct NodeFront
{
    NodeFrontShadow* shadow;
    // The records made, the first of the chunk being read (the comparison's), the stack's entries, the calls' arguments
    // and the wide leaves so far.
    uint32_t count;
    uint32_t pending;
    uint32_t stack_count;
    uint32_t argument_count;
    uint32_t wide_count;
    // The entries the last token took off the stack (node_front_values): their place and how many.
    uint32_t taken_first;
    uint32_t taken_count;
    // The records made in the unit so far: a record's place in the unit's postorder (`ordinals`), which a chunk's
    // renumbering of the kept records doesn't change.
    uint32_t ordinal;
    // A record's columns. `forms` is the walk's CheckInputNodeForm, which says where its requested type and its context
    // come from; `types` the type its token's query asked for (a return's, the function's), `contexts` the graph's
    // context bits; `wides` a leaf's place among the wide values plus one, or 0.
    uint8_t ops[BUSTER_NODE_FRONT_RECORDS];
    uint8_t forms[BUSTER_NODE_FRONT_RECORDS];
    uint8_t contexts[BUSTER_NODE_FRONT_RECORDS];
    // Whether a left token took the record's value: the walk's visit may then give its node a context (a constant
    // expression's, an assertion's), which the front doesn't know.
    uint8_t taken_by_left[BUSTER_NODE_FRONT_RECORDS];
    uint32_t tokens[BUSTER_NODE_FRONT_RECORDS];
    // Its operands: as records, which the comparison follows, and as the machine would read them, each one's distance back
    // in the postorder in a byte (0 for none, BUSTER_NODE_FRONT_FAR for that or more). `roots`: a full expression's root.
    uint32_t operands[3][BUSTER_NODE_FRONT_RECORDS];
    uint8_t distances[3][BUSTER_NODE_FRONT_RECORDS];
    uint8_t roots[BUSTER_NODE_FRONT_RECORDS];
    uint32_t ordinals[BUSTER_NODE_FRONT_RECORDS];
    // The token of the name a record is, or is the address of (a call's library mark reads its callee's), or
    // BUSTER_CHECK_NONE.
    uint32_t uses[BUSTER_NODE_FRONT_RECORDS];
    uint32_t members[BUSTER_NODE_FRONT_RECORDS];
    uint32_t argument_firsts[BUSTER_NODE_FRONT_RECORDS];
    uint32_t argument_counts[BUSTER_NODE_FRONT_RECORDS];
    TypeId types[BUSTER_NODE_FRONT_RECORDS];
    // A leaf's value as a graph leaf's record holds it.
    TypeId leaf_types[BUSTER_NODE_FRONT_RECORDS];
    uint32_t leaf_flags[BUSTER_NODE_FRONT_RECORDS];
    uint32_t leaf_objects[BUSTER_NODE_FRONT_RECORDS];
    uint32_t leaf_heads[BUSTER_NODE_FRONT_RECORDS];
    uint64_t leaf_integers[BUSTER_NODE_FRONT_RECORDS];
    uint8_t leaf_categories[BUSTER_NODE_FRONT_RECORDS];
    uint8_t leaf_constants[BUSTER_NODE_FRONT_RECORDS];
    uint16_t wides[BUSTER_NODE_FRONT_RECORDS];
    // The walk's node of a record, once compared (BUSTER_CHECK_NONE for one that differed).
    uint32_t nodes[BUSTER_NODE_FRONT_RECORDS];
    // The values not yet taken, each one's record (or BUSTER_NODE_FRONT_UNKNOWN), in groups of one consumer: a group's
    // consumer's token and its first value's place. Two groups next to each other never share a consumer.
    uint32_t stack[BUSTER_NODE_FRONT_RECORDS];
    uint32_t group_consumers[BUSTER_NODE_FRONT_RECORDS];
    uint32_t group_firsts[BUSTER_NODE_FRONT_RECORDS];
    uint32_t group_count;
    // Whether the stack is full and its entries forgotten already (node_front_give).
    bool full;
    uint32_t arguments[BUSTER_NODE_FRONT_RECORDS];
    CheckValue wide_values[BUSTER_NODE_FRONT_WIDES];
};

// Whether the walk acts at `token` so as to read its children's values: a token it never acts at, or whose action is
// nothing, takes none, and a value given to it is no entry.
static inline bool node_front_consumes(const CheckInputReader* reader, uint32_t token)
{
    const uint8_t visit = token < reader->parser->tokens.count ? reader->visits[token] : 0;
    return (visit & BUSTER_CHECK_EXPRESSION_ACTS) != 0 && (visit & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT != CHECK_INPUT_CODE_DROP;
}

// The front no longer knows the record's node's context: a left token took its value, or the stack lost it.
static inline void node_front_context_unknown(NodeFront* front, uint32_t record)
{
    NodeFrontShadow* shadow = front->shadow;
    const uint32_t node = front->nodes[record];
    front->taken_by_left[record] = 1;
    if (node < shadow->node_capacity)
    {
        shadow->contexts[node] = BUSTER_NODE_FRONT_CONTEXT_UNKNOWN;
    }
}

// Every waiting value made unknown: the stack has no room for one more (a static table's thousands of elements wait
// for their list's end), or the arrays none for the records they name. Their consumers are left in turn.
static __attribute__((noinline)) void node_front_forget(NodeFront* front)
{
    for (uint32_t i = 0; i < front->stack_count; i += 1)
    {
        const uint32_t record = front->stack[i];
        if (record != BUSTER_NODE_FRONT_UNKNOWN)
        {
            node_front_context_unknown(front, record);
        }
        front->stack[i] = BUSTER_NODE_FRONT_UNKNOWN;
    }
    front->shadow->resets += 1;
}

// Gives `token`'s value, `record` (or BUSTER_NODE_FRONT_UNKNOWN), to its parent: an entry of the top group where that is
// the parent's already, else of a new one. With no room for it, the value is lost and every waiting one unknown.
static inline void node_front_give(NodeFront* front, const CheckInputReader* reader, uint32_t token, uint32_t record)
{
    const uint32_t consumer = reader->parser->tree.parents[token];
    const bool consumed = node_front_consumes(reader, consumer);
    if (consumed && front->stack_count == BUSTER_NODE_FRONT_RECORDS)
    {
        // Once for a full stack: its entries stay unknown while it is full.
        if (!front->full)
        {
            node_front_forget(front);
            front->full = true;
        }
        if (record != BUSTER_NODE_FRONT_UNKNOWN)
        {
            node_front_context_unknown(front, record);
        }
    }
    else if (consumed)
    {
        if (front->group_count == 0 || front->group_consumers[front->group_count - 1] != consumer)
        {
            front->group_consumers[front->group_count] = consumer;
            front->group_firsts[front->group_count] = front->stack_count;
            front->group_count += 1;
        }
        front->stack[front->stack_count] = record;
        front->stack_count += 1;
        front->full = false;
    }
}

// A token that passes its children's values on (a list, a separator, a group's closing, a braced initializer's
// assignment): its group becomes its parent's, joined to the group below where that is the parent's too, or is dropped
// where the parent takes no values. A static braced initializer's list gives each value's record the value context, as
// the walk's list does when it reaches the token.
static inline void node_front_pass(NodeFront* front, CheckInputReader* reader, uint32_t token)
{
    if (front->group_count != 0 && front->group_consumers[front->group_count - 1] == token)
    {
        const uint32_t top = front->group_count - 1;
        const uint32_t first = front->group_firsts[top];
        if (BUSTER_CHECK_RARE(reader, static_initializers, token))
        {
            NodeFrontShadow* shadow = front->shadow;
            for (uint32_t i = first; i < front->stack_count; i += 1)
            {
                const uint32_t record = front->stack[i];
                if (record != BUSTER_NODE_FRONT_UNKNOWN)
                {
                    const uint32_t node = front->nodes[record];
                    front->contexts[record] |= BUSTER_CHECK_CONTEXT_VALUE;
                    if (node < shadow->node_capacity && shadow->contexts[node] != BUSTER_NODE_FRONT_CONTEXT_UNKNOWN)
                    {
                        shadow->contexts[node] |= BUSTER_CHECK_CONTEXT_VALUE;
                    }
                }
            }
        }
        const uint32_t consumer = reader->parser->tree.parents[token];
        if (!node_front_consumes(reader, consumer))
        {
            front->stack_count = first;
            front->group_count = top;
        }
        else if (top != 0 && front->group_consumers[top - 1] == consumer)
        {
            front->group_count = top;
        }
        else
        {
            front->group_consumers[top] = consumer;
        }
    }
}

// `token`'s values, the top group's where the token is its consumer, taken off the stack into `values` in source order:
// how many, or BUSTER_NODE_FRONT_UNKNOWN where one is unknown or they are more than BUSTER_NODE_FRONT_VALUES.
static inline uint32_t node_front_values(NodeFront* front, uint32_t token, uint32_t* values)
{
    const bool grouped = front->group_count != 0 && front->group_consumers[front->group_count - 1] == token;
    const uint32_t first = grouped ? front->group_firsts[front->group_count - 1] : front->stack_count;
    const uint32_t count = front->stack_count - first;
    bool known = count <= BUSTER_NODE_FRONT_VALUES;
    for (uint32_t i = 0; known && i < count; i += 1)
    {
        values[i] = front->stack[first + i];
        known = values[i] != BUSTER_NODE_FRONT_UNKNOWN;
    }
    front->taken_first = first;
    front->taken_count = count;
    front->stack_count = first;
    front->group_count -= grouped;
    return known ? count : BUSTER_NODE_FRONT_UNKNOWN;
}

// The values the last token took (node_front_values) are a left token's: their records' nodes may get a context from the
// walk's visit, which the front doesn't know. A record compared already says so at its node.
static inline void node_front_taken_by_left(NodeFront* front)
{
    for (uint32_t i = 0; i < front->taken_count; i += 1)
    {
        const uint32_t record = front->stack[front->taken_first + i];
        if (record != BUSTER_NODE_FRONT_UNKNOWN)
        {
            node_front_context_unknown(front, record);
        }
    }
    front->taken_count = 0;
}

// A record of `op` at `token` with operands a, b and c (records, or BUSTER_NODE_FRONT_UNKNOWN for none): its index, or
// BUSTER_NODE_FRONT_UNKNOWN where the front's arrays are full.
static inline uint32_t node_front_add(NodeFront* front, CheckOp op, uint32_t token, uint32_t a, uint32_t b, uint32_t c, CheckInputNodeForm form, TypeId type)
{
    const uint32_t record = front->count;
    uint32_t result = BUSTER_NODE_FRONT_UNKNOWN;
    if (record < BUSTER_NODE_FRONT_RECORDS)
    {
        front->ops[record] = (uint8_t)op;
        front->forms[record] = (uint8_t)form;
        front->contexts[record] = 0;
        front->taken_by_left[record] = 0;
        front->tokens[record] = token;
        const uint32_t ordinal = front->ordinal;
        const uint32_t operands[3] = { a, b, c };
        for (uint32_t k = 0; k < 3; k += 1)
        {
            const uint32_t distance = operands[k] != BUSTER_NODE_FRONT_UNKNOWN ? ordinal - front->ordinals[operands[k]] : 0;
            front->operands[k][record] = operands[k];
            front->distances[k][record] = (uint8_t)(distance < BUSTER_NODE_FRONT_FAR ? distance : BUSTER_NODE_FRONT_FAR);
        }
        front->ordinals[record] = ordinal;
        front->ordinal = ordinal + 1;
        front->roots[record] = form == CHECK_INPUT_FORM_DISCARD || form == CHECK_INPUT_FORM_INITIALIZER;
        front->uses[record] = op == CHECK_OP_VALUE ? token : op == CHECK_OP_ADDRESS && a != BUSTER_NODE_FRONT_UNKNOWN && front->ops[a] == CHECK_OP_VALUE ? front->tokens[a] :
                                                             BUSTER_CHECK_NONE;
        front->members[record] = 0;
        front->argument_firsts[record] = 0;
        front->argument_counts[record] = 0;
        front->types[record] = type;
        front->wides[record] = 0;
        front->nodes[record] = BUSTER_CHECK_NONE;
        front->count = record + 1;
        result = record;
    }
    return result;
}

// A leaf's record of `value` at `token`, or BUSTER_NODE_FRONT_UNKNOWN where the arrays are full.
static inline uint32_t node_front_leaf(NodeFront* front, uint32_t token, const CheckValue* value)
{
    const bool wide = check_graph_leaf_rest(value);
    const bool room = !wide || front->wide_count < BUSTER_NODE_FRONT_WIDES;
    const uint32_t record = room ? node_front_add(front, CHECK_OP_VALUE, token, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN,
                                                  CHECK_INPUT_FORM_LEAF, 0) : BUSTER_NODE_FRONT_UNKNOWN;
    if (record != BUSTER_NODE_FRONT_UNKNOWN)
    {
        front->leaf_types[record] = value->type;
        front->leaf_flags[record] = value->flags;
        front->leaf_objects[record] = value->object;
        front->leaf_heads[record] = value->overload_head;
        front->leaf_integers[record] = (uint64_t)value->integer;
        front->leaf_categories[record] = (uint8_t)value->category;
        front->leaf_constants[record] = (uint8_t)value->constant_kind;
        if (wide)
        {
            front->wide_values[front->wide_count] = *value;
            front->wide_count += 1;
            front->wides[record] = (uint16_t)front->wide_count;
        }
    }
    return record;
}

// The literal `token` decoded as check_input_literal decodes it, where that is all it does: false for a literal it reports,
// warns of or makes a type for (an embedded resource, a bit-precise constant, a character constant with an escape or of
// several characters, a literal with a warning's event), which is the reader's own.
static bool node_front_literal(CheckInputReader* reader, uint32_t token, CheckLiteral* result)
{
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const uint8_t* bytes = 0;
    uint32_t length = 0;
    const uint32_t name = parser->tokens.names[token];
    uint32_t embed_length = 0;
    bool plain = false;
    *result = (CheckLiteral){ 0 };
    if (parse_embed_bytes(parser, token, &embed_length) != NULL)
    {
        plain = false;
    }
    else if (name == C_NAME_TRUE || name == C_NAME_FALSE)
    {
        result->type = BUSTER_TYPE_BOOL;
        result->integer = name == C_NAME_TRUE;
        result->valid = true;
        plain = true;
    }
    else if (name == C_NAME_NULLPTR || name == C_NAME_GNU_NULLPTR)
    {
        result->type = BUSTER_TYPE_NULLPTR;
        result->valid = true;
        plain = true;
    }
    else if (name == 0 && check_input_spelling(reader, token, &bytes, &length))
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_CHARACTER)
        {
            bool escaped = false;
            for (uint32_t i = 0; i < length; i += 1)
            {
                escaped = escaped || bytes[i] == '\\';
            }
            *result = check_literal_character(bytes, length, reader->pp->options.language.standard >= 2023, reader->types->char_unsigned);
            plain = result->valid && !escaped && result->characters <= 1;
        }
        else
        {
            check_literal_number(bytes, length, reader->scratch, result);
            plain = result->valid;
        }
    }
    CheckWarningEvent event;
    plain = plain && result->valid && result->bit_width == 0 && !result->resource && !check_literal_warning_event(result, token, &event);
    arena_pop_to(reader->scratch, mark);
    return plain;
}

// A call's library mark, as the walk's call gives it (check_input_expression_call): the plain library builtin its callee's
// declaration is, where the callee is a name or the address of one.
static uint32_t node_front_library(NodeFront* front, CheckInputReader* reader, uint32_t callee)
{
    const uint32_t use = front->uses[callee];
    uint32_t library = use != BUSTER_CHECK_NONE ? check_input_library_call(reader, use) : 0;
    library = library != 0 && check_input_no_builtin(reader, reader->parser->tokens.names[use], use) ? 0 : library;
    return library;
}

// Before a chunk: the records the waiting values still name kept, in their order, and every other one dropped. A
// condition's value waits for its statement's body (the tree puts a control's closing after it). A kept record was
// compared in its own chunk, so only what a later record reads of it stays: its place in the postorder (an operand's
// distance), its node (the comparison of its consumer), and its operator, token and use (a callee's library mark).
static void node_front_keep(NodeFront* front)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < front->stack_count; i += 1)
    {
        const uint32_t record = front->stack[i];
        if (record != BUSTER_NODE_FRONT_UNKNOWN)
        {
            front->ops[kept] = front->ops[record];
            front->tokens[kept] = front->tokens[record];
            front->ordinals[kept] = front->ordinals[record];
            front->uses[kept] = front->uses[record];
            front->nodes[kept] = front->nodes[record];
            front->stack[i] = kept;
            kept += 1;
        }
    }
    front->count = kept;
    front->pending = kept;
    front->argument_count = 0;
    front->wide_count = 0;
}

// The front's pass over a chunk's `acting` tokens and classes, in the walk's order, with the chunk's identifiers' and
// literals' entries (check_input_expression_identifiers, check_input_expression_literals): each token's record, or
// none, by its class.
static void node_front_chunk(NodeFront* front, CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes, uint32_t acting,
                             const CheckInputIdentifiers* identifiers, const CheckInputLiterals* literals)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    NodeFrontShadow* shadow = front->shadow;
    const uint32_t token_count = parser->tokens.count;
    const uint64_t start = __rdtsc();
    node_front_keep(front);
    if (front->count + BUSTER_CHECK_EXPRESSION_CHUNK > BUSTER_NODE_FRONT_RECORDS)
    {
        // More values wait than the arrays keep beside a chunk's records.
        node_front_forget(front);
        front->count = 0;
        front->pending = 0;
    }
    uint32_t identifier = 0;
    uint32_t literal = 0;
    uint32_t values[BUSTER_NODE_FRONT_VALUES];
    for (uint32_t i = 0; i < acting; i += 1)
    {
        const uint32_t token = tokens[i];
        const uint32_t code = (classes[i] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
        // Whether the front leaves the token, and whether the token has a value for its consumer: its record's, or an
        // unknown one where it is left or the arrays are full.
        uint32_t record = BUSTER_NODE_FRONT_UNKNOWN;
        bool value = false;
        bool left = false;
        front->taken_count = 0;
        switch (code)
        {
            case CHECK_INPUT_CODE_LIST:
            case CHECK_INPUT_CODE_SEPARATOR:
            case CHECK_INPUT_CODE_GROUP:
                node_front_pass(front, reader, token);
                break;
            case CHECK_INPUT_CODE_IDENTIFIER:
            {
                const uint32_t at = identifier;
                const uint32_t status = identifiers->status[at];
                identifier += 1;
                left = (status & BUSTER_CHECK_IDENTIFIER_ACTED) == 0;
                value = left || (status & BUSTER_CHECK_IDENTIFIER_NODE) != 0;
                if (!left && value)
                {
                    const CheckValue leaf =
                    {
                        .type = (TypeId)identifiers->types[at],
                        .category = (CheckCategory)(identifiers->types[at] >> 32),
                        .flags = (uint32_t)identifiers->flags[at],
                        .constant_kind = (CheckConstantKind)(identifiers->flags[at] >> 32),
                        .integer = (status & BUSTER_CHECK_IDENTIFIER_ENUMERATOR) != 0 ? reader->enum_values[identifiers->declarations[at]] : 0,
                        .object = (uint32_t)identifiers->objects[at],
                        .overload_head = (uint32_t)(identifiers->objects[at] >> 32),
                    };
                    record = node_front_leaf(front, token, &leaf);
                }
                break;
            }
            case CHECK_INPUT_CODE_LITERAL:
            {
                const uint32_t at = literal;
                literal += 1;
                CheckLiteral decoded = { .type = BUSTER_TYPE_INT, .integer = literals->values[at], .valid = true };
                left = check_input_function_name_token(parser, token) || (literals->values[at] == BUSTER_CHECK_NONE && !node_front_literal(reader, token, &decoded));
                value = true;
                if (!left)
                {
                    const CheckValue leaf = check_input_literal_value(reader, token, decoded);
                    record = node_front_leaf(front, token, &leaf);
                }
                break;
            }
            case CHECK_INPUT_CODE_STRING:
            {
                const CheckInitString* string = check_input_string(reader, token);
                value = string != 0;
                if (value)
                {
                    const CheckValue leaf =
                    {
                        .type = string->type,
                        .category = CHECK_LVALUE,
                        .flags = BUSTER_CHECK_STATIC_OBJECT | (string->object != 0 ? BUSTER_CHECK_NONNULL_ADDRESS : 0),
                        .constant_kind = CHECK_CONSTANT_ADDRESS,
                        .object = string->object,
                    };
                    record = node_front_leaf(front, token, &leaf);
                }
                break;
            }
            case CHECK_INPUT_CODE_PREFIX:
            case CHECK_INPUT_CODE_POSTFIX:
            case CHECK_INPUT_CODE_BINARY:
            case CHECK_INPUT_CODE_CAST:
            case CHECK_INPUT_CODE_SUBSCRIPT:
            case CHECK_INPUT_CODE_TERNARY:
            case CHECK_INPUT_CODE_MEMBER:
            {
                const uint32_t count = node_front_values(front, token, values);
                const uint8_t kind = parser->tokens.kinds[token];
                CheckOp op = CHECK_OP_COUNT;
                uint32_t wanted = 1;
                uint32_t member = 0;
                switch (code)
                {
                    case CHECK_INPUT_CODE_PREFIX:
                        switch (kind)
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
                        break;
                    case CHECK_INPUT_CODE_POSTFIX:
                        op = kind == PARSE_KIND_PLUSPLUS ? CHECK_OP_POST_INCREMENT : kind == PARSE_KIND_MINUSMINUS ? CHECK_OP_POST_DECREMENT : CHECK_OP_COUNT;
                        break;
                    case CHECK_INPUT_CODE_BINARY:
                        op = check_input_binary(kind);
                        wanted = 2;
                        break;
                    case CHECK_INPUT_CODE_CAST:
                        op = reader->query_types[token] != 0 ? CHECK_OP_CAST : CHECK_OP_COUNT;
                        break;
                    case CHECK_INPUT_CODE_SUBSCRIPT:
                        op = CHECK_OP_SUBSCRIPT;
                        wanted = 2;
                        break;
                    case CHECK_INPUT_CODE_TERNARY:
                        op = CHECK_OP_CONDITIONAL;
                        wanted = count == 2 ? 2 : 3;
                        break;
                    default:
                        if (token + 1 < token_count && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER)
                        {
                            op = kind == PARSE_KIND_ARROW ? CHECK_OP_ARROW : CHECK_OP_MEMBER;
                            member = parser->tokens.names[token + 1];
                        }
                        break;
                }
                left = op == CHECK_OP_COUNT || count != wanted;
                value = true;
                if (!left)
                {
                    // A conditional with its middle omitted (x ?: y) takes its first value twice.
                    const bool omitted = code == CHECK_INPUT_CODE_TERNARY && count == 2;
                    const uint32_t b = omitted ? values[0] : count >= 2 ? values[1] : BUSTER_NODE_FRONT_UNKNOWN;
                    const uint32_t c = omitted ? values[1] : count == 3 ? values[2] : BUSTER_NODE_FRONT_UNKNOWN;
                    record = node_front_add(front, op, token, values[0], b, c, CHECK_INPUT_FORM_OPERATOR, reader->query_types[token]);
                    if (record != BUSTER_NODE_FRONT_UNKNOWN)
                    {
                        front->members[record] = member;
                    }
                }
                break;
            }
            case CHECK_INPUT_CODE_CALL:
            {
                const uint32_t count = node_front_values(front, token, values);
                left = count == BUSTER_NODE_FRONT_UNKNOWN || count == 0 || input->kr_parameter_count != 0 || input->no_builtin ||
                       front->argument_count + count > BUSTER_NODE_FRONT_RECORDS;
                value = true;
                if (!left)
                {
                    record = node_front_add(front, CHECK_OP_CALL, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, CHECK_INPUT_FORM_OPERATOR,
                                            reader->query_types[token]);
                    if (record != BUSTER_NODE_FRONT_UNKNOWN)
                    {
                        front->members[record] = node_front_library(front, reader, values[0]);
                        front->argument_firsts[record] = front->argument_count;
                        front->argument_counts[record] = count - 1;
                        for (uint32_t k = 1; k < count; k += 1)
                        {
                            front->arguments[front->argument_count] = values[k];
                            front->argument_count += 1;
                        }
                    }
                }
                break;
            }
            case CHECK_INPUT_CODE_TERMINATOR:
            {
                // Each of its values discarded, a CAST to void, or tested in a condition's context, as the walk's
                // terminator does; a statement expression's last statement gives the same nodes.
                const uint32_t count = node_front_values(front, token, values);
                const bool condition = reader->conditions[token] != 0;
                bool region = false;
                if (input->region_count != 0)
                {
                    const uint32_t parent = parser->tree.parents[token];
                    uint32_t selected = parent < token_count ? input->expression_regions[parent] : 0;
                    if (selected == 0 && parent < token_count && parser->tree.roles[parent] == PARSE_ROLE_LABEL)
                    {
                        selected = check_input_labeled_region(reader, parent);
                    }
                    region = selected != 0 && input->regions[selected - 1].final_statement == token;
                }
                left = count == BUSTER_NODE_FRONT_UNKNOWN || (condition && count > 1) || (region && count != 1);
                for (uint32_t k = 0; !left && k < count; k += 1)
                {
                    (void)node_front_add(front, condition ? CHECK_OP_CONDITION : CHECK_OP_CAST, token, values[k], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN,
                                         CHECK_INPUT_FORM_DISCARD, BUSTER_TYPE_VOID);
                }
                break;
            }
            case CHECK_INPUT_CODE_CONDITION:
            {
                const uint32_t count = node_front_values(front, token, values);
                left = count != 1;
                if (!left)
                {
                    const uint32_t keyword = reader->first_child[token];
                    const bool switched = keyword < token_count && parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH;
                    record = node_front_add(front, switched ? CHECK_OP_POSITIVE : CHECK_OP_CONDITION, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN,
                                            CHECK_INPUT_FORM_OPERATOR, reader->query_types[token]);
                    if (record != BUSTER_NODE_FRONT_UNKNOWN)
                    {
                        front->roots[record] = 1;
                    }
                }
                break;
            }
            case CHECK_INPUT_CODE_RETURN:
            {
                const uint32_t count = node_front_values(front, token, values);
                const TypeId type = reader->return_types[token];
                left = type == 0 || count == BUSTER_NODE_FRONT_UNKNOWN || count > 1;
                if (!left && count == 1)
                {
                    record = node_front_add(front, CHECK_OP_CONVERT, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, CHECK_INPUT_FORM_OPERATOR, type);
                    if (record != BUSTER_NODE_FRONT_UNKNOWN)
                    {
                        front->roots[record] = 1;
                    }
                }
                break;
            }
            case CHECK_INPUT_CODE_INITIALIZE:
            {
                // A braced initializer's assignment passes its values on.
                if (reader->initializers[token] == 2)
                {
                    node_front_pass(front, reader, token);
                }
                else
                {
                    const uint32_t count = node_front_values(front, token, values);
                    left = count != 1;
                    value = true;
                    if (!left)
                    {
                        record = node_front_add(front, CHECK_OP_INITIALIZE, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, CHECK_INPUT_FORM_INITIALIZER,
                                                reader->query_types[token]);
                    }
                }
                break;
            }
            default:
            {
                // The walk's own visit, whose values and nodes the front doesn't know.
                (void)node_front_values(front, token, values);
                left = true;
                value = true;
                break;
            }
        }
        shadow->left[code] += left;
        if (left)
        {
            shadow->left_queries += parser->tokens.kinds[token] == PARSE_KIND_SIZEOF;
            node_front_taken_by_left(front);
        }
        if (value)
        {
            node_front_give(front, reader, token, left ? BUSTER_NODE_FRONT_UNKNOWN : record);
        }
    }
    shadow->tokens += acting;
    shadow->records += front->count - front->pending;
    shadow->ticks += __rdtsc() - start;
}

// The front's own records among the walk's nodes strictly between two of them, counted up to `limit`.
static uint32_t node_front_made_between(const NodeFrontShadow* shadow, uint32_t low, uint32_t high, uint32_t limit)
{
    uint32_t made = 0;
    for (uint32_t node = low + 1; node < high && node < shadow->node_capacity && made < limit; node += 1)
    {
        made += (uint32_t)(shadow->nodes[node >> 6] >> (node & 63) & 1);
    }
    return made;
}

// The records of the chunk just read compared with the walk's nodes, after the walk acted at the chunk's tokens: each
// record's node is its token's (a discard's the one the walk numbered after the terminator's last value), with the
// record's operator, its operands' nodes, its leaf's value, its member, its arguments, its requested type and its
// context. A record that agrees marks its node (NodeFrontShadow.nodes).
static void node_front_compare(NodeFront* front, const CheckInputReader* reader)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    NodeFrontShadow* shadow = front->shadow;
    for (uint32_t record = front->pending; record < front->count; record += 1)
    {
        const uint32_t token = front->tokens[record];
        const CheckInputNodeForm form = (CheckInputNodeForm)front->forms[record];
        const bool discard = form == CHECK_INPUT_FORM_DISCARD;
        const uint32_t a = front->operands[0][record];
        const uint32_t operand = a != BUSTER_NODE_FRONT_UNKNOWN ? front->nodes[a] : BUSTER_CHECK_NONE;
        uint32_t node = BUSTER_CHECK_NONE;
        if (discard)
        {
            // The walk numbers a discard when it acts at the terminator, as the front makes its record: after the node of
            // the record before it, or else after its value's node (a for's step is discarded after the loop's body).
            const uint32_t before = record != 0 ? front->nodes[record - 1] : BUSTER_CHECK_NONE;
            node = before != BUSTER_CHECK_NONE ? before + 1 : operand != BUSTER_CHECK_NONE ? operand + 1 : BUSTER_CHECK_NONE;
            while (node < graph->count && operand != BUSTER_CHECK_NONE && (graph->tokens[node] != token || graph->operand0[node] != operand))
            {
                node += 1;
            }
        }
        else
        {
            node = input->token_nodes[token];
        }
        uint32_t field = 0;
        if (node >= graph->count || graph->tokens[node] != token)
        {
            field = BUSTER_NODE_FRONT_FIELD_NODE;
        }
        else if (graph->ops[node] != (CheckOp)front->ops[record])
        {
            field = BUSTER_NODE_FRONT_FIELD_OPERATOR;
        }
        else if (form == CHECK_INPUT_FORM_LEAF)
        {
            const CheckValue theirs = check_graph_leaf_value(graph, node);
            const uint16_t wide = front->wides[record];
            bool same = false;
            if (wide != 0)
            {
                const CheckValue* mine = front->wide_values + wide - 1;
                same = mine->type == theirs.type && mine->category == theirs.category && mine->flags == theirs.flags && mine->constant_kind == theirs.constant_kind &&
                       mine->integer == theirs.integer && mine->floating_storage[0] == theirs.floating_storage[0] &&
                       (mine->floating_storage[1] & 0xffff) == (theirs.floating_storage[1] & 0xffff) && mine->imaginary_storage[0] == theirs.imaginary_storage[0] &&
                       (mine->imaginary_storage[1] & 0xffff) == (theirs.imaginary_storage[1] & 0xffff) && mine->object == theirs.object &&
                       mine->overload_head == theirs.overload_head && mine->offset == theirs.offset && mine->bit_width == theirs.bit_width && mine->fault == theirs.fault &&
                       mine->fault_node == theirs.fault_node && mine->object_bit_offset == theirs.object_bit_offset;
            }
            else
            {
                same = !check_graph_leaf_rest(&theirs) && front->leaf_types[record] == theirs.type && front->leaf_categories[record] == (uint8_t)theirs.category &&
                       front->leaf_flags[record] == theirs.flags && front->leaf_constants[record] == (uint8_t)theirs.constant_kind &&
                       front->leaf_integers[record] == (uint64_t)theirs.integer && front->leaf_objects[record] == theirs.object &&
                       front->leaf_heads[record] == theirs.overload_head;
            }
            field = same ? 0 : BUSTER_NODE_FRONT_FIELD_LEAF;
        }
        else
        {
            const uint32_t b = front->operands[1][record];
            const uint32_t c = front->operands[2][record];
            const uint32_t second = b != BUSTER_NODE_FRONT_UNKNOWN ? front->nodes[b] : BUSTER_CHECK_NONE;
            const uint32_t third = c != BUSTER_NODE_FRONT_UNKNOWN ? front->nodes[c] : BUSTER_CHECK_NONE;
            const bool operands = operand != BUSTER_CHECK_NONE && graph->operand0[node] == operand && graph->operand1[node] == second && graph->operand2[node] == third &&
                                  (b == BUSTER_NODE_FRONT_UNKNOWN || second != BUSTER_CHECK_NONE) && (c == BUSTER_NODE_FRONT_UNKNOWN || third != BUSTER_CHECK_NONE);
            bool arguments = graph->argument_counts[node] == front->argument_counts[record];
            for (uint32_t k = 0; arguments && k < front->argument_counts[record]; k += 1)
            {
                const uint32_t argument = front->nodes[front->arguments[front->argument_firsts[record] + k]];
                arguments = argument != BUSTER_CHECK_NONE && graph->argument_nodes[graph->argument_offsets[node] + k] == argument;
            }
            // An operand's distance against the walk's two nodes, which the walk numbers in the same postorder: one more
            // than the front's own records between them (the walk also numbers the nodes of the tokens the front left).
            const uint32_t nodes[3] = { operand, second, third };
            bool distances = true;
            bool past = false;
            for (uint32_t k = 0; operands && k < 3; k += 1)
            {
                const uint32_t distance = front->distances[k][record];
                const bool present = front->operands[k][record] != BUSTER_NODE_FRONT_UNKNOWN;
                if (present && front->roots[record] == 0)
                {
                    shadow->operands += 1;
                    shadow->operands_near += distance <= BUSTER_NODE_FRONT_NEAR;
                    shadow->operands_byte += distance > BUSTER_NODE_FRONT_NEAR && distance < BUSTER_NODE_FRONT_FAR;
                    shadow->operands_far += distance == BUSTER_NODE_FRONT_FAR;
                    past = past || distance > BUSTER_NODE_FRONT_NEAR;
                }
                else if (present)
                {
                    shadow->root_operands += 1;
                    shadow->root_operands_past += distance > BUSTER_NODE_FRONT_NEAR;
                }
                const uint32_t between = present ? node_front_made_between(shadow, nodes[k], node, BUSTER_NODE_FRONT_FAR) + 1 : 0;
                distances = distances && (!present || (distance == BUSTER_NODE_FRONT_FAR ? between >= BUSTER_NODE_FRONT_FAR : between == distance));
            }
            // A call's arguments, which its record lists beside its operands: their distances are counted, not kept.
            for (uint32_t k = 0; operands && arguments && k < front->argument_counts[record]; k += 1)
            {
                const uint32_t distance = front->ordinals[record] - front->ordinals[front->arguments[front->argument_firsts[record] + k]];
                shadow->arguments += 1;
                shadow->arguments_past += distance > BUSTER_NODE_FRONT_NEAR;
                past = past || distance > BUSTER_NODE_FRONT_NEAR;
            }
            field = !operands ? BUSTER_NODE_FRONT_FIELD_OPERAND : graph->members[node] != front->members[record] ? BUSTER_NODE_FRONT_FIELD_MEMBER :
                    !arguments ? BUSTER_NODE_FRONT_FIELD_ARGUMENTS : graph->types[node] != front->types[record] ? BUSTER_NODE_FRONT_FIELD_TYPE :
                    !distances ? BUSTER_NODE_FRONT_FIELD_DISTANCE : 0;
            if (field == 0 && past && node < shadow->node_capacity)
            {
                shadow->past[node >> 6] |= UINT64_C(1) << (node & 63);
            }
        }
        shadow->checked += 1;
        if (field == 0)
        {
            // Its context, as the flush gives a node its own and a list its values': compared at the walk's end
            // (node_front_finish).
            const bool statics = (form == CHECK_INPUT_FORM_OPERATOR || form == CHECK_INPUT_FORM_INITIALIZER) && reader->static_initializers[token];
            const uint8_t context = (uint8_t)((statics && form == CHECK_INPUT_FORM_OPERATOR ? BUSTER_CHECK_CONTEXT_STATIC : 0) |
                                              (statics && form == CHECK_INPUT_FORM_INITIALIZER ? BUSTER_CHECK_CONTEXT_VALUE : 0) | front->contexts[record]);
            front->nodes[record] = node;
            if (node < shadow->node_capacity)
            {
                shadow->nodes[node >> 6] |= UINT64_C(1) << (node & 63);
                shadow->contexts[node] = front->taken_by_left[record] != 0 ? BUSTER_NODE_FRONT_CONTEXT_UNKNOWN : context;
            }
        }
        else
        {
            if (shadow->differences < BUSTER_NODE_FRONT_LISTED)
            {
                shadow->difference_tokens[shadow->differences] = token;
                shadow->difference_fields[shadow->differences] = field;
            }
            shadow->differences += 1;
        }
    }
    front->pending = front->count;
}

// After the walk's last chunk: each agreed node's context compared with the graph's, final now. A node whose context
// differs is a difference, and no longer the front's.
static void node_front_finish(NodeFront* front, const CheckInputReader* reader, uint32_t base)
{
    const CheckGraph* graph = &reader->output->graph;
    NodeFrontShadow* shadow = front->shadow;
    shadow->waiting += front->stack_count;
    for (uint32_t node = base; node < graph->count && node < shadow->node_capacity; node += 1)
    {
        const bool agreed = (shadow->nodes[node >> 6] >> (node & 63) & 1) != 0;
        if (agreed && shadow->contexts[node] != BUSTER_NODE_FRONT_CONTEXT_UNKNOWN && graph->contexts[node] != shadow->contexts[node])
        {
            if (shadow->differences < BUSTER_NODE_FRONT_LISTED)
            {
                shadow->difference_tokens[shadow->differences] = graph->tokens[node];
                shadow->difference_fields[shadow->differences] = BUSTER_NODE_FRONT_FIELD_CONTEXT;
            }
            shadow->differences += 1;
            shadow->nodes[node >> 6] &= ~(UINT64_C(1) << (node & 63));
        }
    }
}
