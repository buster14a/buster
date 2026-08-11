#include <buster/lib/compiler/assembly/aarch64_encoding.h>

#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif
#include <buster/lib/compiler/assembly/generated/aarch64-assembly.generated.h>
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic pop
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic pop
#endif
#include <buster/lib/compiler/assembly/generated/aarch64-production-plan.generated.h>
#include <buster/lib/compiler/assembly/generated/arm-a64-m1-fixed.generated.h>

BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_COVERAGE_DIRECT == (u32)BUSTER_AARCH64_GENERATED_COVERAGE_DIRECT);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_COVERAGE_NORMALIZED == (u32)BUSTER_AARCH64_GENERATED_COVERAGE_NORMALIZED);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_COVERAGE_ALIAS == (u32)BUSTER_AARCH64_GENERATED_COVERAGE_ALIAS);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_COVERAGE_PRIVILEGED_SYSTEM == (u32)BUSTER_AARCH64_GENERATED_COVERAGE_PRIVILEGED_SYSTEM);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_COVERAGE_RESERVED_UNENCODABLE == (u32)BUSTER_AARCH64_GENERATED_COVERAGE_RESERVED_UNENCODABLE);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_COVERAGE_UNSUPPORTED_TOKEN == (u32)BUSTER_AARCH64_GENERATED_COVERAGE_UNSUPPORTED_TOKEN);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_COVERAGE_UNCLASSIFIED == (u32)BUSTER_AARCH64_GENERATED_COVERAGE_UNCLASSIFIED);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_COVERAGE_CLASS_COUNT == (u32)BUSTER_AARCH64_GENERATED_COVERAGE_CLASS_COUNT);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_NONE == (u32)BUSTER_AARCH64_GENERATED_REASON_NONE);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_ALIAS_OF_CANONICAL == (u32)BUSTER_AARCH64_GENERATED_REASON_ALIAS_OF_CANONICAL);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_SYSTEM_OR_PRIVILEGED == (u32)BUSTER_AARCH64_GENERATED_REASON_SYSTEM_OR_PRIVILEGED);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNMAPPED_VARIABLE == (u32)BUSTER_AARCH64_GENERATED_REASON_UNMAPPED_VARIABLE);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_CONFLICTING_BIT_ASSIGNMENT ==
                (u32)BUSTER_AARCH64_GENERATED_REASON_CONFLICTING_BIT_ASSIGNMENT);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_MALFORMED_DAG == (u32)BUSTER_AARCH64_GENERATED_REASON_MALFORMED_DAG);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_MALFORMED_TEMPLATE == (u32)BUSTER_AARCH64_GENERATED_REASON_MALFORMED_TEMPLATE);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNKNOWN_FIELD == (u32)BUSTER_AARCH64_GENERATED_REASON_UNKNOWN_FIELD);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNKNOWN_PREDICATE == (u32)BUSTER_AARCH64_GENERATED_REASON_UNKNOWN_PREDICATE);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_MISSING_OPERAND == (u32)BUSTER_AARCH64_GENERATED_REASON_MISSING_OPERAND);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_INVALID_JSON == (u32)BUSTER_AARCH64_GENERATED_REASON_INVALID_JSON);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_NULL_FIELD == (u32)BUSTER_AARCH64_GENERATED_REASON_NULL_FIELD);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNPROVEN_FIELD_SEMANTICS ==
                (u32)BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_FIELD_SEMANTICS);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNPROVEN_OPERAND_KIND ==
                (u32)BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_OPERAND_KIND);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNPROVEN_IMMEDIATE_RANGE ==
                (u32)BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_IMMEDIATE_RANGE);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNPROVEN_MEMORY_FORM ==
                (u32)BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_MEMORY_FORM);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNPROVEN_TIED_OPERAND ==
                (u32)BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_TIED_OPERAND);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNPROVEN_CORRESPONDENCE ==
                (u32)BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_CORRESPONDENCE);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_UNSUPPORTED_ADDRESS_GRAMMAR ==
                (u32)BUSTER_AARCH64_GENERATED_REASON_UNSUPPORTED_ADDRESS_GRAMMAR);
BUSTER_CT_CHECK((u32)BUSTER_AARCH64_METADATA_REASON_COUNT == (u32)BUSTER_AARCH64_GENERATED_REASON_COUNT);
BUSTER_CT_CHECK(BUSTER_AARCH64_GENERATED_PRODUCTION_SCHEMA_VERSION == 1);
BUSTER_CT_CHECK(BUSTER_AARCH64_GENERATED_PRODUCTION_FORM_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_generated_production_forms));
BUSTER_CT_CHECK(BUSTER_AARCH64_GENERATED_PRODUCTION_FIELD_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_generated_production_fields));
BUSTER_CT_CHECK(BUSTER_AARCH64_GENERATED_PRODUCTION_SEGMENT_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_generated_production_segments));
BUSTER_CT_CHECK(BUSTER_AARCH64_GENERATED_PRODUCTION_FORM_COUNT < UINT16_MAX);
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(buster_aarch64_arm_m1_generated_fixed_rows) == BUSTER_AARCH64_ARM_M1_FIXED_SPELLING_COUNT);

#define A64_NO_PC_RELATIVE_OPERAND UINT8_MAX

#define A64_METADATA_FIELD_UNMAPPED BUSTER_AARCH64_GENERATED_FIELD_UNMAPPED

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL u32 a64_metadata_packed_access_counter;
BUSTER_GLOBAL_LOCAL void a64_metadata_note_packed_access(void)
{
    a64_metadata_packed_access_counter += 1;
}

void buster_aarch64_metadata_test_reset_packed_access_counter(void)
{
    a64_metadata_packed_access_counter = 0;
}

u32 buster_aarch64_metadata_test_packed_access_count(void)
{
    return a64_metadata_packed_access_counter;
}
#define A64_METADATA_PACKED_ACCESS() a64_metadata_note_packed_access()
#else
#define A64_METADATA_PACKED_ACCESS() ((void)0)
#endif

BUSTER_GLOBAL_LOCAL bool a64_metadata_count_range_valid(u32 total, u32 first, u32 count)
{
    return first <= total && count <= total - first;
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_string_descriptor(u32 offset, BusterAarch64MetadataString* result)
{
    A64_METADATA_PACKED_ACCESS();
    if (!result || offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE)
    {
        return false;
    }
    u32 length = 0;
    while (offset + length < BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE)
    {
        if (!buster_aarch64_generated_string_byte((u64)offset + length))
        {
            *result = (BusterAarch64MetadataString){.offset = offset, .length = length};
            return true;
        }
        length += 1;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_string_equals(u32 offset, char const* literal)
{
    if (!literal || offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE)
    {
        return false;
    }
    u32 index = 0;
    for (;; index += 1)
    {
        char8 actual = buster_aarch64_generated_string_byte((u64)offset + index);
        char expected = literal[index];
        if ((char)actual != expected)
        {
            return false;
        }
        if (!expected)
        {
            return true;
        }
        if (offset + index >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE - 1)
        {
            return false;
        }
    }
}

BUSTER_GLOBAL_LOCAL u32 a64_metadata_width_mask(u8 width)
{
    return width == 32 ? UINT32_MAX : (width ? (UINT32_C(1) << width) - 1u : 0);
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_generated_form(u32 form_id, BusterAarch64GeneratedForm* result)
{
    A64_METADATA_PACKED_ACCESS();
    if (!result || form_id >= BUSTER_AARCH64_GENERATED_FORM_COUNT)
    {
        return false;
    }
    BusterAarch64GeneratedForm form = buster_aarch64_generated_form_at(form_id);
    // normalized_form_id is the generated canonical row identity.  It also
    // prevents a zero-valued malformed accessor result from masquerading as
    // form zero.
    if (form.normalized_form_id != form_id || form.name_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||
        form.mnemonic_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || form.asm_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||
        form.out_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || form.in_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||
        !a64_metadata_count_range_valid(BUSTER_AARCH64_GENERATED_FIELD_COUNT, form.field_first, form.field_count) ||
        !a64_metadata_count_range_valid(BUSTER_AARCH64_GENERATED_OPERAND_COUNT, form.operand_first, form.operand_count) ||
        !a64_metadata_count_range_valid(BUSTER_AARCH64_GENERATED_PREDICATE_COUNT, form.predicate_first, form.predicate_count))
    {
        return false;
    }
    *result = form;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_generated_field(u32 field_id, BusterAarch64GeneratedField* result)
{
    A64_METADATA_PACKED_ACCESS();
    if (!result || field_id >= BUSTER_AARCH64_GENERATED_FIELD_COUNT)
    {
        return false;
    }
    BusterAarch64GeneratedField field = buster_aarch64_generated_field_at(field_id);
    if (field.name_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||
        !a64_metadata_count_range_valid(BUSTER_AARCH64_GENERATED_SEGMENT_COUNT, field.segment_first, field.segment_count))
    {
        return false;
    }
    *result = field;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_generated_segment(u32 segment_id, BusterAarch64GeneratedBitSegment* result)
{
    A64_METADATA_PACKED_ACCESS();
    if (!result || segment_id >= BUSTER_AARCH64_GENERATED_SEGMENT_COUNT)
    {
        return false;
    }
    // The generated accessor is bounded; structural value checks are done by
    // the raw-layout validator so descriptor inspection can still expose a
    // malformed segment to an audit caller.
    *result = buster_aarch64_generated_segment_at(segment_id);
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_form_raw_layout_complete(u32 form_id, BusterAarch64GeneratedForm* form_result)
{
    BusterAarch64GeneratedForm form = {0};
    if (!a64_metadata_generated_form(form_id, &form))
    {
        return false;
    }
    if (form.fixed_value & ~form.fixed_mask)
    {
        return false;
    }
    u32 instruction_used = form.fixed_mask;
    for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
    {
        BusterAarch64GeneratedField field = {0};
        if (!a64_metadata_generated_field(form.field_first + field_index, &field) || !field.segment_count || !field.source_mask || !field.width ||
            field.width > 32 || (field.flags & A64_METADATA_FIELD_UNMAPPED))
        {
            return false;
        }
        u32 source_used = 0;
        for (u32 segment_index = 0; segment_index < field.segment_count; segment_index += 1)
        {
            BusterAarch64GeneratedBitSegment segment = {0};
            if (!a64_metadata_generated_segment(field.segment_first + segment_index, &segment) || !segment.width || segment.width > 32 ||
                segment.instruction_lsb >= 32 || segment.value_lsb >= 32 || (u32)segment.instruction_lsb + segment.width > 32 ||
                (u32)segment.value_lsb + segment.width > 32)
            {
                return false;
            }
            u32 segment_mask = a64_metadata_width_mask(segment.width);
            u32 source_segment_mask = segment_mask << segment.value_lsb;
            u32 instruction_segment_mask = segment_mask << segment.instruction_lsb;
            if ((source_used & source_segment_mask) || (instruction_used & instruction_segment_mask))
            {
                return false;
            }
            source_used |= source_segment_mask;
            instruction_used |= instruction_segment_mask;
        }
        if (source_used != field.source_mask || (field.source_mask & ~a64_metadata_width_mask(field.width)))
        {
            return false;
        }
    }
    if (instruction_used != UINT32_MAX)
    {
        return false;
    }
    if (form_result)
    {
        *form_result = form;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL Target a64_metadata_apple_m1_target(void)
{
    // Keep the policy target independent of the host running the compiler.
    // The feature set is intentionally implicit here so target.c remains the
    // single authority for the pinned Apple-M1 default profile.
    return (Target){
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_A64_APPLE_M1,
        .os = OPERATING_SYSTEM_MACOS,
        .cpu_features_explicit = false,
    };
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_target_is_m1_profile(Target target)
{
    if (target.cpu_model == CPU_MODEL_A64_APPLE_M1)
    {
        return true;
    }
    return target.cpu_model == CPU_MODEL_NATIVE && target_native.cpu_arch == CPU_ARCH_AARCH64 &&
           target_native.cpu_model == CPU_MODEL_A64_APPLE_M1;
}

typedef struct A64MetadataPredicateFeature A64MetadataPredicateFeature;
struct A64MetadataPredicateFeature
{
    char const* name;
    TargetCpuFeature feature;
};

BUSTER_GLOBAL_LOCAL bool a64_metadata_predicate_feature(u32 predicate_offset, TargetCpuFeature* feature)
{
    static A64MetadataPredicateFeature const predicate_features[] = {
        {.name = "HasAES", .feature = TARGET_CPU_FEATURE_AARCH64_AES},
        {.name = "HasAltNZCV", .feature = TARGET_CPU_FEATURE_AARCH64_ALTNZCV},
        {.name = "HasCRC", .feature = TARGET_CPU_FEATURE_AARCH64_CRC},
        {.name = "HasComplxNum", .feature = TARGET_CPU_FEATURE_AARCH64_COMPLXNUM},
        {.name = "HasDotProd", .feature = TARGET_CPU_FEATURE_AARCH64_DOTPROD},
        {.name = "HasFP16FML", .feature = TARGET_CPU_FEATURE_AARCH64_FP16FML},
        {.name = "HasFPARMv8", .feature = TARGET_CPU_FEATURE_AARCH64_FP_ARMV8},
        {.name = "HasFRInt3264", .feature = TARGET_CPU_FEATURE_AARCH64_FPTOINT},
        {.name = "HasFlagM", .feature = TARGET_CPU_FEATURE_AARCH64_FLAGM},
        {.name = "HasFullFP16", .feature = TARGET_CPU_FEATURE_AARCH64_FULLFP16},
        {.name = "HasJS", .feature = TARGET_CPU_FEATURE_AARCH64_JSCONV},
        {.name = "HasLOR", .feature = TARGET_CPU_FEATURE_AARCH64_LOR},
        {.name = "HasLSE", .feature = TARGET_CPU_FEATURE_AARCH64_LSE},
        {.name = "HasNEON", .feature = TARGET_CPU_FEATURE_AARCH64_NEON},
        {.name = "HasPAuth", .feature = TARGET_CPU_FEATURE_AARCH64_PAUTH},
        {.name = "HasRCPC", .feature = TARGET_CPU_FEATURE_AARCH64_RCPC},
        {.name = "HasRCPC_IMMO", .feature = TARGET_CPU_FEATURE_AARCH64_RCPC_IMMO},
        {.name = "HasRDM", .feature = TARGET_CPU_FEATURE_AARCH64_RDM},
        {.name = "HasSB", .feature = TARGET_CPU_FEATURE_AARCH64_SB},
        {.name = "HasSHA2", .feature = TARGET_CPU_FEATURE_AARCH64_SHA2},
        {.name = "HasSHA3", .feature = TARGET_CPU_FEATURE_AARCH64_SHA3},
        {.name = "HasSME", .feature = TARGET_CPU_FEATURE_AARCH64_SME},
        {.name = "HasTRACEV8_4", .feature = TARGET_CPU_FEATURE_AARCH64_TRACEV8_4},
    };
    if (!feature)
    {
        return false;
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(predicate_features); index += 1)
    {
        if (a64_metadata_string_equals(predicate_offset, predicate_features[index].name))
        {
            *feature = predicate_features[index].feature;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_form_predicates_supported(BusterAarch64GeneratedForm form, Target target)
{
    if (target.cpu_arch != CPU_ARCH_AARCH64 || !target_cpu_features_are_valid(target))
    {
        return false;
    }
    // The importer omits malformed predicate expressions from the retained
    // predicate list, so the independent profile-flags bit is the runtime
    // evidence that the expression was not valid. Fail closed for every
    // AArch64 target, not only the Apple-M1 membership branch below.
    if (form.profile_flags & BUSTER_AARCH64_GENERATED_FORM_FLAG_PREDICATE_PARSE_ERROR)
    {
        return false;
    }
    TargetCpuFeatures features = target_cpu_features_effective(target);
    bool m1_profile = a64_metadata_target_is_m1_profile(target);
    if (m1_profile && !(form.profile_flags & BUSTER_AARCH64_GENERATED_FORM_FLAG_APPLE_M1_PROFILE_MEMBER))
    {
        // Membership is importer-derived and records parser failures that
        // cannot be reconstructed from the retained predicate strings.
        // Dynamic feature checks below still handle explicit removals.
        return false;
    }
    for (u32 predicate_index = 0; predicate_index < form.predicate_count; predicate_index += 1)
    {
        u32 offset = buster_aarch64_generated_predicate_at(form.predicate_first + predicate_index);
        if (a64_metadata_string_equals(offset, "HasEL3"))
        {
            // EL3 is deliberately not represented as a generic feature bit.
            // Keep it visible as a privileged/system predicate and gate it by
            // the pinned model capability only.
            if (!m1_profile)
            {
                return false;
            }
            continue;
        }
        if (a64_metadata_string_equals(offset, "HasNEONandIsStreamingSafe"))
        {
            // The runtime has no streaming-mode state. A non-streaming A64
            // target therefore treats this LLVM predicate as NEON support.
            if (!target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_NEON))
            {
                return false;
            }
            continue;
        }
        TargetCpuFeature feature = TARGET_CPU_FEATURE_NONE;
        if (!a64_metadata_predicate_feature(offset, &feature) || !target_cpu_features_contains(features, feature))
        {
            // Unknown predicates fail closed, as do known predicates whose
            // corresponding target extension is disabled.
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_form_m1_predicates(BusterAarch64GeneratedForm form)
{
    return (form.profile_flags & BUSTER_AARCH64_GENERATED_FORM_FLAG_APPLE_M1_PROFILE_MEMBER) &&
           a64_metadata_form_predicates_supported(form, a64_metadata_apple_m1_target());
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_form_and_layout(u32 form_id, BusterAarch64GeneratedForm* form, bool* raw_layout_complete)
{
    BusterAarch64GeneratedForm generated = {0};
    if (!a64_metadata_generated_form(form_id, &generated))
    {
        return false;
    }
    bool is_raw_layout_complete = a64_metadata_form_raw_layout_complete(form_id, 0);
    if (form)
    {
        *form = generated;
    }
    if (raw_layout_complete)
    {
        *raw_layout_complete = is_raw_layout_complete;
    }
    return true;
}

u32 buster_aarch64_metadata_schema_version(void)
{
    return BUSTER_AARCH64_GENERATED_SCHEMA_VERSION;
}

u32 buster_aarch64_metadata_form_count(void)
{
    return BUSTER_AARCH64_GENERATED_FORM_COUNT;
}

u32 buster_aarch64_metadata_field_count(void)
{
    return BUSTER_AARCH64_GENERATED_FIELD_COUNT;
}

u32 buster_aarch64_metadata_segment_count(void)
{
    return BUSTER_AARCH64_GENERATED_SEGMENT_COUNT;
}

u32 buster_aarch64_metadata_operand_count(void)
{
    return BUSTER_AARCH64_GENERATED_OPERAND_COUNT;
}

u32 buster_aarch64_metadata_predicate_count(void)
{
    return BUSTER_AARCH64_GENERATED_PREDICATE_COUNT;
}

u32 buster_aarch64_metadata_string_pool_size(void)
{
    return BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE;
}

BUSTER_GLOBAL_LOCAL char8 a64_metadata_ascii_lower(char8 value)
{
    return value >= 'A' && value <= 'Z' ? (char8)(value + ('a' - 'A')) : value;
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_string_case_equal(BusterAarch64MetadataString string, String8 wanted)
{
    if (!wanted.pointer || !wanted.length || string.length != wanted.length || string.offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||
        wanted.length > BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE - string.offset)
    {
        return false;
    }
    for (u32 index = 0; index < string.length; index += 1)
    {
        char8 actual = (char8)buster_aarch64_generated_string_byte((u64)string.offset + index);
        if (a64_metadata_ascii_lower(actual) != a64_metadata_ascii_lower(wanted.pointer[index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_candidate_range_valid(BusterAarch64MetadataCandidateRange range)
{
    return range.key.length != 0 && range.key.offset < BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE &&
           range.key.length <= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE - range.key.offset &&
           a64_metadata_count_range_valid(BUSTER_AARCH64_GENERATED_MNEMONIC_CANDIDATE_COUNT, range.candidate_first, range.candidate_count);
}

u32 buster_aarch64_metadata_mnemonic_range_count(void)
{
    return BUSTER_AARCH64_GENERATED_MNEMONIC_RANGE_COUNT;
}

bool buster_aarch64_metadata_mnemonic_range(u32 range_index, BusterAarch64MetadataCandidateRange* result)
{
    A64_METADATA_PACKED_ACCESS();
    if (!result || range_index >= BUSTER_AARCH64_GENERATED_MNEMONIC_RANGE_COUNT)
    {
        return false;
    }
    BusterAarch64GeneratedMnemonicRange generated = buster_aarch64_generated_mnemonic_range_at(range_index);
    BusterAarch64MetadataString key = {0};
    if (!a64_metadata_string_descriptor(generated.key_offset, &key))
    {
        return false;
    }
    BusterAarch64MetadataCandidateRange candidate_range = {
        .key = key,
        .candidate_first = generated.candidate_first,
        .candidate_count = generated.candidate_count,
    };
    if (!a64_metadata_candidate_range_valid(candidate_range))
    {
        return false;
    }
    *result = candidate_range;
    return true;
}

bool buster_aarch64_metadata_mnemonic_lookup(String8 mnemonic, BusterAarch64MetadataCandidateRange* result)
{
    A64_METADATA_PACKED_ACCESS();
    if (!result || !mnemonic.pointer || !mnemonic.length)
    {
        return false;
    }
    for (u32 range_index = 0; range_index < BUSTER_AARCH64_GENERATED_MNEMONIC_RANGE_COUNT; range_index += 1)
    {
        BusterAarch64MetadataCandidateRange candidate_range = {0};
        if (!buster_aarch64_metadata_mnemonic_range(range_index, &candidate_range))
        {
            return false;
        }
        if (a64_metadata_string_case_equal(candidate_range.key, mnemonic))
        {
            *result = candidate_range;
            return true;
        }
    }
    return false;
}

bool buster_aarch64_metadata_mnemonic_candidate(BusterAarch64MetadataCandidateRange range, u32 candidate_index, u32* form_id)
{
    A64_METADATA_PACKED_ACCESS();
    if (!form_id || !a64_metadata_candidate_range_valid(range) || candidate_index >= range.candidate_count)
    {
        return false;
    }
    u32 absolute_index = range.candidate_first + candidate_index;
    u32 candidate = buster_aarch64_generated_mnemonic_candidate_at(absolute_index);
    if (candidate == UINT32_MAX || candidate >= BUSTER_AARCH64_GENERATED_FORM_COUNT)
    {
        return false;
    }
    *form_id = candidate;
    return true;
}

BUSTER_GLOBAL_LOCAL u32 a64_fixed_row_string_length(char const* string)
{
    if (!string)
    {
        return 0;
    }
    u32 length = 0;
    while (string[length])
    {
        if (length == UINT32_MAX)
        {
            return 0;
        }
        length += 1;
    }
    return length;
}

BUSTER_GLOBAL_LOCAL bool a64_fixed_row_space(char8 value)
{
    return value == ' ' || value == '\t' || value == '\r';
}

BUSTER_GLOBAL_LOCAL bool a64_fixed_row_spelling_equal(String8 wanted, char const* expected)
{
    // Canonical rows contain a single space only for TSB CSYNC. Treat runs of
    // the existing parser whitespace as one separator, without accepting any
    // additional token or changing punctuation/operand spelling.
    u64 wanted_index = 0;
    u32 expected_index = 0;
    while (wanted_index < wanted.length || (expected && expected[expected_index]))
    {
        while (wanted_index < wanted.length && a64_fixed_row_space(wanted.pointer[wanted_index]))
        {
            wanted_index += 1;
        }
        while (expected && expected[expected_index] && a64_fixed_row_space((char8)expected[expected_index]))
        {
            expected_index += 1;
        }
        bool wanted_end = wanted_index == wanted.length;
        bool expected_end = !expected || !expected[expected_index];
        if (wanted_end || expected_end)
        {
            return wanted_end && expected_end;
        }
        while (wanted_index < wanted.length && !a64_fixed_row_space(wanted.pointer[wanted_index]) && expected[expected_index] &&
               !a64_fixed_row_space((char8)expected[expected_index]))
        {
            if (a64_metadata_ascii_lower(wanted.pointer[wanted_index]) != a64_metadata_ascii_lower((char8)expected[expected_index]))
            {
                return false;
            }
            wanted_index += 1;
            expected_index += 1;
        }
        bool wanted_token_end = wanted_index == wanted.length || a64_fixed_row_space(wanted.pointer[wanted_index]);
        bool expected_token_end = !expected[expected_index] || a64_fixed_row_space((char8)expected[expected_index]);
        if (wanted_token_end != expected_token_end)
        {
            return false;
        }
    }
    return true;
}

u32 buster_aarch64_arm_m1_fixed_spelling_count(void)
{
    return BUSTER_AARCH64_ARM_M1_FIXED_SPELLING_COUNT;
}

bool buster_aarch64_arm_m1_fixed_spelling(u32 index, BusterAarch64ArmM1FixedSpelling* result)
{
    if (!result || index >= BUSTER_AARCH64_ARM_M1_FIXED_SPELLING_COUNT)
    {
        return false;
    }
    BusterAarch64ArmM1GeneratedFixedRow const* row = buster_aarch64_arm_m1_generated_fixed_rows + index;
    u32 spelling_length = a64_fixed_row_string_length(row->spelling);
    u32 row_id_length = a64_fixed_row_string_length(row->arm_row_id);
    bool required_feature_valid = row->required_feature == TARGET_CPU_FEATURE_NONE ||
                                  (row->required_feature >= TARGET_CPU_FEATURE_AARCH64_V8_4A &&
                                   row->required_feature < TARGET_CPU_FEATURE_COUNT);
    if (!spelling_length || !row_id_length || (row->word & ~BUSTER_AARCH64_ARM_M1_FIXED_MASK) || row->canonical == row->alias ||
        !required_feature_valid)
    {
        return false;
    }
    *result = (BusterAarch64ArmM1FixedSpelling){
        .spelling = {.pointer = (char8*)row->spelling, .length = spelling_length},
        .arm_row_id = {.pointer = (char8*)row->arm_row_id, .length = row_id_length},
        .word = row->word,
        .arm_row_digest = row->arm_row_digest,
        .required_feature = row->required_feature,
        .canonical = row->canonical,
        .alias = row->alias,
        .system = row->system,
    };
    return true;
}

bool buster_aarch64_arm_m1_fixed_lookup(String8 spelling, BusterAarch64ArmM1FixedSpelling* result)
{
    if (!result || !spelling.pointer || !spelling.length)
    {
        return false;
    }
    for (u32 index = 0; index < BUSTER_AARCH64_ARM_M1_FIXED_SPELLING_COUNT; index += 1)
    {
        BusterAarch64ArmM1GeneratedFixedRow const* row = buster_aarch64_arm_m1_generated_fixed_rows + index;
        if (a64_fixed_row_spelling_equal(spelling, row->spelling))
        {
            return buster_aarch64_arm_m1_fixed_spelling(index, result);
        }
    }
    return false;
}

bool buster_aarch64_arm_m1_fixed_target(Target target)
{
    return target.cpu_arch == CPU_ARCH_AARCH64 && a64_metadata_target_is_m1_profile(target) && target_cpu_features_are_valid(target);
}

bool buster_aarch64_arm_m1_fixed_supported_for_target(BusterAarch64ArmM1FixedSpelling fixed, Target target)
{
    return buster_aarch64_arm_m1_fixed_target(target) &&
           (fixed.required_feature == TARGET_CPU_FEATURE_NONE || target_cpu_feature_has(target, fixed.required_feature));
}

BusterAarch64MetadataCounts buster_aarch64_metadata_counts(void)
{
    BusterAarch64MetadataCounts result = {
        .form_count = buster_aarch64_metadata_form_count(),
        .field_count = buster_aarch64_metadata_field_count(),
        .segment_count = buster_aarch64_metadata_segment_count(),
        .operand_count = buster_aarch64_metadata_operand_count(),
        .predicate_count = buster_aarch64_metadata_predicate_count(),
        .string_pool_size = buster_aarch64_metadata_string_pool_size(),
    };
    for (u32 form_id = 0; form_id < BUSTER_AARCH64_GENERATED_FORM_COUNT; form_id += 1)
    {
        BusterAarch64GeneratedForm form = {0};
        if (!a64_metadata_generated_form(form_id, &form) || !a64_metadata_form_m1_predicates(form))
        {
            continue;
        }
        result.apple_m1_supported_count += 1;
        if (a64_metadata_form_raw_layout_complete(form_id, 0))
        {
            result.apple_m1_raw_layout_complete_count += 1;
        }
    }
    result.apple_m1_raw_layout_incomplete_count = result.apple_m1_supported_count - result.apple_m1_raw_layout_complete_count;
    return result;
}

u8 buster_aarch64_metadata_string_byte(BusterAarch64MetadataString string, u32 index)
{
    A64_METADATA_PACKED_ACCESS();
    if (index >= string.length || string.offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||
        index >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE - string.offset)
    {
        return 0;
    }
    return (u8)buster_aarch64_generated_string_byte((u64)string.offset + index);
}

bool buster_aarch64_metadata_string(u32 offset, BusterAarch64MetadataString* result)
{
    A64_METADATA_PACKED_ACCESS();
    return a64_metadata_string_descriptor(offset, result);
}

bool buster_aarch64_metadata_form(u32 form_id, BusterAarch64MetadataForm* result)
{
    if (!result)
    {
        return false;
    }
    BusterAarch64GeneratedForm form = {0};
    bool raw_layout_complete = false;
    if (!a64_metadata_form_and_layout(form_id, &form, &raw_layout_complete))
    {
        return false;
    }
    BusterAarch64MetadataString strings[5] = {0};
    if (!a64_metadata_string_descriptor(form.name_offset, strings + 0) || !a64_metadata_string_descriptor(form.mnemonic_offset, strings + 1) ||
        !a64_metadata_string_descriptor(form.asm_offset, strings + 2) || !a64_metadata_string_descriptor(form.out_offset, strings + 3) ||
        !a64_metadata_string_descriptor(form.in_offset, strings + 4))
    {
        return false;
    }
    *result = (BusterAarch64MetadataForm){
        .id = form_id,
        .source_hash = form.source_hash,
        .name_hash = form.name_hash,
        .signature_hash = form.signature_hash,
        .name = strings[0],
        .mnemonic = strings[1],
        .assembly = strings[2],
        .output_operands = strings[3],
        .input_operands = strings[4],
        .field_first = form.field_first,
        .operand_first = form.operand_first,
        .predicate_first = form.predicate_first,
        .normalized_form_id = form.normalized_form_id,
        .field_count = form.field_count,
        .operand_count = form.operand_count,
        .predicate_count = form.predicate_count,
        .fixed_mask = form.fixed_mask,
        .fixed_value = form.fixed_value,
        .coverage_class = form.coverage_class,
        .encoder_family = form.encoder_family,
        .test_class = form.test_class,
        .reason_id = form.reason_id,
        .assembly_flags = form.asm_flags,
        .address_kind = form.address_kind,
        .address_flags = form.address_flags,
        .address_base_index = form.address_base_index,
        .address_offset_index = form.address_offset_index,
        .raw_layout_complete = raw_layout_complete,
        .apple_m1_profile_member = (form.profile_flags & BUSTER_AARCH64_GENERATED_FORM_FLAG_APPLE_M1_PROFILE_MEMBER) != 0,
        .predicate_parse_error = (form.profile_flags & BUSTER_AARCH64_GENERATED_FORM_FLAG_PREDICATE_PARSE_ERROR) != 0,
        .provisionally_apple_m1 = (form.profile_flags & BUSTER_AARCH64_GENERATED_FORM_FLAG_APPLE_M1_PROFILE_MEMBER) &&
                                  a64_metadata_form_predicates_supported(form, a64_metadata_apple_m1_target()),
    };
    return true;
}

bool buster_aarch64_metadata_field(u32 form_id, u32 field_index, BusterAarch64MetadataField* result)
{
    if (!result)
    {
        return false;
    }
    BusterAarch64GeneratedForm form = {0};
    if (!a64_metadata_generated_form(form_id, &form) || field_index >= form.field_count)
    {
        return false;
    }
    BusterAarch64GeneratedField field = {0};
    if (!a64_metadata_generated_field(form.field_first + field_index, &field))
    {
        return false;
    }
    BusterAarch64MetadataString name = {0};
    if (!a64_metadata_string_descriptor(field.name_offset, &name))
    {
        return false;
    }
    *result = (BusterAarch64MetadataField){
        .id = form.field_first + field_index,
        .name = name,
        .segment_first = field.segment_first,
        .source_mask = field.source_mask,
        .width = field.width,
        .segment_count = field.segment_count,
        .transform = field.transform,
        .relocation = field.relocation,
        .relocation_end = field.relocation_end,
        .shift = field.shift,
        .flags = field.flags,
    };
    return true;
}

bool buster_aarch64_metadata_segment(u32 form_id, u32 field_index, u32 segment_index, BusterAarch64MetadataSegment* result)
{
    if (!result)
    {
        return false;
    }
    BusterAarch64GeneratedForm form = {0};
    if (!a64_metadata_generated_form(form_id, &form) || field_index >= form.field_count)
    {
        return false;
    }
    BusterAarch64GeneratedField field = {0};
    if (!a64_metadata_generated_field(form.field_first + field_index, &field) || segment_index >= field.segment_count)
    {
        return false;
    }
    BusterAarch64GeneratedBitSegment segment = {0};
    if (!a64_metadata_generated_segment(field.segment_first + segment_index, &segment))
    {
        return false;
    }
    *result = (BusterAarch64MetadataSegment){
        .id = field.segment_first + segment_index,
        .instruction_lsb = segment.instruction_lsb,
        .width = segment.width,
        .value_lsb = segment.value_lsb,
    };
    return true;
}

bool buster_aarch64_metadata_operand(u32 form_id, u32 operand_index, BusterAarch64MetadataOperand* result)
{
    A64_METADATA_PACKED_ACCESS();
    if (!result)
    {
        return false;
    }
    BusterAarch64GeneratedForm form = {0};
    if (!a64_metadata_generated_form(form_id, &form) || operand_index >= form.operand_count)
    {
        return false;
    }
    BusterAarch64GeneratedOperand operand = buster_aarch64_generated_operand_at(form.operand_first + operand_index);
    if (operand.syntax_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || operand.type_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||
        operand.name_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE)
    {
        return false;
    }
    BusterAarch64MetadataString strings[3] = {0};
    if (!a64_metadata_string_descriptor(operand.syntax_offset, strings + 0) || !a64_metadata_string_descriptor(operand.type_offset, strings + 1) ||
        !a64_metadata_string_descriptor(operand.name_offset, strings + 2))
    {
        return false;
    }
    *result = (BusterAarch64MetadataOperand){
        .id = form.operand_first + operand_index,
        .syntax = strings[0],
        .type = strings[1],
        .name = strings[2],
        .field_index = operand.field_index,
        .immediate_min = operand.immediate_min,
        .immediate_max = operand.immediate_max,
        .register_width = operand.register_width,
        .tied_to = operand.tied_to,
        .address_base_index = operand.address_base_index,
        .address_offset_index = operand.address_offset_index,
        .direction = operand.direction,
        .kind = operand.kind,
        .flags = operand.flags,
        .scale = operand.scale,
        .immediate_flags = operand.immediate_flags,
        .address_kind = operand.address_kind,
        .address_flags = operand.address_flags,
    };
    return true;
}

bool buster_aarch64_metadata_predicate(u32 form_id, u32 predicate_index, BusterAarch64MetadataString* result)
{
    A64_METADATA_PACKED_ACCESS();
    if (!result)
    {
        return false;
    }
    BusterAarch64GeneratedForm form = {0};
    if (!a64_metadata_generated_form(form_id, &form) || predicate_index >= form.predicate_count)
    {
        return false;
    }
    return a64_metadata_string_descriptor(buster_aarch64_generated_predicate_at(form.predicate_first + predicate_index), result);
}

bool buster_aarch64_metadata_form_supported_for_target(u32 form_id, Target target)
{
    BusterAarch64GeneratedForm form = {0};
    return a64_metadata_generated_form(form_id, &form) && a64_metadata_form_predicates_supported(form, target);
}

bool buster_aarch64_metadata_form_supported(u32 form_id, BusterAarch64MetadataTarget target)
{
    if (target >= BUSTER_AARCH64_METADATA_TARGET_COUNT)
    {
        return false;
    }
    switch (target)
    {
    case BUSTER_AARCH64_METADATA_TARGET_APPLE_M1:
        return buster_aarch64_metadata_form_supported_for_target(form_id, a64_metadata_apple_m1_target());
    case BUSTER_AARCH64_METADATA_TARGET_COUNT:
        return false;
    }
    return false;
}

bool buster_aarch64_metadata_form_provisionally_apple_m1_supported(u32 form_id)
{
    return buster_aarch64_metadata_form_supported(form_id, BUSTER_AARCH64_METADATA_TARGET_APPLE_M1);
}

bool buster_aarch64_metadata_form_has_complete_raw_layout(u32 form_id)
{
    return a64_metadata_form_raw_layout_complete(form_id, 0);
}

BUSTER_GLOBAL_LOCAL bool a64_metadata_raw_layout(u32 form_id, BusterAarch64GeneratedForm* form_result)
{
    return a64_metadata_form_raw_layout_complete(form_id, form_result);
}

bool buster_aarch64_metadata_raw_encode(u32 form_id, u32 const* field_values, u32 field_count, u32* word)
{
    if (!word)
    {
        return false;
    }
    BusterAarch64GeneratedForm form = {0};
    if (!a64_metadata_raw_layout(form_id, &form) || field_count != form.field_count || (field_count && !field_values))
    {
        return false;
    }
    u32 result = form.fixed_value;
    for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
    {
        BusterAarch64GeneratedField field = {0};
        if (!a64_metadata_generated_field(form.field_first + field_index, &field) || (field_values[field_index] & ~field.source_mask))
        {
            return false;
        }
        u32 source_value = field_values[field_index];
        for (u32 segment_index = 0; segment_index < field.segment_count; segment_index += 1)
        {
            BusterAarch64GeneratedBitSegment segment = {0};
            if (!a64_metadata_generated_segment(field.segment_first + segment_index, &segment))
            {
                return false;
            }
            u32 mask = a64_metadata_width_mask(segment.width);
            result |= ((source_value >> segment.value_lsb) & mask) << segment.instruction_lsb;
        }
    }
    if ((result & form.fixed_mask) != form.fixed_value)
    {
        return false;
    }
    *word = result;
    return true;
}

bool buster_aarch64_metadata_raw_decode(u32 form_id, u32 word, u32* field_values, u32 field_count)
{
    BusterAarch64GeneratedForm form = {0};
    if (!a64_metadata_raw_layout(form_id, &form) || field_count != form.field_count || (field_count && !field_values) ||
        (word & form.fixed_mask) != form.fixed_value)
    {
        return false;
    }
    for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
    {
        BusterAarch64GeneratedField field = {0};
        if (!a64_metadata_generated_field(form.field_first + field_index, &field))
        {
            return false;
        }
        u32 source_value = 0;
        for (u32 segment_index = 0; segment_index < field.segment_count; segment_index += 1)
        {
            BusterAarch64GeneratedBitSegment segment = {0};
            if (!a64_metadata_generated_segment(field.segment_first + segment_index, &segment))
            {
                return false;
            }
            u32 mask = a64_metadata_width_mask(segment.width);
            source_value |= ((word >> segment.instruction_lsb) & mask) << segment.value_lsb;
        }
        field_values[field_index] = source_value;
    }
    return true;
}

u32 buster_aarch64_production_plan_form_count(void)
{
    return BUSTER_AARCH64_GENERATED_PRODUCTION_FORM_COUNT;
}

u32 buster_aarch64_production_plan_field_count(void)
{
    return BUSTER_AARCH64_GENERATED_PRODUCTION_FIELD_COUNT;
}

u32 buster_aarch64_production_plan_segment_count(void)
{
    return BUSTER_AARCH64_GENERATED_PRODUCTION_SEGMENT_COUNT;
}

BUSTER_GLOBAL_LOCAL u32 a64_production_width_mask(u8 width)
{
    return width == 32 ? UINT32_MAX : (width ? (UINT32_C(1) << width) - 1u : 0);
}

bool buster_aarch64_production_raw_encode(u32 form_id, u32 const* field_values, u32 field_count, u32* word)
{
    if (!word)
    {
        return false;
    }
    u16 plan_index = buster_aarch64_generated_production_plan_index(form_id);
    if (plan_index == UINT16_MAX)
    {
        return false;
    }
    BusterAarch64GeneratedProductionForm const* form = buster_aarch64_generated_production_form_at(plan_index);
    if (!form || field_count != form->field_count || (field_count && !field_values) ||
        (form->fixed_value & ~form->fixed_mask))
    {
        return false;
    }
    u32 result = form->fixed_value;
    for (u32 field_index = 0; field_index < form->field_count; field_index += 1)
    {
        BusterAarch64GeneratedProductionField const* field =
            buster_aarch64_generated_production_field_at(form->field_first + field_index);
        if (!field || field->segment_count == 0 || (field_values[field_index] & ~field->source_mask))
        {
            return false;
        }
        u32 source_value = field_values[field_index];
        for (u32 segment_index = 0; segment_index < field->segment_count; segment_index += 1)
        {
            BusterAarch64GeneratedProductionSegment const* segment =
                buster_aarch64_generated_production_segment_at(field->segment_first + segment_index);
            if (!segment || !segment->width || segment->width > 32 || segment->instruction_lsb >= 32 || segment->value_lsb >= 32 ||
                (u32)segment->instruction_lsb + segment->width > 32 || (u32)segment->value_lsb + segment->width > 32)
            {
                return false;
            }
            u32 mask = a64_production_width_mask(segment->width);
            result |= ((source_value >> segment->value_lsb) & mask) << segment->instruction_lsb;
        }
    }
    if ((result & form->fixed_mask) != form->fixed_value)
    {
        return false;
    }
    *word = result;
    return true;
}

u32 a64_generated_form_count(void)
{
    return buster_aarch64_metadata_form_count();
}

u32 a64_generated_field_count(void)
{
    return buster_aarch64_metadata_field_count();
}

bool a64_generated_form(u32 form_id, BusterAarch64MetadataForm* result)
{
    return buster_aarch64_metadata_form(form_id, result);
}

bool a64_generated_raw_encode(u32 form_id, u32 const* field_values, u32 field_count, u32* word)
{
    return buster_aarch64_metadata_raw_encode(form_id, field_values, field_count, word);
}

bool a64_generated_raw_decode(u32 form_id, u32 word, u32* field_values, u32 field_count)
{
    return buster_aarch64_metadata_raw_decode(form_id, word, field_values, field_count);
}

#if BUSTER_INCLUDE_TESTS
bool buster_aarch64_metadata_test_predicate_parse_error_fails_closed(Target target)
{
    BusterAarch64GeneratedForm malformed = {
        .profile_flags = BUSTER_AARCH64_GENERATED_FORM_FLAG_PREDICATE_PARSE_ERROR,
    };
    return !a64_metadata_form_predicates_supported(malformed, target);
}
#endif

bool a64_generated_production_raw_encode(u32 form_id, u32 const* field_values, u32 field_count, u32* word)
{
    return buster_aarch64_production_raw_encode(form_id, field_values, field_count, word);
}

BUSTER_GLOBAL_LOCAL A64OpcodeDescriptor const a64_opcode_descriptors[A64_OPCODE_COUNT] = {
    [A64_OPCODE_NOP] =
        {
            .fixed_mask = UINT32_MAX,
            .fixed_value = UINT32_C(0xd503201f),
            .pc_relative_operand = A64_NO_PC_RELATIVE_OPERAND,
        },
    [A64_OPCODE_B] =
        {
            .fixed_mask = UINT32_C(0xfc000000),
            .fixed_value = UINT32_C(0x14000000),
            .operand_count = 1,
            .pc_relative_operand = 0,
            .pc_relative_layout = A64_PC_RELATIVE_IMM26,
        },
    [A64_OPCODE_BL] =
        {
            .fixed_mask = UINT32_C(0xfc000000),
            .fixed_value = UINT32_C(0x94000000),
            .operand_count = 1,
            .pc_relative_operand = 0,
            .pc_relative_layout = A64_PC_RELATIVE_IMM26,
        },
    [A64_OPCODE_B_COND] =
        {
            .fixed_mask = UINT32_C(0xff000010),
            .fixed_value = UINT32_C(0x54000000),
            .operand_count = 2,
            .pc_relative_operand = 0,
            .pc_relative_layout = A64_PC_RELATIVE_IMM19,
        },
    [A64_OPCODE_RET] =
        {
            .fixed_mask = UINT32_C(0xfffffc1f),
            .fixed_value = UINT32_C(0xd65f0000),
            .operand_count = 1,
            .pc_relative_operand = A64_NO_PC_RELATIVE_OPERAND,
        },
    [A64_OPCODE_BR] =
        {
            .fixed_mask = UINT32_C(0xfffffc1f),
            .fixed_value = UINT32_C(0xd61f0000),
            .operand_count = 1,
            .pc_relative_operand = A64_NO_PC_RELATIVE_OPERAND,
        },
    [A64_OPCODE_BLR] =
        {
            .fixed_mask = UINT32_C(0xfffffc1f),
            .fixed_value = UINT32_C(0xd63f0000),
            .operand_count = 1,
            .pc_relative_operand = A64_NO_PC_RELATIVE_OPERAND,
        },
    [A64_OPCODE_LDR_LITERAL_64] =
        {
            .fixed_mask = UINT32_C(0xff000000),
            .fixed_value = UINT32_C(0x58000000),
            .operand_count = 2,
            .pc_relative_operand = 1,
            .pc_relative_layout = A64_PC_RELATIVE_IMM19,
        },
    [A64_OPCODE_ADRP] =
        {
            .fixed_mask = UINT32_C(0x9f000000),
            .fixed_value = UINT32_C(0x90000000),
            .operand_count = 2,
            .pc_relative_operand = 1,
            .pc_relative_layout = A64_PC_RELATIVE_ADRP,
        },
    [A64_OPCODE_ADR] =
        {
            .fixed_mask = UINT32_C(0x9f000000),
            .fixed_value = UINT32_C(0x10000000),
            .operand_count = 2,
            .pc_relative_operand = 1,
            .pc_relative_layout = A64_PC_RELATIVE_ADR,
        },
    [A64_OPCODE_CBZ_W] =
        {
            .fixed_mask = UINT32_C(0xff000000),
            .fixed_value = UINT32_C(0x34000000),
            .operand_count = 2,
            .pc_relative_operand = 1,
            .pc_relative_layout = A64_PC_RELATIVE_IMM19,
        },
    [A64_OPCODE_CBNZ_W] =
        {
            .fixed_mask = UINT32_C(0xff000000),
            .fixed_value = UINT32_C(0x35000000),
            .operand_count = 2,
            .pc_relative_operand = 1,
            .pc_relative_layout = A64_PC_RELATIVE_IMM19,
        },
    [A64_OPCODE_CBZ_X] =
        {
            .fixed_mask = UINT32_C(0xff000000),
            .fixed_value = UINT32_C(0xb4000000),
            .operand_count = 2,
            .pc_relative_operand = 1,
            .pc_relative_layout = A64_PC_RELATIVE_IMM19,
        },
    [A64_OPCODE_CBNZ_X] =
        {
            .fixed_mask = UINT32_C(0xff000000),
            .fixed_value = UINT32_C(0xb5000000),
            .operand_count = 2,
            .pc_relative_operand = 1,
            .pc_relative_layout = A64_PC_RELATIVE_IMM19,
        },
    [A64_OPCODE_TBZ] =
        {
            .fixed_mask = UINT32_C(0x7f000000),
            .fixed_value = UINT32_C(0x36000000),
            .operand_count = 3,
            .pc_relative_operand = 2,
            .pc_relative_layout = A64_PC_RELATIVE_IMM14,
        },
    [A64_OPCODE_TBNZ] =
        {
            .fixed_mask = UINT32_C(0x7f000000),
            .fixed_value = UINT32_C(0x37000000),
            .operand_count = 3,
            .pc_relative_operand = 2,
            .pc_relative_layout = A64_PC_RELATIVE_IMM14,
        },
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(a64_opcode_descriptors) == A64_OPCODE_COUNT);

A64OpcodeDescriptor const* a64_opcode_descriptor(A64Opcode opcode)
{
    if (!opcode || opcode >= A64_OPCODE_COUNT)
    {
        return 0;
    }
    A64OpcodeDescriptor const* descriptor = a64_opcode_descriptors + opcode;
    // Keep malformed table rows inert.  The descriptor is the trust boundary
    // for all three entry points below, so an invalid row must never be used
    // to shift or mask caller-controlled values.
    if (descriptor->fixed_value & ~descriptor->fixed_mask || descriptor->operand_count > A64_MC_MAX_OPERANDS)
    {
        return 0;
    }
    bool has_pc_relative_operand = descriptor->pc_relative_operand != A64_NO_PC_RELATIVE_OPERAND;
    if (has_pc_relative_operand && descriptor->pc_relative_operand >= descriptor->operand_count)
    {
        return 0;
    }
    if (!has_pc_relative_operand && descriptor->pc_relative_layout != A64_PC_RELATIVE_NONE)
    {
        return 0;
    }
    if (has_pc_relative_operand)
    {
        // Every supported layout has a symmetric insert/extract pair. Keep
        // this explicit so an accidentally added enum value cannot make
        // patching accept a layout with no implementation.
        switch ((A64PCRelativeLayout)descriptor->pc_relative_layout)
        {
        case A64_PC_RELATIVE_IMM26:
        case A64_PC_RELATIVE_IMM19:
        case A64_PC_RELATIVE_ADRP:
        case A64_PC_RELATIVE_IMM14:
        case A64_PC_RELATIVE_ADR:
            break;
        case A64_PC_RELATIVE_NONE:
        case A64_PC_RELATIVE_LAYOUT_COUNT:
            return 0;
        }
    }
    return descriptor;
}

bool a64_signed_scaled_immediate_encode(s64 value, u8 bits, u8 scale_log2, u32* encoded)
{
    if (!encoded || !bits || bits > 32 || scale_log2 > 31 || (u32)bits + scale_log2 > 63)
    {
        return false;
    }
    u64 scale = UINT64_C(1) << scale_log2;
    if ((u64)value & (scale - 1))
    {
        return false;
    }
    s64 scaled = value / (s64)scale;
    s64 minimum = -(INT64_C(1) << (bits - 1));
    s64 maximum = (INT64_C(1) << (bits - 1)) - 1;
    if (scaled < minimum || scaled > maximum)
    {
        return false;
    }
    u64 mask = (UINT64_C(1) << bits) - 1;
    *encoded = (u32)((u64)scaled & mask);
    return true;
}

bool a64_signed_scaled_immediate_decode(u32 encoded, u8 bits, u8 scale_log2, s64* value)
{
    if (!value || !bits || bits > 32 || scale_log2 > 31 || (u32)bits + scale_log2 > 63)
    {
        return false;
    }
    u64 limit = UINT64_C(1) << bits;
    u64 mask = limit - 1;
    if ((u64)encoded & ~mask)
    {
        return false;
    }
    u64 sign = limit >> 1;
    s64 scaled = (u64)encoded < sign ? (s64)encoded : -(s64)(limit - encoded);
    *value = scaled * (s64)(UINT64_C(1) << scale_log2);
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_mc_operand(A64MCInst const* instruction, u32 index, A64MCOperandKind kind, s64* value)
{
    if (!instruction || index >= instruction->operand_count || index >= A64_MC_MAX_OPERANDS || instruction->operands[index].kind != kind)
    {
        return false;
    }
    if (value)
    {
        *value = instruction->operands[index].value;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_pc_relative_insert(A64PCRelativeLayout layout, u32 word, s64 displacement, u32* patched)
{
    u32 immediate = 0;
    if (!patched)
    {
        return false;
    }
    switch (layout)
    {
    case A64_PC_RELATIVE_IMM26:
        if (!a64_signed_scaled_immediate_encode(displacement, 26, 2, &immediate))
        {
            return false;
        }
        *patched = (word & ~UINT32_C(0x03ffffff)) | immediate;
        return true;
    case A64_PC_RELATIVE_IMM19:
        if (!a64_signed_scaled_immediate_encode(displacement, 19, 2, &immediate))
        {
            return false;
        }
        *patched = (word & ~UINT32_C(0x00ffffe0)) | (immediate << 5);
        return true;
    case A64_PC_RELATIVE_IMM14:
        if (!a64_signed_scaled_immediate_encode(displacement, 14, 2, &immediate))
        {
            return false;
        }
        *patched = (word & ~UINT32_C(0x0007ffe0)) | (immediate << 5);
        return true;
    case A64_PC_RELATIVE_ADRP:
        if (!a64_signed_scaled_immediate_encode(displacement, 21, 12, &immediate))
        {
            return false;
        }
        *patched = (word & ~UINT32_C(0x60ffffe0)) | ((immediate & 3) << 29) | (((immediate >> 2) & UINT32_C(0x7ffff)) << 5);
        return true;
    case A64_PC_RELATIVE_ADR:
        if (!a64_signed_scaled_immediate_encode(displacement, 21, 0, &immediate))
        {
            return false;
        }
        *patched = (word & ~UINT32_C(0x60ffffe0)) | ((immediate & 3) << 29) | (((immediate >> 2) & UINT32_C(0x7ffff)) << 5);
        return true;
    case A64_PC_RELATIVE_NONE:
    case A64_PC_RELATIVE_LAYOUT_COUNT:
        return false;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool a64_pc_relative_extract(A64PCRelativeLayout layout, u32 word, s64* displacement)
{
    u32 immediate = 0;
    switch (layout)
    {
    case A64_PC_RELATIVE_IMM26:
        immediate = word & UINT32_C(0x03ffffff);
        return a64_signed_scaled_immediate_decode(immediate, 26, 2, displacement);
    case A64_PC_RELATIVE_IMM19:
        immediate = (word >> 5) & UINT32_C(0x7ffff);
        return a64_signed_scaled_immediate_decode(immediate, 19, 2, displacement);
    case A64_PC_RELATIVE_IMM14:
        immediate = (word >> 5) & UINT32_C(0x3fff);
        return a64_signed_scaled_immediate_decode(immediate, 14, 2, displacement);
    case A64_PC_RELATIVE_ADRP:
        immediate = ((word >> 29) & 3) | (((word >> 5) & UINT32_C(0x7ffff)) << 2);
        return a64_signed_scaled_immediate_decode(immediate, 21, 12, displacement);
    case A64_PC_RELATIVE_ADR:
        immediate = ((word >> 29) & 3) | (((word >> 5) & UINT32_C(0x7ffff)) << 2);
        return a64_signed_scaled_immediate_decode(immediate, 21, 0, displacement);
    case A64_PC_RELATIVE_NONE:
    case A64_PC_RELATIVE_LAYOUT_COUNT:
        return false;
    }
    return false;
}

bool a64_pc_relative_patch(A64Opcode opcode, u32 word, s64 displacement, u32* patched)
{
    A64OpcodeDescriptor const* descriptor = a64_opcode_descriptor(opcode);
    return descriptor && descriptor->pc_relative_operand != A64_NO_PC_RELATIVE_OPERAND && (word & descriptor->fixed_mask) == descriptor->fixed_value &&
           a64_pc_relative_insert((A64PCRelativeLayout)descriptor->pc_relative_layout, word, displacement, patched);
}

bool a64_mc_encode(A64MCInst const* instruction, u32* word)
{
    if (!instruction || !word)
    {
        return false;
    }
    A64OpcodeDescriptor const* descriptor = a64_opcode_descriptor(instruction->opcode);
    if (!descriptor || instruction->operand_count != descriptor->operand_count)
    {
        return false;
    }
    u32 result = descriptor->fixed_value;
    s64 value = 0;
    switch (instruction->opcode)
    {
    case A64_OPCODE_NOP:
        break;
    case A64_OPCODE_B:
    case A64_OPCODE_BL:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_PC_RELATIVE, &value) ||
            !a64_pc_relative_insert((A64PCRelativeLayout)descriptor->pc_relative_layout, result, value, &result))
        {
            return false;
        }
        break;
    case A64_OPCODE_B_COND:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_PC_RELATIVE, &value) ||
            !a64_pc_relative_insert((A64PCRelativeLayout)descriptor->pc_relative_layout, result, value, &result) ||
            !a64_mc_operand(instruction, 1, A64_MC_OPERAND_IMMEDIATE, &value) || value < 0 || value > 15)
        {
            return false;
        }
        result |= (u32)value;
        break;
    case A64_OPCODE_RET:
    case A64_OPCODE_BR:
    case A64_OPCODE_BLR:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_REGISTER, &value) || value < 0 || value > 31)
        {
            return false;
        }
        result |= (u32)value << 5;
        break;
    case A64_OPCODE_LDR_LITERAL_64:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_REGISTER, &value) || value < 0 || value > 31)
        {
            return false;
        }
        result |= (u32)value;
        if (!a64_mc_operand(instruction, 1, A64_MC_OPERAND_PC_RELATIVE, &value) ||
            !a64_pc_relative_insert((A64PCRelativeLayout)descriptor->pc_relative_layout, result, value, &result))
        {
            return false;
        }
        break;
    case A64_OPCODE_ADRP:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_REGISTER, &value) || value < 0 || value > 31)
        {
            return false;
        }
        result |= (u32)value;
        if (!a64_mc_operand(instruction, 1, A64_MC_OPERAND_PC_RELATIVE, &value) ||
            !a64_pc_relative_insert((A64PCRelativeLayout)descriptor->pc_relative_layout, result, value, &result))
        {
            return false;
        }
        break;
    case A64_OPCODE_ADR:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_REGISTER, &value) || value < 0 || value > 31)
        {
            return false;
        }
        result |= (u32)value;
        if (!a64_mc_operand(instruction, 1, A64_MC_OPERAND_PC_RELATIVE, &value) ||
            !a64_pc_relative_insert((A64PCRelativeLayout)descriptor->pc_relative_layout, result, value, &result))
        {
            return false;
        }
        break;
    case A64_OPCODE_CBZ_W:
    case A64_OPCODE_CBNZ_W:
    case A64_OPCODE_CBZ_X:
    case A64_OPCODE_CBNZ_X:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_REGISTER, &value) || value < 0 || value > 31)
        {
            return false;
        }
        result |= (u32)value;
        if (!a64_mc_operand(instruction, 1, A64_MC_OPERAND_PC_RELATIVE, &value) ||
            !a64_pc_relative_insert((A64PCRelativeLayout)descriptor->pc_relative_layout, result, value, &result))
        {
            return false;
        }
        break;
    case A64_OPCODE_TBZ:
    case A64_OPCODE_TBNZ:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_REGISTER, &value) || value < 0 || value > 31)
        {
            return false;
        }
        result |= (u32)value;
        if (!a64_mc_operand(instruction, 1, A64_MC_OPERAND_IMMEDIATE, &value) || value < 0 || value > 63)
        {
            return false;
        }
        result |= ((u32)value & 0x1f) << 19;
        result |= ((u32)value >> 5) << 31;
        if (!a64_mc_operand(instruction, 2, A64_MC_OPERAND_PC_RELATIVE, &value) ||
            !a64_pc_relative_insert((A64PCRelativeLayout)descriptor->pc_relative_layout, result, value, &result))
        {
            return false;
        }
        break;
    case A64_OPCODE_INVALID:
    case A64_OPCODE_COUNT:
        return false;
    }
    *word = result;
    return true;
}

BUSTER_GLOBAL_LOCAL A64MCOperand a64_mc_operand_make(A64MCOperandKind kind, s64 value)
{
    return (A64MCOperand){.value = value, .kind = (u8)kind};
}

bool a64_mc_decode(u32 word, A64MCInst* instruction)
{
    if (!instruction)
    {
        return false;
    }
    // Specific fixed forms precede the broad PC-relative masks. The current
    // descriptors have no decode collisions; this explicit order becomes the
    // seed for the generated decoder's priority table.
    static A64Opcode const decode_order[] = {
        A64_OPCODE_NOP,  A64_OPCODE_RET, A64_OPCODE_BR, A64_OPCODE_BLR, A64_OPCODE_B_COND, A64_OPCODE_LDR_LITERAL_64,
        A64_OPCODE_ADRP, A64_OPCODE_ADR, A64_OPCODE_CBZ_W, A64_OPCODE_CBNZ_W, A64_OPCODE_CBZ_X, A64_OPCODE_CBNZ_X,
        A64_OPCODE_TBZ,   A64_OPCODE_TBNZ, A64_OPCODE_BL, A64_OPCODE_B,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(decode_order); index += 1)
    {
        A64Opcode opcode = decode_order[index];
        A64OpcodeDescriptor const* descriptor = a64_opcode_descriptor(opcode);
        if ((word & descriptor->fixed_mask) != descriptor->fixed_value)
        {
            continue;
        }
        A64MCInst result = {.opcode = opcode, .operand_count = descriptor->operand_count};
        s64 displacement = 0;
        switch (opcode)
        {
        case A64_OPCODE_NOP:
            break;
        case A64_OPCODE_B:
        case A64_OPCODE_BL:
            if (!a64_pc_relative_extract((A64PCRelativeLayout)descriptor->pc_relative_layout, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            break;
        case A64_OPCODE_B_COND:
            if (!a64_pc_relative_extract((A64PCRelativeLayout)descriptor->pc_relative_layout, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            result.operands[1] = a64_mc_operand_make(A64_MC_OPERAND_IMMEDIATE, word & 15);
            break;
        case A64_OPCODE_RET:
        case A64_OPCODE_BR:
        case A64_OPCODE_BLR:
        {
            u32 register_number = (word >> 5) & 31;
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_REGISTER, register_number);
        }
        break;
        case A64_OPCODE_LDR_LITERAL_64:
            if (!a64_pc_relative_extract((A64PCRelativeLayout)descriptor->pc_relative_layout, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_REGISTER, word & 31);
            result.operands[1] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            break;
        case A64_OPCODE_ADRP:
            if (!a64_pc_relative_extract((A64PCRelativeLayout)descriptor->pc_relative_layout, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_REGISTER, word & 31);
            result.operands[1] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            break;
        case A64_OPCODE_ADR:
            if (!a64_pc_relative_extract((A64PCRelativeLayout)descriptor->pc_relative_layout, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_REGISTER, word & 31);
            result.operands[1] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            break;
        case A64_OPCODE_CBZ_W:
        case A64_OPCODE_CBNZ_W:
        case A64_OPCODE_CBZ_X:
        case A64_OPCODE_CBNZ_X:
            if (!a64_pc_relative_extract((A64PCRelativeLayout)descriptor->pc_relative_layout, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_REGISTER, word & 31);
            result.operands[1] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            break;
        case A64_OPCODE_TBZ:
        case A64_OPCODE_TBNZ:
        {
            if (!a64_pc_relative_extract((A64PCRelativeLayout)descriptor->pc_relative_layout, word, &displacement))
            {
                return false;
            }
            u32 bit = ((word >> 31) & 1) << 5 | ((word >> 19) & 0x1f);
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_REGISTER, word & 31);
            result.operands[1] = a64_mc_operand_make(A64_MC_OPERAND_IMMEDIATE, bit);
            result.operands[2] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
        }
        break;
        case A64_OPCODE_INVALID:
        case A64_OPCODE_COUNT:
            return false;
        }
        *instruction = result;
        return true;
    }
    return false;
}

bool a64_pc_relative_displacement(u64 target, u64 place, s64 addend, s64* displacement)
{
    if (!displacement)
    {
        return false;
    }
    bool addend_negative = addend < 0;
    u64 addend_magnitude = addend_negative ? (u64)(-(addend + 1)) + 1 : (u64)addend;
    bool result_negative = false;
    u64 result_magnitude = 0;
    if (target >= place)
    {
        u64 difference = target - place;
        if (!addend_negative)
        {
            if (difference > (u64)INT64_MAX - addend_magnitude)
            {
                return false;
            }
            result_magnitude = difference + addend_magnitude;
        }
        else if (difference >= addend_magnitude)
        {
            result_magnitude = difference - addend_magnitude;
            if (result_magnitude > (u64)INT64_MAX)
            {
                return false;
            }
        }
        else
        {
            result_negative = true;
            result_magnitude = addend_magnitude - difference;
        }
    }
    else
    {
        u64 difference = place - target;
        if (!addend_negative)
        {
            if (addend_magnitude >= difference)
            {
                result_magnitude = addend_magnitude - difference;
            }
            else
            {
                result_negative = true;
                result_magnitude = difference - addend_magnitude;
                if (result_magnitude > (u64)INT64_MAX + 1)
                {
                    return false;
                }
            }
        }
        else
        {
            if (difference > (u64)INT64_MAX + 1 - addend_magnitude)
            {
                return false;
            }
            result_negative = true;
            result_magnitude = difference + addend_magnitude;
        }
    }
    if (result_negative)
    {
        *displacement = result_magnitude == (u64)INT64_MAX + 1 ? INT64_MIN : -(s64)result_magnitude;
    }
    else
    {
        *displacement = (s64)result_magnitude;
    }
    return true;
}

bool a64_adrp_encode(u32 destination_register, u64 instruction_address, u64 target_address, u32* word)
{
    if (!word || destination_register > 31)
    {
        return false;
    }
    u64 instruction_page = instruction_address & ~UINT64_C(0xfff);
    u64 target_page = target_address & ~UINT64_C(0xfff);
    u64 delta = target_page - instruction_page;
    s64 displacement = 0;
    if (delta <= UINT64_C(0xfffff000))
    {
        displacement = (s64)delta;
    }
    else
    {
        u64 distance = 0 - delta;
        if (distance > UINT64_C(0x100000000))
        {
            return false;
        }
        displacement = -(s64)distance;
    }
    A64MCInst instruction = {
        .operands =
            {
                {.value = destination_register, .kind = A64_MC_OPERAND_REGISTER},
                {.value = displacement, .kind = A64_MC_OPERAND_PC_RELATIVE},
            },
        .opcode = A64_OPCODE_ADRP,
        .operand_count = 2,
    };
    return a64_mc_encode(&instruction, word);
}

bool a64_adr_encode(u32 destination_register, u64 instruction_address, u64 target_address, u32* word)
{
    if (!word || destination_register > 31)
    {
        return false;
    }
    s64 displacement = 0;
    if (!a64_pc_relative_displacement(target_address, instruction_address, 0, &displacement))
    {
        return false;
    }
    A64MCInst instruction = {
        .operands =
            {
                {.value = destination_register, .kind = A64_MC_OPERAND_REGISTER},
                {.value = displacement, .kind = A64_MC_OPERAND_PC_RELATIVE},
            },
        .opcode = A64_OPCODE_ADR,
        .operand_count = 2,
    };
    return a64_mc_encode(&instruction, word);
}

bool a64_condition_invert(u32 condition, u32* inverse)
{
    if (!inverse || condition > 13)
    {
        return false;
    }
    *inverse = condition ^ 1;
    return true;
}
