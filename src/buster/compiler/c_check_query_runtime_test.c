// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Independent source/default proofs and executable Clang observations for strict floating constant queries.
#ifndef BUSTER_C_CHECK_QUERY_RUNTIME_TEST_INCLUDED
#define BUSTER_C_CHECK_QUERY_RUNTIME_TEST_INCLUDED

typedef struct CheckQueryFixture CheckQueryFixture;
struct CheckQueryFixture
{
    const char* expression;
    uint8_t source;
    uint8_t runtime;
    uint8_t evaluated;
    bool inexact;
    bool optimized_constant;
};

static void check_test_query_source(CheckTest* test, const CheckQueryFixture* fixture, uint32_t index)
{
    char source[2048];
    snprintf(source, sizeof(source), "#pragma STDC FENV_ACCESS ON\nextern int next(void);extern volatile double observed;"
             "int tested(int n,double x){_Static_assert(__builtin_constant_p(%s)==%u,\"source proof\");return __builtin_constant_p(%s);}\n",
             fixture->expression, fixture->source, fixture->expression);
    InternTable identifiers = { 0 };
    Preprocessor pp = { 0 };
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                              pp_run_source(&pp, "strict-query.c", (const uint8_t*)source, (uint32_t)strlen(source));
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        Parser parser = { 0 };
        CheckTestTypes types = { 0 };
        Arena storage = arena_reserve(UINT64_C(1) << 21);
        Arena output = arena_reserve(UINT64_C(1) << 20);
        Arena scratch = arena_reserve(UINT64_C(1) << 21);
        const ParseOptions parse_options = { .scalar = path == 0 };
        const CheckOptions options = { .scalar = path == 0, .batch_size = (index % BUSTER_CHECK_BATCH) + 1 };
        bool ready = parse_initialize(&parser, &parse_options, pp_options.language) && parse_translation_unit(&parser, &pp) && check_test_types_open(&types);
        CheckInputReport reports[16];
        CheckInput input = { .reports = reports, .report_capacity = BUSTER_ARRAY_LENGTH(reports) };
        CheckStream stream = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &storage, &scratch) &&
                check_graph_run(&types.table, &input.graph, &output, &scratch, &options, &stream);
        char description[160];
        snprintf(description, sizeof(description), "strict query %u path %u preserves independent source and executable proofs", index, path);
        bool valid = ready && stream.report_count == 0 && stream.query_count != 0;
        if (valid)
        {
            const CheckBuiltinQueryEffect* effect = stream.queries + stream.query_count - 1;
            const CheckValue checked_value = check_stream_value(&stream, effect->node);
            const CheckValue* value = &checked_value;
            valid = value->constant_kind == CHECK_CONSTANT_INTEGER && value->integer == fixture->source &&
                    effect->runtime_value.constant_kind == CHECK_CONSTANT_INTEGER && effect->runtime_value.integer == fixture->runtime &&
                    effect->evaluated_operands == fixture->evaluated;
            if (!valid)
            {
                fprintf(stderr, "test-checker: query %u/%u source=%llu runtime=%llu evaluated=%u expected=%u/%u/%u\n", index, path,
                        (unsigned long long)value->integer, (unsigned long long)effect->runtime_value.integer, effect->evaluated_operands,
                        fixture->source, fixture->runtime, fixture->evaluated);
            }
        }
        else
        {
            fprintf(stderr, "test-checker: query %u/%u input=%u graph=%u reports=%u queries=%u\n", index, path, input.report_count,
                    stream.status, stream.report_count, stream.query_count);
        }
        check_test_expect(test, valid, description);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&storage);
        check_test_types_close(&types);
        parse_release(&parser);
    }
    check_test_expect(test, preprocessed, "strict query source preprocessing");
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_query_conversion_proofs(CheckTest* test, TypeTable* table)
{
    const TypeId targets[] = { BUSTER_TYPE_INT, BUSTER_TYPE_UINT, BUSTER_TYPE_INT128, BUSTER_TYPE_UINT128 };
    const long double values[] = { 1.0e100L, -1.0e100L, __builtin_nanl("") };
    for (uint32_t target = 0; target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        for (uint32_t value = 0; value < BUSTER_ARRAY_LENGTH(values); value += 1)
        {
            Arena storage = arena_reserve(65536);
            Arena output = arena_reserve(65536);
            Arena scratch = arena_reserve(65536);
            CheckGraph graph;
            uint8_t environments[3] = { 0 };
            const bool ready = check_graph_initialize(&graph, &storage, 3, 1);
            check_test_expect(test, ready, "speculative query conversion graph allocation");
            if (ready)
            {
                graph.fp_environments = environments;
                const uint32_t leaf = check_test_leaf(&graph, 1, check_test_float(BUSTER_TYPE_LONG_DOUBLE, values[value]));
                const uint32_t cast = check_graph_add(&graph, CHECK_OP_CAST, 2, leaf, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                graph.types[cast] = targets[target];
                environments[cast] = BUSTER_CHECK_FP_ENV_ACCESS;
                const uint32_t query = check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_CONSTANT_P, &cast, 1);
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    const CheckOptions options = { .scalar = path == 0 };
                    CheckStream stream;
                    // Saturated, as clang folds the undefined conversion (a NaN to 0), and so constant, as clang's
                    // __builtin_constant_p of it answers in either FP environment.
                    const unsigned __int128 maximum = targets[target] == BUSTER_TYPE_INT ? INT32_MAX : targets[target] == BUSTER_TYPE_UINT ? UINT32_MAX :
                                                      targets[target] == BUSTER_TYPE_INT128 ? ~(unsigned __int128)0 >> 1 : ~(unsigned __int128)0;
                    const bool sign = targets[target] == BUSTER_TYPE_INT || targets[target] == BUSTER_TYPE_INT128;
                    const unsigned __int128 expected = value == 0 ? maximum : value == 1 && sign ? maximum + 1 : 0;
                    check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &options, &stream) && stream.report_count == 0 &&
                        check_stream_value(&stream, cast).fault == CHECK_FAULT_NONE && check_stream_constant_kind(&stream, cast) == CHECK_CONSTANT_INTEGER &&
                        check_stream_value(&stream, cast).integer == expected && check_stream_value(&stream, query).integer == 1 && stream.queries[0].runtime_value.integer == 1 &&
                        stream.queries[0].runtime_plan == CHECK_BUILTIN_QUERY_RUNTIME_SKIP && stream.queries[0].evaluated_operands == 0 &&
                        stream.statistics.scalar_nodes + stream.statistics.vector_nodes == 3,
                        "a constant query of an out-of-range floating conversion is constant, the value saturated as clang folds it");
                    arena_pop_to(&output, 0);
                }
            }
            arena_release(&scratch);
            arena_release(&output);
            arena_release(&storage);
        }
    }
}

static __attribute__((noinline)) void check_test_query_runtime(CheckTest* test, TypeTable* table, const char* clang)
{
    check_test_query_conversion_proofs(test, table);
    const CheckQueryFixture fixtures[] = {
        { "1.0/2.0", 1, 1, 0, false },
        { "1.0/3.0", 1, 0, 1, true },
        { "x+1.0/3.0", 0, 0, 1, true },
        { "(n++,1.0/3.0)", 0, 0, 0, false },
        { "next()+1.0/3.0", 0, 0, 0, false },
        { "1?1.0/3.0:next()", 1, 0, 0, false },
        { "1?1.0/2.0:next()", 1, 1, 0, false },
        { "1?1.0/3.0:(n++,2.0)", 1, 0, 0, false },
        { "1?1.0/3.0:observed", 1, 0, 0, false },
        { "__builtin_constant_p(1.0/3.0)", 1, 1, 1, true },
        { "1+__builtin_constant_p(1.0/3.0)", 1, 1, 1, true },
        { "__builtin_constant_p(x+1.0/3.0)", 1, 1, 1, true },
        { "(double)1", 1, 1, 0, false },
        { "(float)16777217", 1, 0, 1, true },
        { "(float)16777216", 1, 1, 0, false },
        { "(int)1.5", 1, 1, 0, false },
        { "0x1p-1022/2.0", 1, 1, 0, false },
        { "0x1p-1074/2.0", 1, 0, 1, true },
        { "1.0+0.0", 1, 1, 0, false },
        { "1.0+0x1p-64", 1, 0, 1, true },
        { "0.0/0.0", 1, 1, 0, false },
        { "__builtin_fabs(-1.0)", 1, 1, 0, false },
        { "(x,1.0/2.0)", 0, 0, 1, false, true },
        { "__imag__ x", 0, 1, 1, false },
        { "__builtin_expect(1,next())", 1, 1, 0, false },
        { "(__builtin_expect(1,next()),1.0/3.0)", 1, 0, 0, false },
        { "(1.0+2.0i)/(2.0+3.0i)", 1, 1, 0, false },
        { "1.0/__builtin_inf()", 1, 1, 0, false },
        { "__builtin_nan(\"\")<0.0", 1, 0, 1, false },
        { "__builtin_nan(\"\")==0.0", 1, 0, 1, false, true },
        { "__builtin_sqrt(4.0)", 0, 0, 0, false },
        { "__builtin_floor(1.5)", 0, 0, 1, false, true },
        { "__builtin_fabs(1.0/3.0)", 1, 0, 1, true },
        { "!x", 0, 0, 1, false },
        { "x?1:2", 0, 0, 1, false },
        { "__builtin_isinf_sign(x)", 0, 0, 1, false },
        { "__builtin_fpclassify(0,1,2,3,4,x)", 0, 0, 1, false },
        { "1/0", 0, 0, 0, false },
    };
    char directory[] = "/tmp/buster-check-query-runtime-XXXXXX";
    const bool directory_ready = mkdtemp(directory) != NULL;
    char source_path[160];
    char binary_path[160];
    char output_path[160];
    char error_path[160];
    snprintf(source_path, sizeof(source_path), "%s/oracle.c", directory);
    snprintf(binary_path, sizeof(binary_path), "%s/oracle", directory);
    snprintf(output_path, sizeof(output_path), "%s/out", directory);
    snprintf(error_path, sizeof(error_path), "%s/err", directory);
    FILE* source = directory_ready ? fopen(source_path, "w") : NULL;
    bool written = source != NULL && fprintf(source, "#include <fenv.h>\n#pragma STDC FENV_ACCESS ON\nvolatile double observed=2;int visits;int next(void){visits++;return 2;}\n") > 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        check_test_query_source(test, fixtures + i, i);
        if (written)
        {
            written = fprintf(source, "int f%u(int n,double x){_Static_assert(__builtin_constant_p(%s)==%u,\"source\");"
                              "feclearexcept(FE_ALL_EXCEPT);int q=__builtin_constant_p(%s);return q+2*!!fetestexcept(FE_INEXACT)+4*visits;}\n",
                              i, fixtures[i].expression, fixtures[i].source, fixtures[i].expression) > 0;
        }
    }
    if (written)
    {
        written = fprintf(source, "int main(void){\n") > 0;
        for (uint32_t i = 0; written && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
        {
            written = fprintf(source, "if(f%u(2,1.0)!=%u+(%u && OPTIMIZED))return %u;\n", i,
                              fixtures[i].runtime + 2u * fixtures[i].inexact, fixtures[i].optimized_constant, i + 1) > 0;
        }
        written = fprintf(source, "return 0;}\n") > 0 && written;
    }
    if (source != NULL)
    {
        written = fclose(source) == 0 && written;
    }
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        char* arguments[] = { (char*)clang, "-std=gnu17", mode == 0 ? "-O0" : "-O2", mode == 0 ? "-DOPTIMIZED=0" : "-DOPTIMIZED=1", "-Wno-unused-value", source_path, "-lm", "-o", binary_path, NULL };
        char* execute[] = { binary_path, NULL };
        const int compiled = written ? object_test_run(arguments, output_path, error_path) : -1;
        const int executed = compiled == 0 ? object_test_run(execute, output_path, error_path) : -1;
        if (compiled != 0 || executed != 0)
        {
            fprintf(stderr, "test-checker: independent query oracle O%u compile=%d case=%d\n", mode == 0 ? 0 : 2, compiled, executed);
        }
        check_test_expect(test, compiled == 0 && executed == 0,
                          "independent Clang runtime observes baseline O0 proofs and separately qualified optimized results");
    }
    if (directory_ready)
    {
        unlink(source_path);
        unlink(binary_path);
        unlink(output_path);
        unlink(error_path);
        rmdir(directory);
    }
    Arena storage = arena_reserve(65536);
    CheckGraph graph;
    uint8_t environments[4] = { 0 };
    const bool ready = check_graph_initialize(&graph, &storage, 4, 1);
    if (ready)
    {
        graph.fp_environments = environments;
        const uint32_t one = check_test_leaf(&graph, 1, check_test_float(BUSTER_TYPE_DOUBLE, 1));
        const uint32_t three = check_test_leaf(&graph, 2, check_test_float(BUSTER_TYPE_DOUBLE, 3));
        const uint32_t division = check_graph_add(&graph, CHECK_OP_DIVIDE, 3, one, three, BUSTER_CHECK_NONE);
        environments[division] = BUSTER_CHECK_FP_ENV_ACCESS;
        check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_CONSTANT_P, &division, 1);
        check_test_special_graph_bounds(test, table, &graph);
    }
    check_test_expect(test, ready, "strict-query lazy proof capacity fixture");
    arena_release(&storage);
}

#endif
