// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original variable-array source fixtures; both parser/checker paths use installed Clang as an external oracle.

#ifndef BUSTER_C_CHECK_VLA_TEST_INCLUDED
#define BUSTER_C_CHECK_VLA_TEST_INCLUDED

static void check_test_vla_metadata(CheckTest* test)
{
    static const char source[] =
        "void proto(int n,int a[n]); unsigned long f(int n,int parameter[n]) { int a[n++][n+1]; typedef int Row[n+2]; Row b; int (*p)[n+3]; "
        "typedef __typeof__(int[n+5]) Query; Query q; __typeof__(int[n+6])*r; "
        "return sizeof a + sizeof b + sizeof *p + sizeof(int[n+4]) + sizeof q + sizeof *r; }\n";
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        const PPOptions options =
        {
            .language =
            {
                .standard = 2017,
                .gnu = true,
            },
            .no_standard_includes = true,
        };
        const ParseOptions parse_options = { .scalar = scalar != 0 };
        CheckTestTypes types = { 0 };
        Arena storage = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        bool ready = storage.base != 0 && scratch.base != 0 && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                     parse_initialize(&parser, &parse_options, options.language) && check_test_types_open(&types) &&
                     pp_run_source(&pp, "vla-metadata.c", (const uint8_t*)source, sizeof(source) - 1);
        CheckInput input = { 0 };
        if (ready)
        {
            parse_translation_unit(&parser, &pp);
            ready = parser.errors == 0 && parser.fatal == PARSE_MESSAGE_NONE && check_input(&input, &parser, &pp, &types.table, &storage, &scratch);
        }
        check_test_expect(test, ready, "VLA metadata source adaptation");
        if (ready)
        {
            check_test_expect(test, input.vla_count == 9, "one VLA identity per dynamic dimension without repeated typedef bounds");
            uint32_t inline_bounds = 0;
            uint32_t unevaluated = 0;
            uint32_t reads = 0;
            uint32_t sizes = 0;
            for (uint32_t i = 0; i < input.vla_count; i += 1)
            {
                const CheckInputVla* vla = &input.vlas[i];
                check_test_expect(test, (vla->evaluated ? vla->body < parser.tokens.count : vla->body == BUSTER_CHECK_NONE) && vla->bound_begin < vla->bound_end &&
                                  vla->bound_node < input.graph.count && input.graph.ops[vla->bound_node] == CHECK_OP_VLA_BOUND &&
                                  input.graph.members[vla->bound_node] == i + 1, "evaluated VLA bound has one explicit capture and exact identity");
                check_test_expect(test, type_kind(&types.table, vla->type) == BUSTER_TYPE_KIND_VARIABLE_ARRAY &&
                                  types.table.types.extent[type_index(vla->type)] == (uint64_t)vla->bound_begin + 1,
                                  "variable dimension type retains its source bound identity");
                inline_bounds += vla->declaration == BUSTER_CHECK_NONE;
                unevaluated += !vla->evaluated;
            }
            for (uint32_t node = 0; node < input.graph.count; node += 1)
            {
                reads += input.graph.ops[node] == CHECK_OP_VLA_EXTENT;
                sizes += input.graph.ops[node] == CHECK_OP_SIZEOF_VLA;
            }
            check_test_expect(test, unevaluated == 1, "prototype bounds are unevaluated and definition parameter bounds are activated");
            check_test_expect(test, inline_bounds == 1 && reads == 6 && sizes == 6, "sizeof reuses saved declaration and typeof extents and evaluates only its new inline bound");
        }
        check_test_types_close(&types);
        arena_release(&storage);
        arena_release(&scratch);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

// The issue #611 shapes exercise the complete source reader, with exact owners and captures at every size. Their
// measured driver instruction growth lives in buster-performance; this correctness check has no timing threshold.
static void check_test_vla_scaling(CheckTest* test)
{
    static const uint32_t counts[] = { 2000, 4000, 8000, 16000, 32000 };
    for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(counts); sample += 1)
    {
        const uint32_t count = counts[sample];
        const uint32_t capacity = count * 32 + 128;
        char* source = malloc(capacity);
        bool written = source != 0;
        uint32_t length = written ? (uint32_t)snprintf(source, capacity, "int f(int n) {\n") : 0;
        for (uint32_t i = 0; written && i < count; i += 1)
        {
            const int added = snprintf(source + length, capacity - length, "typedef int A%u[n];\n", i);
            written = added > 0 && (uint32_t)added < capacity - length;
            length += written ? (uint32_t)added : 0;
        }
        if (written)
        {
            const int added = snprintf(source + length, capacity - length, "return n; }\n");
            written = added > 0 && (uint32_t)added < capacity - length;
            length += written ? (uint32_t)added : 0;
        }
        check_test_expect(test, written, "VLA scaling source allocation and generation");
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            static InternTable identifiers;
            static Preprocessor pp;
            static Parser parser;
            const PPOptions options =
            {
                .language = { .standard = 2017, .gnu = true },
                .no_standard_includes = true,
            };
            const ParseOptions parse_options = { .scalar = scalar != 0 };
            const TypeCapacity type_capacity =
            {
                .types = count + 4096,
                .parameters = 8192,
                .tags = 512,
                .members = 4096,
            };
            CheckTestTypes types = { 0 };
            types.arena = arena_reserve(type_capacity_bytes(type_capacity));
            types.index = arena_reserve(type_index_capacity_bytes(type_capacity));
            Arena storage = arena_reserve(UINT64_C(1) << 30);
            Arena scratch = arena_reserve(UINT64_C(1) << 30);
            bool ready = storage.base != 0 && scratch.base != 0 && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                         parse_initialize(&parser, &parse_options, options.language) && type_initialize(&types.table, &types.arena, &types.index, type_capacity) &&
                         pp_run_source(&pp, "vla-scaling.c", (const uint8_t*)source, length);
            CheckInput input = { 0 };
            if (ready)
            {
                parse_translation_unit(&parser, &pp);
                ready = parser.errors == 0 && parser.fatal == PARSE_MESSAGE_NONE && check_input(&input, &parser, &pp, &types.table, &storage, &scratch);
            }
            char description[128];
            snprintf(description, sizeof(description), "VLA scaling %u declarations, %s reader", count, scalar ? "scalar" : "vector");
            check_test_expect(test, ready && input.vla_count == count, description);
            bool exact = ready && input.vla_count == count;
            for (uint32_t i = 0; exact && i < count; i += 1)
            {
                const CheckInputVla* vla = input.vlas + i;
                char name[24];
                const uint32_t name_length = (uint32_t)snprintf(name, sizeof(name), "A%u", i);
                const uint32_t identifier = vla->declaration < parser.tokens.count ? parser.tokens.names[vla->declaration] : 0;
                exact = identifier != 0 && intern_length(&identifiers, identifier) == name_length &&
                        memcmp(intern_bytes(&identifiers, identifier), name, name_length) == 0 && vla->evaluated &&
                        vla->body < parser.tokens.count && parser.tree.roles[vla->body] == PARSE_ROLE_OPEN_BODY &&
                        vla->scope_begin == vla->declaration + 4 && vla->scope_end > vla->scope_begin &&
                        vla->bound_node < input.graph.count && input.graph.ops[vla->bound_node] == CHECK_OP_VLA_BOUND && input.graph.members[vla->bound_node] == i + 1;
            }
            check_test_expect(test, exact, "VLA scaling retains every declaration's owner, scope and unique bound capture");
            check_test_types_close(&types);
            arena_release(&storage);
            arena_release(&scratch);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
        free(source);
    }
}

static __attribute__((noinline)) void check_test_vla_sources(CheckTest* test, const char* clang)
{
    char repeated_size[8192];
    uint32_t length = (uint32_t)snprintf(repeated_size, sizeof(repeated_size), "unsigned long f(int n) { typedef char A");
    for (uint32_t dimension = 0; dimension < 24; dimension += 1)
    {
        length += (uint32_t)snprintf(repeated_size + length, sizeof(repeated_size) - length, "[n+%u]", dimension);
    }
    length += (uint32_t)snprintf(repeated_size + length, sizeof(repeated_size) - length, "; A a; return ");
    for (uint32_t query = 0; query < 128; query += 1)
    {
        length += (uint32_t)snprintf(repeated_size + length, sizeof(repeated_size) - length, "%ssizeof a", query != 0 ? "+" : "");
    }
    snprintf(repeated_size + length, sizeof(repeated_size) - length, "; }\n");
    CheckTestFixtures catalog;
    const CheckTestBoundSource bindings[] = {
        { "@repeated-size", repeated_size },
    };
    const uint32_t file_count = check_test_private_catalog(test, &catalog, "checker/vla-sources", bindings, BUSTER_ARRAY_LENGTH(bindings), 0);
    const CheckSourceFixture* fixtures = catalog.cases;
    CheckTestOracleBatch batch;
    const bool allocated = check_test_oracle_batch(test, &batch, &catalog.storage, file_count);
    char directory[] = "/tmp/buster-checker-vla-XXXXXX";
    const bool created = allocated && mkdtemp(directory) != NULL;
    bool ready = created;
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        batch.pointers[i] = batch.paths[i];
        ready = parse_test_write(batch.paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), batch.pointers, file_count, batch.outcomes);
    check_test_expect(test, ready, "variable array Clang oracle could not run");
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixtures[i].name);
        check_test_expect(test, batch.outcomes[i].rejected == fixtures[i].rejected, description);
        check_test_source(test, &fixtures[i], batch.paths[i], &batch.outcomes[i]);
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
    check_test_vla_metadata(test);
    check_test_vla_scaling(test);
}

#endif
