// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Generated sizeof forests: count the actual operand folds and the tokens handed to them, not wall time. Both
// reader paths must do at most one token pass of folding and produce identical typed streams through 16,000 levels.

static void check_test_sizeof_source(CheckTest* test, const char* source, uint32_t form, uint32_t depth)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    const PPOptions options =
    {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = true,
    };
    CheckTestTypes types[2] = { 0 };
    CheckInput input[2] = { 0 };
    CheckStream streams[2] = { 0 };
    Arena storage[2] = { 0 };
    Arena output[2] = { 0 };
    Arena scratch[2] = { 0 };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                 pp_run_source(&pp, "sizeof-scaling.c", (const uint8_t*)source, strlen(source));
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { .scalar = path == 0, .verify = true };
        storage[path] = arena_reserve(UINT64_C(1) << 28);
        output[path] = arena_reserve(UINT64_C(1) << 28);
        scratch[path] = arena_reserve(UINT64_C(1) << 28);
        ready = ready && storage[path].base != NULL && output[path].base != NULL && scratch[path].base != NULL &&
                check_test_types_open(&types[path]) && parse_initialize(&parsers[path], &parsing, options.language) &&
                parse_translation_unit(&parsers[path], &pp) &&
                check_input(&input[path], &parsers[path], &pp, &types[path].table, &storage[path], &scratch[path]) &&
                check_graph_run(&types[path].table, &input[path].graph, &output[path], &scratch[path], &checking, &streams[path]);
        const uint32_t folds = form == 3 ? depth : form == 1 && depth == 1 ? 0 : 1;
        const uint32_t queries = form == 4 ? 1 : depth;
        char description[256];
        snprintf(description, sizeof(description), "sizeof form %u depth %u path %u: %u/%u folds, %llu/%u tokens", form, depth, path,
                 input[path].sizeof_queries, folds, (unsigned long long)input[path].sizeof_query_tokens, input[path].token_count);
        check_test_expect(test, ready && input[path].sizeof_queries == folds && input[path].sizeof_query_tokens <= input[path].token_count, description);
        bool values = ready && input[path].report_count == 0 && streams[path].report_count == 0 &&
                      input[path].constant_warning_count == 0 && streams[path].warning_count == 0;
        uint32_t found = 0;
        for (uint32_t node = 0; ready && node < streams[path].count; node += 1)
        {
            if (input[path].graph.ops[node] == CHECK_OP_SIZEOF)
            {
                const CheckValue value = streams[path].values[node];
                const uint32_t expected = form == 3 || form == 4 || found == 0 ? 4 : 8;
                values &= value.type == BUSTER_TYPE_ULONG && value.constant_kind == CHECK_CONSTANT_INTEGER && value.integer == expected;
                found += 1;
            }
        }
        check_test_expect(test, values && found == queries, "generated sizeof graph retains each query, correct values and no warnings");
    }
    if (ready)
    {
        check_test_expect(test, input[0].graph.argument_count == input[1].graph.argument_count &&
                               check_test_stream_equal(&streams[0], &streams[1], input[0].graph.argument_count), "sizeof scalar/vector typed streams agree");
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        arena_release(&scratch[path]);
        arena_release(&output[path]);
        arena_release(&storage[path]);
        check_test_types_close(&types[path]);
        parse_release(&parsers[path]);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_sizeof_scaling(CheckTest* test)
{
    const uint32_t depths[] = { 1, 2, 21, 22, 63, 64, 65, 500, 1000, 2000, 4000, 8000, 16000 };
    for (uint32_t form = 0; form < 5; form += 1)
    {
        for (uint32_t size = 0; size < BUSTER_ARRAY_LENGTH(depths); size += 1)
        {
            const uint32_t depth = depths[size];
            const uint64_t capacity = (uint64_t)depth * 16 + 128;
            char* source = malloc(capacity);
            check_test_expect(test, source != NULL, "sizeof scaling source allocation");
            if (source != NULL)
            {
                uint64_t length = (uint64_t)snprintf(source, capacity, "unsigned long f(void){return ");
                if (form == 4)
                {
                    memcpy(source + length, "sizeof(", 7);
                    length += 7;
                }
                for (uint32_t level = 0; level < depth; level += 1)
                {
                    const char* opening = form == 2 ? "sizeof(1+" : form == 3 ? "sizeof(0)+" : form == 4 ? "(" : "sizeof(";
                    const uint64_t bytes = strlen(opening);
                    memcpy(source + length, opening, bytes);
                    length += bytes;
                }
                const char* inner = form == 1 ? "int" : "0";
                const uint64_t bytes = strlen(inner);
                memcpy(source + length, inner, bytes);
                length += bytes;
                const uint32_t closes = form == 3 ? 0 : depth + (form == 4);
                memset(source + length, ')', closes);
                length += closes;
                memcpy(source + length, ";}\n", 4);
                check_test_sizeof_source(test, source, form, depth);
                free(source);
            }
        }
    }
}
