// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent byte-routing checks for #106. Include after c_fragment_interleave.c. Each case starts with known
// output bytes and partitions them into operator/leaf input slices; both kernels must reproduce the original
// bytes and leave the surrounding output untouched. Unique bytes within a case expose reordered or reused lanes.
// Guarded cases end all three slices exactly at separate PROT_NONE pages, including each empty input and output.
// These tests establish the routing contract only, not fragment construction or semantic-consumer correctness.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_C_FRAGMENT_INTERLEAVE_TEST_RANDOM_CASES 4096u
#define BUSTER_C_FRAGMENT_INTERLEAVE_TEST_PADDING 64u

typedef struct CFragmentInterleaveTest CFragmentInterleaveTest;
struct CFragmentInterleaveTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t routing_cases;
    uint64_t guarded_cases;
    uint64_t state;
    uint64_t mask;
    uint32_t count;
};

static void c_fragment_interleave_test_check(CFragmentInterleaveTest* test, bool condition, const char* description)
{
    test->checks += 1;
    test->failures += !condition;
    if (!condition && test->failures <= 12)
    {
        fprintf(stderr, "fragment-interleave: %s (count %u, mask 0x%016llx)\n", description, test->count, (unsigned long long)test->mask);
    }
}

// Two high halves of an LCG avoid the short periods of its low bits, including for a seed of zero.
static uint64_t c_fragment_interleave_test_random(CFragmentInterleaveTest* test)
{
    test->state = test->state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    const uint64_t high = test->state & UINT64_C(0xffffffff00000000);
    test->state = test->state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return high | (test->state >> 32);
}

// Construct the expected masks a bit at a time, independently of the production shift/mask helper.
static uint64_t c_fragment_interleave_test_mask(uint32_t count)
{
    uint64_t result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result |= UINT64_C(1) << i;
    }
    return result;
}

static void c_fragment_interleave_test_inputs(CFragmentInterleaveTest* test, uint8_t* operators, uint8_t* leaves, uint8_t* expected)
{
    const uint8_t first = (uint8_t)c_fragment_interleave_test_random(test);
    const uint8_t stride = (uint8_t)c_fragment_interleave_test_random(test) | 1;
    uint32_t operator_count = 0;
    uint32_t leaf_count = 0;
    for (uint32_t i = 0; i < test->count; i += 1)
    {
        const uint8_t value = (uint8_t)(first + i * stride);
        expected[i] = value;
        if (test->mask & (UINT64_C(1) << i))
        {
            operators[operator_count++] = value;
        }
        else
        {
            leaves[leaf_count++] = value;
        }
    }
}

static void c_fragment_interleave_test_case(CFragmentInterleaveTest* test, uint32_t count, uint64_t mask)
{
    test->count = count;
    test->mask = mask;
    // The odd multipliers cover every byte alignment for each of the three independently placed slices.
    const uint32_t operator_offset = (uint32_t)(test->routing_cases * 17) % BUSTER_C_FRAGMENT_INTERLEAVE_TEST_PADDING;
    const uint32_t leaf_offset = (uint32_t)(test->routing_cases * 29 + 7) % BUSTER_C_FRAGMENT_INTERLEAVE_TEST_PADDING;
    const uint32_t output_offset = (uint32_t)(test->routing_cases * 11 + 3) % BUSTER_C_FRAGMENT_INTERLEAVE_TEST_PADDING;
    uint8_t operator_storage[BUSTER_C_FRAGMENT_INTERLEAVE_BYTES + BUSTER_C_FRAGMENT_INTERLEAVE_TEST_PADDING];
    uint8_t leaf_storage[sizeof(operator_storage)];
    uint8_t saved_operators[sizeof(operator_storage)];
    uint8_t saved_leaves[sizeof(leaf_storage)];
    uint8_t expected[BUSTER_C_FRAGMENT_INTERLEAVE_BYTES + 2 * BUSTER_C_FRAGMENT_INTERLEAVE_TEST_PADDING];
    uint8_t scalar[sizeof(expected)];
    uint8_t vector[sizeof(expected)];
    uint8_t* operators = operator_storage + operator_offset;
    uint8_t* leaves = leaf_storage + leaf_offset;
    memset(operator_storage, 0x7c, sizeof(operator_storage));
    memset(leaf_storage, 0xb3, sizeof(leaf_storage));
    memset(expected, 0xa5, sizeof(expected));
    memset(scalar, 0xa5, sizeof(scalar));
    memset(vector, 0xa5, sizeof(vector));
    c_fragment_interleave_test_inputs(test, operators, leaves, expected + output_offset);
    memcpy(saved_operators, operator_storage, sizeof(operator_storage));
    memcpy(saved_leaves, leaf_storage, sizeof(leaf_storage));
    const bool scalar_valid = c_fragment_interleave_scalar(operators, leaves, mask, count, scalar + output_offset);
    const bool vector_valid = c_fragment_interleave(operators, leaves, mask, count, vector + output_offset);
    c_fragment_interleave_test_check(test, scalar_valid && vector_valid, "valid routing rejected");
    c_fragment_interleave_test_check(test, memcmp(scalar, expected, sizeof(expected)) == 0, "scalar routing or untouched output boundary differs");
    c_fragment_interleave_test_check(test, memcmp(vector, expected, sizeof(expected)) == 0, "vector routing or untouched output boundary differs");
    c_fragment_interleave_test_check(test, memcmp(scalar, vector, sizeof(expected)) == 0, "scalar and vector differ");
    c_fragment_interleave_test_check(test, memcmp(operator_storage, saved_operators, sizeof(operator_storage)) == 0, "operator input was modified");
    c_fragment_interleave_test_check(test, memcmp(leaf_storage, saved_leaves, sizeof(leaf_storage)) == 0, "leaf input was modified");
    test->routing_cases += 1;
}

static void c_fragment_interleave_test_routing(CFragmentInterleaveTest* test)
{
    for (uint32_t count = 0; count <= 12; count += 1)
    {
        for (uint64_t mask = 0; mask < (UINT64_C(1) << count); mask += 1)
        {
            c_fragment_interleave_test_case(test, count, mask);
        }
    }
    for (uint32_t count = 0; count <= BUSTER_C_FRAGMENT_INTERLEAVE_BYTES; count += 1)
    {
        const uint64_t valid_mask = c_fragment_interleave_test_mask(count);
        test->count = count;
        test->mask = valid_mask;
        c_fragment_interleave_test_check(test, c_fragment_interleave_mask(count) == valid_mask, "valid-mask helper differs");
        c_fragment_interleave_test_case(test, count, 0);
        c_fragment_interleave_test_case(test, count, valid_mask);
        for (uint32_t sample = 0; sample < BUSTER_C_FRAGMENT_INTERLEAVE_TEST_RANDOM_CASES; sample += 1)
        {
            const uint64_t mask = c_fragment_interleave_test_random(test) & valid_mask;
            c_fragment_interleave_test_case(test, count, mask);
        }
    }
}

static void c_fragment_interleave_test_slice_case(CFragmentInterleaveTest* test, const uint8_t* operators, const uint8_t* leaves, uint64_t mask, uint32_t count, const uint8_t* expected_live)
{
    test->count = count;
    test->mask = mask;
    uint8_t expected[BUSTER_C_FRAGMENT_INTERLEAVE_BYTES + 2];
    uint8_t scalar[sizeof(expected)];
    uint8_t vector[sizeof(expected)];
    memset(expected, 0xa5, sizeof(expected));
    memset(scalar, 0xa5, sizeof(scalar));
    memset(vector, 0xa5, sizeof(vector));
    memcpy(expected + 1, expected_live, count);
    const bool scalar_valid = c_fragment_interleave_scalar(operators, leaves, mask, count, scalar + 1);
    const bool vector_valid = c_fragment_interleave(operators, leaves, mask, count, vector + 1);
    c_fragment_interleave_test_check(test, scalar_valid && vector_valid, "null or overlapping input slices rejected");
    c_fragment_interleave_test_check(test, memcmp(scalar, expected, sizeof(expected)) == 0, "scalar null or overlapping slice result differs");
    c_fragment_interleave_test_check(test, memcmp(vector, expected, sizeof(expected)) == 0, "vector null or overlapping slice result differs");
    test->routing_cases += 1;
}

static void c_fragment_interleave_test_slices(CFragmentInterleaveTest* test)
{
    uint8_t input[BUSTER_C_FRAGMENT_INTERLEAVE_BYTES];
    for (uint32_t i = 0; i < sizeof(input); i += 1)
    {
        input[i] = (uint8_t)(17 + i * 3);
    }
    const uint32_t counts[] =
    {
        1,
        BUSTER_C_FRAGMENT_INTERLEAVE_BYTES,
    };
    for (uint32_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i += 1)
    {
        const uint32_t count = counts[i];
        c_fragment_interleave_test_slice_case(test, 0, input, 0, count, input);
        c_fragment_interleave_test_slice_case(test, input, 0, c_fragment_interleave_test_mask(count), count, input);
    }

    // Operators use shared[0..5), leaves use shared[2..6): both read the overlapping three bytes in their own order.
    const uint8_t shared[] =
    {
        11,
        22,
        33,
        44,
        55,
        66,
    };
    const uint8_t expected[] =
    {
        11,
        33,
        22,
        44,
        33,
        55,
        44,
        66,
        55,
    };
    c_fragment_interleave_test_slice_case(test, shared, shared + 2, UINT64_C(0x155), sizeof(expected), expected);
}

static void c_fragment_interleave_test_guard_case(CFragmentInterleaveTest* test, uint8_t* mapping, size_t page, uint32_t count, uint64_t mask)
{
    test->count = count;
    test->mask = mask;
    uint32_t operator_count = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        operator_count += (mask >> i) & 1;
    }
    uint8_t* operators = mapping + page - operator_count;
    uint8_t* leaves = mapping + 3 * page - (count - operator_count);
    uint8_t* output_end = mapping + 5 * page;
    uint8_t* output = output_end - count;
    uint8_t expected[BUSTER_C_FRAGMENT_INTERLEAVE_BYTES];
    uint8_t expected_operators[BUSTER_C_FRAGMENT_INTERLEAVE_BYTES];
    uint8_t expected_leaves[BUSTER_C_FRAGMENT_INTERLEAVE_BYTES];
    memset(expected, 0xa5, sizeof(expected));
    c_fragment_interleave_test_inputs(test, operators, leaves, expected + sizeof(expected) - count);
    if (operator_count != 0)
    {
        memcpy(expected_operators, operators, operator_count);
    }
    if (count != operator_count)
    {
        memcpy(expected_leaves, leaves, count - operator_count);
    }
    // The readable prefix checks for writes before output; the following page catches every write after it.
    memset(output_end - sizeof(expected), 0xa5, sizeof(expected));
    c_fragment_interleave_test_check(test, c_fragment_interleave_scalar(operators, leaves, mask, count, output), "guarded scalar routing rejected");
    c_fragment_interleave_test_check(test, memcmp(output_end - sizeof(expected), expected, sizeof(expected)) == 0, "guarded scalar output differs");
    memset(output_end - sizeof(expected), 0xa5, sizeof(expected));
    c_fragment_interleave_test_check(test, c_fragment_interleave(operators, leaves, mask, count, output), "guarded vector routing rejected");
    c_fragment_interleave_test_check(test, memcmp(output_end - sizeof(expected), expected, sizeof(expected)) == 0, "guarded vector output differs");
    c_fragment_interleave_test_check(test, operator_count == 0 || memcmp(operators, expected_operators, operator_count) == 0, "guarded operator input was modified");
    c_fragment_interleave_test_check(test, count == operator_count || memcmp(leaves, expected_leaves, count - operator_count) == 0, "guarded leaf input was modified");
    test->guarded_cases += 1;
}

static void c_fragment_interleave_test_invalid(CFragmentInterleaveTest* test, uint8_t* inaccessible)
{
    for (uint32_t count = 0; count < BUSTER_C_FRAGMENT_INTERLEAVE_BYTES; count += 1)
    {
        test->count = count;
        test->mask = UINT64_C(1) << count;
        c_fragment_interleave_test_check(test, !c_fragment_interleave_scalar(inaccessible, inaccessible, test->mask, count, inaccessible), "scalar accepted an inactive mask bit");
        c_fragment_interleave_test_check(test, !c_fragment_interleave(inaccessible, inaccessible, test->mask, count, inaccessible), "vector accepted an inactive mask bit");
    }
    const uint32_t invalid_counts[] =
    {
        BUSTER_C_FRAGMENT_INTERLEAVE_BYTES + 1,
        128,
        UINT32_MAX,
    };
    for (uint32_t i = 0; i < sizeof(invalid_counts) / sizeof(invalid_counts[0]); i += 1)
    {
        test->count = invalid_counts[i];
        test->mask = 0;
        c_fragment_interleave_test_check(test, !c_fragment_interleave_scalar(inaccessible, inaccessible, 0, test->count, inaccessible), "scalar accepted an oversized output count");
        c_fragment_interleave_test_check(test, !c_fragment_interleave(inaccessible, inaccessible, 0, test->count, inaccessible), "vector accepted an oversized output count");
    }
    test->count = 0;
    test->mask = 0;
    c_fragment_interleave_test_check(test, c_fragment_interleave_scalar(0, 0, 0, 0, 0), "scalar rejected null empty slices");
    c_fragment_interleave_test_check(test, c_fragment_interleave(0, 0, 0, 0, 0), "vector rejected null empty slices");
}

static void c_fragment_interleave_test_guards(CFragmentInterleaveTest* test)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    const size_t page = page_size > 0 ? (size_t)page_size : 0;
    uint8_t* mapping = MAP_FAILED;
    bool ready = page >= BUSTER_C_FRAGMENT_INTERLEAVE_BYTES && page <= SIZE_MAX / 6;
    if (ready)
    {
        mapping = mmap(0, page * 6, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mapping != MAP_FAILED;
    }
    for (uint32_t i = 1; ready && i < 6; i += 2)
    {
        ready = mprotect(mapping + i * page, page, PROT_NONE) == 0;
    }
    c_fragment_interleave_test_check(test, ready, "guard-page setup failed");
    if (ready)
    {
        for (uint32_t count = 0; count <= BUSTER_C_FRAGMENT_INTERLEAVE_BYTES; count += 1)
        {
            const uint64_t valid_mask = c_fragment_interleave_test_mask(count);
            c_fragment_interleave_test_guard_case(test, mapping, page, count, 0);
            c_fragment_interleave_test_guard_case(test, mapping, page, count, valid_mask);
            c_fragment_interleave_test_guard_case(test, mapping, page, count, UINT64_C(0xaaaaaaaaaaaaaaaa) & valid_mask);
            const uint64_t random_mask = c_fragment_interleave_test_random(test) & valid_mask;
            c_fragment_interleave_test_guard_case(test, mapping, page, count, random_mask);
        }
        c_fragment_interleave_test_invalid(test, mapping + page);
    }
    if (mapping != MAP_FAILED)
    {
        c_fragment_interleave_test_check(test, munmap(mapping, page * 6) == 0, "guard-page release failed");
    }
}

static int run_fragment_interleave_tests(int argc, char** argv)
{
    CFragmentInterleaveTest test = { .state = 1 };
    bool arguments = argc == 0 || (argc == 2 && strcmp(argv[0], "--seed") == 0);
    if (arguments && argc == 2)
    {
        char* end = 0;
        errno = 0;
        test.state = strtoull(argv[1], &end, 0);
        arguments = errno == 0 && end != argv[1] && *end == 0 && argv[1][0] != '-';
    }
    int result = 1;
    if (arguments)
    {
        const uint64_t seed = test.state;
        c_fragment_interleave_test_routing(&test);
        c_fragment_interleave_test_slices(&test);
        c_fragment_interleave_test_guards(&test);
        printf("fragment-interleave: %llu routing cases, %llu guarded cases, %llu checks, %llu failures (seed %llu)\n", (unsigned long long)test.routing_cases, (unsigned long long)test.guarded_cases, (unsigned long long)test.checks, (unsigned long long)test.failures, (unsigned long long)seed);
        result = test.failures != 0;
    }
    else
    {
        fprintf(stderr, "usage: test-fragment-interleave [--seed N]\n");
    }
    return result;
}
