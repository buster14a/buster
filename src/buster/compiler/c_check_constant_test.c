// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original declaration-time expression fixtures, checked by both parser/checker paths and installed Clang.
// The FILE-sized arithmetic is authored from the target sizes; no system header or external fixture is copied.

static void check_test_program_suite(CheckTest* test, const char* suite, const char* clang);

static void check_test_constant_parameter_names(CheckTest* test)
{
    enum { COUNT = 1024 };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t bytes = COUNT * sizeof(CheckConstantParameter);
    const uint64_t readable = (bytes + page - 1) / page * page;
    uint8_t* mapping = mmap(0, readable + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + readable, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "prototype name tree guarded records");
    if (ready)
    {
        CheckConstantParameter* parameters = (CheckConstantParameter*)(mapping + readable - bytes);
        CheckConstantPart functions[2] = { 0 };
        uint8_t owners[COUNT];
        for (uint32_t i = 0; i < COUNT - 2; i += 1)
        {
            // The first 33 names force every bit decision, including bit 31; later names mix duplicates,
            // unnamed parameters and two interleaved prototype scopes. The independent oracle scans only here.
            const uint32_t name = i < 32 ? ~(UINT32_C(1) << i) : i == 32 ? UINT32_MAX : (i * 73) % 193;
            owners[i] = i < 33 ? 0 : i & 1;
            parameters[i] = (CheckConstantParameter){ .name = name };
            bool expected = true;
            for (uint32_t previous = 0; previous < i; previous += 1)
            {
                expected &= name == 0 || owners[previous] != owners[i] || parameters[previous].name != name;
            }
            const bool unique = check_constant_parameter_unique(&functions[owners[i]], parameters, i);
            check_test_expect(test, unique == expected, "prototype name tree differs from independent membership");
        }
        parameters[COUNT - 1] = (CheckConstantParameter){ .name = UINT32_MAX };
        check_test_expect(test, !check_constant_parameter_unique(&functions[0], parameters, COUNT - 1), "prototype name tree full-depth duplicate");
        CheckConstantPart empty = { 0 };
        parameters[COUNT - 1] = (CheckConstantParameter){ 0 };
        check_test_expect(test, check_constant_parameter_unique(&empty, parameters, COUNT - 1) && empty.parameter_names == 0,
                          "unnamed parameter leaves prototype tree empty");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, readable + page);
    }
}

static __attribute__((noinline)) void check_test_constant_parameter_sources(CheckTest* test, const char* clang)
{
    const uint32_t counts[] = {
        0,
        1,
        15,
        16,
        17,
        31,
        32,
        33,
        256,
        4096,
    };
    char directory[] = "/tmp/buster-checker-parameters-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    check_test_expect(test, created, "prototype scaling fixtures' directory");
    for (uint32_t c = 0; created && c < BUSTER_ARRAY_LENGTH(counts); c += 1)
    {
        const uint32_t count = counts[c];
        for (uint32_t form = 0; form < (count >= 256 ? 4u : 2u); form += 1)
        {
            Arena text = arena_reserve((uint64_t)count * 32 + 256);
            const uint64_t capacity = (uint64_t)count * 32 + 256;
            char* source = arena_push(&text, capacity, 1);
            uint64_t length = 0;
            if (source != 0)
            {
                length = (uint64_t)snprintf(source, capacity, "enum { A=sizeof(int (*)(");
                for (uint32_t i = 0; i < count; i += 1)
                {
                    const uint32_t name = form == 2 && i == 1 ? 0 : form == 3 && i + 1 == count ? 0 : i;
                    length += form == 1 ? (uint64_t)snprintf(source + length, capacity - length, "%sint", i == 0 ? "" : ",") :
                                          (uint64_t)snprintf(source + length, capacity - length, "%sint p%u", i == 0 ? "" : ",", name);
                }
                length += (uint64_t)snprintf(source + length, capacity - length, "%s)) }; _Static_assert(A==8,\"prototype pointer\");\n", count == 0 ? "void" : "");
            }
            char path[160];
            snprintf(path, sizeof(path), "%s/%u-%u.c", directory, count, form);
            char* paths[] = { path };
            const char* arguments[] = { "-std=gnu17" };
            ParseTestClang outcome = { 0 };
            const bool ready = source != 0 && length < capacity && parse_test_write(path, source, length) &&
                               parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome);
            check_test_expect(test, ready && outcome.rejected == (form >= 2), "prototype scaling fixture Clang expectation");
            if (ready)
            {
                const CheckSourceFixture fixture = {
                    .name = "generated prototype parameter names",
                    .source = source,
                    .rejected = form >= 2,
                };
                check_test_source(test, &fixture, path, &outcome);
            }
            unlink(path);
            arena_release(&text);
        }
    }
    if (created)
    {
        rmdir(directory);
    }
}

static __attribute__((noinline)) void check_test_constant_sources(CheckTest* test, const char* clang)
{
    check_test_constant_parameter_names(test);
    check_test_constant_parameter_sources(test, clang);
    // Clang 23 tests only the low 64 bits of this GNU condition; GCC and the full-width C integer truth rule
    // select 7. Keep the original counterexample as a direct checker regression and an explicit oracle probe.
    const char* wide_truth_source = "enum { N=__builtin_choose_expr(((__int128)1<<100),7,0) }; _Static_assert(N==7, \"full-width truth\");\n";
    char nested_selections[4][8192];
    for (uint32_t shape = 0; shape < BUSTER_ARRAY_LENGTH(nested_selections); shape += 1)
    {
        // Clang's ICE validation stalls on deep choose-expression enums; runtime syntax is a bounded oracle.
        uint32_t used = (uint32_t)snprintf(nested_selections[shape], sizeof(nested_selections[shape]), shape < 2 ? "int f(void){return " : "enum { N=");
        const char* opening = shape == 0 ? "__builtin_choose_expr(1," : shape == 1 ? "__builtin_choose_expr(0," :
                              shape == 2 ? "_Generic(0,int:" : "_Generic(0,default:";
        const char* closing = shape < 2 ? ",0)" : shape == 2 ? ",default:0)" : ",int:0)";
        for (uint32_t depth = 0; depth < 128; depth += 1)
        {
            used += (uint32_t)snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used, "%s", opening);
        }
        used += (uint32_t)snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used, "7");
        for (uint32_t depth = 0; depth < 128; depth += 1)
        {
            used += (uint32_t)snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used, "%s", closing);
        }
        if (shape < 2)
        {
            snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used, ";}\n");
        }
        else
        {
            snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used,
                     " }; _Static_assert(N==%u, \"nested selection\");\n", shape % 2 == 0 ? 7u : 0u);
        }
    }
    char nested_source[4096];
    uint32_t length = (uint32_t)snprintf(nested_source, sizeof(nested_source), "enum { N=");
    for (uint32_t i = 0; i < 96; i += 1)
    {
        length += (uint32_t)snprintf(nested_source + length, sizeof(nested_source) - length, "sizeof(char[");
    }
    length += (uint32_t)snprintf(nested_source + length, sizeof(nested_source) - length, "1");
    for (uint32_t i = 0; i < 96; i += 1)
    {
        length += (uint32_t)snprintf(nested_source + length, sizeof(nested_source) - length, "])");
    }
    snprintf(nested_source + length, sizeof(nested_source) - length, " }; _Static_assert(N==1, \"nested type queries\");\n");
    char nested_attributes[8192];
    length = (uint32_t)snprintf(nested_attributes, sizeof(nested_attributes), "struct S { long value; }; enum { N=");
    for (uint32_t i = 0; i < 96; i += 1)
    {
        length += (uint32_t)snprintf(nested_attributes + length, sizeof(nested_attributes) - length, "sizeof(struct __attribute__((aligned(");
    }
    length += (uint32_t)snprintf(nested_attributes + length, sizeof(nested_attributes) - length, "8");
    for (uint32_t i = 0; i < 96; i += 1)
    {
        length += (uint32_t)snprintf(nested_attributes + length, sizeof(nested_attributes) - length, "))) S)");
    }
    snprintf(nested_attributes + length, sizeof(nested_attributes) - length, " }; _Static_assert(N==8, \"nested tag attribute jobs\");\n");
    char long_shuffle[4096];
    length = (uint32_t)snprintf(long_shuffle, sizeof(long_shuffle), "typedef int V __attribute__((vector_size(16))); V a; enum { N=sizeof(__builtin_shufflevector(a,a");
    for (uint32_t i = 0; i < 512; i += 1)
    {
        length += (uint32_t)snprintf(long_shuffle + length, sizeof(long_shuffle) - length, ",%u", i % 8);
    }
    snprintf(long_shuffle + length, sizeof(long_shuffle) - length, ")) }; _Static_assert(N==2048, \"long shuffle operand slice\");\n");
    char raw_shuffle[4096];
    length = (uint32_t)snprintf(raw_shuffle, sizeof(raw_shuffle), "typedef int V __attribute__((vector_size(16))); V a; typedef int T [[gnu::aligned(sizeof(__builtin_shufflevector(a,a");
    for (uint32_t i = 0; i < 512; i += 1)
    {
        length += (uint32_t)snprintf(raw_shuffle + length, sizeof(raw_shuffle) - length, ",%u", i % 8);
    }
    snprintf(raw_shuffle + length, sizeof(raw_shuffle) - length, ")))]]; _Static_assert(_Alignof(T)==2048, \"raw long shuffle operand slice\");\n");
    CheckTestFixtures catalog;
    const CheckTestBoundSource bindings[] = {
        { "@nested-selections-0", nested_selections[0] },
        { "@nested-selections-1", nested_selections[1] },
        { "@nested-selections-2", nested_selections[2] },
        { "@nested-selections-3", nested_selections[3] },
        { "@long-shuffle", long_shuffle },
        { "@raw-shuffle", raw_shuffle },
        { "@nested-attributes", nested_attributes },
        { "@nested-source", nested_source },
        { "@wide-truth-source", wide_truth_source },
    };
    const uint32_t file_count = check_test_private_catalog(test, &catalog, "checker/constant-expressions", bindings, BUSTER_ARRAY_LENGTH(bindings), 0);
    const CheckSourceFixture* fixtures = catalog.cases;
    CheckTestOracleBatch batch;
    const bool allocated = check_test_oracle_batch(test, &batch, &catalog.storage, file_count);
    char directory[] = "/tmp/buster-checker-constant-XXXXXX";
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
    check_test_expect(test, ready, "declaration-time constant Clang oracle could not run");
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixtures[i].name);
        if (fixtures[i].source == wide_truth_source)
        {
            printf("test-checker: full-width choose_expr oracle probe: Clang %s the GCC-confirmed checker regression\n", batch.outcomes[i].rejected ? "rejects" : "accepts");
        }
        else
        {
            check_test_expect(test, batch.outcomes[i].rejected == (fixtures[i].rejected && !fixtures[i].extension), description);
        }
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
    check_test_program_suite(test, "checker/programs/constant-sources", clang);
}
