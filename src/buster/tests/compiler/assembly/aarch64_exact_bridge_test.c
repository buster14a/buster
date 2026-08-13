#include <buster/tests/compiler/assembly/aarch64_exact_bridge_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_exact_bridge.h>
#include <buster/lib/compiler/assembly/generated/aarch64-exact-crosswalk.generated.h>

UnitTestResult aarch64_exact_bridge_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, a64_exact_crosswalk_count() == BUSTER_AARCH64_EXACT_CROSSWALK_COUNT);
    for (u32 index = 0; index < a64_exact_crosswalk_count(); index += 1)
    {
        A64ExactCrosswalkEntry entry = {0};
        A64ExactFormKey key = {0};
        BUSTER_TEST(arguments, a64_exact_crosswalk(index, &entry) && a64_exact_key(index, &key) &&
                                 key.form_index == entry.canonical.form_index && key.row_digest == entry.canonical.row_digest &&
                                 a64_exact_key_valid(key));
        A64ExactFormKey lookup = {0};
        BUSTER_TEST(arguments, a64_exact_lookup(entry.llvm_name, &lookup) && lookup.form_index == key.form_index &&
                                 lookup.row_digest == key.row_digest);
    }

    // Differentially check the three projections whose LLVM field shape is
    // not Arm's canonical normalized field list.
    {
        u32 source[] = {3, 5, 7, 9};
        u32 normalized[4] = {0};
        u32 count = 0;
        BUSTER_TEST(arguments, a64_exact_normalize_ubfm_wri(source, 4, normalized, 4, &count) && count == 4 &&
                                 memcmp(source, normalized, sizeof(source)) == 0);
        A64ExactFormKey key = {0};
        BUSTER_TEST(arguments, a64_exact_lookup(S8("UBFMWri"), &key));
        u32 old_word = 0;
        u32 exact_word = 0;
        BUSTER_TEST(arguments, buster_aarch64_production_raw_encode(UINT32_C(6392), source, 4, &old_word) &&
                                 a64_exact_emit(key, normalized, count, &exact_word) && old_word == exact_word);
        source[2] = 32;
        BUSTER_TEST(arguments, !a64_exact_normalize_ubfm_wri(source, 4, normalized, 4, &count));
    }
    {
        u32 source[] = {31, 5, 0x1005};
        u32 normalized[4] = {0};
        u32 count = 0;
        BUSTER_TEST(arguments, a64_exact_normalize_subs_xri(source, 3, normalized, 4, &count) && count == 4 &&
                                 normalized[2] == 5 && normalized[3] == 1);
        A64ExactFormKey key = {0};
        BUSTER_TEST(arguments, a64_exact_lookup(S8("SUBSXri"), &key));
        u32 old_word = 0;
        u32 exact_word = 0;
        BUSTER_TEST(arguments, buster_aarch64_production_raw_encode(UINT32_C(6063), source, 3, &old_word) &&
                                 a64_exact_emit(key, normalized, count, &exact_word) && old_word == exact_word);
        source[2] = 0x4000;
        BUSTER_TEST(arguments, !a64_exact_normalize_subs_xri(source, 3, normalized, 4, &count));
    }
    {
        u32 source[] = {3, 31, 0x1003};
        u32 normalized[4] = {0};
        u32 count = 0;
        BUSTER_TEST(arguments, a64_exact_normalize_add_xri(source, 3, normalized, 4, &count) && count == 4 &&
                                 normalized[2] == 3 && normalized[3] == 1);
        A64ExactFormKey key = {0};
        BUSTER_TEST(arguments, a64_exact_lookup(S8("ADDXri"), &key));
        u32 old_word = 0;
        u32 exact_word = 0;
        BUSTER_TEST(arguments, buster_aarch64_production_raw_encode(UINT32_C(87), source, 3, &old_word) &&
                                 a64_exact_emit(key, normalized, count, &exact_word) && old_word == exact_word);
        source[2] = 0x2003;
        BUSTER_TEST(arguments, !a64_exact_normalize_add_xri(source, 3, normalized, 4, &count));
    }

    A64ExactFormKey bad = {.form_index = 0, .row_digest = UINT64_C(1)};
    u32 word = 0;
    BUSTER_TEST(arguments, !a64_exact_key_valid(bad) && !a64_exact_emit(bad, (u32[1]){0}, 1, &word));
    return result;
}

#endif
