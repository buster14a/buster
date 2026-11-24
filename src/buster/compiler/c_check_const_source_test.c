// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original const-object sources, with outcomes established by the issue's Clang 23.1.4 survey.
// Include after c_check_program_test.c; both source and native tests reuse the existing test machinery.
#ifndef BUSTER_C_CHECK_CONST_SOURCE_TEST_INCLUDED
#define BUSTER_C_CHECK_CONST_SOURCE_TEST_INCLUDED

// The sparse array fixture's text, and the text clang checks in its place, of its outcome: the same extent, a designated
// element and a read through the zero fill. Clang's constant evaluator materializes a designated array's elements up
// to the designator, 134 million values for the fixture's: 67 s, 48 s of it the kernel's page faults, alone on a 4-CPU
// VM, where [65536] takes 42 ms. check_test_const_sources writes the oracle's text for clang (the fixture is known by
// its source's address), and this checker checks the fixture's own, which check_test_source reads from the fixture.
// They are kept here rather than in CheckSourceFixture, whose other tables' writers write each fixture's source.
static const char check_const_sparse_source[] = "const int a[268435456]={[134217728]=7};enum{E=a[134217728],Z=a[268435455]};_Static_assert(E==7 && Z==0,\"sparse\");\n";
static const char check_const_sparse_oracle[] = "const int a[268435456]={[65536]=7};enum{E=a[65536],Z=a[268435455]};_Static_assert(E==7 && Z==0,\"sparse\");\n";



static void check_const_source_atomic_runtime(CheckTest* test)
{
    static const char source[] =
        "const _Atomic(int) az=3;struct P{_Atomic(int) x;};const struct P p={4};\n"
        "enum{A=az,B=p.x};int g=az;int h=p.x;\n"
        "_Static_assert(az==3 && p.x==4,\"source constant atomic reads\");\n"
        "int f(void){return az;}int m(void){return p.x;}\n";
    for (uint32_t path = 0; path < 2; path += 1)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parse_options = { .scalar = path == 0 };
        const CheckOptions check_options = { .verify = true };
        CheckTestTypes types = { 0 };
        Arena input_storage = arena_reserve(UINT64_C(1) << 24);
        Arena output_storage = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        bool ready = input_storage.base != NULL && output_storage.base != NULL && scratch.base != NULL &&
                     intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                     parse_initialize(&parser, &parse_options, pp_options.language) && check_test_types_open(&types) &&
                     pp_run_source(&pp, "const-atomic-runtime.c", (const uint8_t*)source, sizeof(source) - 1) &&
                     parse_translation_unit(&parser, &pp);
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers initializers = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &input_storage, &scratch) &&
                check_graph_run(&types.table, &input.graph, &output_storage, &scratch, &check_options, &stream) && stream.report_count == 0 &&
                check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &output_storage, &scratch);
        check_test_expect(test, ready && !input.resource && stream.constant_evaluated != NULL,
                          "const atomic source contexts accept while retaining an explicit graph evaluation closure");
        uint32_t returns = 0;
        for (uint32_t node = 0; ready && node < stream.count; node += 1)
        {
            const uint32_t token = input.graph.tokens[node];
            if (input.graph.ops[node] == CHECK_OP_CONVERT && token < parser.tokens.count && parser.tokens.kinds[token] == PARSE_KIND_RETURN)
            {
                const uint32_t operand = input.graph.operand0[node];
                const bool runtime = stream.constant_evaluated != NULL && stream.constant_evaluated[node] == 0 &&
                                     operand < node && stream.constant_evaluated[operand] == 0;
                check_test_expect(test, runtime && (stream.conversions[node].operands[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0 &&
                                       (check_stream_flags(&stream, node) & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                                       check_stream_constant_kind(&stream, node) == CHECK_CONSTANT_NONE,
                                  "ordinary const atomic scalar/member returns retain observable lvalue loads outside the constant closure");
                returns += 1;
            }
        }
        check_test_expect(test, ready && returns == 2, "atomic runtime control exercises both a scalar root and a const record member");
        arena_release(&scratch);
        arena_release(&output_storage);
        arena_release(&input_storage);
        check_test_types_close(&types);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

static void check_test_const_sources(CheckTest* test, const char* clang)
{
    check_const_source_atomic_runtime(test);
    CheckTestFixtures catalog;
    const CheckTestBoundSource bindings[] = {
        { "@check-const-sparse-source", check_const_sparse_source },
    };
    const uint32_t file_count = check_test_private_catalog(test, &catalog, "checker/const-objects", bindings, BUSTER_ARRAY_LENGTH(bindings), 0);
    const CheckSourceFixture* fixtures = catalog.cases;
    CheckTestOracleBatch batch;
    const bool allocated = check_test_oracle_batch(test, &batch, &catalog.storage, file_count);
    char directory[] = "/tmp/buster-checker-const-XXXXXX";
    const bool created = allocated && mkdtemp(directory) != NULL;
    bool ready = created;
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        batch.pointers[i] = batch.paths[i];
        const char* text = fixtures[i].source == check_const_sparse_source ? check_const_sparse_oracle : fixtures[i].source;
        ready = parse_test_write(batch.paths[i], text, strlen(text));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), batch.pointers, file_count, batch.outcomes);
    check_test_expect(test, ready, "const-object source Clang oracle is available");
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        char description[224];
        snprintf(description, sizeof(description), "%s: recorded GNU17 acceptance agrees with Clang", fixtures[i].name);
        check_test_expect(test, batch.outcomes[i].rejected == fixtures[i].rejected, description);
        check_test_source(test, fixtures + i, batch.paths[i], batch.outcomes + i);
    }
    for (uint32_t i = 0; created && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        unlink(batch.paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
    arena_release(&catalog.storage);
}

static void check_test_const_programs(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/const-programs", clang);
}

#endif
