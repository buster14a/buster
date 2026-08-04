#include <buster/tests/compiler/assembly/x86_64_metadata_test.h>

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_string_equal(BusterX86MetadataString first, String8 second)
{
    if (first.length != second.length || (!second.pointer && second.length)) return false;
    for (u32 index = 0; index < first.length; index += 1)
    {
        if (buster_x86_metadata_string_byte(first, index) != (u8)second.pointer[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_string8(BusterX86MetadataString value, char8* buffer, u32 capacity, String8* result)
{
    if (!buffer || !result || value.length >= capacity) return false;
    for (u32 index = 0; index < value.length; index += 1) buffer[index] = (char8)buster_x86_metadata_string_byte(value, index);
    *result = (String8){.pointer = buffer, .length = value.length};
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_candidate(BusterX86MetadataCandidateRange range, u32 position, u32* form_id)
{
    return buster_x86_metadata_candidate_at(range, position, form_id);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_coverage_candidate(BusterX86MetadataCoverageRange range, u32 position,
                                                                  u32* coverage_id)
{
    return buster_x86_metadata_coverage_candidate_at(range, position, coverage_id);
}

BUSTER_GLOBAL_LOCAL u32 x86_64_metadata_test_visible_operand_count(u32 form_id, u16 operand_count)
{
    u32 visible_count = 0;
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (buster_x86_metadata_operand(form_id, operand_index, &operand)) visible_count += operand.visible != 0;
    }
    return visible_count;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_range_is_sorted(BusterX86MetadataCandidateRange range)
{
    u32 previous = 0;
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 current = 0;
        if (!x86_64_metadata_test_candidate(range, index, &current) || (index && current <= previous)) return false;
        previous = current;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_range_contains_iclass(BusterX86MetadataCandidateRange range, String8 iclass)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 form_id = 0;
        BusterX86MetadataForm form = {0};
        if (!x86_64_metadata_test_candidate(range, index, &form_id) || !buster_x86_metadata_form(form_id, &form)) return false;
        if (x86_64_metadata_test_string_equal(form.iclass, iclass)) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u32 x86_64_metadata_test_filtered_count(BusterX86MetadataCandidateRange range, BusterX86MetadataFilter filter,
                                                             u32 expected_form_id, bool* found)
{
    BusterX86MetadataCandidateIterator iterator = buster_x86_metadata_filter(range, filter);
    u32 count = 0;
    *found = false;
    u32 form_id = 0;
    while (buster_x86_metadata_candidate_next(&iterator, &form_id))
    {
        count += 1;
        *found |= form_id == expected_form_id;
    }
    return count;
}

#if !BUSTER_SINGLE_THREADED
typedef struct X86_64MetadataConcurrentLookupState X86_64MetadataConcurrentLookupState;
struct X86_64MetadataConcurrentLookupState
{
    atomic_bool start;
    atomic_bool failed;
};

BUSTER_GLOBAL_LOCAL void x86_64_metadata_test_concurrent_lookup(void* argument)
{
    X86_64MetadataConcurrentLookupState* state = (X86_64MetadataConcurrentLookupState*)argument;
    while (!atomic_load_explicit(&state->start, memory_order_acquire))
    {
    }
    for (u32 iteration = 0; iteration < 64; iteration += 1)
    {
        BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_mnemonic(S8("mov"));
        u32 form_id = 0;
        if (!range.count || !x86_64_metadata_test_candidate(range, 0, &form_id) || form_id >= buster_x86_metadata_form_count())
        {
            atomic_store_explicit(&state->failed, true, memory_order_release);
            return;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_concurrent_lookup_stress(void)
{
    X86_64MetadataConcurrentLookupState state = {0};
    atomic_init(&state.start, false);
    atomic_init(&state.failed, false);
    OsThreadHandle* threads[8] = {0};
    u32 thread_count = 0;
    bool created = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(threads); index += 1)
    {
        threads[index] = os_thread_create((ThreadCreateOptions){
            .callback = &x86_64_metadata_test_concurrent_lookup,
            .argument = &state,
        });
        if (!threads[index])
        {
            created = false;
            break;
        }
        thread_count += 1;
    }
    atomic_store_explicit(&state.start, true, memory_order_release);
    for (u32 index = 0; index < thread_count; index += 1) created &= os_thread_join(threads[index]);
    return created && !atomic_load_explicit(&state.failed, memory_order_acquire);
}
#endif

UnitTestResult x86_64_metadata_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BusterX86MetadataCounts counts = buster_x86_metadata_counts();
    BusterX86MetadataValidationResult validation = {0};
    BUSTER_TEST(arguments, buster_x86_metadata_schema_version() == 2);
    BUSTER_TEST(arguments, buster_x86_metadata_form_count() == 11013);
    BUSTER_TEST(arguments, buster_x86_metadata_normalized_form_count() == 10636);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage_count() == 11013);
    BUSTER_TEST(arguments, buster_x86_metadata_operand_count() == 32813);
    BUSTER_TEST(arguments, buster_x86_metadata_string_pool_size() == 1726254);
    BUSTER_TEST(arguments, buster_x86_metadata_validate(&validation) && validation.valid &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_NONE);
    BUSTER_TEST(arguments, counts.total_form_count == 11013 && counts.normalized_form_count == 10636 && counts.coverage_count == 11013);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_DIRECT] == 0);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_NORMALIZED] == 10636);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_NOT64] == 268);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_PRIVILEGED] == 109);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_RESERVED] == 0);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_UNSUPPORTED_TOKEN] == 0);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_UNCLASSIFIED] == 0);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_NONE] == 10636);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_MODE_NOT64] == 268);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_CPL0] == 109);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_UNKNOWN_PATTERN_TOKEN] == 0);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_UNKNOWN_OPERAND_TOKEN] == 0);

    BusterX86MetadataForm first_form = {0};
    BusterX86MetadataForm last_form = {0};
    BusterX86MetadataCoverage first_coverage = {0};
    BusterX86MetadataCoverage last_coverage = {0};
    BUSTER_TEST(arguments, buster_x86_metadata_form(0, &first_form) && first_form.id == 0);
    BUSTER_TEST(arguments, buster_x86_metadata_form(11012, &last_form) && last_form.id == 11012);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage(0, &first_coverage) && first_coverage.id == 0);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage(11012, &last_coverage) && last_coverage.id == 11012);
    BUSTER_TEST(arguments, !buster_x86_metadata_form(11013, &first_form));
    BUSTER_TEST(arguments, !buster_x86_metadata_coverage(11013, &first_coverage));
    BUSTER_TEST(arguments, !buster_x86_metadata_form(0, 0) && !buster_x86_metadata_coverage(0, 0));

    BusterX86MetadataString pool_string = {0};
    u32 pool_size = buster_x86_metadata_string_pool_size();
    BUSTER_TEST(arguments, buster_x86_metadata_string(0, &pool_string) && pool_string.length == 0);
    BUSTER_TEST(arguments, buster_x86_metadata_string(pool_size - 1, &pool_string) && pool_string.length == 0);
    BUSTER_TEST(arguments, !buster_x86_metadata_string(pool_size, &pool_string));
    BUSTER_TEST(arguments, !buster_x86_metadata_string(UINT32_MAX, &pool_string));
    BusterX86MetadataOperand operand = {0};
    BUSTER_TEST(arguments, !buster_x86_metadata_operand(0, first_form.operand_count, &operand));
    BUSTER_TEST(arguments, !buster_x86_metadata_operand(11013, 0, &operand));

    BusterX86MetadataValidationPatch patch = {
        .kind = BUSTER_X86_METADATA_PATCH_FORM_SOURCE_OFFSET,
        .index = 0,
        .value = 0xffffffffu,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_STRING_OFFSET);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_FORM_ICLASS_OFFSET,
        .index = 0,
        .value = 0xffffffffu,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_STRING_OFFSET);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_STABLE_HASH, .index = 0, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_FORM_HASH);
    patch.value = 1;
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_FORM_HASH);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_FORM_OPERAND_RANGE,
        .index = 0,
        .value = ((u64)32813 << 32) | 1,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_OPERAND_RANGE);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_COVERAGE_CLASS, .index = 0, .value = 0xff};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_RESERVED, .index = 0, .value = 1};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);
    patch.value = 0x100;
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);
    patch.value = 0x10000;
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_RESERVED2, .index = 0, .value = 1};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_OPERAND_RESERVED, .index = 0, .value = 0x010203};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_RESERVED);

    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_SOURCE_HASH, .index = 0, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_HASH);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_FORM_ID, .index = 0, .value = 11013};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_FORM_ID);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_FORM_ID, .index = 1, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_FORM_ID);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_SOURCE_OFFSET, .index = 0, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_SOURCE);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_REASON_OFFSET, .index = 0, .value = 1};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_REASON);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_COVERAGE_REASON_ID,
        .index = 0,
        .value = BUSTER_X86_METADATA_REASON_CPL0,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_REASON);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_COVERAGE_ENCODER_FAMILY,
        .index = 0,
        .value = BUSTER_X86_METADATA_ENCODER_SYSTEM,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_CLASSIFICATION);
    patch = (BusterX86MetadataValidationPatch){
        .kind = BUSTER_X86_METADATA_PATCH_COVERAGE_TEST_CLASS,
        .index = 0,
        .value = BUSTER_X86_METADATA_TEST_PRIVILEGED_SCHEMA,
    };
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_COVERAGE_CLASSIFICATION);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_COVERAGE_CLASS, .index = 0, .value = 0xff};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_INDEX_CAPACITY, .index = 0, .value = 0};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY);

#if !BUSTER_SINGLE_THREADED
    BUSTER_TEST(arguments, x86_64_metadata_test_concurrent_lookup_stress());
#endif

    BusterX86MetadataCandidateRange mov = buster_x86_metadata_lookup_mnemonic(S8("MOV"));
    BusterX86MetadataCandidateRange mov_iclass = buster_x86_metadata_lookup_iclass(S8("MOV"));
    BusterX86MetadataCandidateRange mov_again = buster_x86_metadata_lookup_mnemonic(S8("mov"));
    BUSTER_TEST(arguments, mov.count > 0 && mov.first == mov_again.first && mov.count == mov_again.count &&
                               mov.index_kind == mov_again.index_kind && mov.index_kind != mov_iclass.index_kind);
    BUSTER_TEST(arguments, x86_64_metadata_test_range_is_sorted(mov) &&
                               x86_64_metadata_test_range_contains_iclass(mov, S8("MOV")));
    BUSTER_TEST(arguments, buster_x86_metadata_lookup_mnemonic(S8("not_an_x86_mnemonic")).count == 0);

    BusterX86MetadataCandidateRange call = buster_x86_metadata_lookup_mnemonic(S8("call"));
    BusterX86MetadataCandidateRange ret = buster_x86_metadata_lookup_mnemonic(S8("ret"));
    BusterX86MetadataCandidateRange call_iclass = buster_x86_metadata_lookup_iclass(S8("CALL_NEAR"));
    BusterX86MetadataCandidateRange ret_iclass = buster_x86_metadata_lookup_iclass(S8("RET_NEAR"));
    BusterX86MetadataCandidateRange xop = buster_x86_metadata_lookup_mnemonic(S8("VPMACSSWW"));
    BusterX86MetadataCandidateRange avx512 = buster_x86_metadata_lookup_mnemonic(S8("V4FMADDPS"));
    BusterX86MetadataCandidateRange system = buster_x86_metadata_lookup_mnemonic(S8("WRMSR"));
    BusterX86MetadataCandidateRange amx = buster_x86_metadata_lookup_mnemonic(S8("TILELOADD"));
    BusterX86MetadataCandidateRange avx10 = buster_x86_metadata_lookup_iform(
        S8("VCVTBF42HF8_XMMhf8_MASKmskw_XMMbf4_AVX512"));
    BusterX86MetadataCandidateRange apx = buster_x86_metadata_lookup_iform(S8("LDTILECFG_MEM_APX"));
    BUSTER_TEST(arguments, call.count > 0 && ret.count > 0 && call_iclass.count > 0 && ret_iclass.count > 0 && xop.count > 0 &&
                               avx512.count > 0 && system.count > 0 && amx.count > 0 && avx10.count > 0 && apx.count > 0);
    BUSTER_TEST(arguments, x86_64_metadata_test_range_is_sorted(call) && x86_64_metadata_test_range_is_sorted(ret) &&
                               x86_64_metadata_test_range_is_sorted(call_iclass) && x86_64_metadata_test_range_is_sorted(ret_iclass) &&
                               x86_64_metadata_test_range_is_sorted(xop) && x86_64_metadata_test_range_is_sorted(avx512) &&
                               x86_64_metadata_test_range_is_sorted(system) && x86_64_metadata_test_range_is_sorted(amx) &&
                               x86_64_metadata_test_range_is_sorted(avx10) && x86_64_metadata_test_range_is_sorted(apx));
    BUSTER_TEST(arguments, x86_64_metadata_test_range_contains_iclass(call, S8("CALL_NEAR")) &&
                               x86_64_metadata_test_range_contains_iclass(ret, S8("RET_NEAR")) &&
                               x86_64_metadata_test_range_contains_iclass(xop, S8("VPMACSSWW")) &&
                               x86_64_metadata_test_range_contains_iclass(avx512, S8("V4FMADDPS")) &&
                               x86_64_metadata_test_range_contains_iclass(system, S8("WRMSR")) &&
                               x86_64_metadata_test_range_contains_iclass(amx, S8("TILELOADD")));

    if (mov.count)
    {
        u32 mov_id = 0;
        BusterX86MetadataForm mov_form = {0};
        char8 iform_buffer[512] = {0};
        String8 iform_key = {0};
        bool found_iform = false;
        for (u32 index = 0; index < mov.count && !found_iform; index += 1)
        {
            found_iform = x86_64_metadata_test_candidate(mov, index, &mov_id) && buster_x86_metadata_form(mov_id, &mov_form) &&
                          mov_form.iform.length > 0 && x86_64_metadata_test_string8(mov_form.iform, iform_buffer, sizeof(iform_buffer), &iform_key);
        }
        BUSTER_TEST(arguments, found_iform);
        if (found_iform)
        {
            BusterX86MetadataCandidateRange iform = buster_x86_metadata_lookup_iform(iform_key);
            BUSTER_TEST(arguments, iform.count > 0);
            for (u32 index = 0; index < iform.count; index += 1)
            {
                u32 iform_id = 0;
                BusterX86MetadataForm iform_form = {0};
                BUSTER_TEST(arguments, x86_64_metadata_test_candidate(iform, index, &iform_id) &&
                                           buster_x86_metadata_form(iform_id, &iform_form) &&
                                           x86_64_metadata_test_string_equal(iform_form.iform, iform_key));
            }
            BusterX86MetadataFilter shape_filter = {
                .has_operand_count = true,
                .has_visible_operand_count = true,
                .operand_count = mov_form.operand_count,
                .visible_operand_count = (u16)x86_64_metadata_test_visible_operand_count(mov_id, mov_form.operand_count),
                .operand_shape_count = mov_form.operand_count,
            };
            for (u32 index = 0; index < mov_form.operand_count && index < BUSTER_X86_METADATA_MAX_OPERAND_SHAPE; index += 1)
            {
                BusterX86MetadataOperand shape_operand = {0};
                BUSTER_TEST(arguments, buster_x86_metadata_operand(mov_id, index, &shape_operand));
                shape_filter.operand_shape[index] = (BusterX86MetadataOperandShape){
                    .kind = shape_operand.kind,
                    .visible = shape_operand.visible,
                };
            }
            bool found = false;
            u32 shape_count = x86_64_metadata_test_filtered_count(mov, shape_filter, mov_id, &found);
            BUSTER_TEST(arguments, found && shape_count > 0);
        }
    }

    if (system.count)
    {
        u32 system_id = 0;
        BusterX86MetadataFilter privileged_filter = {.privileged_only = true};
        bool found = false;
        BUSTER_TEST(arguments, x86_64_metadata_test_candidate(system, 0, &system_id));
        u32 privileged_count = x86_64_metadata_test_filtered_count(system, privileged_filter, system_id, &found);
        BUSTER_TEST(arguments, found && privileged_count == system.count);
        BusterX86MetadataForm system_form = {0};
        BusterX86MetadataCoverage system_coverage = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form(system_id, &system_form) &&
                               system_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED &&
                               system_form.reason_id == BUSTER_X86_METADATA_REASON_CPL0);
        BUSTER_TEST(arguments, buster_x86_metadata_coverage(system_id, &system_coverage) &&
                               system_coverage.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED &&
                               system_coverage.reason_id == BUSTER_X86_METADATA_REASON_CPL0);
    }

    if (xop.count)
    {
        u32 xop_id = 0;
        BusterX86MetadataForm xop_form = {0};
        BUSTER_TEST(arguments, x86_64_metadata_test_candidate(xop, 0, &xop_id) && buster_x86_metadata_form(xop_id, &xop_form));
        BusterX86MetadataFilter xop_filter = {
            .require_64_bit = true,
            .has_isa_set = true,
            .isa_set = xop_form.isa_set,
            .has_prefix_kind = true,
            .prefix_kind = xop_form.prefix_kind,
            .has_encoder_family = true,
            .encoder_family = xop_form.encoder_family,
        };
        BusterX86MetadataCandidateIterator iterator = buster_x86_metadata_filter(xop, xop_filter);
        u32 filtered_id = 0;
        u32 filtered_count = 0;
        while (buster_x86_metadata_candidate_next(&iterator, &filtered_id))
        {
            BusterX86MetadataForm filtered_form = {0};
            BUSTER_TEST(arguments, buster_x86_metadata_form(filtered_id, &filtered_form) &&
                                       filtered_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                       filtered_form.prefix_kind == xop_form.prefix_kind &&
                                       filtered_form.encoder_family == xop_form.encoder_family &&
                                       x86_64_metadata_test_string_equal(filtered_form.isa_set, S8("XOP")));
            filtered_count += 1;
        }
        BUSTER_TEST(arguments, filtered_count > 0);
    }

    if (avx10.count)
    {
        u32 avx10_id = 0;
        BusterX86MetadataForm avx10_form = {0};
        BUSTER_TEST(arguments, x86_64_metadata_test_candidate(avx10, 0, &avx10_id) && buster_x86_metadata_form(avx10_id, &avx10_form));
        BusterX86MetadataFilter avx10_filter = {
            .require_64_bit = true,
            .has_isa_set = true,
            .isa_set = avx10_form.isa_set,
            .has_prefix_kind = true,
            .prefix_kind = avx10_form.prefix_kind,
            .has_encoder_family = true,
            .encoder_family = avx10_form.encoder_family,
        };
        bool found = false;
        u32 avx10_count = x86_64_metadata_test_filtered_count(avx10, avx10_filter, avx10_id, &found);
        BUSTER_TEST(arguments, found && avx10_count > 0);
    }

    u32 first_form_hash_id = UINT32_MAX;
    u32 first_coverage_hash_id = UINT32_MAX;
    BusterX86MetadataCandidateRange form_hash = buster_x86_metadata_lookup_form_hash(first_form.stable_hash);
    BusterX86MetadataCoverageRange coverage_hash = buster_x86_metadata_lookup_coverage_hash(first_coverage.source_hash);
    BUSTER_TEST(arguments, form_hash.count == 1 && x86_64_metadata_test_candidate(form_hash, 0, &first_form_hash_id) &&
                               first_form_hash_id == first_form.id);
    BUSTER_TEST(arguments, coverage_hash.count == 1 && x86_64_metadata_test_coverage_candidate(coverage_hash, 0, &first_coverage_hash_id) &&
                               first_coverage_hash_id == first_coverage.id);
    BUSTER_TEST(arguments, buster_x86_metadata_lookup_form_hash(0).count == 0 && buster_x86_metadata_lookup_coverage_hash(0).count == 0);
    return result;
}
