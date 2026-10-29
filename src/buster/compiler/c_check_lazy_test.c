// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original lazy-body fixtures. Syntax, bindings, system provenance and source semantics all come from the real
// frontend; the independently stated source-order mask is the selection oracle. Include after CheckTest helpers.

#ifndef BUSTER_CHECK_LAZY_TEST_INCLUDED
#define BUSTER_CHECK_LAZY_TEST_INCLUDED 1

typedef struct CheckLazyFixture CheckLazyFixture;
struct CheckLazyFixture
{
    const char* name;
    const char* source;
    uint64_t deferred;
    uint32_t bodies;
    bool ordinary_valid;
    bool lazy_valid;
};

static void check_test_lazy_failures(CheckTest* test, const Parser* parser, const Preprocessor* pp, const CheckLazy* plan, Arena* scratch)
{
    const uint64_t capacities[] = { 0, 1, 15, 63, plan->retained_bytes, plan->retained_bytes + 64, 4096, 16384 };
    const uint64_t scratch_mark = scratch->length;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(capacities); i += 1)
    {
        Arena output = arena_reserve(capacities[i] + 8);
        uint8_t* guard = BUSTER_ARENA_PUSH_ARRAY(&output, uint8_t, 8);
        if (guard != 0)
        {
            memset(guard, 0xa7, 8);
        }
        const uint64_t mark = output.length;
        CheckLazy failed = { 0 };
        const bool valid = guard != 0 && check_lazy_plan(&failed, parser, pp, &output, scratch, false);
        check_test_expect(test, valid || (failed.status != CHECK_LAZY_OK && failed.bodies == 0 && failed.deferred_tokens == 0 && output.length == mark),
                          "lazy planner failure discards partial publication and restores its output mark");
        check_test_expect(test, scratch->length == scratch_mark && (guard == 0 || guard[0] == 0xa7) && (guard == 0 || guard[7] == 0xa7),
                          "lazy planner capacity failure preserves borrowed input and earlier allocations");
        arena_release(&output);
    }
    Arena output = arena_reserve(1u << 24);
    Arena exhausted = { 0 };
    CheckLazy failed = { 0 };
    check_test_expect(test, !check_lazy_plan(&failed, parser, pp, &output, &exhausted, false) && failed.status == CHECK_LAZY_RESOURCE &&
                           output.length == 0 && exhausted.length == 0, "lazy planner releases allocations when private grammar/graph scratch is exhausted");
    check_test_expect(test, !check_lazy_plan(&failed, parser, pp, scratch, scratch, false) && failed.status == CHECK_LAZY_INVALID &&
                           scratch->length == scratch_mark, "lazy planner rejects aliased retained/transient arenas");
    uint8_t* mask = BUSTER_ARENA_PUSH_ARRAY(&output, uint8_t, (uint64_t)parser->tokens.count + 1);
    if (mask != 0)
    {
        memcpy(mask, plan->deferred_tokens, (uint64_t)parser->tokens.count + 1);
        mask[parser->tokens.count] = 1;
        check_test_expect(test, !check_lazy_mask_valid(parser, mask), "lazy mask rejects a live sentinel");
        mask[parser->tokens.count] = 0;
        if (plan->body_count != 0)
        {
            const CheckLazyBody body = plan->bodies[0];
            memset(mask, 0, (uint64_t)parser->tokens.count + 1);
            memset(mask + body.begin, 1, body.end - body.begin);
            check_test_expect(test, check_lazy_mask_valid(parser, mask), "lazy mask accepts a complete original body interval");
            mask[body.end - 1] = 0;
            check_test_expect(test, !check_lazy_mask_valid(parser, mask), "lazy mask rejects a missing closing brace");
            mask[body.begin] = 0;
            check_test_expect(test, !check_lazy_mask_valid(parser, mask), "lazy mask rejects an interior-only interval");
        }
    }
    arena_release(&output);
}

static void check_test_lazy_fixture(CheckTest* test, const CheckLazyFixture* fixture, bool failures)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parse_options = { .extension_metadata = true };
    Arena output[2] = { arena_reserve(1u << 24), arena_reserve(1u << 24) };
    Arena scratch = arena_reserve(1u << 24);
    CheckLazy plans[2] = { 0 };
    bool ready = output[0].base != 0 && output[1].base != 0 && scratch.base != 0 && intern_initialize(&identifiers) &&
                 pp_initialize(&pp, &options, &identifiers) && parse_initialize(&parser, &parse_options, options.language) &&
                 pp_run_source(&pp, "lazy-original.c", (const uint8_t*)fixture->source, strlen(fixture->source)) && parse_translation_unit(&parser, &pp);
    char description[256];
    snprintf(description, sizeof(description), "%s: lazy source syntax setup", fixture->name);
    check_test_expect(test, ready, description);
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        parser.options.scalar = path == 0;
        const bool planned = check_lazy_plan(&plans[path], &parser, &pp, &output[path], &scratch, path == 0);
        snprintf(description, sizeof(description), "%s: %s demand plan", fixture->name, path == 0 ? "scalar" : "vector");
        check_test_expect(test, planned && plans[path].status == CHECK_LAZY_OK && plans[path].body_count == fixture->bodies && scratch.length == 0,
                          description);
        uint64_t deferred = 0;
        for (uint32_t body = 0; planned && body < plans[path].body_count && body < 64; body += 1)
        {
            deferred |= !plans[path].bodies[body].selected ? UINT64_C(1) << body : 0;
            check_test_expect(test, plans[path].bodies[body].declaration < parser.tokens.count &&
                                   parser.tokens.kinds[plans[path].bodies[body].declaration] == PARSE_KIND_IDENTIFIER,
                              "lazy plan retains exact declaration-token identities");
        }
        snprintf(description, sizeof(description), "%s: independent original-source demand mask", fixture->name);
        check_test_expect(test, planned && deferred == fixture->deferred && plans[path].deferred_count == (uint32_t)__builtin_popcountll(fixture->deferred),
                          description);
        if (planned)
        {
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                CheckTestTypes types = { 0 };
                Arena retained = arena_reserve(1u << 24);
                Arena checked = arena_reserve(1u << 24);
                CheckInput input = { 0 };
                CheckStream stream = { 0 };
                CheckInitializers initializers = { 0 };
                CheckAsms asms = { 0 };
                CheckProgram program = { 0 };
                const bool initialized = retained.base != 0 && checked.base != 0 && check_test_types_open(&types);
                const bool adapted = initialized && (mode == 0 ? check_input(&input, &parser, &pp, &types.table, &retained, &scratch) :
                                       check_input_selected(&input, &parser, &pp, &types.table, &retained, &scratch, plans[path].deferred_tokens));
                const CheckOptions checking = { .scalar = path == 0 };
                const bool graphed = adapted && check_graph_run(&types.table, &input.graph, &checked, &scratch, &checking, &stream);
                const bool prepared = graphed && stream.report_count == 0 &&
                                      check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &checked, &scratch) &&
                                      check_asms_run(&asms, &input, &stream, &types.table, &checked, &scratch);
                const CheckProgramOptions program_options = { .initializers = &initializers, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0 };
                const bool accepted = prepared && check_program(&program, &parser, &input, &stream, &types.table, program_options, &checked, &scratch);
                const bool expected = mode == 0 ? fixture->ordinary_valid : fixture->lazy_valid;
                snprintf(description, sizeof(description), "%s: %s checking has the required acceptance", fixture->name, mode == 0 ? "default all-body" : "selected-body");
                check_test_expect(test, initialized && accepted == expected && !input.resource && (!adapted || graphed), description);
                if (adapted && mode != 0)
                {
                    check_test_expect(test, input.deferred_tokens != plans[path].deferred_tokens &&
                                           memcmp(input.deferred_tokens, plans[path].deferred_tokens, (uint64_t)parser.tokens.count + 1) == 0,
                                      "selected source reader owns an immutable demand mask copy");
                    for (uint32_t node = 0; node < input.graph.count; node += 1)
                    {
                        const uint32_t token = input.graph.tokens[node];
                        check_test_expect(test, token < parser.tokens.count && input.deferred_tokens[token] == 0,
                                          "deferred body contributes no semantic expression node");
                    }
                    for (uint32_t event = 0; event < input.environment.bindings->count; event += 1)
                    {
                        const CBindings* bindings = input.environment.bindings;
                        const uint32_t declaration = bindings->declaration[event];
                        if (bindings->operation[event] == C_BINDING_USE && declaration < parser.tokens.count)
                        {
                            check_test_expect(test, input.deferred_tokens[declaration] == 0, "deferred body contributes no semantic name-use event");
                        }
                    }
                }
                if (accepted)
                {
                    check_test_expect(test, program.body_count == (mode == 0 ? fixture->bodies : plans[path].selected_count),
                                      "typed program publishes exactly the semantically selected bodies");
                }
                check_test_expect(test, scratch.length == 0, "both body-checking modes restore private scratch");
                arena_release(&checked);
                arena_release(&retained);
                check_test_types_close(&types);
            }
        }
    }
    if (ready && plans[0].status == CHECK_LAZY_OK && plans[1].status == CHECK_LAZY_OK)
    {
        check_test_expect(test, plans[0].body_count == plans[1].body_count && plans[0].reference_count == plans[1].reference_count &&
                               memcmp(plans[0].deferred_tokens, plans[1].deferred_tokens, (uint64_t)parser.tokens.count + 1) == 0,
                          "independent binding and reachability kernels produce identical lazy source masks");
        for (uint32_t body = 0; body < plans[0].body_count; body += 1)
        {
            const CheckLazyBody a = plans[0].bodies[body];
            const CheckLazyBody b = plans[1].bodies[body];
            check_test_expect(test, a.begin == b.begin && a.end == b.end && a.declaration == b.declaration && a.flags == b.flags && a.selected == b.selected,
                              "lazy paths retain identical ordered body rows");
        }
        if (failures)
        {
            check_test_lazy_failures(test, &parser, &pp, plans, &scratch);
        }
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    arena_release(output + 1);
    arena_release(output);
}

static void check_test_lazy_frontier(CheckTest* test)
{
    static char source[32768];
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    uint32_t length = (uint32_t)snprintf(source, sizeof(source), "# 1 \"frontier.h\" 1 3\n");
    for (uint32_t i = 0; i < 137; i += 1)
    {
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "static int f%u(void);", i);
    }
    for (uint32_t i = 0; i < 137; i += 1)
    {
        if (i == 70)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "static int f%u(void){return 7;}", i);
        }
        else
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "static int f%u(void){return f%u();}", i, i == 136 ? 71 : i + 1);
        }
    }
    length += (uint32_t)snprintf(source + length, sizeof(source) - length, "\n# 1 \"frontier.c\" 2\nint entry(void){return f0();}\n");
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parse_options = { .extension_metadata = true };
    Arena outputs[2] = { arena_reserve(1u << 24), arena_reserve(1u << 24) };
    Arena scratch = arena_reserve(1u << 24);
    CheckLazy plans[2] = { 0 };
    const bool ready = length < sizeof(source) && outputs[0].base != 0 && outputs[1].base != 0 && scratch.base != 0 &&
                       intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                       parse_initialize(&parser, &parse_options, options.language) && pp_run_source(&pp, "frontier.c", (const uint8_t*)source, length) &&
                       parse_translation_unit(&parser, &pp);
    check_test_expect(test, ready, "original 138-body demand graph parses");
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const bool planned = check_lazy_plan(plans + path, &parser, &pp, outputs + path, &scratch, path == 0);
        check_test_expect(test, planned && plans[path].body_count == 138 && plans[path].selected_count == 72 && plans[path].deferred_count == 66,
                          "demand crossing two bitset words retains 71-function chain and ordinary entry");
        for (uint32_t body = 0; planned && body < plans[path].body_count; body += 1)
        {
            check_test_expect(test, plans[path].bodies[body].selected == (body < 71 || body == 137),
                              "unrooted cycle and partial tail remain deferred in original body order");
        }
        check_test_expect(test, scratch.length == 0, "large demand traversal releases scratch");
    }
    if (plans[0].status == CHECK_LAZY_OK && plans[1].status == CHECK_LAZY_OK)
    {
        check_test_expect(test, memcmp(plans[0].deferred_tokens, plans[1].deferred_tokens, (uint64_t)parser.tokens.count + 1) == 0,
                          "scalar and vector demand frontiers match every token beyond bitset word boundaries");
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    arena_release(outputs + 1);
    arena_release(outputs);
}

static __attribute__((noinline)) void check_test_lazy(CheckTest* test)
{
    static const CheckLazyFixture fixtures[] =
    {
        { "empty unit", "", 0, 0, true, true },
        { "unused erroneous system body", "# 1 \"header.h\" 1 3\nstatic int dead(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true },
        { "ordinary K&R declaration-list body", "int f(a)int a;{return a;}\n", 0, 1, true, true },
        { "grouped K&R declaration-list body", "int (f(a))int a;{return a;}\n", 0, 1, true, true },
        { "prototype before K&R definition", "int f(int);int f(a)int a;{return a;}\n", 0, 1, true, true },
        { "unused erroneous K&R system body", "# 1 \"header.h\" 1 3\nstatic int dead(a)int a;{return missing;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true },
        { "referenced K&R system body", "# 1 \"header.h\" 1 3\nstatic int used(a)int a;{return a+1;}static int dead(b)int b;{return missing;}\n# 1 \"user.c\" 2\nint entry(void){return used(2);}\n", 2, 3, false, true },
        { "ordinary body is always checked", "static int dead(void){return missing;} int entry(void){return 0;}", 0, 2, false, false },
        { "used system body is checked", "# 1 \"header.h\" 1 3\nstatic int used_body(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){return used_body();}\n", 0, 2, false, false },
        { "dead header cycle", "# 1 \"header.h\" 1 3\nstatic int b(void); static int a(void){return b();} static int b(void){return a();}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 3, 3, true, true },
        { "transitive call and address", "# 1 \"header.h\" 1 3\nstatic int leaf(void){return 1;} static int middle(void){return leaf();} static int dead(void){return 2;}\n# 1 \"user.c\" 2\nint (*saved)(void)=middle; int entry(void){return saved();}\n", 4, 4, true, true },
        { "exact shadowing object identity", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){int hidden=3; return hidden;}\n", 1, 2, false, true },
        { "function typedef block declaration", "# 1 \"header.h\" 1 3\nstatic int target(void){return 7;}\n# 1 \"user.c\" 2\ntypedef int Function(void); int entry(void){Function target;return target();}\n", 0, 2, true, true },
        { "enumerator shadowing", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){enum {hidden=2};return hidden;}\n", 1, 2, false, true },
        { "retained header function", "# 1 \"header.h\" 1 3\nstatic int kept(void) __attribute__((used)); static int kept(void){return 3;} static int dead(void){return 4;}\n", 2, 2, true, true },
        { "used local data demands its body", "# 1 \"header.h\" 1 3\nstatic int kept(void){static int data __attribute__((used))=3;return data;} static int dead(void){return 4;}\n", 2, 2, true, true },
        { "plain inline can defer", "# 1 \"header.h\" 1 3\ninline int dead(void){return missing;}\n", 1, 1, false, true },
        { "extern inline is an emitted definition", "# 1 \"header.h\" 1 3\nextern inline int kept(void){return missing;}\n", 0, 1, false, false },
        { "later noninline declaration retains body", "# 1 \"header.h\" 1 3\ninline int kept(void){return missing;}\n# 1 \"user.c\" 2\nint kept(void);\n", 0, 1, false, false },
        { "system macro used in ordinary source", "# 1 \"header.h\" 1 3\n#define DEFINE(name) static int name(void){return missing;}\n# 1 \"user.c\" 2\nDEFINE(ordinary)\n", 0, 1, false, false },
        { "ordinary macro used in system source", "#define DEFINE(name) static int name(void){return missing;}\n# 1 \"header.h\" 1 3\nDEFINE(hidden)\n", 1, 1, false, true },
        { "original incomplete type remains incomplete", "# 1 \"header.h\" 1 3\nstruct S; static int query(void){return sizeof(struct S);}\n# 1 \"user.c\" 2\nstruct S{int x;}; int entry(void){return query();}\n", 0, 2, false, false },
        { "deferred incomplete query has no effect", "# 1 \"header.h\" 1 3\nstruct S; static int query(void){return sizeof(struct S);}\n# 1 \"user.c\" 2\nstruct S{int x;}; int entry(void){return sizeof(struct S);}\n", 1, 2, false, true },
        { "graph error is really deferred", "# 1 \"header.h\" 1 3\nstatic int wrong(void){return (void)1;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true },
        { "uninvolved ordinary static body stays checked", "# 1 \"header.h\" 1 3\nstatic int dead(void){return 0;}\n# 1 \"user.c\" 2\nstatic int local(void){return missing;}\n", 1, 2, false, false },
        { "inactive duplicate switch does not reach program", "# 1 \"header.h\" 1 3\nstatic int dead(int n){switch(n){case 1:return 1;case 1:return 2;}return 0;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true },
        { "inactive statement expression region", "# 1 \"header.h\" 1 3\nstatic int dead(int n){return ({int x=n; x+1;});}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, true, true },
        { "inactive invalid initializer", "# 1 \"header.h\" 1 3\nstatic int dead(void){static int*p=3.0;return 0;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true },
        { "inactive invalid FENV placement", "# 1 \"header.h\" 1 3\nstatic int dead(void){int x=1;\n#pragma STDC FENV_ACCESS ON\nreturn x;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true },
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        check_test_lazy_fixture(test, fixtures + fixture, fixture == 1);
    }
    check_test_lazy_frontier(test);
}

#endif
