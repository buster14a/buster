#include <buster/tests/hash_test.h>
BUSTER_TEST_F_DECL UnitTestResult hash_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};

    // 0 is reserved as an "empty" sentinel by hash consumers; no input may
    // hash to it, and equal inputs must hash equally.
    BUSTER_TEST(arguments, buster_hash_64(0, 0) != 0);

    u8 zero_byte[] = {0};
    BUSTER_TEST(arguments, buster_hash_64(zero_byte, sizeof(zero_byte)) != 0);

    String8 sample = S8("sample");
    u64 first = buster_hash_64((u8*)sample.pointer, sample.length);
    BUSTER_TEST(arguments, first != 0);
    BUSTER_TEST(arguments, first == buster_hash_64((u8*)sample.pointer, sample.length));

    String8 other = S8("sampl3");
    BUSTER_TEST(arguments, first != buster_hash_64((u8*)other.pointer, other.length));

    return result;
}
