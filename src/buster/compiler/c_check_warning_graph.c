// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source conversion facts after checking, before initializer/liveness pruning. Select candidate opcodes in
// sixteen-node blocks; scalar is the independent traversal twin. Only established warning facts allocate storage.
// Stage a new immutable suffix so failure cannot publish an event pointer into a rolled-back arena.

static uint32_t check_warning_graph_first(const CheckGraph* graph, const Parser* parser, uint32_t node)
{
    uint32_t first = graph->tokens[node];
    const uint32_t end = graph->source_positions[node];
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        uint32_t token = graph->tokens[node];
        const uint8_t role = parser->tree.roles[token];
        if (role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_CLOSE_KEYWORD)
        {
            uint32_t open = token;
            while (open != 0 && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == token))
            {
                open -= 1;
            }
            if (parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == token)
            {
                token = role == PARSE_ROLE_CLOSE_KEYWORD && open != 0 ? open - 1 : open;
            }
        }
        first = token < first ? token : first;
        const uint32_t child = graph->operand0[node];
        if (child >= node || graph->tokens[child] >= first)
        {
            break;
        }
        node = child;
    }
    while (first != 0 && parser->tokens.kinds[first - 1] == PARSE_KIND_L_PAREN &&
           parser->tree.parents[first - 1] >= first && parser->tree.parents[first - 1] < end)
    {
        first -= 1;
    }
    return first;
}

// `lvalue` is the token naming the conversion's destination — an INITIALIZE's `=` (whose graph type carries
// the declared type, so `declared` passes it), an ASSIGN's left operand's root, or 0 where no declaration is
// reachable (#992).
// Always inlined (#880), as profile-guided builds do; hot caller: check_input_warning_graph.
// A call's argument (`call`, its CALL node, and its index `argument`; BUSTER_CHECK_NONE otherwise) prints its parameter's
// typedef (#1100, check_input_parameter_spelling).
static inline __attribute__((always_inline)) void check_warning_graph_null(CheckInputReader* reader, const CheckStream* stream, uint32_t source, TypeId destination,
                                     uint32_t lvalue, TypeId declared, uint32_t completion, uint32_t call, uint32_t argument)
{
    const CheckGraph* graph = &reader->output->graph;
    const CheckValue value = source < graph->count ? check_stream_value(stream, source) : (CheckValue){ 0 };
    if (source < graph->count && check_warning_null_integer(reader->types, destination, &value))
    {
        uint32_t first = check_warning_graph_first(graph, reader->parser, source);
        uint32_t end = graph->source_positions[source];
        uint32_t root = graph->tokens[source];
        for (uint32_t steps = 0; root < reader->parser->tokens.count && steps < reader->parser->tokens.count; steps += 1)
        {
            const uint32_t parent = reader->parser->tree.parents[root];
            if (parent >= completion || reader->parser->tree.roles[parent] != PARSE_ROLE_CLOSE_GROUP)
            {
                break;
            }
            uint32_t open = root;
            while (open != 0 && !(reader->parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && reader->parser->tree.parents[open] == parent))
            {
                open -= 1;
            }
            first = open < first ? open : first;
            end = parent + 1 > end ? parent + 1 : end;
            root = parent;
        }
        reader->warning_bound_open = BUSTER_CHECK_NONE;
        for (uint32_t vla = 0; vla < reader->output->vla_count; vla += 1)
        {
            const CheckInputVla* bound = reader->output->vlas + vla;
            if (bound->bound_begin <= first && end <= bound->bound_end &&
                (reader->warning_bound_open == BUSTER_CHECK_NONE || reader->warning_bound_open < bound->bound_begin))
            {
                reader->warning_bound_open = bound->bound_begin;
            }
        }
        // The destination operand's own spelling (#992): the lvalue's declared type, qualifiers kept, and its
        // typedef specifier's declaration where its base was a typedef name.
        uint32_t spelling = 0;
        TypeId printed = check_input_object_spelling(reader->types, reader->output, reader->parser, lvalue, declared, destination, &spelling);
        // A member's own declarator's specifiers (#1837): `s.lp = 1-1` of a member `LP lp` converts to 'LP'.
        const CheckInput* input = reader->output;
        const uint32_t lvalue_node = lvalue != 0 && lvalue < input->token_count && input->token_nodes != NULL ? input->token_nodes[lvalue] : BUSTER_CHECK_NONE;
        const bool member = lvalue_node < graph->count && (graph->ops[lvalue_node] == CHECK_OP_MEMBER || graph->ops[lvalue_node] == CHECK_OP_ARROW);
        const uint32_t selected = member ? check_semantic_member_of(reader->parser, input, stream, reader->types, lvalue_node) : 0;
        const uint32_t declarator = selected != 0 ? check_semantic_member_token(reader->parser, input, reader->types, selected) : BUSTER_CHECK_NONE;
        spelling = declarator < input->token_count ? check_input_specifier_spelling(input, reader->parser, declarator) : spelling;
        printed = declarator < input->token_count ? destination : printed;
        spelling = call < graph->count ? check_input_call_parameter_spelling(reader->parser, reader->output, stream, reader->types, call, argument) : spelling;
        check_input_null_spelling(reader, destination, printed, spelling, &value, first, end, completion);
    }
}

// Whether node `operand`'s value can be a pointer once converted as an operand (check_action_operand): a pointer, an array
// or a function, which decay to one, an atomic type, whose value has its base type, or a type that isn't valid. An ordered
// comparison warns only where exactly one operand is a pointer (check_warning_ordered_event), so one of two other operands
// needs no action built, which read every operand's whole value (#1050).
static bool check_warning_graph_pointer_operand(const TypeTable* types, const CheckStream* stream, uint32_t operand)
{
    const TypeId type = check_stream_type(stream, operand);
    bool result = true;
    if (type_is_valid(types, type))
    {
        const uint8_t kind = check_action_kind(types, type);
        result = kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_ATOMIC || type_kind_is_array(kind);
    }
    return result;
}

// Whether node `source`'s value can be a null pointer constant that warns (check_warning_null_integer): an integer constant
// expression of value zero without a fault. Its constant kind and flags, two of its narrow columns, say whether it is one,
// and then its payload row, which a value of columns alone hasn't (its integer is zero, its fault none), its value and
// fault, all before its conversion's record or its whole value is read. On sqlite3.c's -c those two reads were most of
// this pass, as the records are no longer in the caches when it runs (#1050).
static bool check_warning_graph_null_candidate(const CheckGraph* graph, const CheckStream* stream, uint32_t source)
{
    bool result = source < graph->count && check_stream_constant_kind(stream, source) == CHECK_CONSTANT_INTEGER && (check_stream_flags(stream, source) & BUSTER_CHECK_ICE) != 0;
    if (result && (stream->dense_values != NULL || stream->payload_rows[source] != 0))
    {
        const CheckValue value = check_stream_value(stream, source);
        result = value.integer == 0 && value.fault == CHECK_FAULT_NONE;
    }
    return result;
}

// clang's diagnostics of a use of a member that a deprecated or unavailable attribute marks (#147, DiagnoseUseOfDecl's of
// a member expression): at the name of each `.` or `->` whose member is one, "'x' is deprecated"
// (-Wdeprecated-declarations), with the attribute's message after a colon and a note at the attribute, or the error
// "'x' is unavailable" (CHECK_INPUT_UNAVAILABLE_USE), with its note at the member's declaration. The member is the one
// the access's record finds by its name, through anonymous members (type_lookup_member's), as the checker found it. A
// deprecated context drops a deprecated use's and an unavailable one either's (CheckInput.availability_contexts). It runs
// only where some member is marked, after checking: the record's type is the operand's checked one.
static void check_input_member_uses(CheckInputReader* reader, const CheckStream* stream)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    const uint32_t count = parser->tokens.count;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t operand = graph->operand0[node];
        const uint32_t name = graph->tokens[node] + 1;
        if ((op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && stream->diagnostics[node] == CHECK_DIAG_NONE && operand < graph->count && name < count &&
            parser->tree.roles[name] == PARSE_ROLE_MEMBER)
        {
            TypeId record = type_strip_aligned(types, check_stream_type(stream, operand));
            if (op == CHECK_OP_ARROW)
            {
                record = type_kind(types, record) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, record)) : 0;
            }
            const uint8_t kind = record != 0 ? type_kind(types, record) : BUSTER_TYPE_KIND_NONE;
            uint64_t offset = 0;
            uint32_t qualifiers = 0;
            const uint32_t member = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ?
                                    type_lookup_member(types, record, graph->members[node], &offset, &qualifiers) : 0;
            const CheckInputMarkedMember* marked = member != 0 && member != BUSTER_TYPE_MEMBER_UNKNOWN ?
                                                   check_marked_member(input->marked_members, input->marked_member_count, member) : NULL;
            // Once: the uses' pass gave a use the evaluator read too (a variable length array's bound) its diagnostic.
            bool reported = false;
            const uint64_t key = (uint64_t)CHECK_WARNING_EVENT_DEPRECATED_USE << 32 | ((uint64_t)name + 1);
            for (uint32_t r = 0; marked != NULL && marked->unavailable && !reported && r < reader->output->report_count; r += 1)
            {
                reported = reader->output->reports[r].token == name && reader->output->reports[r].message == CHECK_INPUT_UNAVAILABLE_USE;
            }
            reported |= marked != NULL && !marked->unavailable && check_warning_set_contains(&reader->source_warning_keys, key);
            if (marked != NULL && !reported)
            {
                check_input_marked_member_use(reader, name, marked, 0);
            }
        }
    }
}

// clang's warning at a call that resolves to an overloadable function's overload a deprecated attribute marks (#147,
// DiagnoseUseOfDecl's of the selected overload): "'f' is deprecated" (-Wdeprecated-declarations) at the callee's name,
// with the attribute's message and the note at it, where the name's context doesn't drop it
// (CheckInput.availability_contexts). The selections are the stream's (CheckStream.overloads), and an address taken
// through the overload set isn't one, as clang doesn't warn of it.
static void check_input_overload_uses(CheckInputReader* reader, const CheckStream* stream)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    for (uint32_t i = 0; i < stream->overload_count && stream->overloads != NULL; i += 1)
    {
        const uint32_t node = stream->overloads[i].node;
        const CheckOverloadResult* selected = &stream->overloads[i].selected;
        const uint32_t callee = node < graph->count && graph->ops[node] == CHECK_OP_CALL ? graph->operand0[node] : BUSTER_CHECK_NONE;
        const uint32_t name = callee < graph->count ? graph->tokens[callee] : count;
        const CheckInputMarkedMember* marked = selected->status == CHECK_OVERLOAD_OK && stream->diagnostics[node] == CHECK_DIAG_NONE && name < count &&
                                               parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER ?
                                               check_marked_member(input->marked_overloads, input->marked_overload_count, selected->declaration) : NULL;
        const uint8_t context = marked != NULL && input->availability_contexts != NULL ? input->availability_contexts[name] : 0;
        if (marked != NULL && !check_marked_dropped(marked, context))
        {
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_DEPRECATED_DECLARATIONS,
                .kind = CHECK_WARNING_EVENT_DEPRECATED_USE,
                .token = name,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = name + 1,
                .payload = { .name = parser->tokens.names[name], .token = marked->message },
            };
            const CheckWarningNote note =
            {
                .kind = CHECK_WARNING_NOTE_DEPRECATED_HERE,
                .token = marked->attribute,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .name = parser->tokens.names[marked->name], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            check_input_warning_event(reader, &event, &note, 1);
        }
    }
}

#include "c_check_noderef_graph.c"
#include "c_check_condition_graph.c"

// Whether graph node `node` is a call clang's BuildResolvedCallExpr sees (#147, check_input_interrupt_calls): a call, or a
// builtin's, but those clang parses as no call (va_arg, convertvector, bit_cast and __builtin_available, keywords' forms).
static bool check_interrupt_call(const CheckGraph* graph, uint32_t node)
{
    const CheckOp op = graph->ops[node];
    const uint32_t member = graph->members != NULL ? graph->members[node] : 0;
    const bool builtin = op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW || op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY ||
                         op == CHECK_OP_BUILTIN_ATOMIC || op == CHECK_OP_BUILTIN_X86 || (op == CHECK_OP_BUILTIN_MISC && member != CBUILTIN_AVAILABLE) ||
                         (op == CHECK_OP_VARARGS && member != CBUILTIN_VA_ARG) || (op == CHECK_OP_BUILTIN_VECTOR && member != CBUILTIN_CONVERTVECTOR);
    return op == CHECK_OP_CALL || builtin;
}

// The first token below `limit` whose name is `name`, or `limit`: where clang looks a builtin's name up first, and so
// declares it, which its notes name (#147). The vector path compares 16 tokens' names at a time; the scalar path, its
// twin, each token's.
static uint32_t check_interrupt_first_use(const Parser* parser, uint32_t name, uint32_t limit, bool scalar)
{
    (void)scalar;

    uint32_t result = limit;
    uint32_t t = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && result == limit && limit - t >= 16; t += 16)
    {
        const __mmask16 equal = _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(parser->tokens.names + t), _mm512_set1_epi32((int)name));
        result = equal != 0 ? t + (uint32_t)__builtin_ctz(equal) : limit;
    }
    for (; result == limit && t < limit; t += 1)
    {
        result = parser->tokens.names[t] == name ? t : limit;
    }
    return result;
}

// A body of a function x86's interrupt or no_caller_saved_registers marks (#147): its braces, and which (0 interrupt,
// whose message clang gives where both do, 1 no_caller_saved_registers).
typedef struct CheckInterruptBody CheckInterruptBody;
struct CheckInterruptBody
{
    uint32_t open;
    uint32_t close;
    uint32_t caller;
};

// What check_input_interrupt_calls looks calls up in (#147): per object, its first declaration interrupt marks and its
// first no_caller_saved_registers marks; the bodies they mark, in token order; and the unit's reports before the pass.
typedef struct CheckInterruptCalls CheckInterruptCalls;
struct CheckInterruptCalls
{
    const uint32_t* interrupts;
    const uint32_t* saving;
    // Per declaring token, in a unit with CheckInput.redeclaration_links (#1447): bit 0 where interrupt marks its
    // declaration's chain at it, bit 1 where no_caller_saved_registers does. Null otherwise.
    const uint8_t* marks;
    const CheckInterruptBody* bodies;
    uint32_t filled;
    uint32_t report_begin;
};

// Whether `token` is a keyword whose parenthesized head holds a full expression (#1447): if, while, switch, for.
static bool check_interrupt_head(const Parser* parser, uint32_t token)
{
    const uint8_t kind = parser->tokens.kinds[token];
    return kind == PARSE_KIND_IF || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_SWITCH || kind == PARSE_KIND_FOR;
}

// Whether the declaration `declaration` of `object`, a function's, has interrupt (`which` 0) or no_caller_saved_registers
// (1) at it, as clang's redeclarations inherit them from the declaration their lookup found (#147): an earlier declaration
// of its object has it (CheckInterruptCalls.interrupts or saving), or, in a unit whose lookups found other declarations
// than their entities' latest, its chain does (CheckInterruptCalls.marks, #1447).
static bool check_interrupt_marked(const CheckInterruptCalls* calls, uint32_t object, uint32_t declaration, uint32_t which)
{
    const uint32_t first = which == 0 ? calls->interrupts[object] : calls->saving[object];
    return calls->marks != NULL ? (calls->marks[declaration] >> which & 1u) != 0 : first <= declaration;
}

// Appends a range of a direct call's full expression (#1447, CheckInput.interrupt_ranges): its tokens from `first` to `last`,
// which drop the other diagnostics there, or with `keep` its operands that don't hold the call (another argument, another
// list element), which keep theirs; `owner` the call's callee's first token, whose own ranges they are.
static void check_interrupt_range(CheckInputReader* reader, uint32_t first, uint32_t last, uint32_t owner, bool keep)
{
    CheckInput* input = reader->output;
    if (input->interrupt_range_count == input->interrupt_range_capacity && !input->resource)
    {
        const uint32_t capacity = input->interrupt_range_capacity != 0 ? input->interrupt_range_capacity * 2 : 4;
        uint32_t* grown = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, (uint64_t)capacity * 3);
        input->resource |= grown == NULL;
        if (grown != NULL && input->interrupt_range_count != 0)
        {
            memcpy(grown, input->interrupt_ranges, (uint64_t)input->interrupt_range_count * 3 * sizeof(uint32_t));
        }
        input->interrupt_ranges = grown != NULL ? grown : input->interrupt_ranges;
        input->interrupt_range_capacity = grown != NULL ? capacity : input->interrupt_range_capacity;
    }
    if (input->interrupt_range_count < input->interrupt_range_capacity && first <= last && last < BUSTER_CHECK_INTERRUPT_KEEP)
    {
        input->interrupt_ranges[input->interrupt_range_count * 3] = first | (keep ? BUSTER_CHECK_INTERRUPT_KEEP : 0u);
        input->interrupt_ranges[input->interrupt_range_count * 3 + 1] = last;
        input->interrupt_ranges[input->interrupt_range_count * 3 + 2] = owner;
        input->interrupt_range_count += 1;
    }
}

// Records the full expression around a direct call's error, from its callee's first token `first` to its `)` `close`
// (#1447, CheckInput.interrupt_ranges). Out through the groups around it: a call's or an initializer list's keeps its
// other arguments or elements, which clang builds whatever this one is (check_interrupt_range's `keep`); any other group
// (parentheses, a subscript, _Generic's) is the call's expression's. It ends at its statement (a `;`, a block's braces),
// a control statement's head's parentheses, or the declarator's initializer that holds it (CheckInput.initializer_begins
// and initializer_ends): another declarator's is its own full expression. A cold scan of the tokens around the rare error.
static void check_interrupt_full_expression(CheckInputReader* reader, uint32_t first, uint32_t close)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t low = first;
    uint32_t high = close;
    uint32_t begin = 0;
    bool top = false;
    for (uint32_t steps = 0; !top && steps < count; steps += 1)
    {
        // The opener around the tokens from `low` on, or the statement's boundary before them.
        uint32_t depth = 0;
        uint32_t t = low;
        uint32_t opener = BUSTER_CHECK_NONE;
        // The `;` or `}` that ends the statement before, or the unit's start (BUSTER_CHECK_NONE).
        uint32_t stop = BUSTER_CHECK_NONE;
        bool boundary = false;
        while (opener == BUSTER_CHECK_NONE && !boundary)
        {
            boundary = t == 0;
            t -= boundary ? 0 : 1;
            const uint8_t kind = boundary ? PARSE_KIND_SEMI : parser->tokens.kinds[t];
            const bool closing = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
            const bool opening = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
            const bool ends = !boundary && depth == 0 && (kind == PARSE_KIND_SEMI || kind == PARSE_KIND_R_BRACE);
            stop = ends ? t : stop;
            boundary |= ends;
            opener = !boundary && depth == 0 && opening ? t : BUSTER_CHECK_NONE;
            depth += !boundary && closing ? 1 : 0;
            depth -= !boundary && opening && depth != 0 ? 1 : 0;
        }
        const uint8_t role = opener != BUSTER_CHECK_NONE ? parser->tree.roles[opener] : PARSE_ROLE_NONE;
        const bool statement = opener != BUSTER_CHECK_NONE && parser->tokens.kinds[opener] == PARSE_KIND_L_BRACE && role != PARSE_ROLE_OPEN_INITIALIZER;
        const bool condition = opener != BUSTER_CHECK_NONE && opener != 0 && parser->tokens.kinds[opener] == PARSE_KIND_L_PAREN && check_interrupt_head(parser, opener - 1);
        top = boundary || statement || condition;
        begin = boundary ? (stop != BUSTER_CHECK_NONE ? stop + 1 : 0) : opener != BUSTER_CHECK_NONE ? opener + 1 : begin;
        const uint32_t closer = !top ? parser->tree.parents[opener] : BUSTER_CHECK_NONE;
        if (!top && closer < count && closer > high && (role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_OPEN_INITIALIZER))
        {
            // The argument or element that holds the call, between the list's `,`s at its level.
            uint32_t segment_low = opener + 1;
            uint32_t segment_high = closer - 1;
            uint32_t level = 0;
            for (uint32_t k = opener + 1; k < closer; k += 1)
            {
                const uint8_t kind = parser->tokens.kinds[k];
                const bool comma = level == 0 && kind == PARSE_KIND_COMMA;
                segment_low = comma && k < low ? k + 1 : segment_low;
                segment_high = comma && k > high && segment_high == closer - 1 ? k - 1 : segment_high;
                level += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE ? 1 : 0;
                level -= level != 0 && (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE) ? 1 : 0;
            }
            if (segment_low > opener + 1)
            {
                check_interrupt_range(reader, opener + 1, segment_low - 1, first, true);
            }
            if (segment_high < closer - 1)
            {
                check_interrupt_range(reader, segment_high + 1, closer - 1, first, true);
            }
        }
        low = top ? low : opener;
        high = top || closer >= count ? high : closer;
    }
    // The statement's end, or the control head's `)`.
    uint32_t end = high;
    uint32_t depth = 0;
    bool open = true;
    while (open && end + 1 < count)
    {
        const uint32_t t = end + 1;
        const uint8_t kind = parser->tokens.kinds[t];
        const bool closing = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
        const bool opening = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        open = !(depth == 0 && (kind == PARSE_KIND_SEMI || closing));
        depth += opening ? 1 : 0;
        depth -= closing && depth != 0 ? 1 : 0;
        end = open ? t : end;
    }
    // The declarator's initializer that holds the call, the innermost, where it is in one.
    for (uint32_t d = begin; d < first && d < input->token_count; d += 1)
    {
        const uint32_t initializer = check_sparse_get(input->initializer_begins, d);
        const uint32_t ending = initializer != BUSTER_CHECK_NONE ? check_sparse_get(input->initializer_ends, d) : BUSTER_CHECK_NONE;
        const bool holds = initializer != BUSTER_CHECK_NONE && ending != BUSTER_CHECK_NONE && initializer <= first && ending > close && initializer >= begin;
        begin = holds ? initializer : begin;
        end = holds && ending - 1 < end ? ending - 1 : end;
    }
    check_interrupt_range(reader, begin, end, first, false);
}

// A call's error or warning (#147, check_input_interrupt_calls): its callee from `callee_first` to its `(` (`open`), its
// `)` (`close`), the name its callee names through parentheses, selections and one address-of (`named`), or a builtin's
// (`builtin`), BUSTER_CHECK_NONE for neither.
static void check_interrupt_call_report(CheckInputReader* reader, const CheckInterruptCalls* calls, uint32_t callee_first, uint32_t open, uint32_t close,
                                        uint32_t named, uint32_t builtin)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool scalar = parser->options.scalar;
    const uint32_t location = callee_first < open ? check_input_expression_root_location(parser, callee_first, open, NULL) : BUSTER_CHECK_NONE;
    const uint32_t declaration = named < count ? input->declarations[named] : BUSTER_CHECK_NONE;
    const uint32_t object = declaration < count && input->declaration_kinds[declaration] == C_BINDING_FUNCTION ? input->objects[declaration] : 0;
    const bool function = object != 0 && object <= count;
    // The body the call is in, of those the attributes mark: the last that opens before it.
    uint32_t low = 0;
    uint32_t high = location < count ? calls->filled : 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = calls->bodies[middle].open < close ? middle + 1 : low;
        high = calls->bodies[middle].open < close ? high : middle;
    }
    const CheckInterruptBody* body = low != 0 && close < calls->bodies[low - 1].close ? calls->bodies + low - 1 : NULL;
    // A call around a direct call's error (whose callee or arguments hold it) is clang's invalid expression, which
    // it builds no call of, and so warns of nothing: one of this pass's errors, of the few a unit has, inside it.
    bool invalid = false;
    for (uint32_t r = calls->report_begin; location < count && !invalid && r < input->report_count; r += 1)
    {
        invalid = input->reports[r].message == CHECK_INPUT_INTERRUPT_CALLED && input->reports[r].token >= callee_first && input->reports[r].completion < close;
    }
    if (location < count && function && check_interrupt_marked(calls, object, declaration, 0))
    {
        const uint32_t before = input->report_count;
        check_input_report(reader, location, CHECK_INPUT_INTERRUPT_CALLED);
        if (input->report_count > before)
        {
            input->reports[before].completion = close;
            input->reports[before].suborder = (uint64_t)location + 1;
        }
        check_interrupt_full_expression(reader, callee_first, close);
    }
    else if (location < count && body != NULL && !invalid && !(function && check_interrupt_marked(calls, object, declaration, 1)))
    {
        const uint32_t noted = function ? declaration : builtin < count ? check_interrupt_first_use(parser, parser->tokens.names[builtin], builtin + 1, scalar) :
                                                                        BUSTER_CHECK_NONE;
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_EXCESSIVE_REGSAVE,
            .kind = CHECK_WARNING_EVENT_EXCESSIVE_REGSAVE,
            .token = location,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = close,
            .suborder = (uint64_t)location + 1,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = body->caller },
        };
        const CheckWarningNote note =
        {
            .kind = CHECK_WARNING_NOTE_DECLARED_HERE,
            .token = noted,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .payload = { .name = noted < count ? parser->tokens.names[noted] : 0, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_event(reader, &event, &note, noted < count ? 1u : 0u);
    }
}

// The opening '(' of the group, call or subscript that `close` closes, by the parse tree's link from it (#1447): a
// backward search, in check_interrupt_evaluated_calls' rare calls alone. `close` itself where none is found.
static uint32_t check_interrupt_opening(const Parser* parser, uint32_t close)
{
    uint32_t result = close;
    for (uint32_t t = close; result == close && t != 0;)
    {
        t -= 1;
        const uint8_t kind = parser->tokens.kinds[t];
        result = (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE) && parser->tree.parents[t] == close ? t : result;
    }
    return result;
}

// The calls the reader's evaluator alone reads (#1447): a typeof's operand's, a constant array bound's, whose `)` no graph
// node has (CheckGraph.tokens), and which clang builds as it builds any call (BuildResolvedCallExpr): their direct calls
// of interrupt service routines and their -Wexcessive-regsave warnings (check_interrupt_call_report). A call's callee is
// the postfix expression before its `(`, and the name it names one written alone, in parentheses or after one `&`, which
// must be a declared name's use or a builtin's; a callee through a member or a postfix expression (`s.f()`) names nothing,
// and is warned of without a note (#1447). The graph's tokens are marked in a bitset; the
// vector path then compares 64 tokens' roles at a time with PARSE_ROLE_CLOSE_CALL, the scalar path, its twin, each
// token's.
static void check_interrupt_evaluated_calls(CheckInputReader* reader, const CheckInterruptCalls* calls)
{
    CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint64_t words = ((uint64_t)count + 63) / 64 + 1;
    uint64_t* graphed = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint64_t, words);
    input->resource |= graphed == NULL;
    if (graphed != NULL)
    {
        memset(graphed, 0, words * sizeof(uint64_t));
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            const uint32_t token = graph->tokens[node];
            graphed[(token < count ? token : count) / 64] |= UINT64_C(1) << ((token < count ? token : count) % 64);
        }
    }
    for (uint32_t first = 0; graphed != NULL && !input->resource && first < count; first += 64)
    {
        const uint32_t live = count - first < 64 ? count - first : 64;
        uint64_t selected = 0;
        if (!BUSTER_SCALAR_PATH(parser->options.scalar))
        {
            const __mmask64 lanes = live == 64 ? ~(__mmask64)0 : ((__mmask64)1 << live) - 1;
            const __m512i roles = _mm512_maskz_loadu_epi8(lanes, parser->tree.roles + first);
            selected = _mm512_mask_cmpeq_epi8_mask(lanes, roles, _mm512_set1_epi8((char)PARSE_ROLE_CLOSE_CALL));
        }
        else
        {
            for (uint32_t k = 0; k < live; k += 1)
            {
                selected |= (uint64_t)(parser->tree.roles[first + k] == PARSE_ROLE_CLOSE_CALL) << k;
            }
        }
        selected &= ~graphed[first / 64];
        for (; !input->resource && selected != 0; selected &= selected - 1)
        {
            const uint32_t close = first + (uint32_t)__builtin_ctzll(selected);
            const uint32_t open = check_interrupt_opening(parser, close);
            // The callee: the postfix expression that ends before the `(`, through calls', subscripts' and members' bases to
            // a name or a group.
            uint32_t callee_first = open;
            bool more = open != 0 && open != close;
            // Whether the callee goes through a member or a postfix expression's base: an expression's, never a declarator's.
            bool through = false;
            for (uint32_t at = open, steps = 0; more && steps < count; steps += 1)
            {
                const uint32_t last = at - 1;
                const uint8_t kind = parser->tokens.kinds[last];
                const bool closing = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE;
                const uint32_t opening = closing ? check_interrupt_opening(parser, last) : last;
                const bool name = kind == PARSE_KIND_IDENTIFIER;
                const bool member = name && last >= 2 && (parser->tokens.kinds[last - 1] == PARSE_KIND_PERIOD || parser->tokens.kinds[last - 1] == PARSE_KIND_ARROW);
                const bool postfix = closing && opening != last &&
                                     (parser->tokens.kinds[opening] == PARSE_KIND_L_SQUARE || parser->tree.roles[last] == PARSE_ROLE_CLOSE_CALL);
                callee_first = name || (closing && opening != last) ? opening : callee_first;
                at = member ? last - 1 : postfix ? opening : at;
                through |= member || postfix;
                more = (member || postfix) && at != 0;
            }
            // Its name, alone, in parentheses, or after one `&` in them.
            uint32_t low = callee_first;
            uint32_t high = open != 0 ? open - 1 : 0;
            bool addressed = false;
            for (uint32_t steps = 0; callee_first < open && low < high && steps < count; steps += 1)
            {
                const bool group = parser->tokens.kinds[low] == PARSE_KIND_L_PAREN && parser->tree.parents[low] == high;
                const bool address = !group && !addressed && parser->tokens.kinds[low] == PARSE_KIND_AMP;
                addressed |= address;
                low += group || address ? 1 : 0;
                high -= group ? 1 : 0;
                steps = group || address ? steps : count;
            }
            const uint32_t named = callee_first < open && low == high && parser->tokens.kinds[low] == PARSE_KIND_IDENTIFIER ? low : BUSTER_CHECK_NONE;
            const uint32_t declaration = named < count ? input->declarations[named] : BUSTER_CHECK_NONE;
            // A builtin's name, which no declaration binds (#1447): clang's call of the builtin it declares, noted at its first
            // use (check_interrupt_call_report).
            // (In a unit the reader took, a called name no declaration binds is one: an undeclared function's call binds its
            // implicit declaration at the name.)
            const bool builtin = named < count && declaration >= count && !addressed;
            // A use of a declared name: not a function declarator's own name (whose parameter list's role a call's can be), but
            // an undeclared function's, which its call declares at the name; a builtin's; or a callee through a member or a postfix expression (`s.f()`, `t[0]()`), which names nothing.
            const bool implicit = declaration == named && named < count && input->function_flags != NULL &&
                                  (input->function_flags[named] & CHECK_INPUT_FUNCTION_IMPLICIT) != 0;
            if (open != close && ((declaration < count && (declaration != named || implicit)) || builtin || (named >= count && through)))
            {
                check_interrupt_call_report(reader, calls, callee_first, open, close, named, builtin ? named : BUSTER_CHECK_NONE);
            }
        }
    }
}

// x86's interrupt and no_caller_saved_registers at calls (#147, clang's BuildResolvedCallExpr), in a unit that read either
// (CheckInput.interrupt_calls). A call of a function an interrupt attribute marks is clang's error "interrupt service
// routine cannot be called directly" at its callee (Expr::getExprLoc's place: check_input_expression_root_location); its
// callee is the declaration clang's ActOnCallExpr finds, its name through parentheses, selections and one address-of.
// Any other call in the body of a function interrupt or no_caller_saved_registers marks warns (-Wexcessive-regsave, the
// first's message where both do), unless its callee is a function no_caller_saved_registers marks, with a note at the
// callee's declaration: the one its name finds, or a builtin's first use in the unit, where clang declares it; a call
// through a pointer has none; a call around a direct call's error, clang's invalid expression, none. A declaration has
// an attribute that it or an earlier declaration of its entity has, as
// clang's redeclarations inherit them, a declaration's own (no_caller_saved_registers through a typedef's function type
// isn't). The calls are the graph's, in its order, which is clang's: a call's callee's and arguments' calls first; then
// those the reader's evaluator alone reads (a typeof's, a constant array bound's: check_interrupt_evaluated_calls),
// each at its place. A unit without the
// attributes takes none of this; one with them, two columns of its objects and a pass over its nodes, the vector path 16
// nodes' operations at a time, the scalar path, its twin, each node's.
static void check_input_interrupt_calls(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool scalar = parser->options.scalar;
    Arena* scratch = reader->scratch;
    const uint64_t mark = scratch->length;
    const uint32_t report_begin = input->report_count;
    // Per object, its first declaration interrupt marks, and its first no_caller_saved_registers marks, or NONE.
    uint32_t* interrupts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
    uint32_t* saving = interrupts != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : NULL;
    bool valid = saving != NULL;
    if (valid)
    {
        memset(interrupts, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(saving, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        const CheckSparseWide all = input->attributes;
        for (uint32_t first = check_sparse_wide_present(all) ? check_input_attributed_next(all, 0, count, scalar) : count; first < count;
             first = check_input_attributed_next(all, first + 16, count, scalar))
        {
            const uint32_t lanes = check_input_attributed_group(all, first, count, scalar);
            for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
                const CheckInputAttributes* attributes = check_input_attributes_at(input, d);
                const uint32_t object = input->declaration_kinds[d] == C_BINDING_FUNCTION ? input->objects[d] : 0;
                for (uint32_t i = 0; object != 0 && object <= count && attributes != NULL && i < attributes->count; i += 1)
                {
                    const CheckInputAttribute* attribute = attributes->records + i;
                    const bool kept = !attribute->ignored && !attribute->subject_ignored && !attribute->specifier_type;
                    const bool interrupt = kept && attribute->kind == CHECK_ATTRIBUTE_INTERRUPT;
                    const bool saves = kept && attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && attribute->value == CHECK_CALL_NO_CALLER_SAVED;
                    interrupts[object] = interrupt && d < interrupts[object] ? d : interrupts[object];
                    saving[object] = saves && d < saving[object] ? d : saving[object];
                }
            }
        }
    }
    // In a unit whose lookups found other declarations than their entities' latest (#1447, CheckInput.redeclaration_links),
    // each declaration's marks: its own and those of the declaration it found (the link's, or its object's latest before
    // it), in source order. A scan of the declaring tokens, the vector path comparing 64 tokens' kinds at a time with
    // C_BINDING_FUNCTION, the scalar path, its twin, each token's.
    uint8_t* marks = valid && input->redeclaration_link_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : NULL;
    uint32_t* latest = marks != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : NULL;
    valid = valid && (input->redeclaration_link_count == 0 || latest != NULL);
    if (latest != NULL)
    {
        memset(marks, 0, (uint64_t)count + 1);
        memset(latest, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        const uint32_t limit = input->token_count < count ? input->token_count : count;
        uint32_t link = 0;
        for (uint32_t first = 0; first < limit; first += 64)
        {
            const uint32_t live = limit - first < 64 ? limit - first : 64;
            uint64_t selected = 0;
            if (!BUSTER_SCALAR_PATH(parser->options.scalar))
            {
                const __mmask64 lanes = live == 64 ? ~(__mmask64)0 : ((__mmask64)1 << live) - 1;
                const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, input->declaration_kinds + first);
                selected = _mm512_mask_cmpeq_epi8_mask(lanes, kinds, _mm512_set1_epi8((char)C_BINDING_FUNCTION));
            }
            else
            {
                for (uint32_t k = 0; k < live; k += 1)
                {
                    selected |= (uint64_t)(input->declaration_kinds[first + k] == C_BINDING_FUNCTION) << k;
                }
            }
            for (; selected != 0; selected &= selected - 1)
            {
                const uint32_t d = first + (uint32_t)__builtin_ctzll(selected);
                const uint32_t object = input->declarations[d] == d ? input->objects[d] : 0;
                if (object != 0 && object <= count)
                {
                    while (link < input->redeclaration_link_count && input->redeclaration_links[link * 2] < d)
                    {
                        link += 1;
                    }
                    const bool linked = link < input->redeclaration_link_count && input->redeclaration_links[link * 2] == d;
                    const uint32_t found = linked ? input->redeclaration_links[link * 2 + 1] : latest[object];
                    const CheckInputAttributes* attributes = check_sparse_wide_present(input->attributes) ? check_input_attributes_at(input, d) : NULL;
                    uint8_t own = 0;
                    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
                    {
                        const CheckInputAttribute* attribute = attributes->records + i;
                        const bool kept = !attribute->ignored && !attribute->subject_ignored && !attribute->specifier_type;
                        own |= kept && attribute->kind == CHECK_ATTRIBUTE_INTERRUPT ? 1u : 0u;
                        own |= kept && attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && attribute->value == CHECK_CALL_NO_CALLER_SAVED ? 2u : 0u;
                    }
                    marks[d] = own | (found < count ? marks[found] : 0u);
                    latest[object] = d;
                }
            }
        }
    }
    // The bodies of the functions they mark, in token order.
    const bool bodied = valid && check_sparse_present(input->body_declarations);
    uint32_t body_count = 0;
    for (uint32_t opening = bodied ? check_sparse_next(input->body_declarations, 0, count, !BUSTER_SCALAR_PATH(scalar)) : count; opening < count;
         opening = check_sparse_next(input->body_declarations, opening + 1, count, !BUSTER_SCALAR_PATH(scalar)))
    {
        const uint32_t declaration = check_sparse_get(input->body_declarations, opening);
        const uint32_t object = declaration < count ? input->objects[declaration] : 0;
        body_count += object != 0 && object <= count &&
                      (marks != NULL ? marks[declaration] != 0 : interrupts[object] <= declaration || saving[object] <= declaration);
    }
    CheckInterruptBody* bodies = valid && body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInterruptBody, body_count) : NULL;
    valid = valid && (body_count == 0 || bodies != NULL);
    uint32_t filled = 0;
    for (uint32_t opening = bodied && bodies != NULL ? check_sparse_next(input->body_declarations, 0, count, !BUSTER_SCALAR_PATH(scalar)) : count; opening < count;
         opening = check_sparse_next(input->body_declarations, opening + 1, count, !BUSTER_SCALAR_PATH(scalar)))
    {
        const uint32_t declaration = check_sparse_get(input->body_declarations, opening);
        const uint32_t object = declaration < count ? input->objects[declaration] : 0;
        const bool marked = object != 0 && object <= count &&
                            (marks != NULL ? marks[declaration] != 0 : interrupts[object] <= declaration || saving[object] <= declaration);
        const bool interrupted = marks != NULL ? (marks[declaration] & 1u) != 0 : interrupts[object] <= declaration;
        if (marked && filled < body_count)
        {
            bodies[filled++] = (CheckInterruptBody){ .open = opening, .close = parser->tree.parents[opening], .caller = interrupted ? 0u : 1u };
        }
    }
    const CheckInterruptCalls calls = { .interrupts = interrupts, .saving = saving, .marks = marks, .bodies = bodies, .filled = filled,
                                        .report_begin = report_begin };
    for (uint32_t first = 0; valid && first < graph->count; first += 16)
    {
        const uint32_t live = graph->count - first < 16 ? graph->count - first : 16;
        uint32_t selected = (1u << live) - 1;
        if (!BUSTER_SCALAR_PATH(scalar))
        {
            const __mmask16 lanes = (__mmask16)selected;
            const __m512i ops = _mm512_maskz_loadu_epi32(lanes, graph->ops + first);
            selected = _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_CALL)) |
                       _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_UNARY)) |
                       _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_OVERFLOW)) |
                       _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_VARARGS)) |
                       (_mm512_mask_cmpge_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_MISC)) &
                        _mm512_mask_cmple_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_ATOMIC))) |
                       _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_VECTOR)) |
                       _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_X86));
        }
        for (; valid && selected != 0; selected &= selected - 1)
        {
            const uint32_t node = first + (uint32_t)__builtin_ctz(selected);
            const uint32_t close = graph->tokens[node];
            uint32_t open = close;
            bool opened = false;
            while (check_interrupt_call(graph, node) && !opened && open != 0 && close < count)
            {
                open -= 1;
                opened = parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == close;
            }
            // The callee's first token, and the declaration a call's callee names (none for a builtin's, whose name is
            // the token before its `(`, past the parentheses around it).
            uint32_t callee_first = BUSTER_CHECK_NONE;
            uint32_t named = BUSTER_CHECK_NONE;
            uint32_t builtin = BUSTER_CHECK_NONE;
            if (opened && graph->ops[node] == CHECK_OP_CALL)
            {
                uint32_t at = graph->operand0[node];
                callee_first = at < node ? check_warning_graph_first(graph, parser, at) : open;
                for (uint32_t steps = 0; at < node && steps < 2; steps += 1)
                {
                    while (at < node && graph->ops[at] == CHECK_OP_SELECT)
                    {
                        at = graph->operand0[at];
                    }
                    at = steps == 0 && at < node && graph->ops[at] == CHECK_OP_ADDRESS ? graph->operand0[at] : at;
                }
                named = at < node && graph->ops[at] == CHECK_OP_VALUE ? graph->tokens[at] : BUSTER_CHECK_NONE;
            }
            else if (opened && open != 0)
            {
                builtin = open - 1;
                while (builtin != 0 && parser->tokens.kinds[builtin] == PARSE_KIND_R_PAREN)
                {
                    builtin -= 1;
                }
                callee_first = builtin;
            }
            while (callee_first != BUSTER_CHECK_NONE && callee_first != 0 && callee_first < open && parser->tokens.kinds[callee_first - 1] == PARSE_KIND_L_PAREN &&
                   parser->tree.parents[callee_first - 1] >= callee_first && parser->tree.parents[callee_first - 1] < open)
            {
                callee_first -= 1;
            }
            check_interrupt_call_report(reader, &calls, callee_first, open, close, named, builtin);
            valid = !input->resource;
        }
    }
    if (valid)
    {
        check_interrupt_evaluated_calls(reader, &calls);
        valid = !input->resource;
    }
    input->resource |= !valid;
    arena_pop_to(scratch, mark);
}

// An indirection through a pointer to void (#147, clang's CheckIndirectionOperand): whether `node` is one clang warns of,
// -Wvoid-ptr-dereference, in dead code too, but not right after a unary `&` (`&*p`, clang's IsAfterAmp; `&(*p)` warns).
// Whether its context is an evaluated one is check_warning_graph_void_context's.
static bool check_warning_graph_void_candidate(const CheckInput* input, const Parser* parser, const TypeTable* types, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t operand = graph->operand0[node];
    const uint32_t star = graph->tokens[node];
    const TypeId type = operand < graph->count ? type_unqualified(check_stream_type(stream, operand)) : 0;
    const TypeId pointer = type != 0 ? type_unqualified(type_strip_aligned(types, type)) : 0;
    const bool through_void = pointer != 0 && type_kind(types, pointer) == BUSTER_TYPE_KIND_POINTER &&
                              type_kind(types, type_unqualified(type_strip_aligned(types, type_element(types, pointer)))) == BUSTER_TYPE_KIND_VOID;
    const uint32_t before = star != 0 && star < parser->tokens.count && parser->tokens.kinds[star - 1] == PARSE_KIND_AMP && input->token_nodes != NULL ?
                            input->token_nodes[star - 1] : BUSTER_CHECK_NONE;
    const bool after_amp = before < graph->count && graph->ops[before] == CHECK_OP_ADDRESS;
    return through_void && !after_amp && star < parser->tokens.count;
}

// A token's context as clang's parser enters it (Sema's expression evaluation contexts), which decides whether a void
// dereference warns: 0 while not yet known, else KNOWN, and UNEVALUATED in an unevaluated operand. TRANSFORMED marks
// what a sizeof of a variable array evaluates after all, and SELECTION a _Generic's separators.
typedef enum CheckVoidContext
{
    CHECK_VOID_CONTEXT_KNOWN = 1,
    CHECK_VOID_CONTEXT_UNEVALUATED = 2,
    CHECK_VOID_CONTEXT_TRANSFORMED = 4,
    CHECK_VOID_CONTEXT_SELECTION = 8,
    // An array declarator's bound (clang's ConstantEvaluated context), where DiagRuntimeBehavior drops the runtime
    // warnings (#1447, check_common_bound).
    CHECK_VOID_CONTEXT_BOUND = 16,
} CheckVoidContext;

// Whether a token of `role` is a declaration's specifiers' (a type name's root, as a declarator's tokens hang from it).
static bool check_warning_graph_void_specifiers(uint8_t role)
{
    return role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG || role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_STRUCT ||
           role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
}

// Whether the `]` at `close` ends an array declarator's bound, in a declaration or a type name, rather than a subscript:
// up the declarator's own tokens (`*`, groups, qualifiers, other bounds, the declarators' commas, a function
// declarator's parentheses, attributes) come the declaration's specifiers. A subscript's climb stops at an operator, a
// statement or another group first.
static bool check_warning_graph_void_declarator(const Parser* parser, uint32_t close)
{
    const uint32_t count = parser->tokens.count;
    uint32_t up = parser->tree.parents[close];
    bool chain = true;
    for (uint32_t steps = 0; chain && up < count && steps < count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[up];
        chain = role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_QUALIFIER_LEFT || role == PARSE_ROLE_COMMA ||
                role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_FUNCTION || role == PARSE_ROLE_CLOSE_ATTRIBUTE ||
                (role == PARSE_ROLE_PREFIX && parser->tokens.kinds[up] == PARSE_KIND_STAR);
        up = chain ? parser->tree.parents[up] : up;
    }
    return !chain && up < count && check_warning_graph_void_specifiers(parser->tree.roles[up]);
}

// The opener of the group `close` closes: the nearest token before it at its depth, as the group's contents are deeper.
static uint32_t check_warning_graph_void_opener(const Parser* parser, uint32_t close)
{
    const uint32_t depth = parser->tree.depths[close];
    uint32_t open = close;
    while (open > 0 && parser->tree.depths[open - 1] > depth)
    {
        open -= 1;
    }
    return open > 0 ? open - 1 : BUSTER_CHECK_NONE;
}

// Whether the static selection whose `)` is `close` (a _Generic's or a __builtin_choose_expr's) chooses the operand that
// holds `token`, a token inside its parentheses: `token` and the chosen operand's token lie between the same top-level
// commas, as the separators' tree nests the operands before each one under it. The node at its `)` is the selection's,
// or, for one the source reader chose, the chosen operand's own, under the conversions at that `)`, or a constant the
// reader folded there, which chose no operand that matters. True where `close` closes no selection, or one whose choice the
// graph doesn't hold.
static bool check_warning_graph_chooses(const CheckInput* input, const Parser* parser, uint32_t close, uint32_t token)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t open = parser->tree.roles[close] == PARSE_ROLE_CLOSE_KEYWORD ? check_warning_graph_void_opener(parser, close) : BUSTER_CHECK_NONE;
    const uint32_t keyword = open != BUSTER_CHECK_NONE && open > 0 ? open - 1 : BUSTER_CHECK_NONE;
    const bool selection = keyword != BUSTER_CHECK_NONE && parser->tree.depths != NULL && open + 1 < close &&
                           (parser->tokens.kinds[keyword] == PARSE_KIND_GENERIC || parser->tokens.names[keyword] == C_NAME_BUILTIN_CHOOSE_EXPR);
    uint32_t chosen = selection && close < input->token_count && input->token_nodes != NULL ? input->token_nodes[close] : BUSTER_CHECK_NONE;
    chosen = chosen < graph->count && graph->ops[chosen] == CHECK_OP_SELECT ? graph->operand0[chosen] : chosen;
    for (uint32_t steps = 0; chosen < graph->count && graph->ops[chosen] == CHECK_OP_CONVERT && graph->tokens[chosen] == close && steps < graph->count;
         steps += 1)
    {
        chosen = graph->operand0[chosen];
    }
    const uint32_t chosen_token = chosen < graph->count ? graph->tokens[chosen] : BUSTER_CHECK_NONE;
    // (A selection the reader folded to a constant at its `)` chose an operand of no side effect and no write.)
    bool result = !(selection && chosen_token == close);
    if (selection && chosen_token > open && chosen_token < close && token > open && token < close)
    {
        const uint32_t depth = parser->tree.depths[open + 1];
        uint32_t before_token = 0;
        uint32_t before_chosen = 0;
        for (uint32_t at = open + 1; at < close; at += 1)
        {
            const bool comma = parser->tokens.kinds[at] == PARSE_KIND_COMMA && parser->tree.depths[at] == depth;
            before_token += comma && at < token ? 1u : 0u;
            before_chosen += comma && at < chosen_token ? 1u : 0u;
        }
        result = before_token == before_chosen;
    }
    return result;
}

// The context of `child` of `parent`, whose context is `context`: the parent's, but in an operand that opens another.
// A sizeof's or an alignof's operand (CLOSE_SIZEOF's, or the keyword's without parentheses), a typeof's and a
// _Generic's controlling expression (left of its first separator) are unevaluated; an array declarator's bound is
// evaluated again (ConstantEvaluated), so `sizeof(char (*)[(*p, n)])` warns, and so is the rest of a specifier's group
// (_Atomic, _BitInt). A sizeof whose operand is an expression (not a type name) of variable array type (the graph's
// CHECK_OP_SIZEOF_VLA), itself in an evaluated context, evaluates the operand after all (clang's
// TransformToPotentiallyEvaluated), which warns of its dereferences outside other queries then, a bound's a second time.
static uint8_t check_warning_graph_void_step(const CheckInput* input, const Parser* parser, uint32_t parent, uint8_t context, uint32_t child)
{
    const uint32_t count = parser->tokens.count;
    const uint8_t role = parser->tree.roles[parent];
    const uint8_t transformed = context & CHECK_VOID_CONTEXT_TRANSFORMED;
    uint8_t result = context & (uint8_t)~CHECK_VOID_CONTEXT_SELECTION;
    if ((context & CHECK_VOID_CONTEXT_SELECTION) != 0)
    {
        const bool left = child < parent;
        result = left && parser->tree.roles[child] == PARSE_ROLE_SEPARATOR ? context : left ? CHECK_VOID_CONTEXT_KNOWN | CHECK_VOID_CONTEXT_UNEVALUATED : result;
    }
    else if (role == PARSE_ROLE_CLOSE_SIZEOF ||
             (role == PARSE_ROLE_PREFIX && parser->tokens.kinds[parent] == PARSE_KIND_SIZEOF && parser->tree.roles[child] != PARSE_ROLE_CLOSE_SIZEOF))
    {
        const uint32_t keyword = role == PARSE_ROLE_CLOSE_SIZEOF ? parser->tree.parents[parent] : parent;
        const uint32_t node = keyword < count && input->token_nodes != NULL ? input->token_nodes[keyword] : BUSTER_CHECK_NONE;
        // (A type name's, `sizeof(int[n])`, isn't: its bounds were evaluated where they were read.)
        const bool runtime = node < input->graph.count && input->graph.ops[node] == CHECK_OP_SIZEOF_VLA && !check_warning_graph_void_specifiers(parser->tree.roles[child]);
        const bool evaluated = (context & CHECK_VOID_CONTEXT_UNEVALUATED) == 0 || transformed != 0;
        result = CHECK_VOID_CONTEXT_KNOWN | CHECK_VOID_CONTEXT_UNEVALUATED | (runtime && evaluated ? CHECK_VOID_CONTEXT_TRANSFORMED : 0);
    }
    else if (role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_KEYWORD)
    {
        const uint32_t open = check_warning_graph_void_opener(parser, parent);
        const uint8_t keyword = open != BUSTER_CHECK_NONE && open > 0 ? parser->tokens.kinds[open - 1] : PARSE_KIND_UNKNOWN;
        // Between the parentheses: a declarator's tokens past a specifier's `)` (`typeof(int) y = e;`, `_BitInt(100) n = e;`,
        // whose `=` the parser hangs off it) are no operand of its (#1447).
        const bool inside = open != BUSTER_CHECK_NONE && child > open && child < parent;
        if (inside && role == PARSE_ROLE_CLOSE_SPECIFIER)
        {
            result = keyword == PARSE_KIND_TYPEOF ? CHECK_VOID_CONTEXT_KNOWN | CHECK_VOID_CONTEXT_UNEVALUATED : CHECK_VOID_CONTEXT_KNOWN | transformed;
        }
        else if (inside && keyword == PARSE_KIND_GENERIC && parser->tree.roles[child] == PARSE_ROLE_SEPARATOR)
        {
            result = context | CHECK_VOID_CONTEXT_SELECTION;
        }
    }
    else if (role == PARSE_ROLE_CLOSE_SUBSCRIPT && check_warning_graph_void_declarator(parser, parent))
    {
        result = CHECK_VOID_CONTEXT_KNOWN | CHECK_VOID_CONTEXT_BOUND | transformed;
    }
    return result;
}

// `token`'s context (check_warning_graph_void_step's), each token's once: a climb stops at the first token whose
// context is known, or at a root, whose context is evaluated, and writes the contexts down the path it took (`path`,
// room for every token). A unit's climbs then cost O(tokens) however many dereferences ask, as comma or operator chains
// would otherwise make them quadratic.
static uint8_t check_warning_graph_void_context(const CheckInput* input, const Parser* parser, uint8_t* known, uint32_t* path, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t length = 0;
    uint32_t up = token;
    while (up < count && known[up] == 0 && length < count)
    {
        path[length] = up;
        length += 1;
        up = parser->tree.parents[up];
    }
    uint8_t context = up < count && known[up] != 0 ? known[up] : CHECK_VOID_CONTEXT_KNOWN;
    for (uint32_t i = length; i-- > 0;)
    {
        const uint32_t child = path[i];
        context = up < count ? check_warning_graph_void_step(input, parser, up, context, child) : CHECK_VOID_CONTEXT_KNOWN;
        known[child] = context;
        up = child;
    }
    return token < count && known[token] != 0 ? known[token] : CHECK_VOID_CONTEXT_KNOWN;
}

// The void dereferences' warnings (check_warning_graph_void_candidate's `count` nodes), at each `*`, of the operand's type as
// clang prints it (its top-level qualifiers dropped, its typedef kept), once in an evaluated context, twice in a bound a
// sizeof of a variable array evaluates again, and none in an unevaluated one (check_warning_graph_void_context). Units
// without such a dereference take none of it.
static void check_warning_graph_void_dereferences(CheckInputReader* reader, const CheckStream* stream, const uint32_t* candidates, uint32_t count)
{
    const CheckGraph* graph = &reader->output->graph;
    const Parser* parser = reader->parser;
    const uint32_t tokens = parser->tokens.count;
    uint8_t* known = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, tokens);
    uint32_t* path = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, tokens);
    const bool valid = known != NULL && path != NULL;
    reader->output->resource |= !valid;
    if (valid)
    {
        memset(known, 0, tokens);
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const uint32_t node = candidates[i];
        const uint32_t operand = graph->operand0[node];
        const uint32_t star = graph->tokens[node];
        const uint8_t context = check_warning_graph_void_context(reader->output, parser, known, path, star);
        const uint32_t warnings = ((context & CHECK_VOID_CONTEXT_UNEVALUATED) == 0 ? 1u : 0u) + ((context & CHECK_VOID_CONTEXT_TRANSFORMED) != 0 ? 1u : 0u);
        if (warnings != 0)
        {
            // (Through inner dereferences to the object they name: `**b`'s operand spells `VP *b`'s VP.)
            uint32_t named = operand;
            while (named < graph->count && graph->ops[named] == CHECK_OP_DEREFERENCE)
            {
                named = graph->operand0[named];
            }
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_VOID_PTR_DEREFERENCE,
                .kind = CHECK_WARNING_EVENT_VOID_PTR_DEREFERENCE,
                .token = star,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = star + 1,
                .payload = { .first_type = type_unqualified(check_stream_type(stream, operand)), .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                             .data = check_input_operand_spelling(reader, named) },
            };
            const uint64_t key = (uint64_t)event.kind << 32 | ((uint64_t)star + 1);
            const bool first = !check_warning_set_contains(&reader->source_warning_keys, key);
            check_input_warning_once(reader, &event);
            if (first && warnings == 2)
            {
                check_input_warning_event(reader, &event, NULL, 0);
            }
        }
    }
}

// A C-style cast from a pointer (an array or a function decayed to one too) to an integer type narrower than it, not
// _Bool (#1943, clang's CheckCStyleCast: C 6.3.2.3p6 makes the result undefined where it can't be represented): at the
// cast's `(` as soon as it is read, in an unevaluated operand too: -Wpointer-to-int-cast, or -Wpointer-to-enum-cast to an
// enumeration, each a pointer to void's -Wvoid- form.
static void check_warning_graph_pointer_cast(CheckInputReader* reader, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = &reader->output->graph;
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t operand = graph->operand0[node];
    const uint32_t close = graph->tokens[node];
    const TypeId written = operand < graph->count ? type_unqualified(check_stream_type(stream, operand)) : 0;
    const TypeId plain = written != 0 ? type_unqualified(type_strip_aligned(types, written)) : 0;
    const uint8_t from = plain != 0 ? type_kind(types, plain) : BUSTER_TYPE_KIND_NONE;
    const bool array = from == BUSTER_TYPE_KIND_ARRAY || from == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || from == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
    const TypeId target = check_stream_type(stream, node);
    const TypeId integer = target != 0 ? type_unqualified(type_strip_aligned(types, target)) : 0;
    const uint8_t kind = integer != 0 ? type_kind(types, integer) : BUSTER_TYPE_KIND_NONE;
    // (A pointer is 8 bytes; an array or a function decays to one.)
    const bool from_pointer = from == BUSTER_TYPE_KIND_POINTER || array || from == BUSTER_TYPE_KIND_FUNCTION;
    const bool narrower = from_pointer && integer != BUSTER_TYPE_BOOL &&
                          (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_BIT_INT) && type_size(types, integer) < 8;
    const uint32_t open = narrower && close < parser->tokens.count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_CAST ?
                          check_warning_graph_void_opener(parser, close) : BUSTER_CHECK_NONE;
    // (The operand as the cast reads it, which clang prints: an array's or a function's after its decay, without sugar.)
    const TypeId source = open >= parser->tokens.count ? 0 : array ? type_pointer(types, type_element(types, plain)) :
                          from == BUSTER_TYPE_KIND_FUNCTION ? type_pointer(types, written) : written;
    reader->output->resource |= open < parser->tokens.count && source == 0;
    const TypeId pointer = source != 0 ? type_unqualified(type_strip_aligned(types, source)) : 0;
    if (pointer != 0)
    {
        const bool to_void = type_kind(types, type_unqualified(type_strip_aligned(types, type_element(types, pointer)))) == BUSTER_TYPE_KIND_VOID;
        const bool to_enum = kind == BUSTER_TYPE_KIND_ENUM;
        const uint32_t id = to_enum ? (to_void ? CHECK_WARNING_ID_VOID_POINTER_TO_ENUM_CAST : CHECK_WARNING_ID_POINTER_TO_ENUM_CAST) :
                                      (to_void ? CHECK_WARNING_ID_VOID_POINTER_TO_INT_CAST : CHECK_WARNING_ID_POINTER_TO_INT_CAST);
        const uint32_t completion = graph->source_positions[node];
        const CheckWarningEvent event =
        {
            .id = id,
            .kind = CHECK_WARNING_EVENT_POINTER_TO_INT_CAST,
            .token = open,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = completion > open ? completion : close + 1,
            .payload = { .first_type = source, .second_type = target, .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                         .name = source == written ? check_input_operand_spelling(reader, operand) : 0,
                         .data = check_input_operand_spelling(reader, node) },
        };
        check_input_warning_once(reader, &event);
    }
}

// GCC's cast to a union from a member's type (#2301, clang's CheckCStyleCast, BUSTER_CHECK_CONVERT_TO_UNION): the extension's
// -Wgnu-union-cast at the cast's `(`, as soon as it is read, in an unevaluated operand too. Off by default.
static void check_warning_graph_union_cast(CheckInputReader* reader, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = &reader->output->graph;
    const Parser* parser = reader->parser;
    const uint32_t close = graph->tokens[node];
    const bool to_union = node < stream->count && (stream->conversions[node].operands[0].flags & BUSTER_CHECK_CONVERT_TO_UNION) != 0 &&
                          stream->diagnostics[node] == CHECK_DIAG_NONE;
    const uint32_t open = to_union && close < parser->tokens.count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_CAST ?
                          check_warning_graph_void_opener(parser, close) : BUSTER_CHECK_NONE;
    if (open < parser->tokens.count)
    {
        const uint32_t completion = graph->source_positions[node];
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_GNU_UNION_CAST,
            .kind = CHECK_WARNING_EVENT_GNU_UNION_CAST,
            .token = open,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .flags = BUSTER_CHECK_WARNING_DEFAULT_IGNORED,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = completion > open ? completion : close + 1,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_once(reader, &event);
    }
}

// A struct's or a union's cast to its own type (#2375, clang's CheckCStyleCast, ext_typecheck_cast_nonscalar): -Wpedantic's
// "C99 forbids casting nonscalar type 'T' to the same type" at the cast's `(`, its type as written, as soon as it is read,
// in an unevaluated operand too. Off by default.
static void check_warning_graph_nonscalar_cast(CheckInputReader* reader, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = &reader->output->graph;
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    const uint32_t close = graph->tokens[node];
    const TypeId target = graph->types != NULL ? graph->types[node] : 0;
    const uint8_t kind = target != 0 ? type_kind(types, type_unqualified(type_strip_aligned(types, target))) : BUSTER_TYPE_KIND_NONE;
    const bool same = (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && node < stream->count &&
                      (stream->conversions[node].operands[0].flags & BUSTER_CHECK_CONVERT_TO_UNION) == 0 && stream->diagnostics[node] == CHECK_DIAG_NONE;
    const uint32_t open = same && close < parser->tokens.count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_CAST ?
                          check_warning_graph_void_opener(parser, close) : BUSTER_CHECK_NONE;
    if (open < parser->tokens.count)
    {
        const uint32_t completion = graph->source_positions[node];
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_NONSCALAR_SAME_CAST,
            .kind = CHECK_WARNING_EVENT_NONSCALAR_SAME_CAST,
            .token = open,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .flags = BUSTER_CHECK_WARNING_DEFAULT_IGNORED,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = completion > open ? completion : close + 1,
            .payload = { .first_type = target, .name = check_input_operand_type_spelling(reader->output, parser, node), .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_once(reader, &event);
    }
}

// -Wunevaluated-expression (#1447, clang's CheckUnaryExprOrTypeTraitOperand and CreateGenericSelectionExpr): the expression
// operand of a sizeof or an alignof (of no variable array type) and a _Generic's controlling expression are unevaluated,
// and one with a side effect, as Expr::HasSideEffects finds it without the possible ones, warns at its expression's
// location. A side effect is an assignment, an increment or a decrement, va_arg, or an atomic builtin clang makes an
// AtomicExpr of; a call, a volatile read or a statement expression's declaration is none of itself. HasSideEffects
// reads an expression's operands down from the operand, but no further than an expression whose location a macro
// wrote (a macro's argument too), a nested sizeof's or alignof's operand, a type (a cast's, a compound literal's, a
// typeof's), or a static selection's unchosen operands or controlling expression; in a GNU statement expression it reads
// each statement's expressions as clang's SideEffectFinder visits them, which skips an unevaluated-argument builtin's
// call that is a statement's own expression. The checks climb the parse tree from each side effect in the operand, as
// a statement expression's statements are graph roots of their own.

// Whether a macro wrote `token`, its argument's tokens too (clang's isMacroID).
static bool check_warning_graph_macro(const Parser* parser, uint32_t token)
{
    const bool result = token < parser->tokens.count && parser->tokens.flags != NULL && (parser->tokens.flags[token] & BUSTER_PP_FROM_MACRO) != 0;
    return result;
}

// Whether the parse tree's climb from `token` reaches `ancestor`.
static bool check_warning_graph_within(const Parser* parser, uint32_t token, uint32_t ancestor)
{
    const uint32_t count = parser->tokens.count;
    uint32_t at = token;
    for (uint32_t steps = 0; at < count && at != ancestor && steps < count; steps += 1)
    {
        at = parser->tree.parents[at];
    }
    return at == ancestor && at < count;
}

// The first token of `token`'s subtree, a run of tokens: down its first children while they come before it, or, where the
// parser left those out, back while the token before climbs to it.
static uint32_t check_warning_graph_start(const Parser* parser, uint32_t token)
{
    uint32_t result = token;
    if (parser->first_children != NULL)
    {
        for (uint32_t steps = 0; result < parser->tokens.count && parser->first_children[result] < result && steps < parser->tokens.count; steps += 1)
        {
            result = parser->first_children[result];
        }
    }
    else
    {
        while (result != 0 && result < parser->tokens.count && check_warning_graph_within(parser, result - 1, token))
        {
            result -= 1;
        }
    }
    return result;
}

// The location clang gives the expression whose node is the token `token` (Expr::getExprLoc), or BUSTER_CHECK_NONE for
// a token that is no expression's (a statement's, a declaration's): an operator's own, a member's name, and the first token
// of a group, a cast, a compound literal, a call, a keyword's operation, a conditional or a subscript (whose base's location
// clang gives, its first token where the base is a name).
static uint32_t check_warning_graph_location(const CheckInput* input, const Parser* parser, uint32_t token)
{
    const uint8_t role = parser->tree.roles[token];
    const uint32_t node = token < input->token_count && input->token_nodes != NULL ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    const CheckOp op = node < input->graph.count ? input->graph.ops[node] : CHECK_OP_COUNT;
    uint32_t result = BUSTER_CHECK_NONE;
    if (role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_CLOSE_COMPOUND || role == PARSE_ROLE_CLOSE_COMPOUND_BODY ||
        role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_KEYWORD || role == PARSE_ROLE_CLOSE_TERNARY ||
        (role == PARSE_ROLE_CLOSE_SUBSCRIPT && !check_warning_graph_void_declarator(parser, token)))
    {
        result = check_warning_graph_start(parser, token);
    }
    else if (role == PARSE_ROLE_MEMBER_OPERATOR)
    {
        result = token + 1;
    }
    else if (role == PARSE_ROLE_PREFIX || role == PARSE_ROLE_POSTFIX || role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_COMMA ||
             (role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_LOGICAL_OR) || (role == PARSE_ROLE_ASSIGN && op != CHECK_OP_INITIALIZE))
    {
        result = token;
    }
    return result;
}

// Whether the node at `token` is a side effect of itself (an assignment's operator, an increment's or a decrement's,
// va_arg's or an atomic builtin's call), and the location clang gives it in `*location`.
static bool check_warning_graph_effect(const CheckInput* input, const Parser* parser, uint32_t token, uint32_t* location)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t node = token < input->token_count && input->token_nodes != NULL ? input->token_nodes[token] : BUSTER_CHECK_NONE;
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const uint32_t id = op == CHECK_OP_BUILTIN_ATOMIC && graph->members != NULL ? graph->members[node] : CBUILTIN_NONE;
    // (Clang's AtomicExpr builtins: not the fences, the lock-free queries or the __sync family.)
    const bool atomic = (id >= CBUILTIN_ATOMIC_LOAD && id <= CBUILTIN_ATOMIC_CLEAR) || (id >= CBUILTIN_ATOMIC_FETCH_MIN && id <= CBUILTIN_ATOMIC_FETCH_UDEC) ||
                        (id >= CBUILTIN_C11_ATOMIC_INIT && id <= CBUILTIN_C11_ATOMIC_FETCH_MAX) ||
                        (id >= CBUILTIN_ATOMIC_FETCH_FMAXIMUM && id <= CBUILTIN_ATOMIC_FETCH_FMINIMUM_NUM);
    // A token the constant reader read (an array bound's, an enumerator's value's, with no node) is an assignment's or an
    // increment's by its role or kind, but a designator's `=` (#1447, check_warning_graph_constant_queries).
    const uint8_t role = parser->tree.roles[token];
    const uint8_t kind = parser->tokens.kinds[token];
    const bool designator = token >= 2 && (parser->tokens.kinds[token - 1] == PARSE_KIND_R_SQUARE ||
                                           (parser->tokens.kinds[token - 1] == PARSE_KIND_IDENTIFIER && parser->tokens.kinds[token - 2] == PARSE_KIND_PERIOD));
    const bool unread = node == BUSTER_CHECK_NONE && input->token_nodes != NULL &&
                        ((role == PARSE_ROLE_ASSIGN && !designator) || kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS);
    const bool result = op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN) || op == CHECK_OP_VARARGS || atomic || unread;
    *location = result ? check_warning_graph_location(input, parser, token) : BUSTER_CHECK_NONE;
    return result;
}

// Whether HasSideEffects reaches the side effect at `token`, whose location is `location`, from the operand whose
// parent is `stop`: up the parse tree, through no expression whose location a macro wrote, no nested sizeof or alignof, no
// type, no specifier's operand (typeof's), no tag's body or bit-field, no function's parameters, no attribute and no
// static selection's other operands; in a statement expression, not from an unevaluated-argument builtin's call that is
// a statement's own expression. A declaration's initializer or array bound there is read.
static bool check_warning_graph_effect_reached(const CheckInput* input, const Parser* parser, uint32_t token, uint32_t location, uint32_t stop)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t count = parser->tokens.count;
    bool result = !check_warning_graph_macro(parser, location);
    uint32_t child = token;
    for (uint32_t at = parser->tree.parents[token], steps = 0; result && at < count && at != stop && steps < count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        const uint8_t kind = parser->tokens.kinds[at];
        const uint32_t node = at < input->token_count && input->token_nodes != NULL ? input->token_nodes[at] : BUSTER_CHECK_NONE;
        const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
        // (A tag's body and a specifier's operand are before their closer: a declaration's declarators and initializers,
        // which the tree hangs under its specifier's closer, are after it.)
        const bool inside = child < at;
        bool blocked = role == PARSE_ROLE_CLOSE_SIZEOF || (role == PARSE_ROLE_PREFIX && kind == PARSE_KIND_SIZEOF) ||
                       ((role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM) && inside) ||
                       role == PARSE_ROLE_BITFIELD || role == PARSE_ROLE_CLOSE_FUNCTION ||
                       role == PARSE_ROLE_CLOSE_ATTRIBUTE || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER || role == PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT ||
                       role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT || role == PARSE_ROLE_CLOSE_ATTRIBUTE_LIST || role == PARSE_ROLE_CLOSE_ASM ||
                       ((role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_CLOSE_COMPOUND) && inside);
        // (A static selection's other operands and its controlling expression.)
        blocked |= role == PARSE_ROLE_CLOSE_KEYWORD && !check_warning_graph_chooses(input, parser, at, token);
        if (role == PARSE_ROLE_CLOSE_CALL && (op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_MISC || op == CHECK_OP_BUILTIN_UNARY) &&
                 graph->members != NULL)
        {
            // An unevaluated-argument builtin's call that a statement holds as its own expression, which SideEffectFinder's
            // visitor skips (a condition's, a return's, an initializer's, an expression statement's).
            const uint32_t id = graph->members[node];
            const uint32_t up = parser->tree.parents[at];
            const uint8_t up_role = up < count ? parser->tree.roles[up] : PARSE_ROLE_NONE;
            const uint32_t up_node = up < input->token_count && input->token_nodes != NULL ? input->token_nodes[up] : BUSTER_CHECK_NONE;
            const bool statement = up_role == PARSE_ROLE_TERMINATOR || up_role == PARSE_ROLE_CLOSE_CONDITION || up_role == PARSE_ROLE_RETURN ||
                                   up_role == PARSE_ROLE_CLOSE_FOR || (up_role == PARSE_ROLE_ASSIGN && up_node < graph->count && graph->ops[up_node] == CHECK_OP_INITIALIZE);
            blocked |= statement && (id == CBUILTIN_CONSTANT_P || id == CBUILTIN_OBJECT_SIZE || id == CBUILTIN_DYNAMIC_OBJECT_SIZE || id == CBUILTIN_CLASSIFY_TYPE);
        }
        result = !blocked && !check_warning_graph_macro(parser, check_warning_graph_location(input, parser, at));
        child = at;
        at = parser->tree.parents[at];
    }
    return result;
}

// Whether the tokens [first, end), an unevaluated operand's whose parent is `stop`, hold a side effect HasSideEffects finds
// (check_warning_graph_effect_reached). The candidates are the operators of assignments, increments and decrements and
// the closers of calls and keywords' operations: 64 tokens' roles and kinds at a time, the scalar path's twin a token at
// a time; each candidate's node says whether it is one.
static bool check_warning_graph_side_effects(const CheckInput* input, const Parser* parser, uint32_t first, uint32_t end, uint32_t stop)
{
    const bool vector = !BUSTER_SCALAR_PATH(parser->options.scalar);
    bool result = false;
    for (uint32_t block = first; !result && block < end; block += 64)
    {
        const uint32_t live = end - block < 64 ? end - block : 64;
        const uint64_t lanes = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
        uint64_t candidates = 0;
        if (vector)
        {
            const __m512i roles = _mm512_maskz_loadu_epi8((__mmask64)lanes, parser->tree.roles + block);
            const __m512i kinds = _mm512_maskz_loadu_epi8((__mmask64)lanes, parser->tokens.kinds + block);
            candidates = _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, roles, _mm512_set1_epi8((char)PARSE_ROLE_ASSIGN)) |
                         _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, roles, _mm512_set1_epi8((char)PARSE_ROLE_CLOSE_CALL)) |
                         _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, roles, _mm512_set1_epi8((char)PARSE_ROLE_CLOSE_KEYWORD)) |
                         _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, kinds, _mm512_set1_epi8((char)PARSE_KIND_PLUSPLUS)) |
                         _mm512_mask_cmpeq_epi8_mask((__mmask64)lanes, kinds, _mm512_set1_epi8((char)PARSE_KIND_MINUSMINUS));
        }
        else
        {
            for (uint32_t lane = 0; lane < live; lane += 1)
            {
                const uint8_t role = parser->tree.roles[block + lane];
                const uint8_t kind = parser->tokens.kinds[block + lane];
                const bool candidate = role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_KEYWORD ||
                                       kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS;
                candidates |= candidate ? UINT64_C(1) << lane : 0;
            }
        }
        for (; !result && candidates != 0; candidates &= candidates - 1)
        {
            const uint32_t token = block + (uint32_t)__builtin_ctzll(candidates);
            uint32_t location = BUSTER_CHECK_NONE;
            result = check_warning_graph_effect(input, parser, token, &location) && check_warning_graph_effect_reached(input, parser, token, location, stop);
        }
    }
    return result;
}

// The warning of an unevaluated operand with a side effect (check_warning_graph_side_effects): the operand's tokens
// [first, end), whose parent is `stop`, at `location`, the operand's expression's, published once with `completion` in
// lookahead, as clang's parser builds the operation there.
static void check_warning_graph_unevaluated(CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t stop, uint32_t location, uint32_t completion)
{
    const Parser* parser = reader->parser;
    if (first < end && location < parser->tokens.count && !check_warning_graph_macro(parser, location) &&
        check_warning_graph_side_effects(reader->output, parser, first, end, stop))
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_UNEVALUATED_EXPRESSION,
            .kind = CHECK_WARNING_EVENT_UNEVALUATED_EXPRESSION,
            .token = location,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = completion,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_once(reader, &event);
    }
}

// The unevaluated operand of the sizeof or alignof `node`: its tokens, its parent, its expression's location and the token
// after the operation (check_warning_graph_unevaluated). A sizeof's own parentheses are clang's group, whose `(` is the
// location; an operand of variable array type warns of none. Its node's source position is past its last token.
static void check_warning_graph_operand(CheckInputReader* reader, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = &reader->output->graph;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t token = graph->tokens[node];
    const CheckOp op = graph->ops[node];
    const uint32_t end = graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE;
    if ((op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF) && graph->operand0[node] < graph->count && token + 1 < end && end <= count &&
        parser->tree.roles[token] == PARSE_ROLE_PREFIX && parser->tokens.kinds[token] == PARSE_KIND_SIZEOF &&
        !check_input_vla_runtime_size(reader->types, check_stream_type(stream, graph->operand0[node])))
    {
        const bool grouped = parser->tree.roles[token + 1] == PARSE_ROLE_OPEN_SIZEOF;
        uint32_t root = BUSTER_CHECK_NONE;
        for (uint32_t at = token + 1; !grouped && root == BUSTER_CHECK_NONE && at < end; at += 1)
        {
            root = parser->tree.parents[at] == token ? at : root;
        }
        const uint32_t location = grouped ? token + 1 : root < count ? check_warning_graph_location(reader->output, parser, root) : BUSTER_CHECK_NONE;
        check_warning_graph_unevaluated(reader, grouped ? token + 2 : token + 1, grouped ? end - 1 : end, grouped ? end - 1 : token, location, end);
    }
}

// Whether the tokens from `first` start a type name: a type keyword, a qualifier, a tag's keyword, typeof, or a typedef's
// name as its declaration binds it.
static bool check_warning_graph_type_name(const CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint8_t kind = first < parser->tokens.count ? parser->tokens.kinds[first] : PARSE_KIND_COUNT;
    const uint32_t declaration = kind == PARSE_KIND_IDENTIFIER && first < reader->output->token_count ? reader->output->declarations[first] : BUSTER_CHECK_NONE;
    return kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_TYPEOF ||
           (declaration < parser->tokens.count && reader->output->declaration_kinds != NULL &&
            reader->output->declaration_kinds[declaration] == C_BINDING_TYPEDEF);
}

// The sizeof and alignof operations the constant reader reads (#1447): an array bound's, an enumerator's value's, which
// have no node (check_warning_graph_operand warns of the graph's). Their keywords found 64 tokens at a time (the scalar
// path's twin visits every token); an expression operand with a side effect warns as clang's
// CheckUnaryExprOrTypeTraitOperand does, a type name's not.
static void check_warning_graph_constant_queries(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const uint32_t count = parser->tokens.count;
    const bool scalar = BUSTER_SCALAR_PATH(parser->options.scalar);
    for (uint32_t first = 0; input->token_nodes != NULL && first < count && !reader->output->resource; first += 64)
    {
        uint64_t active = scalar ? check_input_scan_all(first, count) : check_input_scan_equal(parser->tokens.kinds, first, count, PARSE_KIND_SIZEOF);
        for (; active != 0 && !reader->output->resource; active &= active - 1)
        {
            const uint32_t keyword = first + (uint32_t)__builtin_ctzll(active);
            const bool nodeless = keyword < input->token_count && input->token_nodes[keyword] == BUSTER_CHECK_NONE;
            const bool query = nodeless && parser->tokens.kinds[keyword] == PARSE_KIND_SIZEOF && parser->tree.roles[keyword] == PARSE_ROLE_PREFIX && keyword + 1 < count;
            const bool grouped = query && parser->tree.roles[keyword + 1] == PARSE_ROLE_OPEN_SIZEOF;
            const uint32_t close = grouped ? parser->tree.parents[keyword + 1] : BUSTER_CHECK_NONE;
            if (grouped && close < count && close > keyword + 2 && !check_warning_graph_type_name(reader, keyword + 2))
            {
                check_warning_graph_unevaluated(reader, keyword + 2, close, close, keyword + 1, close + 1);
            }
            else if (query && !grouped && !check_warning_graph_type_name(reader, keyword + 1))
            {
                // (The operand is the keyword's subtree after it, the root among those tokens the one whose parent is it.)
                uint32_t end = keyword + 1;
                uint32_t root = BUSTER_CHECK_NONE;
                for (bool under = true; under && end < count; end += under ? 1 : 0)
                {
                    uint32_t at = end;
                    for (uint32_t steps = 0; at != keyword && at < count && steps < count; steps += 1)
                    {
                        root = parser->tree.parents[at] == keyword ? at : root;
                        at = parser->tree.parents[at];
                    }
                    under = at == keyword;
                }
                if (root < end)
                {
                    check_warning_graph_unevaluated(reader, keyword + 1, end, keyword, check_warning_graph_location(input, parser, root), end);
                }
            }
        }
    }
}

// The controlling expression of each _Generic (check_warning_graph_unevaluated): its keyword's kind found 64 tokens at a
// time (the scalar path's twin visits every token), its `)` the keyword's parent, its operand the tokens after its `(` up
// to its first separator, whose bracket depth is its first token's.
static void check_warning_graph_generics(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool scalar = BUSTER_SCALAR_PATH(parser->options.scalar);
    for (uint32_t first = 0; first < count && !reader->output->resource; first += 64)
    {
        uint64_t active = scalar ? check_input_scan_all(first, count) : check_input_scan_equal(parser->tokens.kinds, first, count, PARSE_KIND_GENERIC);
        for (; active != 0 && !reader->output->resource; active &= active - 1)
        {
            const uint32_t keyword = first + (uint32_t)__builtin_ctzll(active);
            const uint32_t close = parser->tokens.kinds[keyword] == PARSE_KIND_GENERIC ? parser->tree.parents[keyword] : BUSTER_CHECK_NONE;
            const uint32_t open = keyword + 1;
            if (close < count && close > open + 1 && parser->tree.roles[close] == PARSE_ROLE_CLOSE_KEYWORD && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN)
            {
                const uint32_t depth = parser->tree.depths != NULL ? parser->tree.depths[open + 1] : 0;
                uint32_t separator = open + 1;
                while (separator < close && !(parser->tokens.kinds[separator] == PARSE_KIND_COMMA && parser->tree.depths != NULL &&
                                               parser->tree.depths[separator] == depth))
                {
                    separator += 1;
                }
                uint32_t root = BUSTER_CHECK_NONE;
                for (uint32_t at = open + 1; root == BUSTER_CHECK_NONE && at < separator; at += 1)
                {
                    const uint32_t parent = parser->tree.parents[at];
                    root = parent < open + 1 || parent >= separator ? at : root;
                }
                if (separator < close && root < separator)
                {
                    check_warning_graph_unevaluated(reader, open + 1, separator, parser->tree.parents[root], check_warning_graph_location(reader->output, parser, root),
                                                    close + 1);
                }
            }
        }
    }
}

static bool check_input_warning_graph(CheckInput* input, const Parser* parser, TypeTable* types, const CheckStream* stream, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    CheckInput staged = *input;
    const CheckWarningEvents prefix = input->warning_events;
    CheckInputReader reader = {
        .output = &staged,
        .parser = parser,
        .types = types,
        .arena = output,
        .scratch = scratch,
        .ends = parser->ends,
        .warning_query_arena = output,
        .warning_bound_open = BUSTER_CHECK_NONE,
        .source_warning_keys = { .arena = output },
        .warning_events = {
            .arena = output,
            .events = (CheckWarningEvent*)prefix.events,
            .count = prefix.count,
            .capacity = prefix.count,
            .notes = (CheckWarningNote*)prefix.notes,
            .note_count = prefix.note_count,
            .note_capacity = prefix.note_count,
        },
    };
    bool valid = !input->resource && output != scratch && stream->count == input->graph.count && stream->status == CHECK_STATUS_OK;
    for (uint32_t i = 0; valid && i < prefix.count; i += 1)
    {
        const CheckWarningEvent* event = prefix.events + i;
        const uint64_t key = (uint64_t)event->kind << 32 | ((uint64_t)event->token + 1);
        valid = check_warning_set_add(&reader.source_warning_keys, key);
    }
    const CheckGraph* graph = &input->graph;
    uint32_t* void_candidates = NULL;
    uint32_t void_count = 0;
    uint32_t void_capacity = 0;
    for (uint32_t first = 0; valid && first < graph->count; first += 16)
    {
        const uint32_t live = graph->count - first < 16 ? graph->count - first : 16;
        uint32_t selected = (1u << live) - 1;
        if (!BUSTER_SCALAR_PATH(parser->options.scalar))
        {
            const __mmask16 lanes = (__mmask16)selected;
            const __m512i ops = _mm512_maskz_loadu_epi32(lanes, graph->ops + first);
            selected = _mm512_mask_cmpge_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_LESS)) &
                       _mm512_mask_cmple_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_GREATER_EQUAL));
            selected |= _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_ASSIGN)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_INITIALIZE)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_CONVERT)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_CALL)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_DEREFERENCE)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_SIZEOF)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_ALIGNOF)) |
                        _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_CAST));
        }
        for (; valid && selected != 0; selected &= selected - 1)
        {
            const uint32_t node = first + (uint32_t)__builtin_ctz(selected);
            const CheckOp op = graph->ops[node];
            if (stream->diagnostics[node] == CHECK_DIAG_NONE)
            {
                const uint32_t token = graph->tokens[node];
                const uint32_t completion = graph->source_positions[node];
                if (op >= CHECK_OP_LESS && op <= CHECK_OP_GREATER_EQUAL &&
                    (check_warning_graph_pointer_operand(types, stream, graph->operand0[node]) || check_warning_graph_pointer_operand(types, stream, graph->operand1[node])))
                {
                    CheckAction action;
                    check_graph_action(graph, stream, node, NULL, &action);
                    const CheckActionResult checked = { 0 };
                    CheckWarningEvent event;
                    if (check_warning_ordered_event(types, &action, &checked, token, completion, &event))
                    {
                        // Each operand's own spelling (#992): `__size_t` for a sizeof/alignof operand's result,
                        // a typedef specifier's declaration where a named object's base was a typedef name.
                        event.payload.name = check_input_operand_spelling(&reader, graph->operand0[node]);
                        event.payload.data = check_input_operand_spelling(&reader, graph->operand1[node]);
                        check_input_warning_once(&reader, &event);
                    }
                }
                else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT)
                {
                    const uint32_t slot = op == CHECK_OP_ASSIGN ? 1 : 0;
                    const uint32_t source = slot == 0 ? graph->operand0[node] : graph->operand1[node];
                    // The destination operand's naming token (#992): an INITIALIZE's `=` scans back to its
                    // specifiers and its node's type is the declared type; an ASSIGN's is its left operand's
                    // root; a CONVERT's is none (a `return` or cast).
                    const uint32_t lvalue_node = slot == 0 ? graph->operand1[node] : graph->operand0[node];
                    const uint32_t lvalue = op == CHECK_OP_INITIALIZE ? graph->tokens[node] :
                        op == CHECK_OP_ASSIGN && lvalue_node < graph->count ? graph->tokens[lvalue_node] : 0;
                    const TypeId declared = op == CHECK_OP_INITIALIZE ? graph->types[node] : 0;
                    if (check_warning_graph_null_candidate(graph, stream, source))
                    {
                        check_warning_graph_null(&reader, stream, source, stream->conversions[node].operands[slot].type, lvalue, declared, completion,
                                                 BUSTER_CHECK_NONE, 0);
                    }
                }
                else if (op == CHECK_OP_DEREFERENCE && check_warning_graph_void_candidate(&staged, parser, types, stream, node))
                {
                    // (Rare: a list that doubles, warned after the scan.)
                    if (void_count == void_capacity)
                    {
                        const uint32_t capacity = void_capacity == 0 ? 16 : void_capacity * 2;
                        uint32_t* grown = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, capacity);
                        if (grown != NULL && void_count != 0)
                        {
                            memcpy(grown, void_candidates, (uint64_t)void_count * sizeof(uint32_t));
                        }
                        staged.resource |= grown == NULL;
                        void_candidates = grown;
                        void_capacity = grown != NULL ? capacity : 0;
                    }
                    if (void_candidates != NULL)
                    {
                        void_candidates[void_count++] = node;
                    }
                }
                else if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
                {
                    check_warning_graph_operand(&reader, stream, node);
                }
                else if (op == CHECK_OP_CAST)
                {
                    check_warning_graph_pointer_cast(&reader, stream, node);
                    check_warning_graph_union_cast(&reader, stream, node);
                    check_warning_graph_nonscalar_cast(&reader, stream, node);
                }
                else if (op == CHECK_OP_CALL)
                {
                    // diagnose_if's and enable_if's conditions at the call (#1447, c_check_condition_graph.c).
                    if (input->condition_declaration_count != 0)
                    {
                        check_input_condition_call(&reader, stream, types, node);
                    }
                    const uint32_t begin = graph->argument_offsets[node];
                    for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                    {
                        const uint32_t argument = graph->argument_nodes[begin + i];
                        if (check_warning_graph_null_candidate(graph, stream, argument))
                        {
                            check_warning_graph_null(&reader, stream, argument, stream->argument_conversions[begin + i].type, 0, 0, token, node, i);
                        }
                    }
                }
            }
            valid = !staged.resource;
        }
    }
    if (valid && void_count != 0)
    {
        check_warning_graph_void_dereferences(&reader, stream, void_candidates, void_count);
        valid = !staged.resource;
    }
    if (valid)
    {
        check_warning_graph_generics(&reader);
        check_warning_graph_constant_queries(&reader);
        valid = !staged.resource;
    }
    if (valid && check_sparse_wide_present(input->noderef_types))
    {
        check_input_noderef_graph(&reader, stream);
        valid = !staged.resource;
    }
    if (valid && input->marked_member_count != 0)
    {
        check_input_member_uses(&reader, stream);
        valid = !staged.resource;
    }
    if (valid && input->marked_overload_count != 0)
    {
        check_input_overload_uses(&reader, stream);
        valid = !staged.resource;
    }
    if (valid && input->interrupt_calls)
    {
        // A direct call's error is clang's invalid call alone (#1447): a unit valid until then goes on to its later checks
        // as one whose only reports are recovered ones does (CheckInput.recovered_valid), and is rejected.
        const uint32_t before = staged.report_count;
        const bool clean = before == 0 || (staged.recovered_valid && staged.recovered_published == before);
        check_input_interrupt_calls(&reader);
        valid = !staged.resource;
        if (valid && clean && staged.report_count > before)
        {
            staged.recovered_published += staged.report_count - before;
            staged.recovered_valid = true;
        }
    }
    if (valid)
    {
        input->warning_events = staged.warning_events;
        input->noderef_values = staged.noderef_values;
        // The unavailable members' errors (check_input_member_uses) and the direct calls of interrupt service routines
        // (check_input_interrupt_calls), in the reports' capacity the reader gave them.
        input->report_count = staged.report_count;
        input->recovered_published = staged.recovered_published;
        input->recovered_valid = staged.recovered_valid;
        input->interrupt_ranges = staged.interrupt_ranges;
        input->interrupt_range_count = staged.interrupt_range_count;
        input->interrupt_range_capacity = staged.interrupt_range_capacity;
    }
    else
    {
        arena_pop_to(output, mark);
        input->resource = true;
    }
    return valid;
}
