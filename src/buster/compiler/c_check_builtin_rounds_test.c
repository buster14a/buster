// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's floating-environment query semantics. Include after c_check_builtin_rounds.c and c_checker_test.c.
// Independent field expectations, inaccessible argument storage, bounded batches and every two-part partition
// exercise the action contract. Original syntax fixtures use the existing external Clang helper; they neither
// execute a floating-environment change nor claim source integration. Configure --clang with the approved oracle.

#include <stddef.h>

#define BUSTER_CHECK_BUILTIN_ROUNDS_TEST_BATCH 17u

typedef struct CheckBuiltinRoundsTest CheckBuiltinRoundsTest;
struct CheckBuiltinRoundsTest
{
    uint64_t checks;
    uint64_t failures;
};

static void check_builtin_rounds_test_expect(CheckBuiltinRoundsTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtins-rounds: %s\n", description);
        }
    }
}

static bool check_builtin_rounds_test_equal(const CheckBuiltinRoundsResult* a, const CheckBuiltinRoundsResult* b)
{
    const bool result = check_test_action_equal(&a->action, &b->action) && a->evaluated_operands == b->evaluated_operands &&
        a->runtime_builtin == b->runtime_builtin && a->compiler_barrier == b->compiler_barrier && a->reads_fp_environment == b->reads_fp_environment;
    return result;
}

static void check_builtin_rounds_test_result(CheckBuiltinRoundsTest* test, const CheckBuiltinRoundsAction* input, const CheckBuiltinRoundsResult* output)
{
    const CheckDiagnostic expected = input->id != CBUILTIN_FLT_ROUNDS ? CHECK_DIAG_UNSUPPORTED :
        input->argument_count != 0 ? CHECK_DIAG_ARGUMENT_COUNT : CHECK_DIAG_NONE;
    check_builtin_rounds_test_expect(test, output->action.diagnostic == expected && output->action.diagnostic_operand == 0,
        "identity and exact zero arity determine the diagnostic before argument access");
    check_builtin_rounds_test_expect(test, output->evaluated_operands == 0 && output->action.value.constant_kind == CHECK_CONSTANT_NONE &&
        output->action.value.fault == CHECK_FAULT_NONE && output->action.value.fault_node == 0,
        "rounding query never evaluates operands or invents a constant/fault");
    const CheckValue value = output->action.value;
    if (expected == CHECK_DIAG_NONE)
    {
        check_builtin_rounds_test_expect(test, value.type == BUSTER_TYPE_INT && value.category == CHECK_RVALUE && value.flags == BUSTER_CHECK_SIDE_EFFECT &&
            value.integer == 0 && value.floating == 0 && value.object == 0 && value.offset == 0 && value.bit_width == 0 &&
            output->runtime_builtin == CBUILTIN_FLT_ROUNDS && output->compiler_barrier && output->reads_fp_environment,
            "valid query is a nonconstant runtime int with a floating-environment ordering barrier");
    }
    else
    {
        check_builtin_rounds_test_expect(test, value.type == 0 && value.flags == 0 && output->runtime_builtin == CBUILTIN_NONE &&
            !output->compiler_barrier && !output->reads_fp_environment,
            "invalid query has no executable builtin or environment effect");
    }
    bool conversions_empty = output->action.store_conversion.type == 0 && output->action.store_conversion.flags == 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(output->action.conversions); i += 1)
    {
        conversions_empty &= output->action.conversions[i].type == 0 && output->action.conversions[i].flags == 0;
    }
    check_builtin_rounds_test_expect(test, conversions_empty, "zero-argument query has no source conversions");
}

static void check_builtin_rounds_test_values(CheckBuiltinRoundsTest* test)
{
    const CBuiltinId identities[] = { CBUILTIN_FLT_ROUNDS, CBUILTIN_NONE, CBUILTIN_CLZ, CBUILTIN_TRAP, (CBuiltinId)UINT32_MAX };
    const uint32_t counts[] = { 0, 1, 2, UINT32_MAX };
    CheckValue decoy =
    {
        .type = BUSTER_TYPE_DOUBLE,
        .category = CHECK_LVALUE,
        .flags = BUSTER_CHECK_ICE | BUSTER_CHECK_SIDE_EFFECT,
        .constant_kind = CHECK_CONSTANT_INTEGER,
        .integer = 3,
        .fault = CHECK_FAULT_DIVIDE_BY_ZERO,
        .fault_node = 999,
    };
    for (uint32_t id = 0; id < BUSTER_ARRAY_LENGTH(identities); id += 1)
    {
        for (uint32_t count = 0; count < BUSTER_ARRAY_LENGTH(counts); count += 1)
        {
            for (uint32_t has_arguments = 0; has_arguments < 2; has_arguments += 1)
            {
                const CheckBuiltinRoundsAction input =
                {
                    .id = identities[id],
                    .arguments = has_arguments ? &decoy : NULL,
                    .argument_count = counts[count],
                    .node = UINT32_MAX,
                };
                CheckBuiltinRoundsResult output;
                memset(&output, 0xa5, sizeof(output));
                check_builtin_rounds_one(&input, &output);
                check_builtin_rounds_test_result(test, &input, &output);
            }
        }
    }
    check_builtin_rounds(NULL, NULL, 0);
    check_builtin_rounds_test_expect(test, true, "empty batch permits null slices");
}

static void check_builtin_rounds_test_pages(CheckBuiltinRoundsTest* test)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    const size_t page = page_size > 0 ? (size_t)page_size : 0;
    const size_t bytes = page * 4;
    uint8_t* mapping = page != 0 ? mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED && BUSTER_CHECK_BUILTIN_ROUNDS_TEST_BATCH * sizeof(CheckBuiltinRoundsAction) + 32 <= page &&
        BUSTER_CHECK_BUILTIN_ROUNDS_TEST_BATCH * sizeof(CheckBuiltinRoundsResult) + 32 <= page;
    if (ready)
    {
        ready = mprotect(mapping + page, page, PROT_NONE) == 0 && mprotect(mapping + page * 3, page, PROT_NONE) == 0;
    }
    check_builtin_rounds_test_expect(test, ready, "guarded action/result fixture allocation");
    if (ready)
    {
        const CheckValue* inaccessible = (const CheckValue*)(mapping + page);
        check_builtin_rounds((const CheckBuiltinRoundsAction*)inaccessible, (CheckBuiltinRoundsResult*)(mapping + page * 3), 0);
        for (uint32_t count = 1; count <= BUSTER_CHECK_BUILTIN_ROUNDS_TEST_BATCH; count += 1)
        {
            uint8_t* input_end = mapping + page;
            uint8_t* output_end = mapping + page * 3;
            CheckBuiltinRoundsAction* inputs = (CheckBuiltinRoundsAction*)(input_end - count * sizeof(CheckBuiltinRoundsAction));
            CheckBuiltinRoundsResult* outputs = (CheckBuiltinRoundsResult*)(output_end - count * sizeof(CheckBuiltinRoundsResult));
            memset(mapping, 0xa5, page);
            for (uint32_t i = 0; i < count; i += 1)
            {
                inputs[i] = (CheckBuiltinRoundsAction)
                {
                    .id = i % 4 == 3 ? CBUILTIN_TRAP : CBUILTIN_FLT_ROUNDS,
                    .arguments = inaccessible,
                    .argument_count = i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : UINT32_MAX,
                    .node = i,
                };
            }
            CheckBuiltinRoundsAction before[BUSTER_CHECK_BUILTIN_ROUNDS_TEST_BATCH];
            memcpy(before, inputs, count * sizeof(*inputs));
            CheckBuiltinRoundsResult individual[BUSTER_CHECK_BUILTIN_ROUNDS_TEST_BATCH];
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_builtin_rounds_one(inputs + i, individual + i);
                check_builtin_rounds_test_result(test, inputs + i, individual + i);
            }
            for (uint32_t split = 0; split <= count; split += 1)
            {
                memset(mapping + page * 2, 0xa5, page);
                check_builtin_rounds(inputs, outputs, split);
                check_builtin_rounds(inputs + split, outputs + split, count - split);
                bool equal = true;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    equal &= check_builtin_rounds_test_equal(outputs + i, individual + i);
                }
                check_builtin_rounds_test_expect(test, equal, "guarded batch and every two-part partition match individual actions");
                bool canary = true;
                for (uint32_t i = 1; i <= 32; i += 1)
                {
                    canary &= ((const uint8_t*)inputs)[-(ptrdiff_t)i] == 0xa5 && ((const uint8_t*)outputs)[-(ptrdiff_t)i] == 0xa5;
                }
                check_builtin_rounds_test_expect(test, canary && memcmp(inputs, before, count * sizeof(*inputs)) == 0,
                    "bounded writes preserve prefix guards and immutable actions");
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, bytes);
    }
}

static void check_builtin_rounds_test_clang(CheckBuiltinRoundsTest* test, const char* clang)
{
    static const struct
    {
        const char* source;
        bool rejected;
    } cases[] =
    {
        { "int f(void) { return __builtin_flt_rounds(); }\n", false },
        { "_Static_assert(_Generic(__builtin_flt_rounds(), int:1, default:0), \"int result\");\n", false },
        { "_Static_assert(sizeof(__builtin_flt_rounds()) == sizeof(int), \"int size\");\n", false },
        { "_Static_assert(__builtin_constant_p(__builtin_flt_rounds()) == 0, \"runtime query\");\n", false },
        { "_Static_assert(__builtin_flt_rounds() == 1, \"not an ICE\");\n", true },
        { "enum E { e = __builtin_flt_rounds() };\n", true },
        { "int f(void) { return __builtin_flt_rounds(0); }\n", true },
        { "int f(void) { return __builtin_flt_rounds(1.0); }\n", true },
        { "int f(void) { return __builtin_flt_rounds(0,1); }\n", true },
    };
    char directory[] = "/tmp/buster-check-builtin-rounds-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    bool ready = created;
    char paths[BUSTER_ARRAY_LENGTH(cases)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(cases)];
    ParseTestClang results[BUSTER_ARRAY_LENGTH(cases)];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], cases[i].source, strlen(cases[i].source));
    }
    const char* standards[] = { "-std=gnu17", "-std=gnu23" };
    for (uint32_t mode = 0; mode < BUSTER_ARRAY_LENGTH(standards); mode += 1)
    {
        const char* arguments[] = { standards[mode] };
        const bool ran = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, BUSTER_ARRAY_LENGTH(cases), results);
        check_builtin_rounds_test_expect(test, ran, "independent Clang rounding query source assertions and constraints ran");
        for (uint32_t i = 0; ran && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
        {
            check_builtin_rounds_test_expect(test, results[i].rejected == cases[i].rejected, "Clang agrees with original rounding-query source fixture");
        }
    }
    for (uint32_t i = 0; created && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
}

static int run_check_builtin_rounds_tests(int argc, char** argv)
{
    const bool epoch = oracle_epoch_begin();
    CheckBuiltinRoundsTest test = { 0 };
    const char* clang = "clang";
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            i += 1;
            clang = argv[i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    check_builtin_rounds_test_expect(&test, options, "usage: test-check-builtins-rounds [--clang PATH]");
    if (options)
    {
        check_builtin_rounds_test_values(&test);
        check_builtin_rounds_test_pages(&test);
        check_builtin_rounds_test_clang(&test, clang);
    }
    printf("test-check-builtins-rounds: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    oracle_cache_report(oracle_cache(), "test-check-builtins-rounds");
    oracle_epoch_end(epoch);
    return result;
}
