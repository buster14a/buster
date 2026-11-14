// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original lazy-body fixtures. Syntax, bindings, system provenance and source semantics all come from the real
// frontend; the independently stated source-order mask is the selection oracle. Include after CheckTest helpers.

#ifndef BUSTER_CHECK_LAZY_TEST_INCLUDED
#define BUSTER_CHECK_LAZY_TEST_INCLUDED 1

typedef enum CheckLazyTestPath
{
    CHECK_LAZY_TEST_ANY,
    CHECK_LAZY_TEST_TREE,
    CHECK_LAZY_TEST_REPLAY,
} CheckLazyTestPath;

typedef struct CheckLazyFixture CheckLazyFixture;
struct CheckLazyFixture
{
    const char* name;
    const char* source;
    uint64_t deferred;
    uint32_t bodies;
    bool ordinary_valid;
    bool lazy_valid;
    CheckLazyTestPath path;
    bool no_history;
};

static void check_test_lazy_path(CheckTest* test, const CheckLazy* plan, uint32_t token_count, CheckLazyTestPath expected, const char* description)
{
    const bool tree = plan->status == CHECK_LAZY_OK && plan->tree_tokens == token_count && plan->replay_tokens == 0;
    const bool replay = plan->status == CHECK_LAZY_OK && plan->replay_tokens == token_count && plan->tree_tokens == 0;
    bool valid = expected == CHECK_LAZY_TEST_TREE ? tree : expected == CHECK_LAZY_TEST_REPLAY ? replay : tree || replay;
    valid &= !plan->tree_all_selected || (tree && plan->selected_count == plan->body_count && plan->deferred_count == 0);
    check_test_expect(test, valid, description);
}

// The replay remains a separate grammar/binding oracle. Demand graphs may store different edges; only the
// source-ordered body policy and exact compatibility mask have to agree. A tree proof that every body stays
// selected can omit dependency-derived RETAIN bits; all other flags still agree with the replay.
static void check_test_lazy_same(CheckTest* test, const CheckLazy* planned, const CheckLazy* expected, const char* description)
{
    const bool comparable = planned->status == CHECK_LAZY_OK && expected->status == CHECK_LAZY_OK &&
                            planned->body_count == expected->body_count && planned->token_count == expected->token_count;
    bool same = comparable && planned->selected_count == expected->selected_count && planned->deferred_count == expected->deferred_count &&
                memcmp(planned->deferred_tokens, expected->deferred_tokens, (uint64_t)planned->token_count + 1) == 0;
    const uint32_t flags = planned->tree_all_selected || expected->tree_all_selected ? ~BUSTER_CHECK_LAZY_RETAIN : UINT32_MAX;
    const uint32_t body_count = comparable ? planned->body_count : 0;
    for (uint32_t body = 0; body < body_count; body += 1)
    {
        const CheckLazyBody a = planned->bodies[body];
        const CheckLazyBody b = expected->bodies[body];
        same &= a.begin == b.begin && a.end == b.end && a.declaration == b.declaration && (a.flags & flags) == (b.flags & flags) && a.selected == b.selected;
    }
    check_test_expect(test, same, description);
}

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
    static Parser parsers[2];
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true, .no_history = fixture->no_history };
    Arena output[6] =
    {
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
    };
    Arena scratch = arena_reserve(1u << 24);
    CheckLazy plans[4] = { 0 };
    CheckLazy replays[2] = { 0 };
    bool ready = scratch.base != 0 && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                 pp_run_source(&pp, "lazy-original.c", (const uint8_t*)fixture->source, strlen(fixture->source));
    for (uint32_t path = 0; path < BUSTER_ARRAY_LENGTH(output); path += 1)
    {
        ready &= output[path].base != 0;
    }
    char description[256];
    snprintf(description, sizeof(description), "%s: lazy source syntax setup", fixture->name);
    check_test_expect(test, ready, description);
    for (uint32_t path = 0; ready && path < BUSTER_ARRAY_LENGTH(parsers); path += 1)
    {
        const ParseOptions parse_options = { .scalar = path == 0, .extension_metadata = true };
        ready = parse_initialize(parsers + path, &parse_options, options.language) && parse_translation_unit(parsers + path, &pp);
        snprintf(description, sizeof(description), "%s: independent %s syntax parse", fixture->name, path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready, description);
        const bool replayed = ready && check_lazy_replay(replays + path, parsers + path, &pp, output + 4 + path, &scratch, path == 0);
        snprintf(description, sizeof(description), "%s: %s grammar replay oracle", fixture->name, path == 0 ? "scalar" : "vector");
        check_test_expect(test, replayed && scratch.length == 0, description);
        check_test_lazy_path(test, replays + path, parsers[path].tokens.count, CHECK_LAZY_TEST_REPLAY, "replay oracle reports every grammar token");
        uint32_t eager_selected[2] = { 0 };
        const bool scalar_eager = check_lazy_eager(parsers + path, &pp, true, eager_selected);
        const bool vector_eager = check_lazy_eager(parsers + path, &pp, false, eager_selected + 1);
        const bool opaque = replayed && replays[path].body_count != 0 && (replays[path].bodies[0].flags & BUSTER_CHECK_LAZY_OPAQUE) != 0;
        snprintf(description, sizeof(description), "%s: eager preflight agrees with the old opaque policy", fixture->name);
        check_test_expect(test, replayed && scalar_eager == vector_eager && eager_selected[0] == eager_selected[1] &&
                               (replays[path].body_count == 0 || scalar_eager == opaque) &&
                               (!scalar_eager || (replays[path].deferred_count == 0 && eager_selected[0] == replays[path].body_count)), description);
    }
    for (uint32_t path = 0; ready && path < BUSTER_ARRAY_LENGTH(plans); path += 1)
    {
        Parser* parser = parsers + path / 2;
        const bool scalar = (path & 1) == 0;
        const bool planned = check_lazy_plan(&plans[path], parser, &pp, &output[path], &scratch, scalar);
        snprintf(description, sizeof(description), "%s: %s parse with %s demand plan", fixture->name, path < 2 ? "scalar" : "vector", scalar ? "scalar" : "vector");
        check_test_expect(test, planned && plans[path].status == CHECK_LAZY_OK && plans[path].body_count == fixture->bodies && scratch.length == 0,
                          description);
        snprintf(description, sizeof(description), "%s: %s parse/%s plan agrees with grammar replay", fixture->name, path < 2 ? "scalar" : "vector", scalar ? "scalar" : "vector");
        check_test_lazy_same(test, plans + path, replays + path / 2, description);
        snprintf(description, sizeof(description), "%s: production planner reports the required tree/replay coverage", fixture->name);
        check_test_lazy_path(test, plans + path, parser->tokens.count, fixture->path, description);
        uint64_t deferred = 0;
        for (uint32_t body = 0; planned && body < plans[path].body_count && body < 64; body += 1)
        {
            deferred |= !plans[path].bodies[body].selected ? UINT64_C(1) << body : 0;
            check_test_expect(test, plans[path].bodies[body].declaration < parser->tokens.count &&
                                   parser->tokens.kinds[plans[path].bodies[body].declaration] == PARSE_KIND_IDENTIFIER,
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
                const bool adapted = initialized && (mode == 0 ? check_input(&input, parser, &pp, &types.table, &retained, &scratch) :
                                       check_input_selected(&input, parser, &pp, &types.table, &retained, &scratch, plans[path].deferred_tokens));
                const CheckOptions checking = { .scalar = scalar };
                const bool graphed = adapted && check_graph_run(&types.table, &input.graph, &checked, &scratch, &checking, &stream);
                const bool prepared = graphed && stream.report_count == 0 &&
                                      check_initializers_run(&initializers, &input, &stream, parser, &types.table, &checked, &scratch) &&
                                      check_asms_run(&asms, &input, &stream, &types.table, &checked, &scratch);
                const CheckProgramOptions program_options = { .initializers = &initializers, .asms = &asms, .identifiers = &identifiers, .scalar = scalar };
                const bool accepted = prepared && check_program(&program, parser, &input, &stream, &types.table, program_options, &checked, &scratch);
                const bool expected = mode == 0 ? fixture->ordinary_valid : fixture->lazy_valid;
                snprintf(description, sizeof(description), "%s: %s checking has the required acceptance", fixture->name, mode == 0 ? "default all-body" : "selected-body");
                check_test_expect(test, initialized && accepted == expected && !input.resource && (!adapted || graphed), description);
                if (adapted && mode != 0)
                {
                    check_test_expect(test, input.deferred_tokens != plans[path].deferred_tokens &&
                                           memcmp(input.deferred_tokens, plans[path].deferred_tokens, (uint64_t)parser->tokens.count + 1) == 0,
                                      "selected source reader owns an immutable demand mask copy");
                    for (uint32_t node = 0; node < input.graph.count; node += 1)
                    {
                        const uint32_t token = input.graph.tokens[node];
                        check_test_expect(test, token < parser->tokens.count && input.deferred_tokens[token] == 0,
                                          "deferred body contributes no semantic expression node");
                    }
                    for (uint32_t event = 0; event < input.environment.bindings->count; event += 1)
                    {
                        const CBindings* bindings = input.environment.bindings;
                        const uint32_t declaration = bindings->declaration[event];
                        if (bindings->operation[event] == C_BINDING_USE && declaration < parser->tokens.count)
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
    for (uint32_t path = 1; ready && path < BUSTER_ARRAY_LENGTH(plans); path += 1)
    {
        check_test_lazy_same(test, plans + path, plans, "independent syntax parses and demand kernels produce identical lazy body rows and masks");
    }
    if (ready && failures && plans[0].status == CHECK_LAZY_OK)
    {
        check_test_lazy_failures(test, parsers, &pp, plans, &scratch);
    }
    for (uint32_t path = 0; path < BUSTER_ARRAY_LENGTH(parsers); path += 1)
    {
        parse_release(parsers + path);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    for (uint32_t path = 0; path < BUSTER_ARRAY_LENGTH(output); path += 1)
    {
        arena_release(output + path);
    }
}

static void check_test_lazy_frontier(CheckTest* test)
{
    static char source[32768];
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
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
    Arena outputs[6] =
    {
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
        arena_reserve(1u << 24),
    };
    Arena scratch = arena_reserve(1u << 24);
    CheckLazy plans[4] = { 0 };
    CheckLazy replays[2] = { 0 };
    bool ready = length < sizeof(source) && scratch.base != 0 && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                 pp_run_source(&pp, "frontier.c", (const uint8_t*)source, length);
    for (uint32_t path = 0; path < BUSTER_ARRAY_LENGTH(outputs); path += 1)
    {
        ready &= outputs[path].base != 0;
    }
    check_test_expect(test, ready, "original 138-body demand graph preprocesses");
    for (uint32_t path = 0; ready && path < BUSTER_ARRAY_LENGTH(parsers); path += 1)
    {
        const ParseOptions parse_options = { .scalar = path == 0, .extension_metadata = true };
        ready = parse_initialize(parsers + path, &parse_options, options.language) && parse_translation_unit(parsers + path, &pp);
        check_test_expect(test, ready, "original 138-body demand graph parses independently on both syntax paths");
        const bool replayed = ready && check_lazy_replay(replays + path, parsers + path, &pp, outputs + 4 + path, &scratch, path == 0);
        check_test_expect(test, replayed && scratch.length == 0, "original 138-body demand graph has an independent grammar replay oracle");
        check_test_lazy_path(test, replays + path, parsers[path].tokens.count, CHECK_LAZY_TEST_REPLAY, "frontier replay reports every grammar token");
    }
    for (uint32_t path = 0; ready && path < BUSTER_ARRAY_LENGTH(plans); path += 1)
    {
        const Parser* parser = parsers + path / 2;
        const bool planned = check_lazy_plan(plans + path, parser, &pp, outputs + path, &scratch, (path & 1) == 0);
        check_test_expect(test, planned && plans[path].body_count == 138 && plans[path].selected_count == 72 && plans[path].deferred_count == 66,
                          "demand crossing two bitset words retains 71-function chain and ordinary entry");
        check_test_lazy_same(test, plans + path, replays + path / 2, "demand frontier agrees with the grammar replay on every body and token");
        check_test_lazy_path(test, plans + path, parser->tokens.count, CHECK_LAZY_TEST_TREE, "ordinary prototype/function demand frontier needs no grammar replay");
        for (uint32_t body = 0; planned && body < plans[path].body_count; body += 1)
        {
            check_test_expect(test, plans[path].bodies[body].selected == (body < 71 || body == 137),
                              "unrooted cycle and partial tail remain deferred in original body order");
        }
        check_test_expect(test, scratch.length == 0, "large demand traversal releases scratch");
    }
    for (uint32_t path = 1; ready && path < BUSTER_ARRAY_LENGTH(plans); path += 1)
    {
        check_test_lazy_same(test, plans + path, plans, "independent syntax parses and demand frontiers agree beyond bitset word boundaries");
    }
    for (uint32_t path = 0; path < BUSTER_ARRAY_LENGTH(parsers); path += 1)
    {
        parse_release(parsers + path);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    for (uint32_t path = 0; path < BUSTER_ARRAY_LENGTH(outputs); path += 1)
    {
        arena_release(outputs + path);
    }
}

// Exact ends protect both the masked token loads and the name-table gathers. In particular, the nested
// ATTRIBUTE_LIST opener is not one of the four attribute attachment roles that marks a special token.
static void check_test_lazy_masks(CheckTest* test)
{
    enum
    {
        KINDS,
        ROLES,
        NAMES,
        TARGETS,
        COLUMNS,
    };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[COLUMNS];
    bool ready = true;
    for (uint32_t column = 0; column < COLUMNS; column += 1)
    {
        mappings[column] = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[column] != MAP_FAILED && mprotect(mappings[column] + page, page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "lazy name masks allocate exact guarded column ends");
    static const uint32_t target_values[] =
    {
        0,
        1,
        0,
        BUSTER_CHECK_LAZY_TREE_OPAQUE,
        BUSTER_CHECK_LAZY_TREE_KEEP,
        BUSTER_CHECK_LAZY_TREE_OPAQUE | BUSTER_CHECK_LAZY_TREE_KEEP | 1,
    };
    static const uint32_t name_values[] = { 0, 1, 2, 3, 4, 5, 6, UINT32_MAX, UINT32_C(0x80000000), INT32_MAX };
    static const uint8_t role_values[] =
    {
        PARSE_ROLE_NONE,
        PARSE_ROLE_OPEN_ATTRIBUTE,
        PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER,
        PARSE_ROLE_OPEN_ATTRIBUTE_LEFT,
        PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT,
        PARSE_ROLE_OPEN_ATTRIBUTE_LIST,
        PARSE_ROLE_OPERAND,
    };
    static const uint32_t limits[] = { 0, 1, BUSTER_ARRAY_LENGTH(target_values) };
    uint32_t cases = 0;
    uint32_t differences = 0;
    static Parser parser;
    for (uint32_t limit_index = 0; ready && limit_index < BUSTER_ARRAY_LENGTH(limits); limit_index += 1)
    {
        const uint32_t limit = limits[limit_index];
        uint32_t* targets = (uint32_t*)(mappings[TARGETS] + page - limit * sizeof(uint32_t));
        for (uint32_t name = 0; name < limit; name += 1)
        {
            targets[name] = target_values[name];
        }
        for (uint32_t count = 0; count <= 64; count += 1)
        {
            for (uint32_t round = 0; round < 8; round += 1)
            {
                const uint32_t first = (round & 1) != 0 ? 5 : 0;
                uint8_t* kinds = mappings[KINDS] + page - count - first;
                uint8_t* roles = mappings[ROLES] + page - count - first;
                uint32_t* names = (uint32_t*)(mappings[NAMES] + page - ((uint64_t)count + first) * sizeof(uint32_t));
                memset(kinds, PARSE_KIND_ASM, count + first);
                memset(roles, PARSE_ROLE_OPEN_ATTRIBUTE, count + first);
                memset(names, 0xff, ((uint64_t)count + first) * sizeof(uint32_t));
                uint64_t expected = 0;
                for (uint32_t lane = 0; lane < count; lane += 1)
                {
                    const uint8_t role = role_values[(lane + round) % BUSTER_ARRAY_LENGTH(role_values)];
                    const uint32_t name = name_values[(lane * 3 + round) % BUSTER_ARRAY_LENGTH(name_values)];
                    const bool assembly = (lane + round) % 11 == 0;
                    kinds[first + lane] = assembly ? PARSE_KIND_ASM : PARSE_KIND_IDENTIFIER;
                    roles[first + lane] = role;
                    names[first + lane] = name;
                    const bool named = name < limit && (name == 1 || (name >= 3 && name <= 5));
                    const bool attribute = role == PARSE_ROLE_OPEN_ATTRIBUTE || role == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER ||
                                           role == PARSE_ROLE_OPEN_ATTRIBUTE_LEFT || role == PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT;
                    expected |= named || assembly || attribute ? UINT64_C(1) << lane : 0;
                }
                parser.tokens.kinds = kinds;
                parser.tokens.names = names;
                parser.tree.roles = roles;
                const uint64_t scalar = check_lazy_tree_mask(&parser, targets, limit, first, count, true);
                const uint64_t vector = check_lazy_tree_mask(&parser, targets, limit, first, count, false);
                differences += scalar != expected || vector != expected;
                cases += 1;
            }
        }
    }
    check_test_expect(test, differences == 0 && cases == 1560, "lazy scalar/vector masks match independent keys and roles at every guarded tail");
    for (uint32_t column = 0; column < COLUMNS; column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], 2 * page);
        }
    }
    printf("lazy name masks: %u cases, %u differences (guarded tails and name tables)\n", cases, differences);
}

static __attribute__((noinline)) void check_test_lazy(CheckTest* test)
{
    static const CheckLazyFixture fixtures[] =
    {
        { "empty unit", "", 0, 0, true, true },
        { "unused erroneous system body", "# 1 \"header.h\" 1 3\nstatic int dead(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true, CHECK_LAZY_TEST_TREE },
        { "grouped function declarator", "# 1 \"header.h\" 1 3\nstatic int (hidden)(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true },
        { "function returning a function pointer", "# 1 \"header.h\" 1 3\nstatic int (*hidden(void))(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true },
        { "ordinary K&R declaration-list body", "int f(a)int a;{return a;}\n", 0, 1, true, true, CHECK_LAZY_TEST_REPLAY },
        { "grouped K&R declaration-list body", "int (f(a))int a;{return a;}\n", 0, 1, true, true },
        { "prototype before K&R definition", "int f(int);int f(a)int a;{return a;}\n", 0, 1, true, true },
        { "unused erroneous K&R system body", "# 1 \"header.h\" 1 3\nstatic int dead(a)int a;{return missing;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 1, 2, false, true },
        { "referenced K&R system body", "# 1 \"header.h\" 1 3\nstatic int used(a)int a;{return a+1;}static int dead(b)int b;{return missing;}\n# 1 \"user.c\" 2\nint entry(void){return used(2);}\n", 2, 3, false, true },
        { "ordinary body is always checked", "static int dead(void){return missing;} int entry(void){return 0;}", 0, 2, false, false },
        { "used system body is checked", "# 1 \"header.h\" 1 3\nstatic int used_body(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){return used_body();}\n", 0, 2, false, false },
        { "dead header cycle", "# 1 \"header.h\" 1 3\nstatic int b(void); static int a(void){return b();} static int b(void){return a();}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 3, 3, true, true },
        { "transitive call and address", "# 1 \"header.h\" 1 3\nstatic int leaf(void){return 1;} static int middle(void){return leaf();} static int dead(void){return 2;}\n# 1 \"user.c\" 2\nint (*saved)(void)=middle; int entry(void){return saved();}\n", 4, 4, true, true },
        { "exact shadowing object identity", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){int hidden=3; return hidden;}\n", 1, 2, false, true },
        { "grouped array declarator remains a shadow", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){int (hidden)[4];return sizeof hidden;}\n", 1, 2, false, true },
        { "grouped member array remains a member", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nstruct S{int (hidden)[4];};int entry(struct S value){return sizeof value.hidden;}\n", 1, 2, false, true },
        { "independent namespaces and nested shadows", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\n"
          "struct hidden{int hidden;};int prototype(int hidden);int entry(int hidden){"
          "{int hidden=1;(void)hidden;}{enum{hidden=2};(void)hidden;}"
          "{struct hidden value={.hidden=3};(void)value.hidden;}goto hidden;hidden:return hidden;}\n", 1, 2, false, true },
        { "function parameter is an adjusted object", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(int hidden(void)){return hidden();}\n", 1, 2, false, true },
        { "record field prototype preserves conservative bindings", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nstruct S{int (*callback)(int hidden,int a[sizeof hidden]);};int entry(void){return 0;}\n", 0, 2, false, false, CHECK_LAZY_TEST_REPLAY },
        { "prototype shadow ends before demanded body", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint prototype(int hidden);int entry(void){return hidden();}\n", 0, 2, false, false },
        { "array bound precedes declarator activation", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){int hidden[sizeof &hidden];return sizeof hidden;}\n", 0, 2, false, false },
        { "initializer follows declarator activation", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){int hidden=sizeof &hidden;return hidden;}\n", 1, 2, false, true },
        { "enumerator value precedes activation", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){enum{hidden=sizeof &hidden};return hidden;}\n", 0, 2, false, false },
        { "block function redeclaration demands body", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){extern int hidden(void);return hidden();}\n", 0, 2, false, false },
        { "closed block restores function binding", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){{int hidden=1;(void)hidden;}return hidden();}\n", 0, 2, false, false },
        { "function typedef block declaration", "# 1 \"header.h\" 1 3\nstatic int target(void){return 7;}\n# 1 \"user.c\" 2\ntypedef int Function(void); int entry(void){Function target;return target();}\n", 0, 2, true, true },
        { "enumerator shadowing", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){enum {hidden=2};return hidden;}\n", 1, 2, false, true },
        { "retained header function", "# 1 \"header.h\" 1 3\nstatic int kept(void) __attribute__((used)); static int kept(void){return 3;} static int dead(void){return 4;}\n", 2, 2, true, true },
        { "used local data demands its body", "# 1 \"header.h\" 1 3\nstatic int kept(void){static int data __attribute__((used))=3;return data;} static int dead(void){return 4;}\n", 2, 2, true, true },
        { "GNU inline attribute retains body", "# 1 \"header.h\" 1 3\nstatic inline int hidden(void) __attribute__((gnu_inline));static inline int hidden(void){return missing;}\n", 0, 1, false, false },
        { "constructor attribute retains body", "# 1 \"header.h\" 1 3\n__attribute__((constructor)) static void hidden(void){missing=1;}\n", 0, 1, false, false },
        { "attribute expression demands function body", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){int x __attribute__((aligned(sizeof &hidden)));return sizeof x;}\n", 0, 2, false, false, CHECK_LAZY_TEST_REPLAY },
        { "opaque assembly retains every body", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}static int untouched(void){return 1;}\n# 1 \"user.c\" 2\n__asm__(\"\");int entry(void){return 0;}\n", 0, 3, false, false, CHECK_LAZY_TEST_TREE },
        { "opaque assembly bypasses grouped K&R certification", "# 1 \"header.h\" 1 3\nstatic int (hidden(a))int a;{return missing;}\n# 1 \"user.c\" 2\n__asm__(\"\");int entry(void){return 0;}\n", 0, 2, false, false, CHECK_LAZY_TEST_REPLAY },
        { "opaque alias retains every body", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}static int untouched(void){return 1;}\n# 1 \"user.c\" 2\nextern int named(void) __attribute__((alias(\"untouched\")));\n", 0, 2, false, false },
        { "missing history retains every body", "# 1 \"header.h\" 1 3\nstatic int hidden(void){return missing;}\n# 1 \"user.c\" 2\nint entry(void){return 0;}\n", 0, 2, false, false, CHECK_LAZY_TEST_TREE, true },
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
    check_test_lazy_masks(test);
}

#endif
