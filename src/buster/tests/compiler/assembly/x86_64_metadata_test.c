#include <buster/tests/compiler/assembly/x86_64_metadata_test.h>
#if BUSTER_INCLUDE_TESTS

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

BUSTER_GLOBAL_LOCAL BusterX86MetadataOperandSignature x86_64_metadata_test_physical_signature(u8 kind, u8 physical_class,
                                                                                                u16 physical_width_flags)
{
    return (BusterX86MetadataOperandSignature){
        .kind = kind,
        .physical_class = physical_class,
        .physical_width_flags = physical_width_flags,
        .has_physical_class = true,
        .has_physical_width = physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataResolveResult x86_64_metadata_test_resolve_physical(
    String8 mnemonic, BusterX86MetadataOperandSignature const* operands, u32 operand_count, String8 const* features,
    u32 feature_count, u16 required_field_flags, u16 forbidden_field_flags, u16 decorator_flags, u16 apx_flags, u16 amx_flags,
    bool include_privileged, bool include_not64, u32* form_ids, u32 form_id_capacity)
{
    return buster_x86_metadata_resolve(
        (BusterX86MetadataResolveQuery){
            .mnemonic = mnemonic,
            .operands = operands,
            .operand_count = operand_count,
            .features = {.names = features, .count = feature_count},
            .decorator_flags = decorator_flags,
            .apx_flags = apx_flags,
            .amx_flags = amx_flags,
            .required_field_flags = required_field_flags,
            .forbidden_field_flags = forbidden_field_flags,
            .include_privileged = include_privileged,
            .include_not64 = include_not64,
        },
        form_ids, form_id_capacity);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_build_signatures(u32 form_id, bool include_implicit,
                                                                BusterX86MetadataOperandSignature* signatures,
                                                                char8 atom_buffers[16][128], char8 width_buffers[16][128],
                                                                String8* feature, char8* feature_buffer, u32 feature_capacity,
                                                                u32* signature_count)
{
    BusterX86MetadataForm form = {0};
    if (!signatures || !feature || !feature_buffer || !signature_count || !buster_x86_metadata_form(form_id, &form)) return false;
    u32 count = 0;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (!buster_x86_metadata_operand(form_id, operand_index, &operand)) return false;
        if (!include_implicit && !operand.visible) continue;
        if (count >= 16 || !x86_64_metadata_test_string8(operand.atom, atom_buffers[count], 128, &signatures[count].atom) ||
            !x86_64_metadata_test_string8(operand.width, width_buffers[count], 128, &signatures[count].width))
        {
            return false;
        }
        signatures[count].kind = operand.kind;
        signatures[count].field_source = operand.field_source;
        signatures[count].access = operand.access;
        signatures[count].has_atom = operand.atom.length != 0;
        signatures[count].has_width = operand.width.length != 0;
        signatures[count].has_field_source = true;
        signatures[count].has_access = true;
        signatures[count].has_slot = operand.slot != UINT8_MAX;
        signatures[count].slot = operand.slot;
        signatures[count].has_visible = include_implicit;
        signatures[count].visible = operand.visible;
        count += 1;
    }
    BusterX86MetadataString feature_string = form.isa_set.length ? form.isa_set : form.extension;
    if (!x86_64_metadata_test_string8(feature_string, feature_buffer, feature_capacity, feature)) return false;
    *signature_count = count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_normalized_form(BusterX86MetadataCandidateRange range, u32* form_id,
                                                                      BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED)
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_extension(BusterX86MetadataCandidateRange range, String8 extension,
                                                                         u32* form_id, BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
            x86_64_metadata_test_string_equal(candidate_form.extension, extension))
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_decorators(BusterX86MetadataCandidateRange range, u16 decorators,
                                                                          u32* form_id, BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
            (candidate_form.decorator_flags & decorators) == decorators)
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_operand_kind(BusterX86MetadataCandidateRange range, u8 kind,
                                                                            bool visible, u32* form_id,
                                                                            BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED) continue;
        for (u32 operand_index = 0; operand_index < candidate_form.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand operand = {0};
            if (!buster_x86_metadata_operand(candidate, operand_index, &operand)) return false;
            if (operand.kind == kind && (!visible || operand.visible))
            {
                if (form) *form = candidate_form;
                if (form_id) *form_id = candidate;
                return true;
            }
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_coverage(BusterX86MetadataCandidateRange range, u8 coverage_class,
                                                                        u32* form_id, BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == coverage_class)
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_first_form_with_apx(BusterX86MetadataCandidateRange range, u32* form_id,
                                                                    BusterX86MetadataForm* form)
{
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 candidate = 0;
        BusterX86MetadataForm candidate_form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) return false;
        if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED && candidate_form.apx_flags)
        {
            if (form) *form = candidate_form;
            if (form_id) *form_id = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u32 x86_64_metadata_test_max_visible_operand_count(BusterX86MetadataCandidateRange range)
{
    u32 maximum = 0;
    for (u32 index = 0; index < range.count; index += 1)
    {
        u32 form_id = 0;
        BusterX86MetadataForm form = {0};
        if (!buster_x86_metadata_candidate_at(range, index, &form_id) || !buster_x86_metadata_form(form_id, &form)) continue;
        maximum = BUSTER_MAX(maximum, x86_64_metadata_test_visible_operand_count(form_id, form.operand_count));
    }
    return maximum;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataResolveResult x86_64_metadata_test_resolve_form(String8 mnemonic, u32 form_id,
                                                                                      bool include_implicit,
                                                                                      bool include_privileged, bool include_not64,
                                                                                      u16 decorator_flags, u16 apx_flags,
                                                                                      u16 amx_flags, u8 address_size, u32* form_ids,
                                                                                      u32 form_id_capacity)
{
    BusterX86MetadataResolveResult invalid = {.status = BUSTER_X86_METADATA_RESOLVE_INVALID_INPUT};
    BusterX86MetadataOperandSignature signatures[16] = {0};
    char8 atom_buffers[16][128] = {0};
    char8 width_buffers[16][128] = {0};
    char8 feature_buffer[256] = {0};
    String8 feature = {0};
    u32 signature_count = 0;
    if (!x86_64_metadata_test_build_signatures(form_id, include_implicit, signatures, atom_buffers, width_buffers, &feature,
                                                feature_buffer, sizeof(feature_buffer), &signature_count))
    {
        return invalid;
    }
    String8 feature_names[1] = {feature};
    return buster_x86_metadata_resolve(
        (BusterX86MetadataResolveQuery){
            .mnemonic = mnemonic,
            .operands = signatures,
            .operand_count = signature_count,
            .features = {.names = feature_names, .count = 1},
            .decorator_flags = decorator_flags,
            .apx_flags = apx_flags,
            .amx_flags = amx_flags,
            .address_size = address_size,
            .include_implicit = include_implicit,
            .include_privileged = include_privileged,
            .include_not64 = include_not64,
        },
        form_ids, form_id_capacity);
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
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_FIELD_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_DECORATOR_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_APX_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_AMX_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_MODE_FLAGS, .index = 0, .value = UINT16_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_ENCODING_WIDTHS, .index = 0, .value = 3};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_FORM_MANDATORY_PREFIX, .index = 0, .value = 0x67};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_OPERAND_KIND, .index = 0, .value = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_OPERAND_FIELD_SOURCE, .index = 0, .value = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);
    patch = (BusterX86MetadataValidationPatch){.kind = BUSTER_X86_METADATA_PATCH_OPERAND_ACCESS, .index = 0, .value = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_x86_metadata_validate_patch(patch, &validation) &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_ENUM);

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

    {
        BusterX86MetadataForm mov_form = {0};
        u32 mov_form_id = 0;
        bool found_mov = x86_64_metadata_test_first_form_with_extension(mov, S8("BASE"), &mov_form_id, &mov_form) ||
                         x86_64_metadata_test_first_normalized_form(mov, &mov_form_id, &mov_form);
        u32 resolved_ids[512] = {0};
        u32 resolved_again[512] = {0};
        BusterX86MetadataResolveResult resolved = {0};
        BusterX86MetadataResolveResult resolved_again_result = {0};
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 feature = {0};
        u32 signature_count = 0;
        bool built_mov = found_mov && x86_64_metadata_test_build_signatures(mov_form_id, false, signatures, atom_buffers, width_buffers,
                                                                               &feature, feature_buffer, sizeof(feature_buffer),
                                                                               &signature_count);
        BUSTER_TEST(arguments, built_mov);
        if (built_mov)
        {
            String8 features[1] = {feature};
            BusterX86MetadataResolveQuery query = {
                .mnemonic = S8("MoV"),
                .operands = signatures,
                .operand_count = signature_count,
                .features = {.names = features, .count = 1},
            };
            resolved = buster_x86_metadata_resolve(query, resolved_ids, BUSTER_ARRAY_LENGTH(resolved_ids));
            BUSTER_TEST(arguments, resolved.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && resolved.required_candidate_count > 0 &&
                                       resolved.candidate_count == resolved.required_candidate_count);
            bool ordered = true;
            for (u32 index = 1; index < resolved.candidate_count; index += 1) ordered &= resolved_ids[index] > resolved_ids[index - 1];
            BUSTER_TEST(arguments, ordered);
            query.mnemonic = S8("mov");
            resolved_again_result = buster_x86_metadata_resolve(query, resolved_again, BUSTER_ARRAY_LENGTH(resolved_again));
            bool same = resolved_again_result.status == resolved.status && resolved_again_result.candidate_count == resolved.candidate_count;
            for (u32 index = 0; same && index < resolved.candidate_count; index += 1) same &= resolved_again[index] == resolved_ids[index];
            BUSTER_TEST(arguments, same);

            u32 maximum = x86_64_metadata_test_max_visible_operand_count(mov);
            if (maximum < 16)
            {
                BusterX86MetadataResolveResult wrong_count = buster_x86_metadata_resolve(
                    (BusterX86MetadataResolveQuery){
                        .mnemonic = S8("mov"), .operands = signatures, .operand_count = maximum + 1,
                        .features = {.names = features, .count = 1},
                    },
                    resolved_ids, BUSTER_ARRAY_LENGTH(resolved_ids));
                BUSTER_TEST(arguments, wrong_count.status == BUSTER_X86_METADATA_RESOLVE_WRONG_OPERAND_COUNT);
            }
            if (signature_count)
            {
                BusterX86MetadataOperandSignature bad_signatures[16] = {0};
                memcpy(bad_signatures, signatures, sizeof(bad_signatures));
                bad_signatures[0].width = S8("__metadata_width_mismatch__");
                bad_signatures[0].has_width = true;
                BusterX86MetadataResolveResult mismatch = buster_x86_metadata_resolve(
                    (BusterX86MetadataResolveQuery){
                        .mnemonic = S8("mov"), .operands = bad_signatures, .operand_count = signature_count,
                        .features = {.names = features, .count = 1},
                    },
                    resolved_ids, BUSTER_ARRAY_LENGTH(resolved_ids));
                BUSTER_TEST(arguments, mismatch.status == BUSTER_X86_METADATA_RESOLVE_OPERAND_CLASS_WIDTH_MISMATCH);
                BusterX86MetadataResolveResult unsupported_decorator = buster_x86_metadata_resolve(
                    (BusterX86MetadataResolveQuery){
                        .mnemonic = S8("mov"), .operands = signatures, .operand_count = signature_count,
                        .features = {.names = features, .count = 1}, .decorator_flags = BUSTER_X86_METADATA_DECORATOR_SAE,
                    },
                    resolved_ids, BUSTER_ARRAY_LENGTH(resolved_ids));
                BUSTER_TEST(arguments, unsupported_decorator.status == BUSTER_X86_METADATA_RESOLVE_UNSUPPORTED_DECORATOR);
            }
        }
    }

    {
        BusterX86MetadataCandidateRange addps = buster_x86_metadata_lookup_mnemonic(S8("ADDPS"));
        BusterX86MetadataForm sse_form = {0};
        u32 sse_form_id = 0;
        bool found_sse = x86_64_metadata_test_first_form_with_extension(addps, S8("SSE"), &sse_form_id, &sse_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult sse_result = {0};
        if (found_sse) sse_result = x86_64_metadata_test_resolve_form(S8("ADDPS"), sse_form_id, false, false, false, 0, 0, 0, 0, ids,
                                                                       BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_sse && sse_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && sse_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange vaddps = buster_x86_metadata_lookup_mnemonic(S8("VADDPS"));
        BusterX86MetadataForm avx_form = {0};
        u32 avx_form_id = 0;
        bool found_avx = x86_64_metadata_test_first_form_with_extension(vaddps, S8("AVX"), &avx_form_id, &avx_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult avx_result = {0};
        if (found_avx) avx_result = x86_64_metadata_test_resolve_form(S8("VADDPS"), avx_form_id, false, false, false, 0, 0, 0, 0, ids,
                                                                       BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_avx && avx_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && avx_result.candidate_count > 0);
        if (found_avx)
        {
            BusterX86MetadataOperandSignature signatures[16] = {0};
            char8 atom_buffers[16][128] = {0};
            char8 width_buffers[16][128] = {0};
            char8 feature_buffer[256] = {0};
            String8 feature = {0};
            u32 signature_count = 0;
            bool built = x86_64_metadata_test_build_signatures(avx_form_id, false, signatures, atom_buffers, width_buffers, &feature,
                                                                feature_buffer, sizeof(feature_buffer), &signature_count);
            String8 no_features[1] = {S8("__no_feature__")};
            BusterX86MetadataResolveResult disabled = {0};
            if (built)
            {
                disabled = buster_x86_metadata_resolve(
                    (BusterX86MetadataResolveQuery){
                        .mnemonic = S8("VADDPS"), .operands = signatures, .operand_count = signature_count,
                        .features = {.names = no_features, .count = 1},
                    },
                    ids, BUSTER_ARRAY_LENGTH(ids));
            }
            BUSTER_TEST(arguments, built && disabled.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);
        }
    }

    {
        BusterX86MetadataCandidateRange v4fmaddps = buster_x86_metadata_lookup_mnemonic(S8("V4FMADDPS"));
        BusterX86MetadataForm avx512_form = {0};
        u32 avx512_form_id = 0;
        bool found_avx512 = x86_64_metadata_test_first_normalized_form(v4fmaddps, &avx512_form_id, &avx512_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult avx512_result = {0};
        if (found_avx512)
        {
            avx512_result = x86_64_metadata_test_resolve_form(S8("V4FMADDPS"), avx512_form_id, false, false, false,
                                                               avx512_form.decorator_flags, 0, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_avx512 && avx512_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   avx512_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange vrndscaleps = buster_x86_metadata_lookup_mnemonic(S8("VRNDSCALEPS"));
        BusterX86MetadataForm sae_form = {0};
        u32 sae_form_id = 0;
        bool found_sae = x86_64_metadata_test_first_form_with_decorators(vrndscaleps, BUSTER_X86_METADATA_DECORATOR_SAE, &sae_form_id,
                                                                          &sae_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult sae_result = {0};
        if (found_sae)
        {
            sae_result = x86_64_metadata_test_resolve_form(S8("VRNDSCALEPS"), sae_form_id, false, false, false,
                                                            sae_form.decorator_flags, 0, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_sae && sae_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && sae_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange xop_range = buster_x86_metadata_lookup_mnemonic(S8("VPMACSSWW"));
        u32 xop_id = 0;
        BusterX86MetadataForm xop_form = {0};
        bool found_xop = x86_64_metadata_test_first_normalized_form(xop_range, &xop_id, &xop_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult xop_result = {0};
        if (found_xop) xop_result = x86_64_metadata_test_resolve_form(S8("VPMACSSWW"), xop_id, false, false, false, 0, 0, 0, 0, ids,
                                                                       BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_xop && xop_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && xop_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange amx_range = buster_x86_metadata_lookup_mnemonic(S8("TILELOADD"));
        BusterX86MetadataForm amx_form = {0};
        u32 amx_id = 0;
        bool found_amx = x86_64_metadata_test_first_form_with_extension(amx_range, S8("AMX_TILE"), &amx_id, &amx_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult amx_result = {0};
        if (found_amx) amx_result = x86_64_metadata_test_resolve_form(S8("TILELOADD"), amx_id, false, false, false, amx_form.decorator_flags,
                                                                       0, amx_form.amx_flags, 64, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_amx && amx_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && amx_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange apx_range = buster_x86_metadata_lookup_mnemonic(S8("ADD"));
        BusterX86MetadataForm apx_form = {0};
        u32 apx_id = 0;
        bool found_apx = x86_64_metadata_test_first_form_with_apx(apx_range, &apx_id, &apx_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult apx_result = {0};
        if (found_apx) apx_result = x86_64_metadata_test_resolve_form(S8("ADD"), apx_id, false, false, false, apx_form.decorator_flags,
                                                                       apx_form.apx_flags, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_apx && apx_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && apx_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange avx10_range = buster_x86_metadata_lookup_iform(
            S8("VCVTBF42HF8_XMMhf8_MASKmskw_XMMbf4_AVX512"));
        BusterX86MetadataForm avx10_form = {0};
        u32 avx10_id = 0;
        bool found_avx10 = x86_64_metadata_test_first_normalized_form(avx10_range, &avx10_id, &avx10_form);
        char8 mnemonic_buffer[128] = {0};
        String8 mnemonic = {0};
        if (found_avx10) found_avx10 = x86_64_metadata_test_string8(avx10_form.iclass, mnemonic_buffer, sizeof(mnemonic_buffer), &mnemonic);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult avx10_result = {0};
        if (found_avx10) avx10_result = x86_64_metadata_test_resolve_form(mnemonic, avx10_id, false, false, false,
                                                                           avx10_form.decorator_flags, avx10_form.apx_flags,
                                                                           avx10_form.amx_flags, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, found_avx10 && avx10_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   avx10_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange jmp_range = buster_x86_metadata_lookup_mnemonic(S8("JMP"));
        BusterX86MetadataForm relative_form = {0};
        BusterX86MetadataForm memory_form = {0};
        u32 relative_id = 0;
        u32 memory_id = 0;
        bool found_relative = x86_64_metadata_test_first_form_with_operand_kind(jmp_range, BUSTER_X86_METADATA_OPERAND_RELATIVE, true,
                                                                                  &relative_id, &relative_form);
        bool found_memory = x86_64_metadata_test_first_form_with_operand_kind(jmp_range, BUSTER_X86_METADATA_OPERAND_MEMORY, true,
                                                                               &memory_id, &memory_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult relative_result = {0};
        BusterX86MetadataResolveResult memory_result = {0};
        if (found_relative)
        {
            relative_result = x86_64_metadata_test_resolve_form(S8("JMP"), relative_id, false, false, false,
                                                                 relative_form.decorator_flags, 0, 0, 64, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        if (found_memory)
        {
            memory_result = x86_64_metadata_test_resolve_form(S8("JMP"), memory_id, false, false, false,
                                                               memory_form.decorator_flags, 0, 0, 64, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_relative && relative_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   relative_result.candidate_count > 0);
        BUSTER_TEST(arguments, found_memory && memory_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && memory_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange ret_range = buster_x86_metadata_lookup_mnemonic(S8("RET"));
        BusterX86MetadataForm implicit_form = {0};
        u32 implicit_id = 0;
        bool found_implicit = false;
        for (u32 index = 0; index < ret_range.count && !found_implicit; index += 1)
        {
            u32 candidate = 0;
            BusterX86MetadataForm candidate_form = {0};
            if (!buster_x86_metadata_candidate_at(ret_range, index, &candidate) || !buster_x86_metadata_form(candidate, &candidate_form)) continue;
            if (candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                x86_64_metadata_test_visible_operand_count(candidate, candidate_form.operand_count) == 0)
            {
                implicit_id = candidate;
                implicit_form = candidate_form;
                found_implicit = true;
            }
        }
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult implicit_result = {0};
        if (found_implicit)
        {
            implicit_result = x86_64_metadata_test_resolve_form(S8("RET"), implicit_id, true, false, false,
                                                                 implicit_form.decorator_flags, 0, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_implicit && implicit_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   implicit_result.candidate_count > 0);
    }

    {
        BusterX86MetadataCandidateRange system_range = buster_x86_metadata_lookup_mnemonic(S8("WRMSR"));
        BusterX86MetadataForm system_form = {0};
        u32 system_id = 0;
        bool found_system = x86_64_metadata_test_first_form_with_coverage(system_range, BUSTER_X86_METADATA_COVERAGE_PRIVILEGED, &system_id,
                                                                           &system_form);
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult system_result = {0};
        BusterX86MetadataResolveResult excluded_result = {0};
        if (found_system)
        {
            system_result = x86_64_metadata_test_resolve_form(S8("WRMSR"), system_id, false, true, false, system_form.decorator_flags, 0,
                                                               0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
            excluded_result = x86_64_metadata_test_resolve_form(S8("WRMSR"), system_id, false, false, false, system_form.decorator_flags,
                                                                 0, 0, 0, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, found_system && system_result.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && system_result.candidate_count > 0);
        BUSTER_TEST(arguments, found_system &&
                                   excluded_result.status == BUSTER_X86_METADATA_RESOLVE_AMBIGUOUS_OR_UNSUPPORTED_METADATA);
    }

    {
        struct
        {
            String8 mnemonic;
            String8 extension;
        } extension_cases[] = {
            {S8("VFMADDPS"), S8("FMA4")},
            {S8("BEXTR"), S8("TBM")},
            {S8("PFADD"), S8("3DNOW")},
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(extension_cases); case_index += 1)
        {
            BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_mnemonic(extension_cases[case_index].mnemonic);
            BusterX86MetadataForm form = {0};
            u32 form_id = 0;
            bool found = x86_64_metadata_test_first_form_with_extension(range, extension_cases[case_index].extension, &form_id, &form);
            u32 ids[128] = {0};
            BusterX86MetadataResolveResult resolved = {0};
            if (found)
            {
                resolved = x86_64_metadata_test_resolve_form(extension_cases[case_index].mnemonic, form_id, false, false, false,
                                                              form.decorator_flags, form.apx_flags, form.amx_flags, 0, ids,
                                                              BUSTER_ARRAY_LENGTH(ids));
            }
            BUSTER_TEST(arguments, found && resolved.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && resolved.candidate_count > 0);
        }
    }

    {
        // MODE_16|MODE_32|MODE_64 is a legal multi-mode row.  It must remain
        // visible to the ordinary 64-bit resolver, while an explicit
        // MODE_NOT64 row is inspection-only.
        u16 multi_mode = BUSTER_X86_METADATA_MODE_16 | BUSTER_X86_METADATA_MODE_32 | BUSTER_X86_METADATA_MODE_64;
        u16 not64_mode = BUSTER_X86_METADATA_MODE_16 | BUSTER_X86_METADATA_MODE_32;
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   multi_mode, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   not64_mode, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_NOT64, BUSTER_X86_METADATA_COVERAGE_NOT64, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_NOT64, BUSTER_X86_METADATA_COVERAGE_NOT64, true,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));

        u32 not64_form_id = 0;
        String8 not64_mnemonic = {0};
        bool found_not64_only = false;
        char8 not64_mnemonic_buffer[128] = {0};
        for (u32 candidate_form_id = 0; candidate_form_id < buster_x86_metadata_form_count() && !found_not64_only;
             candidate_form_id += 1)
        {
            BusterX86MetadataForm candidate_form = {0};
            if (!buster_x86_metadata_form(candidate_form_id, &candidate_form) ||
                candidate_form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NOT64 ||
                !x86_64_metadata_test_string8(candidate_form.iclass, not64_mnemonic_buffer, sizeof(not64_mnemonic_buffer),
                                               &not64_mnemonic))
                continue;
            BusterX86MetadataCandidateRange candidate_range = buster_x86_metadata_lookup_mnemonic(not64_mnemonic);
            bool only_not64 = candidate_range.count > 0;
            for (u32 position = 0; position < candidate_range.count && only_not64; position += 1)
            {
                u32 range_form_id = 0;
                BusterX86MetadataForm range_form = {0};
                only_not64 = buster_x86_metadata_candidate_at(candidate_range, position, &range_form_id) &&
                             buster_x86_metadata_form(range_form_id, &range_form) &&
                             range_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64;
            }
            if (only_not64)
            {
                not64_form_id = candidate_form_id;
                found_not64_only = true;
            }
        }
        BusterX86MetadataOperandSignature not64_signatures[16] = {0};
        char8 not64_atom_buffers[16][128] = {0};
        char8 not64_width_buffers[16][128] = {0};
        char8 not64_feature_buffer[256] = {0};
        String8 not64_feature = {0};
        u32 not64_signature_count = 0;
        bool built_not64 = found_not64_only && x86_64_metadata_test_build_signatures(
                                                  not64_form_id, false, not64_signatures, not64_atom_buffers, not64_width_buffers,
                                                  &not64_feature, not64_feature_buffer, sizeof(not64_feature_buffer),
                                                  &not64_signature_count);
        String8 not64_features[1] = {not64_feature};
        u32 not64_ids[128] = {0};
        BusterX86MetadataResolveResult not64_rejected = {0};
        BusterX86MetadataResolveResult not64_inspected = {0};
        if (built_not64)
        {
            not64_rejected = buster_x86_metadata_resolve(
                (BusterX86MetadataResolveQuery){
                    .mnemonic = not64_mnemonic,
                    .operands = not64_signatures,
                    .operand_count = not64_signature_count,
                    .features = {.names = not64_features, .count = BUSTER_ARRAY_LENGTH(not64_features)},
                },
                not64_ids, BUSTER_ARRAY_LENGTH(not64_ids));
            not64_inspected = buster_x86_metadata_resolve(
                (BusterX86MetadataResolveQuery){
                    .mnemonic = not64_mnemonic,
                    .operands = not64_signatures,
                    .operand_count = not64_signature_count,
                    .features = {.names = not64_features, .count = BUSTER_ARRAY_LENGTH(not64_features)},
                    .include_not64 = true,
                },
                not64_ids, BUSTER_ARRAY_LENGTH(not64_ids));
        }
        BUSTER_TEST(arguments, built_not64 && not64_rejected.status == BUSTER_X86_METADATA_RESOLVE_EXECUTION_MODE_MISMATCH);
        BUSTER_TEST(arguments, built_not64 && not64_inspected.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   not64_inspected.candidate_count > 0);
    }

    {
        // These are caller-owned physical signatures.  No raw XED atom or
        // width token is copied from a selected form before resolution.
        String8 all_features[1] = {S8("*")};
        u16 gpr_widths[] = {
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_8,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_16,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_32,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_64,
        };
        for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(gpr_widths); width_index += 1)
        {
            BusterX86MetadataOperandSignature mov_operands[2] = {
                x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                         BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, gpr_widths[width_index]),
                x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                         BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, gpr_widths[width_index]),
            };
            u32 ids[256] = {0};
            BusterX86MetadataResolveResult resolved = x86_64_metadata_test_resolve_physical(
                S8("MOV"), mov_operands, BUSTER_ARRAY_LENGTH(mov_operands), all_features, BUSTER_ARRAY_LENGTH(all_features), 0, 0,
                0, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
            BUSTER_TEST(arguments, resolved.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && resolved.candidate_count > 0);
        }

        BusterX86MetadataOperandSignature accumulator_operand = x86_64_metadata_test_physical_signature(
            BUSTER_X86_METADATA_OPERAND_REGISTER, BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_32);
        u32 accumulator_ids[64] = {0};
        BusterX86MetadataResolveResult accumulator = buster_x86_metadata_resolve(
            (BusterX86MetadataResolveQuery){
                .mnemonic = S8("VMFUNC"),
                .operands = &accumulator_operand,
                .operand_count = 1,
                .features = {.names = all_features, .count = BUSTER_ARRAY_LENGTH(all_features)},
                .include_implicit = true,
            },
            accumulator_ids, BUSTER_ARRAY_LENGTH(accumulator_ids));
        BUSTER_TEST(arguments, accumulator.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && accumulator.candidate_count > 0);

        bool found_fixed_special = false;
        bool fixed_special_is_special = false;
        for (u32 form_id = 0; form_id < buster_x86_metadata_form_count() && !found_fixed_special; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 operand_index = 0; operand_index < form.operand_count && !found_fixed_special; operand_index += 1)
            {
                BusterX86MetadataOperand special_operand = {0};
                if (!buster_x86_metadata_operand(form_id, operand_index, &special_operand)) continue;
                bool fixed_special_atom = x86_64_metadata_test_string_equal(special_operand.atom, S8("XED_REG_RIP")) ||
                                           x86_64_metadata_test_string_equal(special_operand.atom, S8("XED_REG_X87STATUS"));
                if (fixed_special_atom)
                {
                    found_fixed_special = true;
                    fixed_special_is_special = special_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
                }
            }
        }
        BUSTER_TEST(arguments, found_fixed_special && fixed_special_is_special);

        BusterX86MetadataOperandSignature xmm_operands[2] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_128),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_128),
        };
        String8 sse_features[1] = {S8("sse2")};
        u32 xmm_ids[128] = {0};
        BusterX86MetadataResolveResult xmm = x86_64_metadata_test_resolve_physical(
            S8("ADDPS"), xmm_operands, BUSTER_ARRAY_LENGTH(xmm_operands), sse_features, BUSTER_ARRAY_LENGTH(sse_features), 0, 0,
            0, 0, 0, false, false, xmm_ids, BUSTER_ARRAY_LENGTH(xmm_ids));
        BUSTER_TEST(arguments, xmm.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && xmm.candidate_count > 0);

        BusterX86MetadataOperandSignature ymm_operands[3] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_256),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_256),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_256),
        };
        String8 avx_features[1] = {S8("avx")};
        u32 ymm_ids[128] = {0};
        BusterX86MetadataResolveResult ymm = x86_64_metadata_test_resolve_physical(
            S8("VADDPS"), ymm_operands, BUSTER_ARRAY_LENGTH(ymm_operands), avx_features, BUSTER_ARRAY_LENGTH(avx_features), 0, 0,
            0, 0, 0, false, false, ymm_ids, BUSTER_ARRAY_LENGTH(ymm_ids));
        BUSTER_TEST(arguments, ymm.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && ymm.candidate_count > 0);

        BusterX86MetadataOperandSignature zmm_operands[4] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_512),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 0),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_512),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_512),
        };
        String8 avx512_features[1] = {S8("avx512f")};
        u32 zmm_ids[128] = {0};
        BusterX86MetadataResolveResult zmm = x86_64_metadata_test_resolve_physical(
            S8("VADDPS"), zmm_operands, BUSTER_ARRAY_LENGTH(zmm_operands), avx512_features,
            BUSTER_ARRAY_LENGTH(avx512_features), 0, 0, BUSTER_X86_METADATA_DECORATOR_MASK, 0, 0, false, false, zmm_ids,
            BUSTER_ARRAY_LENGTH(zmm_ids));
        BUSTER_TEST(arguments, zmm.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && zmm.candidate_count > 0);

        BusterX86MetadataOperandSignature immediate_operands[2] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_32),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_IMMEDIATE,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_IMMEDIATE, 0),
        };
        u32 immediate_ids[128] = {0};
        BusterX86MetadataResolveResult immediate = x86_64_metadata_test_resolve_physical(
            S8("MOV"), immediate_operands, BUSTER_ARRAY_LENGTH(immediate_operands), all_features,
            BUSTER_ARRAY_LENGTH(all_features), 0, 0, 0, 0, 0, false, false, immediate_ids, BUSTER_ARRAY_LENGTH(immediate_ids));
        BUSTER_TEST(arguments, immediate.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && immediate.candidate_count > 0);

        BusterX86MetadataOperandSignature relative_operand = x86_64_metadata_test_physical_signature(
            BUSTER_X86_METADATA_OPERAND_RELATIVE, BUSTER_X86_METADATA_PHYSICAL_CLASS_RELATIVE, 0);
        u32 relative_ids[128] = {0};
        BusterX86MetadataResolveResult relative = x86_64_metadata_test_resolve_physical(
            S8("JMP"), &relative_operand, 1, all_features, BUSTER_ARRAY_LENGTH(all_features), 0, 0, 0, 0, 0, false, false,
            relative_ids, BUSTER_ARRAY_LENGTH(relative_ids));
        BUSTER_TEST(arguments, relative.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && relative.candidate_count > 0);
    }

    {
        String8 all_features[1] = {S8("*")};
        BusterX86MetadataOperandSignature mov_memory_operands[2] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_64),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_MEMORY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_64),
        };
        u32 ids[256] = {0};
        BusterX86MetadataResolveResult memory = x86_64_metadata_test_resolve_physical(
            S8("MOV"), mov_memory_operands, BUSTER_ARRAY_LENGTH(mov_memory_operands), all_features,
            BUSTER_ARRAY_LENGTH(all_features), BUSTER_X86_METADATA_FIELD_MEMORY, 0, 0, 0, 0, false, false, ids,
            BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, memory.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && memory.candidate_count > 0);
        BusterX86MetadataResolveResult memory_sib = x86_64_metadata_test_resolve_physical(
            S8("MOV"), mov_memory_operands, BUSTER_ARRAY_LENGTH(mov_memory_operands), all_features,
            BUSTER_ARRAY_LENGTH(all_features), BUSTER_X86_METADATA_FIELD_SIB, 0, 0, 0, 0, false, false, ids,
            BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, memory_sib.status == BUSTER_X86_METADATA_RESOLVE_ADDRESSING_FIELD_MISMATCH);
        BusterX86MetadataResolveResult memory_vsib = x86_64_metadata_test_resolve_physical(
            S8("MOV"), mov_memory_operands, BUSTER_ARRAY_LENGTH(mov_memory_operands), all_features,
            BUSTER_ARRAY_LENGTH(all_features), BUSTER_X86_METADATA_FIELD_VSIB, 0, 0, 0, 0, false, false, ids,
            BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, memory_vsib.status == BUSTER_X86_METADATA_RESOLVE_ADDRESSING_FIELD_MISMATCH);

        BusterX86MetadataOperandSignature tile_memory_operands[2] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_REGISTER,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_MEMORY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY,
                                                     BUSTER_X86_METADATA_PHYSICAL_WIDTH_32),
        };
        String8 amx_features[1] = {S8("amx-tile")};
        BusterX86MetadataResolveResult sib = x86_64_metadata_test_resolve_physical(
            S8("TILELOADD"), tile_memory_operands, BUSTER_ARRAY_LENGTH(tile_memory_operands), amx_features,
            BUSTER_ARRAY_LENGTH(amx_features), BUSTER_X86_METADATA_FIELD_SIB, 0, 0, 0, 0, false, false, ids,
            BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, sib.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && sib.candidate_count > 0);
    }

    {
        // A register candidate reaches feature filtering while a same-shape
        // memory candidate is rejected by the addressing predicate.  The
        // final diagnostic must be feature-disabled, not address mismatch.
        BusterX86MetadataOperandSignature unconstrained[3] = {
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_ANY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY, 0),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_ANY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY, 0),
            x86_64_metadata_test_physical_signature(BUSTER_X86_METADATA_OPERAND_ANY,
                                                     BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY, 0),
        };
        u32 ids[256] = {0};
        BusterX86MetadataResolveResult mixed = x86_64_metadata_test_resolve_physical(
            S8("VADDPS"), unconstrained, BUSTER_ARRAY_LENGTH(unconstrained), 0, 0, BUSTER_X86_METADATA_FIELD_REGISTER, 0, 0, 0,
            0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, mixed.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);
    }

    {
        BusterX86MetadataCandidateRange vadd_range = buster_x86_metadata_lookup_mnemonic(S8("VADDPS"));
        BusterX86MetadataForm broadcast_form = {0};
        u32 broadcast_id = 0;
        bool found_broadcast = x86_64_metadata_test_first_form_with_decorators(
            vadd_range, BUSTER_X86_METADATA_DECORATOR_BROADCAST, &broadcast_id, &broadcast_form);
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 feature = {0};
        u32 signature_count = 0;
        bool built = found_broadcast && x86_64_metadata_test_build_signatures(
                                        broadcast_id, false, signatures, atom_buffers, width_buffers, &feature, feature_buffer,
                                        sizeof(feature_buffer), &signature_count);
        String8 features[1] = {feature};
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult broadcast = {0};
        if (built)
        {
            broadcast = x86_64_metadata_test_resolve_physical(
                S8("VADDPS"), signatures, signature_count, features, BUSTER_ARRAY_LENGTH(features), 0, 0,
                BUSTER_X86_METADATA_DECORATOR_BROADCAST, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && broadcast.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && broadcast.candidate_count > 0);

        BusterX86MetadataCandidateRange vrnd_range = buster_x86_metadata_lookup_mnemonic(S8("VRNDSCALEPS"));
        BusterX86MetadataForm sae_form = {0};
        u32 sae_id = 0;
        bool found_sae = x86_64_metadata_test_first_form_with_decorators(vrnd_range, BUSTER_X86_METADATA_DECORATOR_SAE, &sae_id,
                                                                          &sae_form);
        signature_count = 0;
        feature = (String8){0};
        built = found_sae && x86_64_metadata_test_build_signatures(sae_id, false, signatures, atom_buffers, width_buffers, &feature,
                                                                    feature_buffer, sizeof(feature_buffer), &signature_count);
        features[0] = feature;
        BusterX86MetadataResolveResult sae_only = {0};
        if (built)
        {
            sae_only = x86_64_metadata_test_resolve_physical(
                S8("VRNDSCALEPS"), signatures, signature_count, features, BUSTER_ARRAY_LENGTH(features), 0, 0,
                BUSTER_X86_METADATA_DECORATOR_SAE, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && sae_only.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && sae_only.candidate_count > 0);
    }

    {
        BusterX86MetadataOperandSignature no_operands[1] = {x86_64_metadata_test_physical_signature(
            BUSTER_X86_METADATA_OPERAND_REGISTER, BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
            BUSTER_X86_METADATA_PHYSICAL_WIDTH_32)};
        String8 all_features[1] = {S8("*")};
        u32 ids[64] = {0};
        BusterX86MetadataResolveResult invalid_zeroing = x86_64_metadata_test_resolve_physical(
            S8("ADD"), no_operands, 1, all_features, BUSTER_ARRAY_LENGTH(all_features), 0, 0,
            BUSTER_X86_METADATA_DECORATOR_ZEROING, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, invalid_zeroing.status == BUSTER_X86_METADATA_RESOLVE_INVALID_INPUT);
        BusterX86MetadataResolveResult invalid_broadcast = x86_64_metadata_test_resolve_physical(
            S8("VADDPS"), no_operands, 1, all_features, BUSTER_ARRAY_LENGTH(all_features), 0, 0,
            BUSTER_X86_METADATA_DECORATOR_BROADCAST, 0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        BUSTER_TEST(arguments, invalid_broadcast.status == BUSTER_X86_METADATA_RESOLVE_INVALID_INPUT);
    }

    {
        // Canonical effective feature names are conjunctive for the AVX10
        // composite and do not fall back to the generic AVX512 extension.
        BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_iform(
            S8("VCVTBF42HF8_XMMhf8_MASKmskw_XMMbf4_AVX512"));
        u32 form_id = 0;
        BusterX86MetadataForm form = {0};
        bool found = x86_64_metadata_test_first_normalized_form(range, &form_id, &form);
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 raw_feature = {0};
        u32 signature_count = 0;
        bool built = found && x86_64_metadata_test_build_signatures(form_id, false, signatures, atom_buffers, width_buffers,
                                                                      &raw_feature, feature_buffer, sizeof(feature_buffer),
                                                                      &signature_count);
        String8 enabled_features[2] = {S8("avx10.2"), S8("avx10-v1-aux")};
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult enabled = {0};
        BusterX86MetadataResolveResult disabled = {0};
        if (built)
        {
            enabled = x86_64_metadata_test_resolve_physical(S8("VCVTBF42HF8"), signatures, signature_count, enabled_features,
                                                             BUSTER_ARRAY_LENGTH(enabled_features), 0, 0, form.decorator_flags,
                                                             0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
            String8 disabled_features[1] = {S8("avx10.2")};
            disabled = x86_64_metadata_test_resolve_physical(S8("VCVTBF42HF8"), signatures, signature_count, disabled_features,
                                                              BUSTER_ARRAY_LENGTH(disabled_features), 0, 0, form.decorator_flags,
                                                              0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && enabled.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && enabled.candidate_count > 0);
        BUSTER_TEST(arguments, built && disabled.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);

        BusterX86MetadataForm wide_form = {0};
        u32 wide_form_id = 0;
        bool found_wide = false;
        BusterX86MetadataCandidateRange wide_range = buster_x86_metadata_lookup_iform(
            S8("VCVTBF42HF8_ZMMhf8_MASKmskw_YMMbf4_AVX512"));
        for (u32 position = 0; position < wide_range.count && !found_wide; position += 1)
        {
            u32 candidate_id = 0;
            BusterX86MetadataForm candidate_form = {0};
            found_wide = buster_x86_metadata_candidate_at(wide_range, position, &candidate_id) &&
                         buster_x86_metadata_form(candidate_id, &candidate_form) &&
                         candidate_form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                         x86_64_metadata_test_string_equal(candidate_form.isa_set, S8("AVX10_V2_AUX_512"));
            if (found_wide)
            {
                wide_form_id = candidate_id;
                wide_form = candidate_form;
            }
        }
        BusterX86MetadataOperandSignature wide_signatures[16] = {0};
        char8 wide_atom_buffers[16][128] = {0};
        char8 wide_width_buffers[16][128] = {0};
        char8 wide_feature_buffer[256] = {0};
        String8 wide_raw_feature = {0};
        u32 wide_signature_count = 0;
        bool built_wide = found_wide && x86_64_metadata_test_build_signatures(
                                             wide_form_id, false, wide_signatures, wide_atom_buffers, wide_width_buffers,
                                             &wide_raw_feature, wide_feature_buffer, sizeof(wide_feature_buffer),
                                             &wide_signature_count);
        String8 wide_features[3] = {S8("avx10.2"), S8("avx10-v1-aux"), S8("avx10-512")};
        u32 wide_ids[128] = {0};
        BusterX86MetadataResolveResult wide_enabled = {0};
        BusterX86MetadataResolveResult wide_disabled = {0};
        if (built_wide)
        {
            wide_enabled = x86_64_metadata_test_resolve_physical(S8("VCVTBF42HF8"), wide_signatures, wide_signature_count,
                                                                  wide_features, BUSTER_ARRAY_LENGTH(wide_features), 0, 0,
                                                                  wide_form.decorator_flags, 0, 0, false, false, wide_ids,
                                                                  BUSTER_ARRAY_LENGTH(wide_ids));
            String8 missing_wide_feature[2] = {S8("avx10.2"), S8("avx10-v1-aux")};
            wide_disabled = x86_64_metadata_test_resolve_physical(
                S8("VCVTBF42HF8"), wide_signatures, wide_signature_count, missing_wide_feature,
                BUSTER_ARRAY_LENGTH(missing_wide_feature), 0, 0, wide_form.decorator_flags, 0, 0, false, false, wide_ids,
                BUSTER_ARRAY_LENGTH(wide_ids));
        }
        BUSTER_TEST(arguments, built_wide && wide_enabled.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   wide_enabled.candidate_count > 0);
        BUSTER_TEST(arguments, built_wide && wide_disabled.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);
    }

    {
        BusterX86MetadataCandidateRange movrs_range = buster_x86_metadata_lookup_mnemonic(S8("VMOVRSB"));
        BusterX86MetadataForm movrs_form = {0};
        u32 movrs_id = 0;
        bool found_movrs = x86_64_metadata_test_first_normalized_form(movrs_range, &movrs_id, &movrs_form);
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 raw_feature = {0};
        u32 signature_count = 0;
        bool built = found_movrs && x86_64_metadata_test_build_signatures(movrs_id, false, signatures, atom_buffers, width_buffers,
                                                                           &raw_feature, feature_buffer, sizeof(feature_buffer),
                                                                           &signature_count);
        String8 features[2] = {S8("avx10.1"), S8("movrs")};
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult enabled = {0};
        BusterX86MetadataResolveResult disabled = {0};
        if (built)
        {
            enabled = x86_64_metadata_test_resolve_physical(S8("VMOVRSB"), signatures, signature_count, features,
                                                             BUSTER_ARRAY_LENGTH(features), 0, 0, movrs_form.decorator_flags, 0, 0,
                                                             false, false, ids, BUSTER_ARRAY_LENGTH(ids));
            String8 missing_feature[1] = {S8("avx10.1")};
            disabled = x86_64_metadata_test_resolve_physical(S8("VMOVRSB"), signatures, signature_count, missing_feature,
                                                              BUSTER_ARRAY_LENGTH(missing_feature), 0, 0, movrs_form.decorator_flags,
                                                              0, 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && enabled.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS && enabled.candidate_count > 0);
        BUSTER_TEST(arguments, built && disabled.status == BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE);
    }

    {
        // The result reports missing APX/AMX attributes independently from
        // decorator bits, so an attribute diagnostic is never empty.
        BusterX86MetadataCandidateRange add_range = buster_x86_metadata_lookup_mnemonic(S8("ADD"));
        u32 add_id = 0;
        BusterX86MetadataForm add_form = {0};
        bool found_add = x86_64_metadata_test_first_form_with_extension(add_range, S8("BASE"), &add_id, &add_form);
        BusterX86MetadataOperandSignature signatures[16] = {0};
        char8 atom_buffers[16][128] = {0};
        char8 width_buffers[16][128] = {0};
        char8 feature_buffer[256] = {0};
        String8 feature = {0};
        u32 signature_count = 0;
        bool built = found_add && x86_64_metadata_test_build_signatures(add_id, false, signatures, atom_buffers, width_buffers,
                                                                         &feature, feature_buffer, sizeof(feature_buffer),
                                                                         &signature_count);
        String8 features[1] = {feature};
        u32 ids[128] = {0};
        BusterX86MetadataResolveResult missing_apx = {0};
        BusterX86MetadataResolveResult missing_amx = {0};
        if (built)
        {
            missing_apx = x86_64_metadata_test_resolve_physical(S8("ADD"), signatures, signature_count, features,
                                                                 BUSTER_ARRAY_LENGTH(features), 0, 0, 0, BUSTER_X86_METADATA_APX,
                                                                 0, false, false, ids, BUSTER_ARRAY_LENGTH(ids));
            missing_amx = x86_64_metadata_test_resolve_physical(S8("ADD"), signatures, signature_count, features,
                                                                 BUSTER_ARRAY_LENGTH(features), 0, 0, 0, 0,
                                                                 BUSTER_X86_METADATA_AMX_TILE_REGISTER, false, false, ids,
                                                                 BUSTER_ARRAY_LENGTH(ids));
        }
        BUSTER_TEST(arguments, built && missing_apx.status == BUSTER_X86_METADATA_RESOLVE_UNSUPPORTED_DECORATOR &&
                                   missing_apx.unsupported_apx_flags != 0 && missing_apx.unsupported_decorator_flags == 0);
        BUSTER_TEST(arguments, built && missing_amx.status == BUSTER_X86_METADATA_RESOLVE_UNSUPPORTED_DECORATOR &&
                                   missing_amx.unsupported_amx_flags != 0 && missing_amx.unsupported_decorator_flags == 0);
    }

    {
        u32 known_class = 0;
        u32 known_width = 0;
        u32 known_both = 0;
        u32 conservative_unknown = 0;
        for (u32 form_id = 0; form_id < buster_x86_metadata_form_count(); form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
            {
                BusterX86MetadataOperand normalized_operand = {0};
                if (!buster_x86_metadata_operand(form_id, operand_index, &normalized_operand) || !normalized_operand.visible) continue;
                bool class_known = normalized_operand.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE &&
                                   normalized_operand.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN &&
                                   normalized_operand.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
                bool width_known = normalized_operand.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN &&
                                   normalized_operand.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY;
                known_class += class_known;
                known_width += width_known;
                known_both += class_known && width_known;
                conservative_unknown += !class_known || !width_known;
            }
        }
        BUSTER_TEST(arguments, known_class > 0 && known_width > 0 && known_both > 0 && conservative_unknown > 0);
    }
    return result;
}
#endif
