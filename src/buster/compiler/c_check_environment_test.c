// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Production-reader history fixtures for #113. Unlike c_environment_test.c's supplied semantic events, these use
// check_input's actual events, types and completion positions. Poisoning its released scratch must leave the
// immutable handoff usable by both binding kernels and by out-of-order 1/8-lane position queries. These query
// schedules do not execute deferred bodies. Include after CheckTest helpers and before run_checker_tests.

typedef struct CheckEnvironmentFixture CheckEnvironmentFixture;
struct CheckEnvironmentFixture
{
    const char* source;
    const char* name;
    CBindingNamespace space;
    uint32_t declarations[3];
    uint32_t body_count;
    bool rejected;
    bool history_types;
    bool completion;
    bool body_fallback;
};

static void check_test_environment_queries(CheckTest* test, const CheckEnvironmentFixture* fixture, const Parser* parser,
                                           const InternTable* identifiers, const CheckInput* input, const TypeTable* types)
{
    const CheckEnvironment environment = input->environment;
    const IdentId name = intern_find(identifiers, (const uint8_t*)fixture->name, (uint32_t)strlen(fixture->name));
    uint32_t occurrences[32];
    uint32_t occurrence_count = 0;
    for (uint32_t token = 0; token < parser->tokens.count; token += 1)
    {
        if (parser->tokens.names[token] == name && occurrence_count < BUSTER_ARRAY_LENGTH(occurrences))
        {
            occurrences[occurrence_count++] = token;
        }
    }
    check_test_expect(test, name != 0 && environment.body_count == fixture->body_count && fixture->body_count <= 2,
                      "production environment identifier/body count");
    // Pin real activation positions independently of the event's point: after the whole declarator, before its
    // initializer; after a tag name; after the fixed A=1 enumerator definition in the macro fixture.
    for (uint32_t event = 0; occurrence_count != 0 && event < environment.bindings->count; event += 1)
    {
        const CBindings* bindings = environment.bindings;
        if (bindings->operation[event] == C_BINDING_DECLARE && bindings->depth[event] == 0 && bindings->declaration[event] == occurrences[0] &&
            bindings->key[event] == (uint32_t)fixture->space * bindings->name_capacity + name)
        {
            const uint32_t declaration = occurrences[0];
            uint32_t point = declaration + 1;
            const TypeId type = environment.declaration_types[declaration];
            const uint8_t kind = type_kind(types, type);
            if (fixture->space == C_BINDING_ORDINARY && (kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind)))
            {
                point = parser->tree.parents[declaration + 1] + 1;
            }
            else if (bindings->kind[event] == C_BINDING_ENUMERATOR)
            {
                point = declaration + 3;
            }
            check_test_expect(test, bindings->point[event] == point &&
                                   c_environment_lookup(&environment.file, fixture->space, name, point - 1) == 0 &&
                                   c_environment_lookup(&environment.file, fixture->space, name, point) == event + 1,
                              "production declaration activated before/after its C declaration point");
        }
    }
    uint32_t keys[5];
    uint32_t points[5];
    uint32_t expected[5];
    uint32_t results[5];
    uint32_t queries = 0;
    for (uint32_t slot = 0; slot <= fixture->body_count && fixture->body_count <= 2 && environment.body_count == fixture->body_count; slot += 1)
    {
        const uint32_t occurrence = fixture->declarations[slot];
        const bool resolved = occurrence != BUSTER_CHECK_NONE;
        check_test_expect(test, !resolved || occurrence < occurrence_count, "environment fixture declaration occurrence");
        const uint32_t declaration = resolved && occurrence < occurrence_count ? occurrences[occurrence] : BUSTER_CHECK_NONE;
        const uint32_t first = slot < fixture->body_count ? environment.bodies[slot].begin : parser->tokens.count;
        const uint32_t end = slot < fixture->body_count ? environment.bodies[slot].end - 1 : first;
        if (slot < fixture->body_count)
        {
            const CBodyRange body = environment.bodies[slot];
            check_test_expect(test, body.begin < body.end && body.end <= parser->tokens.count &&
                                   parser->tree.roles[body.begin] == PARSE_ROLE_OPEN_BODY && parser->tree.roles[body.end - 1] == PARSE_ROLE_CLOSE_BODY &&
                                   parser->tree.parents[body.begin] == body.end - 1 && (slot == 0 || environment.bodies[slot - 1].end <= body.begin),
                              "production body slot does not match parser's source-order extent");
        }
        for (uint32_t edge = 0; edge < (slot < fixture->body_count ? 2u : 1u); edge += 1)
        {
            const uint32_t point = edge == 0 ? first : end;
            const uint32_t handle = c_environment_lookup(&environment.file, fixture->space, name, point);
            const uint32_t found = handle != 0 ? environment.bindings->declaration[handle - 1] : BUSTER_CHECK_NONE;
            check_test_expect(test, found == declaration, "production file history chose a local/later declaration");
            keys[queries] = (uint32_t)fixture->space * environment.file.names + name;
            points[queries] = point;
            expected[queries++] = handle;
            if (resolved && occurrence < occurrence_count && fixture->completion)
            {
                const TypeId type = environment.declaration_types[declaration];
                const uint32_t tag = types->types.index[type_index(type)];
                const uint32_t complete = environment.tag_completions[tag];
                check_test_expect(test, complete != BUSTER_CHECK_NONE && complete > 0 && complete <= parser->tokens.count &&
                                       parser->tokens.kinds[complete - 1] == PARSE_KIND_R_BRACE,
                                  "retained completion is not the token after the tag's closing brace");
                const uint8_t expected_complete = slot != 0;
                uint8_t scalar = 3;
                uint8_t vector = 5;
                c_environment_types_scalar(types, environment.tag_completions, environment.tag_count, &type, &point, 1, &scalar);
                c_environment_types(types, environment.tag_completions, environment.tag_count, &type, &point, 1, &vector);
                check_test_expect(test, scalar == expected_complete && vector == scalar, "production tag completion leaked into an earlier body");
            }
        }
    }
    if (fixture->history_types && occurrence_count >= 3)
    {
        const TypeId earlier = environment.declaration_types[occurrences[0]];
        const TypeId later = environment.declaration_types[occurrences[2]];
        check_test_expect(test, earlier != later, "later composite replaced the historical declaration TypeId");
        if (type_kind(types, earlier) == BUSTER_TYPE_KIND_FUNCTION)
        {
            uint32_t earlier_count = 0;
            uint32_t later_count = 0;
            type_parameters(types, earlier, &earlier_count);
            type_parameters(types, later, &later_count);
            check_test_expect(test, earlier_count == 0 && later_count == 1, "historical unprototyped function acquired later parameters");
        }
        else
        {
            check_test_expect(test, type_kind(types, earlier) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY && type_kind(types, later) == BUSTER_TYPE_KIND_ARRAY,
                              "historical array declaration acquired a later bound");
        }
    }
    const uint32_t partitions[] =
    {
        1,
        3,
        16,
    };
    const uint32_t lanes[] =
    {
        1,
        8,
    };
    for (uint32_t partition = 0; partition < BUSTER_ARRAY_LENGTH(partitions); partition += 1)
    {
        for (uint32_t workers = 0; workers < BUSTER_ARRAY_LENGTH(lanes); workers += 1)
        {
            CEnvironmentTestJobs jobs =
            {
                .environment = &environment.file,
                .keys = keys,
                .points = points,
                .results = results,
                .count = queries,
                .partition = partitions[partition],
            };
            const bool ran = lane_run(lanes[workers], c_environment_test_jobs, &jobs, 0);
            check_test_expect(test, ran, "production history query workers failed to start");
            for (uint32_t worker = 0; ran && worker < lanes[workers]; worker += 1)
            {
                check_test_expect(test, jobs.valid[worker], "production history batch rejected a valid key");
            }
            check_test_expect(test, !ran || memcmp(expected, results, (uint64_t)queries * sizeof(uint32_t)) == 0,
                              "production history changed with out-of-order query scheduling");
        }
    }
}

static void check_test_environment_replay(CheckTest* test, const CheckInput* input, Arena* scratch)
{
    const CheckEnvironment* environment = &input->environment;
    const CBindings* bindings = environment->bindings;
    const uint64_t mark = scratch->length;
    uint32_t* scalar = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bindings->count + 1);
    uint32_t* vector = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bindings->count + 1);
    CBindingStatistics statistics;
    const bool valid = scalar != 0 && vector != 0 && c_bindings_scalar(bindings, scalar, scratch) && c_bindings_batch(bindings, vector, scratch, &statistics);
    check_test_expect(test, valid && bindings->count == input->binding_event_count && bindings->capacity == bindings->count &&
                           bindings->heads == 0 && bindings->active == 0 && bindings->previous == 0,
                      "retained events are not a compact replayable reader view");
    for (uint32_t event = 0; valid && event < bindings->count; event += 1)
    {
        check_test_expect(test, scalar[event] == vector[event], "retained event scalar/vector replay differs");
        if (bindings->operation[event] == C_BINDING_USE)
        {
            const uint32_t declaration = scalar[event] != 0 ? bindings->declaration[scalar[event] - 1] : BUSTER_CHECK_NONE;
            check_test_expect(test, input->declarations[bindings->declaration[event]] == declaration, "scratch reset changed a source-use declaration");
        }
    }
    arena_pop_to(scratch, mark);
}

static void check_test_environment_failures(CheckTest* test, const CheckEnvironment* source, const Parser* parser, Preprocessor* pp,
                                            const TypeTable* types, Arena* scratch, uint64_t input_bytes)
{
    const uint64_t mark = scratch->length;
    // Walk every allocation boundary, including failures after the event and completion copies. Output rollback
    // must not invalidate the source, whose storage belongs to a separate retained arena.
    for (uint64_t capacity = 0; capacity < source->retained_bytes + 256; capacity += 32)
    {
        Arena output = arena_reserve(capacity);
        CheckEnvironment copy;
        const bool valid = check_environment_publish(&copy, parser, source->bindings, source->declaration_types, types, source->tag_completions, &output, scratch);
        check_test_expect(test, valid || (!copy.valid && copy.bindings == 0 && copy.file.bindings == 0 && output.length == 0),
                          "failed environment publication retained partial output");
        check_test_expect(test, scratch->length == mark && source->valid, "publication failure changed source/scratch ownership");
        arena_release(&output);
    }
    CheckEnvironment copy;
    check_test_expect(test, !check_environment_publish(&copy, parser, source->bindings, source->declaration_types, types, source->tag_completions, scratch, scratch) &&
                           !copy.valid && scratch->length == mark, "environment publication accepted aliased output/scratch");
    Arena output = arena_reserve(1u << 16);
    Arena exhausted = { 0 };
    check_test_expect(test, !check_environment_publish(&copy, parser, source->bindings, source->declaration_types, types, source->tag_completions, &output, &exhausted) &&
                           !copy.valid && output.length == 0, "late index scratch failure published partial source history");
    arena_release(&output);
    CheckTestTypes limited_types = { 0 };
    const bool initialized = check_test_types_open(&limited_types);
    Arena limited = arena_reserve(input_bytes - 1);
    check_test_expect(test, initialized && limited.base != 0, "late source-publication failure setup");
    if (initialized && limited.base != 0)
    {
        CheckInput failed;
        const bool adapted = check_input(&failed, parser, pp, &limited_types.table, &limited, scratch);
        check_test_expect(test, !adapted && failed.resource && failed.report_count == 0 && !failed.environment.valid && failed.environment.bindings == 0 &&
                               limited.length == input_bytes - source->retained_bytes && scratch->length == mark,
                          "late history publication failure did not propagate through the sequential checker");
        check_test_expect(test, !check_input(&failed, parser, pp, &limited_types.table, scratch, scratch) && failed.resource &&
                               !failed.environment.valid && scratch->length == mark, "source reader accepted aliased retained/transient arenas");
    }
    arena_release(&limited);
    check_test_types_close(&limited_types);
}

static void check_test_environment_source(CheckTest* test, const CheckEnvironmentFixture* fixture, bool failures)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    const PPOptions options =
    {
        .language =
        {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
    };
    CheckTestTypes types[2] = { 0 };
    Arena storage[2] = { 0 };
    Arena checked_storage[2] = { 0 };
    Arena scratch[2] = { 0 };
    CheckInput inputs[2] = { 0 };
    CheckStream streams[2] = { 0 };
    bool checked[2] = { 0 };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                 pp_run_source(&pp, "checker-environment.c", (const uint8_t*)fixture->source, (uint32_t)strlen(fixture->source));
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const ParseOptions parse_options = { .scalar = path == 0 };
        ready = ready && parse_initialize(&parsers[path], &parse_options, options.language) && check_test_types_open(&types[path]);
        storage[path] = arena_reserve(1u << 24);
        checked_storage[path] = arena_reserve(1u << 24);
        scratch[path] = arena_reserve(1u << 24);
        ready = ready && storage[path].base != 0 && checked_storage[path].base != 0 && scratch[path].base != 0;
    }
    check_test_expect(test, ready, "production environment source allocation/preprocessing");
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const bool parsed = parse_translation_unit(&parsers[path], &pp);
        check_test_expect(test, parsed, "production environment fixture did not parse");
        if (parsed)
        {
            const uint64_t mark = scratch[path].length;
            void* previous = arena_push(&scratch[path], 1u << 20, 64);
            check_test_expect(test, previous != 0, "source reader scratch initialization");
            if (previous != 0)
            {
                memset(previous, path == 0 ? 0x3c : 0xc3, 1u << 20);
            }
            arena_pop_to(&scratch[path], mark);
            const bool adapted = check_input(&inputs[path], &parsers[path], &pp, &types[path].table, &storage[path], &scratch[path]);
            char description[384];
            snprintf(description, sizeof(description), "production environment source: %s path, %s expected: %.250s", path == 0 ? "scalar" : "vector",
                     fixture->rejected ? "reject" : "accept", fixture->source);
            check_test_expect(test, adapted != fixture->rejected && !inputs[path].resource, description);
            check_test_expect(test, scratch[path].length == mark && inputs[path].environment.valid == adapted,
                              "source publication/scratch-reset status disagrees with checker");
            if (adapted)
            {
                // Reuse the released reader's pages, then allocate the replay workspace over them too.
                void* poison = arena_push(&scratch[path], 1u << 20, 64);
                check_test_expect(test, poison != 0, "scratch poison allocation");
                if (poison != 0)
                {
                    memset(poison, 0xa5, 1u << 20);
                }
                arena_pop_to(&scratch[path], mark);
                const CheckOptions check_options = { .scalar = path == 0 };
                checked[path] = check_graph_run(&types[path].table, &inputs[path].graph, &checked_storage[path], &scratch[path], &check_options, &streams[path]);
                check_test_expect(test, checked[path] && streams[path].report_count == 0 && scratch[path].length == mark,
                                  "production history fixture lost valid sequential graph output after scratch reuse");
                check_test_body_bindings(test, &inputs[path], fixture->body_fallback);
                check_test_environment_replay(test, &inputs[path], &scratch[path]);
                if (fixture->name[0] != 0)
                {
                    check_test_environment_queries(test, fixture, &parsers[path], &identifiers, &inputs[path], &types[path].table);
                }
                else
                {
                    check_test_expect(test, inputs[path].environment.body_count == 0 && inputs[path].environment.file.count == 0,
                                      "empty source acquired declarations/bodies");
                }
                if (failures && path == 1)
                {
                    check_test_environment_failures(test, &inputs[path].environment, &parsers[path], &pp, &types[path].table, &scratch[path], storage[path].length);
                }
            }
        }
    }
    if (ready && inputs[0].environment.valid && inputs[1].environment.valid)
    {
        const CheckEnvironment* a = &inputs[0].environment;
        const CheckEnvironment* b = &inputs[1].environment;
        const CBindings* x = a->bindings;
        const CBindings* y = b->bindings;
        const bool counts = x->count == y->count && a->body_count == b->body_count && a->tag_count == b->tag_count;
        check_test_expect(test, counts, "production scalar/vector history counts differ");
        check_test_expect(test, checked[0] && checked[1] && inputs[0].graph.argument_count == inputs[1].graph.argument_count &&
                               check_test_stream_equal(&streams[0], &streams[1], inputs[0].graph.argument_count),
                          "production history handoff changed scalar/vector sequential typed output");
        for (uint32_t event = 0; counts && event < x->count; event += 1)
        {
            const bool restore = x->operation[event] == C_BINDING_RESTORE;
            check_test_expect(test, x->operation[event] == y->operation[event] && x->declaration[event] == y->declaration[event] && x->point[event] == y->point[event] &&
                                   (restore || (x->key[event] == y->key[event] && x->end[event] == y->end[event] && x->depth[event] == y->depth[event] && x->kind[event] == y->kind[event])),
                              "production scalar/vector defined event columns differ");
        }
        for (uint32_t tag = 0; counts && tag < a->tag_count; tag += 1)
        {
            check_test_expect(test, a->tag_completions[tag] == b->tag_completions[tag], "production completion columns differ");
        }
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        arena_release(&scratch[path]);
        arena_release(&checked_storage[path]);
        arena_release(&storage[path]);
        check_test_types_close(&types[path]);
        parse_release(&parsers[path]);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_environment_counts(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = mapping != MAP_FAILED && mprotect(mapping, page, PROT_READ | PROT_WRITE) == 0;
    check_test_expect(test, mapped, "guarded production body-count mapping");
    static Parser parser;
    parser = (Parser){ 0 };
    for (uint32_t count = 0; mapped && count <= 129; count += 1)
    {
        uint8_t* roles = mapping + page - count;
        uint32_t expected = 0;
        for (uint32_t token = 0; token < count; token += 1)
        {
            roles[token] = token % 3 == 1 ? PARSE_ROLE_CLOSE_BODY : PARSE_ROLE_OPERAND;
            expected += token % 3 == 1;
        }
        parser.tree.roles = roles;
        parser.tokens.count = count;
        parser.options.scalar = true;
        const uint32_t scalar = check_environment_body_count(&parser);
        parser.options.scalar = false;
        check_test_expect(test, scalar == expected && check_environment_body_count(&parser) == expected,
                          "guarded production body-count scalar/vector mismatch");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}

static void check_test_environments(CheckTest* test)
{
    const CheckEnvironmentFixture fixtures[] =
    {
        {
            .source = "int x; int f(void) { int x=1; return x; } int g(void) { return x; } int x;\n",
            .name = "x",
            .declarations =
            {
                0,
                0,
                4,
            },
            .body_count = 2,
        },
        {
            .source = "int target(); int before(void) { return target(); } int target(int); int after(void) { return target(1); }\n",
            .name = "target",
            .declarations =
            {
                0,
                2,
                2,
            },
            .body_count = 2,
            .history_types = true,
        },
        {
            .source = "int x=sizeof x; int f(void) { return x; }\n",
            .name = "x",
            .declarations =
            {
                0,
                0,
                0,
            },
            .body_count = 1,
        },
        {
            .source = "extern int a[]; int before(void) { return a[0]; } extern int a[7]; int after(void) { return a[6]; }\n",
            .name = "a",
            .declarations =
            {
                0,
                2,
                2,
            },
            .body_count = 2,
            .history_types = true,
        },
        {
            .source = "int f(int f) { return f; } int g(int x) { int y=x; { int x=y+1; y=x; } return y; }\n",
            .name = "f",
            .declarations =
            {
                0,
                0,
                0,
            },
            .body_count = 2,
        },
        {
            .source = "int target(); int f(void) { { int target(int); target(1); } return target(); } int target(int); int g(void) { return target(1); }\n",
            .name = "target",
            .declarations =
            {
                0,
                4,
                4,
            },
            .body_count = 2,
        },
        {
            .source = "struct S; int before(void) { struct S { long y; }; return 0; } struct S { int x; }; int after(void) { return 0; }\n",
            .name = "S",
            .space = C_BINDING_TAG,
            .declarations =
            {
                0,
                0,
                0,
            },
            .body_count = 2,
            .completion = true,
        },
        {
            .source = "int before(void) { int hidden(int); return 0; } int after(void) { return 0; }\n",
            .name = "hidden",
            .declarations =
            {
                BUSTER_CHECK_NONE,
                BUSTER_CHECK_NONE,
                BUSTER_CHECK_NONE,
            },
            .body_count = 2,
        },
        {
            .source = "#define BODY { return A; }\nenum E { A=1 }; int f(void) BODY int g(void) BODY\n",
            .name = "A",
            .declarations =
            {
                0,
                0,
                0,
            },
            .body_count = 2,
        },
        {
            .source = "int f(void) { return later; } int later=7;\n",
            .name = "later",
            .rejected = true,
        },
        {
            .source = "struct S; int f(void) { return sizeof(struct S); } struct S { int x; };\n",
            .name = "S",
            .rejected = true,
        },
        {
            .source = "",
            .name = "",
        },
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        check_test_environment_source(test, fixtures + fixture, fixture == 0);
    }
    char source[8192];
    int written = snprintf(source, sizeof(source), "int seed; int f(int x) {");
    bool generated = written > 0 && (uint32_t)written < sizeof(source);
    uint32_t length = generated ? (uint32_t)written : 0;
    for (uint32_t scope = 0; generated && scope < 32; scope += 1)
    {
        written = snprintf(source + length, sizeof(source) - length, "{ int x=%u; x=x+1; }", scope);
        generated = written > 0 && (uint32_t)written < sizeof(source) - length;
        length += generated ? (uint32_t)written : 0;
    }
    if (generated)
    {
        written = snprintf(source + length, sizeof(source) - length, "return x+seed; } int g(void) { return seed; }\n");
        generated = written > 0 && (uint32_t)written < sizeof(source) - length;
    }
    check_test_expect(test, generated, "body fallback source generation");
    if (generated)
    {
        const CheckEnvironmentFixture fixture =
        {
            .source = source,
            .name = "seed",
            .declarations =
            {
                0,
                0,
                0,
            },
            .body_count = 2,
            .body_fallback = true,
        };
        check_test_environment_source(test, &fixture, false);
    }
    check_test_environment_counts(test);
}
