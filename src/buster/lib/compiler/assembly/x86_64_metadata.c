#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/hash.h>

#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif
#include <buster/lib/compiler/assembly/generated/x86_64-assembly.generated.h>
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic pop
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

// These values cross the generated-table boundary through the public ABI.
// Keep every direct cast/copy tied to a compile-time schema check so a
// generator enum reorder fails here instead of changing runtime meaning.
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wenum-compare"
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wenum-compare"
#endif
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_DIRECT == BUSTER_X86_GENERATED_COVERAGE_DIRECT);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_NORMALIZED == BUSTER_X86_GENERATED_COVERAGE_NORMALIZED);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_NOT64 == BUSTER_X86_GENERATED_COVERAGE_NOT64);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_PRIVILEGED == BUSTER_X86_GENERATED_COVERAGE_PRIVILEGED);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_RESERVED == BUSTER_X86_GENERATED_COVERAGE_RESERVED);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_UNSUPPORTED_TOKEN == BUSTER_X86_GENERATED_COVERAGE_UNSUPPORTED_TOKEN);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_UNCLASSIFIED == BUSTER_X86_GENERATED_COVERAGE_UNCLASSIFIED);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_COUNT == BUSTER_X86_GENERATED_COVERAGE_UNCLASSIFIED + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_PREFIX_LEGACY == BUSTER_X86_GENERATED_PREFIX_LEGACY);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_PREFIX_REX == BUSTER_X86_GENERATED_PREFIX_REX);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_PREFIX_REX2 == BUSTER_X86_GENERATED_PREFIX_REX2);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_PREFIX_VEX == BUSTER_X86_GENERATED_PREFIX_VEX);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_PREFIX_XOP == BUSTER_X86_GENERATED_PREFIX_XOP);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_PREFIX_EVEX == BUSTER_X86_GENERATED_PREFIX_EVEX);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_PREFIX_COUNT == BUSTER_X86_GENERATED_PREFIX_EVEX + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ENCODER_LEGACY == BUSTER_X86_GENERATED_ENCODER_LEGACY);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ENCODER_REX == BUSTER_X86_GENERATED_ENCODER_REX);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ENCODER_REX2 == BUSTER_X86_GENERATED_ENCODER_REX2);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ENCODER_VEX == BUSTER_X86_GENERATED_ENCODER_VEX);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ENCODER_XOP == BUSTER_X86_GENERATED_ENCODER_XOP);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ENCODER_EVEX == BUSTER_X86_GENERATED_ENCODER_EVEX);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ENCODER_AMX == BUSTER_X86_GENERATED_ENCODER_AMX);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ENCODER_SYSTEM == BUSTER_X86_GENERATED_ENCODER_SYSTEM);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ENCODER_COUNT == BUSTER_X86_GENERATED_ENCODER_SYSTEM + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TEST_SCHEMA == BUSTER_X86_GENERATED_TEST_SCHEMA);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TEST_PRIVILEGED_SCHEMA == BUSTER_X86_GENERATED_TEST_PRIVILEGED_SCHEMA);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TEST_NOT64_SCHEMA == BUSTER_X86_GENERATED_TEST_NOT64_SCHEMA);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TEST_CLASS_COUNT == BUSTER_X86_GENERATED_TEST_NOT64_SCHEMA + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_NONE == BUSTER_X86_GENERATED_REASON_NONE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_MODE_NOT64 == BUSTER_X86_GENERATED_REASON_MODE_NOT64);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_CPL0 == BUSTER_X86_GENERATED_REASON_CPL0);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_UNKNOWN_PATTERN_TOKEN == BUSTER_X86_GENERATED_REASON_UNKNOWN_PATTERN_TOKEN);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_UNKNOWN_OPERAND_TOKEN == BUSTER_X86_GENERATED_REASON_UNKNOWN_OPERAND_TOKEN);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_COUNT == BUSTER_X86_GENERATED_REASON_UNKNOWN_OPERAND_TOKEN + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_NONE == BUSTER_X86_GENERATED_OPERAND_NONE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_REGISTER == BUSTER_X86_GENERATED_OPERAND_REGISTER);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_MEMORY == BUSTER_X86_GENERATED_OPERAND_MEMORY);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_IMMEDIATE == BUSTER_X86_GENERATED_OPERAND_IMMEDIATE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_RELATIVE == BUSTER_X86_GENERATED_OPERAND_RELATIVE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_ABSOLUTE == BUSTER_X86_GENERATED_OPERAND_ABSOLUTE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_BASE == BUSTER_X86_GENERATED_OPERAND_BASE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_SEGMENT == BUSTER_X86_GENERATED_OPERAND_SEGMENT);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR == BUSTER_X86_GENERATED_OPERAND_ADDRESS_GENERATOR);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_PSEUDO == BUSTER_X86_GENERATED_OPERAND_PSEUDO);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_OPERAND_KIND_COUNT == BUSTER_X86_GENERATED_OPERAND_PSEUDO + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ACCESS_READ == BUSTER_X86_GENERATED_ACCESS_READ);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ACCESS_WRITE == BUSTER_X86_GENERATED_ACCESS_WRITE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ACCESS_COND == BUSTER_X86_GENERATED_ACCESS_COND);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ACCESS_SUPPRESSED == BUSTER_X86_GENERATED_ACCESS_SUPPRESSED);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_ACCESS_IMPLICIT == BUSTER_X86_GENERATED_ACCESS_IMPLICIT);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_LEGACY == BUSTER_X86_GENERATED_MAP_LEGACY);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_0F == BUSTER_X86_GENERATED_MAP_0F);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_0F38 == BUSTER_X86_GENERATED_MAP_0F38);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_0F3A == BUSTER_X86_GENERATED_MAP_0F3A);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_4 == BUSTER_X86_GENERATED_MAP_4);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_5 == BUSTER_X86_GENERATED_MAP_5);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_6 == BUSTER_X86_GENERATED_MAP_6);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_7 == BUSTER_X86_GENERATED_MAP_7);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_X8 == BUSTER_X86_GENERATED_MAP_X8);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_X9 == BUSTER_X86_GENERATED_MAP_X9);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_XA == BUSTER_X86_GENERATED_MAP_XA);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MAP_COUNT == BUSTER_X86_GENERATED_MAP_XA + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_NONE == BUSTER_X86_GENERATED_TUPLE_NONE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_FULL == BUSTER_X86_GENERATED_TUPLE_FULL);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_HALF == BUSTER_X86_GENERATED_TUPLE_HALF);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_QUARTER == BUSTER_X86_GENERATED_TUPLE_QUARTER);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_EIGHTH == BUSTER_X86_GENERATED_TUPLE_EIGHTH);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_SCALAR == BUSTER_X86_GENERATED_TUPLE_SCALAR);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_TUPLE1 == BUSTER_X86_GENERATED_TUPLE_TUPLE1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_TUPLE1_4X == BUSTER_X86_GENERATED_TUPLE_TUPLE1_4X);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_TUPLE1_BYTE == BUSTER_X86_GENERATED_TUPLE_TUPLE1_BYTE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_TUPLE1_WORD == BUSTER_X86_GENERATED_TUPLE_TUPLE1_WORD);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_TUPLE2 == BUSTER_X86_GENERATED_TUPLE_TUPLE2);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_TUPLE4 == BUSTER_X86_GENERATED_TUPLE_TUPLE4);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_TUPLE8 == BUSTER_X86_GENERATED_TUPLE_TUPLE8);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TUPLE_COUNT == BUSTER_X86_GENERATED_TUPLE_TUPLE8 + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SOURCE_NONE == BUSTER_X86_GENERATED_FIELD_SOURCE_NONE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SOURCE_REG == BUSTER_X86_GENERATED_FIELD_SOURCE_REG);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SOURCE_RM == BUSTER_X86_GENERATED_FIELD_SOURCE_RM);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SOURCE_VVVV == BUSTER_X86_GENERATED_FIELD_SOURCE_VVVV);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SOURCE_MASK == BUSTER_X86_GENERATED_FIELD_SOURCE_MASK);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SOURCE_FIXED == BUSTER_X86_GENERATED_FIELD_SOURCE_FIXED);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE == BUSTER_X86_GENERATED_FIELD_SOURCE_IMMEDIATE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SOURCE_RELATIVE == BUSTER_X86_GENERATED_FIELD_SOURCE_RELATIVE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SOURCE_COUNT == BUSTER_X86_GENERATED_FIELD_SOURCE_RELATIVE + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_MODRM == BUSTER_X86_GENERATED_FIELD_MODRM);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_SIB == BUSTER_X86_GENERATED_FIELD_SIB);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_VSIB == BUSTER_X86_GENERATED_FIELD_VSIB);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_MEMORY == BUSTER_X86_GENERATED_FIELD_MEMORY);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_REGISTER == BUSTER_X86_GENERATED_FIELD_REGISTER);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_DISPLACEMENT == BUSTER_X86_GENERATED_FIELD_DISPLACEMENT);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_IMMEDIATE == BUSTER_X86_GENERATED_FIELD_IMMEDIATE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_RELATIVE == BUSTER_X86_GENERATED_FIELD_RELATIVE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_FIELD_END == BUSTER_X86_GENERATED_FIELD_FIELD_END);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_DECORATOR_MASK == BUSTER_X86_GENERATED_DECORATOR_MASK);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_DECORATOR_ZEROING == BUSTER_X86_GENERATED_DECORATOR_ZEROING);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_DECORATOR_BROADCAST == BUSTER_X86_GENERATED_DECORATOR_BROADCAST);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_DECORATOR_ROUNDING == BUSTER_X86_GENERATED_DECORATOR_ROUNDING);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_DECORATOR_SAE == BUSTER_X86_GENERATED_DECORATOR_SAE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_APX == BUSTER_X86_GENERATED_APX);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_APX_ND == BUSTER_X86_GENERATED_APX_ND);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_APX_NF == BUSTER_X86_GENERATED_APX_NF);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_APX_NDD == BUSTER_X86_GENERATED_APX_NDD);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_APX_SCC == BUSTER_X86_GENERATED_APX_SCC);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_APX_EGPR == BUSTER_X86_GENERATED_APX_EGPR);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_AMX_TILE_REGISTER == BUSTER_X86_GENERATED_AMX_TILE_REGISTER);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_AMX_TILE_MEMORY == BUSTER_X86_GENERATED_AMX_TILE_MEMORY);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_AMX_TILE_ROW == BUSTER_X86_GENERATED_AMX_TILE_ROW);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_AMX_TILE_COLUMN == BUSTER_X86_GENERATED_AMX_TILE_COLUMN);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MODE_16 == BUSTER_X86_GENERATED_MODE_16);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MODE_32 == BUSTER_X86_GENERATED_MODE_32);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MODE_64 == BUSTER_X86_GENERATED_MODE_64);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MODE_NOT64 == BUSTER_X86_GENERATED_MODE_NOT64);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MODE_EA16 == BUSTER_X86_GENERATED_MODE_EA16);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MODE_EA32 == BUSTER_X86_GENERATED_MODE_EA32);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MODE_EA64 == BUSTER_X86_GENERATED_MODE_EA64);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_MODE_EANOT16 == BUSTER_X86_GENERATED_MODE_EANOT16);
#if BUSTER_COMPILER_CLANG
#pragma clang diagnostic pop
#elif BUSTER_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

// Packed wire widths and generated counts are compile-time invariants. The
// checked-in representation intentionally has no large C aggregate arrays;
// these checks replace sizeof(array) checks while retaining exact schema ties.
BUSTER_CT_CHECK(sizeof(BusterX86GeneratedOperand) >= 16);
BUSTER_CT_CHECK(sizeof(BusterX86GeneratedForm) >= 156);
BUSTER_CT_CHECK(sizeof(BusterX86GeneratedCoverage) >= 25);
BUSTER_CT_CHECK(buster_x86_generated_operands_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_OPERAND_COUNT * 16u);
BUSTER_CT_CHECK(buster_x86_generated_forms_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_FORM_COUNT * 156u);
BUSTER_CT_CHECK(buster_x86_generated_coverage_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_COVERAGE_COUNT * 25u);
BUSTER_CT_CHECK(BUSTER_X86_GENERATED_STRING_POOL_SIZE > 0);
BUSTER_CT_CHECK(BUSTER_X86_GENERATED_INDEX_CAPACITY >= BUSTER_X86_GENERATED_FORM_COUNT);
BUSTER_CT_CHECK(buster_x86_generated_mnemonic_ranges_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_MNEMONIC_RANGE_COUNT * 12u);
BUSTER_CT_CHECK(buster_x86_generated_mnemonic_candidates_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_MNEMONIC_CANDIDATE_COUNT * 4u);
BUSTER_CT_CHECK(buster_x86_generated_iclass_ranges_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_ICLASS_RANGE_COUNT * 12u);
BUSTER_CT_CHECK(buster_x86_generated_iclass_candidates_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_ICLASS_CANDIDATE_COUNT * 4u);
BUSTER_CT_CHECK(buster_x86_generated_iform_ranges_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_IFORM_RANGE_COUNT * 12u);
BUSTER_CT_CHECK(buster_x86_generated_iform_candidates_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_IFORM_CANDIDATE_COUNT * 4u);
BUSTER_CT_CHECK(buster_x86_generated_form_hash_ranges_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_FORM_HASH_RANGE_COUNT * 16u);
BUSTER_CT_CHECK(buster_x86_generated_form_hash_candidates_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_FORM_HASH_CANDIDATE_COUNT * 4u);
BUSTER_CT_CHECK(buster_x86_generated_coverage_hash_ranges_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_COVERAGE_HASH_RANGE_COUNT * 16u);
BUSTER_CT_CHECK(buster_x86_generated_coverage_hash_candidates_blob_BYTE_COUNT == (u64)BUSTER_X86_GENERATED_COVERAGE_HASH_CANDIDATE_COUNT * 4u);

enum
{
    BUSTER_X86_METADATA_INDEX_MNEMONIC,
    BUSTER_X86_METADATA_INDEX_ICLASS,
    BUSTER_X86_METADATA_INDEX_IFORM,
    BUSTER_X86_METADATA_INDEX_FORM_HASH,
    BUSTER_X86_METADATA_INDEX_COVERAGE_HASH,
    BUSTER_X86_METADATA_INDEX_COUNT,
};

#define BUSTER_X86_METADATA_HASH_STRING_CAPACITY 4096u

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validation_fail(BusterX86MetadataValidationResult* result,
                                                              BusterX86MetadataValidationError error, u32 index, u32 detail)
{
    if (result)
    {
        *result = (BusterX86MetadataValidationResult){.valid = false, .error = error, .index = index, .detail = detail};
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_string_offset_terminated(u32 offset, u32* length)
{
    if (offset >= BUSTER_X86_GENERATED_STRING_POOL_SIZE)
    {
        return false;
    }
    u32 result = 0;
    while (offset <= BUSTER_X86_GENERATED_STRING_POOL_SIZE - result)
    {
        if (buster_x86_generated_string_byte((u64)offset + result) == 0)
        {
            if (length)
            {
                *length = result;
            }
            return true;
        }
        result += 1;
        if (result == BUSTER_X86_GENERATED_STRING_POOL_SIZE - offset)
        {
            break;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_string_equal_offsets(u32 first_offset, u32 second_offset)
{
    u32 first_length = 0;
    u32 second_length = 0;
    if (!buster_x86_metadata_string_offset_terminated(first_offset, &first_length) ||
        !buster_x86_metadata_string_offset_terminated(second_offset, &second_length) || first_length != second_length)
    {
        return false;
    }
    for (u32 index = 0; index < first_length; index += 1)
    {
        if (buster_x86_generated_string_byte((u64)first_offset + index) !=
            buster_x86_generated_string_byte((u64)second_offset + index))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u64 buster_x86_metadata_hash_string(u32 offset, bool* valid)
{
    u32 length = 0;
    if (!buster_x86_metadata_string_offset_terminated(offset, &length) || length > BUSTER_X86_METADATA_HASH_STRING_CAPACITY)
    {
        *valid = false;
        return 0;
    }
    u8 bytes[BUSTER_X86_METADATA_HASH_STRING_CAPACITY] = {0};
    for (u32 index = 0; index < length; index += 1)
    {
        bytes[index] = (u8)buster_x86_generated_string_byte((u64)offset + index);
    }
    static const u8 empty[] = {0};
    u8 const* pointer = length ? bytes : empty;
    return buster_hash_64((u8*)pointer, length);
}

BUSTER_GLOBAL_LOCAL u64 buster_x86_metadata_form_stable_hash(const BusterX86GeneratedForm* form, bool* valid)
{
    u32 offsets[] = {
        form->source_offset, form->iclass_offset, form->iform_offset, form->isa_set_offset, form->category_offset, form->extension_offset,
        form->attributes_offset, form->cpl_offset, form->exceptions_offset, form->flags_offset, form->disasm_offset,
        form->disasm_intel_offset, form->disasm_attsv_offset, form->real_opcode_offset, form->uname_offset, form->comment_offset,
        form->version_offset, form->pattern_offset, form->operands_offset, form->operand_annotation_offset,
    };
    u64 result = UINT64_C(0x9e3779b97f4a7c15);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(offsets); index += 1)
    {
        bool string_valid = true;
        u64 hash = buster_x86_metadata_hash_string(offsets[index], &string_valid);
        if (!string_valid)
        {
            *valid = false;
            return 0;
        }
        result ^= hash + UINT64_C(0x9e3779b97f4a7c15) + (result << 6) + (result >> 2);
    }
    *valid = true;
    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_string_offset(u32 offset, u32 index, u32 detail,
                                                                      BusterX86MetadataValidationResult* result)
{
    u32 length = 0;
    if (offset >= BUSTER_X86_GENERATED_STRING_POOL_SIZE)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_STRING_OFFSET, index, detail);
    }
    if (!buster_x86_metadata_string_offset_terminated(offset, &length))
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_STRING_TERMINATION, index, detail);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_operand_record(const BusterX86GeneratedOperand* operand, u32 index,
                                                                      BusterX86MetadataValidationResult* result)
{
    if (!buster_x86_metadata_validate_string_offset(operand->atom_offset, index, 0, result) ||
        !buster_x86_metadata_validate_string_offset(operand->width_offset, index, 1, result))
    {
        return false;
    }
    if (operand->reserved[0] || operand->reserved[1] || operand->reserved[2])
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_RESERVED, index, 0);
    }
    if ((operand->slot != UINT8_MAX && operand->slot >= 16) || operand->visible > 1 ||
        operand->kind >= BUSTER_X86_METADATA_OPERAND_KIND_COUNT ||
        operand->field_source >= BUSTER_X86_METADATA_FIELD_SOURCE_COUNT || (operand->access & ~0x1fu))
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_ENUM, index, 0);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_form_record(const BusterX86GeneratedForm* form, u32 index,
                                                                   BusterX86MetadataValidationResult* result)
{
    u32 offsets[] = {
        form->source_offset, form->iclass_offset, form->iform_offset, form->isa_set_offset, form->category_offset, form->extension_offset,
        form->attributes_offset, form->cpl_offset, form->exceptions_offset, form->flags_offset, form->disasm_offset,
        form->disasm_intel_offset, form->disasm_attsv_offset, form->real_opcode_offset, form->uname_offset, form->comment_offset,
        form->version_offset, form->pattern_offset, form->operands_offset, form->operand_annotation_offset, form->tuple_offset,
        form->element_size_offset, form->reason_offset,
    };
    for (u32 offset_index = 0; offset_index < BUSTER_ARRAY_LENGTH(offsets); offset_index += 1)
    {
        if (!buster_x86_metadata_validate_string_offset(offsets[offset_index], index, offset_index, result))
        {
            return false;
        }
    }
    bool hash_valid = true;
    u64 expected_hash = buster_x86_metadata_form_stable_hash(form, &hash_valid);
    if (!hash_valid || !form->stable_hash || form->stable_hash != expected_hash)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_FORM_HASH, index, 0);
    }
    if (form->operand_first > BUSTER_X86_GENERATED_OPERAND_COUNT ||
        form->operand_count > BUSTER_X86_GENERATED_OPERAND_COUNT - form->operand_first || form->operand_count > 16)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_OPERAND_RANGE, index, 0);
    }
    if (form->reserved[0] || form->reserved[1] || form->reserved[2] || form->reserved2)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_RESERVED, index, 0);
    }
    if (form->coverage_class >= BUSTER_X86_METADATA_COVERAGE_COUNT || form->encoder_family >= BUSTER_X86_METADATA_ENCODER_COUNT ||
        form->test_class >= BUSTER_X86_METADATA_TEST_CLASS_COUNT || form->prefix_kind >= BUSTER_X86_METADATA_PREFIX_COUNT ||
        form->map >= BUSTER_X86_METADATA_MAP_COUNT || form->tuple_kind >= BUSTER_X86_METADATA_TUPLE_COUNT || form->fixed_byte_count > 16 ||
        form->reason_id >= BUSTER_X86_METADATA_REASON_COUNT)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_ENUM, index, 0);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_coverage_record(const BusterX86GeneratedCoverage* coverage, u32 index,
                                                                       BusterX86MetadataValidationResult* result)
{
    if (!buster_x86_metadata_validate_string_offset(coverage->source_offset, index, 0, result) ||
        !buster_x86_metadata_validate_string_offset(coverage->reason_offset, index, 1, result))
    {
        return false;
    }
    if (!coverage->source_hash)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_HASH, index, 0);
    }
    if (coverage->coverage_class >= BUSTER_X86_METADATA_COVERAGE_COUNT || coverage->encoder_family >= BUSTER_X86_METADATA_ENCODER_COUNT ||
        coverage->test_class >= BUSTER_X86_METADATA_TEST_CLASS_COUNT || coverage->reason_id >= BUSTER_X86_METADATA_REASON_COUNT)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_ENUM, index, 0);
    }
    if (coverage->normalized_form_id >= BUSTER_X86_GENERATED_FORM_COUNT)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_FORM_ID, index,
                                                   coverage->normalized_form_id);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_text_range_count(u32 kind)
{
    switch (kind)
    {
        case BUSTER_X86_METADATA_INDEX_MNEMONIC: return BUSTER_X86_GENERATED_MNEMONIC_RANGE_COUNT;
        case BUSTER_X86_METADATA_INDEX_ICLASS: return BUSTER_X86_GENERATED_ICLASS_RANGE_COUNT;
        case BUSTER_X86_METADATA_INDEX_IFORM: return BUSTER_X86_GENERATED_IFORM_RANGE_COUNT;
        default: return 0;
    }
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_text_candidate_count(u32 kind)
{
    switch (kind)
    {
        case BUSTER_X86_METADATA_INDEX_MNEMONIC: return BUSTER_X86_GENERATED_MNEMONIC_CANDIDATE_COUNT;
        case BUSTER_X86_METADATA_INDEX_ICLASS: return BUSTER_X86_GENERATED_ICLASS_CANDIDATE_COUNT;
        case BUSTER_X86_METADATA_INDEX_IFORM: return BUSTER_X86_GENERATED_IFORM_CANDIDATE_COUNT;
        default: return 0;
    }
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedTextRange buster_x86_metadata_text_range_at(u32 kind, u32 index)
{
    switch (kind)
    {
        case BUSTER_X86_METADATA_INDEX_MNEMONIC: return buster_x86_generated_mnemonic_range_at(index);
        case BUSTER_X86_METADATA_INDEX_ICLASS: return buster_x86_generated_iclass_range_at(index);
        case BUSTER_X86_METADATA_INDEX_IFORM: return buster_x86_generated_iform_range_at(index);
        default: return (BusterX86GeneratedTextRange){0};
    }
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_text_candidate_at(u32 kind, u32 index)
{
    switch (kind)
    {
        case BUSTER_X86_METADATA_INDEX_MNEMONIC: return buster_x86_generated_mnemonic_candidate_at(index);
        case BUSTER_X86_METADATA_INDEX_ICLASS: return buster_x86_generated_iclass_candidate_at(index);
        case BUSTER_X86_METADATA_INDEX_IFORM: return buster_x86_generated_iform_candidate_at(index);
        default: return UINT32_MAX;
    }
}

BUSTER_GLOBAL_LOCAL int buster_x86_metadata_compare_pool_string(u32 offset, const char8* pointer, u32 length)
{
    u32 index = 0;
    while (index < length)
    {
        char8 left = buster_x86_generated_string_byte((u64)offset + index);
        if (!left)
        {
            return -1;
        }
        if (left != pointer[index])
        {
            return left > pointer[index] ? 1 : -1;
        }
        index += 1;
    }
    return buster_x86_generated_string_byte((u64)offset + length) == 0 ? 0 : 1;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_is_space(char8 character)
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\f' || character == '\v';
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_lowercase_pool_strings_equal(u32 first_offset, u32 second_offset)
{
    u32 first_length = 0;
    u32 second_length = 0;
    if (!buster_x86_metadata_string_offset_terminated(first_offset, &first_length) ||
        !buster_x86_metadata_string_offset_terminated(second_offset, &second_length) || first_length != second_length)
    {
        return false;
    }
    for (u32 index = 0; index < first_length; index += 1)
    {
        char8 first = buster_x86_generated_string_byte((u64)first_offset + index);
        char8 second = buster_x86_generated_string_byte((u64)second_offset + index);
        if (first >= 'A' && first <= 'Z') first = (char8)(first - 'A' + 'a');
        if (second >= 'A' && second <= 'Z') second = (char8)(second - 'A' + 'a');
        if (first != second) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_source_key_matches(const BusterX86GeneratedForm* form, u32 key_offset)
{
    u32 key_length = 0;
    if (!buster_x86_metadata_string_offset_terminated(key_offset, &key_length) || key_length == 0 || key_length > 255)
    {
        return false;
    }
    char8 key[256] = {0};
    for (u32 index = 0; index < key_length; index += 1)
    {
        key[index] = buster_x86_generated_string_byte((u64)key_offset + index);
    }
    u32 source_offsets[] = {form->disasm_intel_offset, form->disasm_attsv_offset, form->disasm_offset};
    bool had_source = false;
    for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(source_offsets); source_index += 1)
    {
        u32 source_length = 0;
        if (!buster_x86_metadata_string_offset_terminated(source_offsets[source_index], &source_length))
        {
            return false;
        }
        u32 start = 0;
        while (start < source_length &&
               buster_x86_metadata_is_space(buster_x86_generated_string_byte((u64)source_offsets[source_index] + start)))
        {
            start += 1;
        }
        u32 end = start;
        while (end < source_length &&
               !buster_x86_metadata_is_space(buster_x86_generated_string_byte((u64)source_offsets[source_index] + end)))
        {
            end += 1;
        }
        if (end == start)
        {
            continue;
        }
        had_source = true;
        if (end - start != key_length)
        {
            continue;
        }
        bool equal = true;
        for (u32 index = 0; index < key_length; index += 1)
        {
            char8 character = buster_x86_generated_string_byte((u64)source_offsets[source_index] + start + index);
            if (character >= 'A' && character <= 'Z')
            {
                character = (char8)(character - 'A' + 'a');
            }
            equal &= character == key[index];
        }
        if (equal)
        {
            return true;
        }
    }
    if (!had_source)
    {
        u32 iclass_length = 0;
        if (!buster_x86_metadata_string_offset_terminated(form->iclass_offset, &iclass_length) || iclass_length != key_length)
        {
            return false;
        }
        for (u32 index = 0; index < key_length; index += 1)
        {
            char8 character = buster_x86_generated_string_byte((u64)form->iclass_offset + index);
            if (character >= 'A' && character <= 'Z')
            {
                character = (char8)(character - 'A' + 'a');
            }
            if (character != key[index])
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_text_index(u32 kind, BusterX86MetadataValidationResult* result)
{
    u32 range_count = buster_x86_metadata_text_range_count(kind);
    u32 candidate_count = buster_x86_metadata_text_candidate_count(kind);
    if (range_count > BUSTER_X86_GENERATED_INDEX_CAPACITY || candidate_count > BUSTER_X86_GENERATED_INDEX_CAPACITY)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, kind, candidate_count);
    }
    u32 previous_key = 0;
    for (u32 range_index = 0; range_index < range_count; range_index += 1)
    {
        BusterX86GeneratedTextRange range = buster_x86_metadata_text_range_at(kind, range_index);
        if (!buster_x86_metadata_validate_string_offset(range.key_offset, range_index, kind, result) || !range.candidate_count ||
            range.candidate_first > candidate_count || range.candidate_count > candidate_count - range.candidate_first)
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, kind);
        }
        if (range_index)
        {
            u32 previous_length = 0;
            u32 current_length = 0;
            if (!buster_x86_metadata_string_offset_terminated(previous_key, &previous_length) ||
                !buster_x86_metadata_string_offset_terminated(range.key_offset, &current_length))
            {
                return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, kind);
            }
            u32 count = BUSTER_MIN(previous_length, current_length);
            int comparison = 0;
            for (u32 index = 0; index < count; index += 1)
            {
                char8 left = buster_x86_generated_string_byte((u64)previous_key + index);
                char8 right = buster_x86_generated_string_byte((u64)range.key_offset + index);
                if (left != right)
                {
                    comparison = left > right ? 1 : -1;
                    break;
                }
            }
            if (!comparison)
            {
                comparison = (previous_length > current_length) - (previous_length < current_length);
            }
            if (comparison >= 0)
            {
                return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, kind);
            }
        }
        previous_key = range.key_offset;
        u32 previous_id = 0;
        for (u32 candidate_index = 0; candidate_index < range.candidate_count; candidate_index += 1)
        {
            u32 id = buster_x86_metadata_text_candidate_at(kind, range.candidate_first + candidate_index);
            if (id == UINT32_MAX || id >= BUSTER_X86_GENERATED_FORM_COUNT || (candidate_index && id <= previous_id))
            {
                return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, candidate_index);
            }
            BusterX86GeneratedForm form = buster_x86_generated_form_at(id);
            bool key_matches = kind == BUSTER_X86_METADATA_INDEX_MNEMONIC
                                   ? buster_x86_metadata_form_source_key_matches(&form, range.key_offset)
                                   : buster_x86_metadata_lowercase_pool_strings_equal(
                                         range.key_offset, kind == BUSTER_X86_METADATA_INDEX_ICLASS ? form.iclass_offset : form.iform_offset);
            if (!key_matches)
            {
                return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, id);
            }
            previous_id = id;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_hash_range_count(u32 kind)
{
    return kind == BUSTER_X86_METADATA_INDEX_FORM_HASH ? BUSTER_X86_GENERATED_FORM_HASH_RANGE_COUNT
                                                        : BUSTER_X86_GENERATED_COVERAGE_HASH_RANGE_COUNT;
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_hash_candidate_count(u32 kind)
{
    return kind == BUSTER_X86_METADATA_INDEX_FORM_HASH ? BUSTER_X86_GENERATED_FORM_HASH_CANDIDATE_COUNT
                                                        : BUSTER_X86_GENERATED_COVERAGE_HASH_CANDIDATE_COUNT;
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedHashRange buster_x86_metadata_hash_range_at(u32 kind, u32 index)
{
    return kind == BUSTER_X86_METADATA_INDEX_FORM_HASH ? buster_x86_generated_form_hash_range_at(index)
                                                       : buster_x86_generated_coverage_hash_range_at(index);
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_hash_candidate_at(u32 kind, u32 index)
{
    return kind == BUSTER_X86_METADATA_INDEX_FORM_HASH ? buster_x86_generated_form_hash_candidate_at(index)
                                                       : buster_x86_generated_coverage_hash_candidate_at(index);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_hash_index(u32 kind, BusterX86MetadataValidationResult* result)
{
    u32 range_count = buster_x86_metadata_hash_range_count(kind);
    u32 candidate_count = buster_x86_metadata_hash_candidate_count(kind);
    if (range_count > BUSTER_X86_GENERATED_INDEX_CAPACITY || candidate_count > BUSTER_X86_GENERATED_INDEX_CAPACITY)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, kind, candidate_count);
    }
    u64 previous_key = 0;
    for (u32 range_index = 0; range_index < range_count; range_index += 1)
    {
        BusterX86GeneratedHashRange range = buster_x86_metadata_hash_range_at(kind, range_index);
        if (!range.key || !range.candidate_count || range.candidate_first > candidate_count ||
            range.candidate_count > candidate_count - range.candidate_first || (range_index && range.key <= previous_key))
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, kind);
        }
        previous_key = range.key;
        u32 previous_id = 0;
        for (u32 candidate_index = 0; candidate_index < range.candidate_count; candidate_index += 1)
        {
            u32 id = buster_x86_metadata_hash_candidate_at(kind, range.candidate_first + candidate_index);
            if (id == UINT32_MAX || (kind == BUSTER_X86_METADATA_INDEX_FORM_HASH && id >= BUSTER_X86_GENERATED_FORM_COUNT) ||
                (kind == BUSTER_X86_METADATA_INDEX_COVERAGE_HASH && id >= BUSTER_X86_GENERATED_COVERAGE_COUNT) ||
                (candidate_index && id <= previous_id))
            {
                return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, candidate_index);
            }
            if (kind == BUSTER_X86_METADATA_INDEX_FORM_HASH)
            {
                BusterX86GeneratedForm form = buster_x86_generated_form_at(id);
                if (form.stable_hash != range.key)
                {
                    return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, id);
                }
            }
            else
            {
                BusterX86GeneratedCoverage coverage = buster_x86_generated_coverage_at(id);
                if (coverage.source_hash != range.key)
                {
                    return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, id);
                }
            }
            previous_id = id;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_index_storage(BusterX86MetadataValidationResult* result)
{
    if (BUSTER_X86_GENERATED_INDEX_CAPACITY < BUSTER_X86_GENERATED_FORM_COUNT ||
        BUSTER_X86_GENERATED_INDEX_CAPACITY < BUSTER_X86_GENERATED_COVERAGE_COUNT ||
        BUSTER_X86_GENERATED_MNEMONIC_CANDIDATE_COUNT > BUSTER_X86_GENERATED_INDEX_CAPACITY ||
        BUSTER_X86_GENERATED_ICLASS_CANDIDATE_COUNT > BUSTER_X86_GENERATED_INDEX_CAPACITY ||
        BUSTER_X86_GENERATED_IFORM_CANDIDATE_COUNT > BUSTER_X86_GENERATED_INDEX_CAPACITY ||
        BUSTER_X86_GENERATED_FORM_HASH_CANDIDATE_COUNT > BUSTER_X86_GENERATED_INDEX_CAPACITY ||
        BUSTER_X86_GENERATED_COVERAGE_HASH_CANDIDATE_COUNT > BUSTER_X86_GENERATED_INDEX_CAPACITY)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, 0, 0);
    }
    return buster_x86_metadata_validate_text_index(BUSTER_X86_METADATA_INDEX_MNEMONIC, result) &&
           buster_x86_metadata_validate_text_index(BUSTER_X86_METADATA_INDEX_ICLASS, result) &&
           buster_x86_metadata_validate_text_index(BUSTER_X86_METADATA_INDEX_IFORM, result) &&
           buster_x86_metadata_validate_hash_index(BUSTER_X86_METADATA_INDEX_FORM_HASH, result) &&
           buster_x86_metadata_validate_hash_index(BUSTER_X86_METADATA_INDEX_COVERAGE_HASH, result);
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataString buster_x86_metadata_string_unchecked(u32 offset)
{
    BusterX86MetadataString result = {0};
    u32 length = 0;
    if (buster_x86_metadata_string_offset_terminated(offset, &length))
    {
        result.offset = offset;
        result.length = length;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_copy_form(BusterX86GeneratedForm source, u32 form_id,
                                                       BusterX86MetadataForm* destination)
{
    *destination = (BusterX86MetadataForm){
        .id = form_id,
        .stable_hash = source.stable_hash,
        .source = buster_x86_metadata_string_unchecked(source.source_offset),
        .iclass = buster_x86_metadata_string_unchecked(source.iclass_offset),
        .iform = buster_x86_metadata_string_unchecked(source.iform_offset),
        .isa_set = buster_x86_metadata_string_unchecked(source.isa_set_offset),
        .category = buster_x86_metadata_string_unchecked(source.category_offset),
        .extension = buster_x86_metadata_string_unchecked(source.extension_offset),
        .attributes = buster_x86_metadata_string_unchecked(source.attributes_offset),
        .cpl = buster_x86_metadata_string_unchecked(source.cpl_offset),
        .exceptions = buster_x86_metadata_string_unchecked(source.exceptions_offset),
        .flags = buster_x86_metadata_string_unchecked(source.flags_offset),
        .disasm = buster_x86_metadata_string_unchecked(source.disasm_offset),
        .disasm_intel = buster_x86_metadata_string_unchecked(source.disasm_intel_offset),
        .disasm_att = buster_x86_metadata_string_unchecked(source.disasm_attsv_offset),
        .real_opcode = buster_x86_metadata_string_unchecked(source.real_opcode_offset),
        .uname = buster_x86_metadata_string_unchecked(source.uname_offset),
        .comment = buster_x86_metadata_string_unchecked(source.comment_offset),
        .version = buster_x86_metadata_string_unchecked(source.version_offset),
        .pattern = buster_x86_metadata_string_unchecked(source.pattern_offset),
        .operands = buster_x86_metadata_string_unchecked(source.operands_offset),
        .operand_annotation = buster_x86_metadata_string_unchecked(source.operand_annotation_offset),
        .tuple = buster_x86_metadata_string_unchecked(source.tuple_offset),
        .element_size = buster_x86_metadata_string_unchecked(source.element_size_offset),
        .reason = buster_x86_metadata_string_unchecked(source.reason_offset),
        .operand_first = source.operand_first,
        .operand_count = source.operand_count,
        .coverage_class = source.coverage_class,
        .encoder_family = source.encoder_family,
        .test_class = source.test_class,
        .prefix_kind = source.prefix_kind,
        .map = source.map,
        .fixed_byte_count = source.fixed_byte_count,
        .mandatory_prefix = source.mandatory_prefix,
        .field_flags = source.field_flags,
        .decorator_flags = source.decorator_flags,
        .apx_flags = source.apx_flags,
        .amx_flags = source.amx_flags,
        .mode_flags = source.mode_flags,
        .displacement_width = source.displacement_width,
        .displacement_scale = source.displacement_scale,
        .immediate_width = source.immediate_width,
        .immediate_signed = source.immediate_signed,
        .relocation_base = source.relocation_base,
        .tuple_kind = source.tuple_kind,
        .tuple_offset = source.tuple_offset,
        .element_size_offset = source.element_size_offset,
        .token_count = source.token_count,
        .reason_id = source.reason_id,
    };
    memcpy(destination->fixed_bytes, source.fixed_bytes, sizeof(destination->fixed_bytes));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_is_64_bit(BusterX86GeneratedForm form)
{
    u16 mode = form.mode_flags;
    u16 mode_bits = mode & (BUSTER_X86_GENERATED_MODE_16 | BUSTER_X86_GENERATED_MODE_32 | BUSTER_X86_GENERATED_MODE_64);
    return !(mode & BUSTER_X86_GENERATED_MODE_NOT64) && (!mode_bits || (mode_bits & BUSTER_X86_GENERATED_MODE_64));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_matches_filter(BusterX86GeneratedForm form, BusterX86MetadataFilter filter)
{
    u8 coverage_class = form.coverage_class;
    if (filter.require_64_bit && !buster_x86_metadata_form_is_64_bit(form)) return false;
    if (filter.exclude_not64 && (coverage_class == BUSTER_X86_GENERATED_COVERAGE_NOT64 || form.mode_flags & BUSTER_X86_GENERATED_MODE_NOT64)) return false;
    if (filter.privileged_only && coverage_class != BUSTER_X86_GENERATED_COVERAGE_PRIVILEGED) return false;
    if (filter.exclude_privileged && coverage_class == BUSTER_X86_GENERATED_COVERAGE_PRIVILEGED) return false;
    if (filter.exclude_reserved && coverage_class == BUSTER_X86_GENERATED_COVERAGE_RESERVED) return false;
    if (filter.exclude_unsupported_token && coverage_class == BUSTER_X86_GENERATED_COVERAGE_UNSUPPORTED_TOKEN) return false;
    if (filter.has_coverage_class_mask && !(filter.coverage_class_mask & (1u << coverage_class))) return false;
    if (filter.has_prefix_kind && form.prefix_kind != filter.prefix_kind) return false;
    if (filter.has_encoder_family && form.encoder_family != filter.encoder_family) return false;
    if (filter.has_isa_set && !buster_x86_metadata_string_equal_offsets(form.isa_set_offset, filter.isa_set.offset)) return false;
    if (filter.has_operand_count && form.operand_count != filter.operand_count) return false;
    u32 visible_count = 0;
    if (filter.has_visible_operand_count || filter.operand_shape_count)
    {
        if (filter.operand_shape_count > BUSTER_X86_METADATA_MAX_OPERAND_SHAPE ||
            (filter.operand_shape_count && form.operand_count != filter.operand_shape_count)) return false;
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterX86GeneratedOperand operand = buster_x86_generated_operand_at(form.operand_first + operand_index);
            visible_count += operand.visible != 0;
            if (filter.operand_shape_count)
            {
                BusterX86MetadataOperandShape shape = filter.operand_shape[operand_index];
                if (shape.kind != BUSTER_X86_METADATA_ANY_U8 && shape.kind != operand.kind) return false;
                if (shape.visible != BUSTER_X86_METADATA_ANY_U8 && shape.visible != operand.visible) return false;
            }
        }
    }
    return !filter.has_visible_operand_count || visible_count == filter.visible_operand_count;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_table(BusterX86MetadataValidationResult* result)
{
    if (BUSTER_X86_GENERATED_SCHEMA_VERSION != 2 || !BUSTER_X86_GENERATED_PACKED_BASE64)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_SCHEMA_VERSION, 0,
                                                   BUSTER_X86_GENERATED_SCHEMA_VERSION);
    }
    if (BUSTER_X86_GENERATED_FORM_COUNT != BUSTER_X86_GENERATED_COVERAGE_COUNT ||
        BUSTER_X86_GENERATED_FORM_COUNT != 11013 || BUSTER_X86_GENERATED_OPERAND_COUNT != 32813)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COUNT, 0, BUSTER_X86_GENERATED_FORM_COUNT);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_OPERAND_COUNT; index += 1)
    {
        BusterX86GeneratedOperand operand = buster_x86_generated_operand_at(index);
        if (!buster_x86_metadata_validate_operand_record(&operand, index, result)) return false;
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_FORM_COUNT; index += 1)
    {
        BusterX86GeneratedForm form = buster_x86_generated_form_at(index);
        if (!buster_x86_metadata_validate_form_record(&form, index, result)) return false;
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_COVERAGE_COUNT; index += 1)
    {
        BusterX86GeneratedCoverage coverage = buster_x86_generated_coverage_at(index);
        if (!buster_x86_metadata_validate_coverage_record(&coverage, index, result)) return false;
        if (coverage.normalized_form_id != index)
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_FORM_ID, index,
                                                       coverage.normalized_form_id);
        }
        BusterX86GeneratedForm form = buster_x86_generated_form_at(coverage.normalized_form_id);
        if (coverage.source_hash != form.stable_hash || coverage.source_offset != form.source_offset)
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_SOURCE, index,
                                                       coverage.normalized_form_id);
        }
        if (coverage.reason_id != form.reason_id || coverage.reason_offset != form.reason_offset)
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_REASON, index,
                                                       coverage.normalized_form_id);
        }
        if (coverage.coverage_class != form.coverage_class || coverage.encoder_family != form.encoder_family ||
            coverage.test_class != form.test_class)
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_CLASSIFICATION, index,
                                                       coverage.normalized_form_id);
        }
    }
    if (!buster_x86_metadata_validate_index_storage(result)) return false;
    if (result)
    {
        *result = (BusterX86MetadataValidationResult){.valid = true, .error = BUSTER_X86_METADATA_VALIDATION_NONE};
    }
    return true;
}

u32 buster_x86_metadata_schema_version(void) { return BUSTER_X86_GENERATED_SCHEMA_VERSION; }
u32 buster_x86_metadata_form_count(void) { return BUSTER_X86_GENERATED_FORM_COUNT; }
u32 buster_x86_metadata_coverage_count(void) { return BUSTER_X86_GENERATED_COVERAGE_COUNT; }
u32 buster_x86_metadata_operand_count(void) { return BUSTER_X86_GENERATED_OPERAND_COUNT; }
u32 buster_x86_metadata_string_pool_size(void) { return BUSTER_X86_GENERATED_STRING_POOL_SIZE; }

u32 buster_x86_metadata_normalized_form_count(void)
{
    u32 count = 0;
    for (u32 index = 0; index < BUSTER_X86_GENERATED_FORM_COUNT; index += 1)
    {
        count += buster_x86_generated_form_at(index).coverage_class == BUSTER_X86_GENERATED_COVERAGE_NORMALIZED;
    }
    return count;
}

BusterX86MetadataCounts buster_x86_metadata_counts(void)
{
    BusterX86MetadataCounts result = {
        .total_form_count = BUSTER_X86_GENERATED_FORM_COUNT,
        .normalized_form_count = buster_x86_metadata_normalized_form_count(),
        .coverage_count = BUSTER_X86_GENERATED_COVERAGE_COUNT,
    };
    for (u32 index = 0; index < BUSTER_X86_GENERATED_COVERAGE_COUNT; index += 1)
    {
        BusterX86GeneratedCoverage coverage = buster_x86_generated_coverage_at(index);
        if (coverage.coverage_class < BUSTER_X86_METADATA_COVERAGE_COUNT) result.coverage_class_counts[coverage.coverage_class] += 1;
        if (coverage.reason_id < BUSTER_X86_METADATA_REASON_COUNT) result.reason_counts[coverage.reason_id] += 1;
    }
    return result;
}

bool buster_x86_metadata_validate(BusterX86MetadataValidationResult* result)
{
    return buster_x86_metadata_validate_table(result);
}

bool buster_x86_metadata_string(u32 offset, BusterX86MetadataString* result)
{
    if (!result || !buster_x86_metadata_string_offset_terminated(offset, &result->length)) return false;
    result->offset = offset;
    return true;
}

u8 buster_x86_metadata_string_byte(BusterX86MetadataString string, u32 index)
{
    return index < string.length ? (u8)buster_x86_generated_string_byte((u64)string.offset + index) : 0;
}

bool buster_x86_metadata_form(u32 form_id, BusterX86MetadataForm* result)
{
    if (!result || form_id >= BUSTER_X86_GENERATED_FORM_COUNT) return false;
    BusterX86GeneratedForm form = buster_x86_generated_form_at(form_id);
    if (!buster_x86_metadata_validate_form_record(&form, form_id, 0)) return false;
    buster_x86_metadata_copy_form(form, form_id, result);
    return true;
}

bool buster_x86_metadata_operand(u32 form_id, u32 operand_index, BusterX86MetadataOperand* result)
{
    if (!result || form_id >= BUSTER_X86_GENERATED_FORM_COUNT) return false;
    BusterX86GeneratedForm form = buster_x86_generated_form_at(form_id);
    if (!buster_x86_metadata_validate_form_record(&form, form_id, 0) || operand_index >= form.operand_count ||
        form.operand_first > BUSTER_X86_GENERATED_OPERAND_COUNT || operand_index >= BUSTER_X86_GENERATED_OPERAND_COUNT - form.operand_first)
    {
        return false;
    }
    BusterX86GeneratedOperand operand = buster_x86_generated_operand_at(form.operand_first + operand_index);
    if (!buster_x86_metadata_validate_operand_record(&operand, form.operand_first + operand_index, 0)) return false;
    *result = (BusterX86MetadataOperand){
        .atom = buster_x86_metadata_string_unchecked(operand.atom_offset), .width = buster_x86_metadata_string_unchecked(operand.width_offset),
        .slot = operand.slot, .visible = operand.visible, .kind = operand.kind, .access = operand.access, .field_source = operand.field_source,
    };
    return true;
}

bool buster_x86_metadata_coverage(u32 coverage_id, BusterX86MetadataCoverage* result)
{
    if (!result || coverage_id >= BUSTER_X86_GENERATED_COVERAGE_COUNT) return false;
    BusterX86GeneratedCoverage coverage = buster_x86_generated_coverage_at(coverage_id);
    if (!buster_x86_metadata_validate_coverage_record(&coverage, coverage_id, 0) || coverage.normalized_form_id != coverage_id) return false;
    BusterX86GeneratedForm form = buster_x86_generated_form_at(coverage.normalized_form_id);
    if (!buster_x86_metadata_validate_form_record(&form, coverage.normalized_form_id, 0) || coverage.source_hash != form.stable_hash ||
        coverage.source_offset != form.source_offset || coverage.reason_id != form.reason_id || coverage.reason_offset != form.reason_offset ||
        coverage.coverage_class != form.coverage_class || coverage.encoder_family != form.encoder_family || coverage.test_class != form.test_class)
    {
        return false;
    }
    *result = (BusterX86MetadataCoverage){
        .id = coverage_id, .source_hash = coverage.source_hash, .source = buster_x86_metadata_string_unchecked(coverage.source_offset),
        .normalized_form_id = coverage.normalized_form_id, .coverage_class = coverage.coverage_class,
        .encoder_family = coverage.encoder_family, .test_class = coverage.test_class, .reason_id = coverage.reason_id,
        .reason = buster_x86_metadata_string_unchecked(coverage.reason_offset),
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_normalize_lookup(String8 input, bool first_token, char8* buffer, u32 capacity, u32* length)
{
    if (!input.pointer || !input.length || input.length > UINT32_MAX) return false;
    u32 start = 0;
    while (start < input.length && buster_x86_metadata_is_space(input.pointer[start])) start += 1;
    u32 end = start;
    while (end < input.length && (!first_token || !buster_x86_metadata_is_space(input.pointer[end]))) end += 1;
    u32 index = 0;
    if (!first_token)
    {
        while (end > start && buster_x86_metadata_is_space(input.pointer[end - 1])) end -= 1;
        if (end != start)
        {
            for (index = start; index < end; index += 1)
            {
                if (buster_x86_metadata_is_space(input.pointer[index])) return false;
            }
        }
    }
    if (end == start || end - start >= capacity) return false;
    for (index = start; index < end; index += 1)
    {
        char8 character = input.pointer[index];
        if (character >= 'A' && character <= 'Z') character = (char8)(character - 'A' + 'a');
        buffer[index - start] = character;
    }
    *length = (u32)(end - start);
    return true;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataCandidateRange buster_x86_metadata_lookup_text(u32 kind, String8 input)
{
    BusterX86MetadataCandidateRange result = {0};
    char8 buffer[256] = {0};
    u32 length = 0;
    if (!buster_x86_metadata_normalize_lookup(input, kind == BUSTER_X86_METADATA_INDEX_MNEMONIC, buffer, sizeof(buffer), &length)) return result;
    u32 low = 0;
    u32 high = buster_x86_metadata_text_range_count(kind);
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        BusterX86GeneratedTextRange range = buster_x86_metadata_text_range_at(kind, middle);
        int comparison = buster_x86_metadata_compare_pool_string(range.key_offset, buffer, length);
        if (comparison < 0) low = middle + 1;
        else high = middle;
    }
    if (low < buster_x86_metadata_text_range_count(kind))
    {
        BusterX86GeneratedTextRange range = buster_x86_metadata_text_range_at(kind, low);
        if (buster_x86_metadata_compare_pool_string(range.key_offset, buffer, length) == 0)
        {
            result.first = range.candidate_first;
            result.count = range.candidate_count;
            result.index_kind = (u8)kind;
        }
    }
    return result;
}

BusterX86MetadataCandidateRange buster_x86_metadata_lookup_mnemonic(String8 mnemonic)
{
    return buster_x86_metadata_lookup_text(BUSTER_X86_METADATA_INDEX_MNEMONIC, mnemonic);
}

BusterX86MetadataCandidateRange buster_x86_metadata_lookup_iclass(String8 iclass)
{
    return buster_x86_metadata_lookup_text(BUSTER_X86_METADATA_INDEX_ICLASS, iclass);
}

BusterX86MetadataCandidateRange buster_x86_metadata_lookup_iform(String8 iform)
{
    return buster_x86_metadata_lookup_text(BUSTER_X86_METADATA_INDEX_IFORM, iform);
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataCandidateRange buster_x86_metadata_lookup_hash(u32 kind, u64 key)
{
    BusterX86MetadataCandidateRange result = {0};
    if (!key) return result;
    u32 low = 0;
    u32 high = buster_x86_metadata_hash_range_count(kind);
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        BusterX86GeneratedHashRange range = buster_x86_metadata_hash_range_at(kind, middle);
        if (range.key < key) low = middle + 1;
        else high = middle;
    }
    if (low < buster_x86_metadata_hash_range_count(kind))
    {
        BusterX86GeneratedHashRange range = buster_x86_metadata_hash_range_at(kind, low);
        if (range.key == key)
        {
            result.first = range.candidate_first;
            result.count = range.candidate_count;
            result.index_kind = (u8)kind;
        }
    }
    return result;
}

BusterX86MetadataCandidateRange buster_x86_metadata_lookup_form_hash(u64 stable_hash)
{
    return buster_x86_metadata_lookup_hash(BUSTER_X86_METADATA_INDEX_FORM_HASH, stable_hash);
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataCoverageRange buster_x86_metadata_empty_coverage_range(void)
{
    return (BusterX86MetadataCoverageRange){0};
}

BusterX86MetadataCoverageRange buster_x86_metadata_lookup_coverage_hash(u64 source_hash)
{
    BusterX86MetadataCoverageRange result = buster_x86_metadata_empty_coverage_range();
    BusterX86MetadataCandidateRange candidates = buster_x86_metadata_lookup_hash(BUSTER_X86_METADATA_INDEX_COVERAGE_HASH, source_hash);
    result.first = candidates.first;
    result.count = candidates.count;
    return result;
}

bool buster_x86_metadata_candidate_at(BusterX86MetadataCandidateRange candidates, u32 position, u32* form_id)
{
    if (!form_id || position >= candidates.count || candidates.first > BUSTER_X86_GENERATED_INDEX_CAPACITY ||
        position >= BUSTER_X86_GENERATED_INDEX_CAPACITY - candidates.first)
    {
        return false;
    }
    u32 id = buster_x86_metadata_text_candidate_at(candidates.index_kind, candidates.first + position);
    if (candidates.index_kind == BUSTER_X86_METADATA_INDEX_FORM_HASH)
    {
        id = buster_x86_generated_form_hash_candidate_at(candidates.first + position);
    }
    if (id == UINT32_MAX || id >= BUSTER_X86_GENERATED_FORM_COUNT) return false;
    *form_id = id;
    return true;
}

bool buster_x86_metadata_coverage_candidate_at(BusterX86MetadataCoverageRange candidates, u32 position, u32* coverage_id)
{
    if (!coverage_id || position >= candidates.count || candidates.first > BUSTER_X86_GENERATED_COVERAGE_HASH_CANDIDATE_COUNT ||
        position >= BUSTER_X86_GENERATED_COVERAGE_HASH_CANDIDATE_COUNT - candidates.first)
    {
        return false;
    }
    u32 id = buster_x86_generated_coverage_hash_candidate_at(candidates.first + position);
    if (id == UINT32_MAX || id >= BUSTER_X86_GENERATED_COVERAGE_COUNT) return false;
    *coverage_id = id;
    return true;
}

BusterX86MetadataCandidateIterator buster_x86_metadata_filter(BusterX86MetadataCandidateRange candidates,
                                                               BusterX86MetadataFilter filter)
{
    return (BusterX86MetadataCandidateIterator){.candidates = candidates, .filter = filter};
}

bool buster_x86_metadata_candidate_next(BusterX86MetadataCandidateIterator* iterator, u32* form_id)
{
    if (!iterator || !form_id) return false;
    while (iterator->position < iterator->candidates.count)
    {
        u32 id = 0;
        u32 position = iterator->position++;
        if (!buster_x86_metadata_candidate_at(iterator->candidates, position, &id)) return false;
        BusterX86GeneratedForm form = buster_x86_generated_form_at(id);
        if (buster_x86_metadata_form_matches_filter(form, iterator->filter))
        {
            *form_id = id;
            return true;
        }
    }
    return false;
}

#if BUSTER_INCLUDE_TESTS
bool buster_x86_metadata_validate_patch(BusterX86MetadataValidationPatch patch, BusterX86MetadataValidationResult* result)
{
    if (patch.kind >= BUSTER_X86_METADATA_PATCH_COUNT)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_ENUM, patch.index, patch.kind);
    }
    if (patch.kind == BUSTER_X86_METADATA_PATCH_INDEX_CAPACITY)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, patch.index, (u32)patch.value);
    }
    if (patch.kind <= BUSTER_X86_METADATA_PATCH_FORM_RESERVED2)
    {
        if (patch.index >= BUSTER_X86_GENERATED_FORM_COUNT)
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COUNT, patch.index, 0);
        }
        BusterX86GeneratedForm form = buster_x86_generated_form_at(patch.index);
        switch (patch.kind)
        {
            case BUSTER_X86_METADATA_PATCH_FORM_SOURCE_OFFSET: form.source_offset = (u32)patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_ICLASS_OFFSET: form.iclass_offset = (u32)patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_STABLE_HASH: form.stable_hash = patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_OPERAND_RANGE:
                form.operand_first = (u32)(patch.value >> 32);
                form.operand_count = (u16)patch.value;
                break;
            case BUSTER_X86_METADATA_PATCH_FORM_COVERAGE_CLASS: form.coverage_class = (u8)patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_PREFIX_KIND: form.prefix_kind = (u8)patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_RESERVED:
                form.reserved[0] = (u8)patch.value;
                form.reserved[1] = (u8)(patch.value >> 8);
                form.reserved[2] = (u8)(patch.value >> 16);
                break;
            case BUSTER_X86_METADATA_PATCH_FORM_RESERVED2: form.reserved2 = (u16)patch.value; break;
            default: break;
        }
        return buster_x86_metadata_validate_form_record(&form, patch.index, result);
    }
    if (patch.kind == BUSTER_X86_METADATA_PATCH_OPERAND_RESERVED)
    {
        if (patch.index >= BUSTER_X86_GENERATED_OPERAND_COUNT)
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COUNT, patch.index, 0);
        }
        BusterX86GeneratedOperand operand = buster_x86_generated_operand_at(patch.index);
        operand.reserved[0] = (u8)patch.value;
        operand.reserved[1] = (u8)(patch.value >> 8);
        operand.reserved[2] = (u8)(patch.value >> 16);
        return buster_x86_metadata_validate_operand_record(&operand, patch.index, result);
    }
    if (patch.index >= BUSTER_X86_GENERATED_COVERAGE_COUNT)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COUNT, patch.index, 0);
    }
    BusterX86GeneratedCoverage coverage = buster_x86_generated_coverage_at(patch.index);
    switch (patch.kind)
    {
        case BUSTER_X86_METADATA_PATCH_COVERAGE_SOURCE_HASH: coverage.source_hash = patch.value; break;
        case BUSTER_X86_METADATA_PATCH_COVERAGE_FORM_ID: coverage.normalized_form_id = (u32)patch.value; break;
        case BUSTER_X86_METADATA_PATCH_COVERAGE_CLASS: coverage.coverage_class = (u8)patch.value; break;
        case BUSTER_X86_METADATA_PATCH_COVERAGE_SOURCE_OFFSET: coverage.source_offset = (u32)patch.value; break;
        case BUSTER_X86_METADATA_PATCH_COVERAGE_REASON_OFFSET: coverage.reason_offset = (u32)patch.value; break;
        case BUSTER_X86_METADATA_PATCH_COVERAGE_REASON_ID: coverage.reason_id = (u16)patch.value; break;
        case BUSTER_X86_METADATA_PATCH_COVERAGE_ENCODER_FAMILY: coverage.encoder_family = (u8)patch.value; break;
        case BUSTER_X86_METADATA_PATCH_COVERAGE_TEST_CLASS: coverage.test_class = (u8)patch.value; break;
        default: break;
    }
    if (!buster_x86_metadata_validate_coverage_record(&coverage, patch.index, result)) return false;
    if (coverage.normalized_form_id != patch.index || coverage.normalized_form_id >= BUSTER_X86_GENERATED_FORM_COUNT)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_FORM_ID, patch.index,
                                                   coverage.normalized_form_id);
    }
    BusterX86GeneratedForm form = buster_x86_generated_form_at(coverage.normalized_form_id);
    if (coverage.source_hash != form.stable_hash || coverage.source_offset != form.source_offset)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_SOURCE, patch.index,
                                                   coverage.normalized_form_id);
    }
    if (coverage.reason_id != form.reason_id || coverage.reason_offset != form.reason_offset)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_REASON, patch.index,
                                                   coverage.normalized_form_id);
    }
    if (coverage.coverage_class != form.coverage_class || coverage.encoder_family != form.encoder_family ||
        coverage.test_class != form.test_class)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_CLASSIFICATION, patch.index,
                                                   coverage.normalized_form_id);
    }
    if (result) *result = (BusterX86MetadataValidationResult){.valid = true, .error = BUSTER_X86_METADATA_VALIDATION_NONE};
    return true;
}
#endif
