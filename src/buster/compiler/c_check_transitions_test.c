// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The memoized transitions' checks (#1050, c_check_transitions.c). Each source runs through the scalar reader and path,
// the oracle, and through the vector reader with its transitions in node order and in windows, each verifying every hit
// against its action, and without them: every stream must be the oracle's, with no mismatch. A fixture states the hits,
// misses, folds and entries node order must count. Then the windows' keys and hashes against check_transition_key's and
// check_transition_hash's, node by node, and the table's limit: scratch arenas from too small for the table to room enough,
// where the run falls back to the regions, stops inserting, or runs whole, its stream the oracle's each time.

typedef struct CheckTransitionFixture CheckTransitionFixture;
struct CheckTransitionFixture
{
    const char* name;
    const char* source;
    // The counts node order must give for the reader's graph of the source: hits, misses, folds and the table's entries, which
    // a miss whose entry's guards failed writes over rather than adds.
    uint32_t hits;
    uint32_t misses;
    uint32_t folds;
    uint32_t entries;
    // Whether the oracle reports a diagnostic (a fixture of a tag's completion reads an incomplete one first).
    bool reported;
};

// Every eligible node's key and hash by the windows' batches against check_transition_key's and check_transition_hash's,
// on a stream that is complete: an empty table, so that no lane hits.
static void check_test_transition_keys(CheckTest* test, const char* name, const CheckGraph* graph, CheckStream* stream, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint32_t* shapes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->count + 1);
    CheckTransitionWindow* window = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckTransitionWindow, 1);
    uint32_t* words = arena_push_array(scratch, (uint64_t)BUSTER_CHECK_TRANSITION_HORIZON * BUSTER_CHECK_TRANSITION_ARGUMENTS * BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS,
                                       sizeof(uint32_t), 64);
    uint32_t* twin_words = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_CHECK_TRANSITION_ARGUMENTS * BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS);
    CheckTransitions table;
    bool valid = shapes != NULL && window != NULL && words != NULL && twin_words != NULL && check_transitions_initialize(&table, scratch, graph->count);
    uint32_t keyed = 0;
    uint32_t folds = 0;
    uint32_t vector_member = 0;
    for (uint32_t node = 0; valid && node < graph->count; node += 1)
    {
        shapes[node] = check_transition_shape(stream, node);
    }
    for (uint32_t first = 0; valid && first < graph->count; first += BUSTER_CHECK_TRANSITION_HORIZON)
    {
        const uint32_t span = graph->count - first < BUSTER_CHECK_TRANSITION_HORIZON ? graph->count - first : BUSTER_CHECK_TRANSITION_HORIZON;
        *window = (CheckTransitionWindow){ .argument_words = words, .first = first };
        uint64_t members = 0;
        uint64_t eligible = 0;
        while (vector_member < graph->vector_member_count && graph->vector_members[vector_member].node < first + span)
        {
            members |= UINT64_C(1) << (graph->vector_members[vector_member].node - first);
            vector_member += 1;
        }
        for (uint32_t position = 0; position < span; position += 1)
        {
            const uint32_t node = first + position;
            eligible |= (uint64_t)(!check_graph_is_leaf(graph, node) && check_transition_node(graph, stream, node, (members >> position & 1) != 0)) << position;
        }
        for (uint64_t remaining = eligible; remaining != 0;)
        {
            uint32_t positions[BUSTER_CHECK_TRANSITION_LANES];
            uint32_t lanes = 0;
            for (; remaining != 0 && lanes < BUSTER_CHECK_TRANSITION_LANES; remaining &= remaining - 1)
            {
                positions[lanes++] = (uint32_t)__builtin_ctzll(remaining);
            }
            const uint64_t hits = check_transition_batch(NULL, graph, stream, &table, window, positions, lanes, shapes, NULL, scratch, false);
            valid &= hits == 0;
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                const uint32_t position = positions[lane];
                const uint32_t node = first + position;
                _Alignas(64) uint32_t key[BUSTER_CHECK_TRANSITION_WORDS];
                uint32_t objects[3];
                const CheckTransitionClass class = check_transition_key(graph, stream, NULL, node, true, key, objects, twin_words);
                const CheckTransitionMiss* miss = window->misses + position;
                const bool fold = (window->folds >> position & 1) != 0;
                bool same = (class == CHECK_TRANSITION_FOLD) == fold && (class == CHECK_TRANSITION_KEYED) == miss->keyed;
                if (same && miss->keyed)
                {
                    same = memcmp(key, miss->key, sizeof(key)) == 0 && check_transition_hash(key) == miss->hash && memcmp(objects, miss->objects, sizeof(objects)) == 0;
                    const uint32_t count = key[BUSTER_CHECK_KEY_HEADER] >> 18;
                    same &= memcmp(twin_words, check_transition_words(window, position), (uint64_t)count * BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS * sizeof(uint32_t)) == 0;
                }
                keyed += miss->keyed;
                folds += fold;
                valid &= same;
            }
        }
    }
    char description[256];
    snprintf(description, sizeof(description), "%s: the windows' keys, hashes and folds are check_transition_key's (%u keyed, %u folds)", name, keyed, folds);
    check_test_expect(test, valid, description);
    arena_pop_to(scratch, mark);
}

// A source through the scalar oracle and every vector variant (CheckOptions' transitions in node order and in windows,
// verified, and none), each stream the oracle's; node order's counts as the fixture states, the windows' keys checked.
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
        { .verify = true, .verify_transitions = true },
        { .verify = true, .verify_transitions = true, .transition_windows = true },
        { .verify = true, .no_transitions = true },
        { .transition_windows = true, .batch_size = 3 },
    };
    const char* const names[4] = { "node order", "windows", "regions", "windows of 3-node batches" };
    for (uint32_t v = 0; ready && v < 4; v += 1)
    {
        const uint64_t mark = output[1].length;
        CheckStream stream;
        const bool checked = check_graph_run(&types[1].table, &input[1].graph, &output[1], &scratch[1], variants + v, &stream);
        const CheckStatistics* statistics = &stream.statistics;
        snprintf(description, sizeof(description), "%s, %s: the stream is the oracle's (%u hits, %u misses, %u folds, %u mismatches)", fixture->name, names[v],
                 statistics->transition_hits, statistics->transition_misses, statistics->transition_folds, statistics->transition_mismatches);
        check_test_expect(test, checked && check_test_stream_equal(&oracle, &stream, input[1].graph.argument_count) && statistics->transition_mismatches == 0,
                          description);
        // A scalar host's run takes the scalar path whatever its options (check_graph_run), with no transitions to count.
        if (checked && v == 0 && BUSTER_HOST_VECTOR)
        {
            snprintf(description, sizeof(description), "%s: node order counts %u hits, %u misses, %u folds and %u entries (%u, %u, %u and %u expected)",
                     fixture->name, statistics->transition_hits, statistics->transition_misses, statistics->transition_folds, statistics->transition_entries,
                     fixture->hits, fixture->misses, fixture->folds, fixture->entries);
            check_test_expect(test, statistics->transition_hits == fixture->hits && statistics->transition_misses == fixture->misses &&
                                   statistics->transition_folds == fixture->folds && statistics->transition_entries == fixture->entries &&
                                   statistics->transition_inserts <= statistics->transition_misses && statistics->transition_entries <= statistics->transition_inserts,
                              description);
            check_test_transition_keys(test, fixture->name, &input[1].graph, &stream, &scratch[1]);
        }
        if (checked && v == 2)
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
// a static object's: `count` distinct member keys, which pass the table's first 1,024 entries, and their repeats.
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
    const CheckOptions full = { .verify_transitions = true };
    CheckStream whole;
    const uint64_t output_mark = output.length;
    ready = ready && check_graph_run(&types.table, &input.graph, &output, &scratch, &full, &whole);
    const uint32_t entries = ready ? whole.statistics.transition_entries : 0;
    check_test_expect(test, ready && (entries > BUSTER_CHECK_TRANSITION_INITIAL || !BUSTER_HOST_VECTOR) &&
                            check_test_stream_equal(&oracle, &whole, input.graph.argument_count),
                      "the table's limit: with room, the table passes its first entries");
    arena_pop_to(&output, output_mark);
    bool fell_back = false;
    bool stopped = false;
    bool grew = false;
    for (uint64_t room = UINT64_C(1) << 16; ready && room <= UINT64_C(1) << 26; room += room / 8)
    {
        // A view of the scratch arena with `room` bytes: too few for the run's own arrays at first (the run fails, which the
        // regions would too), then too few for the table's too, then for its growth.
        Arena view = scratch;
        view.capacity = scratch.length + room;
        CheckStream stream;
        const bool checked = check_graph_run(&types.table, &input.graph, &output, &view, &full, &stream);
        scratch.committed = view.committed > scratch.committed ? view.committed : scratch.committed;
        scratch.peak = view.peak > scratch.peak ? view.peak : scratch.peak;
        if (checked)
        {
            const uint32_t taken = stream.statistics.transition_entries;
            fell_back |= taken == 0 && stream.statistics.transition_hits == 0;
            stopped |= taken != 0 && taken < entries;
            grew |= taken == entries;
            check_test_expect(test, check_test_stream_equal(&oracle, &stream, input.graph.argument_count) && stream.statistics.transition_mismatches == 0,
                              "the table's limit: a run within the room's stream is the oracle's");
        }
        arena_pop_to(&output, output_mark);
    }
    char description[256];
    snprintf(description, sizeof(description), "the table's limit: the sweep falls back (%d), stops inserting (%d) and grows whole (%d)", fell_back, stopped, grew);
    // A scalar host's runs take the scalar path (check_graph_run), whose streams the sweep still compares.
    check_test_expect(test, (fell_back && stopped && grew) || !BUSTER_HOST_VECTOR, description);
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
    static const CheckTransitionFixture fixtures[] =
    {
        {
            .name = "repeats",
            .source = "int f(int a, int b) { int x = a + b; int y = a + b; return x + y + a + b; }\n"
                      "int g(int a, int b) { return a * b + a * b; }\n",
            .hits = 5,
            .misses = 9,
            .folds = 0,
            .entries = 9,
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
            .hits = 2,
            .misses = 4,
            .folds = 12,
            .entries = 4,
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
            // Calls: repeated, with constant arguments (folds) and zero ones (keyed), and of several prototypes.
            .name = "calls",
            .source = "int k(int, long); double d(double, ...);\n"
                      "int m(int a) { return k(a, a) + k(a, a) + k(1, 2) + k(0, 0) + k(0, 0) + (int)d(a, a, a) + (int)d(a, a, a); }\n",
            .hits = 9,
            .misses = 6,
            .folds = 1,
            .entries = 6,
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
            // Constants to fold, pure, in batches of check_actions, and floating, complex and conditional ones.
            .name = "folds",
            .source = "int f(int a) { return 1 + 2 + 3 + a * 4 + (a ? 5 : 6) + (int)(1.5 * a) + (int)__real__ (2.0i + a); }\n"
                      "unsigned g(unsigned a) { return (a << 3) + (a >> 1) + ~a + -a + !a; }\n",
            .hits = 5,
            .misses = 10,
            .folds = 9,
            .entries = 10,
        },
        {
            // Statements past several 64-node windows, each a chain through its expression's operators.
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
            .hits = 112,
            .misses = 11,
            .folds = 6,
            .entries = 11,
        },
        {
            // Errors: an operand that isn't one, whose answer's diagnostic the hits repeat.
            .name = "diagnostics",
            .source = "struct A { int x; };\n"
                      "int f(struct A a, struct A b) { return a + 1; }\n"
                      "int g(struct A a) { return a + 1; }\n",
            .hits = 1,
            .misses = 1,
            .folds = 2,
            .entries = 1,
            .reported = true,
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        check_test_transition_source(test, fixtures + i);
    }
    check_test_transition_limit(test);
}
