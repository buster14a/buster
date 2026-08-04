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

#define BUSTER_X86_METADATA_FIELD_FLAGS_ALL \
    (BUSTER_X86_GENERATED_FIELD_MODRM | BUSTER_X86_GENERATED_FIELD_SIB | BUSTER_X86_GENERATED_FIELD_VSIB | \
     BUSTER_X86_GENERATED_FIELD_MEMORY | BUSTER_X86_GENERATED_FIELD_REGISTER | BUSTER_X86_GENERATED_FIELD_DISPLACEMENT | \
     BUSTER_X86_GENERATED_FIELD_IMMEDIATE | BUSTER_X86_GENERATED_FIELD_RELATIVE | BUSTER_X86_GENERATED_FIELD_FIELD_END)
#define BUSTER_X86_METADATA_DECORATOR_FLAGS_ALL \
    (BUSTER_X86_GENERATED_DECORATOR_MASK | BUSTER_X86_GENERATED_DECORATOR_ZEROING | BUSTER_X86_GENERATED_DECORATOR_BROADCAST | \
     BUSTER_X86_GENERATED_DECORATOR_ROUNDING | BUSTER_X86_GENERATED_DECORATOR_SAE)
#define BUSTER_X86_METADATA_APX_FLAGS_ALL \
    (BUSTER_X86_GENERATED_APX | BUSTER_X86_GENERATED_APX_ND | BUSTER_X86_GENERATED_APX_NF | BUSTER_X86_GENERATED_APX_NDD | \
     BUSTER_X86_GENERATED_APX_SCC | BUSTER_X86_GENERATED_APX_EGPR)
#define BUSTER_X86_METADATA_AMX_FLAGS_ALL \
    (BUSTER_X86_GENERATED_AMX_TILE_REGISTER | BUSTER_X86_GENERATED_AMX_TILE_MEMORY | BUSTER_X86_GENERATED_AMX_TILE_ROW | \
     BUSTER_X86_GENERATED_AMX_TILE_COLUMN)
#define BUSTER_X86_METADATA_MODE_FLAGS_ALL \
    (BUSTER_X86_GENERATED_MODE_16 | BUSTER_X86_GENERATED_MODE_32 | BUSTER_X86_GENERATED_MODE_64 | \
     BUSTER_X86_GENERATED_MODE_NOT64 | BUSTER_X86_GENERATED_MODE_EA16 | BUSTER_X86_GENERATED_MODE_EA32 | \
     BUSTER_X86_GENERATED_MODE_EA64 | BUSTER_X86_GENERATED_MODE_EANOT16)
#define BUSTER_X86_METADATA_PHYSICAL_WIDTH_FLAGS_ALL \
    (BUSTER_X86_METADATA_PHYSICAL_WIDTH_8 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_16 | \
     BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_64 | \
     BUSTER_X86_METADATA_PHYSICAL_WIDTH_80 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 | \
     BUSTER_X86_METADATA_PHYSICAL_WIDTH_256 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_512 | \
     BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN)

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_physical_operand_view(BusterX86GeneratedForm form,
                                                                      BusterX86GeneratedOperand operand,
                                                                      u8* physical_class, u16* physical_width_flags);

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
    if ((form->field_flags & ~BUSTER_X86_METADATA_FIELD_FLAGS_ALL) ||
        (form->decorator_flags & ~BUSTER_X86_METADATA_DECORATOR_FLAGS_ALL) ||
        (form->apx_flags & ~BUSTER_X86_METADATA_APX_FLAGS_ALL) || (form->amx_flags & ~BUSTER_X86_METADATA_AMX_FLAGS_ALL) ||
        (form->mode_flags & ~BUSTER_X86_METADATA_MODE_FLAGS_ALL) || form->immediate_signed > 1 || form->displacement_scale > 1 ||
        (form->mandatory_prefix != 0 && form->mandatory_prefix != 0x66 && form->mandatory_prefix != 0xf2 &&
         form->mandatory_prefix != 0xf3) ||
        (form->relocation_base > 1))
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS, index, 0);
    }
    if ((form->immediate_width != 0 && form->immediate_width != 1 && form->immediate_width != 2 && form->immediate_width != 4 &&
         form->immediate_width != 8) ||
        (form->displacement_width != 0 && form->displacement_width != 1 && form->displacement_width != 2 &&
         form->displacement_width != 4 && form->displacement_width != 8))
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS, index, 1);
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
    u8 physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE;
    u16 physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
    buster_x86_metadata_physical_operand_view(form, operand, &physical_class, &physical_width_flags);
    *result = (BusterX86MetadataOperand){
        .atom = buster_x86_metadata_string_unchecked(operand.atom_offset), .width = buster_x86_metadata_string_unchecked(operand.width_offset),
        .slot = operand.slot, .visible = operand.visible, .kind = operand.kind, .access = operand.access, .field_source = operand.field_source,
        .physical_class = physical_class, .physical_width_flags = physical_width_flags,
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

BUSTER_GLOBAL_LOCAL char8 buster_x86_metadata_lowercase_character(char8 character)
{
    return character >= 'A' && character <= 'Z' ? (char8)(character - 'A' + 'a') : character;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_query_string_valid(String8 string)
{
    return string.length <= UINT32_MAX && (!string.length || string.pointer != 0);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_string_input_equal(u32 offset, String8 input)
{
    u32 length = 0;
    if (!buster_x86_metadata_string_offset_terminated(offset, &length) || length != input.length) return false;
    for (u32 index = 0; index < length; index += 1)
    {
        char8 left = buster_x86_generated_string_byte((u64)offset + index);
        char8 right = input.pointer[index];
        if (buster_x86_metadata_lowercase_character(left) != buster_x86_metadata_lowercase_character(right)) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_pool_string_equal_literal(u32 offset, String8 literal)
{
    return buster_x86_metadata_string_input_equal(offset, literal);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_pool_string_has_prefix(u32 offset, String8 prefix)
{
    u32 length = 0;
    if (!buster_x86_metadata_string_offset_terminated(offset, &length) || prefix.length > length) return false;
    for (u32 index = 0; index < prefix.length; index += 1)
    {
        char8 left = buster_x86_generated_string_byte((u64)offset + index);
        char8 right = prefix.pointer[index];
        if (buster_x86_metadata_lowercase_character(left) != buster_x86_metadata_lowercase_character(right)) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_pool_string_has_suffix(u32 offset, String8 suffix)
{
    u32 length = 0;
    if (!buster_x86_metadata_string_offset_terminated(offset, &length) || suffix.length > length) return false;
    u32 first = length - (u32)suffix.length;
    for (u32 index = 0; index < suffix.length; index += 1)
    {
        char8 left = buster_x86_generated_string_byte((u64)offset + first + index);
        char8 right = suffix.pointer[index];
        if (buster_x86_metadata_lowercase_character(left) != buster_x86_metadata_lowercase_character(right)) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_pool_string_contains(u32 offset, String8 needle)
{
    u32 length = 0;
    if (!needle.length || !buster_x86_metadata_string_offset_terminated(offset, &length) || needle.length > length) return false;
    for (u32 first = 0; first <= length - needle.length; first += 1)
    {
        bool equal = true;
        for (u32 index = 0; index < needle.length; index += 1)
        {
            char8 left = buster_x86_generated_string_byte((u64)offset + first + index);
            char8 right = needle.pointer[index];
            if (buster_x86_metadata_lowercase_character(left) != buster_x86_metadata_lowercase_character(right))
            {
                equal = false;
                break;
            }
        }
        if (equal) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_feature_input_contains_literal(BusterX86MetadataFeatureInput input,
                                                                              String8 literal)
{
    for (u32 index = 0; index < input.count; index += 1)
    {
        String8 feature = input.names[index];
        if (feature.length == 1 && feature.pointer[0] == '*') return true;
        if (feature.length == literal.length)
        {
            bool equal = true;
            for (u32 character = 0; character < literal.length; character += 1)
            {
                if (buster_x86_metadata_lowercase_character(feature.pointer[character]) !=
                    buster_x86_metadata_lowercase_character(literal.pointer[character]))
                {
                    equal = false;
                    break;
                }
            }
            if (equal) return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_feature_input_contains_pool(BusterX86MetadataFeatureInput input, u32 offset)
{
    if (!buster_x86_metadata_string_offset_terminated(offset, 0)) return false;
    for (u32 index = 0; index < input.count; index += 1)
    {
        String8 feature = input.names[index];
        if (buster_x86_metadata_string_input_equal(offset, feature) ||
            (feature.length == 1 && feature.pointer[0] == '*'))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_feature_input_contains_all(BusterX86MetadataFeatureInput input,
                                                                         String8 first, String8 second, String8 third,
                                                                         String8 fourth)
{
    String8 required[4] = {first, second, third, fourth};
    for (u32 index = 0; index < sizeof(required) / sizeof(required[0]); index += 1)
    {
        if (required[index].length && !buster_x86_metadata_feature_input_contains_literal(input, required[index])) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_feature_input_contains_avx512_width(u32 offset,
                                                                                   BusterX86MetadataFeatureInput input,
                                                                                   String8 feature)
{
    if (!buster_x86_metadata_feature_input_contains_literal(input, feature)) return false;
    return (!buster_x86_metadata_pool_string_has_suffix(offset, S8("128")) &&
            !buster_x86_metadata_pool_string_has_suffix(offset, S8("128N")) &&
            !buster_x86_metadata_pool_string_has_suffix(offset, S8("256"))) ||
           buster_x86_metadata_feature_input_contains_literal(input, S8("avx512vl"));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_canonical_feature_matches(u32 offset, BusterX86MetadataFeatureInput input)
{
    // The raw spelling is always an explicit escape hatch for metadata clients
    // that have the exact generated ISA vocabulary.  Canonical spellings are
    // handled below only where the mapping is known to be conjunctive.
    if (buster_x86_metadata_feature_input_contains_pool(input, offset)) return true;

    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AMX_TILE")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("amx-tile"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AMX_INT8")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("amx-tile"), S8("amx-int8"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AMX_BF16")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("amx-tile"), S8("amx-bf16"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AMX_FP16")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("amx-tile"), S8("amx-fp16"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AMX_COMPLEX")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("amx-tile"), S8("amx-complex"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AMX_FP8")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("amx-tile"), S8("amx-fp8"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AMX_AVX512")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("amx-tile"), S8("amx-avx512"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AMX_MOVRS")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("amx-tile"), S8("amx-movrs"), S8(""), S8(""));

    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("APX_F")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("apx"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_has_prefix(offset, S8("APX_F_")))
    {
        bool n3 = buster_x86_metadata_pool_string_contains(offset, S8("N3"));
        String8 secondary = S8("");
        if (buster_x86_metadata_pool_string_contains(offset, S8("BMI1"))) secondary = S8("bmi1");
        else if (buster_x86_metadata_pool_string_contains(offset, S8("BMI2"))) secondary = S8("bmi2");
        else if (buster_x86_metadata_pool_string_contains(offset, S8("LZCNT"))) secondary = S8("lzcnt");
        else if (buster_x86_metadata_pool_string_contains(offset, S8("POPCNT"))) secondary = S8("popcnt");
        else if (buster_x86_metadata_pool_string_contains(offset, S8("AMX_MOVRS")))
            secondary = S8("amx-movrs");
        else if (buster_x86_metadata_pool_string_contains(offset, S8("AMX"))) secondary = S8("amx-tile");
        else if (buster_x86_metadata_pool_string_contains(offset, S8("MOVRS"))) secondary = S8("movrs");
        else if (!n3)
            return false;
        return buster_x86_metadata_feature_input_contains_all(input, S8("apx"),
                                                               n3 ? S8("apx-nci-ndd-nf") : S8(""), secondary, S8(""));
    }

    if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX10_2_BF16_")))
    {
        return buster_x86_metadata_feature_input_contains_all(
            input, S8("avx10.2"), S8("avx512bf16"),
            buster_x86_metadata_pool_string_has_suffix(offset, S8("512")) ? S8("avx10-512") : S8(""), S8(""));
    }
    if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX10_V2_AUX_")))
    {
        return buster_x86_metadata_feature_input_contains_all(
            input, S8("avx10.2"), S8("avx10-v1-aux"),
            buster_x86_metadata_pool_string_has_suffix(offset, S8("512")) ? S8("avx10-512") : S8(""), S8(""));
    }
    if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX10_MOVRS_")))
    {
        return buster_x86_metadata_feature_input_contains_all(
            input, S8("avx10.1"), S8("movrs"),
            buster_x86_metadata_pool_string_has_suffix(offset, S8("512")) ? S8("avx10-512") : S8(""), S8(""));
    }

    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX2")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx2"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX_GFNI")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx"), S8("gfni"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX_IFMA")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx"), S8("avx-ifma"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX_NE_CONVERT")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx"), S8("avx-ne-convert"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX_VNNI")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx2"), S8("avx-vnni"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX_VNNI_INT8")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx2"), S8("avx-vnni-int8"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX_VNNI_INT16")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx2"), S8("avx-vnni-int16"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("GFNI")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("gfni"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("VAES")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("vaes"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("VPCLMULQDQ")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("vpclmulqdq"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AES")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("aes"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("PCLMUL")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("pclmul"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SSE")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sse2"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SSE2")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sse2"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SSE3")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sse3"), S8(""), S8(""), S8(""));

    // AVX-512 forms are width-sensitive.  Require the corresponding
    // canonical subfeature, and require AVX512VL for the 128/256 encodings.
    String8 avx512_feature = S8("");
    if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512F_"))) avx512_feature = S8("avx512f");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512BW_"))) avx512_feature = S8("avx512bw");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512CD_"))) avx512_feature = S8("avx512cd");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512DQ_"))) avx512_feature = S8("avx512dq");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512ER_"))) avx512_feature = S8("avx512er");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512PF_"))) avx512_feature = S8("avx512pf");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512IFMA_"))) avx512_feature = S8("avx512ifma");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512VBMI_"))) avx512_feature = S8("avx512vbmi");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512VBMI2_"))) avx512_feature = S8("avx512vbmi2");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512VNNI_"))) avx512_feature = S8("avx512vnni");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512BITALG_"))) avx512_feature = S8("avx512bitalg");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512VPOPCNTDQ_"))) avx512_feature = S8("avx512vpopcntdq");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512VP2INTERSECT_")))
        avx512_feature = S8("avx512vp2intersect");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_BF16_"))) avx512_feature = S8("avx512bf16");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_FP16_"))) avx512_feature = S8("avx512fp16");
    if (avx512_feature.length)
        return buster_x86_metadata_feature_input_contains_avx512_width(offset, input, avx512_feature);
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_is_baseline(BusterX86GeneratedForm form)
{
    return buster_x86_metadata_string_input_equal(form.extension_offset, S8("BASE"));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_feature_available(BusterX86GeneratedForm form,
                                                                      BusterX86MetadataFeatureInput input)
{
    // BASE is the implicit x86-64 baseline and is available regardless of
    // which optional effective features the caller listed.
    if (buster_x86_metadata_form_is_baseline(form)) return true;
    u32 requirement = form.isa_set_offset;
    u32 requirement_length = 0;
    if (!buster_x86_metadata_string_offset_terminated(requirement, &requirement_length)) return false;
    // Some imported rows have no ISA-set token and use the encoder family as
    // their only feature requirement.  This is a fallback, never an OR with
    // a more specific isa_set: extension cannot authorize a specific ISA row.
    if (!requirement_length) requirement = form.extension_offset;
    if (!buster_x86_metadata_string_offset_terminated(requirement, &requirement_length)) return false;
    if (!requirement_length) return true;
    return buster_x86_metadata_canonical_feature_matches(requirement, input);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_atom_base_equal(u32 offset, String8 input, u8 kind)
{
    u32 candidate_length = 0;
    if (!buster_x86_metadata_string_offset_terminated(offset, &candidate_length) || !input.length) return false;
    u32 candidate_end = candidate_length;
    u32 input_end = (u32)input.length;
    if (kind == BUSTER_X86_METADATA_OPERAND_REGISTER)
    {
        for (u32 index = 0; index < candidate_end; index += 1)
        {
            if (buster_x86_generated_string_byte((u64)offset + index) == '_')
            {
                candidate_end = index;
                break;
            }
        }
        for (u32 index = 0; index < input_end; index += 1)
        {
            if (input.pointer[index] == '_')
            {
                input_end = index;
                break;
            }
        }
    }
    else if (kind == BUSTER_X86_METADATA_OPERAND_MEMORY || kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE ||
             kind == BUSTER_X86_METADATA_OPERAND_RELATIVE || kind == BUSTER_X86_METADATA_OPERAND_ABSOLUTE ||
             kind == BUSTER_X86_METADATA_OPERAND_BASE || kind == BUSTER_X86_METADATA_OPERAND_SEGMENT)
    {
        while (candidate_end && buster_x86_generated_string_byte((u64)offset + candidate_end - 1) >= '0' &&
               buster_x86_generated_string_byte((u64)offset + candidate_end - 1) <= '9')
        {
            candidate_end -= 1;
        }
        while (input_end && input.pointer[input_end - 1] >= '0' && input.pointer[input_end - 1] <= '9') input_end -= 1;
    }
    if (candidate_end != input_end) return false;
    for (u32 index = 0; index < candidate_end; index += 1)
    {
        char8 left = buster_x86_generated_string_byte((u64)offset + index);
        char8 right = input.pointer[index];
        if (buster_x86_metadata_lowercase_character(left) != buster_x86_metadata_lowercase_character(right)) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_physical_width_from_bytes(u8 bytes)
{
    switch (bytes)
    {
        case 1: return BUSTER_X86_METADATA_PHYSICAL_WIDTH_8;
        case 2: return BUSTER_X86_METADATA_PHYSICAL_WIDTH_16;
        case 4: return BUSTER_X86_METADATA_PHYSICAL_WIDTH_32;
        case 8: return BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
        default: return BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
    }
}

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_physical_width_from_token(u32 offset)
{
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("b")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("i8")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("u8")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("f8")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("zi8")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("z4i8")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("z4u8")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_8;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("w")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("i16")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("u16")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("f16")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("bf16")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("zi16")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("z2i16")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("z2u16")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_16;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("d")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("i32")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("u32")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("f32")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("zi32")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("zd")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_32;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("q")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("i64")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("u64")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("f64")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("zi64")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("dq")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("i128")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("u128")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_128;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("qq")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("u256")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_256;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("m512"))) return BUSTER_X86_METADATA_PHYSICAL_WIDTH_512;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("v")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_16 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 |
               BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("z")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    return BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_physical_register_view(BusterX86GeneratedOperand operand, u8* physical_class,
                                                                      u16* physical_width_flags)
{
    *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN;
    *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
    // The importer preserves fixed XED_REG_* atoms for accumulator-only and
    // other fixed-register forms.  Resolve only architectural spellings whose
    // class and width are unambiguous.  Status/flags/instruction-pointer and
    // x87 pseudo-registers are intentionally SPECIAL/UNKNOWN below, while a
    // spelling not covered by this table remains conservatively UNKNOWN.
    if (buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_AL")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_AH")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_BL")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_BH")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_CL")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_CH")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_DL")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_DH")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_SPL")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_BPL")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_SIL")) ||
        buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_DIL")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_8;
    }
    else if (buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_AX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_BX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_CX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_DX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_SI")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_DI")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_BP")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_SP")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_16;
    }
    else if (buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_EAX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_EBX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_ECX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_EDX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_ESI")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_EDI")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_EBP")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_ESP")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_32;
    }
    else if (buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RAX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RBX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RCX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RDX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RSI")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RDI")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RBP")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RSP")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_R8")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_R9")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_R10")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_R11")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_R12")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_R13")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_R14")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_R15")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_XMM")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_128;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_YMM")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_256;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_ZMM")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_512;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_K")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_TMM")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_MMX")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_BND")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_BND;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_128;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_CR")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_XCR0")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_DR")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_CS")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_DS")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_ES")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_FS")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_GS")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_SS")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_16;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_X87")) ||
             buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XED_REG_ST")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_BSR0")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_FSBASE")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_GSBASE")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_GDTR")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_IDTR")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_LDTR")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_MSRS")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_SSP")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_STACKPOP")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_STACKPUSH")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_TR")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_TSC")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_TSCAUX")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_UIF")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_IA32_KERNEL_GS_BASE")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_IP")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_EIP")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RIP")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_FLAGS")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_EFLAGS")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_RFLAGS")) ||
             buster_x86_metadata_pool_string_equal_literal(operand.atom_offset, S8("XED_REG_MXCSR")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("GPR8")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_8;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("GPR16")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_16;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("GPR32")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_32;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("GPR64")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("GPRv")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_16 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 |
                                BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("GPRy")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("GPRz")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("A_GPR")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = buster_x86_metadata_physical_width_from_token(operand.width_offset);
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("XMM")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_128;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("YMM")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_256;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("ZMM")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_512;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("MMX")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("MASK")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK;
        *physical_width_flags = buster_x86_metadata_physical_width_from_token(operand.width_offset);
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("TMM")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("BND")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_BND;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_128;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("CR")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("DR")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("SEG")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_16;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("X87")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_80;
    }
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_physical_operand_view(BusterX86GeneratedForm form,
                                                                      BusterX86GeneratedOperand operand,
                                                                      u8* physical_class, u16* physical_width_flags)
{
    *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE;
    *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
    switch (operand.kind)
    {
        case BUSTER_X86_GENERATED_OPERAND_REGISTER:
            buster_x86_metadata_physical_register_view(operand, physical_class, physical_width_flags);
            break;
        case BUSTER_X86_GENERATED_OPERAND_MEMORY:
            *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY;
            *physical_width_flags = buster_x86_metadata_physical_width_from_token(operand.width_offset);
            break;
        case BUSTER_X86_GENERATED_OPERAND_IMMEDIATE:
            *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_IMMEDIATE;
            *physical_width_flags = buster_x86_metadata_physical_width_from_bytes(form.immediate_width);
            break;
        case BUSTER_X86_GENERATED_OPERAND_RELATIVE:
            *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_RELATIVE;
            *physical_width_flags = buster_x86_metadata_physical_width_from_bytes(form.displacement_width);
            break;
        case BUSTER_X86_GENERATED_OPERAND_ABSOLUTE: *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_ABSOLUTE; break;
        case BUSTER_X86_GENERATED_OPERAND_BASE: *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_BASE; break;
        case BUSTER_X86_GENERATED_OPERAND_SEGMENT: *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT; break;
        case BUSTER_X86_GENERATED_OPERAND_ADDRESS_GENERATOR:
            *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_ADDRESS_GENERATOR;
            break;
        case BUSTER_X86_GENERATED_OPERAND_PSEUDO: *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_PSEUDO; break;
        default: break;
    }
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_operand_signature_matches(BusterX86GeneratedOperand operand,
                                                                         BusterX86GeneratedForm form,
                                                                         BusterX86MetadataOperandSignature signature)
{
    if (signature.kind != BUSTER_X86_METADATA_OPERAND_ANY && signature.kind != operand.kind) return false;
    if (signature.has_atom && !buster_x86_metadata_string_input_equal(operand.atom_offset, signature.atom) &&
        !buster_x86_metadata_atom_base_equal(operand.atom_offset, signature.atom, operand.kind))
    {
        return false;
    }
    if (signature.has_width && !buster_x86_metadata_string_input_equal(operand.width_offset, signature.width)) return false;
    if (signature.has_field_source && signature.field_source != operand.field_source) return false;
    if (signature.has_access && signature.access != operand.access) return false;
    if (signature.has_slot && signature.slot != operand.slot) return false;
    bool has_physical_class = signature.has_physical_class || signature.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE;
    bool has_physical_width = signature.has_physical_width || signature.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY;
    if (has_physical_class || has_physical_width)
    {
        u8 physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE;
        u16 physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
        buster_x86_metadata_physical_operand_view(form, operand, &physical_class, &physical_width_flags);
        if (has_physical_class && signature.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY &&
            signature.physical_class != physical_class)
            return false;
        if (has_physical_width && signature.physical_width_flags &&
            !(signature.physical_width_flags & physical_width_flags))
            return false;
    }
    if (signature.has_visible && signature.visible != operand.visible) return false;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_query_has_memory_signature(BusterX86MetadataResolveQuery query)
{
    for (u32 index = 0; index < query.operand_count; index += 1)
    {
        BusterX86MetadataOperandSignature signature = query.operands[index];
        bool has_physical_class = signature.has_physical_class ||
                                  signature.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE;
        if (signature.kind == BUSTER_X86_METADATA_OPERAND_MEMORY ||
            (has_physical_class && signature.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY))
            return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_operand_signatures_match(BusterX86GeneratedForm form,
                                                                              BusterX86MetadataResolveQuery query,
                                                                              bool* shape_matches, u32* selected_count,
                                                                              bool* has_memory)
{
    u32 selected = 0;
    u32 query_index = 0;
    *has_memory = false;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86GeneratedOperand operand = buster_x86_generated_operand_at(form.operand_first + operand_index);
        if (!buster_x86_metadata_validate_operand_record(&operand, form.operand_first + operand_index, 0)) return false;
        bool selected_operand = query.include_implicit || operand.visible;
        if (selected_operand && operand.kind == BUSTER_X86_GENERATED_OPERAND_MEMORY) *has_memory = true;
        if (!selected_operand) continue;
        selected += 1;
        if (selected > query.operand_count ||
            !buster_x86_metadata_operand_signature_matches(operand, form, query.operands[query_index]))
        {
            *shape_matches = false;
        }
        query_index += 1;
    }
    *selected_count = selected;
    if (selected != query.operand_count) *shape_matches = false;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_address_size_matches(BusterX86GeneratedForm form, u8 address_size,
                                                                        bool has_memory)
{
    if (!has_memory || address_size == BUSTER_X86_METADATA_ADDRESS_SIZE_ANY) return true;
    u16 address_flags = form.mode_flags &
                        (BUSTER_X86_GENERATED_MODE_EA16 | BUSTER_X86_GENERATED_MODE_EA32 | BUSTER_X86_GENERATED_MODE_EA64 |
                         BUSTER_X86_GENERATED_MODE_EANOT16);
    if (!address_flags) return true;
    if (address_size == 16) return (address_flags & BUSTER_X86_GENERATED_MODE_EA16) != 0;
    if (address_size == 32) return (address_flags & (BUSTER_X86_GENERATED_MODE_EA32 | BUSTER_X86_GENERATED_MODE_EANOT16)) != 0;
    return address_size == 64 && (address_flags & (BUSTER_X86_GENERATED_MODE_EA64 | BUSTER_X86_GENERATED_MODE_EANOT16)) != 0;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_has_non64_mode(BusterX86GeneratedForm form)
{
    u16 mode_bits = form.mode_flags &
                    (BUSTER_X86_GENERATED_MODE_16 | BUSTER_X86_GENERATED_MODE_32 | BUSTER_X86_GENERATED_MODE_64 |
                     BUSTER_X86_GENERATED_MODE_NOT64);
    // MODE_16|MODE_32|MODE_64 is a multi-mode row and is legal in 64-bit
    // execution.  Only an explicit MODE_NOT64, NOT64 coverage, or an
    // explicit mode set with no MODE_64 bit is non-64.
    return form.coverage_class == BUSTER_X86_GENERATED_COVERAGE_NOT64 || (mode_bits & BUSTER_X86_GENERATED_MODE_NOT64) ||
           (mode_bits && !(mode_bits & BUSTER_X86_GENERATED_MODE_64));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_execution_mode_matches(BusterX86GeneratedForm form,
                                                                           BusterX86MetadataResolveQuery query)
{
    u16 mode_bits = form.mode_flags &
                    (BUSTER_X86_GENERATED_MODE_16 | BUSTER_X86_GENERATED_MODE_32 | BUSTER_X86_GENERATED_MODE_64 |
                     BUSTER_X86_GENERATED_MODE_NOT64);
    bool explicit_not64 = form.coverage_class == BUSTER_X86_GENERATED_COVERAGE_NOT64 ||
                          (mode_bits & BUSTER_X86_GENERATED_MODE_NOT64) != 0;
    bool has_64 = (mode_bits & BUSTER_X86_GENERATED_MODE_64) != 0;
    bool has_explicit_mode = mode_bits != 0;
    if (query.include_not64) return true;
    if (explicit_not64) return false;
    if (query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64 && has_explicit_mode && !has_64) return false;
    // No execution-mode bits means that the generated row carries no mode
    // restriction; coverage and the remaining metadata filters still apply.
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_field_flags_match(BusterX86GeneratedForm form,
                                                                      BusterX86MetadataResolveQuery query)
{
    return (form.field_flags & query.required_field_flags) == query.required_field_flags &&
           !(form.field_flags & query.forbidden_field_flags);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_coverage_allowed(BusterX86GeneratedForm form,
                                                                      BusterX86MetadataResolveQuery query)
{
    if (form.coverage_class == BUSTER_X86_GENERATED_COVERAGE_NORMALIZED) return true;
    if (form.coverage_class == BUSTER_X86_GENERATED_COVERAGE_PRIVILEGED && query.include_privileged) return true;
    if (form.coverage_class == BUSTER_X86_GENERATED_COVERAGE_NOT64 && query.include_not64) return true;
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_resolution_query_valid(BusterX86MetadataResolveQuery query, u32* error_detail)
{
    if (!buster_x86_metadata_query_string_valid(query.mnemonic) ||
        (query.operand_count && !query.operands) || (query.features.count && !query.features.names) || query.operand_count > 16 ||
        query.decorator_flags & ~BUSTER_X86_METADATA_DECORATOR_FLAGS_ALL || query.apx_flags & ~BUSTER_X86_METADATA_APX_FLAGS_ALL ||
        query.amx_flags & ~BUSTER_X86_METADATA_AMX_FLAGS_ALL ||
        query.required_field_flags & ~BUSTER_X86_METADATA_FIELD_FLAGS_ALL ||
        query.forbidden_field_flags & ~BUSTER_X86_METADATA_FIELD_FLAGS_ALL ||
        query.required_field_flags & query.forbidden_field_flags ||
        (query.address_size != BUSTER_X86_METADATA_ADDRESS_SIZE_ANY && query.address_size != 16 && query.address_size != 32 &&
         query.address_size != 64) ||
        query.execution_mode >= BUSTER_X86_METADATA_EXECUTION_MODE_COUNT ||
        (query.decorator_flags & BUSTER_X86_METADATA_DECORATOR_ZEROING &&
         !(query.decorator_flags & BUSTER_X86_METADATA_DECORATOR_MASK)) ||
        query.reserved)
    {
        if (error_detail) *error_detail = 0;
        return false;
    }
    for (u32 index = 0; index < query.features.count; index += 1)
    {
        String8 feature = query.features.names[index];
        if (!buster_x86_metadata_query_string_valid(feature) || !feature.length)
        {
            if (error_detail) *error_detail = index;
            return false;
        }
        for (u32 character = 0; character < feature.length; character += 1)
        {
            if (buster_x86_metadata_is_space(feature.pointer[character]))
            {
                if (error_detail) *error_detail = index;
                return false;
            }
        }
    }
    for (u32 index = 0; index < query.operand_count; index += 1)
    {
        BusterX86MetadataOperandSignature signature = query.operands[index];
        bool has_physical_class = signature.has_physical_class || signature.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE;
        bool has_physical_width = signature.has_physical_width || signature.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY;
        if (!buster_x86_metadata_query_string_valid(signature.atom) || !buster_x86_metadata_query_string_valid(signature.width) ||
            (signature.kind != BUSTER_X86_METADATA_OPERAND_ANY && signature.kind >= BUSTER_X86_METADATA_OPERAND_KIND_COUNT) ||
            (signature.has_field_source && signature.field_source >= BUSTER_X86_METADATA_FIELD_SOURCE_COUNT) ||
            (signature.has_access && signature.access & ~0x1fu) ||
            (signature.has_slot && signature.slot != UINT8_MAX && signature.slot >= 16) ||
            (has_physical_class && signature.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY &&
             signature.physical_class >= BUSTER_X86_METADATA_PHYSICAL_CLASS_COUNT) ||
            (has_physical_width && signature.physical_width_flags & ~BUSTER_X86_METADATA_PHYSICAL_WIDTH_FLAGS_ALL) ||
            (signature.has_visible && signature.visible > 1) || signature.reserved[0] || signature.reserved[1])
        {
            if (error_detail) *error_detail = index;
            return false;
        }
    }
    bool has_memory = buster_x86_metadata_query_has_memory_signature(query);
    if ((query.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST) && !has_memory)
    {
        if (error_detail) *error_detail = query.operand_count + 1;
        return false;
    }
    return true;
}

BusterX86MetadataResolveResult buster_x86_metadata_resolve(BusterX86MetadataResolveQuery query, u32* form_ids,
                                                           u32 form_id_capacity)
{
    BusterX86MetadataResolveResult result = {
        .status = BUSTER_X86_METADATA_RESOLVE_INVALID_INPUT,
        .form_ids = form_ids,
        .form_id_capacity = form_id_capacity,
    };
    if ((form_id_capacity && !form_ids) || !buster_x86_metadata_resolution_query_valid(query, 0)) return result;
    result.mnemonic_candidates = buster_x86_metadata_lookup_mnemonic(query.mnemonic);
    if (!result.mnemonic_candidates.count)
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_UNKNOWN_MNEMONIC;
        return result;
    }

    bool metadata_invalid = false;
    bool coverage_excluded = false;
    bool saw_allowed_form = false;
    bool saw_execution_mode = false;
    bool execution_mode_excluded = false;
    bool saw_operand_count = false;
    bool saw_operand_shape = false;
    bool addressing_excluded = false;
    bool saw_addressing_match = false;
    bool saw_decorator_match = false;
    bool saw_feature_match = false;
    bool saw_unavailable_feature = false;
    bool last_id_valid = false;
    u32 last_id = 0;
    for (u32 position = 0; position < result.mnemonic_candidates.count; position += 1)
    {
        u32 form_id = 0;
        if (!buster_x86_metadata_candidate_at(result.mnemonic_candidates, position, &form_id) || (last_id_valid && form_id <= last_id))
        {
            metadata_invalid = true;
            continue;
        }
        last_id = form_id;
        last_id_valid = true;
        BusterX86GeneratedForm form = buster_x86_generated_form_at(form_id);
        if (!buster_x86_metadata_validate_form_record(&form, form_id, 0))
        {
            metadata_invalid = true;
            continue;
        }
        if (!buster_x86_metadata_form_coverage_allowed(form, query))
        {
            coverage_excluded = true;
            if (buster_x86_metadata_form_has_non64_mode(form)) execution_mode_excluded = true;
            continue;
        }
        saw_allowed_form = true;
        if (!buster_x86_metadata_form_execution_mode_matches(form, query))
        {
            execution_mode_excluded = true;
            continue;
        }
        saw_execution_mode = true;
        bool shape_matches = true;
        u32 selected_count = 0;
        bool has_memory = false;
        if (!buster_x86_metadata_form_operand_signatures_match(form, query, &shape_matches, &selected_count, &has_memory))
        {
            metadata_invalid = true;
            continue;
        }
        if (selected_count != query.operand_count) continue;
        saw_operand_count = true;
        if (!shape_matches) continue;
        saw_operand_shape = true;
        if (!buster_x86_metadata_form_address_size_matches(form, query.address_size, has_memory) ||
            !buster_x86_metadata_form_field_flags_match(form, query))
        {
            addressing_excluded = true;
            continue;
        }
        saw_addressing_match = true;
        u16 missing_decorators = query.decorator_flags & (u16)~form.decorator_flags;
        u16 missing_apx = query.apx_flags & (u16)~form.apx_flags;
        u16 missing_amx = query.amx_flags & (u16)~form.amx_flags;
        if ((query.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST) && !has_memory)
            missing_decorators |= BUSTER_X86_METADATA_DECORATOR_BROADCAST;
        if (missing_decorators || missing_apx || missing_amx)
        {
            result.unsupported_decorator_flags |= missing_decorators;
            result.unsupported_apx_flags |= missing_apx;
            result.unsupported_amx_flags |= missing_amx;
            continue;
        }
        saw_decorator_match = true;
        if (!buster_x86_metadata_form_feature_available(form, query.features))
        {
            if (!saw_unavailable_feature)
            {
                result.required_feature = buster_x86_metadata_string_unchecked(form.isa_set_offset);
                if (!result.required_feature.length) result.required_feature = buster_x86_metadata_string_unchecked(form.extension_offset);
            }
            saw_unavailable_feature = true;
            continue;
        }
        saw_feature_match = true;
        result.required_candidate_count += 1;
        if (result.candidate_count < form_id_capacity)
        {
            form_ids[result.candidate_count] = form_id;
            result.candidate_count += 1;
        }
    }
    if (metadata_invalid)
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_AMBIGUOUS_OR_UNSUPPORTED_METADATA;
    }
    else if (result.required_candidate_count && result.candidate_count != result.required_candidate_count)
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_OUTPUT_CAPACITY;
    }
    else if (result.required_candidate_count)
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_SUCCESS;
    }
    else if (!saw_execution_mode && execution_mode_excluded)
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_EXECUTION_MODE_MISMATCH;
    }
    else if (!saw_operand_count)
    {
        result.status = !saw_allowed_form && coverage_excluded ? BUSTER_X86_METADATA_RESOLVE_AMBIGUOUS_OR_UNSUPPORTED_METADATA
                                           : BUSTER_X86_METADATA_RESOLVE_WRONG_OPERAND_COUNT;
    }
    else if (!saw_operand_shape)
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_OPERAND_CLASS_WIDTH_MISMATCH;
    }
    else if (!saw_addressing_match && addressing_excluded)
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_ADDRESSING_FIELD_MISMATCH;
    }
    else if (!saw_decorator_match)
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_UNSUPPORTED_DECORATOR;
    }
    else if (!saw_feature_match && saw_unavailable_feature)
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE;
    }
    else
    {
        result.status = BUSTER_X86_METADATA_RESOLVE_AMBIGUOUS_OR_UNSUPPORTED_METADATA;
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
bool buster_x86_metadata_validate_patch(BusterX86MetadataValidationPatch patch, BusterX86MetadataValidationResult* result)
{
    if (patch.kind >= BUSTER_X86_METADATA_PATCH_COUNT)
    {
        return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_ENUM, patch.index, (u32)patch.kind);
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
            case BUSTER_X86_METADATA_PATCH_FORM_FIELD_FLAGS: form.field_flags = (u16)patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_DECORATOR_FLAGS: form.decorator_flags = (u16)patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_APX_FLAGS: form.apx_flags = (u16)patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_AMX_FLAGS: form.amx_flags = (u16)patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_MODE_FLAGS: form.mode_flags = (u16)patch.value; break;
            case BUSTER_X86_METADATA_PATCH_FORM_ENCODING_WIDTHS:
                form.displacement_width = (u8)patch.value;
                form.displacement_scale = (u8)(patch.value >> 8);
                form.immediate_width = (u8)(patch.value >> 16);
                form.immediate_signed = (u8)(patch.value >> 24);
                form.relocation_base = (u8)(patch.value >> 32);
                break;
            case BUSTER_X86_METADATA_PATCH_FORM_MANDATORY_PREFIX: form.mandatory_prefix = (u8)patch.value; break;
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
    if (patch.kind >= BUSTER_X86_METADATA_PATCH_OPERAND_RESERVED && patch.kind <= BUSTER_X86_METADATA_PATCH_OPERAND_ACCESS)
    {
        if (patch.index >= BUSTER_X86_GENERATED_OPERAND_COUNT)
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COUNT, patch.index, 0);
        }
        BusterX86GeneratedOperand operand = buster_x86_generated_operand_at(patch.index);
        if (patch.kind == BUSTER_X86_METADATA_PATCH_OPERAND_RESERVED)
        {
            operand.reserved[0] = (u8)patch.value;
            operand.reserved[1] = (u8)(patch.value >> 8);
            operand.reserved[2] = (u8)(patch.value >> 16);
        }
        else if (patch.kind == BUSTER_X86_METADATA_PATCH_OPERAND_KIND)
        {
            operand.kind = (u8)patch.value;
        }
        else if (patch.kind == BUSTER_X86_METADATA_PATCH_OPERAND_FIELD_SOURCE)
        {
            operand.field_source = (u8)patch.value;
        }
        else if (patch.kind == BUSTER_X86_METADATA_PATCH_OPERAND_ACCESS)
        {
            operand.access = (u8)patch.value;
        }
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

bool buster_x86_metadata_test_execution_mode_matches(u16 mode_flags, u8 coverage_class, bool include_not64,
                                                       u8 execution_mode)
{
    BusterX86GeneratedForm form = {0};
    form.mode_flags = mode_flags;
    form.coverage_class = coverage_class;
    return buster_x86_metadata_form_execution_mode_matches(
        form, (BusterX86MetadataResolveQuery){.execution_mode = execution_mode, .include_not64 = include_not64});
}
#endif
