// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Environment boundary tests, not a replacement source adapter/type checker. Source fixtures below run through
// preprocessing and both real parsers for token identities and body extents. Their semantic events are explicitly
// supplied by the test, while Clang independently checks the C rule. Random events have a linear-search oracle.

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

typedef struct CEnvironmentTest CEnvironmentTest;
struct CEnvironmentTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

static void c_environment_test_check(CEnvironmentTest* test, bool condition, const char* message)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 20)
        {
            fprintf(stderr, "test-environments: %s\n", message);
        }
        test->failures += 1;
    }
}

static uint32_t c_environment_test_random(CEnvironmentTest* test)
{
    test->random = test->random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(test->random >> 32);
}

// Independent of the index, its construction and both binary searches.
static uint32_t c_environment_test_oracle(const CBindings* bindings, uint32_t key, uint32_t point)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < bindings->count; i += 1)
    {
        if (bindings->operation[i] == C_BINDING_DECLARE && bindings->depth[i] == 0 && bindings->key[i] == key && bindings->point[i] <= point)
        {
            result = i + 1;
        }
    }
    return result;
}

typedef struct CEnvironmentTestJobs CEnvironmentTestJobs;
struct CEnvironmentTestJobs
{
    const CEnvironment* environment;
    const uint32_t* keys;
    const uint32_t* points;
    uint32_t* results;
    uint32_t count;
    uint32_t partition;
    bool valid[8];
};

static void c_environment_test_jobs(void* argument)
{
    CEnvironmentTestJobs* jobs = argument;
    bool valid = true;
    const uint32_t lane = lane_index();
    // Reverse order, cyclic placement and changing batch partitions all retain the original source output slots.
    const uint32_t batches = (jobs->count + jobs->partition - 1) / jobs->partition;
    for (uint32_t reverse = lane; reverse < batches; reverse += lane_count())
    {
        const uint32_t begin = (batches - 1 - reverse) * jobs->partition;
        const uint32_t count = jobs->count - begin < jobs->partition ? jobs->count - begin : jobs->partition;
        valid &= c_environment_lookup_batch(jobs->environment, jobs->keys + begin, jobs->points + begin, count, jobs->results + begin);
    }
    jobs->valid[lane] = valid;
}

static void c_environment_test_histories(CEnvironmentTest* test, Arena* arena, Arena* scratch)
{
    CBindings bindings;
    bool valid = c_bindings_initialize(&bindings, arena, 33, 1024);
    c_environment_test_check(test, valid, "binding allocation");
    const uint64_t mark = arena->length;
    for (uint32_t world = 0; valid && world < 200; world += 1)
    {
        c_bindings_reset(&bindings);
        uint32_t scope = 0;
        bool local = false;
        for (uint32_t i = 0; i < 256; i += 1)
        {
            const uint32_t random = c_environment_test_random(test);
            const CBindingNamespace space = (CBindingNamespace)(random % 2);
            const uint32_t name = 1 + (random >> 8) % 32;
            if (i % 19 == 0 && !local)
            {
                scope = c_bindings_mark(&bindings);
                local = true;
            }
            else if (i % 13 == 0 && local)
            {
                c_bindings_restore(&bindings, scope, i / 2);
                local = false;
            }
            else if (random & 1)
            {
                c_bindings_declare(&bindings, space, name, space == C_BINDING_TAG ? C_BINDING_TAG_KIND : (CBindingKind)((random >> 16) % 4), i, i / 2, local);
            }
            else
            {
                c_bindings_use(&bindings, space, name, i, i / 2);
            }
        }
        c_bindings_restore(&bindings, 0, 256);
        CEnvironment environment;
        valid = c_environment_build(&environment, &bindings, arena, scratch);
        c_environment_test_check(test, valid, "history construction");
        uint32_t keys[1024];
        uint32_t points[1024];
        uint32_t expected[1024];
        uint32_t results[1024];
        for (uint32_t i = 0; valid && i < 1024; i += 1)
        {
            const uint32_t random = c_environment_test_random(test);
            keys[i] = (random & 1) * bindings.name_capacity + 1 + (random >> 8) % 32;
            points[i] = i == 0 ? UINT32_MAX : (random >> 16) % 258;
            expected[i] = c_environment_test_oracle(&bindings, keys[i], points[i]);
            const uint32_t got = c_environment_lookup(&environment, (CBindingNamespace)(keys[i] / bindings.name_capacity), keys[i] % bindings.name_capacity, points[i]);
            c_environment_test_check(test, got == expected[i], "scalar history differs from linear oracle");
        }
        valid = valid && c_environment_lookup_batch(&environment, keys, points, 1024, results);
        c_environment_test_check(test, valid, "batch history status");
        for (uint32_t i = 0; valid && i < 1024; i += 1)
        {
            c_environment_test_check(test, results[i] == expected[i], "batch history differs from linear oracle");
        }
        if (world == 0 && valid)
        {
            const uint32_t partitions[] = { 1, 7, 16, 31, 65 };
            const uint32_t lanes[] = { 1, 2, 3, 8 };
            for (uint32_t p = 0; p < 5; p += 1)
            {
                for (uint32_t n = 0; n < 4; n += 1)
                {
                    CEnvironmentTestJobs jobs =
                    {
                        .environment = &environment,
                        .keys = keys,
                        .points = points,
                        .results = results,
                        .count = 1024,
                        .partition = partitions[p],
                    };
                    c_environment_test_check(test, lane_run(lanes[n], c_environment_test_jobs, &jobs, 0), "lane start");
                    for (uint32_t j = 0; j < lanes[n]; j += 1)
                    {
                        c_environment_test_check(test, jobs.valid[j], "lane lookup status");
                    }
                    c_environment_test_check(test, memcmp(results, expected, sizeof(results)) == 0, "out-of-order/lane/partition output changed");
                }
            }
        }
        arena_pop_to(arena, mark);
    }
    if (valid)
    {
        CEnvironment environment;
        const uint64_t before = scratch->length;
        c_environment_test_check(test, !c_environment_build(&environment, &bindings, arena, arena) && environment.bindings == 0, "aliased arenas accepted");
        for (uint64_t capacity = 0; capacity < 2048; capacity += 64)
        {
            Arena tiny = arena_reserve(capacity);
            const bool built = c_environment_build(&environment, &bindings, &tiny, scratch);
            c_environment_test_check(test, built || (environment.bindings == 0 && tiny.length == 0), "failed build did not roll back output");
            c_environment_test_check(test, scratch->length == before, "scratch leaked across build");
            arena_release(&tiny);
            tiny = arena_reserve(capacity);
            const uint64_t arena_mark = arena->length;
            const bool scratch_built = c_environment_build(&environment, &bindings, arena, &tiny);
            c_environment_test_check(test, scratch_built || arena->length == arena_mark, "scratch failure did not roll back index");
            arena_pop_to(arena, arena_mark);
            arena_release(&tiny);
        }
        c_bindings_reset(&bindings);
        const uint64_t empty_mark = arena->length;
        const bool empty = c_environment_build(&environment, &bindings, arena, scratch);
        c_environment_test_check(test, empty && environment.count == 0 && c_environment_lookup(&environment, C_BINDING_TAG, 1, UINT32_MAX) == 0, "empty history");
        c_environment_test_check(test, empty && c_environment_lookup_batch(&environment, 0, 0, 0, 0), "empty query batch");
        arena_pop_to(arena, empty_mark);
        bindings.valid = false;
        c_environment_test_check(test, !c_environment_build(&environment, &bindings, arena, scratch) && environment.bindings == 0 && arena->length == empty_mark, "failed stream published an environment");
    }
}

static void c_environment_test_types(CEnvironmentTest* test)
{
    TypeTestTable world;
    const bool valid = type_test_table_open(&world, 512);
    c_environment_test_check(test, valid, "type table allocation");
    if (valid)
    {
        TypeTable* table = &world.table;
        const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 1);
        const TypeId shadow = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 1);
        const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 2);
        const TypeId pointer = type_pointer(table, record);
        const TypeId aligned = type_aligned(table, record, 6);
        const TypeMember member =
        {
            .name = 3,
            .type = BUSTER_TYPE_INT,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        };
        c_environment_test_check(test, type_complete_record(table, record, &member, 1, (TypeRecordAttributes){ 0 }), "record layout");
        c_environment_test_check(test, type_complete_record(table, shadow, &member, 1, (TypeRecordAttributes){ 0 }), "shadow layout");
        c_environment_test_check(test, type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0), "enum layout");
        uint32_t completions[513];
        for (uint32_t i = 0; i < 513; i += 1)
        {
            completions[i] = UINT32_MAX;
        }
        completions[table->types.index[record >> BUSTER_TYPE_INDEX_SHIFT]] = 20;
        completions[table->types.index[shadow >> BUSTER_TYPE_INDEX_SHIFT]] = 60;
        completions[table->types.index[enumeration >> BUSTER_TYPE_INDEX_SHIFT]] = 40;
        const TypeId array = type_array(table, record, 4);
        const TypeId atomic = type_atomic(table, record);
        const TypeId vector_enum = type_vector(table, enumeration, 4, true);
        const TypeId kinds[] = { record, shadow, enumeration, pointer, aligned, array, atomic, BUSTER_TYPE_INT, 0, vector_enum };
        const uint32_t required[] = { 20, 60, 40, 0, 20, 20, 20, 0, UINT32_MAX, 40 };
        for (uint32_t k = 0; k < 10; k += 1)
        {
            c_environment_test_check(test, c_environment_type_point(table, completions, 513, kinds[k]) == required[k], "derived/tag completion requirement");
        }
        // Guard every input and output at the logical end, not merely at an arena's allocation boundary.
        uint8_t* guarded = mmap(0, 6 * 8192, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        bool mapped = guarded != MAP_FAILED;
        for (uint32_t i = 0; mapped && i < 6; i += 1)
        {
            mapped = mprotect(guarded + i * 8192, 4096, PROT_READ | PROT_WRITE) == 0;
        }
        c_environment_test_check(test, mapped, "guard mapping");
        for (uint32_t count = 0; mapped && count <= 97; count += 1)
        {
            TypeId* types = (TypeId*)(guarded + 4096 - count * 4);
            uint32_t* points = (uint32_t*)(guarded + 8192 + 4096 - count * 4);
            uint8_t* scalar = guarded + 2 * 8192 + 4096 - count;
            uint8_t* vector = guarded + 3 * 8192 + 4096 - count;
            for (uint32_t i = 0; i < count; i += 1)
            {
                types[i] = kinds[i % 10];
                points[i] = (i % 65);
                scalar[i] = vector[i] = 0xa5;
            }
            c_environment_types_scalar(table, completions, 513, types, points, count, scalar);
            c_environment_types(table, completions, 513, types, points, count, vector);
            for (uint32_t i = 0; i < count; i += 1)
            {
                const bool expected = required[i % 10] != UINT32_MAX && required[i % 10] <= points[i];
                c_environment_test_check(test, vector[i] == expected && scalar[i] == expected, "completion scalar/vector/expected mismatch");
            }
        }
        if (guarded != MAP_FAILED)
        {
            munmap(guarded, 6 * 8192);
        }
        c_environment_test_check(test, c_environment_type_point(table, completions, 1, record) == UINT32_MAX, "missing tag position accepted");
    }
    type_test_table_close(&world);
}

static void c_environment_test_guarded(CEnvironmentTest* test, Arena* arena, Arena* scratch)
{
    const uint64_t mark = arena->length;
    CBindings bindings;
    bool valid = c_bindings_initialize(&bindings, arena, 3, 8);
    c_bindings_declare(&bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 0, 0, 0);
    c_bindings_declare(&bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 9, 10, 0);
    c_bindings_declare(&bindings, C_BINDING_TAG, 1, C_BINDING_TAG_KIND, 19, 20, 0);
    CEnvironment environment;
    valid = valid && c_environment_build(&environment, &bindings, arena, scratch);
    c_environment_test_check(test, valid, "guard history build");
    uint8_t* guard = mmap(0, 6 * 8192, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool mapped = guard != MAP_FAILED;
    for (uint32_t i = 0; mapped && i < 6; i += 1)
    {
        mapped = mprotect(guard + i * 8192, 4096, PROT_READ | PROT_WRITE) == 0;
    }
    c_environment_test_check(test, mapped, "guarded lookup mapping");
    for (uint32_t count = 0; valid && mapped && count <= 97; count += 1)
    {
        uint32_t* keys = (uint32_t*)(guard + 4096 - count * 4);
        uint32_t* points = (uint32_t*)(guard + 8192 + 4096 - count * 4);
        uint32_t* results = (uint32_t*)(guard + 2 * 8192 + 4096 - count * 4);
        for (uint32_t i = 0; i < count; i += 1)
        {
            keys[i] = (i % 2) * 3 + 1 + (i % 7 == 0);
            points[i] = i % 23;
        }
        c_environment_test_check(test, c_environment_lookup_batch(&environment, keys, points, count, results), "guarded batch status");
        for (uint32_t i = 0; i < count; i += 1)
        {
            c_environment_test_check(test, results[i] == c_environment_test_oracle(&bindings, keys[i], points[i]), "guarded lookup mismatch");
        }
        uint8_t* roles = guard + 3 * 8192 + 4096 - count;
        uint32_t* depths = (uint32_t*)(guard + 4 * 8192 + 4096 - count * 4);
        memset(roles, 0, count);
        memset(depths, 0, count * 4);
        if (count >= 2)
        {
            roles[0] = PARSE_ROLE_OPEN_BODY;
            roles[count - 1] = PARSE_ROLE_CLOSE_BODY;
        }
        CBodyRange range;
        uint32_t bodies = 0;
        const ParseTree tree = { .roles = roles, .depths = depths };
        c_environment_test_check(test, c_environment_bodies(&tree, count, &range, 1, &bodies) && bodies == (count >= 2), "guarded body roles");
        if (count >= 2)
        {
            c_environment_test_check(test, range.begin == 0 && range.end == count, "body extent");
            c_environment_test_check(test, !c_environment_bodies(&tree, count, &range, 0, &bodies) && bodies == 0, "body capacity accepted");
        }
    }
    if (valid)
    {
        uint32_t bad = UINT32_MAX;
        uint32_t point = 0;
        uint32_t result = 0;
        c_environment_test_check(test, !c_environment_lookup_batch(&environment, &bad, &point, 1, &result), "invalid key accepted");
        bad = 0;
        c_environment_test_check(test, !c_environment_lookup_batch(&environment, &bad, &point, 1, &result), "identifier zero accepted");
    }
    if (guard != MAP_FAILED)
    {
        munmap(guard, 6 * 8192);
    }
    arena_pop_to(arena, mark);
}

// The role reader sees macro-expanded tokens and nested initializer/tag braces. Semantic events are deliberately
// explicit here: the checker adapter owns their production, and these tests must not disguise a brace skim as one.
static void c_environment_test_source(CEnvironmentTest* test, Arena* arena, Arena* scratch)
{
    char directory[] = "/tmp/buster-environments-XXXXXX";
    const bool made = mkdtemp(directory) != 0;
    c_environment_test_check(test, made, "source temporary directory");
    const char* sources[] =
    {
        "int f(void) { return later; }\nint later = 7;\n",
        "struct S;\nint f(void) { return sizeof(struct S); }\nstruct S { int x; };\n",
        "int x = sizeof(enum { A = 1 });\nint y = A;\nint f(void) { return A; }\n",
        "typedef int T;\nint f(void) { T value = 1; return value; }\nvoid g(void) { int T; T = 2; }\n",
        "int g;\n#define BODY { extern int g; return g; }\nint f(void) BODY\nint h(void) { struct S { int x; }; return sizeof(struct S); }\n",
        "int f(void) { extern int g; return g; }\nint h(void) { return g; }\n",
    };
    for (uint32_t fixture = 0; made && fixture < 6; fixture += 1)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/fixture.c", directory);
        bool valid = parse_test_write(path, sources[fixture], strlen(sources[fixture]));
        const char* arguments[] = { "-std=c17", "-pedantic-errors" };
        ParseTestClang oracle;
        char* paths[] = { path };
        valid = valid && parse_test_clang("clang", arguments, 2, paths, 1, &oracle);
        c_environment_test_check(test, valid && oracle.rejected == (fixture < 2 || fixture == 5), "Clang source rule expectation");
        CBodyRange expected[8];
        uint32_t expected_count = 0;
        for (uint32_t scalar = 0; valid && scalar < 2; scalar += 1)
        {
            const uint64_t mark = arena->length;
            InternTable table = { 0 };
            Preprocessor pp = { 0 };
            Parser parser = { 0 };
            const PPOptions options = { .language = { .standard = 2017 } };
            const ParseOptions parse_options = { .scalar = scalar != 0 };
            bool parsed = intern_initialize(&table) && pp_initialize(&pp, &options, &table) && parse_initialize(&parser, &parse_options, options.language);
            parsed = parsed && pp_run(&pp, path) && parse_translation_unit(&parser, &pp) && parser.errors == 0;
            c_environment_test_check(test, parsed, "source preprocessing/parser");
            if (parsed)
            {
                CBodyRange bodies[8];
                uint32_t count = 0;
                c_environment_test_check(test, c_environment_bodies(&parser.tree, parser.tokens.count, bodies, 8, &count), "parsed body extraction");
                c_environment_test_check(test, count == (fixture >= 3 ? 2 : 1), "parsed function count");
                if (scalar == 0)
                {
                    expected_count = count;
                    memcpy(expected, bodies, count * sizeof(*bodies));
                }
                else
                {
                    c_environment_test_check(test, count == expected_count && memcmp(expected, bodies, count * sizeof(*bodies)) == 0, "parser paths changed body extents");
                }
                if (fixture == 0)
                {
                    const IdentId name = intern_find(&table, (const uint8_t*)"later", 5);
                    uint32_t use = UINT32_MAX;
                    uint32_t declaration = UINT32_MAX;
                    for (uint32_t i = 0; i < parser.tokens.count; i += 1)
                    {
                        if (parser.tokens.names[i] == name)
                        {
                            if (i < bodies[0].end)
                            {
                                use = i;
                            }
                            else
                            {
                                declaration = i;
                            }
                        }
                    }
                    CBindings bindings;
                    bool events = c_bindings_initialize(&bindings, arena, table.count, 16);
                    events = events && c_bindings_use(&bindings, C_BINDING_ORDINARY, name, use, use) != 0;
                    const uint32_t handle = c_bindings_declare(&bindings, C_BINDING_ORDINARY, name, C_BINDING_OBJECT, declaration, declaration + 1, 0);
                    CEnvironment environment;
                    events = events && handle != 0 && c_environment_build(&environment, &bindings, arena, scratch);
                    c_environment_test_check(test, events, "explicit source-position event setup");
                    if (events)
                    {
                        uint32_t replay[16];
                        uint32_t batch[16];
                        CBindingStatistics statistics;
                        c_environment_test_check(test, c_bindings_scalar(&bindings, replay, scratch) && c_bindings_batch(&bindings, batch, scratch, &statistics), "source event twins");
                        c_environment_test_check(test, replay[0] == 0 && batch[0] == 0 && c_environment_lookup(&environment, C_BINDING_ORDINARY, name, use) == 0, "future declaration leaked into body");
                        c_environment_test_check(test, c_environment_lookup(&environment, C_BINDING_ORDINARY, name, declaration + 1) == handle, "declaration not available at its point");
                    }
                }
                if (fixture == 1)
                {
                    const IdentId name = intern_find(&table, (const uint8_t*)"S", 1);
                    uint32_t use = UINT32_MAX;
                    uint32_t completion = UINT32_MAX;
                    for (uint32_t i = 0; i < parser.tokens.count; i += 1)
                    {
                        if (parser.tokens.names[i] == name && i > bodies[0].begin && i < bodies[0].end)
                        {
                            use = i;
                        }
                        if (parser.tree.roles[i] == PARSE_ROLE_CLOSE_STRUCT && i >= bodies[0].end)
                        {
                            completion = i + 1;
                        }
                    }
                    TypeTestTable world = { 0 };
                    bool typed = type_test_table_open(&world, 16);
                    const TypeId record = typed ? type_tag(&world.table, BUSTER_TYPE_KIND_STRUCT, name) : 0;
                    const TypeMember member = { .name = 2, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
                    typed = typed && record != 0 && type_complete_record(&world.table, record, &member, 1, (TypeRecordAttributes){ 0 });
                    c_environment_test_check(test, typed && use != UINT32_MAX && completion != UINT32_MAX, "explicit source completion setup");
                    if (typed)
                    {
                        uint32_t completions[17];
                        for (uint32_t i = 0; i < 17; i += 1)
                        {
                            completions[i] = UINT32_MAX;
                        }
                        completions[world.table.types.index[record >> BUSTER_TYPE_INDEX_SHIFT]] = completion;
                        uint8_t scalar_result = 1;
                        uint8_t vector_result = 1;
                        c_environment_types_scalar(&world.table, completions, 17, &record, &use, 1, &scalar_result);
                        c_environment_types(&world.table, completions, 17, &record, &use, 1, &vector_result);
                        c_environment_test_check(test, scalar_result == 0 && vector_result == 0, "later tag completion leaked into source body");
                    }
                    type_test_table_close(&world);
                }
                if (fixture == 2 || fixture == 3 || fixture == 5)
                {
                    const char* spelling = fixture == 2 ? "A" : fixture == 3 ? "T" : "g";
                    const IdentId name = intern_find(&table, (const uint8_t*)spelling, 1);
                    uint32_t declaration = UINT32_MAX;
                    uint32_t use = UINT32_MAX;
                    for (uint32_t i = 0; i < parser.tokens.count; i += 1)
                    {
                        if (parser.tokens.names[i] == name)
                        {
                            if (declaration == UINT32_MAX)
                            {
                                declaration = i;
                            }
                            else
                            {
                                use = i;
                            }
                        }
                    }
                    CBindings bindings;
                    bool events = c_bindings_initialize(&bindings, arena, table.count, 16);
                    const CBindingKind kind = fixture == 2 ? C_BINDING_ENUMERATOR : fixture == 3 ? C_BINDING_TYPEDEF : C_BINDING_OBJECT;
                    // Only fixture 5's first declaration belongs to a body; it has linkage but no file-scope name.
                    const uint32_t point = declaration + (fixture == 2 ? 3 : 1);
                    const uint32_t handle = c_bindings_declare(&bindings, C_BINDING_ORDINARY, name, kind, declaration, point, fixture == 5);
                    CEnvironment environment;
                    events = events && handle != 0 && c_environment_build(&environment, &bindings, arena, scratch);
                    c_environment_test_check(test, events && use != UINT32_MAX, "explicit initializer/typedef/extern events");
                    if (events)
                    {
                        c_environment_test_check(test, c_environment_lookup(&environment, C_BINDING_ORDINARY, name, use) == (fixture == 5 ? 0 : handle), "initializer typedef or block extern visibility");
                    }
                }
            }
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&table);
            arena_pop_to(arena, mark);
        }
        unlink(path);
    }
    if (made)
    {
        rmdir(directory);
    }
}

static int run_c_environment_tests(int argc, char** argv)
{
    CEnvironmentTest test = { .random = 113 };
    bool valid = argc == 0 || (argc == 2 && strcmp(argv[0], "--seed") == 0);
    if (argc == 2 && valid)
    {
        char* end = 0;
        test.random = strtoull(argv[1], &end, 10);
        valid = *argv[1] != 0 && *end == 0;
    }
    if (valid)
    {
        Arena arena = arena_reserve(64ull << 20);
        Arena scratch = arena_reserve(16ull << 20);
        c_environment_test_histories(&test, &arena, &scratch);
        c_environment_test_types(&test);
        c_environment_test_guarded(&test, &arena, &scratch);
        c_environment_test_source(&test, &arena, &scratch);
        arena_release(&scratch);
        arena_release(&arena);
        printf("test-environments: %llu checks, %llu failures (boundary fixtures; checker/corpus integration pending)\n", (unsigned long long)test.checks, (unsigned long long)test.failures);
    }
    else
    {
        fprintf(stderr, "usage: test-environments [--seed N]\n");
    }
    return !valid || test.failures != 0;
}
