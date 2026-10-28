// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Parser-to-resolver source adapter (#128). Include after c_parser.c. parse_bindings_build records the grammar's
// exact declaration/scope transitions and immediate stack answers. parse_bindings_resolve independently replays
// or batch-resolves the events and materializes a declaration-token column. A checker can consume that column
// without name lookup. Member names and attribute/asm symbols are deliberately not ordinary identifiers.
//
// Output arrays have exactly count logical entries, no padding contract, and belong to the caller's arena. Event
// handles are index+1; zero means a token is outside these namespaces. UINT32_MAX denotes an unresolved source
// declaration (also the source-less builtin typedefs); kinds disambiguates those. The parser's tokens/tree are
// borrowed only during build. Failure rolls the arena back and clears the output. Neither operation prints.
//
// Event extraction currently replays the scalar grammar after the successful syntax parse. Benchmarks charge
// that whole production/handoff cost separately and end to end; resolver-only results cannot hide it. This is
// an opt-in adapter, not an unconditional extra pass in syntax-only parsing.

typedef struct ParseBindings ParseBindings;
struct ParseBindings
{
    CBindings events;
    uint32_t* token_events;
    uint32_t* declarations;
    uint8_t* namespaces;
    uint8_t* kinds;
    uint32_t count;
    CBindingStatistics statistics;
    bool valid;
};

// Collect ordinary function labels before visiting their uses. Local label declarations have block scope; this
// small independent scan tracks only them, so a label covered by __label__ is not also predeclared function-wide.
// Every block is pushed/popped once and each local name has a dense head, including in deeply nested input.
static bool parse_bindings_prepare_labels(const Parser* parser, ParseBindingTrace* trace, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = parser->tokens.count;
    const uint64_t slots = (uint64_t)count + 1;
    uint32_t* heads = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, parser->meanings.capacity);
    uint32_t* names = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* previous = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* marks = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* functions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    const bool valid = heads != 0 && names != 0 && previous != 0 && marks != 0 && ends != 0 && functions != 0;
    if (valid)
    {
        memset(heads, 0, (uint64_t)parser->meanings.capacity * sizeof(uint32_t));
        uint32_t active = 0;
        uint32_t depth = 0;
        uint32_t function = BUSTER_PARSE_NONE;
        bool local = false;
        for (uint32_t t = 0; t < count; t += 1)
        {
            const uint8_t role = parser->tree.roles[t];
            const uint8_t kind = parser->tokens.kinds[t];
            if (role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY)
            {
                marks[depth] = active;
                ends[depth] = parser->tree.parents[t];
                functions[depth] = function;
                depth += 1;
                function = role == PARSE_ROLE_OPEN_BODY ? t : function;
            }
            if (kind == PARSE_KIND_LABEL)
            {
                local = true;
            }
            else if (kind == PARSE_KIND_SEMI)
            {
                local = false;
            }
            else if (local && kind == PARSE_KIND_IDENTIFIER)
            {
                const uint32_t name = parser->tokens.names[t];
                names[active] = name;
                previous[active] = heads[name];
                active += 1;
                heads[name] = active;
            }
            else if (kind == PARSE_KIND_IDENTIFIER && t + 1 < count && parser->tree.roles[t + 1] == PARSE_ROLE_LABEL &&
                     function != BUSTER_PARSE_NONE && heads[parser->tokens.names[t]] == 0)
            {
                trace->label_next[t] = trace->label_first[function];
                trace->label_first[function] = t;
            }
            if (depth != 0 && t == ends[depth - 1])
            {
                depth -= 1;
                while (active > marks[depth])
                {
                    active -= 1;
                    heads[names[active]] = previous[active];
                }
                function = functions[depth];
            }
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}

static bool parse_bindings_build(ParseBindings* output, Parser* parser, Arena* arena)
{
    const uint64_t mark = arena->length;
    const uint64_t scratch_mark = parser->scratch.length;
    const uint32_t count = parser->tokens.count;
    const uint64_t slots = (uint64_t)count + 2;
    const uint64_t capacity = 4 * slots + 2 * BUSTER_PARSE_PREDECLARED_TYPEDEFS;
    *output = (ParseBindings){ .count = count };
    bool valid = arena != &parser->scratch && parser->fatal == PARSE_MESSAGE_NONE && parser->errors == 0 && parser->meanings.meanings != 0 &&
                 parser->tree.roles != 0 && parser->tree.parents != 0 && parser->tree.depths != 0 && capacity < UINT32_MAX;
    valid = valid && c_bindings_initialize(&output->events, arena, parser->meanings.capacity, (uint32_t)capacity);
    output->token_events = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0;
    output->declarations = output->token_events != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0;
    output->namespaces = output->declarations != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots) : 0;
    output->kinds = output->namespaces != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots) : 0;
    valid = output->kinds != 0;
    ParseBindingTrace trace = {
        .bindings = &output->events,
        .source_tree = &parser->tree,
        .token_events = output->token_events,
        .declarations = output->declarations,
        .namespaces = output->namespaces,
        .kinds = output->kinds,
    };
    ParseTree tree = { 0 };
    if (valid)
    {
        memset(output->token_events, 0, slots * sizeof(uint32_t));
        memset(output->declarations, 0xff, slots * sizeof(uint32_t));
        memset(output->namespaces, 0xff, slots);
        memset(output->kinds, 0xff, slots);
        trace.kept_events = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint32_t, slots);
        trace.frame_scopes = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint32_t, slots);
        trace.statement_scopes = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint32_t, slots);
        trace.matches = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint32_t, slots);
        trace.label_first = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint32_t, slots);
        trace.label_next = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint32_t, slots);
        tree.roles = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint8_t, slots);
        tree.depths = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint32_t, slots);
        tree.parents = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint32_t, slots);
        valid = trace.kept_events != 0 && trace.frame_scopes != 0 && trace.statement_scopes != 0 && trace.matches != 0 &&
                trace.label_first != 0 && trace.label_next != 0 && tree.roles != 0 && tree.depths != 0 && tree.parents != 0;
    }
    if (valid)
    {
        memset(trace.label_first, 0xff, slots * sizeof(uint32_t));
        memset(trace.label_next, 0xff, slots * sizeof(uint32_t));
        for (uint32_t t = 0; t < count; t += 1)
        {
            trace.matches[t] = t;
        }
        for (uint32_t t = 0; t < count; t += 1)
        {
            const uint32_t parent = parser->tree.parents[t];
            if (parse_closer_roles[parser->tree.roles[t]] != PARSE_ROLE_NONE && parent < count)
            {
                trace.matches[parent] = t;
            }
        }
        valid = parse_bindings_prepare_labels(parser, &trace, &parser->scratch);
    }
    if (valid)
    {
        // The scalar and fallback parsers restore their initial meaning heads. Use an independent copy so this
        // adapter never alters a Parser supplied by another consumer, including on allocation failure.
        uint8_t* meanings = BUSTER_ARENA_PUSH_ARRAY(&parser->scratch, uint8_t, parser->meanings.capacity);
        valid = meanings != 0;
        if (valid)
        {
            memset(meanings, PARSE_MEANING_ORDINARY, parser->meanings.capacity);
            for (uint32_t p = 0; p < BUSTER_PARSE_PREDECLARED_TYPEDEFS; p += 1)
            {
                const IdentId name = parser->predeclared[p];
                c_bindings_declare(&output->events, C_BINDING_ORDINARY, name, C_BINDING_TYPEDEF, BUSTER_PARSE_NONE, 0, 0);
                meanings[name] = PARSE_MEANING_TYPEDEF;
            }
            trace.builtin_count = output->events.active_count;
            ParseMeanings initial = {
                .meanings = meanings,
                .capacity = parser->meanings.capacity,
                .table = parser->meanings.table,
            };
            uint32_t diagnostic_count = 0;
            const ParseScalarResult result = parse_scalar_traced(parser->tokens.kinds, parser->tokens.names, parser->tokens.flags, count, parser->language,
                                                                 &initial, &tree, &parser->scratch, 0, 0, &diagnostic_count, &trace);
            valid = !result.out_of_memory && result.errors == 0 && output->events.valid;
            if (valid)
            {
                c_bindings_restore(&output->events, 0, count);
                valid = output->events.valid;
            }
        }
    }
    if (valid)
    {
        for (uint32_t t = 0; t < count; t += 1)
        {
            if (output->token_events[t] == 0)
            {
                output->namespaces[t] = UINT8_MAX;
                output->kinds[t] = UINT8_MAX;
            }
        }
        output->valid = true;
    }
    else
    {
        arena_pop_to(arena, mark);
        *output = (ParseBindings){ 0 };
    }
    arena_pop_to(&parser->scratch, scratch_mark);
    return valid;
}

static bool parse_bindings_resolve(ParseBindings* output, bool scalar, Arena* scratch)
{
    (void)scalar;

    const uint64_t mark = scratch->length;
    uint32_t* results = output->valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)output->events.count + 1) : 0;
    bool valid = results != 0;
    CBindingStatistics statistics = { 0 };
    if (valid)
    {
        valid = BUSTER_SCALAR_PATH(scalar) ? c_bindings_scalar(&output->events, results, scratch) :
                         c_bindings_batch(&output->events, results, scratch, &statistics);
    }
    if (valid)
    {
        for (uint32_t token = 0; token < output->count; token += 1)
        {
            const uint32_t event = output->token_events[token];
            const uint32_t resolved = event != 0 ? results[event - 1] : 0;
            output->declarations[token] = resolved != 0 ? output->events.declaration[resolved - 1] : BUSTER_PARSE_NONE;
            if (event != 0)
            {
                output->kinds[token] = resolved != 0 ? output->events.kind[resolved - 1] :
                                       output->namespaces[token] == C_BINDING_LABEL ? C_BINDING_LABEL_KIND :
                                       output->namespaces[token] == C_BINDING_TAG ? C_BINDING_TAG_KIND : C_BINDING_OBJECT;
            }
        }
        if (BUSTER_SCALAR_PATH(scalar))
        {
            for (uint32_t event = 0; event < output->events.count; event += 1)
            {
                statistics.identifiers += output->events.operation[event] != C_BINDING_RESTORE;
                statistics.declarations += output->events.operation[event] == C_BINDING_DECLARE;
            }
        }
        output->statistics = statistics;
    }
    arena_pop_to(scratch, mark);
    return valid;
}
