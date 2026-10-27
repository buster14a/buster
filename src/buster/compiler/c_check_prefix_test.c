// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original diagnostic-order fixtures and adversarial prefix-storage/forest checks. Include after c_checker_test.c
// and c_check_prefix.c. Every available complete-declaration prefix is compared with a fresh parse of exactly those
// declarations, through both parser/checker paths, and with Clang's first error from the original unmodified file.
// The same-malformed-declaration limitation has its own explicit check; it is not counted as oracle parity.

#ifndef BUSTER_CHECK_PREFIX_TEST_IMPLEMENTATION
#define BUSTER_CHECK_PREFIX_TEST_IMPLEMENTATION 1

typedef struct CheckPrefixFixture CheckPrefixFixture;
struct CheckPrefixFixture
{
    const char* name;
    const char* before;
    const char* after;
    uint32_t line;
};

static void check_test_prefix_faults(CheckTest* test, Parser* parser)
{
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    bool ready = output.base != 0 && scratch.base != 0;
    check_test_expect(test, ready, "prefix fault arenas initialize");
    if (ready)
    {
        memset(output.base, 0xa5, output.capacity);
        memset(scratch.base, 0x5a, scratch.capacity);
        output.length = 33;
        scratch.length = 19;
        CheckPrefix prefix;
        ready = check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_READY;
        check_test_expect(test, ready && scratch.length == 19, "prefix ignores poisoned scratch and restores its mark");
        const uint64_t used = output.length;
        if (ready)
        {
            const uint32_t end = prefix.end;
            const uint64_t capacity = output.capacity;
            for (uint64_t limit = 33; limit < used; limit += 64)
            {
                output.length = 33;
                output.capacity = limit;
                const CheckPrefixStatus status = check_prefix_make(&prefix, parser, &output, &scratch);
                check_test_expect(test, status == CHECK_PREFIX_RESOURCE && prefix.end == 0 && prefix.parser.tokens.count == 0 &&
                                      output.length == 33 && scratch.length == 19, "prefix output exhaustion rolls back all columns");
            }
            output.capacity = capacity;
            output.length = 33;
            const uint64_t scratch_capacity = scratch.capacity;
            for (uint64_t limit = 19; limit < 19 + (uint64_t)(end + 1) * 24; limit += 7)
            {
                scratch.capacity = limit;
                const CheckPrefixStatus status = check_prefix_make(&prefix, parser, &output, &scratch);
                check_test_expect(test, status == CHECK_PREFIX_RESOURCE && output.length == 33 && scratch.length == 19,
                                  "prefix DFS exhaustion rolls back output and scratch");
            }
            scratch.capacity = scratch_capacity;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &output) == CHECK_PREFIX_INVALID && output.length == 33,
                              "prefix rejects aliased output and scratch");
            const uint32_t parent = parser->tree.parents[0];
            parser->tree.parents[0] = parser->tokens.count;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_INVALID && output.length == 33,
                              "prefix rejects an outgoing parent across the cutoff");
            parser->tree.parents[0] = 0;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_INVALID,
                              "prefix rejects a self cycle");
            parser->tree.parents[0] = 1;
            const uint32_t second_parent = parser->tree.parents[1];
            parser->tree.parents[1] = 0;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_INVALID,
                              "prefix rejects a disconnected cycle");
            parser->tree.parents[1] = second_parent;
            parser->tree.parents[0] = parent;
            const uint32_t incoming = parser->tree.parents[end];
            parser->tree.parents[end] = 0;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_INVALID,
                              "prefix rejects an incoming edge from discarded tokens");
            parser->tree.parents[end] = incoming;
            const uint32_t split_parent = parser->tree.parents[3];
            parser->tree.parents[3] = parent;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_INVALID,
                              "prefix rejects interleaved subtree source intervals");
            parser->tree.parents[3] = split_parent;
            const uint32_t opener_parent = parser->tree.parents[2];
            parser->tree.parents[2] = parent;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_INVALID,
                              "prefix rejects an opener attached to the wrong closer");
            parser->tree.parents[2] = opener_parent;
            const uint32_t depth_parent = parser->tree.parents[1];
            const uint32_t depth = parser->tree.depths[depth_parent];
            parser->tree.depths[depth_parent] = parser->tree.depths[1] + 1;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_INVALID,
                              "prefix rejects inconsistent node depths");
            parser->tree.depths[depth_parent] = depth;
            const uint8_t role = parser->tree.roles[0];
            parser->tree.roles[0] = PARSE_ROLE_COUNT;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_INVALID,
                              "prefix rejects unknown roles before table lookup");
            parser->tree.roles[0] = role;
            parser->fatal = PARSE_MESSAGE_OUT_OF_MEMORY;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_UNAVAILABLE,
                              "prefix never reads a resource-failed parse as a complete tree");
            parser->fatal = PARSE_MESSAGE_NONE;
            const uint32_t errors = parser->errors;
            parser->errors = 0;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_UNAVAILABLE,
                              "prefix is used only for failed parses");
            parser->errors = errors;
            check_test_expect(test, check_prefix_make(&prefix, parser, &output, &scratch) == CHECK_PREFIX_READY && prefix.end == end,
                              "prefix remains usable after every failure");
        }
    }
    arena_release(&scratch);
    arena_release(&output);
}

// The parser oracle deliberately supplies -w. Warning-promotion controls need an unsuppressed invocation of the
// same installed compiler. These original fixtures are standalone and header-free; every error must name path.
static bool check_test_prefix_clang_warnings(const char* clang, const char* path, ParseTestClang* outcome)
{
    *outcome = (ParseTestClang){ 0 };
    int descriptors[2];
    bool valid = pipe(descriptors) == 0;
    pid_t child = -1;
    if (valid)
    {
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-nostdinc", "-fno-color-diagnostics",
                              "-fno-caret-diagnostics", "-ferror-limit=1", (char*)path, 0 };
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, descriptors[1], 2);
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addclose(&actions, descriptors[0]);
        posix_spawn_file_actions_addclose(&actions, descriptors[1]);
        valid = posix_spawnp(&child, clang, &actions, 0, arguments, environ) == 0;
        posix_spawn_file_actions_destroy(&actions);
        close(descriptors[1]);
        FILE* stream = valid ? fdopen(descriptors[0], "r") : 0;
        valid &= stream != 0;
        if (stream != 0)
        {
            char line[4096];
            const size_t length = strlen(path);
            while (fgets(line, sizeof(line), stream) != 0)
            {
                if (!outcome->rejected && strncmp(line, path, length) == 0 && strstr(line, ": error: ") != 0)
                {
                    outcome->rejected = sscanf(line + length, ":%u:%u:", &outcome->line, &outcome->column) == 2;
                }
            }
            fclose(stream);
        }
        else
        {
            close(descriptors[0]);
        }
        int status = 0;
        if (child >= 0)
        {
            valid &= waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                     (outcome->rejected ? WEXITSTATUS(status) == 1 : WEXITSTATUS(status) == 0);
        }
    }
    return valid;
}

static void check_test_prefix_fixture(CheckTest* test, const CheckPrefixFixture* fixture, const char* source, const char* path,
                                      const ParseTestClang* oracle, bool faults)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Preprocessor before;
    static Parser parser;
    static Parser reference;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool initialized = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) && pp_initialize(&before, &options, &identifiers);
    initialized = initialized && pp_run_source(&pp, path, (const uint8_t*)source, (uint32_t)strlen(source)) &&
                  pp_run_source(&before, "prefix-reference.c", (const uint8_t*)fixture->before, (uint32_t)strlen(fixture->before));
    check_test_expect(test, initialized, "prefix fixture initializes and preprocesses");
    uint32_t lines[2] = { 0 };
    for (uint32_t path_index = 0; initialized && path_index < 2; path_index += 1)
    {
        const ParseOptions parser_options = { .scalar = path_index == 0 };
        Arena output = arena_reserve(UINT64_C(1) << 20);
        Arena input_storage = arena_reserve(UINT64_C(1) << 24);
        Arena values = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        CheckTestTypes types = { 0 };
        bool ready = output.base != 0 && input_storage.base != 0 && values.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parser_options, options.language) && parse_initialize(&reference, &parser_options, options.language);
        check_test_expect(test, ready, "prefix fixture parser and checker storage initialize");
        if (ready)
        {
            const bool parsed = parse_translation_unit(&parser, &pp);
            const bool referenced = parse_translation_unit(&reference, &before);
            const ParseTestOutcome original = parse_test_outcome(&parser, &pp);
            check_test_expect(test, !parsed && parser.fatal == PARSE_MESSAGE_NONE && parser.postorder == 0 && referenced,
                              "prefix fixture has a recoverable parse failure and a complete earlier declaration forest");
            const uint32_t errors = parser.errors;
            const uint32_t diagnostics = parser.diagnostic_count;
            CheckPrefix prefix;
            const CheckPrefixStatus status = check_prefix_make(&prefix, &parser, &output, &scratch);
            const bool empty = reference.tokens.count == 0;
            char description[320];
            snprintf(description, sizeof(description), "%s: %s prefix status %u, end %u (reference %u)", fixture->name,
                     path_index == 0 ? "scalar" : "vector", status, prefix.end, reference.tokens.count);
            check_test_expect(test, status == (empty ? CHECK_PREFIX_EMPTY : CHECK_PREFIX_READY) && prefix.end == reference.tokens.count, description);
            lines[path_index] = original.line;
            if (status == CHECK_PREFIX_READY)
            {
                const Parser* view = &prefix.parser;
                const uint32_t count = view->tokens.count;
                bool nonowning = view->scratch.base == 0;
                for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(view->columns); column += 1)
                {
                    nonowning &= view->columns[column].base == 0;
                }
                check_test_expect(test, nonowning && view->errors == 0 && view->fatal == PARSE_MESSAGE_NONE && view->diagnostic_count == 0,
                                  "prefix view holds no parser arena ownership or copied failure state");
                bool same_roles = true;
                for (uint32_t token = 0; token < count; token += 1)
                {
                    same_roles &= parse_test_canonical_role(view->tree.roles[token]) == parse_test_canonical_role(reference.tree.roles[token]);
                }
                const bool same[] =
                {
                    memcmp(view->tokens.kinds, reference.tokens.kinds, count) == 0,
                    memcmp(view->tokens.names, reference.tokens.names, (uint64_t)count * sizeof(uint32_t)) == 0,
                    same_roles,
                    memcmp(view->tree.parents, reference.tree.parents, (uint64_t)count * sizeof(uint32_t)) == 0,
                    memcmp(view->tree.depths, reference.tree.depths, (uint64_t)count * sizeof(uint32_t)) == 0,
                    memcmp(view->postorder, reference.postorder, (uint64_t)count * sizeof(uint32_t)) == 0,
                    memcmp(view->sizes, reference.sizes, (uint64_t)count * sizeof(uint32_t)) == 0,
                };
                snprintf(description, sizeof(description), "%s: prefix and fresh parse match kinds/names/roles/parents/depths/order/sizes: %u%u%u%u%u%u%u",
                         fixture->name, same[0], same[1], same[2], same[3], same[4], same[5], same[6]);
                check_test_expect(test, same[0] && same[1] && same[2] && same[3] && same[4] && same[5] && same[6], description);
                check_test_expect(test, memcmp(view->tokens.sources, parser.tokens.sources, (uint64_t)count * sizeof(uint32_t)) == 0 &&
                                      memcmp(view->tokens.flags, parser.tokens.flags, count) == 0,
                                  "prefix preserves source indices and token flags");
                bool padded = true;
                for (int32_t i = 0; i < BUSTER_PARSE_TOKEN_PADDING; i += 1)
                {
                    const int64_t indices[2] = { -1 - i, (int64_t)count + i };
                    for (uint32_t side = 0; side < 2; side += 1)
                    {
                        const int64_t token = indices[side];
                        padded &= view->tokens.kinds[token] == PARSE_KIND_END && view->tokens.names[token] == 0 && view->tokens.sources[token] == 0 &&
                                  view->tokens.flags[token] == 0 && view->tree.roles[token] == 0 && view->tree.parents[token] == 0 &&
                                  view->tree.depths[token] == 0 && view->postorder[token] == 0 && view->sizes[token] == 0;
                    }
                }
                check_test_expect(test, padded, "prefix owns fully zeroed leading and trailing parser padding");
                CheckInput input;
                const bool adapted = check_input_prefix(&input, view, &pp, &types.table, &input_storage, &scratch);
                check_test_expect(test, !input.resource, "prefix checker does not exhaust its input storage");
                uint32_t first = BUSTER_CHECK_NONE;
                for (uint32_t i = 0; i < input.report_count; i += 1)
                {
                    first = input.reports[i].token < first ? input.reports[i].token : first;
                }
                if (adapted || input.diagnostic_graph)
                {
                    const CheckOptions check_options = { .scalar = path_index == 0 };
                    CheckStream stream;
                    const bool checked = check_graph_run(&types.table, &input.graph, &values, &scratch, &check_options, &stream) &&
                                         check_input_packed_finalize(&input, view, &types.table, &stream, &scratch);
                    check_test_expect(test, checked, "prefix graph is valid");
                    for (uint32_t i = 0; checked && i < stream.report_count; i += 1)
                    {
                        first = stream.reports[i].token < first ? stream.reports[i].token : first;
                    }
                    CheckInitDiagnostic initializer;
                    const bool diagnosed = checked && check_initializers_diagnose(&initializer, &input, &stream, view, &types.table,
                                                                                   BUSTER_CHECK_NONE, &output, &scratch);
                    check_test_expect(test, diagnosed && initializer.status != CHECK_INIT_RESOURCE, "prefix initializer diagnostic scan completes");
                    if (diagnosed && initializer.status != CHECK_INIT_OK)
                    {
                        first = initializer.token < first ? initializer.token : first;
                    }
                    uint32_t promoted = BUSTER_CHECK_NONE;
                    if (checked && check_diagnostic_first_error(&input, &stream, &promoted))
                    {
                        first = promoted < first ? promoted : first;
                    }
                }
                if (first != BUSTER_CHECK_NONE)
                {
                    check_test_expect(test, first < prefix.end, "prefix semantic diagnostic lies strictly before the excluded declaration");
                    lines[path_index] = check_test_source_line(&parser, &pp, first);
                }
                if (faults && path_index == 0)
                {
                    check_test_prefix_faults(test, &parser);
                }
            }
            snprintf(description, sizeof(description), "%s: first diagnostic line %u, Clang %u, expected %u", fixture->name,
                     lines[path_index], oracle->line, fixture->line);
            check_test_expect(test, oracle->rejected && oracle->line == fixture->line && lines[path_index] == oracle->line, description);
            check_test_expect(test, parser.errors == errors && parser.diagnostic_count == diagnostics && parser.postorder == 0,
                              "prefix checking preserves the original parse failure and diagnostics");
        }
        parse_release(&reference);
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&values);
        arena_release(&input_storage);
        arena_release(&output);
    }
    check_test_expect(test, lines[0] == lines[1], "prefix scalar and vector first diagnostics agree");
    pp_release(&before);
    pp_release(&pp);
    intern_release(&identifiers);
}

// Recovery erased even the earlier well-formed statement. Do not claim its semantic first-error line can be
// recovered from this forest; a separate diagnostic-only reparse would need its own soundness and ordering checks.
static void check_test_prefix_lost_body(CheckTest* test)
{
    static const char source[] = "int first(void)\n{\n    missing;\n    return 0\n}\n";
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                 pp_run_source(&pp, "prefix-lost-body.c", (const uint8_t*)source, sizeof(source) - 1);
    check_test_expect(test, ready, "erased-body prefix fixture initializes");
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const ParseOptions parse_options = { .scalar = path == 0 };
        Arena output = arena_reserve(UINT64_C(1) << 20);
        Arena scratch = arena_reserve(UINT64_C(1) << 20);
        const bool initialized = output.base != 0 && scratch.base != 0 && parse_initialize(&parser, &parse_options, options.language);
        check_test_expect(test, initialized, "erased-body prefix parser initializes");
        if (initialized)
        {
            const bool parsed = parse_translation_unit(&parser, &pp);
            bool erased = !parsed && parser.errors != 0;
            for (uint32_t token = 0; token < parser.tokens.count; token += 1)
            {
                erased &= parser.tree.roles[token] == PARSE_ROLE_ERROR && parser.tree.parents[token] == BUSTER_PARSE_ROOT;
            }
            check_test_expect(test, erased, "parser recovery erased the whole malformed body including its earlier semantic error");
            CheckPrefix prefix;
            check_test_expect(test, check_prefix_make(&prefix, &parser, &output, &scratch) == CHECK_PREFIX_EMPTY &&
                                  prefix.end == 0 && parser.errors != 0 && output.length == 0 && scratch.length == 0,
                              "prefix explicitly leaves same-declaration semantic ordering unavailable without accepting the original");
        }
        parse_release(&parser);
        arena_release(&scratch);
        arena_release(&output);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static __attribute__((noinline)) void check_test_prefixes(CheckTest* test, const char* clang)
{
    const CheckPrefixFixture fixtures[] =
    {
        {
            "earlier function undeclared identifier",
            "int first(void)\n{\n    return missing;\n}\n",
            "int later(void)\n{\n    return 0\n}\n",
            3,
        },
        {
            "typedef and tag survive before invalid member use",
            "typedef struct S { int member; } S;\nint first(S s)\n{\n    return s.absent;\n}\n",
            "int later = ;\n",
            4,
        },
        {
            "earlier conflicting declaration",
            "int name;\ndouble name;\n",
            "int later = ;\n",
            2,
        },
        {
            "earlier scalar called as function",
            "int object;\nint first(void)\n{\n    return object();\n}\n",
            "int later(void) { return 0 }\n",
            4,
        },
        {
            "earlier operand error precedes later binding and syntax errors",
            "int first(void) { return 1(2); }\nint second(void) { return missing; }\n",
            "int later = ;\n",
            1,
        },
        {
            "unresolved unary operand cannot invent an earlier operand error",
            "int first(void)\n{\n    return *\n        missing;\n}\n",
            "int later = ;\n",
            4,
        },
        {
            "unresolved binary operand cannot invent an earlier operand error",
            "int first(void)\n{\n    return 1 +\n        missing;\n}\n",
            "int later = ;\n",
            4,
        },
        {
            "unresolved call argument prevents a synthetic earlier call error",
            "int first(void)\n{\n    return 1(\n        missing);\n}\n",
            "int later = ;\n",
            4,
        },
        {
            "earlier static assertion",
            "typedef int I;\n_Static_assert(sizeof(I) == 8, \"wrong size\");\n",
            "int later[;\n",
            2,
        },
        {
            "nested statement intervals preserve earlier operand error",
            "int first(int n)\n{\n    if (n) { while (n) { n--; } }\n    else { for (int i = 0; i < n; i++) { n += i; } }\n    return absent;\n}\n",
            "int later(void) { return 0 }\n",
            5,
        },
        {
            "earlier incompatible return",
            "struct S { int member; };\nstruct S first(void)\n{\n    return 1;\n}\n",
            "int later = ;\n",
            4,
        },
        {
            "earlier invalid pointer assignment",
            "int first(void)\n{\n    int *p;\n    p = 1.0;\n    return 0;\n}\n",
            "int later = ;\n",
            4,
        },
        {
            "earlier file initializer use",
            "int first = absent;\n",
            "int later = ;\n",
            1,
        },
        {
            "earlier static initializer is not constant",
            "int get(void);\nstatic int value=get();\n",
            "int later = ;\n",
            2,
        },
        {
            "earlier static initializer precedes binding and syntax errors",
            "int get(void);\nstatic int value=get();\nint missing_use=missing;\n",
            "int later = ;\n",
            2,
        },
        {
            "promoted original enum warning precedes malformed first body",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { N=2147483647+1 };\n",
            "int f(void){\nint broken=;\n}\n",
            2,
        },
        {
            "valid prefix leaves original parse diagnostic first",
            "typedef int I;\nstruct S { I value; };\nint first(struct S s) { return s.value; }\n",
            "int later = ;\n",
            4,
        },
        {
            "tentative record completed after malformed declaration is not prematurely rejected",
            "struct S;\nstruct S object;\n",
            "int malformed = ;\nstruct S { int member; };\n",
            3,
        },
        {
            "tentative array completed after malformed declaration is not prematurely rejected",
            "int object[];\n",
            "int malformed = ;\nint object[4];\n",
            2,
        },
        {
            "operation-site incomplete type remains an error in prefix mode",
            "struct S;\nint first(void) { return sizeof(struct S); }\n",
            "int malformed = ;\nstruct S { int member; };\n",
            2,
        },
        {
            "malformed first declaration has no prefix",
            "",
            "int later(void)\n{\n    return 0\n}\n",
            3,
        },
        {
            "earlier parse error is not displaced by a later semantic error",
            "",
            "int malformed = ;\nint later = absent;\n",
            1,
        },
        {
            "nonrecovering unknown type diagnostic cuts at declaration boundary",
            "int first(void) { return absent; }\n",
            "UnknownType later;\n",
            1,
        },
        {
            "first syntax error prevents crossing into later declarations",
            "int first(void) { return absent; }\n",
            "int malformed = ;\nint valid;\nint also_bad = ;\n",
            1,
        },
        {
            "pragma after prefix is excluded",
            "int first(void) { return absent; }\n",
            "#pragma pack(1)\nint later = ;\n",
            1,
        },
        {
            "omitted final member semicolon is a warning before a later error",
            "struct S { int member };\nint first(void) { return absent; }\n",
            "int later = ;\n",
            2,
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-prefix-XXXXXX";
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    char sources[FILE_COUNT][4096];
    ParseTestClang outcomes[FILE_COUNT];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        const int length = snprintf(sources[i], sizeof(sources[i]), "%s%s", fixtures[i].before, fixtures[i].after);
        ready = length >= 0 && (size_t)length < sizeof(sources[i]) && parse_test_write(paths[i], sources[i], (size_t)length);
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "prefix first-error Clang oracle runs");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        const bool warnings = strstr(sources[i], "#pragma clang diagnostic") != 0;
        const bool oracle = !warnings || check_test_prefix_clang_warnings(clang, paths[i], outcomes + i);
        check_test_expect(test, oracle, "prefix warning-promotion Clang oracle runs without suppression");
        if (oracle)
        {
            check_test_prefix_fixture(test, fixtures + i, sources[i], paths[i], outcomes + i, i == 0);
        }
    }
    check_test_prefix_lost_body(test);
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

#endif
