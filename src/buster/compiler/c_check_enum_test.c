// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original enumeration fixtures. Clang is an external oracle for provisional types, completed types and values;
// direct metadata checks separately retain the source-position distinction in both parser/checker paths.
#ifndef BUSTER_C_CHECK_ENUM_TEST_INCLUDED
#define BUSTER_C_CHECK_ENUM_TEST_INCLUDED

static void check_test_enum_metadata(CheckTest* test)
{
    static const char source[] =
        "enum E { Start=2147483647, Wide, Provisional=_Generic(Wide,long:2,default:9) }; "
        "unsigned long result(void){return Wide+Provisional;}\n"
        "enum Overflow { Maximum=0xffffffffffffffffULL, Wrapped };\n"
        "enum Mixed { Positive=0xffffffffffffffffULL, Negative=-1 };\n"
        "enum Huge { High=(unsigned __int128)1<<100, Low=High>>100 };\n";
    for (uint32_t variant = 0; variant < 4; variant += 1)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        const bool c23 = (variant & 2) != 0;
        const PPOptions options =
        {
            .language = { .standard = c23 ? 2023 : 2017, .gnu = true },
            .no_standard_includes = true,
        };
        const ParseOptions parse_options = { .scalar = (variant & 1) != 0 };
        CheckTestTypes types = { 0 };
        Arena storage = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        Arena checked_storage = arena_reserve(UINT64_C(1) << 24);
        bool ready = storage.base != 0 && scratch.base != 0 && checked_storage.base != 0 && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                     parse_initialize(&parser, &parse_options, options.language) && check_test_types_open(&types) &&
                     pp_run_source(&pp, "enum-metadata.c", (const uint8_t*)source, sizeof(source) - 1);
        CheckInput input = { 0 };
        if (ready)
        {
            parse_translation_unit(&parser, &pp);
            ready = parser.errors == 0 && parser.fatal == PARSE_MESSAGE_NONE && check_input(&input, &parser, &pp, &types.table, &storage, &scratch);
        }
        check_test_expect(test, ready && !input.resource, "wide enum metadata source adaptation");
        if (ready)
        {
            CheckStream stream = { 0 };
            const CheckOptions check_options = { 0 };
            check_test_expect(test, check_graph_run(&types.table, &input.graph, &checked_storage, &scratch, &check_options, &stream) && stream.report_count == 0,
                              "C17 and C23 enum graph semantics accept both completed type forms");
            uint32_t declaration = BUSTER_CHECK_NONE;
            uint32_t uses = 0;
            uint32_t values = 0;
            uint32_t warnings = 0;
            for (uint32_t token = 0; token < parser.tokens.count; token += 1)
            {
                const uint8_t* spelling = 0;
                uint32_t length = 0;
                const bool identifier = parser.tokens.kinds[token] == PARSE_KIND_IDENTIFIER;
                if (identifier)
                {
                    uint8_t buffer[32];
                    const uint32_t source = parse_source_index(&parser.tokens, token);
                    length = pp_spelling(&pp, pp.output.kinds[source], pp.output.values[source], pp.output.locations[source], buffer, &spelling);
                }
                if ((input.constant_warnings[token] & CHECK_WARNING_ENUM_TOO_LARGE) != 0)
                {
                    const uint32_t line = check_test_source_line(&parser, &pp, token);
                    const bool successor = identifier && length == 7 && memcmp(spelling, "Wrapped", 7) == 0;
                    check_test_expect(test, warnings == 0 ? successor && line == 2 : parser.tokens.kinds[token] == PARSE_KIND_ENUM && line == warnings + 2,
                                      "enum range warning retains the overflowing successor or completed declaration source token");
                    warnings += 1;
                }
                if (identifier && length == 4 && memcmp(spelling, "Wide", 4) == 0)
                {
                    const TypeId type = input.declared_types[token];
                    if (input.declarations[token] == token)
                    {
                        declaration = token;
                        check_test_expect(test, c23 ? type_kind(&types.table, type) == BUSTER_TYPE_KIND_ENUM &&
                                          types.table.types.base[type_index(type)] == BUSTER_TYPE_UINT : type == BUSTER_TYPE_UINT,
                                          "completed wide enumerator uses its language-mode common type");
                    }
                    else
                    {
                        check_test_expect(test, declaration != BUSTER_CHECK_NONE && input.declarations[token] == declaration,
                                          "provisional and completed enum uses retain the same exact declaration identity");
                        check_test_expect(test, uses == 0 ? type == BUSTER_TYPE_LONG : type == input.declared_types[declaration],
                                          "enum use type records its source position before or after completion");
                        uses += 1;
                    }
                    const uint32_t node = input.token_nodes[token];
                    if (node != BUSTER_CHECK_NONE)
                    {
                        const CheckValue value = input.graph.ops[node] == CHECK_OP_VALUE ? check_graph_leaf_value(&input.graph, node) : (CheckValue){ 0 };
                        check_test_expect(test, value.constant_kind == CHECK_CONSTANT_INTEGER && value.integer == UINT32_C(0x80000000),
                                          "ordinary graph enum leaves preserve their full value bits");
                        values += 1;
                    }
                }
            }
            check_test_expect(test, uses == 2 && values == 1, "metadata fixture exercises provisional query and completed graph use");
            check_test_expect(test, warnings == 3, "enum range warnings distinguish successor overflow and incompatible completed ranges");
        }
        check_test_types_close(&types);
        arena_release(&storage);
        arena_release(&scratch);
        arena_release(&checked_storage);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

static __attribute__((noinline)) void check_test_enum_sources(CheckTest* test, const char* clang)
{
    char nested_records[8192];
    uint32_t length = (uint32_t)snprintf(nested_records, sizeof(nested_records), "enum { N0=sizeof(struct {");
    for (uint32_t i = 1; i <= 48; i += 1)
    {
        length += (uint32_t)snprintf(nested_records + length, sizeof(nested_records) - length, "enum { N%u=sizeof(struct {", i);
    }
    length += (uint32_t)snprintf(nested_records + length, sizeof(nested_records) - length, "int x;");
    for (uint32_t i = 48; i != 0; i -= 1)
    {
        length += (uint32_t)snprintf(nested_records + length, sizeof(nested_records) - length, "})} e%u; char p[N%u];", i, i);
    }
    snprintf(nested_records + length, sizeof(nested_records) - length, "})}; _Static_assert(N0==196, \"nested enum record jobs\");\n");
    char many_values[8192];
    length = (uint32_t)snprintf(many_values, sizeof(many_values), "enum E { E0=((unsigned __int128)1<<100)|7");
    for (uint32_t i = 1; i < 512; i += 1)
    {
        length += (uint32_t)snprintf(many_values + length, sizeof(many_values) - length, ",E%u", i);
    }
    snprintf(many_values + length, sizeof(many_values) - length,
             ",Proof=(E511>>100)==1 }; _Static_assert(E0==7 && E511==518 && Proof==1,\"retained wide chain\");\n");
    CheckTestFixtures catalog;
    const CheckTestBoundSource bindings[] = {
        { "@nested-records", nested_records },
        { "@many-values", many_values },
    };
    const uint32_t file_count = check_test_private_catalog(test, &catalog, "checker/enum-sources", bindings, BUSTER_ARRAY_LENGTH(bindings), 0);
    const CheckSourceFixture* fixtures = catalog.cases;
    char directory[] = "/tmp/buster-checker-enum-XXXXXX";
    const bool directory_ready = mkdtemp(directory) != NULL;
    char path[160];
    char output_path[160];
    char error_path[160];
    snprintf(path, sizeof(path), "%s/enum.c", directory);
    snprintf(output_path, sizeof(output_path), "%s/out", directory);
    snprintf(error_path, sizeof(error_path), "%s/err", directory);
    char* paths[] = { path };
    const char* arguments[] = { "-std=gnu17" };
    for (uint32_t i = 0; i < file_count; i += 1)
    {
        const CheckSourceFixture* fixture = fixtures + i;
        ParseTestClang outcome = { 0 };
        const bool oracle = directory_ready && parse_test_write(path, fixture->source, strlen(fixture->source)) &&
                            parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome);
        check_test_expect(test, oracle && outcome.rejected == fixture->rejected, fixture->name);
        if (oracle)
        {
            check_test_source(test, fixture, path, &outcome);
        }
    }
    arena_release(&catalog.storage);
    const char warning_source[] = "enum E { A=0xffffffffffffffffULL, B };\n";
    char* warning_arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-Werror=enum-too-large", path, NULL };
    const bool warning_oracle = directory_ready && parse_test_write(path, warning_source, sizeof(warning_source) - 1) &&
                                object_test_run(warning_arguments, output_path, error_path) != 0;
    Arena diagnostic_storage = arena_reserve(32768);
    const String8 diagnostic = warning_oracle ? object_test_read_file(&diagnostic_storage, error_path) : (String8){ 0 };
    check_test_expect(test, warning_oracle && diagnostic.pointer != NULL &&
                      memmem(diagnostic.pointer, diagnostic.length, "-Wenum-too-large", sizeof("-Wenum-too-large") - 1) != NULL,
                      "Clang independently confirms enum-too-large warning provenance");
    arena_release(&diagnostic_storage);
    if (directory_ready)
    {
        unlink(path);
        unlink(output_path);
        unlink(error_path);
        rmdir(directory);
    }
    check_test_enum_metadata(test);
}

#endif
