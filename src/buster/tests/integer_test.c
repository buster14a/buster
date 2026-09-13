#include <buster/tests/integer_test.h>
#include <buster/lib/integer.h>

#if BUSTER_INCLUDE_TESTS
UnitTestResult integer_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 aligned = UINT64_MAX;
    bool valid = align_forward_checked(0, 0, &aligned);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    aligned = UINT64_MAX;
    valid = align_forward_checked(9, 3, &aligned);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    u64 values[] = {0, 1, 7, 8, 9, 63, 64, 65};
    u64 alignments[] = {1, 2, 4, 8, 16, 32, 64, 128};
    u64 expected[] = {0, 2, 8, 8, 16, 64, 64, 128};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
    {
        aligned = UINT64_MAX;
        valid = align_forward_checked(values[index], alignments[index], &aligned);
        BUSTER_TEST(arguments, valid && aligned == expected[index]);
    }

    aligned = UINT64_MAX;
    valid = align_forward_checked(1, UINT64_C(1) << 63, &aligned);
    BUSTER_TEST(arguments, valid && aligned == (UINT64_C(1) << 63));

    aligned = UINT64_MAX;
    valid = align_forward_checked((UINT64_C(1) << 63) + 1, UINT64_C(1) << 63, &aligned);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    aligned = UINT64_MAX;
    valid = align_forward_checked(UINT64_MAX, 2, &aligned);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    aligned = UINT64_MAX;
    valid = align_forward_checked(1, 8, 0);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    BUSTER_TEST(arguments, align_forward(9, 8) == 16);
    BUSTER_TEST(arguments, is_aligned(0, 1) && is_aligned(64, 64) && !is_aligned(65, 64));
    return result;
}
#endif
