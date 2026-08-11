#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/string.h>

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
#include <buster/lib/compiler/assembly/generated/arm-a64-m1-gpr.generated.h>
#include <buster/lib/compiler/assembly/generated/arm-a64-m1-scalar-integer.generated.h>
#include <buster/lib/compiler/assembly/generated/aarch64-canonical-decoder.generated.h>

BUSTER_CT_CHECK(BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT == 1523);
BUSTER_CT_CHECK(BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_canonical_decoder_forms));
BUSTER_CT_CHECK(BUSTER_AARCH64_CANONICAL_DECODER_FIELD_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_canonical_decoder_fields));
BUSTER_CT_CHECK(BUSTER_AARCH64_CANONICAL_DECODER_FIELD_SEGMENT_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_canonical_decoder_field_segments));
BUSTER_CT_CHECK(BUSTER_AARCH64_CANONICAL_DECODER_SOURCE_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_canonical_decoder_sources));
BUSTER_CT_CHECK(BUSTER_AARCH64_CANONICAL_DECODER_SOURCE_SEGMENT_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_canonical_decoder_source_segments));
BUSTER_CT_CHECK(BUSTER_AARCH64_CANONICAL_DECODER_CONSTRAINT_PROGRAM_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_canonical_decoder_programs));
BUSTER_CT_CHECK(BUSTER_AARCH64_CANONICAL_DECODER_CONSTRAINT_TOKEN_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_canonical_decoder_tokens));
BUSTER_CT_CHECK(BUSTER_AARCH64_CANONICAL_DECODER_FEATURE_PROGRAM_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_canonical_decoder_features));

BUSTER_CT_CHECK((u32)A64_GPR_REGISTER31_ZR == (u32)BUSTER_AARCH64_ARM_M1_GPR_31_ZR);
BUSTER_CT_CHECK((u32)A64_GPR_REGISTER31_SP == (u32)BUSTER_AARCH64_ARM_M1_GPR_31_SP);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_FORM_COUNT == 80);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_MNEMONIC_COUNT == 63);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_ARITY_1_COUNT == 18);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_ARITY_2_COUNT == 23);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_ARITY_3_COUNT == 31);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_ARITY_4_COUNT == 8);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_BASELINE_COUNT == 43);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_CRC32_COUNT == 8);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_FLAGM_COUNT == 2);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_GPR_PAUTH_COUNT == 27);

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
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT == BUSTER_ARRAY_LENGTH(buster_aarch64_arm_m1_scalar_integer_generated_forms));
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT == 72);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_MNEMONIC_COUNT == 23);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_1_COUNT == 1);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_2_COUNT == 6);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_3_COUNT == 49);
BUSTER_CT_CHECK(BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_4_COUNT == 16);

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

BUSTER_GLOBAL_LOCAL char a64_gpr_ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value;
}

BUSTER_GLOBAL_LOCAL bool a64_gpr_string_equal(String8 string, char const* literal)
{
    if (!string.pointer || !string.length || !literal)
    {
        return false;
    }
    u64 index = 0;
    while (literal[index])
    {
        if (index >= string.length || a64_gpr_ascii_lower((char)string.pointer[index]) != a64_gpr_ascii_lower(literal[index]))
        {
            return false;
        }
        index += 1;
    }
    return index == string.length;
}

BUSTER_GLOBAL_LOCAL bool a64_gpr_generated_form_valid(BusterAarch64ArmM1GprGeneratedForm const* form)
{
    if (!form || !form->mnemonic || !form->arm_row_id || !form->arm_row_digest || !form->operand_count || form->operand_count > 4 ||
        (form->fixed_value & ~form->fixed_mask) || (form->reserved[0] | form->reserved[1] | form->reserved[2]) ||
        (form->required_feature != TARGET_CPU_FEATURE_NONE && form->required_feature != TARGET_CPU_FEATURE_AARCH64_CRC &&
         form->required_feature != TARGET_CPU_FEATURE_AARCH64_FLAGM && form->required_feature != TARGET_CPU_FEATURE_AARCH64_PAUTH))
    {
        return false;
    }
    u32 used = form->fixed_mask;
    for (u32 index = 0; index < form->operand_count; index += 1)
    {
        BusterAarch64ArmM1GprGeneratedOperand operand = form->operands[index];
        if ((operand.width != 32 && operand.width != 64) || operand.bit_lsb >= 32 || operand.bit_lsb > 27 || operand.reserved ||
            (operand.register31_role != BUSTER_AARCH64_ARM_M1_GPR_31_ZR && operand.register31_role != BUSTER_AARCH64_ARM_M1_GPR_31_SP))
        {
            return false;
        }
        u32 field_mask = UINT32_C(0x1f) << operand.bit_lsb;
        if (used & field_mask)
        {
            return false;
        }
        used |= field_mask;
    }
    for (u32 index = form->operand_count; index < 4; index += 1)
    {
        BusterAarch64ArmM1GprGeneratedOperand operand = form->operands[index];
        if (operand.width || operand.bit_lsb || operand.register31_role || operand.reserved)
        {
            return false;
        }
    }
    // The imported projection is deliberately a full direct-register form:
    // every non-fixed bit is one of the operand fields.
    return used == UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL bool a64_gpr_operand_valid(BusterAarch64ArmM1GprGeneratedOperand expected, A64GprOperand actual)
{
    if ((actual.width != 32 && actual.width != 64) || actual.index > 31 || actual.reserved || (actual.index != 31 && actual.stack_pointer))
    {
        return false;
    }
    if (actual.width != expected.width)
    {
        return false;
    }
    if (actual.index == 31 && (actual.stack_pointer ? A64_GPR_REGISTER31_SP : A64_GPR_REGISTER31_ZR) != expected.register31_role)
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_gpr_target_valid(Target target)
{
    return target.cpu_arch == CPU_ARCH_AARCH64 && a64_metadata_target_is_m1_profile(target) && target_cpu_features_are_valid(target);
}

u32 buster_aarch64_arm_m1_gpr_form_count(void)
{
    return BUSTER_AARCH64_ARM_M1_GPR_FORM_COUNT;
}

bool buster_aarch64_arm_m1_gpr_form(u32 form_index, BusterAarch64ArmM1GprForm* result)
{
    if (!result || form_index >= BUSTER_AARCH64_ARM_M1_GPR_FORM_COUNT)
    {
        return false;
    }
    BusterAarch64ArmM1GprGeneratedForm const* form = buster_aarch64_arm_m1_gpr_generated_forms + form_index;
    if (!a64_gpr_generated_form_valid(form))
    {
        return false;
    }
    *result = (BusterAarch64ArmM1GprForm){
        .mnemonic = string_from_pointer((char8*)form->mnemonic),
        .arm_row_id = string_from_pointer((char8*)form->arm_row_id),
        .arm_row_digest = form->arm_row_digest,
        .fixed_mask = form->fixed_mask,
        .fixed_value = form->fixed_value,
        .required_feature = form->required_feature,
        .operand_count = form->operand_count,
    };
    for (u32 index = 0; index < form->operand_count; index += 1)
    {
        result->operands[index] = (BusterAarch64ArmM1GprOperand){
            .width = form->operands[index].width,
            .bit_lsb = form->operands[index].bit_lsb,
            .register31_role = form->operands[index].register31_role,
        };
    }
    return true;
}

bool buster_aarch64_arm_m1_gpr_target(Target target)
{
    return a64_gpr_target_valid(target);
}

bool buster_aarch64_arm_m1_gpr_find_form(String8 mnemonic, A64GprOperand const* operands, u32 operand_count, u32* form_index)
{
    if (!form_index || !mnemonic.pointer || !mnemonic.length || operand_count > 4 || (operand_count && !operands))
    {
        return false;
    }
    u32 found = UINT32_MAX;
    for (u32 index = 0; index < BUSTER_AARCH64_ARM_M1_GPR_FORM_COUNT; index += 1)
    {
        BusterAarch64ArmM1GprGeneratedForm const* form = buster_aarch64_arm_m1_gpr_generated_forms + index;
        if (!a64_gpr_generated_form_valid(form) || !a64_gpr_string_equal(mnemonic, form->mnemonic) || form->operand_count != operand_count)
        {
            continue;
        }
        bool match = true;
        for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
        {
            if (!a64_gpr_operand_valid(form->operands[operand_index], operands[operand_index]))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            if (found != UINT32_MAX)
            {
                // Distinct rows must differ in width/role/arity.  Treat any
                // accidental ambiguity as a malformed generated projection.
                return false;
            }
            found = index;
        }
    }
    if (found == UINT32_MAX)
    {
        return false;
    }
    *form_index = found;
    return true;
}

bool buster_aarch64_arm_m1_gpr_encode(Target target, u32 form_index, A64GprOperand const* operands, u32 operand_count, u32* word)
{
    if (!word || !a64_gpr_target_valid(target) || form_index >= BUSTER_AARCH64_ARM_M1_GPR_FORM_COUNT)
    {
        return false;
    }
    BusterAarch64ArmM1GprGeneratedForm const* form = buster_aarch64_arm_m1_gpr_generated_forms + form_index;
    if (!a64_gpr_generated_form_valid(form) ||
        (form->required_feature != TARGET_CPU_FEATURE_NONE && !target_cpu_feature_has(target, form->required_feature)) ||
        operand_count != form->operand_count || (operand_count && !operands))
    {
        return false;
    }
    u32 result = form->fixed_value;
    for (u32 index = 0; index < operand_count; index += 1)
    {
        if (!a64_gpr_operand_valid(form->operands[index], operands[index]))
        {
            return false;
        }
        result |= (u32)operands[index].index << form->operands[index].bit_lsb;
    }
    *word = result;
    return true;
}

bool buster_aarch64_arm_m1_gpr_encode_mnemonic(Target target, String8 mnemonic, A64GprOperand const* operands, u32 operand_count,
                                               u32* word)
{
    u32 form_index = UINT32_MAX;
    return buster_aarch64_arm_m1_gpr_find_form(mnemonic, operands, operand_count, &form_index) &&
           buster_aarch64_arm_m1_gpr_encode(target, form_index, operands, operand_count, word);
}

bool a64_arm_m1_gpr_find_form(String8 mnemonic, A64GprOperand const* operands, u32 operand_count, u32* form_index)
{
    return buster_aarch64_arm_m1_gpr_find_form(mnemonic, operands, operand_count, form_index);
}

bool a64_arm_m1_gpr_encode(Target target, u32 form_index, A64GprOperand const* operands, u32 operand_count, u32* word)
{
    return buster_aarch64_arm_m1_gpr_encode(target, form_index, operands, operand_count, word);
}

bool a64_arm_m1_gpr_encode_mnemonic(Target target, String8 mnemonic, A64GprOperand const* operands, u32 operand_count, u32* word)
{
    return buster_aarch64_arm_m1_gpr_encode_mnemonic(target, mnemonic, operands, operand_count, word);
}

BUSTER_GLOBAL_LOCAL bool a64_scalar_string_equal(String8 string, char const* literal)
{
    if (!string.pointer || !string.length || !literal) return false;
    u64 index = 0;
    for (;; index += 1)
    {
        if (!literal[index]) return index == string.length;
        if (index >= string.length || a64_gpr_ascii_lower((char)string.pointer[index]) != a64_gpr_ascii_lower(literal[index])) return false;
    }
}

BUSTER_GLOBAL_LOCAL bool a64_scalar_generated_form_valid(BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form)
{
    if (!form || !form->mnemonic || !form->arm_row_id || !form->arm_row_digest || form->recipe >= BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COUNT ||
        (form->fixed_value & ~form->fixed_mask) || (form->width != 32 && form->width != 64) || !form->operand_count ||
        form->operand_count > 4 || form->reserved ||
        (form->required_feature != TARGET_CPU_FEATURE_NONE && form->required_feature != TARGET_CPU_FEATURE_AARCH64_FLAGM))
    {
        return false;
    }
    for (u32 index = 0; index < form->operand_count; index += 1)
    {
        BusterAarch64ArmM1ScalarIntegerGeneratedOperand operand = form->operands[index];
        if (operand.reserved || operand.kind > BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE ||
            operand.register31_role > BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY ||
            (operand.kind == BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER && operand.width != 0 && operand.width != 32 && operand.width != 64) ||
            (operand.kind == BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE && operand.width))
        {
            return false;
        }
    }
    for (u32 index = form->operand_count; index < 4; index += 1)
    {
        if (form->operands[index].kind || form->operands[index].width || form->operands[index].register31_role || form->operands[index].reserved)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_scalar_operand_valid(BusterAarch64ArmM1ScalarIntegerGeneratedOperand expected, A64ScalarIntOperand actual)
{
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(actual.reserved); index += 1)
    {
        if (actual.reserved[index]) return false;
    }
    if (actual.kind != expected.kind) return false;
    if (expected.kind == BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE)
    {
        return !actual.width && !actual.index && !actual.stack_pointer;
    }
    if (actual.value || (actual.width != 32 && actual.width != 64) || actual.index > 31 || (actual.index != 31 && actual.stack_pointer)) return false;
    if (expected.width && actual.width != expected.width) return false;
    if (actual.index == 31 && expected.register31_role != BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY &&
        (actual.stack_pointer ? BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP : BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR) !=
            expected.register31_role)
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_scalar_modifiers_valid(A64ScalarIntModifier const* modifiers, u32 modifier_count)
{
    if (modifier_count > 1 || (modifier_count && !modifiers)) return false;
    if (!modifier_count) return true;
    A64ScalarIntModifier modifier = modifiers[0];
    if (!modifier.present || modifier.kind > A64_SCALAR_INT_MODIFIER_EXTEND ||
        (modifier.kind == A64_SCALAR_INT_MODIFIER_SHIFT && modifier.value > A64_SCALAR_INT_SHIFT_ROR) ||
        (modifier.kind == A64_SCALAR_INT_MODIFIER_EXTEND && modifier.value > A64_SCALAR_INT_EXTEND_SXTX))
    {
        return false;
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modifier.reserved); index += 1)
    {
        if (modifier.reserved[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u64 a64_scalar_width_mask(u8 width)
{
    return width == 32 ? UINT64_C(0xffffffff) : width == 64 ? UINT64_MAX : 0;
}

BUSTER_GLOBAL_LOCAL u64 a64_scalar_rotate_right(u64 value, u32 rotation, u32 width)
{
    u64 mask = width == 64 ? UINT64_MAX : ((UINT64_C(1) << width) - 1);
    value &= mask;
    rotation %= width;
    return rotation ? ((value >> rotation) | (value << (width - rotation))) & mask : value;
}

BUSTER_GLOBAL_LOCAL bool a64_scalar_logical_immediate_encode(u64 value, u8 width, u32* encoded)
{
    if (!encoded || (width != 32 && width != 64)) return false;
    u64 mask = a64_scalar_width_mask(width);
    if ((value & ~mask) || !value || value == mask) return false;
    for (u32 element_width = 2; element_width <= width; element_width <<= 1)
    {
        u64 element_mask = element_width == 64 ? UINT64_MAX : ((UINT64_C(1) << element_width) - 1);
        u64 element = value & element_mask;
        bool replicated = true;
        for (u32 offset = element_width; offset < width; offset += element_width)
        {
            if (((value >> offset) & element_mask) != element)
            {
                replicated = false;
                break;
            }
        }
        if (!replicated || !element || element == element_mask) continue;
        for (u32 ones = 1; ones < element_width; ones += 1)
        {
            u64 ones_mask = (UINT64_C(1) << ones) - 1;
            for (u32 rotation = 0; rotation < element_width; rotation += 1)
            {
                if (a64_scalar_rotate_right(ones_mask, rotation, element_width) != element) continue;
                u32 levels = (~(element_width * 2u - 1u)) & 0x3fu;
                *encoded = ((element_width == 64 ? 1u : 0u) << 22) | (rotation << 16) | (levels | (ones - 1u)) << 10;
                return true;
            }
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool a64_scalar_form_shape_matches(BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form,
                                                         A64ScalarIntOperand const* operands, u32 operand_count,
                                                         A64ScalarIntModifier const* modifiers, u32 modifier_count)
{
    if (!a64_scalar_generated_form_valid(form) || operand_count != form->operand_count || (operand_count && !operands) ||
        !a64_scalar_modifiers_valid(modifiers, modifier_count))
    {
        return false;
    }
    for (u32 index = 0; index < operand_count; index += 1)
    {
        if (!a64_scalar_operand_valid(form->operands[index], operands[index])) return false;
    }
    if (form->recipe == BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT ||
        form->recipe == BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT)
    {
        bool has_stack_pointer = operands[0].stack_pointer || operands[1].stack_pointer;
        bool explicit_extend = modifier_count && modifiers[0].kind == A64_SCALAR_INT_MODIFIER_EXTEND;
        bool explicit_shift = modifier_count && modifiers[0].kind == A64_SCALAR_INT_MODIFIER_SHIFT;
        bool shift_is_lsl = explicit_shift && modifiers[0].value == A64_SCALAR_INT_SHIFT_LSL;
        if (form->recipe == BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT)
        {
            if (!explicit_extend && !(has_stack_pointer && (!explicit_shift || shift_is_lsl))) return false;
        }
        else if (explicit_extend || (has_stack_pointer && (!explicit_shift || shift_is_lsl)))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_scalar_recipe_encode(BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form,
                                                   A64ScalarIntOperand const* operands, u32 operand_count,
                                                   A64ScalarIntModifier const* modifiers, u32 modifier_count, u32* word)
{
    if (!word || !a64_scalar_form_shape_matches(form, operands, operand_count, modifiers, modifier_count)) return false;
    u32 result = form->fixed_value;
    u8 shift_kind = A64_SCALAR_INT_SHIFT_LSL;
    u8 extend_kind = A64_SCALAR_INT_EXTEND_UXTW;
    u64 amount = 0;
    bool has_modifier = modifier_count != 0;
    if (has_modifier)
    {
        shift_kind = modifiers[0].value;
        extend_kind = modifiers[0].value;
        amount = modifiers[0].amount;
    }
    u8 width = form->width;
    if (modifier_count && form->recipe != BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT &&
        form->recipe != BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM &&
        form->recipe != BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT &&
        form->recipe != BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT &&
        form->recipe != BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE)
    {
        return false;
    }
    switch ((BusterAarch64ArmM1ScalarIntegerRecipe)form->recipe)
    {
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT:
    {
        u8 option = A64_SCALAR_INT_EXTEND_UXTW;
        bool explicit_extend = has_modifier && modifiers[0].kind == A64_SCALAR_INT_MODIFIER_EXTEND;
        if (explicit_extend) option = extend_kind;
        else if (width == 64) option = A64_SCALAR_INT_EXTEND_UXTX;
        if (has_modifier && modifiers[0].kind == A64_SCALAR_INT_MODIFIER_SHIFT)
        {
            if (shift_kind != A64_SCALAR_INT_SHIFT_LSL) return false;
            option = width == 64 ? A64_SCALAR_INT_EXTEND_UXTX : A64_SCALAR_INT_EXTEND_UXTW;
        }
        if (amount > 4 || option > A64_SCALAR_INT_EXTEND_SXTX) return false;
        if (width == 32 && (option == A64_SCALAR_INT_EXTEND_UXTX || option == A64_SCALAR_INT_EXTEND_SXTX)) return false;
        bool source_x = option == A64_SCALAR_INT_EXTEND_UXTX || option == A64_SCALAR_INT_EXTEND_SXTX;
        if (operands[2].width != (source_x ? 64u : 32u)) return false;
        result |= (u32)operands[0].index | ((u32)operands[1].index << 5) | (u32)(amount << 10) | ((u32)option << 13) |
                  ((u32)operands[2].index << 16);
    }
    break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM:
        if (operands[2].value > 4095 || (has_modifier && modifiers[0].kind != A64_SCALAR_INT_MODIFIER_SHIFT) ||
            (has_modifier && shift_kind != A64_SCALAR_INT_SHIFT_LSL) || (has_modifier && amount != 0 && amount != 12)) return false;
        result |= (u32)operands[0].index | ((u32)operands[1].index << 5) | (u32)operands[2].value << 10;
        if (has_modifier && amount == 12) result |= UINT32_C(1) << 22;
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT:
        if (has_modifier && modifiers[0].kind != A64_SCALAR_INT_MODIFIER_SHIFT) return false;
        if (shift_kind == A64_SCALAR_INT_SHIFT_ROR || amount > (width == 32 ? 31u : 63u)) return false;
        result |= (u32)operands[0].index | ((u32)operands[1].index << 5) | (u32)amount << 10 | (u32)operands[2].index << 16;
        result |= (u32)shift_kind << 22;
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT:
        if (has_modifier && modifiers[0].kind != A64_SCALAR_INT_MODIFIER_SHIFT) return false;
        if (amount > (width == 32 ? 31u : 63u)) return false;
        result |= (u32)operands[0].index | ((u32)operands[1].index << 5) | (u32)amount << 10 | (u32)operands[2].index << 16;
        result |= (u32)shift_kind << 22;
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM:
    {
        u32 logical = 0;
        if (!a64_scalar_logical_immediate_encode(operands[2].value, width, &logical)) return false;
        result |= logical;
        result |= (u32)operands[0].index | ((u32)operands[1].index << 5);
    }
    break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD:
        if (operands[2].value >= width || operands[3].value >= width) return false;
        result |= (u32)operands[0].index | ((u32)operands[1].index << 5) | ((u32)operands[2].value << 16) |
                  ((u32)operands[3].value << 10);
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_EXTRACT:
        if (operands[3].value >= width) return false;
        result |= (u32)operands[0].index | ((u32)operands[1].index << 5) | ((u32)operands[2].index << 16) |
                  ((u32)operands[3].value << 10);
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE:
        if (operands[1].value > 0xffff || (has_modifier && modifiers[0].kind != A64_SCALAR_INT_MODIFIER_SHIFT) ||
            (has_modifier && shift_kind != A64_SCALAR_INT_SHIFT_LSL) || amount % 16 || amount > (width == 32 ? 16u : 48u)) return false;
        result |= (u32)operands[0].index | ((u32)operands[1].value << 5) | ((u32)(amount / 16) << 21);
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM:
        if (operands[1].value > 31 || operands[2].value > 15 || operands[3].value > 15) return false;
        result |= ((u32)operands[0].index << 5) | (u32)operands[1].value << 16 | (u32)operands[2].value |
                  (u32)operands[3].value << 12;
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG:
        if (operands[2].value > 15 || operands[3].value > 15) return false;
        result |= ((u32)operands[0].index << 5) | (u32)operands[1].index << 16 | (u32)operands[2].value |
                  (u32)operands[3].value << 12;
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_RMIF:
        if (operands[1].value > 63 || operands[2].value > 15) return false;
        result |= (u32)operands[2].value | ((u32)operands[0].index << 5) | ((u32)operands[1].value << 15);
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF:
        if (operands[0].value > 0xffff) return false;
        result |= (u32)operands[0].value;
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COUNT:
        return false;
    }
    if ((result & form->fixed_mask) != form->fixed_value)
    {
        return false;
    }
    *word = result;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_scalar_target_valid(Target target)
{
    return target.cpu_arch == CPU_ARCH_AARCH64 && a64_metadata_target_is_m1_profile(target) && target_cpu_features_are_valid(target);
}

u32 buster_aarch64_arm_m1_scalar_integer_form_count(void)
{
    return BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT;
}

bool buster_aarch64_arm_m1_scalar_integer_form(u32 form_index, BusterAarch64ArmM1ScalarIntegerForm* result)
{
    if (!result || form_index >= BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT) return false;
    BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form = buster_aarch64_arm_m1_scalar_integer_generated_forms + form_index;
    if (!a64_scalar_generated_form_valid(form)) return false;
    *result = (BusterAarch64ArmM1ScalarIntegerForm){
        .mnemonic = string_from_pointer((char8*)form->mnemonic),
        .arm_row_id = string_from_pointer((char8*)form->arm_row_id),
        .arm_row_digest = form->arm_row_digest,
        .fixed_mask = form->fixed_mask,
        .fixed_value = form->fixed_value,
        .required_feature = form->required_feature,
        .recipe = form->recipe,
        .width = form->width,
        .operand_count = form->operand_count,
    };
    for (u32 index = 0; index < form->operand_count; index += 1)
    {
        result->operands[index] = (BusterAarch64ArmM1ScalarIntegerOperand){
            .kind = form->operands[index].kind,
            .width = form->operands[index].width,
            .register31_role = form->operands[index].register31_role,
        };
    }
    return true;
}

bool buster_aarch64_arm_m1_scalar_integer_target(Target target)
{
    return a64_scalar_target_valid(target);
}

bool buster_aarch64_arm_m1_scalar_integer_find_form(String8 mnemonic, A64ScalarIntOperand const* operands, u32 operand_count,
                                                     A64ScalarIntModifier const* modifiers, u32 modifier_count, u32* form_index)
{
    if (!form_index || !mnemonic.pointer || !mnemonic.length || operand_count > 4 || (operand_count && !operands) || modifier_count > 1 ||
        (modifier_count && !modifiers))
    {
        return false;
    }
    u32 found = UINT32_MAX;
    for (u32 index = 0; index < BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT; index += 1)
    {
        BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form = buster_aarch64_arm_m1_scalar_integer_generated_forms + index;
        if (!a64_scalar_generated_form_valid(form) || !a64_scalar_string_equal(mnemonic, form->mnemonic) ||
            !a64_scalar_form_shape_matches(form, operands, operand_count, modifiers, modifier_count))
        {
            continue;
        }
        u32 ignored = 0;
        if (!a64_scalar_recipe_encode(form, operands, operand_count, modifiers, modifier_count, &ignored)) continue;
        if (found != UINT32_MAX) return false;
        found = index;
    }
    if (found == UINT32_MAX) return false;
    *form_index = found;
    return true;
}

bool buster_aarch64_arm_m1_scalar_integer_encode(Target target, u32 form_index, A64ScalarIntOperand const* operands, u32 operand_count,
                                                 A64ScalarIntModifier const* modifiers, u32 modifier_count, u32* word)
{
    if (!word || !a64_scalar_target_valid(target) || form_index >= BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT) return false;
    BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form = buster_aarch64_arm_m1_scalar_integer_generated_forms + form_index;
    if (!a64_scalar_generated_form_valid(form) ||
        (form->required_feature != TARGET_CPU_FEATURE_NONE && !target_cpu_feature_has(target, form->required_feature)))
    {
        return false;
    }
    return a64_scalar_recipe_encode(form, operands, operand_count, modifiers, modifier_count, word);
}

bool buster_aarch64_arm_m1_scalar_integer_encode_mnemonic(Target target, String8 mnemonic, A64ScalarIntOperand const* operands,
                                                          u32 operand_count, A64ScalarIntModifier const* modifiers, u32 modifier_count,
                                                          u32* word)
{
    u32 form_index = UINT32_MAX;
    return buster_aarch64_arm_m1_scalar_integer_find_form(mnemonic, operands, operand_count, modifiers, modifier_count, &form_index) &&
           buster_aarch64_arm_m1_scalar_integer_encode(target, form_index, operands, operand_count, modifiers, modifier_count, word);
}

bool a64_arm_m1_scalar_integer_find_form(String8 mnemonic, A64ScalarIntOperand const* operands, u32 operand_count,
                                          A64ScalarIntModifier const* modifiers, u32 modifier_count, u32* form_index)
{
    return buster_aarch64_arm_m1_scalar_integer_find_form(mnemonic, operands, operand_count, modifiers, modifier_count, form_index);
}

bool a64_arm_m1_scalar_integer_encode(Target target, u32 form_index, A64ScalarIntOperand const* operands, u32 operand_count,
                                      A64ScalarIntModifier const* modifiers, u32 modifier_count, u32* word)
{
    return buster_aarch64_arm_m1_scalar_integer_encode(target, form_index, operands, operand_count, modifiers, modifier_count, word);
}

bool a64_arm_m1_scalar_integer_encode_mnemonic(Target target, String8 mnemonic, A64ScalarIntOperand const* operands, u32 operand_count,
                                               A64ScalarIntModifier const* modifiers, u32 modifier_count, u32* word)
{
    return buster_aarch64_arm_m1_scalar_integer_encode_mnemonic(target, mnemonic, operands, operand_count, modifiers, modifier_count, word);
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
        default:
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
    // Architectural address generation wraps modulo 2^64.  Interpret a
    // wrapped delta as the representable signed IMM21 displacement when it
    // is within ADR's range, mirroring the page-relative helper above.
    u64 delta = target_address - instruction_address;
    s64 displacement = 0;
    if (delta <= UINT64_C(0xfffff))
    {
        displacement = (s64)delta;
    }
    else
    {
        u64 distance = 0 - delta;
        if (distance > UINT64_C(0x100000))
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

// -----------------------------------------------------------------------------
// Canonical Arm A64 Apple-M1 decoder

// Constraint operations are deliberately tiny and data-driven.  The importer
// compiles Arm row/box expressions into these bounded postfix programs; the
// runtime never reparses source text or consults a host assembler.
enum
{
    A64_CANONICAL_CONSTRAINT_EQUAL = 0,
    A64_CANONICAL_CONSTRAINT_NOT_EQUAL = 1,
    A64_CANONICAL_CONSTRAINT_AND = 2,
    A64_CANONICAL_CONSTRAINT_OR = 3,
    A64_CANONICAL_CONSTRAINT_NOT = 4,
    A64_CANONICAL_CONSTRAINT_LOGICAL_SOURCE = UINT16_MAX,
    A64_CANONICAL_CONSTRAINT_STACK_CAPACITY = 64,
};

BUSTER_GLOBAL_LOCAL bool a64_canonical_range_valid(u32 total, u32 first, u32 count)
{
    return first <= total && count <= total - first;
}

BUSTER_GLOBAL_LOCAL bool a64_canonical_segment_valid(BusterAarch64CanonicalDecoderSegment segment)
{
    return segment.width && segment.width <= 32 && segment.instruction_lsb < 32 && segment.value_lsb < 32 &&
           (u32)segment.instruction_lsb + segment.width <= 32 && (u32)segment.value_lsb + segment.width <= 32;
}

BUSTER_GLOBAL_LOCAL bool a64_canonical_form_valid(u32 form_index, BusterAarch64CanonicalDecoderForm const** result)
{
    if (form_index >= BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderForm const* form = buster_aarch64_canonical_decoder_forms + form_index;
    if (form->fixed_value & ~form->fixed_mask || form->field_count > 32 ||
        !a64_canonical_range_valid(BUSTER_AARCH64_CANONICAL_DECODER_FIELD_COUNT, form->field_first, form->field_count) ||
        !a64_canonical_range_valid(BUSTER_AARCH64_CANONICAL_DECODER_SOURCE_COUNT, form->source_first, form->source_count) ||
        form->constraint_program >= BUSTER_AARCH64_CANONICAL_DECODER_CONSTRAINT_PROGRAM_COUNT ||
        form->feature_program >= BUSTER_AARCH64_CANONICAL_DECODER_FEATURE_PROGRAM_COUNT)
    {
        return false;
    }
    u32 instruction_used = form->fixed_mask;
    for (u32 field_index = 0; field_index < form->field_count; field_index += 1)
    {
        BusterAarch64CanonicalDecoderField field = buster_aarch64_canonical_decoder_fields[form->field_first + field_index];
        if (!field.source_mask || !field.width || field.width > 32 ||
            !a64_canonical_range_valid(BUSTER_AARCH64_CANONICAL_DECODER_FIELD_SEGMENT_COUNT, field.segment_first, field.segment_count))
        {
            return false;
        }
        u32 source_used = 0;
        for (u32 segment_index = 0; segment_index < field.segment_count; segment_index += 1)
        {
            BusterAarch64CanonicalDecoderSegment segment =
                buster_aarch64_canonical_decoder_field_segments[field.segment_first + segment_index];
            if (!a64_canonical_segment_valid(segment))
            {
                return false;
            }
            u32 mask = a64_metadata_width_mask(segment.width);
            u32 source_mask = mask << segment.value_lsb;
            u32 instruction_mask = mask << segment.instruction_lsb;
            if ((source_used & source_mask) || (instruction_used & instruction_mask))
            {
                return false;
            }
            source_used |= source_mask;
            instruction_used |= instruction_mask;
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
    for (u32 source_index = 0; source_index < form->source_count; source_index += 1)
    {
        BusterAarch64CanonicalDecoderSource source = buster_aarch64_canonical_decoder_sources[form->source_first + source_index];
        if (!source.width || source.width > 32 ||
            !a64_canonical_range_valid(BUSTER_AARCH64_CANONICAL_DECODER_SOURCE_SEGMENT_COUNT, source.segment_first, source.segment_count))
        {
            return false;
        }
        for (u32 segment_index = 0; segment_index < source.segment_count; segment_index += 1)
        {
            if (!a64_canonical_segment_valid(buster_aarch64_canonical_decoder_source_segments[source.segment_first + segment_index]))
            {
                return false;
            }
        }
    }
    BusterAarch64CanonicalDecoderProgram program = buster_aarch64_canonical_decoder_programs[form->constraint_program];
    if (!a64_canonical_range_valid(BUSTER_AARCH64_CANONICAL_DECODER_CONSTRAINT_TOKEN_COUNT, program.token_first, program.token_count))
    {
        return false;
    }
    for (u32 token_index = 0; token_index < program.token_count; token_index += 1)
    {
        BusterAarch64CanonicalDecoderConstraint token = buster_aarch64_canonical_decoder_tokens[program.token_first + token_index];
        if (token.operation > A64_CANONICAL_CONSTRAINT_NOT ||
            (token.operation <= A64_CANONICAL_CONSTRAINT_NOT_EQUAL && token.source >= form->source_count))
        {
            return false;
        }
    }
    if (result)
    {
        *result = form;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_canonical_source_value(BusterAarch64CanonicalDecoderForm const* form, u32 source_index, u32 word, u32* value)
{
    if (!form || !value || source_index >= form->source_count)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderSource source = buster_aarch64_canonical_decoder_sources[form->source_first + source_index];
    u32 result = 0;
    for (u32 segment_index = 0; segment_index < source.segment_count; segment_index += 1)
    {
        BusterAarch64CanonicalDecoderSegment segment =
            buster_aarch64_canonical_decoder_source_segments[source.segment_first + segment_index];
        if (!a64_canonical_segment_valid(segment))
        {
            return false;
        }
        u32 mask = a64_metadata_width_mask(segment.width);
        result |= ((word >> segment.instruction_lsb) & mask) << segment.value_lsb;
    }
    *value = result;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_canonical_constraints_match(BusterAarch64CanonicalDecoderForm const* form, u32 word)
{
    if (!form)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderProgram program = buster_aarch64_canonical_decoder_programs[form->constraint_program];
    bool stack[A64_CANONICAL_CONSTRAINT_STACK_CAPACITY] = {0};
    u32 stack_count = 0;
    for (u32 token_index = 0; token_index < program.token_count; token_index += 1)
    {
        BusterAarch64CanonicalDecoderConstraint token = buster_aarch64_canonical_decoder_tokens[program.token_first + token_index];
        if (token.operation <= A64_CANONICAL_CONSTRAINT_NOT_EQUAL)
        {
            if (stack_count >= A64_CANONICAL_CONSTRAINT_STACK_CAPACITY)
            {
                return false;
            }
            u32 actual = 0;
            if (!a64_canonical_source_value(form, token.source, word, &actual))
            {
                return false;
            }
            stack[stack_count++] = token.operation == A64_CANONICAL_CONSTRAINT_EQUAL ?
                                       ((actual ^ token.value) & token.mask) == 0 :
                                       ((actual ^ token.value) & token.mask) != 0;
        }
        else if (token.operation == A64_CANONICAL_CONSTRAINT_AND || token.operation == A64_CANONICAL_CONSTRAINT_OR)
        {
            if (stack_count < 2)
            {
                return false;
            }
            bool right = stack[--stack_count];
            bool left = stack[--stack_count];
            stack[stack_count++] = token.operation == A64_CANONICAL_CONSTRAINT_AND ? (left && right) : (left || right);
        }
        else if (token.operation == A64_CANONICAL_CONSTRAINT_NOT)
        {
            if (!stack_count)
            {
                return false;
            }
            stack[stack_count - 1] = !stack[stack_count - 1];
        }
        else
        {
            return false;
        }
    }
    return stack_count == 0 || (stack_count == 1 && stack[0]);
}

BUSTER_GLOBAL_LOCAL bool a64_canonical_target_features_match(BusterAarch64CanonicalDecoderForm const* form, Target target)
{
    if (!form || target.cpu_arch != CPU_ARCH_AARCH64 || !target_cpu_features_are_valid(target))
    {
        return false;
    }
    TargetCpuFeatures effective = target_cpu_features_effective(target);
    BusterAarch64CanonicalDecoderFeatureProgram required = buster_aarch64_canonical_decoder_features[form->feature_program];
    for (u32 word_index = 0; word_index < TARGET_CPU_FEATURE_WORD_COUNT; word_index += 1)
    {
        if ((required.words[word_index] & ~effective.words[word_index]) != 0)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_canonical_more_specific(BusterAarch64CanonicalDecoderForm const* left,
                                                       BusterAarch64CanonicalDecoderForm const* right)
{
    return left && right && left->fixed_mask != right->fixed_mask && (left->fixed_mask & right->fixed_mask) == right->fixed_mask &&
           (left->fixed_value & right->fixed_mask) == (right->fixed_value & right->fixed_mask);
}

u32 buster_aarch64_canonical_form_count(void)
{
    return BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT;
}

bool buster_aarch64_canonical_form(u32 form_index, BusterAarch64CanonicalFormInfo* result)
{
    if (!result)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderForm const* form = 0;
    if (!a64_canonical_form_valid(form_index, &form))
    {
        return false;
    }
    BusterAarch64CanonicalFormInfo local = {
        .form_index = form_index,
        .fixed_mask = form->fixed_mask,
        .fixed_value = form->fixed_value,
        .representative_word = form->representative_word,
        .arm_row_digest = form->arm_row_digest,
        .field_count = form->field_count,
    };
    *result = local;
    return true;
}

bool buster_aarch64_canonical_field(u32 form_index, u32 field_index, BusterAarch64CanonicalFieldInfo* result)
{
    if (!result)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderForm const* form = 0;
    if (!a64_canonical_form_valid(form_index, &form) || field_index >= form->field_count)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderField field = buster_aarch64_canonical_decoder_fields[form->field_first + field_index];
    *result = (BusterAarch64CanonicalFieldInfo){.source_mask = field.source_mask, .width = field.width, .segment_count = field.segment_count};
    return true;
}

bool buster_aarch64_canonical_field_segment(u32 form_index, u32 field_index, u32 segment_index, BusterAarch64MetadataSegment* result)
{
    if (!result)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderForm const* form = 0;
    if (!a64_canonical_form_valid(form_index, &form) || field_index >= form->field_count)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderField field = buster_aarch64_canonical_decoder_fields[form->field_first + field_index];
    if (segment_index >= field.segment_count)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderSegment segment =
        buster_aarch64_canonical_decoder_field_segments[field.segment_first + segment_index];
    if (!a64_canonical_segment_valid(segment))
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

bool buster_aarch64_canonical_raw_encode(u32 form_index, u32 const* field_values, u32 field_count, u32* word)
{
    if (!word)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderForm const* form = 0;
    if (!a64_canonical_form_valid(form_index, &form) || field_count != form->field_count || (field_count && !field_values))
    {
        return false;
    }
    u32 result = form->fixed_value;
    for (u32 field_index = 0; field_index < form->field_count; field_index += 1)
    {
        BusterAarch64CanonicalDecoderField field = buster_aarch64_canonical_decoder_fields[form->field_first + field_index];
        u32 source_value = field_values[field_index];
        if (source_value & ~field.source_mask)
        {
            return false;
        }
        for (u32 segment_index = 0; segment_index < field.segment_count; segment_index += 1)
        {
            BusterAarch64CanonicalDecoderSegment segment =
                buster_aarch64_canonical_decoder_field_segments[field.segment_first + segment_index];
            u32 mask = a64_metadata_width_mask(segment.width);
            result |= ((source_value >> segment.value_lsb) & mask) << segment.instruction_lsb;
        }
    }
    if ((result & form->fixed_mask) != form->fixed_value || !a64_canonical_constraints_match(form, result))
    {
        return false;
    }
    *word = result;
    return true;
}

bool buster_aarch64_canonical_raw_decode(u32 form_index, u32 word, u32* field_values, u32 field_count)
{
    BusterAarch64CanonicalDecoderForm const* form = 0;
    if (!a64_canonical_form_valid(form_index, &form) || field_count != form->field_count || (field_count && !field_values) ||
        (word & form->fixed_mask) != form->fixed_value || !a64_canonical_constraints_match(form, word))
    {
        return false;
    }
    u32 values[32] = {0};
    for (u32 field_index = 0; field_index < form->field_count; field_index += 1)
    {
        BusterAarch64CanonicalDecoderField field = buster_aarch64_canonical_decoder_fields[form->field_first + field_index];
        u32 source_value = 0;
        for (u32 segment_index = 0; segment_index < field.segment_count; segment_index += 1)
        {
            BusterAarch64CanonicalDecoderSegment segment =
                buster_aarch64_canonical_decoder_field_segments[field.segment_first + segment_index];
            u32 mask = a64_metadata_width_mask(segment.width);
            source_value |= ((word >> segment.instruction_lsb) & mask) << segment.value_lsb;
        }
        values[field_index] = source_value;
    }
    if (field_count)
    {
        memcpy(field_values, values, sizeof(u32) * field_count);
    }
    return true;
}

BusterAarch64CanonicalDecodeStatus buster_aarch64_canonical_decode(Target target, u32 word,
                                                                    BusterAarch64CanonicalDecodeResult* result)
{
    if (!result || target.cpu_arch != CPU_ARCH_AARCH64 || !target_cpu_features_are_valid(target))
    {
        return BUSTER_AARCH64_CANONICAL_DECODE_INCOMPLETE;
    }
    u16 supported[ BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT ];
    u32 supported_count = 0;
    bool raw_match = false;
    for (u32 form_index = 0; form_index < BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT; form_index += 1)
    {
        BusterAarch64CanonicalDecoderForm const* form = 0;
        if (!a64_canonical_form_valid(form_index, &form))
        {
            return BUSTER_AARCH64_CANONICAL_DECODE_INCOMPLETE;
        }
        if ((word & form->fixed_mask) != form->fixed_value || !a64_canonical_constraints_match(form, word))
        {
            continue;
        }
        raw_match = true;
        // The feature filter is intentionally before specificity ranking.
        if (a64_canonical_target_features_match(form, target))
        {
            if (supported_count >= BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT)
            {
                return BUSTER_AARCH64_CANONICAL_DECODE_INCOMPLETE;
            }
            supported[supported_count++] = (u16)form_index;
        }
    }
    if (!raw_match)
    {
        return BUSTER_AARCH64_CANONICAL_DECODE_UNALLOCATED;
    }
    if (!supported_count)
    {
        return BUSTER_AARCH64_CANONICAL_DECODE_UNSUPPORTED_FEATURE;
    }
    u32 maxima_count = 0;
    u16 selected = UINT16_MAX;
    for (u32 candidate_index = 0; candidate_index < supported_count; candidate_index += 1)
    {
        BusterAarch64CanonicalDecoderForm const* candidate = buster_aarch64_canonical_decoder_forms + supported[candidate_index];
        bool dominated = false;
        for (u32 other_index = 0; other_index < supported_count; other_index += 1)
        {
            if (candidate_index == other_index)
            {
                continue;
            }
            BusterAarch64CanonicalDecoderForm const* other = buster_aarch64_canonical_decoder_forms + supported[other_index];
            if (a64_canonical_more_specific(other, candidate))
            {
                dominated = true;
                break;
            }
        }
        if (!dominated)
        {
            maxima_count += 1;
            selected = supported[candidate_index];
        }
    }
    if (maxima_count != 1)
    {
        return BUSTER_AARCH64_CANONICAL_DECODE_AMBIGUOUS;
    }
    BusterAarch64CanonicalDecoderForm const* form = buster_aarch64_canonical_decoder_forms + selected;
    BusterAarch64CanonicalDecodeResult local = {0};
    local.form_index = selected;
    local.arm_row_digest = form->arm_row_digest;
    local.field_count = form->field_count;
    if (!buster_aarch64_canonical_raw_decode(selected, word, local.field_values, form->field_count))
    {
        return BUSTER_AARCH64_CANONICAL_DECODE_INCOMPLETE;
    }
    *result = local;
    return BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS;
}

// The typed direct projections intentionally do not carry a second row-index
// table.  Canonical digest identity is the join key shared by the generated
// family recipes and the Arm decoder; requiring a unique digest match keeps a
// stale or duplicated projection fail closed.
BUSTER_GLOBAL_LOCAL bool a64_typed_canonical_index_for_digest(u64 digest, u32* form_index,
                                                              BusterAarch64CanonicalDecoderForm const** form_result)
{
    if (!digest || !form_index)
    {
        return false;
    }
    u32 found = UINT32_MAX;
    BusterAarch64CanonicalDecoderForm const* found_form = 0;
    for (u32 index = 0; index < BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT; index += 1)
    {
        BusterAarch64CanonicalDecoderForm const* form = 0;
        if (!a64_canonical_form_valid(index, &form))
        {
            return false;
        }
        if (form->arm_row_digest != digest)
        {
            continue;
        }
        if (found != UINT32_MAX)
        {
            return false;
        }
        found = index;
        found_form = form;
    }
    if (found == UINT32_MAX)
    {
        return false;
    }
    *form_index = found;
    if (form_result)
    {
        *form_result = found_form;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_typed_output_ranges_overlap(void const* left, uintptr_t left_size, void const* right,
                                                          uintptr_t right_size)
{
    if (!left || !right || !left_size || !right_size)
    {
        return false;
    }
    uintptr_t left_begin = (uintptr_t)left;
    uintptr_t right_begin = (uintptr_t)right;
    if (left_begin > UINTPTR_MAX - left_size || right_begin > UINTPTR_MAX - right_size)
    {
        return true;
    }
    uintptr_t left_end = left_begin + left_size;
    uintptr_t right_end = right_begin + right_size;
    return left_begin < right_end && right_begin < left_end;
}

BUSTER_GLOBAL_LOCAL bool a64_typed_gpr_output_arguments_valid(A64GprOperand* operands, u32 operand_capacity, u32* operand_count)
{
    if (!operand_count || operand_capacity > 4 || (operand_capacity && !operands) ||
        a64_typed_output_ranges_overlap(operands, (uintptr_t)operand_capacity * sizeof(*operands), operand_count, sizeof(*operand_count)))
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_typed_scalar_output_arguments_valid(
    A64ScalarIntOperand* operands, u32 operand_capacity, u32* operand_count, A64ScalarIntModifier* modifiers,
    u32 modifier_capacity, u32* modifier_count)
{
    uintptr_t operand_bytes = (uintptr_t)operand_capacity * sizeof(*operands);
    uintptr_t modifier_bytes = (uintptr_t)modifier_capacity * sizeof(*modifiers);
    if (!operand_count || !modifier_count || operand_capacity > 4 || modifier_capacity > 1 ||
        (operand_capacity && !operands) || (modifier_capacity && !modifiers) ||
        a64_typed_output_ranges_overlap(operands, operand_bytes, operand_count, sizeof(*operand_count)) ||
        a64_typed_output_ranges_overlap(modifiers, modifier_bytes, modifier_count, sizeof(*modifier_count)) ||
        a64_typed_output_ranges_overlap(operand_count, sizeof(*operand_count), modifier_count, sizeof(*modifier_count)) ||
        a64_typed_output_ranges_overlap(operands, operand_bytes, modifiers, modifier_bytes) ||
        a64_typed_output_ranges_overlap(operands, operand_bytes, modifier_count, sizeof(*modifier_count)) ||
        a64_typed_output_ranges_overlap(modifiers, modifier_bytes, operand_count, sizeof(*operand_count)))
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_typed_gpr_generated_index_for_digest(u64 digest, u32* form_index)
{
    if (!digest || !form_index)
    {
        return false;
    }
    u32 found = UINT32_MAX;
    for (u32 index = 0; index < BUSTER_AARCH64_ARM_M1_GPR_FORM_COUNT; index += 1)
    {
        BusterAarch64ArmM1GprGeneratedForm const* form = buster_aarch64_arm_m1_gpr_generated_forms + index;
        if (!a64_gpr_generated_form_valid(form))
        {
            return false;
        }
        if (form->arm_row_digest != digest)
        {
            continue;
        }
        if (found != UINT32_MAX)
        {
            return false;
        }
        found = index;
    }
    if (found == UINT32_MAX)
    {
        return false;
    }
    *form_index = found;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_typed_scalar_generated_index_for_digest(u64 digest, u32* form_index)
{
    if (!digest || !form_index)
    {
        return false;
    }
    u32 found = UINT32_MAX;
    for (u32 index = 0; index < BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT; index += 1)
    {
        BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form = buster_aarch64_arm_m1_scalar_integer_generated_forms + index;
        if (!a64_scalar_generated_form_valid(form))
        {
            return false;
        }
        if (form->arm_row_digest != digest)
        {
            continue;
        }
        if (found != UINT32_MAX)
        {
            return false;
        }
        found = index;
    }
    if (found == UINT32_MAX)
    {
        return false;
    }
    *form_index = found;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_typed_canonical_raw_decode_for_digest(u64 digest, u32 word,
                                                                   BusterAarch64CanonicalDecoderForm const** form_result)
{
    u32 canonical_index = UINT32_MAX;
    BusterAarch64CanonicalDecoderForm const* form = 0;
    if (!a64_typed_canonical_index_for_digest(digest, &canonical_index, &form) || !form || form->field_count > 32)
    {
        return false;
    }
    u32 fields[32] = {0};
    if (!buster_aarch64_canonical_raw_decode(canonical_index, word, fields, form->field_count))
    {
        return false;
    }
    if (form_result)
    {
        *form_result = form;
    }
    return true;
}

bool buster_aarch64_arm_m1_gpr_decode_form(Target target, u32 form_index, u32 word, A64GprOperand* operands,
                                           u32 operand_capacity, u32* operand_count)
{
    if (!a64_typed_gpr_output_arguments_valid(operands, operand_capacity, operand_count) ||
        !a64_gpr_target_valid(target) || form_index >= BUSTER_AARCH64_ARM_M1_GPR_FORM_COUNT)
    {
        return false;
    }
    BusterAarch64ArmM1GprGeneratedForm const* form = buster_aarch64_arm_m1_gpr_generated_forms + form_index;
    if (!a64_gpr_generated_form_valid(form) ||
        (form->required_feature != TARGET_CPU_FEATURE_NONE && !target_cpu_feature_has(target, form->required_feature)) ||
        operand_capacity < form->operand_count)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderForm const* canonical_form = 0;
    if (!a64_typed_canonical_raw_decode_for_digest(form->arm_row_digest, word, &canonical_form) || !canonical_form ||
        canonical_form->fixed_mask != form->fixed_mask || canonical_form->fixed_value != form->fixed_value)
    {
        return false;
    }
    A64GprOperand local[4] = {0};
    for (u32 index = 0; index < form->operand_count; index += 1)
    {
        BusterAarch64ArmM1GprGeneratedOperand expected = form->operands[index];
        local[index] = (A64GprOperand){
            .index = (u8)((word >> expected.bit_lsb) & 31u),
            .width = expected.width,
            .stack_pointer = false,
        };
        if (local[index].index == 31 && expected.register31_role == BUSTER_AARCH64_ARM_M1_GPR_31_SP)
        {
            local[index].stack_pointer = true;
        }
        if (!a64_gpr_operand_valid(expected, local[index]))
        {
            return false;
        }
    }
    u32 reencoded = 0;
    if (!buster_aarch64_arm_m1_gpr_encode(target, form_index, local, form->operand_count, &reencoded) || reencoded != word)
    {
        return false;
    }
    memcpy(operands, local, (size_t)form->operand_count * sizeof(*operands));
    *operand_count = form->operand_count;
    return true;
}

bool buster_aarch64_arm_m1_gpr_decode(Target target, u32 word, u32* form_index, A64GprOperand* operands,
                                      u32 operand_capacity, u32* operand_count)
{
    if (!form_index || !a64_typed_gpr_output_arguments_valid(operands, operand_capacity, operand_count) ||
        a64_typed_output_ranges_overlap(form_index, sizeof(*form_index), operands,
                                         (uintptr_t)operand_capacity * sizeof(*operands)) ||
        a64_typed_output_ranges_overlap(form_index, sizeof(*form_index), operand_count, sizeof(*operand_count)) ||
        !a64_gpr_target_valid(target))
    {
        return false;
    }
    BusterAarch64CanonicalDecodeResult canonical = {0};
    if (buster_aarch64_canonical_decode(target, word, &canonical) != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS)
    {
        return false;
    }
    u32 selected = UINT32_MAX;
    if (!a64_typed_gpr_generated_index_for_digest(canonical.arm_row_digest, &selected))
    {
        return false;
    }
    A64GprOperand local[4] = {0};
    u32 local_count = 0;
    if (!buster_aarch64_arm_m1_gpr_decode_form(target, selected, word, local, BUSTER_ARRAY_LENGTH(local), &local_count) ||
        local_count > operand_capacity)
    {
        return false;
    }
    memcpy(operands, local, (size_t)local_count * sizeof(*operands));
    *form_index = selected;
    *operand_count = local_count;
    return true;
}

bool a64_arm_m1_gpr_decode_form(Target target, u32 form_index, u32 word, A64GprOperand* operands, u32 operand_capacity,
                                u32* operand_count)
{
    return buster_aarch64_arm_m1_gpr_decode_form(target, form_index, word, operands, operand_capacity, operand_count);
}

bool a64_arm_m1_gpr_decode(Target target, u32 word, u32* form_index, A64GprOperand* operands, u32 operand_capacity,
                           u32* operand_count)
{
    return buster_aarch64_arm_m1_gpr_decode(target, word, form_index, operands, operand_capacity, operand_count);
}

BUSTER_GLOBAL_LOCAL bool a64_typed_scalar_decode_register(BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form, u32 index,
                                                          u8 register_index, u8 width, A64ScalarIntOperand decoded[4])
{
    if (!form || index >= form->operand_count || form->operands[index].kind != BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER)
    {
        return false;
    }
    BusterAarch64ArmM1ScalarIntegerGeneratedOperand expected = form->operands[index];
    A64ScalarIntOperand value = {
        .kind = A64_SCALAR_INT_OPERAND_REGISTER,
        .width = expected.width ? expected.width : width,
        .index = register_index,
        .stack_pointer = register_index == 31 && expected.register31_role == BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP,
    };
    if (value.width != width || !a64_scalar_operand_valid(expected, value))
    {
        return false;
    }
    decoded[index] = value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_typed_scalar_decode_immediate(BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form, u32 index,
                                                           u64 immediate, A64ScalarIntOperand decoded[4])
{
    if (!form || index >= form->operand_count || form->operands[index].kind != BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE)
    {
        return false;
    }
    decoded[index] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = immediate};
    return a64_scalar_operand_valid(form->operands[index], decoded[index]);
}

BUSTER_GLOBAL_LOCAL bool a64_typed_scalar_logical_immediate_decode(u8 width, u32 n, u32 immr, u32 imms, u64* value)
{
    if (!value || (width != 32 && width != 64) || n > 1 || immr > 63 || imms > 63)
    {
        return false;
    }
    u32 packed = (n << 6) | ((~imms) & 63u);
    s32 len = -1;
    for (s32 bit = 6; bit >= 0; bit -= 1)
    {
        if (packed & (1u << bit))
        {
            len = bit;
            break;
        }
    }
    if (len < 1 || (width == 32 && (n || len >= 6)) || (width == 64 && len > 6))
    {
        return false;
    }
    u32 levels = (1u << len) - 1u;
    u32 s = imms & levels;
    u32 r = immr & levels;
    if (s == levels)
    {
        return false;
    }
    u32 element_width = 1u << len;
    u64 element_mask = element_width == 64 ? UINT64_MAX : ((UINT64_C(1) << element_width) - 1);
    u64 element = (UINT64_C(1) << (s + 1u)) - 1;
    u32 rotation = element_width ? (r % element_width) : 0;
    if (rotation)
    {
        element = ((element >> rotation) | (element << (element_width - rotation))) & element_mask;
    }
    u64 result = 0;
    for (u32 offset = 0; offset < width; offset += element_width)
    {
        result |= element << offset;
    }
    *value = result & (width == 32 ? UINT64_C(0xffffffff) : UINT64_MAX);
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_typed_scalar_recipe_decode(BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form, u32 word,
                                                        A64ScalarIntOperand operands[4], u32* operand_count,
                                                        A64ScalarIntModifier modifiers[1], u32* modifier_count)
{
    if (!form || !operands || !operand_count || !modifiers || !modifier_count || form->recipe == BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF)
    {
        return false;
    }
    A64ScalarIntOperand decoded[4] = {0};
    A64ScalarIntModifier decoded_modifier = {0};
    u32 decoded_modifier_count = 0;
    u8 width = form->width;
    switch ((BusterAarch64ArmM1ScalarIntegerRecipe)form->recipe)
    {
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT:
    {
        u8 option = (u8)((word >> 13) & 7u);
        u8 amount = (u8)((word >> 10) & 7u);
        bool source_x = option == A64_SCALAR_INT_EXTEND_UXTX || option == A64_SCALAR_INT_EXTEND_SXTX;
        if (amount > 4 || (width == 32 && source_x) ||
            !a64_typed_scalar_decode_register(form, 0, (u8)(word & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 1, (u8)((word >> 5) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 2, (u8)((word >> 16) & 31u), source_x ? 64 : 32, decoded))
        {
            return false;
        }
        bool default_option = option == (width == 64 ? A64_SCALAR_INT_EXTEND_UXTX : A64_SCALAR_INT_EXTEND_UXTW);
        bool has_stack_pointer = decoded[0].stack_pointer || decoded[1].stack_pointer;
        if (!(default_option && amount == 0 && has_stack_pointer))
        {
            decoded_modifier = (A64ScalarIntModifier){
                .kind = A64_SCALAR_INT_MODIFIER_EXTEND,
                .value = option,
                .amount = amount,
                .present = true,
            };
            decoded_modifier_count = 1;
        }
    }
    break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM:
        if (!a64_typed_scalar_decode_register(form, 0, (u8)(word & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 1, (u8)((word >> 5) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 2, (word >> 10) & 4095u, decoded))
        {
            return false;
        }
        if (word & (UINT32_C(1) << 22))
        {
            decoded_modifier = (A64ScalarIntModifier){
                .kind = A64_SCALAR_INT_MODIFIER_SHIFT,
                .value = A64_SCALAR_INT_SHIFT_LSL,
                .amount = 12,
                .present = true,
            };
            decoded_modifier_count = 1;
        }
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT:
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT:
    {
        u8 shift = (u8)((word >> 22) & 3u);
        u8 amount = (u8)((word >> 10) & 63u);
        if ((form->recipe == BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT && shift == A64_SCALAR_INT_SHIFT_ROR) ||
            !a64_typed_scalar_decode_register(form, 0, (u8)(word & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 1, (u8)((word >> 5) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 2, (u8)((word >> 16) & 31u), width, decoded))
        {
            return false;
        }
        if (shift != A64_SCALAR_INT_SHIFT_LSL || amount)
        {
            decoded_modifier = (A64ScalarIntModifier){
                .kind = A64_SCALAR_INT_MODIFIER_SHIFT,
                .value = shift,
                .amount = amount,
                .present = true,
            };
            decoded_modifier_count = 1;
        }
    }
    break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM:
    {
        u64 immediate = 0;
        if (!a64_typed_scalar_logical_immediate_decode(width, (word >> 22) & 1u, (word >> 16) & 63u, (word >> 10) & 63u, &immediate) ||
            !a64_typed_scalar_decode_register(form, 0, (u8)(word & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 1, (u8)((word >> 5) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 2, immediate, decoded))
        {
            return false;
        }
    }
    break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD:
        if (!a64_typed_scalar_decode_register(form, 0, (u8)(word & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 1, (u8)((word >> 5) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 2, (word >> 16) & 63u, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 3, (word >> 10) & 63u, decoded))
        {
            return false;
        }
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_EXTRACT:
        if (!a64_typed_scalar_decode_register(form, 0, (u8)(word & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 1, (u8)((word >> 5) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 2, (u8)((word >> 16) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 3, (word >> 10) & 63u, decoded))
        {
            return false;
        }
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE:
    {
        u8 halfword = (u8)((word >> 21) & 3u);
        if ((width == 32 && halfword > 1) || !a64_typed_scalar_decode_register(form, 0, (u8)(word & 31u), width, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 1, (word >> 5) & 0xffffu, decoded))
        {
            return false;
        }
        if (halfword)
        {
            decoded_modifier = (A64ScalarIntModifier){
                .kind = A64_SCALAR_INT_MODIFIER_SHIFT,
                .value = A64_SCALAR_INT_SHIFT_LSL,
                .amount = (u64)halfword * 16,
                .present = true,
            };
            decoded_modifier_count = 1;
        }
    }
    break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM:
        if (!a64_typed_scalar_decode_register(form, 0, (u8)((word >> 5) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 1, (word >> 16) & 31u, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 2, word & 15u, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 3, (word >> 12) & 15u, decoded))
        {
            return false;
        }
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG:
        if (!a64_typed_scalar_decode_register(form, 0, (u8)((word >> 5) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_register(form, 1, (u8)((word >> 16) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 2, word & 15u, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 3, (word >> 12) & 15u, decoded))
        {
            return false;
        }
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_RMIF:
        if (!a64_typed_scalar_decode_register(form, 0, (u8)((word >> 5) & 31u), width, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 1, (word >> 15) & 63u, decoded) ||
            !a64_typed_scalar_decode_immediate(form, 2, word & 15u, decoded))
        {
            return false;
        }
        break;
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF:
    case BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COUNT:
        return false;
    }
    for (u32 index = 0; index < form->operand_count; index += 1)
    {
        if (!a64_scalar_operand_valid(form->operands[index], decoded[index]))
        {
            return false;
        }
    }
    if (!a64_scalar_modifiers_valid(decoded_modifier_count ? &decoded_modifier : 0, decoded_modifier_count))
    {
        return false;
    }
    u32 reencoded = 0;
    if (!a64_scalar_recipe_encode(form, decoded, form->operand_count, decoded_modifier_count ? &decoded_modifier : 0,
                                  decoded_modifier_count, &reencoded) ||
        reencoded != word)
    {
        return false;
    }
    memcpy(operands, decoded, sizeof(decoded));
    *operand_count = form->operand_count;
    if (decoded_modifier_count)
    {
        *modifiers = decoded_modifier;
    }
    *modifier_count = decoded_modifier_count;
    return true;
}

bool buster_aarch64_arm_m1_scalar_integer_decode_form(
    Target target, u32 form_index, u32 word, A64ScalarIntOperand* operands, u32 operand_capacity, u32* operand_count,
    A64ScalarIntModifier* modifiers, u32 modifier_capacity, u32* modifier_count)
{
    if (!a64_typed_scalar_output_arguments_valid(operands, operand_capacity, operand_count, modifiers, modifier_capacity, modifier_count) ||
        !a64_scalar_target_valid(target) || form_index >= BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT)
    {
        return false;
    }
    BusterAarch64ArmM1ScalarIntegerGeneratedForm const* form = buster_aarch64_arm_m1_scalar_integer_generated_forms + form_index;
    if (!a64_scalar_generated_form_valid(form) ||
        (form->required_feature != TARGET_CPU_FEATURE_NONE && !target_cpu_feature_has(target, form->required_feature)) ||
        operand_capacity < form->operand_count)
    {
        return false;
    }
    BusterAarch64CanonicalDecoderForm const* canonical_form = 0;
    if (!a64_typed_canonical_raw_decode_for_digest(form->arm_row_digest, word, &canonical_form) || !canonical_form ||
        canonical_form->fixed_mask != form->fixed_mask || canonical_form->fixed_value != form->fixed_value)
    {
        return false;
    }
    A64ScalarIntOperand local_operands[4] = {0};
    A64ScalarIntModifier local_modifier = {0};
    u32 local_operand_count = 0;
    u32 local_modifier_count = 0;
    if (!a64_typed_scalar_recipe_decode(form, word, local_operands, &local_operand_count, &local_modifier, &local_modifier_count) ||
        local_modifier_count > modifier_capacity)
    {
        return false;
    }
    memcpy(operands, local_operands, (size_t)local_operand_count * sizeof(*operands));
    if (local_modifier_count)
    {
        memcpy(modifiers, &local_modifier, sizeof(local_modifier));
    }
    *operand_count = local_operand_count;
    *modifier_count = local_modifier_count;
    return true;
}

bool buster_aarch64_arm_m1_scalar_integer_decode(
    Target target, u32 word, u32* form_index, A64ScalarIntOperand* operands, u32 operand_capacity, u32* operand_count,
    A64ScalarIntModifier* modifiers, u32 modifier_capacity, u32* modifier_count)
{
    if (!form_index || !a64_typed_scalar_output_arguments_valid(operands, operand_capacity, operand_count, modifiers, modifier_capacity,
                                                                 modifier_count) ||
        a64_typed_output_ranges_overlap(form_index, sizeof(*form_index), operands,
                                         (uintptr_t)operand_capacity * sizeof(*operands)) ||
        a64_typed_output_ranges_overlap(form_index, sizeof(*form_index), operand_count, sizeof(*operand_count)) ||
        a64_typed_output_ranges_overlap(form_index, sizeof(*form_index), modifiers,
                                         (uintptr_t)modifier_capacity * sizeof(*modifiers)) ||
        a64_typed_output_ranges_overlap(form_index, sizeof(*form_index), modifier_count, sizeof(*modifier_count)) || !a64_scalar_target_valid(target))
    {
        return false;
    }
    BusterAarch64CanonicalDecodeResult canonical = {0};
    if (buster_aarch64_canonical_decode(target, word, &canonical) != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS)
    {
        return false;
    }
    u32 selected = UINT32_MAX;
    if (!a64_typed_scalar_generated_index_for_digest(canonical.arm_row_digest, &selected))
    {
        return false;
    }
    A64ScalarIntOperand local_operands[4] = {0};
    A64ScalarIntModifier local_modifier = {0};
    u32 local_operand_count = 0;
    u32 local_modifier_count = 0;
    if (!buster_aarch64_arm_m1_scalar_integer_decode_form(target, selected, word, local_operands, BUSTER_ARRAY_LENGTH(local_operands),
                                                         &local_operand_count, &local_modifier, 1,
                                                         &local_modifier_count) ||
        local_operand_count > operand_capacity || local_modifier_count > modifier_capacity)
    {
        return false;
    }
    memcpy(operands, local_operands, (size_t)local_operand_count * sizeof(*operands));
    if (local_modifier_count)
    {
        memcpy(modifiers, &local_modifier, sizeof(local_modifier));
    }
    *form_index = selected;
    *operand_count = local_operand_count;
    *modifier_count = local_modifier_count;
    return true;
}

bool a64_arm_m1_scalar_integer_decode_form(
    Target target, u32 form_index, u32 word, A64ScalarIntOperand* operands, u32 operand_capacity, u32* operand_count,
    A64ScalarIntModifier* modifiers, u32 modifier_capacity, u32* modifier_count)
{
    return buster_aarch64_arm_m1_scalar_integer_decode_form(target, form_index, word, operands, operand_capacity, operand_count, modifiers,
                                                            modifier_capacity, modifier_count);
}

bool a64_arm_m1_scalar_integer_decode(
    Target target, u32 word, u32* form_index, A64ScalarIntOperand* operands, u32 operand_capacity, u32* operand_count,
    A64ScalarIntModifier* modifiers, u32 modifier_capacity, u32* modifier_count)
{
    return buster_aarch64_arm_m1_scalar_integer_decode(target, word, form_index, operands, operand_capacity, operand_count, modifiers,
                                                       modifier_capacity, modifier_count);
}
