// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The memoized transitions' checks (#1050, c_check_transitions.c). Each source runs through the scalar reader and path,
// the oracle, and through the vector reader with its transitions in node order, in batches of 32, 3 and 1, verifying every
// hit against its action, and without them: every stream must be the oracle's, with no mismatch. A fixture states the hits,
// misses, folds and entries node order must count. Then each node's class against its value, and the table's limit:
// scratch arenas from too small for the table to room enough, where the run falls back to the regions, stops inserting, or
// runs whole, its stream the oracle's each time.

typedef struct CheckTransitionFixture CheckTransitionFixture;
struct CheckTransitionFixture
{
    const char* name;
    const char* source;
    // The counts node order must give for the reader's graph of the source: hits, misses, folds (nodes with an operand of no
    // class) and the table's entries, which a miss whose entry's guards failed, or whose twin waited in the same batch, writes
    // over rather than adds.
    uint32_t hits;
    uint32_t misses;
    uint32_t folds;
    uint32_t entries;
    // Of the hits, those found after the batch their operands waited in ran (CheckStatistics.transition_flushed_hits).
    uint32_t flushed;
    // Whether the oracle reports a diagnostic (a fixture of a tag's completion reads an incomplete one first).
    bool reported;
};

// Every node's class against its value: two nodes of a complete stream share a class exactly when their values but their
// objects are the same (check_transition_template_equal), a value with a fault or an overload set has none, and the classes
// pass a table's first slots, so that they rehash.
static void check_test_transition_classes(CheckTest* test, const char* name, const CheckGraph* graph, const CheckStream* stream, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint32_t* classes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->count + 1);
    CheckValue* values = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckValue, graph->count + 1);
    CheckTransitions table;
    bool valid = classes != NULL && values != NULL && check_transitions_initialize(&table, scratch, 0);
    uint32_t keyed = 0;
    for (uint32_t node = 0; valid && node < graph->count; node += 1)
    {
        uint32_t template = 0;
        classes[node] = check_transitions_intern(&table, stream, node, &template);
        values[node] = check_stream_value(stream, node);
        const bool unkeyed = values[node].fault != CHECK_FAULT_NONE || values[node].fault_node != 0 || values[node].overload_head != 0;
        const bool payload = stream->payload_rows[node] != 0;
        valid &= (classes[node] == BUSTER_CHECK_CLASS_UNKEYED) == unkeyed && classes[node] != 0 && (template != 0) == (payload && !unkeyed);
        valid &= template == 0 || check_transition_template_equal(table.templates + template - 1, values + node);
        keyed += !unkeyed;
    }
    for (uint32_t a = 0; valid && a < graph->count; a += 1)
    {
        for (uint32_t b = a + 1; valid && b < graph->count; b += 1)
        {
            const bool both = classes[a] != BUSTER_CHECK_CLASS_UNKEYED && classes[b] != BUSTER_CHECK_CLASS_UNKEYED;
            valid &= !both || (classes[a] == classes[b]) == check_transition_template_equal(values + a, values + b);
        }
    }
    char description[256];
    snprintf(description, sizeof(description), "%s: classes are the values but their objects (%u keyed values, %u classes, %u class slots)", name, keyed,
             valid ? table.class_count : 0, valid ? table.class_mask + 1 : 0);
    check_test_expect(test, valid, description);
    arena_pop_to(scratch, mark);
}

// The static words by the vector path, 16 nodes at a time, against the scalar path's, node by node: for the graph as read,
// and with system-header marks of every third token, in arrays from the tokens' whole count down to fewer than a gather's
// 4 bytes, so that the gather's lanes past the array's end go one at a time.
static void check_test_transition_statics(CheckTest* test, const char* name, const CheckGraph* graph, const CheckStream* stream, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint32_t* vector = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->count + 1);
    uint32_t* scalar = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->count + 1);
    uint32_t tokens = 0;
    for (uint32_t node = 0; graph->tokens != NULL && node < graph->count; node += 1)
    {
        tokens = graph->tokens[node] != BUSTER_CHECK_NONE && graph->tokens[node] + 1 > tokens ? graph->tokens[node] + 1 : tokens;
    }
    uint8_t* marks = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)tokens + 1);
    bool valid = vector != NULL && scalar != NULL && marks != NULL;
    for (uint32_t token = 0; valid && token < tokens; token += 1)
    {
        marks[token] = token % 3 == 1;
    }
    const uint32_t counts[] = { 0, tokens, tokens > 0 ? tokens - 1 : 0, tokens > 2 ? tokens - 2 : 0, 3, 1 };
    uint32_t keyed = 0;
    uint32_t system = 0;
    for (uint32_t variant = 0; valid && variant < BUSTER_ARRAY_LENGTH(counts); variant += 1)
    {
        CheckGraph marked = *graph;
        if (variant != 0)
        {
            marked.system_tokens = marks;
            marked.system_token_count = counts[variant] < tokens ? counts[variant] : tokens;
        }
        check_transition_statics(&marked, stream, vector, false);
        check_transition_statics(&marked, stream, scalar, true);
        valid = memcmp(vector, scalar, (uint64_t)graph->count * sizeof(uint32_t)) == 0;
        for (uint32_t node = 0; valid && node < graph->count; node += 1)
        {
            keyed += variant == 0 && scalar[node] > BUSTER_CHECK_TRANSITION_LEAF;
            system += scalar[node] > BUSTER_CHECK_TRANSITION_LEAF &&
                      (scalar[node] >> BUSTER_CHECK_KEY_EXTENSIONS_SHIFT & BUSTER_CHECK_EXTENSION_SYSTEM_HEADER) != 0;
        }
    }
    char description[256];
    snprintf(description, sizeof(description), "%s: the static words 16 nodes at a time are the scalar path's (%u nodes, %u keyed, %u system-header words)", name,
             graph->count, keyed, system);
    check_test_expect(test, valid, description);
    arena_pop_to(scratch, mark);
}

// The environment, operand-object pattern and call count must remain independent in a key. These first three pairs
// collided in the previous header layout; FAST also made a 64-argument call read a 65th argument. Extra sentinel words
// make that failure observable without an out-of-bounds access. The graph spans two full vector blocks and a tail.
static void check_test_transition_policy_keys(CheckTest* test)
{
    enum { COUNT = 35, NODE = 3, WORDS = 128 };
    const struct
    {
        CheckOp op;
        uint8_t environment;
        uint32_t objects[3];
        uint32_t arguments;
    } cases[] =
    {
        { CHECK_OP_SELECT, BUSTER_CHECK_FP_EVAL_DOUBLE, { 0, 0, 0 }, 0 },
        { CHECK_OP_SELECT, 0, { 17, 0, 0 }, 0 },
        { CHECK_OP_SELECT, BUSTER_CHECK_FP_CONTRACT_ON, { 0, 18, 0 }, 0 },
        { CHECK_OP_SELECT, 0, { 0, 18, 18 }, 0 },
        { CHECK_OP_CALL, BUSTER_CHECK_FP_CONTRACT_FAST, { 0, 0, 0 }, 0 },
        { CHECK_OP_CALL, 0, { 0, 0, 0 }, 1 },
        { CHECK_OP_CALL, BUSTER_CHECK_FP_CONTRACT_FAST, { 0, 0, 0 }, BUSTER_CHECK_TRANSITION_ARGUMENTS },
        { CHECK_OP_CALL, BUSTER_CHECK_FP_ENV_ACCESS | BUSTER_CHECK_FP_EVAL_EXTENDED | BUSTER_CHECK_FP_CONTRACT_ON,
          { 0, 0, 0 }, BUSTER_CHECK_TRANSITION_ARGUMENTS },
        { CHECK_OP_SELECT, 0, { 0, 0, 0 }, 0 },
        { CHECK_OP_SELECT, BUSTER_CHECK_FP_CONTRACT_ON, { 0, 0, 0 }, 0 },
        { CHECK_OP_SELECT, BUSTER_CHECK_FP_CONTRACT_FAST, { 0, 0, 0 }, 0 },
    };
    CheckOp ops[COUNT];
    TypeId types[COUNT];
    uint32_t members[COUNT] = { 0 };
    uint32_t operand0[COUNT];
    uint32_t operand1[COUNT];
    uint32_t operand2[COUNT];
    uint32_t argument_counts[COUNT] = { 0 };
    uint32_t argument_offsets[COUNT] = { 0 };
    uint32_t argument_nodes[WORDS] = { 0 };
    uint32_t objects[COUNT] = { 0 };
    uint32_t classes[COUNT];
    uint8_t environments[COUNT];
    uint32_t scalar[COUNT];
    uint32_t vector[COUNT];
    uint32_t inherited[COUNT];
    uint64_t keys[BUSTER_ARRAY_LENGTH(cases)][4];
    CheckGraph graph =
    {
        .count = COUNT,
        .ops = ops,
        .types = types,
        .members = members,
        .operand0 = operand0,
        .operand1 = operand1,
        .operand2 = operand2,
        .argument_counts = argument_counts,
        .argument_offsets = argument_offsets,
        .argument_nodes = argument_nodes,
    };
    const CheckStream stream = { .value_objects = objects };
    for (uint32_t node = 0; node < COUNT; node += 1)
    {
        ops[node] = node < 3 ? CHECK_OP_VALUE : CHECK_OP_SELECT;
        types[node] = BUSTER_TYPE_INT;
        operand0[node] = node < 3 ? BUSTER_CHECK_NONE : 0;
        operand1[node] = node < 3 ? BUSTER_CHECK_NONE : 1;
        operand2[node] = node < 3 ? BUSTER_CHECK_NONE : 2;
        classes[node] = node + 1;
    }
    for (uint32_t variant = 0; variant < BUSTER_ARRAY_LENGTH(cases); variant += 1)
    {
        graph.fp_environments = environments;
        graph.fp_contract_default = BUSTER_CHECK_FP_CONTRACT_FAST;
        graph.argument_count = cases[variant].arguments;
        ops[NODE] = cases[variant].op;
        operand1[NODE] = ops[NODE] == CHECK_OP_CALL ? BUSTER_CHECK_NONE : 1;
        operand2[NODE] = ops[NODE] == CHECK_OP_CALL ? BUSTER_CHECK_NONE : 2;
        argument_counts[NODE] = cases[variant].arguments;
        memcpy(objects, cases[variant].objects, sizeof(cases[variant].objects));
        memset(environments, cases[variant].environment, sizeof(environments));
        check_transition_statics(&graph, &stream, scalar, true);
        check_transition_statics(&graph, &stream, vector, false);
        check_test_expect(test, memcmp(scalar, vector, sizeof(scalar)) == 0,
                          "scalar and vector transition statics preserve contraction, evaluation and call-count fields");
        const uint32_t sentinel = UINT32_C(0xa5a5a5a5);
        uint32_t words[WORDS];
        for (uint32_t i = 0; i < WORDS; i += 1)
        {
            words[i] = sentinel;
        }
        CheckTransitionMiss miss = { 0 };
        const CheckTransitionKind kind = check_transition_key(&graph, &stream, classes, NODE, scalar[NODE], &miss, words);
        bool bounded = true;
        for (uint32_t i = 0; i < WORDS; i += 1)
        {
            bounded &= words[i] == (i < cases[variant].arguments ? classes[0] : sentinel);
        }
        check_test_expect(test, kind == CHECK_TRANSITION_KEYED && bounded,
                          "floating policy never adds or removes an argument while building a transition key");
        memcpy(keys[variant], miss.key, sizeof(miss.key));
        for (uint32_t previous = 0; previous < variant; previous += 1)
        {
            check_test_expect(test, memcmp(keys[previous], miss.key, sizeof(miss.key)) != 0,
                              "distinct floating policies, operand objects and argument counts cannot alias transition keys");
        }
        if ((cases[variant].environment & ~BUSTER_CHECK_FP_CONTRACT_MASK) == 0)
        {
            graph.fp_environments = NULL;
            graph.fp_contract_default = cases[variant].environment;
            for (uint32_t path = 0; path < 2; path += 1)
            {
                check_transition_statics(&graph, &stream, inherited, path != 0);
                check_test_expect(test, memcmp(scalar, inherited, sizeof(scalar)) == 0,
                                  "a graph's contraction default keys exactly as the same explicit lexical policy");
            }
        }
    }
}

// A source through the scalar oracle and every vector variant (CheckOptions' transitions in node order by batches of 32, 3
// and 1, and none), each stream the oracle's; node order's counts as the fixture states, its classes checked.
static void check_test_transition_source(CheckTest* test, const CheckTransitionFixture* fixture)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    const PPOptions options =
    {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = true,
    };
    CheckTestTypes types[2] = { 0 };
    CheckInput input[2] = { 0 };
    CheckStream oracle = { 0 };
    Arena storage[2] = { 0 };
    Arena output[2] = { 0 };
    Arena scratch[2] = { 0 };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                 pp_run_source(&pp, "transitions.c", (const uint8_t*)fixture->source, strlen(fixture->source));
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const ParseOptions parsing = { .scalar = path == 0 };
        storage[path] = arena_reserve(UINT64_C(1) << 28);
        output[path] = arena_reserve(UINT64_C(1) << 28);
        scratch[path] = arena_reserve(UINT64_C(1) << 28);
        ready = ready && storage[path].base != NULL && output[path].base != NULL && scratch[path].base != NULL &&
                check_test_types_open(&types[path]) && parse_initialize(&parsers[path], &parsing, options.language) &&
                parse_translation_unit(&parsers[path], &pp) &&
                check_input(&input[path], &parsers[path], &pp, &types[path].table, &storage[path], &scratch[path]);
    }
    const CheckOptions scalar = { .scalar = true, .verify = true };
    ready = ready && check_graph_run(&types[0].table, &input[0].graph, &output[0], &scratch[0], &scalar, &oracle);
    char description[320];
    snprintf(description, sizeof(description), "%s: the scalar oracle checks the source (%u reports, %s expected)", fixture->name, oracle.report_count,
             fixture->reported ? "some" : "none");
    check_test_expect(test, ready && (oracle.report_count != 0) == fixture->reported, description);
    // Node order first, on a fresh table, so that its misses intern the types the oracle's did; then the others.
    const CheckOptions variants[4] =
    {
        { .verify = true, .transitions = true, .verify_transitions = true },
        { .verify = true },
        { .transitions = true, .verify_transitions = true, .batch_size = 3 },
        { .transitions = true, .batch_size = 1 },
    };
    const char* const names[4] = { "node order", "regions", "node order in 3-node batches", "node order a node at a time" };
    for (uint32_t v = 0; ready && v < 4; v += 1)
    {
        const uint64_t mark = output[1].length;
        CheckStream stream;
        const bool checked = check_graph_run(&types[1].table, &input[1].graph, &output[1], &scratch[1], variants + v, &stream);
        const CheckStatistics* statistics = &stream.statistics;
        // A verified run checks every hit by its action, a flushed hit's too.
        const bool verified = !variants[v].verify_transitions || statistics->transition_verified == statistics->transition_hits;
        snprintf(description, sizeof(description), "%s, %s: the stream is the oracle's (%u hits, %u verified, %u misses, %u folds, %u mismatches)", fixture->name,
                 names[v], statistics->transition_hits, statistics->transition_verified, statistics->transition_misses, statistics->transition_folds,
                 statistics->transition_mismatches);
        check_test_expect(test, checked && check_test_stream_equal(&oracle, &stream, input[1].graph.argument_count) && statistics->transition_mismatches == 0 &&
                                verified,
                          description);
        // A scalar host's run takes the scalar path whatever its options (check_graph_run), with no transitions to count.
        if (checked && v == 0 && BUSTER_HOST_VECTOR)
        {
            snprintf(description, sizeof(description), "%s: node order counts %u hits (%u flushed), %u misses, %u folds and %u entries (%u (%u), %u, %u and %u expected)",
                     fixture->name, statistics->transition_hits, statistics->transition_flushed_hits, statistics->transition_misses, statistics->transition_folds,
                     statistics->transition_entries, fixture->hits, fixture->flushed, fixture->misses, fixture->folds, fixture->entries);
            check_test_expect(test, statistics->transition_hits == fixture->hits && statistics->transition_flushed_hits == fixture->flushed &&
                                   statistics->transition_misses == fixture->misses &&
                                   statistics->transition_folds == fixture->folds && statistics->transition_entries == fixture->entries &&
                                   statistics->transition_inserts <= statistics->transition_misses && statistics->transition_entries <= statistics->transition_inserts,
                              description);
            check_test_transition_classes(test, fixture->name, &input[1].graph, &stream, &scratch[1]);
            check_test_transition_statics(test, fixture->name, &input[1].graph, &stream, &scratch[1]);
        }
        if (checked && v == 1)
        {
            check_test_expect(test, statistics->transition_hits == 0 && statistics->transition_entries == 0, "the regions take no transitions");
        }
        arena_pop_to(&output[1], mark);
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        arena_release(&scratch[path]);
        arena_release(&output[path]);
        arena_release(&storage[path]);
        check_test_types_close(&types[path]);
        parse_release(&parsers[path]);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

// A generated source of `count` structs, each read through a pointer twice and its first member's address compared with
// a static object's: `count` distinct member keys, more than the table's first entries and its fallback's 1,024, and
// their repeats.
static bool check_test_transition_wide_source(Arena* text, uint32_t count)
{
    bool fits = true;
    for (uint32_t i = 0; fits && i < count; i += 1)
    {
        char line[256];
        const int written = snprintf(line, sizeof(line), "struct T%u { int a; long b; }; static struct T%u s%u; int f%u(struct T%u *p) { return p->a + (int)p->b + p->a + (&s%u.b == &p->b); }\n",
                                     i, i, i, i, i, i);
        char* destination = written > 0 ? BUSTER_ARENA_PUSH_ARRAY(text, char, (uint64_t)written) : NULL;
        fits = destination != NULL;
        if (fits)
        {
            memcpy(destination, line, (uint64_t)written);
        }
    }
    char* end = fits ? BUSTER_ARENA_PUSH_ARRAY(text, char, 1) : NULL;
    if (end != NULL)
    {
        *end = 0;
    }
    return end != NULL;
}

// One run of the limit's sweep, by the transitions in a view of `scratch` with `room` bytes: whether it checks, its stream the
// oracle's, and what its table did in `seen` (1: the regions ran, 2: the table stopped inserting, 4: it took all `entries`).
static bool check_test_transition_room(CheckTest* test, TypeTable* table, const CheckGraph* graph, Arena* output, Arena* scratch, const CheckStream* oracle,
                                       uint64_t room, uint32_t entries, uint32_t* seen)
{
    const CheckOptions full = { .transitions = true, .verify_transitions = true };
    const uint64_t output_mark = output->length;
    Arena view = *scratch;
    view.capacity = scratch->length + room;
    CheckStream stream;
    const bool checked = check_graph_run(table, graph, output, &view, &full, &stream);
    scratch->committed = view.committed > scratch->committed ? view.committed : scratch->committed;
    scratch->peak = view.peak > scratch->peak ? view.peak : scratch->peak;
    if (checked)
    {
        const uint32_t taken = stream.statistics.transition_entries;
        *seen |= (taken == 0 && stream.statistics.transition_hits == 0 ? 1u : 0u) | (taken != 0 && taken < entries ? 2u : 0u) | (taken == entries ? 4u : 0u);
        check_test_expect(test, check_test_stream_equal(oracle, &stream, graph->argument_count) && stream.statistics.transition_mismatches == 0 &&
                                stream.statistics.transition_verified == stream.statistics.transition_hits,
                          "the table's limit: a run within the room's stream is the oracle's, every hit verified");
    }
    arena_pop_to(output, output_mark);
    return checked;
}

// The table's limit: the wide source's graph run with scratch arenas whose room goes from too little for the run, through
// too little for the table (the regions run) and room for its first entries (insertion stops), to room for all of them.
// Each run that checks gives the oracle's stream; the sweep must reach the last three.
static void check_test_transition_limit(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parsing = { 0 };
    Arena text = arena_reserve(UINT64_C(1) << 26);
    Arena storage = arena_reserve(UINT64_C(1) << 28);
    Arena output = arena_reserve(UINT64_C(1) << 30);
    Arena scratch = arena_reserve(UINT64_C(1) << 28);
    CheckTestTypes types = { 0 };
    CheckInput input = { 0 };
    CheckStream oracle = { 0 };
    const uint32_t structs = 1500;
    const TypeCapacity capacity = {
        .types = 1u << 16,
        .parameters = 1u << 16,
        .tags = 4096,
        .members = 1u << 14,
    };
    bool ready = text.base != NULL && storage.base != NULL && output.base != NULL && scratch.base != NULL && check_test_transition_wide_source(&text, structs) &&
                 intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                 pp_run_source(&pp, "wide.c", text.base, text.length - 1) && check_test_types_open_with(&types, capacity) &&
                 parse_initialize(&parser, &parsing, options.language) &&
                 parse_translation_unit(&parser, &pp) && check_input(&input, &parser, &pp, &types.table, &storage, &scratch);
    const CheckOptions scalar = { .scalar = true };
    ready = ready && check_graph_run(&types.table, &input.graph, &output, &scratch, &scalar, &oracle) && oracle.report_count == 0;
    check_test_expect(test, ready, "the table's limit: the wide source checks");
    const CheckOptions full = { .transitions = true, .verify_transitions = true };
    CheckStream whole;
    const uint64_t output_mark = output.length;
    ready = ready && check_graph_run(&types.table, &input.graph, &output, &scratch, &full, &whole);
    const uint32_t entries = ready ? whole.statistics.transition_entries : 0;
    check_test_expect(test, ready && (entries > BUSTER_CHECK_TRANSITION_FALLBACK || !BUSTER_HOST_VECTOR) &&
                            check_test_stream_equal(&oracle, &whole, input.graph.argument_count),
                      "the table's limit: with room, the table passes its first entries");
    arena_pop_to(&output, output_mark);
    // Views of the scratch arena with room enough for the run's own arrays and the table, then for its growth.
    uint32_t seen = 0;
    for (uint64_t room = UINT64_C(1) << 16; ready && room <= UINT64_C(1) << 26; room += room / 8)
    {
        check_test_transition_room(test, &types.table, &input.graph, &output, &scratch, &oracle, room, entries, &seen);
    }
    // The least room the run checks in, by bisection (it fails in any less, whichever path it takes). There the run's own
    // arrays leave too little for a table of even 16 entries within its quarter, so the regions run; the rooms past it,
    // 512 bytes apart, reach one where the table takes entries.
    uint64_t low = UINT64_C(1) << 12;
    uint64_t high = UINT64_C(1) << 26;
    uint32_t ignored = 0;
    bool bounded = ready && !check_test_transition_room(test, &types.table, &input.graph, &output, &scratch, &oracle, low, entries, &ignored) &&
                   check_test_transition_room(test, &types.table, &input.graph, &output, &scratch, &oracle, high, entries, &ignored);
    while (bounded && high - low > 64)
    {
        const uint64_t middle = low + (high - low) / 2;
        const bool checked = check_test_transition_room(test, &types.table, &input.graph, &output, &scratch, &oracle, middle, entries, &ignored);
        high = checked ? middle : high;
        low = checked ? low : middle;
    }
    uint32_t least = 0;
    uint32_t past = 0;
    bounded = bounded && check_test_transition_room(test, &types.table, &input.graph, &output, &scratch, &oracle, high, entries, &least);
    for (uint64_t room = high + 512; bounded && room < high + (UINT64_C(1) << 15); room += 512)
    {
        check_test_transition_room(test, &types.table, &input.graph, &output, &scratch, &oracle, room, entries, &past);
    }
    const bool fell_back = (least & 1) != 0;
    const bool reached = (past & 6) != 0;
    const bool stopped = (seen & 2) != 0;
    const bool grew = (seen & 4) != 0;
    char description[256];
    snprintf(description, sizeof(description), "the table's limit: at the least room the regions run (%d) and past it a table (%d); the sweep stops inserting (%d) and grows whole (%d)",
             fell_back, reached, stopped, grew);
    // A scalar host's runs take the scalar path (check_graph_run), whose streams the sweep still compares.
    check_test_expect(test, (fell_back && reached && stopped && grew) || !BUSTER_HOST_VECTOR, description);
    check_test_types_close(&types);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    arena_release(&text);
}

static void check_test_transitions(CheckTest* test)
{
    check_test_transition_policy_keys(test);
    static const CheckTransitionFixture fixtures[] =
    {
        {
            .name = "repeats",
            .source = "int f(int a, int b) { int x = a + b; int y = a + b; return x + y + a + b; }\n"
                      "int g(int a, int b) { return a * b + a * b; }\n",
            .hits = 5,
            .flushed = 2,
            .misses = 9,
            .folds = 0,
            .entries = 7,
        },
        {
            // Pointers' comparisons: the same operand twice and two operands, whose objects' patterns differ.
            .name = "objects",
            .source = "int f(int *p, int *q) { return (p == q) + (p == p) + (q == p) + (q == q) + (p != q); }\n",
            .hits = 5,
            .misses = 5,
            .folds = 0,
            .entries = 5,
        },
        {
            // A static object's members: address constants with offsets, payloads whose templates take each node's object.
            .name = "static members",
            .source = "struct T { int a; long b; char c[4]; };\n"
                      "static struct T s, t;\n"
                      "long f(void) { return s.b + t.b + s.a + t.a + s.c[0] + t.c[0]; }\n"
                      "int g(void) { return &s.b == &t.b; }\n"
                      "int h(void) { return &s.b == &s.b; }\n",
            .hits = 7,
            .misses = 11,
            .folds = 0,
            .entries = 11,
        },
        {
            // A tag read before and after its completion: the guard sends the second read to its action, and the third hits.
            .name = "guards",
            .source = "struct S;\n"
                      "extern struct S *p;\n"
                      "int f(void) { return p->x; }\n"
                      "struct S { int x; };\n"
                      "int g(void) { return p->x; }\n"
                      "int h(void) { return p->x; }\n",
            .hits = 2,
            .misses = 4,
            .folds = 0,
            .entries = 3,
            .reported = true,
        },
        {
            // Calls: repeated, with constant arguments (each value its own class) and zero ones, and of several prototypes.
            .name = "calls",
            .source = "int k(int, long); double d(double, ...);\n"
                      "int m(int a) { return k(a, a) + k(a, a) + k(1, 2) + k(0, 0) + k(0, 0) + (int)d(a, a, a) + (int)d(a, a, a); }\n",
            .hits = 9,
            .misses = 7,
            .folds = 0,
            .entries = 7,
        },
        {
            // A transparent union's activation, which no guard holds: its calls' answers aren't memoized.
            .name = "transparent unions",
            .source = "typedef union { int *ip; long *lp; } U __attribute__((transparent_union));\n"
                      "void take(U u);\n"
                      "void use(int *p, long *q) { take(p); take(p); take(q); take(q); }\n",
            .hits = 3,
            .misses = 5,
            .folds = 0,
            .entries = 1,
        },
        {
            // Constants to fold, pure, in batches of check_actions, and floating, complex and conditional ones: each constant
            // its own class, so that most of their keys are new.
            .name = "folds",
            .source = "int f(int a) { return 1 + 2 + 3 + a * 4 + (a ? 5 : 6) + (int)(1.5 * a) + (int)__real__ (2.0i + a); }\n"
                      "unsigned g(unsigned a) { return (a << 3) + (a >> 1) + ~a + -a + !a; }\n",
            .hits = 5,
            .flushed = 3,
            .misses = 19,
            .folds = 0,
            .entries = 19,
        },
        {
            // Repeated statements, each a chain through its expression's operators.
            .name = "chains",
            .source = "struct L { struct L *next; int v; };\n"
                      "int f(struct L *l) {\n"
                      "  int s = 0;\n"
                      "  s += l->next->next->v; s += l->next->next->v; s += l->next->v * 2; s += l->v - l->next->v;\n"
                      "  s += l->next->next->v; s += l->next->next->v; s += l->next->v * 2; s += l->v - l->next->v;\n"
                      "  s += l->next->next->v; s += l->next->next->v; s += l->next->v * 2; s += l->v - l->next->v;\n"
                      "  s += l->next->next->v; s += l->next->next->v; s += l->next->v * 2; s += l->v - l->next->v;\n"
                      "  s += l->next->next->v; s += l->next->next->v; s += l->next->v * 2; s += l->v - l->next->v;\n"
                      "  s += l->next->next->v; s += l->next->next->v; s += l->next->v * 2; s += l->v - l->next->v;\n"
                      "  return s;\n"
                      "}\n",
            .hits = 117,
            .misses = 12,
            .folds = 0,
            .entries = 12,
        },
        {
            // Errors: an operand that isn't one, whose answer's diagnostic the hits repeat.
            .name = "diagnostics",
            .source = "struct A { int x; };\n"
                      "int f(struct A a, struct A b) { return a + 1; }\n"
                      "int g(struct A a) { return a + 1; }\n",
            .hits = 2,
            .misses = 2,
            .folds = 0,
            .entries = 2,
            .reported = true,
        },
        {
            // A hit found after a batch ran: `1 + 2` waits in the batch, and `(1 + 2) + 3`, which reads it, runs the batch
            // first and then finds h's `3 + 3`, a flushed hit, which a verified run checks by its action too.
            .name = "flushed hits",
            .source = "int h(void) { return 3 + 3; }\n"
                      "int g(void) { return 1 + 2 + 3; }\n",
            .hits = 2,
            .flushed = 1,
            .misses = 3,
            .folds = 0,
            .entries = 3,
        },
        {
            // Faults: a constant division by zero, whose value has no class, so that the nodes reading it are folds.
            .name = "faults",
            .source = "int f(int a) { return (1 / 0) + a + (1 / 0) + a; }\n"
                      "int g(int a) { return (1 / 0) + a; }\n",
            .hits = 0,
            .misses = 3,
            .folds = 6,
            .entries = 0,
        },
        {
            // Vector members (CheckGraph.vector_members): swizzles the table doesn't take, around the sums it does.
            .name = "vector members",
            .source = "typedef float float4 __attribute__((ext_vector_type(4)));\n"
                      "float f(float4 v) { return v.x + v.y + v.x * v.w; }\n"
                      "float g(float4 v, float4 u) { float4 s = v.xyzw + u.wzyx; return s.x + s.y + s.z + s.w + v.x + u.y; }\n"
                      "float h(float4 v, float4 u) { float4 s = v.xyzw + u.wzyx; return s.x + s.y + s.z + s.w + v.x + u.y; }\n",
            .hits = 13,
            .misses = 7,
            .folds = 0,
            .entries = 7,
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        check_test_transition_source(test, fixtures + i);
    }
    check_test_transition_limit(test);
}
