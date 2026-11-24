// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original declaration/statement fixtures run through both production parser/checker schedules and external Clang.
// Include after check_test_source; no Clang implementation or external fixture is incorporated.

// Keep the synthetic fixture's known regions out of the measured helper's optimizer. Otherwise it can prove that
// every transfer succeeds and delete even the old quadratic parent walk, defeating the work-bound regression.
static __attribute__((noinline)) bool check_test_source_region_transfer_call(const CheckInputReader* reader, uint32_t source, uint32_t target)
{
    return check_input_region_transfer(reader, source, target);
}

// The definition the reader's bounded walk keeps: a jump from `source` to `target` enters a statement expression when a
// region on the target's ancestor chain doesn't hold the source. Every ancestor up to the root is read; the reader
// stops at the source and target's common ancestor, whose regions hold both (#2924: its scalar arm was this walk).
static bool check_test_source_region_definition(const CheckInputReader* reader, uint32_t source, uint32_t target)
{
    const Parser* parser = reader->parser;
    bool valid = true;
    for (uint32_t ancestor = target; reader->output->region_count != 0 && ancestor < parser->tokens.count; ancestor = parser->tree.parents[ancestor])
    {
        const uint32_t selected = reader->output->expression_regions[ancestor];
        const CheckInputRegion* region = selected != 0 ? &reader->output->regions[selected - 1] : NULL;
        valid &= region == NULL || (source > region->opening && source < region->closing);
    }
    return valid;
}

static void check_test_source_region_transfers(CheckTest* test)
{
    const Parser no_region_parser = { .tokens = { .count = 1 } };
    CheckInput no_regions = { 0 };
    const CheckInputReader no_region_reader = { .parser = &no_region_parser, .output = &no_regions };
    check_test_expect(test, check_input_region_transfer(&no_region_reader, 0, 0), "an empty region set needs neither region nor postorder columns");
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source = "int f(int x){outer:;{({inner:;if(x)goto inner;({deep:;x++;});x;});}"
                         "({sibling:;x;});return x;}";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && arena.base != 0 && scratch.base != 0 && pp_run_source(&pp, "region-transfers.c", (const uint8_t*)source, strlen(source));
    for (uint32_t schedule = 0; ready && schedule < 4; schedule += 1)
    {
        arena_pop_to(&arena, 0);
        arena_pop_to(&scratch, 0);
        CheckTestTypes types = { 0 };
        const ParseOptions parse_options = { .scalar = (schedule & 1) != 0, .tree_only = (schedule & 2) != 0 };
        bool adapted = check_test_types_open(&types) && parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        check_test_expect(test, adapted && parser.sizes == NULL && ((parser.postorder == NULL) == parse_options.tree_only),
                          "region-transfer fixture retains no optional parser sizes, including tree-only schedules");
        CheckInput input = { 0 };
        adapted = adapted && check_input(&input, &parser, &pp, &types.table, &arena, &scratch);
        check_test_expect(test, adapted && input.report_count == 0 && input.region_count == 3,
                          "reader-owned traversal adapts nested and sibling statement expressions on every schedule");
        const uint64_t slots = (uint64_t)parser.tokens.count + 1;
        uint32_t* columns = adapted ? arena_push_array(&scratch, 9 * slots, sizeof(uint32_t), 4) : NULL;
        check_test_expect(test, columns != NULL, "region-transfer oracle traversal allocation");
        if (columns != NULL)
        {
            // Derive test-only metadata after check_input released its scratch. Production retains this same
            // traversal for all of the reader's consumers; it never needs Parser.sizes or another size pass.
            uint32_t* postorder = columns + 2 * slots;
            uint32_t* positions = columns + 3 * slots;
            uint32_t* sizes = columns + 8 * slots;
            parse_tree_columns(parser.tree.parents, parser.tokens.count, columns, columns + slots, postorder, positions,
                               columns + 4 * slots, columns + 5 * slots, columns + 6 * slots, columns + 7 * slots, sizes);
            for (uint32_t token = 0; token < parser.tokens.count; token += 1)
            {
                const uint32_t position = positions[token];
                check_test_expect(test, position < parser.tokens.count && postorder[position] == token && sizes[position] != 0 && sizes[position] <= position + 1,
                                  "every token, including control punctuation, has a bounded reader-owned subtree");
            }
            const CheckInputReader reader = { .parser = &parser, .output = &input, .postorder_positions = positions, .subtree_sizes = sizes };
            // Every token pair includes region boundaries, descendants, siblings, both jump directions and an absent token.
            // A source just before a subtree in the postorder (its previous sibling's root) is outside it, where the
            // bounded walk must not stop; `refused` counts the pairs the definition refuses.
            uint32_t refused = 0;
            for (uint32_t from = 0; from <= parser.tokens.count; from += 1)
            {
                for (uint32_t to = 0; to <= parser.tokens.count; to += 1)
                {
                    const uint32_t source_token = from == parser.tokens.count ? BUSTER_CHECK_NONE : from;
                    const uint32_t target_token = to == parser.tokens.count ? BUSTER_CHECK_NONE : to;
                    const bool expected = check_test_source_region_definition(&reader, source_token, target_token);
                    refused += !expected;
                    check_test_expect(test, check_input_region_transfer(&reader, source_token, target_token) == expected,
                                      "bounded region transfer equals the full ancestor walk without parser-owned size metadata");
                }
            }
            check_test_expect(test, refused != 0, "region-transfer fixture has jumps its regions refuse");
        }
        parse_release(&parser);
        check_test_types_close(&types);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    arena_release(&arena);

    // A chain's postorder is its token order. Every source is its target's immediate parent, and the one region holds
    // the whole chain. A full walk costs two billion dependent loads; stopping at the shared subtree costs two each.
    enum { COUNT = 65536 };
    uint32_t* parents = malloc((uint64_t)COUNT * sizeof(uint32_t));
    uint32_t* order = malloc((uint64_t)COUNT * sizeof(uint32_t));
    uint32_t* sizes = malloc((uint64_t)COUNT * sizeof(uint32_t));
    uint32_t* selected = calloc(COUNT, sizeof(uint32_t));
    ready = parents != 0 && order != 0 && sizes != 0 && selected != 0;
    check_test_expect(test, ready, "deep region-transfer fixture allocation");
    if (ready)
    {
        for (uint32_t token = 0; token < COUNT; token += 1)
        {
            parents[token] = token + 1;
            order[token] = token;
            sizes[token] = token + 1;
        }
        CheckInputRegion region = { .opening = 0, .closing = COUNT - 1 };
        Parser chain = { .tokens = { .count = COUNT }, .tree = { .parents = parents } };
        CheckInput output = { .expression_regions = selected, .regions = &region };
        const CheckInputReader reader = { .parser = &chain, .output = &output, .postorder_positions = order, .subtree_sizes = sizes };
        for (uint32_t variant = 0; variant < 2; variant += 1)
        {
            output.region_count = variant;
            selected[COUNT - 1] = variant;
            struct timespec start = { 0 };
            struct timespec end = { 0 };
            const bool started = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start) == 0;
            uint32_t accepted = 0;
            for (uint32_t target = 1; target + 2 < COUNT; target += 1)
            {
                accepted += check_test_source_region_transfer_call(&reader, target + 1, target);
            }
            const bool finished = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end) == 0;
            const int64_t elapsed = ((int64_t)end.tv_sec - start.tv_sec) * INT64_C(1000000000) + end.tv_nsec - start.tv_nsec;
            check_test_expect(test, accepted == COUNT - 3 && started && finished && elapsed < INT64_C(1000000000),
                              "65533 deep transfers with no region or one enclosing region finish within one CPU second");
        }
    }
    free(selected);
    free(sizes);
    free(order);
    free(parents);
}

static void check_test_source_fp_history(CheckTest* test, bool complex)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source = complex ?
        "#pragma STDC CX_LIMITED_RANGE ON\ndouble file_on;double f(void){\n"
        "#pragma STDC CX_LIMITED_RANGE OFF\ndouble local_off=0;{\n"
        "#pragma STDC CX_LIMITED_RANGE ON\ndouble inner_on=0;}double restored_off=0;return 0;}double restored_on;\n"
        "#pragma STDC CX_LIMITED_RANGE DEFAULT\ndouble file_default;\n" :
        "#pragma STDC FENV_ACCESS ON\ndouble file_on;double f(void){\n"
        "#pragma STDC FENV_ACCESS OFF\ndouble local_off=0;{\n"
        "#pragma STDC FENV_ACCESS ON\ndouble inner_on=0;}double restored_off=0;return 0;}double restored_on;\n"
        "#pragma STDC FENV_ACCESS DEFAULT\ndouble file_default;\n";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && pp_run_source(&pp, "fp-history.c", (const uint8_t*)source, strlen(source));
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    ready = ready && arena.base != 0 && scratch.base != 0;
    check_test_expect(test, ready, "floating environment history fixture setup");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        const ParseOptions parser_options = { .scalar = scalar != 0 };
        ready = parse_initialize(&parser, &parser_options, options.language) && parse_translation_unit(&parser, &pp);
        check_test_expect(test, ready && parser.tokens.pragma_count == 4, "floating environment history fixture parses");
        const uint64_t arena_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        uint64_t required = 0;
        if (ready)
        {
            arena.length = 0;
            memset(arena_push(&arena, arena.capacity, 1), 0xa5, arena.capacity);
            scratch.length = 0;
            memset(arena_push(&scratch, scratch.capacity, 1), 0x5a, scratch.capacity);
        }
        for (uint32_t pass = 0; ready && pass < 3; pass += 1)
        {
            const uint64_t attempts = pass == 1 ? required - 31 : 1;
            for (uint64_t limit = 0; limit < attempts; limit += 1)
            {
                arena.length = 31;
                arena.capacity = pass == 1 ? 31 + limit : arena_capacity;
                scratch.length = 29;
                scratch.capacity = pass == 2 ? 29 : scratch_capacity;
                uint8_t handled[4] = { 0 };
                CheckInput input = { .graph = { .capacity = parser.tokens.count * 2 } };
                CheckInputReader reader = { .output = &input, .parser = &parser, .pp = &pp, .arena = &arena, .scratch = &scratch };
                const bool valid = check_input_fp_pragmas(&reader, handled);
                if (pass == 0)
                {
                    required = arena.length;
                    check_test_expect(test, valid && !input.resource && input.fp_pragma_count == 4 &&
                                      input.fp_pragmas != 0 && input.fp_environments != 0 && input.cx_limited_ranges != 0 && input.graph.fp_environments != 0 &&
                                      handled[0] == 2 && handled[1] == 2 && handled[2] == 2 && handled[3] == 2 && scratch.length == 29,
                                      "floating environment history publishes initialized retained columns");
                    const char* names[] = { "file_on", "local_off", "inner_on", "restored_off", "restored_on", "file_default" };
                    const uint8_t expected[] = { 1, 0, 1, 0, 1, 0 };
                    uint32_t found = 0;
                    for (uint32_t token = 0; valid && token < parser.tokens.count; token += 1)
                    {
                        const uint32_t name = parser.tokens.names[token];
                        for (uint32_t item = 0; name != 0 && item < BUSTER_ARRAY_LENGTH(names); item += 1)
                        {
                            if (intern_length(&identifiers, name) == strlen(names[item]) && memcmp(intern_bytes(&identifiers, name), names[item], strlen(names[item])) == 0)
                            {
                                found += 1;
                                check_test_expect(test, input.fp_environments[token] == (BUSTER_CHECK_FP_CONTRACT_ON | (complex ? 0 : expected[item])) &&
                                                  input.cx_limited_ranges[token] == (complex ? expected[item] : 0),
                                                  "floating policy restores its enclosing scope without changing the other policy");
                            }
                        }
                    }
                    check_test_expect(test, found == BUSTER_ARRAY_LENGTH(names), "floating environment metadata fixture visits every marker");
                    for (uint32_t pragma = 0; valid && pragma < input.fp_pragma_count; pragma += 1)
                    {
                        check_test_expect(test, input.fp_pragmas[pragma].kind == (complex ? CHECK_FP_PRAGMA_CX_LIMITED_RANGE : CHECK_FP_PRAGMA_FENV_ACCESS) &&
                                          input.fp_pragmas[pragma].warnings == 0 && input.fp_pragmas[pragma].valid,
                                          "floating policy records retain the exact directive kind");
                    }
                    ready = valid && required > 31;
                }
                else
                {
                    check_test_expect(test, !valid && input.resource && input.fp_pragmas == 0 && input.fp_environments == 0 &&
                                      input.cx_limited_ranges == 0 && input.graph.fp_environments == 0 && input.fp_pragma_count == 0 && arena.length == 31 && scratch.length == 29 &&
                                      handled[0] == 0 && handled[1] == 0 && handled[2] == 0 && handled[3] == 0,
                                      "floating environment exhaustion rolls back columns and handled state");
                }
            }
        }
        arena.capacity = arena_capacity;
        scratch.capacity = scratch_capacity;
        parse_release(&parser);
    }
    arena_release(&scratch);
    arena_release(&arena);
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_source_builtin_effects(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source =
        "int side(void);int f(int x){return __builtin_constant_p(1?1.0/3.0:side())+"
        "__builtin_constant_p(0&&side())+__builtin_constant_p(sizeof(side()))+"
        "__builtin_constant_p(_Generic(side(),int:1.0/3.0))+"
        "__builtin_constant_p(__builtin_choose_expr(1,1.0/3.0,side()))+"
        "__builtin_constant_p(__builtin_constant_p(side()))+__builtin_constant_p(x++ + 1.0/3.0);}";
    const bool expected[] = { true, true, false, false, false, true, false, true };
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && pp_run_source(&pp, "constant-p-syntax.c", (const uint8_t*)source, strlen(source));
    check_test_expect(test, ready, "builtin syntax-effect fixture preprocessing");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        CheckTestTypes types = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parse_options = { .scalar = scalar != 0 };
        CheckInput input = { 0 };
        const bool parsed = check_test_types_open(&types) && arena.base != 0 && scratch.base != 0 &&
                            parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        const bool adapted = parsed && check_input(&input, &parser, &pp, &types.table, &arena, &scratch);
        check_test_expect(test, adapted && !input.resource, "builtin syntax-effect source adaptation");
        uint32_t found = 0;
        uint32_t first = BUSTER_CHECK_NONE;
        for (uint32_t call = 0; adapted && call < input.graph.builtin_count; call += 1)
        {
            const CheckBuiltinCall* evidence = input.graph.builtin_calls + call;
            const uint32_t node = evidence->node;
            if (input.graph.ops[node] == CHECK_OP_BUILTIN_QUERY && input.graph.members[node] == CBUILTIN_CONSTANT_P)
            {
                check_test_expect(test, found < BUSTER_ARRAY_LENGTH(expected) && evidence->constant_p_side_effects == expected[found],
                                  "constant_p syntax keeps conditional calls but ignores truly unevaluated operands");
                first = first == BUSTER_CHECK_NONE ? input.graph.argument_nodes[input.graph.argument_offsets[node]] : first;
                found += 1;
            }
        }
        check_test_expect(test, found == BUSTER_ARRAY_LENGTH(expected), "builtin syntax-effect fixture visits all nested calls");
        if (adapted && first != BUSTER_CHECK_NONE)
        {
            CheckInput limited = { .graph = input.graph };
            CheckInputReader reader = { .output = &limited, .scratch = &scratch };
            const uint64_t capacity = scratch.capacity;
            scratch.capacity = scratch.length;
            const bool effects = check_input_builtin_syntax_effects(&reader, first);
            check_test_expect(test, !effects && limited.resource && reader.builtin_syntax_count == 0 && reader.builtin_syntax_effects == 0,
                              "builtin syntax-effect exhaustion cannot fabricate a pure-argument witness");
            scratch.capacity = capacity;
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_source_transparent_history(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source = "union U{int i;unsigned u;};void take(union U);void before(void){take(1);}"
                         "typedef union U T __attribute__((transparent_union));void after(void){take(1);}";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && pp_run_source(&pp, "transparent-history.c", (const uint8_t*)source, strlen(source));
    check_test_expect(test, ready, "transparent union history fixture preprocessing");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        CheckTestTypes types = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parse_options = { .scalar = scalar != 0 };
        CheckInput input = { 0 };
        const bool parsed = check_test_types_open(&types) && arena.base != 0 && output.base != 0 && scratch.base != 0 &&
                            parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        const bool adapted = parsed && check_input(&input, &parser, &pp, &types.table, &arena, &scratch);
        const uint32_t name = intern_find(&identifiers, (const uint8_t*)"T", 1);
        uint32_t tag = BUSTER_CHECK_NONE;
        for (uint32_t token = 0; adapted && token < parser.tokens.count; token += 1)
        {
            if (parser.tokens.names[token] == name && input.declaration_kinds[token] == C_BINDING_TYPEDEF)
            {
                tag = types.table.types.index[type_index(input.declared_types[token])];
            }
        }
        check_test_expect(test, adapted && !input.resource && input.graph.transparent_unions != 0 && tag < input.graph.tag_completion_count,
                          "transparent union history retains the exact typedef's nominal tag");
        uint32_t calls[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        uint32_t found = 0;
        for (uint32_t node = 0; adapted && node < input.graph.count; node += 1)
        {
            if (input.graph.ops[node] == CHECK_OP_CALL)
            {
                if (found < BUSTER_ARRAY_LENGTH(calls))
                {
                    calls[found] = node;
                }
                found += 1;
            }
        }
        if (adapted && found == BUSTER_ARRAY_LENGTH(calls) && input.graph.transparent_unions != 0 && tag < input.graph.tag_completion_count)
        {
            const uint32_t activation = input.graph.transparent_unions[tag];
            check_test_expect(test, input.graph.tag_completions[tag] < activation && input.graph.source_positions[calls[0]] < activation &&
                              activation <= input.graph.source_positions[calls[1]],
                              "transparent attribute activation is distinct from the earlier record completion");
            assert(arena_commit(&scratch, scratch.capacity));
            memset(scratch.base + scratch.length, 0xa5, scratch.capacity - scratch.length);
            CheckStream stream = { 0 };
            const CheckOptions check_options = { 0 };
            const bool checked = check_graph_run(&types.table, &input.graph, &output, &scratch, &check_options, &stream);
            check_test_expect(test, checked && stream.report_count == 1 && stream.reports[0].node == calls[0] &&
                              input.graph.transparent_unions[tag] == activation,
                              "recycled scratch cannot retroactively allow the call before the transparent attribute");
        }
        else
        {
            check_test_expect(test, false, "transparent union fixture retains both original call nodes");
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_source_intrinsic_range(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source = "typedef int V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_pshufd512(x,256);}";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && pp_run_source(&pp, "intrinsic-range.c", (const uint8_t*)source, strlen(source));
    check_test_expect(test, ready, "intrinsic range provenance fixture preprocesses");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        CheckTestTypes types = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parse_options = { .scalar = scalar != 0 };
        const CheckOptions check_options = { 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        const bool parsed = check_test_types_open(&types) && arena.base != 0 && output.base != 0 && scratch.base != 0 &&
                            parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        const bool adapted = parsed && check_input(&input, &parser, &pp, &types.table, &arena, &scratch);
        const bool checked = adapted && check_graph_run(&types.table, &input.graph, &output, &scratch, &check_options, &stream);
        check_test_expect(test, checked && !input.resource && stream.report_count == 0 && stream.x86_count == 1 && input.graph.x86_count == 1,
                          "suppressible intrinsic range diagnostics remain retained after successful typing");
        if (checked && stream.x86_count == 1 && input.graph.x86_count == 1)
        {
            const CheckBuiltinX86Effect* effect = stream.x86;
            const CheckBuiltinX86RangeEvent* event = effect->range_events;
            check_test_expect(test, effect->range_event_count == 1 && event->node == effect->node && event->argument == 1 &&
                              event->low == 0 && event->high == 255 && event->value.constant_kind == CHECK_CONSTANT_INTEGER &&
                              event->value.integer == 256 && event->default_error && event->disposition == CHECK_BUILTIN_X86_DEFERRED &&
                              (effect->checked_ice_arguments & 2) != 0 && (input.graph.x86_calls[0].ice_arguments & 2) != 0,
                              "intrinsic range event preserves source ICE, exact bounds and default-error policy for the diagnostic consumer");
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_source_kr_header_reuse(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    static const char* const sources[] =
    {
        "int f(x)struct S{void member;}x;{return 0;}",
        "int f(A,x)int A;enum E{A=3}x;{return x+A;}",
        "struct S{long outer;};enum{A=19};int f(x,p)struct S{int inner;}x;_Atomic(int*)p;"
        "{struct S s={7};return x.inner+(int)s.outer+A+*p;}",
    };
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers);
    Arena output = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    ready = ready && output.base != NULL && scratch.base != NULL;
    if (ready)
    {
        memset(arena_push(&output, 31, 1), 0xa5, 31);
        memset(arena_push(&scratch, 29, 1), 0x5a, 29);
    }
    check_test_expect(test, ready, "K&R header reuse fixture initializes retained arenas");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        CheckTestTypes types = { 0 };
        ready = check_test_types_open(&types);
        CheckInput input = { 0 };
        for (uint32_t pass = 0; ready && pass < 2 * BUSTER_ARRAY_LENGTH(sources); pass += 1)
        {
            const uint32_t source = pass % BUSTER_ARRAY_LENGTH(sources);
            const ParseOptions parse_options = { .scalar = scalar != 0 };
            arena_pop_to(&output, 31);
            arena_pop_to(&scratch, 29);
            const bool prepared = pp_initialize(&pp, &options, &identifiers) &&
                                  pp_run_source(&pp, "kr-header-reuse.c", (const uint8_t*)sources[source], strlen(sources[source])) &&
                                  parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
            const bool valid = prepared && check_input(&input, &parser, &pp, &types.table, &output, &scratch);
            check_test_expect(test, prepared && valid == (source == 2) && !input.resource &&
                              (input.report_count == 0) == valid && scratch.length == 29 &&
                              ((const uint8_t*)output.base)[0] == 0xa5 && ((const uint8_t*)scratch.base)[0] == 0x5a,
                              "invalid tag/member and enumerator headers leave reusable input, types and arena prefixes intact");
            parse_release(&parser);
            pp_release(&pp);
        }
        check_test_types_close(&types);
    }
    arena_release(&scratch);
    arena_release(&output);
    intern_release(&identifiers);
}

static __attribute__((noinline)) void check_test_source_extensions(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/source-extensions, which check_test_suite reads and
    // runs, each in its own dialect.
    check_test_suite(test, clang, "checker/source-extensions", "extended source clang oracle could not run");
    check_test_source_fp_history(test, false);
    check_test_source_fp_history(test, true);
    check_test_source_builtin_effects(test);
    check_test_source_transparent_history(test);
    check_test_source_intrinsic_range(test);
    check_test_source_region_transfers(test);
    check_test_source_kr_header_reuse(test);
}
