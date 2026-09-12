#include <buster/tests/hash_test.h>
#if BUSTER_INCLUDE_TESTS
UnitTestResult hash_tests(UnitTestArguments* arguments)
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

    // Independent known answers cover both padding branches, multiple blocks,
    // empty/null updates and every split of each bounded binary message.
    {
        u8 bytes[129];
        for (u32 i = 0; i < sizeof(bytes); i += 1) bytes[i] = (u8)i;
        u32 lengths[] = {0, 1, 55, 56, 63, 64, 65, 127, 128, 129};
        String8 expected[] = {
            S8("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
            S8("6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d"),
            S8("463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"),
            S8("da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"),
            S8("29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488"),
            S8("fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"),
            S8("4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"),
            S8("92ca0fa6651ee2f97b884b7246a562fa71250fedefe5ebf270d31c546bfea976"),
            S8("471fb943aa23c511f6f72f8d1652d9c880cfa392ad80503120547703e56a2be5"),
            S8("5099c6a56203f9687f7d33f4bfdf576d31dc91f6b695ecea38b2770c87631135"),
        };
        for (u32 vector = 0; vector < BUSTER_ARRAY_LENGTH(lengths); vector += 1)
        {
            for (u32 split = 0; split <= lengths[vector]; split += 1)
            {
                Sha256 state;
                char8 digest[SHA256_HEX_CAPACITY];
                sha256_init(&state);
                sha256_add(&state, 0, 0);
                sha256_add(&state, bytes, split);
                sha256_add(&state, bytes + split, lengths[vector] - split);
                sha256_finish_hex(&state, digest);
                BUSTER_STRING_TEST(arguments, string_from_pointer(digest), expected[vector]);
            }
        }
        Sha256 state;
        char8 digest[SHA256_HEX_CAPACITY];
        sha256_init(&state);
        sha256_add(&state, "abc", 3);
        sha256_finish_hex(&state, digest);
        BUSTER_STRING_TEST(arguments, string_from_pointer(digest), S8("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        sha256_init(&state);
        u8 chunk[1000];
        memset(chunk, 'a', sizeof(chunk));
        for (u32 i = 0; i < 1000; i += 1) sha256_add(&state, chunk, sizeof(chunk));
        sha256_finish_hex(&state, digest);
        BUSTER_STRING_TEST(arguments, string_from_pointer(digest), S8("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    }

    return result;
}
#endif
