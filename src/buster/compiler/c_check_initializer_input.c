// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Validate every source initializer after expression checking, including automatic objects. Compact retained
// plans own their converted subobject writes; a declaration-token column gives consumers direct access without
// reparsing or name lookup. This phase is required for source acceptance, independently of data/code emission.
// Include after c_check_input.c. Output and scratch are disjoint and the output remains live with CheckInput.
#ifndef BUSTER_C_CHECK_INITIALIZER_INPUT_INCLUDED
#define BUSTER_C_CHECK_INITIALIZER_INPUT_INCLUDED

typedef struct CheckInitializers CheckInitializers;
struct CheckInitializers
{
    CheckInitializer* plans;
    uint32_t* declaration_tokens;
    uint32_t count;
    uint32_t token_count;
    // Every original warning token, deduplicated across repeated range/compound planning.
    uint32_t* warning_tokens;
    uint32_t warning_count;
    CheckInitStatus status;
    TypeId failed_type;
    // A failed conversion's source node plus one and its declaration (CheckInitializer.failed_source, #147).
    uint32_t failed_source;
    // Its subobject's offset in bits (CheckInitializer.failed_offset).
    uint64_t failed_offset;
    uint32_t failed_declaration;
    uint32_t token;
    uint32_t completion;
};

// The plan's index plus one, or zero for a token with no explicit initializer. The producer writes declaration_tokens
// once in ascending token order; consumers need only this bounded lower bound, rather than a zero-filled token column.
static uint32_t check_initializers_plan_index(const CheckInitializers* initializers, uint32_t token)
{
    uint32_t base = 0;
    uint32_t length = initializers->count;
    while (length > 1)
    {
        const uint32_t half = length / 2;
        base = initializers->declaration_tokens[base + half] < token ? base + half : base;
        length -= half;
    }
    const uint32_t lower = length != 0 ? base + (initializers->declaration_tokens[base] < token) : 0;
    const uint32_t index = lower < initializers->count && initializers->declaration_tokens[lower] == token ? lower + 1 : 0;
    return index;
}

// Always inlined, as it was before #1447 grew the type readers it inlines past clang's threshold.
static inline __attribute__((always_inline)) bool check_init_object_candidate(const CheckInput* input, const TypeTable* types, uint32_t token)
{
    const bool initialized = input->objects != NULL && input->objects[token] != 0 &&
                             check_sparse_get(input->initializer_begins, token) < check_sparse_get(input->initializer_ends, token) && type_is_valid(types, input->declared_types[token]);
    const TypeId type = initialized ? type_strip_aligned(types, input->declared_types[token]) : 0;
    return initialized && check_vector_type(types, type) &&
           (type_qualifiers(type) & (BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE)) == BUSTER_TYPE_CONST;
}

// The declarations among tokens 0 to `end` - 1 that have an initializer (initializer_begins isn't BUSTER_CHECK_NONE), in
// token order into `declarations`, which has room for `end`: how many. Few of a unit's tokens declare with an initializer
// (#592), so the vector path tests 16 tokens at a time; the scalar path takes every token.
static uint32_t check_init_declarations(const CheckInput* input, uint32_t end, uint32_t* declarations, bool scalar)
{
    uint32_t count = 0;
    for (uint32_t first = 0; first < end; first += 16)
    {
        const uint32_t lanes = scalar ? (uint32_t)check_input_scan_all(first, end) & 0xffff :
                                        check_sparse_below(input->initializer_begins, first, end, BUSTER_CHECK_NONE);
        for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
            declarations[count] = token;
            count += check_sparse_get(input->initializer_begins, token) != BUSTER_CHECK_NONE;
        }
    }
    return count;
}

// The table of constant objects' bytes, with room for the candidates among `declarations` (check_init_declarations')
// whose initializers end by `cutoff`: an object can only be a candidate where its declaration has an initializer.
static bool check_init_objects_open(CheckInitObjects* objects, const CheckInput* input, const TypeTable* types, const uint32_t* declarations,
                                    uint32_t declaration_count, uint32_t cutoff, Arena* scratch)
{
    *objects = (CheckInitObjects){ 0 };
    uint32_t count = 0;
    for (uint32_t d = 0; d < declaration_count; d += 1)
    {
        const uint32_t token = declarations[d];
        count += check_init_object_candidate(input, types, token) && check_sparse_get(input->initializer_ends, token) <= cutoff;
    }
    uint64_t capacity = count != 0 ? 1 : 0;
    while (capacity < (uint64_t)count * 2)
    {
        capacity *= 2;
    }
    bool valid = capacity <= UINT32_MAX;
    if (valid && capacity != 0)
    {
        objects->entries = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitObject, capacity);
        valid = objects->entries != NULL;
        if (valid)
        {
            memset(objects->entries, 0, capacity * sizeof(CheckInitObject));
            objects->capacity = (uint32_t)capacity;
        }
    }
    return valid;
}

// This reader only consumes a completed plan. It cannot invoke expression evaluation or follow an object
// reference, so source-ordered constant-object retention adds no recursive dependency traversal.
static bool check_init_object_bytes(const TypeTable* types, const CheckInitializer* plan, uint8_t* bytes, uint64_t size)
{
    bool valid = size <= UINT64_MAX / 8 && size == plan->object_size && plan->status == CHECK_INIT_OK;
    if (valid)
    {
        memset(bytes, 0, size);
    }
    for (uint32_t i = 0; valid && i < plan->count; i += 1)
    {
        const CheckInitPart* part = plan->parts + i;
        uint64_t bits = 0;
        uint8_t scalar[16] = { 0 };
        const uint8_t* source = part->bytes;
        valid = check_initializer_part_bits(types, part, &bits) == CHECK_INIT_OK && part->bit_offset <= size * 8 && bits <= size * 8 - part->bit_offset;
        if (valid && part->kind == CHECK_INIT_VALUE)
        {
            valid = bits <= sizeof(scalar) * 8 && check_init_scalar_bytes(types, &part->value, scalar);
            source = scalar;
        }
        if (valid && part->kind != CHECK_INIT_CLEAR)
        {
            valid = bits == 0 || source != NULL;
        }
        if (valid && (part->bit_offset % 8) == 0 && (bits % 8) == 0)
        {
            if (part->kind == CHECK_INIT_CLEAR)
            {
                memset(bytes + part->bit_offset / 8, 0, bits / 8);
            }
            else if (bits != 0)
            {
                memcpy(bytes + part->bit_offset / 8, source, bits / 8);
            }
        }
        else if (valid)
        {
            for (uint64_t bit = 0; bit < bits; bit += 1)
            {
                const uint64_t destination = part->bit_offset + bit;
                const uint8_t mask = (uint8_t)(1u << (destination % 8));
                const bool set = part->kind != CHECK_INIT_CLEAR && ((source[bit / 8] >> (bit % 8)) & 1) != 0;
                bytes[destination / 8] = (uint8_t)((bytes[destination / 8] & ~mask) | (set ? mask : 0));
            }
        }
    }
    return valid;
}

static bool check_init_object_retain(CheckInitObjects* objects, const CheckInput* input, const CheckStream* stream,
                                      const Parser* parser, TypeTable* types, uint32_t token, const CheckInitializer* plan,
                                      bool diagnostic_only, Arena* output, Arena* scratch)
{
    bool valid = true;
    if (check_init_object_candidate(input, types, token))
    {
        const uint32_t slot = check_init_object_slot(objects, input->objects[token]);
        const uint64_t mark = scratch->length;
        const uint64_t size = type_size(types, input->declared_types[token]);
        uint8_t* bytes = size != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, size) : NULL;
        valid = slot != BUSTER_CHECK_NONE && bytes != NULL;
        bool known = valid && check_init_object_bytes(types, plan, bytes, size);
        if (valid && !known && !check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC))
        {
            // Automatic const vectors may have runtime initializers. A bounded static-mode attempt reuses
            // earlier retained bytes; an ordinary nonconstant result does not reject the automatic object.
            const uint64_t output_mark = output->length;
            CheckInitializer constant = { 0 };
            const bool accepted = check_initializer_plan_warnings(types, parser, input->token_nodes, stream, input->strings, input->string_count, input->string_ids,
                                       input->declared_types[token], check_sparse_get(input->initializer_begins, token), check_sparse_get(input->initializer_ends, token),
                                       true, diagnostic_only, true, output, scratch, &constant, &input->graph, NULL, objects);
            valid = accepted || constant.status != CHECK_INIT_RESOURCE;
            known = accepted && check_init_object_bytes(types, &constant, bytes, size);
            arena_pop_to(output, output_mark);
        }
        if (known)
        {
            objects->entries[slot] = (CheckInitObject){
                .object = input->objects[token],
                .end = check_sparse_get(input->initializer_ends, token),
                .type = type_unqualified(type_strip_aligned(types, input->declared_types[token])),
                .bytes = bytes,
            };
        }
        else
        {
            arena_pop_to(scratch, mark);
        }
    }
    return valid;
}

static bool check_init_warning_collector(CheckInitWarningCollector* collector, CheckInitWarningSink* sink, uint32_t token_count, Arena* scratch)
{
    *collector = (CheckInitWarningCollector){ .sink = sink, .token_count = token_count };
    bool valid = sink == NULL || (sink->count <= sink->capacity && (sink->capacity == 0 || sink->tokens != NULL));
    if (valid && sink != NULL)
    {
        collector->seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, token_count);
        valid = token_count == 0 || collector->seen != NULL;
        if (valid)
        {
            if (token_count != 0)
            {
                memset(collector->seen, 0, token_count);
            }
            for (uint32_t i = 0; valid && i < sink->count; i += 1)
            {
                valid = sink->tokens[i] < token_count;
                if (valid)
                {
                    collector->seen[sink->tokens[i]] = 2;
                }
            }
        }
    }
    return valid;
}

static void check_initializers_noderef(CheckInitWarningSink* warnings, const CheckInput* input, uint32_t declaration)
{
    if (warnings != NULL)
    {
        warnings->noderef = (CheckNoderefInitializer){
            .values = input->noderef_values,
            .root = check_input_noderef_at(input, declaration),
            .members = input->noderef_members,
            .member_count = input->noderef_member_count,
        };
    }
}

// The warnings' tokens the collector added to its sink from `first` on, in token order: the tokens it saw, 64 at a time
// (the scalar path takes every token), where it added any.
static void check_init_warning_order(CheckInitWarningCollector* collector, uint32_t first, bool scalar)
{
    if (collector->sink != NULL && collector->sink->count != first)
    {
        uint32_t at = first;
        for (uint32_t token = 0; token < collector->token_count; token += 64)
        {
            const uint64_t lanes = scalar ? check_input_scan_all(token, collector->token_count) :
                                            check_input_scan_equal(collector->seen, token, collector->token_count, 1);
            for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t warned = token + (uint32_t)__builtin_ctzll(selected);
                if (collector->seen[warned] == 1)
                {
                    collector->sink->tokens[at] = warned;
                    at += 1;
                }
            }
        }
    }
}

// One declaration's failed initializer, as CheckInitDiagnostic's first is given (#1447, phase 2).
typedef struct CheckInitFailure CheckInitFailure;
struct CheckInitFailure
{
    CheckInitStatus status;
    TypeId failed_type;
    uint32_t failed_source;
    // Its subobject's offset in bits (CheckInitializer.failed_offset).
    uint64_t failed_offset;
    uint32_t failed_declaration;
    uint32_t token;
    uint32_t completion;
};

typedef struct CheckInitDiagnostic CheckInitDiagnostic;
struct CheckInitDiagnostic
{
    // Every declaration's failed initializer, in declaration order (#1447, phase 2): clang reports each, where the fields
    // below keep the first. In `output` when there is more than one, else NULL with failure_count at most 1.
    CheckInitFailure* failures;
    uint32_t failure_count;
    CheckInitStatus status;
    TypeId failed_type;
    // A failed conversion's source node plus one and its declaration (CheckInitializer.failed_source, #147).
    uint32_t failed_source;
    // Its subobject's offset in bits (CheckInitializer.failed_offset).
    uint64_t failed_offset;
    uint32_t failed_declaration;
    uint32_t token;
    uint32_t completion;
    uint32_t warning_count;
    uint32_t warning_token;
};

// Whether a static object's scalar initializer, whose INITIALIZE (`initialize`, to the declared `target`) failed as one of
// clang's conversions it warns of and still makes (-Wint-conversion's between a pointer and an integer,
// -Wincompatible-pointer-types' between pointers: errors by default), is no constant after that conversion, which clang
// reports next, at the initializer (#147): an object's value read, or an object's address converted to an integer
// narrower than a pointer (an absolute address, `(int *)5`'s, stays a constant). Another failed conversion, of a record or a
// floating value, is the initializer's only error.
// An array of unknown size's recovered "array initializer must be an initializer list" (check_input_initializer_finish,
// #1447) whose initializer is no valid value, which clang reports alone: an undeclared name in it that no statement
// expression shields, or its outermost node (its last token's, as check_init_node takes it) diagnosed, of no valid type
// or past a missing name's diagnostic limit, or with an operand or argument that is (the initializers' diagnosis's
// `complete`). An initializer with no node keeps its report. A statement expression's inner errors leave its
// value valid, as clang's. Such reports leave `input->reports`; only a unit with another error drops one, so a rejected
// unit always says why. Error-free units and those without the report don't look.
// Whether a source error at `token`, in an initializer from `begin`, makes the initializer no value: unless a
// statement expression shields it, a statement of it before its last (its value) holding the name. From the innermost
// statement expression around the name outward, the statement that holds it (from it to the next `;` of that block,
// past nested groups) must be the block's last, its `;` right before the block's `}`. Bounded by the initializer's tokens.
static bool check_init_source_poisons(const Parser* parser, uint32_t begin, uint32_t token)
{
    const uint8_t* kinds = parser->tokens.kinds;
    const uint32_t* parents = parser->tree.parents;
    bool poisons = true;
    uint32_t at = token;
    bool enclosed = true;
    while (poisons && enclosed)
    {
        // The innermost statement expression's `(` before `at` whose group holds it.
        uint32_t open = BUSTER_CHECK_NONE;
        for (uint32_t j = begin; j + 1 < at; j += 1)
        {
            const bool statement = kinds[j] == PARSE_KIND_L_PAREN && kinds[j + 1] == PARSE_KIND_L_BRACE && parents[j] > at;
            open = statement ? j : open;
        }
        enclosed = open != BUSTER_CHECK_NONE;
        const uint32_t close = enclosed ? parents[open + 1] : 0;
        uint32_t semicolon = BUSTER_CHECK_NONE;
        for (uint32_t j = at + 1; enclosed && semicolon == BUSTER_CHECK_NONE && j < close; j += 1)
        {
            const uint8_t kind = kinds[j];
            const bool opener = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_L_SQUARE;
            semicolon = kind == PARSE_KIND_SEMI ? j : semicolon;
            j = opener && parents[j] > j && parents[j] < close ? parents[j] : j;
        }
        poisons = !enclosed || semicolon + 1 == close;
        at = enclosed ? open : at;
    }
    return poisons;
}

static void check_init_listless_drop(CheckInput* input, const Parser* parser, const CheckStream* stream, const TypeTable* types, Arena* scratch)
{
    uint32_t listless = 0;
    for (uint32_t r = 0; r < input->report_count; r += 1)
    {
        listless += input->reports[r].recovered && input->reports[r].message == CHECK_INPUT_DIRECT_INITIALIZER_ARRAY && input->reports[r].completion != 0;
    }
    const uint64_t mark = scratch->length;
    const CheckGraph* graph = &input->graph;
    uint8_t* complete = listless != 0 && stream->count != 0 && stream->count == graph->count && input->token_nodes != NULL ?
                        BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, stream->count) : NULL;
    // With no graph (a walk that ended before any expression), no initializer has a value.
    const bool nodes_ready = listless != 0 && (complete != NULL || stream->count == 0);
    for (uint32_t node = 0; complete != NULL && node < stream->count; node += 1)
    {
        bool checked = graph->tokens[node] < check_input_diagnostic_limit(input, graph->tokens[node]) &&
                       stream->diagnostics[node] == CHECK_DIAG_NONE && type_is_valid(types, check_stream_type(stream, node));
        const uint32_t operands[] = {
            graph->operand0[node],
            graph->operand1[node],
            graph->operand2[node],
        };
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(operands); i += 1)
        {
            checked = operands[i] == BUSTER_CHECK_NONE || (operands[i] < node && complete[operands[i]]);
        }
        if (checked && check_graph_has_arguments(graph->ops[node]) && graph->argument_counts != 0)
        {
            const uint32_t first = graph->argument_offsets != 0 ? graph->argument_offsets[node] : 0;
            const uint32_t count = graph->argument_counts[node];
            checked = first <= graph->argument_count && count <= graph->argument_count - first && (count == 0 || graph->argument_nodes != 0);
            for (uint32_t i = 0; checked && i < count; i += 1)
            {
                const uint32_t operand = graph->argument_nodes[first + i];
                checked = operand < node && complete[operand];
            }
        }
        complete[node] = checked;
    }
    const bool other_errors = input->report_count > listless || stream->report_count != 0;
    uint32_t kept = 0;
    for (uint32_t r = 0; r < input->report_count; r += 1)
    {
        const CheckInputReport* report = input->reports + r;
        const bool candidate = nodes_ready && other_errors && report->recovered && report->message == CHECK_INPUT_DIRECT_INITIALIZER_ARRAY &&
                               report->completion != 0;
        uint32_t outermost = BUSTER_CHECK_NONE;
        for (uint64_t at = report->value; candidate && complete != NULL && at < report->completion && at < input->token_count; at += 1)
        {
            const uint32_t node = input->token_nodes[at];
            outermost = node < stream->count && (outermost == BUSTER_CHECK_NONE || node > outermost) ? node : outermost;
        }
        bool undeclared = false;
        for (uint32_t u = 0; candidate && !undeclared && u < input->report_count; u += 1)
        {
            const CheckInputReport* name = input->reports + u;
            undeclared = (name->message == CHECK_INPUT_UNDECLARED || name->message == CHECK_INPUT_INVALID_USE || name->message == CHECK_INPUT_AMBIGUOUS) &&
                         name->token >= report->value && name->token < report->completion &&
                         check_init_source_poisons(parser, (uint32_t)report->value, name->token);
        }
        // No node is no evidence: a rejected unit's retained graph ends at its first missing name, so a later valid
        // initializer has none (review on #1791). The missing names in it are, read from the reports.
        const bool dropped = candidate && (undeclared || (outermost != BUSTER_CHECK_NONE && !complete[outermost]));
        input->reports[kept] = *report;
        kept += !dropped;
        input->recovered_published -= dropped;
    }
    input->report_count = kept;
    arena_pop_to(scratch, mark);
}

static bool check_init_value_nonconstant(const CheckStream* stream, const TypeTable* types, TypeId target, uint32_t source)
{
    const bool known = source < stream->count && stream->diagnostics[source] == CHECK_DIAG_NONE;
    const CheckValue value = known ? check_stream_value(stream, source) : (CheckValue){ 0 };
    const uint8_t to = check_action_kind(types, type_conversion_operand(types, target));
    const uint8_t from = known ? check_action_kind(types, type_conversion_operand(types, value.type)) : BUSTER_TYPE_KIND_VOID;
    const bool designator = type_kind_is_array(from) || from == BUSTER_TYPE_KIND_FUNCTION;
    const bool pointer = from == BUSTER_TYPE_KIND_POINTER || designator;
    const bool integer = from == BUSTER_TYPE_KIND_INTEGER || from == BUSTER_TYPE_KIND_ENUM || from == BUSTER_TYPE_KIND_BIT_INT;
    // (An enumeration's and a _BitInt's targets are integers too, as check_driver_conversion_kind takes them.)
    const bool to_integer = to == BUSTER_TYPE_KIND_INTEGER || to == BUSTER_TYPE_KIND_ENUM || to == BUSTER_TYPE_KIND_BIT_INT;
    const bool converted = (to_integer && pointer) || (to == BUSTER_TYPE_KIND_POINTER && (pointer || integer));
    const bool read = value.category == CHECK_LVALUE && !designator;
    const bool narrowed = to_integer && value.constant_kind == CHECK_CONSTANT_ADDRESS && value.object != 0 && type_size(types, target) < 8;
    return converted && (read || !check_value_static_initializer(&value, false) || narrowed);
}

// check_init_value_nonconstant of a scalar initializer's INITIALIZE `initialize`, its value its operand.
static bool check_init_converted_nonconstant(const CheckInput* input, const CheckStream* stream, const TypeTable* types, TypeId target, uint32_t initialize)
{
    return check_init_value_nonconstant(stream, types, target, input->graph.operand0[initialize]);
}

// Diagnose complete roots before before_token (exclusive). A report elsewhere in the unit no longer cuts the
// scan: the declaration that holds it is excluded by its nodes' completeness instead, so a later declaration's
// independent warnings survive (#992).
// BUSTER_CHECK_NONE supplies no additional cutoff. The real stream is never edited or relabeled successful.
// Each selected root's mapped nodes must have a checked dependency closure within the cutoff. Missing structural
// nodes are normal; a missing value/designator root makes the planner decline that root without inventing a
// diagnostic. Source-produced diagnostic_graph retains exactly such complete earlier expression fragments.
//
// True means the diagnostic scan completed, including a semantic error in result. False means resource failure
// or an unusable input/stream, with the corresponding status. No CheckInitializers or parts are published, and
// both arenas always return to their entry lengths. The strict publishing API below remains unchanged.
// Optional warning sink storage belongs to the caller and precedes both arena suffixes. It receives distinct
// original token IDs in source order, including warnings before a later semantic failure. capacity == parser->tokens.count is
// sufficient for an empty sink. Resource/input failure restores its entry count; arenas always restore fully.
static bool check_initializers_diagnose_warnings(CheckInitDiagnostic* result, const CheckInput* input, const CheckStream* stream,
                                                 const Parser* parser, TypeTable* types, uint32_t before_token, Arena* output, Arena* scratch,
                                                 CheckInitWarningSink* warnings)
{
    *result = (CheckInitDiagnostic){
        .token = BUSTER_CHECK_NONE,
        .warning_token = BUSTER_CHECK_NONE,
    };
    const uint64_t mark = output != 0 ? output->length : 0;
    const uint64_t temporary = scratch != 0 ? scratch->length : 0;
    const uint32_t warning_start = warnings != NULL ? warnings->count : 0;
    CheckWarningBuffer* semantic = warnings != NULL ? warnings->events : NULL;
    CheckWarningSet* semantic_keys = warnings != NULL ? warnings->event_keys : NULL;
    const CheckWarningBuffer semantic_before = semantic != NULL ? *semantic : (CheckWarningBuffer){ 0 };
    const CheckWarningSet keys_before = semantic_keys != NULL ? *semantic_keys : (CheckWarningSet){ 0 };
    const uint64_t semantic_mark = semantic != NULL && semantic->arena != NULL ? semantic->arena->length : 0;
    bool valid = input != 0 && stream != 0 && parser != 0 && types != 0 && output != 0 && scratch != 0 && output != scratch;
    if (valid)
    {
        valid = input->token_count == parser->tokens.count && input->token_count < UINT32_MAX && stream->count == input->graph.count &&
                stream->status == CHECK_STATUS_OK && !input->resource && (input->report_count == 0 || input->diagnostic_graph) &&
                (input->token_count == 0 || (check_sparse_present(input->initializer_begins) && check_sparse_present(input->initializer_ends) && input->declared_types != 0 &&
                 input->declaration_bits != 0 && input->token_nodes != 0)) && (input->report_count == 0 || input->reports != 0) &&
                (stream->report_count == 0 || stream->reports != 0) && (stream->count == 0 ||
                 (check_stream_has_values(stream) && stream->diagnostics != 0 && input->graph.ops != 0 && input->graph.tokens != 0 &&
                  input->graph.operand0 != 0 && input->graph.operand1 != 0 && input->graph.operand2 != 0));
    }
    if (!valid)
    {
        result->status = input != 0 && input->resource ? CHECK_INIT_RESOURCE :
                         stream != 0 && stream->status == CHECK_STATUS_RESOURCE ? CHECK_INIT_RESOURCE : CHECK_INIT_CHECK_FAILED;
    }
    // Not the first report's token: an unrelated expression or type error earlier in the unit must not keep
    // a later declaration's independent warnings (many-braces, initializer-overrides, non-literal-null) from
    // being collected, as clang still reports them (#992). The declaration that holds the error is excluded
    // by `complete` instead: its nodes are diagnosed or incomplete there.
    uint32_t cutoff = valid ? input->token_count : 0;
    if (before_token < cutoff)
    {
        cutoff = before_token;
    }
    uint8_t* complete = valid && stream->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, stream->count) : 0;
    if (valid && stream->count != 0 && complete == 0)
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    for (uint32_t node = 0; valid && node < stream->count; node += 1)
    {
        const CheckGraph* graph = &input->graph;
        const uint32_t local = check_input_diagnostic_limit(input, graph->tokens[node]);
        const uint32_t limit = local < cutoff ? local : cutoff;
        bool checked = graph->tokens[node] < limit && (graph->source_positions == 0 || graph->source_positions[node] <= limit) &&
                       stream->diagnostics[node] == CHECK_DIAG_NONE && type_is_valid(types, check_stream_type(stream, node));
        const uint32_t operands[] = {
            graph->operand0[node],
            graph->operand1[node],
            graph->operand2[node],
        };
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(operands); i += 1)
        {
            checked = operands[i] == BUSTER_CHECK_NONE || (operands[i] < node && complete[operands[i]]);
        }
        if (checked && check_graph_has_arguments(graph->ops[node]) && graph->argument_counts != 0)
        {
            const uint32_t first = graph->argument_offsets != 0 ? graph->argument_offsets[node] : 0;
            const uint32_t count = graph->argument_counts[node];
            checked = first <= graph->argument_count && count <= graph->argument_count - first && (count == 0 || graph->argument_nodes != 0);
            for (uint32_t i = 0; checked && i < count; i += 1)
            {
                const uint32_t operand = graph->argument_nodes[first + i];
                checked = operand < node && complete[operand];
            }
        }
        complete[node] = checked;
    }
    // The declarations with initializers before the cutoff: those of any other token begin at BUSTER_CHECK_NONE, which
    // makes them ineligible below.
    uint32_t* declarations = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)cutoff + 1) : 0;
    const uint32_t declaration_count = declarations != 0 ? check_init_declarations(input, cutoff, declarations, parser->options.scalar) : 0;
    if (valid && declarations == 0)
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    CheckInitFailure* failures = valid && declaration_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitFailure, declaration_count) : NULL;
    if (valid && declaration_count != 0 && failures == NULL)
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    CheckInitWarningCollector collector = { 0 };
    if (valid && !check_init_warning_collector(&collector, warnings, parser->tokens.count, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    CheckInitObjects objects = { 0 };
    if (valid && !check_init_objects_open(&objects, input, types, declarations, declaration_count, cutoff, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    for (uint32_t d = 0; valid && d < declaration_count; d += 1)
    {
        const uint32_t declaration = declarations[d];
        const uint32_t begin = check_sparse_get(input->initializer_begins, declaration);
        const uint32_t end = check_sparse_get(input->initializer_ends, declaration);
        // A scalar initializer's INITIALIZE (CheckInput.initializer_nodes) is at the declarator, outside the initializer's
        // tokens: the conversion error it has is clang's one for the initializer, which isn't checked again here (#147).
        const uint32_t initialize = check_sparse_get(input->initializer_nodes, declaration);
        const bool placed = begin < end && end <= cutoff && end <= check_input_diagnostic_limit(input, begin) && type_is_valid(types, input->declared_types[declaration]);
        bool eligible = placed && (initialize >= stream->count || complete[initialize]);
        for (uint32_t token = begin; eligible && token < end; token += 1)
        {
            const uint32_t node = input->token_nodes[token];
            eligible = node == BUSTER_CHECK_NONE || (node < stream->count && complete[node]);
            // An atomic compound literal has no value: its own initializer reports the illegal atomic type at `{`
            // (#2441). Do not also diagnose the initializer that uses it. The literal's declaration is at `(`, outside
            // its own initializer span, so that span remains eligible and independent later declarations still run.
            const bool compound = eligible && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_COMPOUND) != 0;
            const TypeId literal = compound ? input->declared_types[token] : 0;
            const uint32_t body = compound ? check_sparse_get(input->initializer_begins, token) : BUSTER_CHECK_NONE;
            const bool atomic_literal = body < end && parser->tree.roles[body] == PARSE_ROLE_OPEN_COMPOUND_BODY &&
                                        type_is_valid(types, literal) && type_kind(types, type_strip_aligned(types, literal)) == BUSTER_TYPE_KIND_ATOMIC &&
                                        check_init_source_poisons(parser, begin, token);
            eligible &= !atomic_literal;
        }
        if (placed && !eligible && initialize < stream->count && stream->diagnostics[initialize] == CHECK_DIAG_INCOMPATIBLE &&
            check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) != 0 && begin < result->token &&
            check_init_converted_nonconstant(input, stream, types, input->declared_types[declaration], initialize))
        {
            result->status = CHECK_INIT_NON_CONSTANT;
            result->token = begin;
            result->completion = end;
        }
        if (placed && !eligible && initialize < stream->count && stream->diagnostics[initialize] == CHECK_DIAG_INCOMPATIBLE &&
            check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) != 0 &&
            check_init_converted_nonconstant(input, stream, types, input->declared_types[declaration], initialize))
        {
            failures[result->failure_count++] = (CheckInitFailure){ .status = CHECK_INIT_NON_CONSTANT, .token = begin, .completion = end };
        }
        if (eligible)
        {
            CheckInitializer plan = { 0 };
            check_initializers_noderef(warnings, input, declaration);
            const bool accepted = check_initializer_plan_warnings(types, parser, input->token_nodes, stream, input->strings, input->string_count, input->string_ids,
                                                         input->declared_types[declaration], begin, end, check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) != 0,
                                                         true, true, output, scratch, &plan, &input->graph, warnings != NULL ? &collector : NULL, &objects);
            const bool retained = !accepted || check_init_object_retain(&objects, input, stream, parser, types, declaration, &plan, true, output, scratch);
            if (plan.warning_count != 0)
            {
                result->warning_count += 1;
                result->warning_token = plan.warning_token < result->warning_token ? plan.warning_token : result->warning_token;
            }
            if (!retained || (!accepted && plan.status == CHECK_INIT_RESOURCE))
            {
                valid = false;
                result->status = CHECK_INIT_RESOURCE;
                result->token = retained ? plan.failed_token : begin;
            }
            else if (!accepted && plan.status != CHECK_INIT_CHECK_FAILED && plan.failed_token < result->token)
            {
                result->status = plan.status;
                result->failed_type = plan.failed_type;
                result->failed_source = plan.failed_source;
                result->failed_offset = plan.failed_offset;
                result->failed_declaration = declaration;
                result->token = plan.failed_token;
                result->completion = end;
            }
            if (valid && !accepted && plan.status != CHECK_INIT_CHECK_FAILED)
            {
                failures[result->failure_count++] = (CheckInitFailure){
                    .status = plan.status,
                    .failed_type = plan.failed_type,
                    .failed_source = plan.failed_source,
                    .failed_offset = plan.failed_offset,
                    .failed_declaration = declaration,
                    .token = plan.failed_token,
                    .completion = end,
                };
            }
            arena_pop_to(output, mark);
        }
    }
    if (valid)
    {
        check_init_warning_order(&collector, warning_start, parser->options.scalar);
    }
    if (output != 0)
    {
        arena_pop_to(output, mark);
    }
    // The failures outlive the walk's scratch in `output` when there is more than one to give.
    CheckInitFailure* kept = valid && result->failure_count > 1 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckInitFailure, result->failure_count) : NULL;
    if (kept != NULL)
    {
        memcpy(kept, failures, (uint64_t)result->failure_count * sizeof(CheckInitFailure));
    }
    else if (valid && result->failure_count > 1)
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    result->failures = kept;
    result->failure_count = valid ? result->failure_count : 0;
    if (scratch != 0)
    {
        arena_pop_to(scratch, temporary);
    }
    if (warnings != NULL)
    {
        warnings->count = valid ? warnings->count : warning_start;
        result->warning_count = valid ? warnings->count - warning_start : 0;
        if (!valid)
        {
            result->warning_token = BUSTER_CHECK_NONE;
        }
    }
    if (!valid && semantic != NULL)
    {
        if (semantic->arena != NULL)
        {
            arena_pop_to(semantic->arena, semantic_mark);
        }
        *semantic = semantic_before;
        if (semantic_keys != NULL)
        {
            *semantic_keys = keys_before;
        }
    }
    return valid;
}

static bool check_initializers_diagnose(CheckInitDiagnostic* result, const CheckInput* input, const CheckStream* stream,
                                        const Parser* parser, TypeTable* types, uint32_t before_token, Arena* output, Arena* scratch)
{
    return check_initializers_diagnose_warnings(result, input, stream, parser, types, before_token, output, scratch, NULL);
}

static bool check_initializers_run_events(CheckInitializers* result, const CheckInput* input, const CheckStream* stream,
                                          const Parser* parser, TypeTable* types, Arena* output, Arena* scratch,
                                          CheckWarningBuffer* semantic, CheckWarningSet* semantic_keys)
{
    *result = (CheckInitializers){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const CheckWarningBuffer semantic_before = semantic != NULL ? *semantic : (CheckWarningBuffer){ 0 };
    const CheckWarningSet keys_before = semantic_keys != NULL ? *semantic_keys : (CheckWarningSet){ 0 };
    const uint64_t semantic_mark = semantic != NULL && semantic->arena != NULL ? semantic->arena->length : 0;
    const uint32_t tokens = input->token_count;
    bool valid = output != scratch && tokens == parser->tokens.count && tokens < UINT32_MAX &&
                 stream->count == input->graph.count && (tokens == 0 ||
                 (check_sparse_present(input->initializer_begins) && check_sparse_present(input->initializer_ends) && input->declared_types != 0 &&
                  input->declaration_bits != 0 && input->token_nodes != 0));
    result->status = valid ? CHECK_INIT_OK : CHECK_INIT_INVALID;
    // A unit whose only reports are recovered ones (#1447) is checked as a valid one.
    if (valid && (input->resource || (input->report_count != 0 && !input->recovered_valid) || stream->status != CHECK_STATUS_OK || stream->report_count != 0))
    {
        valid = false;
        result->status = input->resource || stream->status == CHECK_STATUS_RESOURCE ? CHECK_INIT_RESOURCE : CHECK_INIT_CHECK_FAILED;
    }
    // The declarations with initializers, which the checks, the objects' table and the plans visit rather than every token
    // (#592).
    uint32_t* declarations = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1) : 0;
    const uint32_t count = declarations != 0 ? check_init_declarations(input, tokens, declarations, parser->options.scalar) : 0;
    if (valid && declarations == 0)
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    for (uint32_t d = 0; valid && d < count; d += 1)
    {
        const uint32_t token = declarations[d];
        const uint32_t begin = check_sparse_get(input->initializer_begins, token);
        const uint32_t end = check_sparse_get(input->initializer_ends, token);
        valid = begin < end && end <= tokens && type_is_valid(types, input->declared_types[token]);
        if (!valid)
        {
            result->status = CHECK_INIT_INVALID;
            result->token = token;
        }
    }
    if (valid)
    {
        result->plans = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckInitializer, count) : 0;
        result->declaration_tokens = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count) : 0;
        result->warning_tokens = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, tokens) : 0;
        valid = count == 0 || (result->plans != 0 && result->declaration_tokens != 0 && result->warning_tokens != 0);
        result->status = valid ? CHECK_INIT_OK : CHECK_INIT_RESOURCE;
    }
    CheckInitWarningSink warnings = {
        .tokens = result->warning_tokens,
        .capacity = count != 0 ? tokens : 0,
        .events = semantic,
        .event_keys = semantic_keys,
    };
    CheckInitWarningCollector collector = { 0 };
    if (valid && !check_init_warning_collector(&collector, &warnings, tokens, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    CheckInitObjects objects = { 0 };
    if (valid && !check_init_objects_open(&objects, input, types, declarations, count, tokens, scratch))
    {
        valid = false;
        result->status = CHECK_INIT_RESOURCE;
    }
    const CheckInputLiteralTable* table = input->literal_tables;
    for (uint32_t at = 0; valid && at < count; at += 1)
    {
        const uint32_t token = declarations[at];
        CheckInitializer* plan = result->plans + at;
        while (table != NULL && table->declaration < token)
        {
            table = table->next;
        }
        if (table != NULL && table->declaration == token)
        {
            *plan = table->plan;
        }
        else
        {
            check_initializers_noderef(&warnings, input, token);
            valid = check_initializer_plan_warnings(types, parser, input->token_nodes, stream, input->strings, input->string_count, input->string_ids,
                                      input->declared_types[token], check_sparse_get(input->initializer_begins, token), check_sparse_get(input->initializer_ends, token),
                                      check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC) != 0, false, true, output, scratch, plan, &input->graph, &collector, &objects);
        }
        if (valid && !check_init_object_retain(&objects, input, stream, parser, types, token, plan, false, output, scratch))
        {
            valid = false;
            plan->status = CHECK_INIT_RESOURCE;
            plan->failed_token = check_sparse_get(input->initializer_begins, token);
        }
        if (valid)
        {
            result->declaration_tokens[at] = token;
        }
        else
        {
            result->status = plan->status;
            result->failed_type = plan->failed_type;
            result->failed_source = plan->failed_source;
            result->failed_offset = plan->failed_offset;
            result->failed_declaration = token;
            result->token = plan->failed_token;
            result->completion = check_sparse_get(input->initializer_ends, token);

        }
    }
    if (valid)
    {
        check_init_warning_order(&collector, 0, parser->options.scalar);
        result->count = count;
        result->token_count = tokens;
        result->warning_count = warnings.count;
    }
    else
    {
        const CheckInitStatus status = result->status;
        const TypeId failed_type = result->failed_type;
        const uint32_t failed_source = result->failed_source;
        const uint64_t failed_offset = result->failed_offset;
        const uint32_t failed_declaration = result->failed_declaration;
        const uint32_t token = result->token;
        const uint32_t completion = result->completion;
        arena_pop_to(output, output_mark);
        *result = (CheckInitializers){ .status = status, .failed_type = failed_type, .failed_source = failed_source, .failed_offset = failed_offset, .failed_declaration = failed_declaration,
                                       .token = token, .completion = completion };
        if (semantic != NULL)
        {
            if (semantic->arena != NULL)
            {
                arena_pop_to(semantic->arena, semantic_mark);
            }
            *semantic = semantic_before;
            if (semantic_keys != NULL)
            {
                *semantic_keys = keys_before;
            }
        }
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

static bool check_initializers_run(CheckInitializers* result, const CheckInput* input, const CheckStream* stream,
                                   const Parser* parser, TypeTable* types, Arena* output, Arena* scratch)
{
    return check_initializers_run_events(result, input, stream, parser, types, output, scratch, NULL, NULL);
}

// Finalize this caller-owned, still unpublished batch. Query indexing uses the diagnostic arena, disjoint from
// both walker arenas; no token-sized warning state is retained. Prepare the query index before changing any
// facts, so allocation failure leaves this still-unpublished view untouched.
static bool check_initializers_warning_context(CheckWarningBuffer* events, const CheckInput* input, const Parser* parser,
                                                TypeTable* types, Arena* scratch)
{
    CheckInput staged = *input;
    CheckInputReader reader = {
        .output = &staged,
        .parser = parser,
        .types = types,
        .arena = events->arena,
        .scratch = scratch,
        .ends = parser->ends,
        .warning_query_arena = events->arena,
        .warning_bound_open = BUSTER_CHECK_NONE,
    };
    bool context = false;
    for (uint32_t i = 0; !context && i < events->count; i += 1)
    {
        const CheckWarningEvent* event = events->events + i;
        context = event->kind == CHECK_WARNING_EVENT_NON_LITERAL_NULL ||
                  (event->kind == CHECK_WARNING_EVENT_MESSAGE && event->id == CHECK_WARNING_ID_UNDEFINED_INTERNAL && event->payload.first_type != 0);
    }
    const bool ready = !context || check_input_warning_queries(&reader);
    for (uint32_t i = 0; ready && i < events->count; i += 1)
    {
        CheckWarningEvent* event = events->events + i;
        const bool selected = event->kind == CHECK_WARNING_EVENT_MESSAGE && event->id == CHECK_WARNING_ID_UNDEFINED_INTERNAL && event->payload.first_type != 0;
        if (event->kind == CHECK_WARNING_EVENT_NON_LITERAL_NULL || selected)
        {
            reader.warning_bound_open = BUSTER_CHECK_NONE;
            for (uint32_t vla = 0; vla < input->vla_count; vla += 1)
            {
                const CheckInputVla* bound = input->vlas + vla;
                if (bound->bound_begin <= event->token && event->token < bound->bound_end &&
                    (reader.warning_bound_open == BUSTER_CHECK_NONE || reader.warning_bound_open < bound->bound_begin))
                {
                    reader.warning_bound_open = bound->bound_begin;
                }
            }
            event->flags |= check_input_warning_unevaluated(&reader, event->token) ? BUSTER_CHECK_WARNING_SUPPRESSED : 0;
            event->payload.token = reader.warning_bound_open;
        }
    }
    return ready && !staged.resource;
}

// The TU-final use analysis sees source selections before initializer write pruning. Borrow the immutable
// reader prefix and append only selected-identity witnesses; rendering keeps the original views unchanged.
static bool check_initializers_warning_uses(CheckInput* result, const CheckInput* input, const CheckWarningEvents* initializers, Arena* arena)
{
    const uint64_t mark = arena->length;
    const CheckWarningEvents prefix = input->warning_events;
    CheckWarningBuffer uses = {
        .arena = arena,
        .events = (CheckWarningEvent*)prefix.events,
        .count = prefix.count,
        .capacity = prefix.count,
        .notes = (CheckWarningNote*)prefix.notes,
        .note_count = prefix.note_count,
        .note_capacity = prefix.note_count,
    };
    bool valid = true;
    for (uint32_t i = 0; valid && i < initializers->count; i += 1)
    {
        const CheckWarningEvent* event = initializers->events + i;
        if (event->kind == CHECK_WARNING_EVENT_MESSAGE && event->id == CHECK_WARNING_ID_UNDEFINED_INTERNAL && event->payload.first_type != 0)
        {
            valid = check_warning_append(&uses, event, NULL, 0);
        }
    }
    if (valid)
    {
        *result = *input;
        result->warning_events = check_warning_view(&uses);
    }
    else
    {
        arena_pop_to(arena, mark);
    }
    return valid;
}

#endif
