#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/hash.h>
#include <buster/lib/os.h>

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

#if BUSTER_CPU_ARCH_X86_64 && defined(__AVX512F__) && defined(__AVX512BW__) && !defined(__BUSTER__) && !BUSTER_COMPILER_MSVC
#define BUSTER_METADATA_AVX512 1
#else
#define BUSTER_METADATA_AVX512 0
#endif

// The generated tables stay pointer-free so Buster itself can consume them:
// the string pool is a switch over 422 flat chunks and the numeric blobs add a
// base64 group decode on top of a 560-way switch, so every single byte costs a
// call and a dispatch. Reading a form therefore re-walks that machinery for
// each field, and finding a string's length re-walks it for each byte.
//
// Decode every table once into flat caches and index them directly afterwards.
// The caches live here rather than in the generated header, which keeps the
// generated source exactly as pointer-free and initialization-free as before.
// Filling is lazy, so a run that never touches x86 metadata pays nothing.
//
// Lazy also means unsynchronized: the fills below and the demand-filled caches
// further down are written once and read forever after through plain loads,
// which holds only while a writer cannot overlap a reader.
// buster_x86_metadata_prewarm() does all of it serially for a caller that is
// about to run a gang, and every fill site states
// BUSTER_CHECK_SERIAL_INITIALIZATION so a caller that forgot is told rather
// than left to race.
//
// One decode covers every table. Per-table laziness was measured and bought
// nothing: the lookup indexes yield string-pool offsets and form ids, so a
// consumer that reaches any table reaches most of them.
//
// Record validity is computed here too. The tables are immutable, so a record
// that validates once validates forever, and the public accessors would
// otherwise re-run `validate_form_record` on every single lookup -- the top
// cost of a Release test run once the raw decode was cached.
// `buster_x86_metadata_validate_table` still validates with diagnostics, and
// `buster_x86_metadata_validate_patch` still validates the mutated copy it
// builds, so neither loses coverage.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_form_record(const BusterX86GeneratedForm* form, u32 index,
                                                                 BusterX86MetadataValidationResult* result);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_validate_coverage_record(const BusterX86GeneratedCoverage* coverage, u32 index,
                                                                     BusterX86MetadataValidationResult* result);

BUSTER_GLOBAL_LOCAL char8 buster_x86_metadata_pool_bytes[BUSTER_X86_GENERATED_STRING_POOL_SIZE];
// Distance from each pool byte to its terminating NUL (UINT16_MAX when none
// follows), so asking a string's length is one read instead of a byte scan --
// consumers ask per record and per literal comparison, which rescanned the
// same strings constantly.  The checked-in pool's longest string is 276
// bytes.  A future oversized string saturates to the invalid sentinel during
// decode, making validation fail closed instead of truncating its length.
BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_pool_nul_distances[BUSTER_X86_GENERATED_STRING_POOL_SIZE];
BUSTER_GLOBAL_LOCAL BusterX86GeneratedForm buster_x86_metadata_form_records[BUSTER_X86_GENERATED_FORM_COUNT];
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_records_valid[BUSTER_X86_GENERATED_FORM_COUNT];
BUSTER_GLOBAL_LOCAL BusterX86GeneratedOperand buster_x86_metadata_operand_records[BUSTER_X86_GENERATED_OPERAND_COUNT];
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_coverage_records_valid[BUSTER_X86_GENERATED_COVERAGE_COUNT];
BUSTER_GLOBAL_LOCAL BusterX86GeneratedTextRange buster_x86_metadata_mnemonic_ranges[BUSTER_X86_GENERATED_MNEMONIC_RANGE_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_mnemonic_candidates[BUSTER_X86_GENERATED_MNEMONIC_CANDIDATE_COUNT];
BUSTER_GLOBAL_LOCAL BusterX86GeneratedTextRange buster_x86_metadata_iclass_ranges[BUSTER_X86_GENERATED_ICLASS_RANGE_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_iclass_candidates[BUSTER_X86_GENERATED_ICLASS_CANDIDATE_COUNT];
BUSTER_GLOBAL_LOCAL BusterX86GeneratedTextRange buster_x86_metadata_iform_ranges[BUSTER_X86_GENERATED_IFORM_RANGE_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_iform_candidates[BUSTER_X86_GENERATED_IFORM_CANDIDATE_COUNT];
BUSTER_GLOBAL_LOCAL BusterX86GeneratedHashRange buster_x86_metadata_form_hash_ranges[BUSTER_X86_GENERATED_FORM_HASH_RANGE_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_form_hash_candidates[BUSTER_X86_GENERATED_FORM_HASH_CANDIDATE_COUNT];
// Two flags, not one. `decoding` guards re-entry: validation below reads
// records and strings back through the self-initializing accessors, which call
// this function again, and those calls must return instead of decoding a
// second time. `decoded` is the one every accessor tests, and it is published
// only once the very last table is written -- so no reader, on this thread or
// any other, can ever see it set over half-filled state.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_tables_decoding;
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_tables_decoded;
// Demand-filled caches are plain-load/read-only after this completes. Keep a
// separate outer completion flag so repeated compiler/test prewarm calls do
// not walk every form and operand again; publish it only after the final cache
// write below.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_prewarmed;

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_decode_tables_once(void)
{
    if (buster_x86_metadata_tables_decoding)
    {
        return;
    }
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    buster_x86_metadata_tables_decoding = true;
    for (u64 index = 0; index < BUSTER_X86_GENERATED_STRING_POOL_SIZE; index += 1)
    {
        buster_x86_metadata_pool_bytes[index] = buster_x86_generated_string_byte(index);
    }
    u16 nul_distance = UINT16_MAX;
    for (u64 index = BUSTER_X86_GENERATED_STRING_POOL_SIZE; index; index -= 1)
    {
        u64 position = index - 1;
        if (buster_x86_metadata_pool_bytes[position] == 0)
        {
            nul_distance = 0;
        }
        else if (nul_distance != UINT16_MAX)
        {
            nul_distance = nul_distance == UINT16_MAX - 1 ? UINT16_MAX : (u16)(nul_distance + 1);
        }
        buster_x86_metadata_pool_nul_distances[position] = nul_distance;
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_OPERAND_COUNT; index += 1)
    {
        buster_x86_metadata_operand_records[index] = buster_x86_generated_operand_at(index);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_MNEMONIC_RANGE_COUNT; index += 1)
    {
        buster_x86_metadata_mnemonic_ranges[index] = buster_x86_generated_mnemonic_range_at(index);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_MNEMONIC_CANDIDATE_COUNT; index += 1)
    {
        buster_x86_metadata_mnemonic_candidates[index] = buster_x86_generated_mnemonic_candidate_at(index);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_ICLASS_RANGE_COUNT; index += 1)
    {
        buster_x86_metadata_iclass_ranges[index] = buster_x86_generated_iclass_range_at(index);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_ICLASS_CANDIDATE_COUNT; index += 1)
    {
        buster_x86_metadata_iclass_candidates[index] = buster_x86_generated_iclass_candidate_at(index);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_IFORM_RANGE_COUNT; index += 1)
    {
        buster_x86_metadata_iform_ranges[index] = buster_x86_generated_iform_range_at(index);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_IFORM_CANDIDATE_COUNT; index += 1)
    {
        buster_x86_metadata_iform_candidates[index] = buster_x86_generated_iform_candidate_at(index);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_FORM_HASH_RANGE_COUNT; index += 1)
    {
        buster_x86_metadata_form_hash_ranges[index] = buster_x86_generated_form_hash_range_at(index);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_FORM_HASH_CANDIDATE_COUNT; index += 1)
    {
        buster_x86_metadata_form_hash_candidates[index] = buster_x86_generated_form_hash_candidate_at(index);
    }
    // Records must be readable before they are validated: validation resolves
    // string offsets through the pool and operand ranges through the operand
    // table, both filled above.
    for (u32 index = 0; index < BUSTER_X86_GENERATED_FORM_COUNT; index += 1)
    {
        buster_x86_metadata_form_records[index] = buster_x86_generated_form_at(index);
    }
    // The re-entrant reads validation is about to make are already served by
    // the `decoding` flag set on entry, so the tables it needs are complete
    // without publishing `decoded` yet.
    for (u32 index = 0; index < BUSTER_X86_GENERATED_FORM_COUNT; index += 1)
    {
        buster_x86_metadata_form_records_valid[index] = buster_x86_metadata_validate_form_record(&buster_x86_metadata_form_records[index], index, 0);
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_COVERAGE_COUNT; index += 1)
    {
        BusterX86GeneratedCoverage coverage = buster_x86_generated_coverage_at(index);
        buster_x86_metadata_coverage_records_valid[index] = buster_x86_metadata_validate_coverage_record(&coverage, index, 0);
    }
    buster_x86_metadata_tables_decoded = true;
}

// Every accessor below self-initializes, and the string accessors run one call
// per byte, so the already-decoded test is on the hot path of the whole
// metadata module while the decode itself happens once. Keep the test inline
// and leave only the first call paying for an out-of-line frame.  This has to
// be BUSTER_ALWAYS_INLINE rather than BUSTER_INLINE: the latter expands to
// nothing without optimization, which left the guard out of line in exactly
// the sanitized Debug tree this exists for.
BUSTER_GLOBAL_LOCAL BUSTER_ALWAYS_INLINE void buster_x86_metadata_decode_tables(void)
{
    if (!buster_x86_metadata_tables_decoded)
    {
        buster_x86_metadata_decode_tables_once();
    }
}

BUSTER_GLOBAL_LOCAL char8 buster_x86_metadata_pool_byte(u64 logical)
{
    buster_x86_metadata_decode_tables();
    return logical < BUSTER_X86_GENERATED_STRING_POOL_SIZE ? buster_x86_metadata_pool_bytes[logical] : (char8)0;
}

// The decode flattens the chunked generated pool into one contiguous array, so
// a range of it can be handed out as a plain span.  Comparing or searching a
// pool string through buster_x86_metadata_pool_byte() costs a call per byte
// over a 1.7 MB pool; taking the span once and reading it directly does not.
// An out-of-range request yields an empty span, which every caller below
// treats as "no match" exactly as a zero byte did.
BUSTER_GLOBAL_LOCAL String8 buster_x86_metadata_pool_span(u32 offset, u32 length)
{
    buster_x86_metadata_decode_tables();
    if (offset >= BUSTER_X86_GENERATED_STRING_POOL_SIZE || length > BUSTER_X86_GENERATED_STRING_POOL_SIZE - offset)
    {
        return (String8){0};
    }
    return (String8){.pointer = buster_x86_metadata_pool_bytes + offset, .length = length};
}

String8 buster_x86_metadata_string_span(BusterX86MetadataString string)
{
    return buster_x86_metadata_pool_span(string.offset, string.length);
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedForm buster_x86_metadata_form_record(u32 index)
{
    buster_x86_metadata_decode_tables();
    BusterX86GeneratedForm empty = {0};
    return index < BUSTER_X86_GENERATED_FORM_COUNT ? buster_x86_metadata_form_records[index] : empty;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_record_valid(u32 index)
{
    buster_x86_metadata_decode_tables();
    return index < BUSTER_X86_GENERATED_FORM_COUNT && buster_x86_metadata_form_records_valid[index];
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedOperand buster_x86_metadata_operand_record(u32 index)
{
    buster_x86_metadata_decode_tables();
    BusterX86GeneratedOperand empty = {0};
    return index < BUSTER_X86_GENERATED_OPERAND_COUNT ? buster_x86_metadata_operand_records[index] : empty;
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedCoverage buster_x86_metadata_coverage_record(u32 index)
{
    buster_x86_metadata_decode_tables();
    BusterX86GeneratedCoverage empty = {0};
    return index < BUSTER_X86_GENERATED_COVERAGE_COUNT ? buster_x86_generated_coverage_at(index) : empty;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_coverage_record_valid(u32 index)
{
    buster_x86_metadata_decode_tables();
    return index < BUSTER_X86_GENERATED_COVERAGE_COUNT && buster_x86_metadata_coverage_records_valid[index];
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedTextRange buster_x86_metadata_mnemonic_range(u32 index)
{
    buster_x86_metadata_decode_tables();
    BusterX86GeneratedTextRange empty = {0};
    return index < BUSTER_X86_GENERATED_MNEMONIC_RANGE_COUNT ? buster_x86_metadata_mnemonic_ranges[index] : empty;
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_mnemonic_candidate(u32 index)
{
    buster_x86_metadata_decode_tables();
    return index < BUSTER_X86_GENERATED_MNEMONIC_CANDIDATE_COUNT ? buster_x86_metadata_mnemonic_candidates[index] : 0;
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedTextRange buster_x86_metadata_iclass_range(u32 index)
{
    buster_x86_metadata_decode_tables();
    BusterX86GeneratedTextRange empty = {0};
    return index < BUSTER_X86_GENERATED_ICLASS_RANGE_COUNT ? buster_x86_metadata_iclass_ranges[index] : empty;
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_iclass_candidate(u32 index)
{
    buster_x86_metadata_decode_tables();
    return index < BUSTER_X86_GENERATED_ICLASS_CANDIDATE_COUNT ? buster_x86_metadata_iclass_candidates[index] : 0;
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedTextRange buster_x86_metadata_iform_range(u32 index)
{
    buster_x86_metadata_decode_tables();
    BusterX86GeneratedTextRange empty = {0};
    return index < BUSTER_X86_GENERATED_IFORM_RANGE_COUNT ? buster_x86_metadata_iform_ranges[index] : empty;
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_iform_candidate(u32 index)
{
    buster_x86_metadata_decode_tables();
    return index < BUSTER_X86_GENERATED_IFORM_CANDIDATE_COUNT ? buster_x86_metadata_iform_candidates[index] : 0;
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedHashRange buster_x86_metadata_form_hash_range(u32 index)
{
    buster_x86_metadata_decode_tables();
    BusterX86GeneratedHashRange empty = {0};
    return index < BUSTER_X86_GENERATED_FORM_HASH_RANGE_COUNT ? buster_x86_metadata_form_hash_ranges[index] : empty;
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_form_hash_candidate(u32 index)
{
    buster_x86_metadata_decode_tables();
    return index < BUSTER_X86_GENERATED_FORM_HASH_CANDIDATE_COUNT ? buster_x86_metadata_form_hash_candidates[index] : 0;
}

BUSTER_GLOBAL_LOCAL BusterX86GeneratedHashRange buster_x86_metadata_coverage_hash_range(u32 index)
{
    buster_x86_metadata_decode_tables();
    BusterX86GeneratedHashRange empty = {0};
    return index < BUSTER_X86_GENERATED_COVERAGE_HASH_RANGE_COUNT ? buster_x86_generated_coverage_hash_range_at(index) : empty;
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_coverage_hash_candidate(u32 index)
{
    buster_x86_metadata_decode_tables();
    return index < BUSTER_X86_GENERATED_COVERAGE_HASH_CANDIDATE_COUNT ? buster_x86_generated_coverage_hash_candidate_at(index) : 0;
}

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
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS == BUSTER_X86_GENERATED_COVERAGE_DECODE_ALIAS);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_COVERAGE_COUNT == BUSTER_X86_GENERATED_COVERAGE_DECODE_ALIAS + 1);
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
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA == BUSTER_X86_GENERATED_TEST_DECODE_ALIAS_SCHEMA);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_TEST_CLASS_COUNT == BUSTER_X86_GENERATED_TEST_DECODE_ALIAS_SCHEMA + 1);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_NONE == BUSTER_X86_GENERATED_REASON_NONE);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_MODE_NOT64 == BUSTER_X86_GENERATED_REASON_MODE_NOT64);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_CPL0 == BUSTER_X86_GENERATED_REASON_CPL0);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_UNKNOWN_PATTERN_TOKEN == BUSTER_X86_GENERATED_REASON_UNKNOWN_PATTERN_TOKEN);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_UNKNOWN_OPERAND_TOKEN == BUSTER_X86_GENERATED_REASON_UNKNOWN_OPERAND_TOKEN);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_DECODE_ALIAS == BUSTER_X86_GENERATED_REASON_DECODE_ALIAS);
BUSTER_CT_CHECK(BUSTER_X86_METADATA_REASON_COUNT == BUSTER_X86_GENERATED_REASON_DECODE_ALIAS + 1);
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
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_string_input_equal(u32 offset, String8 input);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_input_string_equal(String8 left, String8 right);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_pool_string_has_token(u32 offset, String8 token);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_is_fixed_not16_nop(BusterX86MetadataForm form);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_generated_form_is_fixed_not16_nop(BusterX86GeneratedForm form);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_prefetchit_address_valid(BusterX86MetadataPhysicalQuery query);

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
    buster_x86_metadata_decode_tables();
    if (offset >= BUSTER_X86_GENERATED_STRING_POOL_SIZE)
    {
        return false;
    }
    u16 distance = buster_x86_metadata_pool_nul_distances[offset];
    if (distance == UINT16_MAX)
    {
        return false;
    }
    if (length)
    {
        *length = distance;
    }
    return true;
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
    String8 first = buster_x86_metadata_pool_span(first_offset, first_length);
    String8 second = buster_x86_metadata_pool_span(second_offset, second_length);
    if (first.length != first_length || second.length != second_length)
    {
        return false;
    }
    for (u32 index = 0; index < first_length; index += 1)
    {
        if (first.pointer[index] != second.pointer[index])
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
    String8 span = buster_x86_metadata_pool_span(offset, length);
    if (span.length != length)
    {
        *valid = false;
        return 0;
    }
    static const u8 empty[] = {0};
    u8 const* pointer = length ? (u8 const*)span.pointer : empty;
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

typedef struct BusterX86MetadataPatternSemantics BusterX86MetadataPatternSemantics;
struct BusterX86MetadataPatternSemantics
{
    // Generated form validation caps fixed bytes at sixteen, while this
    // parsed projection is deliberately the encodable x86 instruction shape:
    // the checked-in snapshot needs at most eight opcode bytes, one trailing
    // selector (3DNow), and two immediates.  Parser overflow marks the pattern
    // unsupported, so future wider schemas fail closed instead of truncating.
    u8 opcode[8];
    u8 opcode_count;
    u8 trailing[1];
    u8 trailing_count;
    u8 mandatory_prefix;
    u16 vector_length;
    u8 mod_kind;
    u8 reg_fixed;
    u8 has_reg_range;
    u8 reg_min;
    u8 reg_max;
    u8 rm_fixed;
    u8 srm_fixed;
    u8 srm_not_equal;
    u8 srm_shift;
    u8 srm_base;
    u8 prefix_kind;
    u8 immediate_width;
    u8 immediate_signed;
    u8 immediate_variable;
    u8 immediate_widths[2];
    u8 immediate_signeds[2];
    u8 immediate_variables[2];
    u8 selector_immediate;
    u8 has_implicit_one;
    u8 has_ignore_66;
    u8 relative_width;
    u8 relative_variable;
    u8 displacement_width;
    u8 w;
    u8 has_w;
    u8 map;
    u8 immediate_count;
    u8 relative_count;
    u8 displacement_count;
    u8 has_modrm;
    u8 explicit_modrm;
    u8 has_sib;
    u8 has_memory;
    u8 has_register;
    u8 has_dynamic_opcode;
    u8 has_srm_register;
    u8 has_unsupported_token;
    u8 has_prefix_control;
    u8 has_branch_hint_control;
    u8 has_force64_control;
    // MODE16/MODE32 are operand-size defaults for this legacy residual
    // cohort.  Keep the source selector separate from the generated mode
    // flags: the latter gates candidate visibility, while this value drives
    // the byte path's implicit 66 decision.
    u8 mode_control;
    u8 operand_size_control;
    u8 has_cet_control;
    u8 cet_value;
    u8 has_cet_no_track;
    u8 has_encdelete_control;
    u8 has_address_control;
    u8 has_decorator_control;
    u8 has_apx_control;
    u8 has_amx_control;
    u8 has_vsib_control;
    u8 has_prefix_kind;
    u8 explicit_evex_selector;
    u8 has_nd;
    u8 nd_value;
    u8 has_nf;
    u8 nf_value;
    u8 has_bcrc;
    u8 bcrc_value;
    u8 has_ubit;
    u8 ubit_value;
    // XED uses these typed 0/1 selectors for opcode aliases whose byte
    // spelling is shared by a real instruction and a legacy NOP.  Keep the
    // value with the parsed form so a token cannot accidentally be treated as
    // an interchangeable presence marker.
    u8 has_lzcnt_control;
    u8 lzcnt_control_value;
    u8 has_tzcnt_control;
    u8 tzcnt_control_value;
    u8 has_cldemote_control;
    u8 cldemote_control_value;
    u8 has_ibhf_control;
    u8 ibhf_control_value;
    u8 has_prefetchrst_control;
    u8 prefetchrst_control_value;
    u8 has_prefetchit_control;
    u8 prefetchit_control_value;
    u8 has_explicit_vector_length;
    u8 has_scc;
    u8 scc_value;
    u8 has_evex_r4;
    u8 evex_r4_value;
    u8 force_sib;
    u8 lock_control;
    u8 rep_control;
    u8 rep_not_f3;
    u8 has_modep5;
    u8 modep5_value;
    u8 has_rep_selector;
    u8 rep_selector_value;
    // MPXMODE is a source-pattern selector rather than an encoded prefix.
    // Keep its value typed so only the two architectural spellings are
    // admitted; the form check below ties each value to the narrow MPX/BASE
    // rows that carry it instead of treating every future token as opaque.
    u8 has_mpx_mode;
    u8 mpx_mode_value;
    u8 has_segment_override;
    u8 segment_override_index;
    // A small set of XED pattern controls describes the prefix/address
    // topology rather than introducing another source operand.  Keep these
    // constraints typed so the encoder can enforce them for every physical
    // query (including direct metadata callers).
    u8 has_remove_segment;
    u8 rex_b_control;
    u8 rex_b4_control;
    u8 has_p4_control;
    u8 p4_value;
    u8 immune66;
    u8 immune66_loop64;
    u8 immune_rexw;
    u8 df64;
    u8 required_address_size;
    u8 forbid_address_override;
    u8 forbid_operand_size_override;
    u8 forbid_mandatory_prefix;
    u8 not16;
    u8 no_rex2;
    u8 no_rexr_prefix;
    u8 no_vector_source;
    u8 apx_fixed_width_no_w_isa;
    u8 no_scc;
    u8 short_ud0;
    u8 has_tuple_control;
    u8 tuple_control_kind;
    u8 has_tile_control;
    u16 vsib_vector_length;
    u8 has_element_size_control;
    u16 element_size_bits;
    u8 has_sae_control;
    u8 has_rounding_control;
    u8 mask_control;
    u8 zeroing_control;
    u16 rounding_length;
    u8 unresolved_blocker;
};
BUSTER_CT_CHECK(sizeof(BusterX86MetadataPatternSemantics) == 144);

enum
{
    BUSTER_X86_METADATA_REX_CONTROL_NONE,
    BUSTER_X86_METADATA_REX_CONTROL_FORBID,
    BUSTER_X86_METADATA_REX_CONTROL_REQUIRE,
};

enum
{
    // XED publishes NELEM_MEM128 forms with the ordinary FULL tuple kind, but
    // their memory width and compressed-displacement scale are fixed at 128
    // bits.  Keep that distinction in the parsed pattern only; the generated
    // and public tuple enums remain byte-for-byte compatible with XED.
    BUSTER_X86_METADATA_PATTERN_TUPLE_MEM128 = BUSTER_X86_METADATA_TUPLE_COUNT,
};

typedef struct BusterX86MetadataPhysicalBinding BusterX86MetadataPhysicalBinding;
struct BusterX86MetadataPhysicalBinding
{
    BusterX86MetadataOperand metadata;
    BusterX86MetadataPhysicalOperand physical;
    u8 has_physical;
    u8 actual_index;
    // Immutable plan facts are copied into the short-lived binding without
    // re-reading atom strings.  The valid bit distinguishes prepared facts
    // from the generic path, which still derives them from the source atom.
    u8 prepared_flags;
    u8 prepared_flags_valid;
    // The prepared exact plan supplies this once from the normalized operand
    // schema.  Keeping it on the binding lets field lookups below avoid
    // rescanning the source atom for every REG/RM/VVVV/IMM query.
    u8 effective_field_source;
    u8 effective_field_source_valid;
};

enum
{
    BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY = 16,
    // Exact plans are keyed by durable machine forms and the serial machine
    // prewarm may include direct rows, family variants, and sequence steps.
    // Keep ample room for that sparse set (which includes sequence-step forms
    // in addition to direct/family descriptors) so a valid public prepare
    // request cannot fail merely because this storage crossed an arbitrary
    // implementation limit.  The table is serially populated and immutable
    // after prewarm; 1024 entries remain a bounded, sub-megabyte projection.
    BUSTER_X86_METADATA_EXACT_PLAN_CAPACITY = 1024,
};

typedef struct BusterX86MetadataExactPlanRecord BusterX86MetadataExactPlanRecord;
struct BusterX86MetadataExactPlanRecord
{
    BusterX86MetadataExactPlan identity;
    BusterX86MetadataForm const* form;
    BusterX86MetadataPatternSemantics const* pattern;
    // Operand views are already normalized into one immutable, contiguous
    // table during metadata prewarm.  Borrow the owning form's range instead
    // of copying sixteen 24-byte slots into every sparse exact plan.  Besides
    // removing duplicate storage, this pulls the machine-fast projection six
    // cache lines closer to the record head on 64-bit hosts.
    BusterX86MetadataOperand const* operands;
    u8 effective_field_sources[BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY];
    u8 operand_flags[BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY];
    u16 operand_count;
    u8 pattern_control_blocker;
    u8 moffs_form;
    u8 maskmov_form;
    u8 requires_dfv;
    u8 loop_form;
    u8 jecxz_form;
    u8 canonical_hidden_segment_override;
    u8 canonical_notrack;
    u8 dataxfer_category;
    // Exact plans whose visible operand schema is already in physical order
    // can bind without revisiting the generic hidden-operand/VSIB/moffs
    // dispatch.  The ordinary matcher remains the authority for all other
    // rows; this bit only selects an equivalent prepared-plan walk.
    u8 exact_bind_simple;
    u8 exact_bind_reg;
    u8 exact_bind_rm;
    u8 exact_bind_vvvv;
    u8 exact_bind_mask;
    u8 exact_bind_memory;
    u8 exact_bind_immediate;
    u8 exact_bind_relative;
    // A deliberately small machine-facing projection.  The generic exact
    // transform remains the authority for every form that does not fit this
    // shape.  For the common scalar legacy/REX rows, prewarm records the
    // operand-to-field map and immutable opcode topology so the machine
    // bridge can validate and write directly without rebuilding bindings or
    // allocating the 64-byte generic scratch object.
    u8 machine_fast_kind;
    u8 machine_fast_binding_count;
    u8 machine_fast_reg_binding;
    u8 machine_fast_rm_binding;
    u8 machine_fast_memory_binding;
    u8 machine_fast_immediate_binding;
    u8 machine_fast_register_count;
    u8 machine_fast_register_bindings[BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY];
    u8 machine_fast_data_width_count;
    u8 machine_fast_data_width_bindings[BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY];
    u8 machine_fast_binding_kind[BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY];
    u16 machine_fast_binding_width_flags[BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY];
    u8 machine_fast_binding_metadata[BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY];
    u8 machine_fast_binding_source[BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY];
    // A small immutable byte template covers exact rows whose only dynamic
    // field is a fixed-width relative displacement (or no field at all).
    // The template is produced by the metadata transform during serial
    // preparation; workers only copy it and, for relative rows, patch the
    // neutral displacement value.  This keeps metadata as the sole byte
    // authority while avoiding the full checked transform for these common
    // zero/branch shapes.
    u8 machine_fast_template[15];
    u8 machine_fast_template_count;
    u8 machine_fast_template_relative_width;
    u8 machine_fast_template_relative_offset;
    u8 machine_fast_template_has_relative;
    // The serially published identity already fixes the token integrity
    // byte for the ordinary machine policy.  Cache that pure value beside
    // the immutable plan so workers do not re-read the form identity and
    // re-run the shift/xor chain on every token validation.  The APX policy
    // bit is the only allowed variation and is folded in at validation time.
    u8 machine_exact_integrity;
    bool ready;
};
BUSTER_CT_CHECK(sizeof(BusterX86MetadataExactPlanRecord) <= 256);

enum
{
    BUSTER_X86_METADATA_MACHINE_FAST_NONE,
    BUSTER_X86_METADATA_MACHINE_FAST_SCALAR,
    BUSTER_X86_METADATA_MACHINE_FAST_TEMPLATE,
};

enum
{
    BUSTER_X86_METADATA_MACHINE_FAST_BINDING_NONE,
    BUSTER_X86_METADATA_MACHINE_FAST_BINDING_REGISTER,
    BUSTER_X86_METADATA_MACHINE_FAST_BINDING_MEMORY,
    BUSTER_X86_METADATA_MACHINE_FAST_BINDING_ADDRESS_GENERATOR,
    BUSTER_X86_METADATA_MACHINE_FAST_BINDING_IMMEDIATE,
};

enum
{
    // Per-operand facts are immutable once an exact plan is published.  The
    // separate prepared_flags_valid bit on a binding keeps a zero-valued fact
    // distinct from an unprepared generic binding.
    BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MOFFS_SUPPLEMENTAL = 1u << 0,
    BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MASKMOV_SUPPLEMENTAL = 1u << 1,
    BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_WRITEMASK = 1u << 2,
    BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_X87 = 1u << 3,
    BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_SELECTOR = 1u << 4,
    BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MOFFS_FIXED_ACCUMULATOR = 1u << 5,
    BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_FIXED_BSR0 = 1u << 6,
};

enum
{
    BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_POLICY_VALID = 1u << 0,
    BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_ALLOWS_APX = 1u << 1,
    BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_FLAGS_ALL = BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_POLICY_VALID |
                                                        BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_ALLOWS_APX,
};

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_machine_exact_token_integrity(u16 slot_plus_one, u8 policy_flags,
                                                                           u32 form_id, u64 stable_hash)
{
    return (u8)(UINT8_C(0xa5) ^ (u8)slot_plus_one ^ (u8)(slot_plus_one >> 8) ^ policy_flags ^
                (u8)form_id ^ (u8)(form_id >> 8) ^ (u8)stable_hash ^ (u8)(stable_hash >> 32));
}

typedef struct BusterX86MetadataEncodeScratch BusterX86MetadataEncodeScratch;
struct BusterX86MetadataEncodeScratch
{
    // Keep enough internal room to finish an otherwise valid scratch form
    // before applying the architectural 15-byte limit.  A scratch overflow
    // must not disguise an instruction-length diagnostic.
    u8 bytes[64];
    BusterX86MetadataRelocation relocations[BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY];
    u32 byte_count;
    u32 relocation_count;
    // How many little-endian value fields the transform wrote.  Every
    // displacement, immediate, relative and absolute field goes through
    // buster_x86_metadata_emit_write_le and nothing else does, so a zero here
    // means the byte string depends on no operand value - only on the form and
    // the operand shape.
    u32 value_field_count;
};

enum
{
    BUSTER_X86_METADATA_PATTERN_MOD_ANY = 0xff,
    BUSTER_X86_METADATA_PATTERN_MOD_MEMORY = 0xfe,
    BUSTER_X86_METADATA_PATTERN_MOD_REGISTER = 3,
    BUSTER_X86_METADATA_PATTERN_FIXED_ANY = 0xff,
    BUSTER_X86_METADATA_PATTERN_MODE_NONE = 0,
    BUSTER_X86_METADATA_PATTERN_MODE_16 = 16,
    BUSTER_X86_METADATA_PATTERN_MODE_32 = 32,
    BUSTER_X86_METADATA_PATTERN_MODE_64 = 64,
    BUSTER_X86_METADATA_PATTERN_MODE_NOT64 = 65,
    BUSTER_X86_METADATA_PATTERN_OPERAND_SIZE_NONE = 0,
    BUSTER_X86_METADATA_PATTERN_OPERAND_SIZE_66 = 1,
    BUSTER_X86_METADATA_PATTERN_OPERAND_SIZE_NO66 = 2,
};

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_is_space(char8 character);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_query_string_valid(String8 string);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_resolution_query_valid(BusterX86MetadataResolveQuery query, u32* error_detail);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_feature_available(BusterX86GeneratedForm form,
                                                                       BusterX86MetadataFeatureInput features);
BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_form_declared_execution_mode(BusterX86GeneratedForm form, bool legacy_mode_cohort);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_execution_mode_matches(BusterX86GeneratedForm form,
                                                                           BusterX86MetadataResolveQuery query);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_coverage_allowed(BusterX86GeneratedForm form,
                                                                     BusterX86MetadataResolveQuery query);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_feature_input_allows_apx(BusterX86MetadataFeatureInput input);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_canonical_segment_override(BusterX86MetadataForm form,
                                                                                BusterX86MetadataPatternSemantics pattern);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_moffs(BusterX86MetadataForm form,
                                                            BusterX86MetadataPatternSemantics pattern);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_moffs_supplemental(BusterX86MetadataForm form,
                                                                         BusterX86MetadataPatternSemantics pattern,
                                                                         BusterX86MetadataOperand metadata);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_maskmov(BusterX86MetadataForm form,
                                                              BusterX86MetadataPatternSemantics pattern);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_maskmov_supplemental(BusterX86MetadataForm form,
                                                                           BusterX86MetadataPatternSemantics pattern,
                                                                           BusterX86MetadataOperand metadata);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_moffs_source_accumulator(BusterX86MetadataPhysicalQuery query,
                                                                            BusterX86MetadataForm form,
                                                                            BusterX86MetadataPatternSemantics pattern);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_canonical_cet_no_track(BusterX86MetadataForm form,
                                                                           BusterX86MetadataPatternSemantics pattern);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_string_has(BusterX86MetadataString string, String8 needle);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_canonical_df64(BusterX86MetadataForm form,
                                                                  BusterX86MetadataPatternSemantics pattern);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_mode66_residual(BusterX86MetadataForm form,
                                                                    BusterX86MetadataPatternSemantics pattern);

// XED keeps the ordinary CMPXCHG/CMPXCHG16B memory rows as `nolock_prefix`
// aliases beside their lock-spelling rows.  The aliases are useful durable
// identities for machine recipes, but a LOCK byte is architectural only for
// these three memory forms.  Keep the exception explicit and shape-checked so
// a generic memory row can never gain an accidental atomic prefix.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_lock_alias_allowed(BusterX86MetadataForm form,
                                                                      BusterX86MetadataPatternSemantics pattern,
                                                                      bool has_memory)
{
    if (!has_memory || pattern.lock_control != 2 || pattern.mod_kind != BUSTER_X86_METADATA_PATTERN_MOD_MEMORY ||
        !pattern.has_modrm || form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED)
        return false;
    return form.id == 9530 || form.id == 10278 || form.id == 10281;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_reg_matches(BusterX86MetadataPatternSemantics pattern, u8 value)
{
    if (pattern.reg_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY) return value == pattern.reg_fixed;
    if (pattern.has_reg_range) return value >= pattern.reg_min && value <= pattern.reg_max;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_reg_is_unconstrained(BusterX86MetadataPatternSemantics pattern)
{
    return pattern.reg_fixed == BUSTER_X86_METADATA_PATTERN_FIXED_ANY && !pattern.has_reg_range;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_token_equal(char8 const* token, u32 length, String8 expected)
{
    if (length != expected.length) return false;
    for (u32 index = 0; index < length; index += 1)
    {
        if (token[index] != expected.pointer[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_token_starts_with(char8 const* token, u32 length, String8 expected)
{
    if (length < expected.length) return false;
    for (u32 index = 0; index < expected.length; index += 1)
    {
        if (token[index] != expected.pointer[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_parse_number(char8 const* token, u32 length, u32* value, u32* bit_count)
{
    if (length < 3 || token[0] != '0' || (token[1] != 'x' && token[1] != 'X' && token[1] != 'b' && token[1] != 'B'))
        return false;
    u32 base = token[1] == 'x' || token[1] == 'X' ? 16 : 2;
    u32 parsed = 0;
    u32 bits = 0;
    for (u32 index = 2; index < length; index += 1)
    {
        char8 character = token[index];
        if (character == '_') continue;
        u32 digit = UINT32_MAX;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10;
        if (digit >= base || parsed > (UINT32_MAX - digit) / base) return false;
        parsed = parsed * base + digit;
        bits += base == 2 ? 1 : 4;
    }
    if (!bits || (base == 2 && bits > 8) || (base == 16 && bits > 8)) return false;
    *value = parsed;
    *bit_count = bits;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_parse_bracket_number(char8 const* token, u32 length, u32* value)
{
    u32 open = 0;
    while (open < length && token[open] != '[') open += 1;
    if (open == length || length < open + 3 || token[length - 1] != ']') return false;
    u32 bits = 0;
    return buster_x86_metadata_emit_parse_number(token + open + 1, length - open - 2, value, &bits);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_parse_bracket_range(char8 const* token, u32 length, u32* minimum,
                                                                       u32* maximum)
{
    u32 open = 0;
    while (open < length && token[open] != '[') open += 1;
    if (open == length || length < open + 5 || token[length - 1] != ']') return false;
    u32 dash = open + 1;
    while (dash + 1 < length - 1 && token[dash] != '-') dash += 1;
    if (dash == open + 1 || dash + 1 >= length - 1) return false;
    u32 low = 0;
    u32 high = 0;
    for (u32 index = open + 1; index < dash; index += 1)
    {
        char8 character = token[index];
        if (character < '0' || character > '9' || low > (UINT32_MAX - (u32)(character - '0')) / 10u) return false;
        low = low * 10u + (u32)(character - '0');
    }
    for (u32 index = dash + 1; index < length - 1; index += 1)
    {
        char8 character = token[index];
        if (character < '0' || character > '9' || high > (UINT32_MAX - (u32)(character - '0')) / 10u) return false;
        high = high * 10u + (u32)(character - '0');
    }
    if (low > high || high > 7) return false;
    *minimum = low;
    *maximum = high;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_token_has(char8 const* token, u32 length, char8 needle)
{
    for (u32 index = 0; index < length; index += 1)
    {
        if (token[index] == needle) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_emit_mark_unresolved(BusterX86MetadataPatternSemantics* pattern, u8 blocker)
{
    if (!pattern->unresolved_blocker) pattern->unresolved_blocker = blocker;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_emit_set_mandatory_prefix(BusterX86MetadataPatternSemantics* pattern, u8 prefix)
{
    if (pattern->mandatory_prefix && pattern->mandatory_prefix != prefix)
        buster_x86_metadata_emit_mark_unresolved(pattern, BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS);
    pattern->mandatory_prefix = prefix;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_emit_set_mode_control(BusterX86MetadataPatternSemantics* pattern, u8 mode)
{
    if (pattern->mode_control && pattern->mode_control != mode)
        buster_x86_metadata_emit_mark_unresolved(pattern, BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS);
    pattern->mode_control = mode;
    pattern->has_prefix_control = 1;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_emit_set_operand_size_control(BusterX86MetadataPatternSemantics* pattern, u8 control)
{
    if (pattern->operand_size_control && pattern->operand_size_control != control)
        buster_x86_metadata_emit_mark_unresolved(pattern, BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS);
    pattern->operand_size_control = control;
    pattern->has_prefix_control = 1;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_emit_set_lock_control(BusterX86MetadataPatternSemantics* pattern, u8 control)
{
    if (pattern->lock_control && pattern->lock_control != control)
        buster_x86_metadata_emit_mark_unresolved(pattern, BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS);
    pattern->lock_control = control;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_emit_set_rep_control(BusterX86MetadataPatternSemantics* pattern, u8 control)
{
    if (pattern->rep_control && pattern->rep_control != control)
        buster_x86_metadata_emit_mark_unresolved(pattern, BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS);
    pattern->rep_control = control;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_emit_set_vector_length(BusterX86MetadataPatternSemantics* pattern, u16 length)
{
    if (pattern->rounding_length && pattern->rounding_length != length)
        buster_x86_metadata_emit_mark_unresolved(pattern, BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS);
    pattern->vector_length = length;
    pattern->has_explicit_vector_length = 1;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_emit_set_fixed_rounding_length(BusterX86MetadataPatternSemantics* pattern, u16 length)
{
    if ((pattern->rounding_length && pattern->rounding_length != length) ||
        (pattern->has_explicit_vector_length && pattern->vector_length != length))
        buster_x86_metadata_emit_mark_unresolved(pattern, BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS);
    pattern->rounding_length = length;
    pattern->vector_length = length;
}

// Pattern semantics per form id, filled the first time the owning normalized
// form is parsed.  The parse seeds itself from these form fields as well as
// the pattern text, so a cached entry is served only when every one of them
// matches the normalized form the entry was parsed from; a fabricated or
// edited form falls back to a fresh parse.
BUSTER_GLOBAL_LOCAL BusterX86MetadataPatternSemantics buster_x86_metadata_pattern_semantics_cache[BUSTER_X86_GENERATED_FORM_COUNT];
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_pattern_semantics_cached[BUSTER_X86_GENERATED_FORM_COUNT];
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_pattern_semantics_results[BUSTER_X86_GENERATED_FORM_COUNT];
BUSTER_GLOBAL_LOCAL BusterX86MetadataForm buster_x86_metadata_normalized_forms[BUSTER_X86_GENERATED_FORM_COUNT];
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_normalized_forms_cached[BUSTER_X86_GENERATED_FORM_COUNT];
// Exact plans are built only on the serial prewarm path and remain immutable
// while worker lanes emit.  The operand copies are keyed by form rather than
// generated operand id because immediate/relative physical widths inherit
// form-level encoding fields.
BUSTER_GLOBAL_LOCAL BusterX86MetadataExactPlanRecord
    buster_x86_metadata_exact_plan_records[BUSTER_X86_METADATA_EXACT_PLAN_CAPACITY];
BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_exact_plan_slots[BUSTER_X86_GENERATED_FORM_COUNT];
BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_exact_plan_count;

// Derived per-form facts for callers that hold no prepared exact plan.  An
// exact plan carries these already, but only 1024 of them exist and they are
// keyed by a durable form key; the canonical backend selects forms at emission
// time and so used to recompute every one of these on every instruction, each
// a string comparison against an iclass or category spelling.  They are pure
// functions of the normalized form and its parsed pattern, so the serial
// prewarm that already walks all forms fills them once here.
//
// These mirror the plan-less spellings exactly, not the prepared plan's: the
// field sources use the operand-only helper the generic path calls, because
// the pattern-aware variant an exact plan stores is a different function and
// substituting it would change emitted bytes.
enum
{
    BUSTER_X86_METADATA_FORM_FACT_MOFFS = 1u << 0,
    BUSTER_X86_METADATA_FORM_FACT_MASKMOV = 1u << 1,
    BUSTER_X86_METADATA_FORM_FACT_HIDDEN_SEGMENT_OVERRIDE = 1u << 2,
    BUSTER_X86_METADATA_FORM_FACT_NOTRACK = 1u << 3,
    BUSTER_X86_METADATA_FORM_FACT_LOOP = 1u << 4,
    BUSTER_X86_METADATA_FORM_FACT_JECXZ = 1u << 5,
    BUSTER_X86_METADATA_FORM_FACT_REQUIRES_DFV = 1u << 6,
    BUSTER_X86_METADATA_FORM_FACT_DATAXFER = 1u << 7,
};
enum
{
    // The row exposes every metadata operand, in query order, with no
    // moffs/maskmov/VSIB dispatch and no per-operand special handling.  This
    // is the same shape an exact plan records as `exact_bind_simple`, and it
    // lets binding skip the generic hidden-operand walk entirely.
    BUSTER_X86_METADATA_FORM_FACT2_BIND_SIMPLE = 1u << 0,
};
typedef struct BusterX86MetadataFormFacts BusterX86MetadataFormFacts;
struct BusterX86MetadataFormFacts
{
    u8 flags;
    u8 shape_flags;
    u8 pattern_control_blocker;
    u8 operand_count;
    // Which binding fills each encoding field, for a BIND_SIMPLE row.  These
    // stand in for the five linear binding scans the generic path runs, and
    // are UINT8_MAX when the row has no operand for that field.
    u8 bind_reg;
    u8 bind_rm;
    u8 bind_vvvv;
    u8 bind_mask;
    u8 bind_memory;
    u8 bind_immediate;
    u8 bind_relative;
};
BUSTER_CT_CHECK(sizeof(BusterX86MetadataFormFacts) == 11);
typedef struct BusterX86MetadataFormOperandFacts BusterX86MetadataFormOperandFacts;
struct BusterX86MetadataFormOperandFacts
{
    // The plan-less binding loop first rewrites an operand's field source to
    // the pattern-aware value and only then asks for the effective source, so
    // both values are recorded and the second is derived from the first.
    // Generated operand ranges partition this table; reserving sixteen slots
    // in every form row wasted most of that storage (the snapshot maximum is
    // ten).  Exact plans keep their own inline projection for their hot path.
    u8 pattern_field_source;
    u8 effective_field_source;
};
BUSTER_CT_CHECK(sizeof(BusterX86MetadataFormOperandFacts) == 2);
BUSTER_GLOBAL_LOCAL BusterX86MetadataFormFacts buster_x86_metadata_form_facts[BUSTER_X86_GENERATED_FORM_COUNT];
BUSTER_GLOBAL_LOCAL BusterX86MetadataFormOperandFacts
    buster_x86_metadata_form_operand_facts[BUSTER_X86_GENERATED_OPERAND_COUNT];
// Published last by the prewarm, after every entry above is written.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_facts_ready;

BUSTER_GLOBAL_LOCAL BUSTER_ALWAYS_INLINE BusterX86MetadataFormOperandFacts*
buster_x86_metadata_form_operand_facts_for(BusterX86MetadataForm form, u32 operand_index)
{
    if (operand_index >= form.operand_count || form.operand_first > BUSTER_X86_GENERATED_OPERAND_COUNT ||
        operand_index >= BUSTER_X86_GENERATED_OPERAND_COUNT - form.operand_first)
        return 0;
    return &buster_x86_metadata_form_operand_facts[form.operand_first + operand_index];
}

// The flat operand-facts table is sound only while generated form ranges are
// a partition: the facts depend on both the operand record and its owning
// form.  Check that snapshot invariant before publishing the cache.  A future
// malformed or differently-shaped table simply retains the generic path.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_operand_ranges_partition(void)
{
    u32 expected_first = 0;
    for (u32 form_id = 0; form_id < BUSTER_X86_GENERATED_FORM_COUNT; form_id += 1)
    {
        BusterX86GeneratedForm form = buster_x86_metadata_form_record(form_id);
        if (!buster_x86_metadata_form_record_valid(form_id) || form.operand_first != expected_first ||
            form.operand_count > BUSTER_X86_GENERATED_OPERAND_COUNT - expected_first)
            return false;
        expected_first += form.operand_count;
    }
    return expected_first == BUSTER_X86_GENERATED_OPERAND_COUNT;
}

// Normalized operand views, one per generated operand record.  Building a view
// resolves the imported width token by comparing it against the operand
// vocabulary, so an uncached buster_x86_metadata_operand() costs several
// string comparisons; binding asks for every operand of a row on every
// emitted instruction.  The owning form is recorded beside each entry because
// the view is derived from the form as well as the record, and nothing here
// guarantees a record range belongs to exactly one form - a mismatch simply
// recomputes.
enum
{
    BUSTER_X86_METADATA_OPERAND_VIEW_UNKNOWN = 0,
};
BUSTER_CT_CHECK(BUSTER_X86_GENERATED_FORM_COUNT <= UINT16_MAX);
BUSTER_GLOBAL_LOCAL BusterX86MetadataOperand buster_x86_metadata_operand_views[BUSTER_X86_GENERATED_OPERAND_COUNT];
// Zero means unknown/invalid; a valid entry stores form_id + 1.  Combining
// ownership and validity removes one array and one hot-path load while keeping
// the overlap defense for malformed/fabricated ranges.
BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_operand_view_owner_plus_one[BUSTER_X86_GENERATED_OPERAND_COUNT];
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_operand_views_ready;

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_pattern_seed_equal(BusterX86MetadataForm form, BusterX86MetadataForm cached)
{
    return form.pattern.offset == cached.pattern.offset && form.pattern.length == cached.pattern.length &&
           form.mandatory_prefix == cached.mandatory_prefix && form.map == cached.map && form.prefix_kind == cached.prefix_kind &&
           form.immediate_width == cached.immediate_width && form.immediate_signed == cached.immediate_signed &&
           form.relocation_base == cached.relocation_base && form.displacement_width == cached.displacement_width &&
           form.field_flags == cached.field_flags;
}

// Returns the prewarmed facts for a form, or null when the table is not
// published or the form is not the normalized row the facts were derived from.
// A null result makes the caller recompute, so this stays correct for
// fabricated forms and for any use before prewarm.
// Borrow the prewarmed normalized row instead of copying it out.  Valid only
// once the prewarm has published, which is also the only time the emission
// paths use it; callers outside that window keep using the copying accessor.
BUSTER_GLOBAL_LOCAL BusterX86MetadataForm const* buster_x86_metadata_normalized_form_borrow(u32 form_id)
{
    if (!buster_x86_metadata_prewarmed || form_id >= BUSTER_X86_GENERATED_FORM_COUNT) return 0;
    if (!buster_x86_metadata_normalized_forms_cached[form_id]) return 0;
    return &buster_x86_metadata_normalized_forms[form_id];
}

// Borrow a form's parsed pattern instead of copying it out.  This is the same
// cache hit buster_x86_metadata_emit_parse_pattern serves, under the same
// cacheability test, but the record stays where it is - an exact plan already
// borrows it this way, and emission reads it on every instruction.  Returns 0
// when the entry is not available, which makes the caller parse into its own
// storage exactly as before.
BUSTER_GLOBAL_LOCAL BusterX86MetadataPatternSemantics const* buster_x86_metadata_pattern_semantics_borrow(
    BusterX86MetadataForm form, bool* parsed)
{
    if (form.id >= BUSTER_X86_GENERATED_FORM_COUNT || !buster_x86_metadata_normalized_forms_cached[form.id] ||
        !buster_x86_metadata_pattern_seed_equal(form, buster_x86_metadata_normalized_forms[form.id]) ||
        !buster_x86_metadata_pattern_semantics_cached[form.id])
        return 0;
    *parsed = buster_x86_metadata_pattern_semantics_results[form.id];
    return &buster_x86_metadata_pattern_semantics_cache[form.id];
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataFormFacts const* buster_x86_metadata_form_facts_for(BusterX86MetadataForm form)
{
    if (!buster_x86_metadata_form_facts_ready || form.id >= BUSTER_X86_GENERATED_FORM_COUNT) return 0;
    if (!buster_x86_metadata_normalized_forms_cached[form.id] ||
        !buster_x86_metadata_pattern_seed_equal(form, buster_x86_metadata_normalized_forms[form.id]))
        return 0;
    BusterX86MetadataForm cached = buster_x86_metadata_normalized_forms[form.id];
    // The facts also depend on fields the pattern seed does not cover.
    if (form.stable_hash != cached.stable_hash || form.iclass.offset != cached.iclass.offset ||
        form.category.offset != cached.category.offset || form.category.length != cached.category.length ||
        form.operand_first != cached.operand_first || form.operand_count != cached.operand_count ||
        form.operand_count > BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY)
        return 0;
    return &buster_x86_metadata_form_facts[form.id];
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_parse_pattern(BusterX86MetadataForm form,
                                                                  BusterX86MetadataPatternSemantics* result)
{
    bool cacheable = form.id < BUSTER_X86_GENERATED_FORM_COUNT && buster_x86_metadata_normalized_forms_cached[form.id] &&
                     buster_x86_metadata_pattern_seed_equal(form, buster_x86_metadata_normalized_forms[form.id]);
    if (cacheable && buster_x86_metadata_pattern_semantics_cached[form.id])
    {
        *result = buster_x86_metadata_pattern_semantics_cache[form.id];
        return buster_x86_metadata_pattern_semantics_results[form.id];
    }
    BusterX86MetadataPatternSemantics pattern;
    memset(&pattern, 0, sizeof(pattern));
    pattern.mandatory_prefix = form.mandatory_prefix;
    pattern.vector_length = 128;
    pattern.mod_kind = BUSTER_X86_METADATA_PATTERN_MOD_ANY;
    pattern.reg_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
    pattern.rm_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
    pattern.srm_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
    pattern.map = form.map;
    pattern.prefix_kind = form.prefix_kind;
    pattern.immediate_width = form.immediate_width;
    pattern.immediate_signed = form.immediate_signed;
    pattern.relative_width = form.relocation_base ? form.displacement_width : 0;
    pattern.displacement_width = form.displacement_width;
    pattern.has_modrm = (form.field_flags & BUSTER_X86_METADATA_FIELD_MODRM) != 0;
    pattern.has_sib = (form.field_flags & BUSTER_X86_METADATA_FIELD_SIB) != 0;
    pattern.has_memory = (form.field_flags & BUSTER_X86_METADATA_FIELD_MEMORY) != 0;
    pattern.has_register = (form.field_flags & BUSTER_X86_METADATA_FIELD_REGISTER) != 0;
    char8 token_buffer[128];
    u32 offset = 0;
    bool after_modrm = false;
    String8 pattern_span = buster_x86_metadata_string_span(form.pattern);
    while (offset < pattern_span.length)
    {
        while (offset < pattern_span.length && buster_x86_metadata_is_space(pattern_span.pointer[offset]))
            offset += 1;
        if (offset == pattern_span.length) break;
        u32 start = offset;
        while (offset < pattern_span.length && !buster_x86_metadata_is_space(pattern_span.pointer[offset]))
            offset += 1;
        u32 length = offset - start;
        if (length >= BUSTER_ARRAY_LENGTH(token_buffer))
        {
            pattern.has_unsupported_token = 1;
            continue;
        }
        u32 token_index = 0;
        for (; token_index < length; token_index += 1)
            token_buffer[token_index] = pattern_span.pointer[start + token_index];
        u32 value = 0;
        if (length >= 3 && token_buffer[0] == '0' && (token_buffer[1] == 'x' || token_buffer[1] == 'X'))
        {
            u32 hex_value = 0;
            u32 hex_bits = 0;
            if (!buster_x86_metadata_emit_parse_number(token_buffer, length, &hex_value, &hex_bits))
            {
                pattern.has_unsupported_token = 1;
            }
            // 3DNow register rows omit the explicit MODRM() marker even
            // though their final numeric token is the post-ModRM opcode
            // extension.  Once the two-byte 0F 0F opcode and MOD/RM shape
            // are known, retain that token as trailing data just as the
            // memory rows do.
            bool numeric_after_modrm = after_modrm ||
                                       (buster_x86_metadata_string_input_equal(form.extension.offset, S8("3DNOW")) &&
                                        pattern.has_modrm && pattern.opcode_count >= 2);
            if (numeric_after_modrm)
            {
                if (pattern.trailing_count < BUSTER_ARRAY_LENGTH(pattern.trailing)) pattern.trailing[pattern.trailing_count++] = (u8)hex_value;
                else pattern.has_unsupported_token = 1;
            }
            else if (pattern.opcode_count < BUSTER_ARRAY_LENGTH(pattern.opcode))
            {
                pattern.opcode[pattern.opcode_count++] = (u8)hex_value;
            }
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (length >= 3 && token_buffer[0] == '0' && (token_buffer[1] == 'b' || token_buffer[1] == 'B'))
        {
            u32 dynamic_value = 0;
            u32 dynamic_bits = 0;
            if (!buster_x86_metadata_emit_parse_number(token_buffer, length, &dynamic_value, &dynamic_bits) || dynamic_bits > 8)
            {
                pattern.has_unsupported_token = 1;
            }
            else
            {
                pattern.has_dynamic_opcode = 1;
                pattern.srm_base = (u8)(dynamic_value << (8 - dynamic_bits));
                pattern.srm_shift = (u8)(8 - dynamic_bits);
                pattern.srm_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
                if (pattern.opcode_count < BUSTER_ARRAY_LENGTH(pattern.opcode)) pattern.opcode[pattern.opcode_count++] = pattern.srm_base;
                else pattern.has_unsupported_token = 1;
            }
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MODRM()")))
        {
            pattern.has_modrm = 1;
            pattern.explicit_modrm = 1;
            after_modrm = true;
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SIB")))
        {
            pattern.has_sib = 1;
            pattern.force_sib = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("MOD[")))
        {
            pattern.has_modrm = 1;
            pattern.explicit_modrm = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MOD[mm]")))
                pattern.mod_kind = BUSTER_X86_METADATA_PATTERN_MOD_ANY;
            else if (buster_x86_metadata_emit_token_has(token_buffer, length, 'm'))
                pattern.mod_kind = BUSTER_X86_METADATA_PATTERN_MOD_MEMORY;
            else if (buster_x86_metadata_emit_parse_bracket_number(token_buffer, length, &value))
                pattern.mod_kind = (u8)value;
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("REG[")))
        {
            pattern.has_modrm = 1;
            pattern.explicit_modrm = 1;
            u32 range_min = 0;
            u32 range_max = 0;
            if (buster_x86_metadata_emit_parse_bracket_range(token_buffer, length, &range_min, &range_max))
            {
                if (pattern.has_reg_range || pattern.reg_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY)
                    buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
                else
                {
                    pattern.has_reg_range = 1;
                    pattern.reg_min = (u8)range_min;
                    pattern.reg_max = (u8)range_max;
                    pattern.reg_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
                }
            }
            else if (buster_x86_metadata_emit_token_has(token_buffer, length, 'r'))
            {
                if (pattern.has_reg_range)
                    buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
                else
                    pattern.reg_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
            }
            else if (buster_x86_metadata_emit_parse_bracket_number(token_buffer, length, &value))
            {
                if (pattern.has_reg_range)
                    buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
                else
                    pattern.reg_fixed = (u8)value;
            }
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("RM[")))
        {
            pattern.has_modrm = 1;
            pattern.explicit_modrm = 1;
            if (buster_x86_metadata_emit_token_has(token_buffer, length, 'n')) pattern.rm_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
            else if (buster_x86_metadata_emit_parse_bracket_number(token_buffer, length, &value)) pattern.rm_fixed = (u8)value;
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("SRM[")))
        {
            if (buster_x86_metadata_emit_token_has(token_buffer, length, 'r'))
            {
                pattern.srm_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
                pattern.has_srm_register = 1;
            }
            else if (buster_x86_metadata_emit_parse_bracket_number(token_buffer, length, &value)) pattern.srm_fixed = (u8)value;
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("MOD=")))
        {
            pattern.has_modrm = 1;
            pattern.explicit_modrm = 1;
            if (token_buffer[length - 1] >= '0' && token_buffer[length - 1] <= '9') pattern.mod_kind = token_buffer[length - 1] - '0';
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("MOD!=")))
        {
            pattern.has_modrm = 1;
            pattern.explicit_modrm = 1;
            if (token_buffer[length - 1] == '3') pattern.mod_kind = BUSTER_X86_METADATA_PATTERN_MOD_MEMORY;
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("REG=")))
        {
            pattern.explicit_modrm = 1;
            if (pattern.has_reg_range)
                buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
            else if (token_buffer[length - 1] >= '0' && token_buffer[length - 1] <= '7') pattern.reg_fixed = token_buffer[length - 1] - '0';
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("RM=")))
        {
            pattern.explicit_modrm = 1;
            if (token_buffer[length - 1] >= '0' && token_buffer[length - 1] <= '7') pattern.rm_fixed = token_buffer[length - 1] - '0';
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("SRM=")) ||
            buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("SRM!=")))
        {
            pattern.srm_not_equal = buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("SRM!="));
            if (token_buffer[length - 1] >= '0' && token_buffer[length - 1] <= '7') pattern.srm_fixed = token_buffer[length - 1] - '0';
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MOD[mm]")) ||
            buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MOD!=3")))
        {
            pattern.has_modrm = 1;
            pattern.mod_kind = BUSTER_X86_METADATA_PATTERN_MOD_MEMORY;
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MOD[0b11]")) ||
            buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MOD=3")))
        {
            pattern.has_modrm = 1;
            pattern.mod_kind = BUSTER_X86_METADATA_PATTERN_MOD_REGISTER;
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REG[rrr]")))
        {
            pattern.has_modrm = 1;
            pattern.reg_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("RM[nnn]")))
        {
            pattern.has_modrm = 1;
            pattern.rm_fixed = BUSTER_X86_METADATA_PATTERN_FIXED_ANY;
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SRM[rrr]")))
        {
            pattern.has_dynamic_opcode = 1;
            pattern.srm_shift = 0;
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norex2_prefix")))
        {
            // This token forbids the APX refining prefix; it does not change
            // the ordinary legacy/REX family carried by the normalized row.
            pattern.has_prefix_control = 1;
            pattern.no_rex2 = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rex2_refining_prefix")))
        {
            pattern.prefix_kind = BUSTER_X86_METADATA_PREFIX_REX2;
            pattern.has_prefix_kind = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("UIMM")))
        {
            u32 immediate_index = pattern.immediate_count;
            if (immediate_index < BUSTER_ARRAY_LENGTH(pattern.immediate_widths)) pattern.immediate_count += 1;
            else pattern.has_unsupported_token = 1;
            u8 width = 0;
            u8 variable = 0;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("UIMM8()")) ||
                buster_x86_metadata_emit_token_equal(token_buffer, length, S8("UIMM8_1()"))) width = 1;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("UIMM16()"))) width = 2;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("UIMM32()"))) width = 4;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("UIMMv()"))) variable = 1;
            else pattern.has_unsupported_token = 1;
            if (immediate_index < BUSTER_ARRAY_LENGTH(pattern.immediate_widths))
            {
                pattern.immediate_widths[immediate_index] = width;
                pattern.immediate_signeds[immediate_index] = 0;
                pattern.immediate_variables[immediate_index] = variable;
            }
            else pattern.has_unsupported_token = 1;
            pattern.immediate_width = width;
            pattern.immediate_variable = variable;
            pattern.immediate_signed = 0;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("SE_IMM")))
        {
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SE_IMM8()"))) pattern.selector_immediate = 1;
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("SIMM")))
        {
            u32 immediate_index = pattern.immediate_count;
            if (immediate_index < BUSTER_ARRAY_LENGTH(pattern.immediate_widths)) pattern.immediate_count += 1;
            else pattern.has_unsupported_token = 1;
            u8 width = 0;
            u8 variable = 0;
            pattern.immediate_signed = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SIMM8()"))) width = 1;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SIMMz()")))
                variable = 1;
            else pattern.has_unsupported_token = 1;
            if (immediate_index < BUSTER_ARRAY_LENGTH(pattern.immediate_widths))
            {
                pattern.immediate_widths[immediate_index] = width;
                pattern.immediate_signeds[immediate_index] = 1;
                pattern.immediate_variables[immediate_index] = variable;
            }
            else pattern.has_unsupported_token = 1;
            pattern.immediate_width = width;
            pattern.immediate_variable = variable;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("BRDISP")))
        {
            pattern.relative_count += 1;
            pattern.has_prefix_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("BRDISP8()"))) pattern.relative_width = 1;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("BRDISP32()"))) pattern.relative_width = 4;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("BRDISP64()"))) pattern.relative_width = 8;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("BRDISPz()")))
            {
                pattern.relative_variable = 1;
                pattern.relative_width = 0;
            }
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("DISP")))
        {
            pattern.displacement_count += 1;
            pattern.has_memory = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("DISPv()"))) pattern.displacement_width = 4;
            else pattern.has_unsupported_token = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MEMDISPv()")))
        {
            // moffs encodings carry an absolute offset directly after the
            // opcode rather than a ModRM displacement.  Keep the source
            // token typed so the narrow MOV A0-A3 cohort can share the
            // ordinary memory operand and relocation plumbing without
            // widening this to implicit-DI forms such as MASKMOV.
            pattern.displacement_count += 1;
            pattern.has_memory = 1;
            continue;
        }
        if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("V0F"))) pattern.map = BUSTER_X86_METADATA_MAP_0F;
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("V0F38"))) pattern.map = BUSTER_X86_METADATA_MAP_0F38;
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("V0F3A"))) pattern.map = BUSTER_X86_METADATA_MAP_0F3A;
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MAP4"))) pattern.map = BUSTER_X86_METADATA_MAP_4;
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MAP5"))) pattern.map = BUSTER_X86_METADATA_MAP_5;
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MAP6"))) pattern.map = BUSTER_X86_METADATA_MAP_6;
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MAP7"))) pattern.map = BUSTER_X86_METADATA_MAP_7;
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("XMAP8")))
        {
            pattern.map = BUSTER_X86_METADATA_MAP_X8;
            pattern.prefix_kind = BUSTER_X86_METADATA_PREFIX_XOP;
            pattern.has_prefix_kind = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("XMAP9")))
        {
            pattern.map = BUSTER_X86_METADATA_MAP_X9;
            pattern.prefix_kind = BUSTER_X86_METADATA_PREFIX_XOP;
            pattern.has_prefix_kind = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("XMAPA")))
        {
            pattern.map = BUSTER_X86_METADATA_MAP_XA;
            pattern.prefix_kind = BUSTER_X86_METADATA_PREFIX_XOP;
            pattern.has_prefix_kind = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("V66")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("66_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("osz_refining_prefix")))
        {
            buster_x86_metadata_emit_set_mandatory_prefix(&pattern, 0x66);
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("66_prefix")))
                buster_x86_metadata_emit_set_operand_size_control(&pattern, BUSTER_X86_METADATA_PATTERN_OPERAND_SIZE_66);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VF2")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("f2_refining_prefix")))
            buster_x86_metadata_emit_set_mandatory_prefix(&pattern, 0xf2);
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VF3")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("f3_refining_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("refining_f3")))
            buster_x86_metadata_emit_set_mandatory_prefix(&pattern, 0xf3);
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("W0"))) { pattern.w = 0; pattern.has_w = 1; }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("W1")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rexw_prefix")))
        {
            pattern.w = 1;
            pattern.has_w = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VL128")))
            buster_x86_metadata_emit_set_vector_length(&pattern, 128);
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VL256")))
            buster_x86_metadata_emit_set_vector_length(&pattern, 256);
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VL512")))
            buster_x86_metadata_emit_set_vector_length(&pattern, 512);
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MODE_SHORT_UD0=1")))
        {
            // The short UD0 row is the two-byte 0f ff form.  Its MODE token
            // selects an opcode spelling; it is not a ModRM presence bit.
            pattern.short_ud0 = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MODE_SHORT_UD0=0")))
        {
            // Long UD0 rows carry their own MOD/RM controls.  The zero form
            // of this selector has no byte-level effect.
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MODEP5=0")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MODEP5=1")))
        {
            // MODEP5 is a selector for the legacy LOOP-family semantics, not
            // a MODRM field.  Keep this branch before the generic MOD* test
            // because MODEP5 shares its first three letters with MOD.
            pattern.has_prefix_control = 1;
            pattern.has_modep5 = 1;
            pattern.modep5_value = token_buffer[length - 1] - '0';
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("MOD")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("REG")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("RM"))) { pattern.has_modrm = 1; pattern.explicit_modrm = 1; }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("EVV")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VV1")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VNP")))
        {
            pattern.has_prefix_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("EVV")))
            {
                pattern.explicit_evex_selector = 1;
                pattern.prefix_kind = BUSTER_X86_METADATA_PREFIX_EVEX;
            }
            else if (!pattern.explicit_evex_selector)
            {
                pattern.prefix_kind = buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VNP")) &&
                                               (form.apx_flags & BUSTER_X86_METADATA_APX)
                                           ? BUSTER_X86_METADATA_PREFIX_EVEX
                                           : BUSTER_X86_METADATA_PREFIX_VEX;
            }
            pattern.has_prefix_kind = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("XOPV")))
        {
            // The compact importer historically copied XOPV rows as VEX.
            // The pattern is the authoritative normalized discriminator:
            // XOPV plus XMAP8/9/A is an XOP prefix, not a VEX prefix.
            pattern.has_prefix_control = 1;
            pattern.prefix_kind = BUSTER_X86_METADATA_PREFIX_XOP;
            pattern.has_prefix_kind = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("ND=")))
        {
            pattern.has_apx_control = 1;
            pattern.has_nd = 1;
            if (length && (token_buffer[length - 1] == '0' || token_buffer[length - 1] == '1'))
                pattern.nd_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("NF=")))
        {
            pattern.has_apx_control = 1;
            pattern.has_nf = 1;
            if (length && (token_buffer[length - 1] == '0' || token_buffer[length - 1] == '1'))
                pattern.nf_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NO_SCC_NF0")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NO_SCC_NF1")))
        {
            pattern.has_apx_control = 1;
            pattern.has_nf = 1;
            pattern.nf_value = token_buffer[length - 1] == '1';
            pattern.no_scc = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("EVAPX")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("EVAPX()")))
        {
            pattern.has_apx_control = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("EVAPX_SCC")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("EVAPX_SCC()")))
        {
            pattern.has_apx_control = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NOEVSR")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NOVSR")))
        {
            pattern.has_prefix_control = 1;
            pattern.no_vector_source = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("MASK")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("ZEROING")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SAE")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SAE()")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("AVX512_ROUND")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("FIX_ROUND")))
        {
            pattern.has_decorator_control = 1;
            if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("MASK")))
            {
                if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MASK=0"))) pattern.mask_control = 1;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MASK=1"))) pattern.mask_control = 2;
                else buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS);
            }
            else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("ZEROING")))
            {
                if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ZEROING=0"))) pattern.zeroing_control = 1;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ZEROING=1"))) pattern.zeroing_control = 2;
                else buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS);
            }
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SAE")) ||
                     buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SAE()")))
                pattern.has_sae_control = 1;
            else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("AVX512_ROUND")))
                pattern.has_rounding_control = 1;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("FIX_ROUND_LEN128()")))
                buster_x86_metadata_emit_set_fixed_rounding_length(&pattern, 128);
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("FIX_ROUND_LEN512()")))
                buster_x86_metadata_emit_set_fixed_rounding_length(&pattern, 512);
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("NELEM_")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("ESIZE_")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("TILE")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("UISA_")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("VMODRM_")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("SIB")))
        {
            pattern.has_amx_control = 1;
            if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("NELEM_")))
            {
                pattern.has_tuple_control = 1;
                if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_FULL()")) ||
                    buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_FULLMEM()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_FULL;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_MEM128()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_PATTERN_TUPLE_MEM128;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_HALF()")) ||
                         buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_HALFMEM()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_HALF;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_QUARTER()")) ||
                         buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_QUARTERMEM()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_QUARTER;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_EIGHTHMEM()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_EIGHTH;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_ONE()")) ||
                         buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_MOVDDUP()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_SCALAR;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_TUPLE1()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_TUPLE1;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_TUPLE1_4X()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_TUPLE1_4X;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_TUPLE1_BYTE()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_TUPLE1_BYTE;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_TUPLE1_WORD()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_TUPLE1_WORD;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_TUPLE2()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_TUPLE2;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_TUPLE4()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_TUPLE4;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("NELEM_TUPLE8()")))
                    pattern.tuple_control_kind = BUSTER_X86_METADATA_TUPLE_TUPLE8;
                else buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS);
            }
            if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("ESIZE_")))
            {
                pattern.has_element_size_control = 1;
                if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ESIZE_4_BITS()"))) pattern.element_size_bits = 4;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ESIZE_8_BITS()"))) pattern.element_size_bits = 8;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ESIZE_16_BITS()"))) pattern.element_size_bits = 16;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ESIZE_32_BITS()"))) pattern.element_size_bits = 32;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ESIZE_64_BITS()"))) pattern.element_size_bits = 64;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ESIZE_128_BITS()"))) pattern.element_size_bits = 128;
                else buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS);
            }
            if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("TILE"))) pattern.has_tile_control = 1;
            if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("VMODRM_")) ||
                buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("UISA_VMODRM_")))
            {
                pattern.has_vsib_control = 1;
                if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VMODRM_XMM()")) ||
                    buster_x86_metadata_emit_token_equal(token_buffer, length, S8("UISA_VMODRM_XMM()")))
                    pattern.vsib_vector_length = 128;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VMODRM_YMM()")) ||
                         buster_x86_metadata_emit_token_equal(token_buffer, length, S8("UISA_VMODRM_YMM()")))
                    pattern.vsib_vector_length = 256;
                else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("VMODRM_ZMM()")) ||
                         buster_x86_metadata_emit_token_equal(token_buffer, length, S8("UISA_VMODRM_ZMM()")))
                    pattern.vsib_vector_length = 512;
                else
                    buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS);
            }
            else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("UISA_")))
                buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("lock_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("LOCK")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("nolock_prefix")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_address_control = 1;
            buster_x86_metadata_emit_set_lock_control(
                &pattern, buster_x86_metadata_emit_token_equal(token_buffer, length, S8("nolock_prefix")) ? 2 : 1);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("repe")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REP")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rep_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("repne")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REPNE")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norep")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_address_control = 1;
            buster_x86_metadata_emit_set_rep_control(
                &pattern, buster_x86_metadata_emit_token_equal(token_buffer, length, S8("repne")) ||
                              buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REPNE"))
                          ? 2
                          : buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norep")) ? 3 : 1);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("no66_prefix")))
        {
            buster_x86_metadata_emit_set_operand_size_control(&pattern, BUSTER_X86_METADATA_PATTERN_OPERAND_SIZE_NO66);
            pattern.forbid_operand_size_override = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("CR_WIDTH()")))
        {
            // CR_WIDTH is a source-semantic assertion in the XED pattern,
            // but it also explicitly fixes the ModRM instruction's W bit to
            // zero.  Keep the normalized operand width validation while
            // preventing the generic GPR64 heuristic from inventing REX.W.
            pattern.w = 0;
            pattern.has_w = 1;
            pattern.no_rex2 = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("WBNOINVD=0")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("WBNOINVD=1")))
        {
            // WBNOINVD selects the fixed opcode mnemonic.  The normalized
            // iclass, mandatory prefix, and fixed bytes carry that selection.
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REP=0")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REP=2")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REP=3")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_rep_selector = 1;
            pattern.rep_selector_value = token_buffer[length - 1] - '0';
            buster_x86_metadata_emit_set_rep_control(&pattern, pattern.rep_selector_value == 0 ? 3
                                                                               : pattern.rep_selector_value == 2 ? 2
                                                                                                                   : 1);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REP!=3")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_address_control = 1;
            // XED REP=3 is the F3 refining prefix.  This row therefore
            // accepts the unprefixed and F2 spellings, but not F3; it is not
            // the same constraint as the source-level "norep" control.
            pattern.rep_not_f3 = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("mode16")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("mode32")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("mode64")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("not64")))
        {
            u8 mode = buster_x86_metadata_emit_token_equal(token_buffer, length, S8("mode16"))
                          ? BUSTER_X86_METADATA_PATTERN_MODE_16
                          : buster_x86_metadata_emit_token_equal(token_buffer, length, S8("mode32"))
                                ? BUSTER_X86_METADATA_PATTERN_MODE_32
                                : buster_x86_metadata_emit_token_equal(token_buffer, length, S8("mode64"))
                                      ? BUSTER_X86_METADATA_PATTERN_MODE_64
                                      : BUSTER_X86_METADATA_PATTERN_MODE_NOT64;
            buster_x86_metadata_emit_set_mode_control(&pattern, mode);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norex2_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rex2_refining_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rexw_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("no_refining_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("not_refining")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("osz_refining_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("66_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("f2_refining_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("f3_refining_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("refining_f3")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("not_refining_f3")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REFINING66")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REFINING66()")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("FORCE64()")))
        {
            pattern.has_prefix_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("FORCE64()")))
                pattern.has_force64_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("no_refining_prefix")) ||
                buster_x86_metadata_emit_token_equal(token_buffer, length, S8("not_refining")))
                pattern.forbid_mandatory_prefix = 1;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("not_refining_f3")))
                pattern.rep_not_f3 = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("not16")))
        {
            // `not16` is an execution-mode exclusion, not a prefix control.
            // Keep recognition narrow: only the fixed BASE NOP rows carry a
            // complete byte-level schema for this token.  Every other row
            // remains an explicit schema blocker rather than being widened
            // by accepting an unmodeled mode predicate.
            if (buster_x86_metadata_form_is_fixed_not16_nop(form)) pattern.not16 = 1;
            else pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("DF64()")))
        {
            // DF64 fixes the row's default data size; it is a schema
            // constraint, not a byte-producing legacy prefix.
            pattern.has_prefix_control = 1;
            pattern.df64 = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ea32")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("eamode32")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ea64")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("eamode64")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("eanot16")))
        {
            pattern.has_address_control = 1;
            pattern.required_address_size = buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ea32")) ||
                                                     buster_x86_metadata_emit_token_equal(token_buffer, length, S8("eamode32"))
                                                 ? 32
                                                 : buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ea64")) ||
                                                           buster_x86_metadata_emit_token_equal(token_buffer, length, S8("eamode64"))
                                                       ? 64
                                                       : 0;
            if (!pattern.required_address_size && !buster_x86_metadata_emit_token_equal(token_buffer, length, S8("eanot16")))
                buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("no67_prefix")))
        {
            pattern.has_address_control = 1;
            pattern.forbid_address_override = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexw_prefix")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_w = 1;
            pattern.w = 0;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("BRANCH_HINT")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("BRANCH_HINT()")))
        {
            pattern.has_prefix_control = 1;
            // Legacy branch hints are CS/DS overrides (2e/3e), not REP/F2/F3.
            // Keep this as a dedicated control so the exact mode64/norex2
            // conditional-branch cohort can select either hint or no hint.
            pattern.has_branch_hint_control = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("BCRC=")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_bcrc = 1;
            if (length && (token_buffer[length - 1] == '0' || token_buffer[length - 1] == '1'))
                pattern.bcrc_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("UBIT=")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_ubit = 1;
            if (length && (token_buffer[length - 1] == '0' || token_buffer[length - 1] == '1'))
                pattern.ubit_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
            if (pattern.ubit_value != 1)
                buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexr_r4")))
        {
            // XED's norexr_r4 atom fixes EVEX's inverted R' bit to one. The
            // ACE-1 rows also carry REG[0b000], which fixes the low R bits;
            // that ordinary pattern field is the separate low-R constraint.
            // Keep this token in the same typed representation as
            // EVEXR4_ONE(), without special-casing any mnemonic.
            pattern.has_prefix_control = 1;
            pattern.has_evex_r4 = 1;
            pattern.evex_r4_value = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rexb")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rexb_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb4")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb4_prefix")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rexb4")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rexb4_prefix")))
        {
            pattern.has_prefix_control = 1;
            bool forbid = buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb")) ||
                          buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb_prefix")) ||
                          buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb4")) ||
                          buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb4_prefix"));
            bool b4 = buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb4")) ||
                      buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexb4_prefix")) ||
                      buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rexb4")) ||
                      buster_x86_metadata_emit_token_equal(token_buffer, length, S8("rexb4_prefix"));
            if (b4)
                pattern.rex_b4_control = forbid ? BUSTER_X86_METADATA_REX_CONTROL_FORBID : BUSTER_X86_METADATA_REX_CONTROL_REQUIRE;
            else
                pattern.rex_b_control = forbid ? BUSTER_X86_METADATA_REX_CONTROL_FORBID : BUSTER_X86_METADATA_REX_CONTROL_REQUIRE;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("norexr_prefix")))
        {
            // XED's norexr_prefix atom forbids the VEX/EVEX R extension bit.
            // Keep it as a typed prefix constraint; unlike norexr_r4 this is
            // the low R field and never changes the prefix family.
            pattern.has_prefix_control = 1;
            pattern.no_rexr_prefix = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("EVEXR4")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("norexr")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("SCC")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("NO_SCC")))
        {
            pattern.has_prefix_control = 1;
            if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("SCC")))
            {
                pattern.has_scc = 1;
                u32 scc_value = 0;
                bool scc_valid = length > 3;
                for (u32 scc_index = 3; scc_valid && scc_index < length; scc_index += 1)
                {
                    char8 scc_digit = token_buffer[scc_index];
                    if (scc_digit < '0' || scc_digit > '9' || scc_value > (15u - (u32)(scc_digit - '0')) / 10u)
                    {
                        scc_valid = false;
                        break;
                    }
                    scc_value = scc_value * 10u + (u32)(scc_digit - '0');
                }
                if (scc_valid) pattern.scc_value = (u8)scc_value;
                else buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS);
            }
            else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("EVEXR4")))
            {
                if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("EVEXR4_ONE()")))
                {
                    pattern.has_evex_r4 = 1;
                    pattern.evex_r4_value = 1;
                }
                else buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS);
            }
            else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("norexr")))
                buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS);
            else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("NO_SCC")))
                pattern.no_scc = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("IMMUNE66_LOOP64()")))
        {
            pattern.has_prefix_control = 1;
            pattern.immune66_loop64 = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("IMMUNE_REXW()")))
        {
            pattern.has_prefix_control = 1;
            pattern.immune_rexw = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("OVERRIDE_SEG0()")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("OVERRIDE_SEG1()")))
        {
            pattern.has_prefix_control = 1;
            u8 segment_index = token_buffer[12] - '0';
            if (pattern.has_segment_override && pattern.segment_override_index != segment_index)
                buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
            pattern.has_segment_override = 1;
            pattern.segment_override_index = segment_index;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("MPXMODE=")))
        {
            // MPXMODE selects between the MPX instruction rows and the BASE
            // NOP aliases in this source snapshot.  It is not a byte-level
            // prefix, so retain it as typed source semantics and validate the
            // value/form pairing after the complete pattern has been parsed.
            pattern.has_prefix_control = 1;
            pattern.has_mpx_mode = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MPXMODE=0")))
                pattern.mpx_mode_value = 0;
            else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("MPXMODE=1")))
                pattern.mpx_mode_value = 1;
            else
                buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("REMOVE_SEGMENT()")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_remove_segment = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("P4=0")) ||
                 buster_x86_metadata_emit_token_equal(token_buffer, length, S8("P4=1")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_p4_control = 1;
            pattern.p4_value = (u8)(token_buffer[length - 1] - '0');
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("P4=")))
        {
            pattern.has_prefix_control = 1;
            pattern.has_p4_control = 1;
            pattern.has_unsupported_token = 1;
        }
        else if ((buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("IMMUNE")) &&
                 !buster_x86_metadata_emit_token_equal(token_buffer, length, S8("IMMUNE66()"))) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("OVERRIDE")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("REMOVE")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("FORCE")) ||
                 buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("P4")))
        {
            pattern.has_prefix_control = 1;
            buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ENCDELETE")))
        {
            // XED's ENCDELETE marker identifies a deleted/noncanonical alias,
            // not a source-selectable encoding.  Keep it typed in the parsed
            // pattern while retaining an explicit blocker so it cannot leak
            // into the encoder through generic prefix-token handling.
            pattern.has_prefix_control = 1;
            pattern.has_encdelete_control = 1;
            buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("ONE()")))
        {
            // ONE() is an implicit constant-1 operand.  It contributes no
            // source operand and no immediate byte; the operand record is
            // checked below so this recognition cannot turn an unrelated
            // token into an emitted encoding.
            pattern.has_implicit_one = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("IGNORE66()")))
        {
            // The canonical source form never synthesizes an optional 66
            // prefix.  Preserve the token so the control check below can
            // reject an impossible mandatory-66 combination rather than
            // silently treating the token as an unrelated spelling.
            pattern.has_ignore_66 = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("IMMUNE66()")))
        {
            // IMMUNE66() describes an optional operand-size prefix that is
            // irrelevant to this form.  The normalized mandatory-prefix and
            // operand-width fields already carry the bytes and source shape;
            // this control adds neither a source operand nor an encoded byte.
            pattern.has_prefix_control = 1;
            pattern.immune66 = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("LZCNT=")))
        {
            pattern.has_lzcnt_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("LZCNT=0")) ||
                buster_x86_metadata_emit_token_equal(token_buffer, length, S8("LZCNT=1")))
                pattern.lzcnt_control_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("TZCNT=")))
        {
            pattern.has_tzcnt_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("TZCNT=0")) ||
                buster_x86_metadata_emit_token_equal(token_buffer, length, S8("TZCNT=1")))
                pattern.tzcnt_control_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("CLDEMOTE=")))
        {
            pattern.has_cldemote_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("CLDEMOTE=0")) ||
                buster_x86_metadata_emit_token_equal(token_buffer, length, S8("CLDEMOTE=1")))
                pattern.cldemote_control_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("IBHF=")))
        {
            pattern.has_ibhf_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("IBHF=0")) ||
                buster_x86_metadata_emit_token_equal(token_buffer, length, S8("IBHF=1")))
                pattern.ibhf_control_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("PREFETCHRST=")))
        {
            pattern.has_prefetchrst_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("PREFETCHRST=0")) ||
                buster_x86_metadata_emit_token_equal(token_buffer, length, S8("PREFETCHRST=1")))
                pattern.prefetchrst_control_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("PREFETCHIT=")))
        {
            pattern.has_prefetchit_control = 1;
            if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("PREFETCHIT=0")) ||
                buster_x86_metadata_emit_token_equal(token_buffer, length, S8("PREFETCHIT=1")))
                pattern.prefetchit_control_value = (u8)(token_buffer[length - 1] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_starts_with(token_buffer, length, S8("CET=")))
        {
            // CET=1 selects an actual CET instruction (ENDBR/RDSSP), while
            // CET=0 selects the legacy NOP aliases occupying those bytes when
            // CET is disabled.  The normalized opcode/prefix/ModRM fields
            // carry the bytes; retain the selector so aliases cannot collapse
            // across the CET policy boundary.
            pattern.has_prefix_control = 1;
            pattern.has_cet_control = 1;
            if (length == 5 && (token_buffer[4] == '0' || token_buffer[4] == '1'))
                pattern.cet_value = (u8)(token_buffer[4] - '0');
            else
                pattern.has_unsupported_token = 1;
        }
        else if (buster_x86_metadata_emit_token_equal(token_buffer, length, S8("CET_NO_TRACK()")))
        {
            // CET_NO_TRACK is an optional source prefix for the four legacy
            // indirect CALL/JMP rows.  It shares byte 3e with the legacy DS
            // branch-hint encoding, but has separate typed query semantics.
            pattern.has_prefix_control = 1;
            pattern.has_cet_no_track = 1;
        }
        else pattern.has_unsupported_token = 1;
    }
    if (pattern.has_reg_range && (pattern.reg_min > pattern.reg_max || pattern.reg_max > 7))
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    pattern.apx_fixed_width_no_w_isa =
        buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_ENQCMD")) ||
        buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_MOVDIR64B")) ||
        buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_INVPCID")) ||
        buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_MSR_IMM")) ||
        buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_VMX"));
    if (!pattern.opcode_count && form.fixed_byte_count)
    {
        u32 fixed_index = 0;
        for (; fixed_index < form.fixed_byte_count && fixed_index < BUSTER_ARRAY_LENGTH(pattern.opcode); fixed_index += 1)
            pattern.opcode[pattern.opcode_count++] = form.fixed_bytes[fixed_index];
        if (fixed_index != form.fixed_byte_count) pattern.has_unsupported_token = 1;
    }
    if (!pattern.immediate_variable) pattern.immediate_width = pattern.immediate_width ? pattern.immediate_width : form.immediate_width;
    if (!pattern.relative_variable)
        pattern.relative_width = pattern.relative_width ? pattern.relative_width : (form.relocation_base ? form.displacement_width : 0);
    pattern.displacement_width = pattern.displacement_width ? pattern.displacement_width : form.displacement_width;
    pattern.has_modrm |= (form.field_flags & BUSTER_X86_METADATA_FIELD_MODRM) != 0;
    if (!pattern.explicit_modrm && pattern.has_dynamic_opcode) pattern.has_modrm = 0;
    // XED's VZEROALL/VZEROUPPER rows carry the generic MODRM field bit even
    // though their fixed VEX 0x77 spelling has no ModRM byte.  Keep the
    // generated row as the sole source of truth, but normalize this precise
    // zero-operand form before either checked or machine-exact emission sees
    // the derived pattern; other VEX rows with explicit ModRM remain intact.
    if (form.operand_count == 0 && !pattern.explicit_modrm && !pattern.has_dynamic_opcode &&
        pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX && pattern.map == 1 && pattern.opcode_count == 1 &&
        pattern.opcode[0] == 0x77 && (pattern.vector_length == 128 || pattern.vector_length == 256))
        pattern.has_modrm = 0;
    pattern.has_sib |= (form.field_flags & BUSTER_X86_METADATA_FIELD_SIB) != 0;
    pattern.has_memory |= (form.field_flags & BUSTER_X86_METADATA_FIELD_MEMORY) != 0;
    pattern.has_register |= (form.field_flags & BUSTER_X86_METADATA_FIELD_REGISTER) != 0;
    bool loop_form = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOP")) ||
                     buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPE")) ||
                     buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPNE"));
    if ((pattern.has_modep5 || pattern.has_rep_selector) && !loop_form)
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    // MODEP5 and REP selectors are the legacy LOOP-family control fields.
    // Their documented forms are byte-level prefix variants, so leave them
    // available to the encoder when the row is one of the LOOP mnemonics.
    // Other forms carrying these controls remain schema-blocked until their
    // semantics are modeled explicitly.
    if (pattern.has_modep5 && pattern.modep5_value != 0 && !loop_form)
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    if (pattern.has_rep_selector && !loop_form)
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    if (pattern.immune66_loop64 && buster_x86_metadata_emit_string_has(form.attributes, S8("UNDOCUMENTED")))
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    if (pattern.has_segment_override && !buster_x86_metadata_emit_canonical_segment_override(form, pattern))
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    if (pattern.has_cet_no_track && !buster_x86_metadata_emit_canonical_cet_no_track(form, pattern))
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    if (pattern.has_remove_segment &&
        (!buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LEA")) || pattern.opcode_count != 1 ||
         pattern.opcode[0] != 0x8d || !pattern.has_memory || pattern.mod_kind != BUSTER_X86_METADATA_PATTERN_MOD_MEMORY))
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    if (pattern.has_p4_control &&
        (!pattern.p4_value || !buster_x86_metadata_string_input_equal(form.iclass.offset, S8("PAUSE")) ||
         pattern.opcode_count != 1 || pattern.opcode[0] != 0x90 || pattern.mandatory_prefix != 0xf3))
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    if (pattern.has_cet_control && pattern.cet_value == 0)
    {
        // These rows are the legacy NOP aliases occupying the CET opcode
        // space when CET is disabled.  They stay blocked even when their
        // exact legacy signature is present: the public source query has no
        // unambiguous mandatory-F3 selector and the same bytes become ENDBR
        // or RDSSP when CET is enabled.  Retain the typed selector for audit
        // and regeneration while refusing to advertise an encoder capability
        // that could weaken CET feature policy.
        buster_x86_metadata_emit_mark_unresolved(&pattern, BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS);
    }
    if (pattern.short_ud0)
    {
        // MODE_SHORT_UD0 is a selector for the fixed 0f ff spelling, never a
        // request to synthesize the long form's ModRM byte.
        pattern.has_modrm = 0;
        pattern.has_sib = 0;
    }
    *result = pattern;
    if (cacheable)
    {
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        buster_x86_metadata_pattern_semantics_cache[form.id] = pattern;
        buster_x86_metadata_pattern_semantics_results[form.id] = !pattern.has_unsupported_token;
        buster_x86_metadata_pattern_semantics_cached[form.id] = true;
    }
    return !pattern.has_unsupported_token;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_apx_fixed_width_no_w(BusterX86MetadataForm form,
                                                                        BusterX86MetadataPatternSemantics pattern)
{
    return pattern.apx_fixed_width_no_w_isa && pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX &&
           (form.apx_flags & BUSTER_X86_METADATA_APX) != 0 && !pattern.has_w && pattern.no_vector_source &&
           pattern.vector_length == 128 && pattern.has_modrm && pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_MEMORY &&
           pattern.has_memory;
}

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_emit_element_size_bits(BusterX86MetadataForm form);
BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_emit_tuple_memory_width(BusterX86MetadataPatternSemantics pattern);

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_form_has_implicit_immediate(BusterX86MetadataForm form)
{
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &operand)) return false;
        if (operand.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE && (operand.access & BUSTER_X86_METADATA_ACCESS_IMPLICIT)) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_mode66_residual(BusterX86MetadataForm form,
                                                                    BusterX86MetadataPatternSemantics pattern)
{
    // The residual is deliberately a token-shape cohort, not a mnemonic
    // wildcard.  Only BASE and X87 rows with one explicit 16/32-bit default
    // and its matching 66/no66 selector qualify; string/segment/REP rows
    // remain gated by their existing controls.
    bool base_or_x87 = buster_x86_metadata_string_input_equal(form.extension.offset, S8("BASE")) ||
                       buster_x86_metadata_string_input_equal(form.extension.offset, S8("X87"));
    if (!base_or_x87 || form.prefix_kind != BUSTER_X86_METADATA_PREFIX_LEGACY ||
        (pattern.mode_control != BUSTER_X86_METADATA_PATTERN_MODE_16 &&
         pattern.mode_control != BUSTER_X86_METADATA_PATTERN_MODE_32) ||
        (pattern.operand_size_control != BUSTER_X86_METADATA_PATTERN_OPERAND_SIZE_66 &&
         pattern.operand_size_control != BUSTER_X86_METADATA_PATTERN_OPERAND_SIZE_NO66))
        return false;
    if (pattern.has_address_control || pattern.lock_control || pattern.rep_control || pattern.rep_not_f3 ||
        pattern.has_modep5 || pattern.has_rep_selector || pattern.has_segment_override || pattern.immune66_loop64 ||
        pattern.df64 || pattern.required_address_size || pattern.forbid_address_override || pattern.forbid_mandatory_prefix ||
        pattern.no_rex2 || pattern.no_vector_source || pattern.has_prefix_kind || pattern.has_w || pattern.has_apx_control ||
        pattern.has_amx_control || pattern.has_decorator_control || pattern.has_bcrc || pattern.has_ubit || pattern.has_scc ||
        pattern.no_scc || pattern.has_evex_r4 || pattern.has_nd || pattern.has_nf || pattern.has_ignore_66)
        return false;
    return true;
}
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_mpx_mode_supported(BusterX86MetadataForm form,
                                                                      BusterX86MetadataPatternSemantics pattern)
{
    if (!pattern.has_mpx_mode || pattern.mpx_mode_value > 1) return false;

    // The normalized MPX cohort is deliberately closed over the seven
    // architectural MPX mnemonics and the three BASE NOP rows that remain
    // NOPs when MPX mode is enabled.  The BASE MPXMODE=0 rows are decode-
    // policy aliases: on an MPX-enabled target their bytes can execute the
    // architectural MPX operation, and a positive feature query cannot
    // express MPX absence.  Keep those aliases blocked rather than exposing
    // an unsound source-selectable NOP form.
    bool mpx_instruction = buster_x86_metadata_string_input_equal(form.extension.offset, S8("MPX")) &&
                           buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("MPX")) &&
                           (buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDMK")) ||
                            buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDCL")) ||
                            buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDCU")) ||
                            buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDCN")) ||
                            buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDMOV")) ||
                            buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDLDX")) ||
                            buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDSTX")));
    bool base_nop = buster_x86_metadata_string_input_equal(form.extension.offset, S8("BASE")) &&
                    buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("PPRO")) &&
                    buster_x86_metadata_string_input_equal(form.iclass.offset, S8("NOP"));
    return pattern.mpx_mode_value == 1 && (mpx_instruction || base_nop);
}
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_boolean_control_matches(BusterX86MetadataForm form,
                                                                            BusterX86MetadataPatternSemantics pattern)
{
    // The XED boolean controls are typed selectors, not generic annotations:
    // value 1 names the architectural mnemonic while value 0 names the
    // legacy NOP/compatibility row sharing its opcode bytes.  Requiring the
    // normalized iclass here keeps a value-0 row from being silently treated
    // as its value-1 neighbour (and vice versa) in both the ledger and the
    // direct emitter.  Decode-only NOP aliases are rejected below because
    // they become real instructions when the corresponding feature exists.
    if (pattern.has_lzcnt_control)
    {
        bool value_one = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LZCNT"));
        bool value_zero = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BSR"));
        if ((!value_one && !value_zero) || pattern.lzcnt_control_value != (u8)value_one) return false;
        // The F3 BSR spelling is a decode-only alias: on a target with
        // LZCNT, the same bytes execute LZCNT.  Canonical BASE BSR rows
        // remain source-selectable; keep this refining-prefix alias blocked.
        if (value_zero) return false;
    }
    if (pattern.has_tzcnt_control)
    {
        bool value_one = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("TZCNT"));
        bool value_zero = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BSF"));
        if ((!value_one && !value_zero) || pattern.tzcnt_control_value != (u8)value_one) return false;
        if (value_zero) return false;
    }
    if (pattern.has_cldemote_control)
    {
        bool value_one = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("CLDEMOTE"));
        bool value_zero = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("NOP"));
        if ((!value_one && !value_zero) || pattern.cldemote_control_value != (u8)value_one) return false;
        // CLDEMOTE=0 is a decode-only NOP alias.  Once CLDEMOTE is enabled,
        // the same bytes execute the architectural instruction; keeping the
        // alias out of source selection avoids silently changing semantics.
        if (value_zero) return false;
    }
    if (pattern.has_ibhf_control)
    {
        bool value_one = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("IBHF"));
        bool value_zero = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("NOP"));
        if ((!value_one && !value_zero) || pattern.ibhf_control_value != (u8)value_one) return false;
        if (value_zero) return false;
    }
    if (pattern.has_prefetchrst_control)
    {
        bool value_one = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("PREFETCHRST2"));
        bool value_zero = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("NOP"));
        if ((!value_one && !value_zero) || pattern.prefetchrst_control_value != (u8)value_one) return false;
        if (value_zero) return false;
    }
    if (pattern.has_prefetchit_control)
    {
        bool value_one = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("PREFETCHIT0")) ||
                         buster_x86_metadata_string_input_equal(form.iclass.offset, S8("PREFETCHIT1"));
        bool value_zero = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("NOP"));
        if ((!value_one && !value_zero) || pattern.prefetchit_control_value != (u8)value_one) return false;
        if (value_zero) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_fixed_bsrinit_no_zeroing(
    BusterX86MetadataForm form, BusterX86MetadataPatternSemantics pattern)
{
    return buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BSRINIT")) &&
           buster_x86_metadata_string_input_equal(form.extension.offset, S8("ACE")) &&
           buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("ACE_1")) &&
           form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX &&
           form.encoder_family == BUSTER_X86_METADATA_ENCODER_VEX && form.map == BUSTER_X86_METADATA_MAP_0F38 &&
           form.mandatory_prefix == 0xf2 && form.mode_flags == BUSTER_X86_METADATA_MODE_64 &&
           form.field_flags == (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_REGISTER) &&
           form.decorator_flags == 0 && form.apx_flags == 0 && form.amx_flags == 0 &&
           pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX && pattern.has_prefix_kind && pattern.no_rexr_prefix &&
           pattern.zeroing_control == 1 && pattern.has_decorator_control && !pattern.has_bcrc && !pattern.has_ubit &&
           !pattern.mask_control && !pattern.has_sae_control && !pattern.has_rounding_control && !pattern.rounding_length &&
           pattern.opcode_count == 1 && pattern.opcode[0] == 0x49 && pattern.mandatory_prefix == 0xf2 &&
           pattern.vector_length == 128 && pattern.has_explicit_vector_length && pattern.map == BUSTER_X86_METADATA_MAP_0F38 &&
           pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_REGISTER && pattern.reg_fixed == 0 &&
           pattern.rm_fixed == BUSTER_X86_METADATA_PATTERN_FIXED_ANY && pattern.srm_fixed == BUSTER_X86_METADATA_PATTERN_FIXED_ANY &&
           pattern.has_modrm && pattern.explicit_modrm && !pattern.has_sib && !pattern.has_memory && pattern.has_register &&
           !pattern.has_dynamic_opcode && !pattern.has_srm_register && !pattern.has_unsupported_token &&
           !pattern.immediate_count && !pattern.relative_count && !pattern.displacement_count && pattern.has_w && pattern.w == 1 &&
           pattern.mode_control == BUSTER_X86_METADATA_PATTERN_MODE_64 && pattern.no_vector_source && pattern.has_prefix_control;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_ibhf_generic_nop_collision(BusterX86MetadataForm form,
                                                                                BusterX86MetadataPatternSemantics pattern)
{
    // The generic BASE NOP row for F3 0F 1E F8 also admits REX.W.  That
    // spelling is IBHF=1, so retaining the generic row would let a source
    // query select bytes whose architectural meaning changes with IBHF.  The
    // IBHF rows and the explicit no-REX row carry typed controls and are not
    // covered by this predicate.
    return buster_x86_metadata_string_input_equal(form.iclass.offset, S8("NOP")) &&
           buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("PPRO")) &&
           buster_x86_metadata_string_input_equal(form.extension.offset, S8("BASE")) &&
           form.mandatory_prefix == 0xf3 && pattern.opcode_count == 2 && pattern.opcode[0] == 0x0f &&
           pattern.opcode[1] == 0x1e && pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_REGISTER &&
           pattern.reg_fixed == 7 && pattern.rm_fixed == 0 && !pattern.has_w;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_pattern_control_blocker(BusterX86MetadataForm form,
                                                                          BusterX86MetadataPatternSemantics pattern)
{
    // These booleans are intentionally not a recognition-only side channel.
    // A control token is emittable only when the normalized fields below
    // carry the corresponding constraint into the byte path.
    if (!buster_x86_metadata_emit_boolean_control_matches(form, pattern))
        return BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS;
    if (buster_x86_metadata_emit_ibhf_generic_nop_collision(form, pattern))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.has_cet_no_track && !buster_x86_metadata_emit_canonical_cet_no_track(form, pattern))
        return BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS;
    if (pattern.has_prefix_kind && form.prefix_kind != pattern.prefix_kind)
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    // BRANCH_HINT is only implemented for the normalized 64-bit conditional
    // branch rows carrying the legacy/no-REX2 contract.  Keep adjacent
    // not64/policy rows and any future non-relative use blocked rather than
    // treating the token as a generic prefix permission.
    if (pattern.has_branch_hint_control &&
        (!(form.mode_flags & BUSTER_X86_METADATA_MODE_64) || !pattern.no_rex2 || !pattern.has_force64_control ||
         pattern.relative_count != 1 ||
         (pattern.relative_width != 1 && pattern.relative_width != 4)))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.has_ignore_66 && pattern.mandatory_prefix == 0x66)
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.mode_control == BUSTER_X86_METADATA_PATTERN_MODE_16 &&
        !(form.mode_flags & BUSTER_X86_METADATA_MODE_16))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.mode_control == BUSTER_X86_METADATA_PATTERN_MODE_32 &&
        !(form.mode_flags & BUSTER_X86_METADATA_MODE_32))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.mode_control == BUSTER_X86_METADATA_PATTERN_MODE_64 &&
        !(form.mode_flags & BUSTER_X86_METADATA_MODE_64))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.mode_control == BUSTER_X86_METADATA_PATTERN_MODE_NOT64 &&
        !(form.mode_flags & BUSTER_X86_METADATA_MODE_NOT64))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.operand_size_control == BUSTER_X86_METADATA_PATTERN_OPERAND_SIZE_66 && form.mandatory_prefix != 0x66)
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.operand_size_control == BUSTER_X86_METADATA_PATTERN_OPERAND_SIZE_NO66 && form.mandatory_prefix == 0x66)
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.rep_not_f3 && (pattern.mandatory_prefix == 0xf3 || form.mandatory_prefix == 0xf3))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.has_evex_r4 && form.prefix_kind != BUSTER_X86_METADATA_PREFIX_EVEX)
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.no_rexr_prefix && form.prefix_kind != BUSTER_X86_METADATA_PREFIX_VEX &&
        form.prefix_kind != BUSTER_X86_METADATA_PREFIX_XOP && form.prefix_kind != BUSTER_X86_METADATA_PREFIX_EVEX)
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 && pattern.map != BUSTER_X86_METADATA_MAP_LEGACY &&
        pattern.map != BUSTER_X86_METADATA_MAP_0F)
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.forbid_mandatory_prefix && form.mandatory_prefix)
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.has_prefix_control)
    {
        bool prefix_supported = pattern.has_prefix_kind ||
                                 form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX ||
                                 form.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP ||
                                 form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX || pattern.mandatory_prefix || pattern.has_w ||
                                 pattern.relative_count || pattern.has_nd || pattern.has_nf || pattern.has_bcrc || pattern.has_ubit ||
                                 pattern.no_vector_source || pattern.no_scc || pattern.forbid_mandatory_prefix ||
                                 pattern.lock_control || pattern.rep_control || pattern.rep_not_f3 || pattern.no_rex2 ||
                                 pattern.no_rexr_prefix ||
                                 pattern.has_modep5 || pattern.has_rep_selector || pattern.has_segment_override ||
                                 pattern.has_branch_hint_control ||
                                 pattern.has_remove_segment || pattern.rex_b_control || pattern.rex_b4_control ||
                                 pattern.has_p4_control ||
                                 pattern.immune66 || pattern.immune66_loop64 || pattern.immune_rexw || pattern.df64 ||
                                 pattern.has_cet_no_track ||
                                 buster_x86_metadata_emit_canonical_df64(form, pattern) ||
                                 pattern.has_mpx_mode ||
                                 (form.mode_flags & (BUSTER_X86_METADATA_MODE_16 | BUSTER_X86_METADATA_MODE_32 |
                                                     BUSTER_X86_METADATA_MODE_64 | BUSTER_X86_METADATA_MODE_NOT64));
        if (!prefix_supported) return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    }
    if (pattern.has_mpx_mode && !buster_x86_metadata_emit_mpx_mode_supported(form, pattern))
        return BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS;
    if (pattern.has_address_control)
    {
        bool address_supported = pattern.required_address_size || pattern.forbid_address_override || pattern.lock_control ||
                                 pattern.rep_control || pattern.rep_not_f3 ||
                                 (form.mode_flags & (BUSTER_X86_METADATA_MODE_EA16 | BUSTER_X86_METADATA_MODE_EA32 |
                                                     BUSTER_X86_METADATA_MODE_EA64 | BUSTER_X86_METADATA_MODE_EANOT16 |
                                                     BUSTER_X86_METADATA_MODE_16 | BUSTER_X86_METADATA_MODE_32 |
                                                     BUSTER_X86_METADATA_MODE_64 | BUSTER_X86_METADATA_MODE_NOT64));
        if (!address_supported) return BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS;
    }
    // ACE-1's VEX BSRINIT row carries XED's ZEROING=0 selector even though
    // VEX has no zeroing decorator.  Keep this exception tied to the exact
    // architectural shape.  BCRC/UBIT and every EVEX decorator control are
    // checked independently: no future non-EVEX row can inherit their bypass.
    bool fixed_non_evex_no_zeroing = buster_x86_metadata_emit_fixed_bsrinit_no_zeroing(form, pattern);
    if ((pattern.has_bcrc || pattern.has_ubit) && form.prefix_kind != BUSTER_X86_METADATA_PREFIX_EVEX)
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if ((pattern.has_rounding_control || pattern.mask_control || pattern.has_sae_control || pattern.rounding_length) &&
        form.prefix_kind != BUSTER_X86_METADATA_PREFIX_EVEX)
        return BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS;
    if (pattern.zeroing_control && form.prefix_kind != BUSTER_X86_METADATA_PREFIX_EVEX && !fixed_non_evex_no_zeroing)
        return BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS;
    if (pattern.has_decorator_control && form.prefix_kind != BUSTER_X86_METADATA_PREFIX_EVEX && !fixed_non_evex_no_zeroing)
        return BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS;
    if (pattern.has_sae_control && !(form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_SAE))
        return BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS;
    if (pattern.has_rounding_control && !(form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_ROUNDING))
        return BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS;
    if (pattern.has_tuple_control)
    {
        u8 required_tuple_kind = pattern.tuple_control_kind == BUSTER_X86_METADATA_PATTERN_TUPLE_MEM128
                                     ? BUSTER_X86_METADATA_TUPLE_FULL
                                     : pattern.tuple_control_kind;
        if (form.tuple_kind != required_tuple_kind) return BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS;
    }
    if (pattern.has_vsib_control && !(form.field_flags & BUSTER_X86_METADATA_FIELD_VSIB))
        return BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS;
    if (pattern.has_element_size_control && buster_x86_metadata_emit_element_size_bits(form) != pattern.element_size_bits)
        return BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS;
    if (pattern.has_implicit_one && (pattern.immediate_count || !buster_x86_metadata_emit_form_has_implicit_immediate(form)))
        return BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS;
    if (pattern.has_apx_control && !(form.apx_flags & BUSTER_X86_METADATA_APX))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.has_nd && !(form.apx_flags & BUSTER_X86_METADATA_APX_ND))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.has_nf && pattern.nf_value && !(form.apx_flags & BUSTER_X86_METADATA_APX_NF))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.has_scc && (!(form.apx_flags & BUSTER_X86_METADATA_APX_SCC) || pattern.scc_value >= 16))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.no_scc && (form.apx_flags & BUSTER_X86_METADATA_APX_SCC))
        return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    if (pattern.has_tile_control &&
        (form.encoder_family != BUSTER_X86_METADATA_ENCODER_AMX || !form.amx_flags))
        return BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS;
    if (pattern.has_amx_control && form.encoder_family == BUSTER_X86_METADATA_ENCODER_AMX && !form.amx_flags)
        return BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS;
    return BUSTER_X86_METADATA_BLOCKER_NONE;
}

// The nine architectural widths are all multiples of eight up to 1024, so
// `width >> 3` indexes them densely and the switch becomes one load.  Every
// other index holds UNKNOWN, which is exactly what the switch's default
// returned, so a misaligned or out-of-range width lands on it too.
static u16 const buster_x86_metadata_width_flag_table[129] = {
    [8 >> 3] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_8,       [16 >> 3] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_16,
    [32 >> 3] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_32,     [64 >> 3] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64,
    [80 >> 3] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_80,     [128 >> 3] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_128,
    [256 >> 3] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_256,   [512 >> 3] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_512,
    [1024 >> 3] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024,
};

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_emit_width_flags(u16 width)
{
    u32 index = (u32)width >> 3;
    bool indexable = (width & 7) == 0 && index < BUSTER_ARRAY_LENGTH(buster_x86_metadata_width_flag_table);
    u16 flags = indexable ? buster_x86_metadata_width_flag_table[index] : 0;
    return flags ? flags : BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
}

#define BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY 0xFFu
static u8 const buster_x86_metadata_required_physical_kind[16] = {
    [BUSTER_X86_METADATA_OPERAND_NONE] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY,
    [BUSTER_X86_METADATA_OPERAND_REGISTER] = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
    [BUSTER_X86_METADATA_OPERAND_MEMORY] = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
    [BUSTER_X86_METADATA_OPERAND_IMMEDIATE] = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
    [BUSTER_X86_METADATA_OPERAND_RELATIVE] = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
    [BUSTER_X86_METADATA_OPERAND_ABSOLUTE] = BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE,
    [BUSTER_X86_METADATA_OPERAND_BASE] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY,
    [BUSTER_X86_METADATA_OPERAND_SEGMENT] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY,
    [BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR] = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
    [BUSTER_X86_METADATA_OPERAND_PSEUDO] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY,
    [10] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY, [11] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY,
    [12] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY, [13] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY,
    [14] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY, [15] = BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY,
};

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_operand_class_extended(BusterX86MetadataPhysicalOperandKind kind, u8 register_physical_class)
{
    if (kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY) return BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY;
    if (kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE) return BUSTER_X86_METADATA_PHYSICAL_CLASS_IMMEDIATE;
    if (kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE) return BUSTER_X86_METADATA_PHYSICAL_CLASS_RELATIVE;
    if (kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE) return BUSTER_X86_METADATA_PHYSICAL_CLASS_ABSOLUTE;
    return register_physical_class;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_operand_class(BusterX86MetadataPhysicalOperand operand)
{
    return buster_x86_metadata_emit_operand_class_extended(operand.kind, operand.reg.physical_class);
}

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_emit_operand_width(BusterX86MetadataPhysicalOperand operand)
{
    if (operand.width) return operand.width;
    if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER) return operand.reg.width;
    return 0;
}

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_emit_data_operand_width(BusterX86MetadataPhysicalBinding* bindings, u32 binding_count)
{
    for (u32 index = 0; index < binding_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand physical = bindings[index].physical;
        if (physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
            physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            u16 width = buster_x86_metadata_emit_operand_width(physical);
            if (width == 16 || width == 32 || width == 64) return width;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_variable_width(u16 data_width, u8 form_width, bool immediate)
{
    if (data_width == 16) return 2;
    if (data_width == 32) return 4;
    if (data_width == 64)
    {
        if (immediate) return 8;
        return form_width == 8 ? 8 : form_width == 1 || form_width == 2 || form_width == 4 ? form_width : 4;
    }
    return form_width == 1 || form_width == 2 || form_width == 4 || form_width == 8 ? form_width : 0;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_physical_query_valid(BusterX86MetadataPhysicalQuery query)
{
    bool mask_flag = (query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_MASK) != 0;
    bool zeroing_flag = (query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_ZEROING) != 0;
    bool broadcast_flag = (query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST) != 0;
    bool rounding_flag = (query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_ROUNDING) != 0;
    bool sae_flag = (query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_SAE) != 0;
    if (!buster_x86_metadata_query_string_valid(query.mnemonic) ||
        (query.operand_count && !query.operands) || query.operand_count > 16 ||
        (query.address_size != BUSTER_X86_METADATA_ADDRESS_SIZE_ANY && query.address_size != 16 && query.address_size != 32 &&
         query.address_size != 64) ||
        query.execution_mode >= BUSTER_X86_METADATA_EXECUTION_MODE_COUNT ||
        query.attributes.decorator_flags & ~BUSTER_X86_METADATA_DECORATOR_FLAGS_ALL ||
        query.attributes.apx_flags & ~BUSTER_X86_METADATA_APX_FLAGS_ALL ||
        query.attributes.amx_flags & ~BUSTER_X86_METADATA_AMX_FLAGS_ALL ||
        query.attributes.implicit_segment >= BUSTER_X86_METADATA_SEGMENT_COUNT ||
        query.attributes.branch_hint >= BUSTER_X86_METADATA_BRANCH_HINT_COUNT ||
        (query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE &&
         query.attributes.branch_hint != BUSTER_X86_METADATA_BRANCH_HINT_NONE) ||
        // The scalar fields are the typed payload and decorator_flags is the
        // compact resolver-facing projection.  They must agree in both
        // directions; otherwise selection can choose a decorated form while
        // emission silently encodes the default modifier.
        (mask_flag != query.attributes.has_mask_register) ||
        (query.attributes.has_mask_register &&
         (query.attributes.mask_register == 0 || query.attributes.mask_register >= 8)) ||
        (!query.attributes.has_mask_register && query.attributes.mask_register != 0) ||
        (zeroing_flag != query.attributes.zeroing) ||
        (broadcast_flag != (query.attributes.broadcast_elements != 0)) ||
        (rounding_flag != (query.attributes.rounding_mode != BUSTER_X86_METADATA_ROUNDING_NONE)) ||
        (sae_flag != query.attributes.sae) ||
        (query.attributes.zeroing && !(query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_MASK)) ||
        query.attributes.rounding_mode >= BUSTER_X86_METADATA_ROUNDING_COUNT ||
        (query.attributes.rep && query.attributes.repne) ||
        (query.attributes.zeroing && (!query.attributes.has_mask_register || query.attributes.mask_register == 0)) ||
        ((query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_MASK) &&
         (!query.attributes.has_mask_register || query.attributes.mask_register == 0)) ||
        (query.attributes.broadcast_elements > 64) ||
        (query.attributes.has_dfv ? query.attributes.dfv >= 16 : query.attributes.dfv != 0))
        return false;
    bool physical_has_memory = false;
    for (u32 index = 0; index < query.operand_count; index += 1)
    {
        physical_has_memory |= query.operands[index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY;
    }
    if (query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE && physical_has_memory) return false;
    if (broadcast_flag && !physical_has_memory) return false;
    BusterX86MetadataResolveQuery resolver_query = {
        .mnemonic = query.mnemonic,
        .features = query.features,
        // The physical validation above proves broadcast has a memory
        // operand.  The resolver-facing signature query cannot carry that
        // physical shape, so do not make its signature-only memory check
        // reject an otherwise valid physical query.
        .decorator_flags = query.attributes.decorator_flags & (u16)~BUSTER_X86_METADATA_DECORATOR_BROADCAST,
        .apx_flags = query.attributes.apx_flags,
        .amx_flags = query.attributes.amx_flags,
        .address_size = query.address_size,
        .execution_mode = query.execution_mode,
        .include_implicit = query.include_implicit,
        .include_privileged = query.include_privileged,
        .include_not64 = query.include_not64,
    };
    if (!buster_x86_metadata_resolution_query_valid(resolver_query, 0)) return false;
    for (u32 index = 0; index < query.operand_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[index];
        if (operand.kind >= BUSTER_X86_METADATA_PHYSICAL_OPERAND_KIND_COUNT) return false;
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            (operand.reg.physical_class >= BUSTER_X86_METADATA_PHYSICAL_CLASS_COUNT ||
             (operand.width && operand.reg.width && operand.width != operand.reg.width) ||
             (operand.reg.high_byte && (operand.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR ||
                                        operand.reg.width != 8 || operand.reg.index < 4 || operand.reg.index > 7))))
            return false;
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            u8 effective_address_size = operand.memory.address_size ? operand.memory.address_size : query.address_size ? query.address_size : 64;
            if (query.address_size && operand.memory.address_size && query.address_size != operand.memory.address_size) return false;
            if (operand.memory.address_size != BUSTER_X86_METADATA_ADDRESS_SIZE_ANY && operand.memory.address_size != 16 &&
                operand.memory.address_size != 32 && operand.memory.address_size != 64)
                return false;
            if (operand.memory.scale != 0 && operand.memory.scale != 1 && operand.memory.scale != 2 && operand.memory.scale != 4 &&
                operand.memory.scale != 8)
                return false;
            if (operand.memory.segment >= BUSTER_X86_METADATA_SEGMENT_COUNT) return false;
            if (operand.memory.has_segment != (operand.memory.segment != BUSTER_X86_METADATA_SEGMENT_NONE)) return false;
            if ((operand.memory.has_base && operand.memory.base.physical_class >= BUSTER_X86_METADATA_PHYSICAL_CLASS_COUNT) ||
                (operand.memory.has_index && operand.memory.index.physical_class >= BUSTER_X86_METADATA_PHYSICAL_CLASS_COUNT))
                return false;
            if (operand.memory.has_base && operand.memory.base.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR) return false;
            if (operand.memory.has_index && !operand.memory.vsib && operand.memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR)
                return false;
            if (operand.memory.vsib && !operand.memory.has_index) return false;
            if (operand.memory.has_index && !operand.memory.vsib && operand.memory.index.index == 4) return false;
            if (operand.memory.has_base &&
                (operand.memory.base.high_byte || operand.memory.base.width != effective_address_size))
                return false;
            if (operand.memory.has_index)
            {
                if (operand.memory.index.high_byte) return false;
                if (operand.memory.vsib)
                {
                    u16 expected_width = operand.memory.index.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM
                                             ? 128
                                             : operand.memory.index.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM ? 256 : 512;
                    if ((operand.memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM &&
                         operand.memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM &&
                         operand.memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM) ||
                        operand.memory.index.width != expected_width)
                        return false;
                }
                else if (operand.memory.index.width != effective_address_size)
                    return false;
            }
            if (operand.has_symbol || operand.symbol.pointer || operand.symbol.length || operand.has_value || operand.has_unsigned_value ||
                (operand.memory.symbol.pointer != 0) != (operand.memory.symbol.length != 0))
                return false;
            if (operand.memory.has_symbol != (operand.memory.symbol.pointer != 0 && operand.memory.symbol.length != 0)) return false;
        }
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE ||
                 operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE ||
                 operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE)
        {
            if ((operand.symbol.pointer != 0) != (operand.symbol.length != 0)) return false;
            bool has_symbol = operand.has_symbol;
            bool has_symbol_text = operand.symbol.pointer != 0 && operand.symbol.length != 0;
            bool has_literal = operand.has_value || operand.has_unsigned_value;
            if (has_symbol != has_symbol_text || (has_symbol && has_literal) || (!has_symbol && !has_literal) ||
                (operand.has_value && operand.has_unsigned_value))
                return false;
            if ((operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE ||
                 operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE) &&
                operand.width != 0 && operand.width != 8 && operand.width != 16 && operand.width != 32 && operand.width != 64)
                return false;
            // Immediate width is a semantic operand width (the encoded field
            // width comes from the selected UIMM/SIMM schema), but retaining
            // the same bit-width vocabulary prevents ambiguous requests.
            if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE && operand.width != 0 && operand.width != 8 &&
                operand.width != 16 && operand.width != 32 && operand.width != 64)
                return false;
        }
        if (operand.has_symbol && !operand.symbol.pointer)
            return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_operand_matches(BusterX86MetadataOperand metadata,
                                                                    BusterX86MetadataPhysicalOperand physical,
                                                                    BusterX86MetadataPatternSemantics pattern)
{
    u8 actual_class = buster_x86_metadata_emit_operand_class(physical);
    u8 metadata_kind = metadata.kind;
    BusterX86MetadataPhysicalOperandKind physical_kind = physical.kind;
    u16 width = buster_x86_metadata_emit_operand_width(physical);
    u8 metadata_physical_class = metadata.physical_class;
    u8 physical_reg_physical_class = physical.reg.physical_class;
    u16 physical_width = physical.width;
    u16 metadata_physical_width_flags = metadata.physical_width_flags;
    u8 pattern_relative_width = pattern.relative_width;
    u16 width_flags = buster_x86_metadata_emit_width_flags(width);
    bool physical_has_symbol = physical.has_symbol;

    bool address_generator = metadata_kind == BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR;
    // Five of the rejects below said the same thing: a metadata kind that
    // names one physical kind rejects every other one.  XED's AGEN source
    // operand (address-only forms such as LEA) is the sixth row: the public
    // physical API spells that ModRM/SIB address as a memory operand, so it
    // requires MEMORY exactly like OPERAND_MEMORY does, which is why the old
    // body tested it twice.  `kind_pair_any` marks the kinds that constrain
    // nothing.  The index is masked rather than bounds-checked because the
    // table covers the whole 4-bit range the kind enum can occupy.
    u8 required_physical_kind = buster_x86_metadata_required_physical_kind[metadata_kind & 15];
    u32 kind_pair_reject = (u32)(required_physical_kind != BUSTER_X86_METADATA_REQUIRED_PHYSICAL_KIND_ANY) &
                           (u32)(physical_kind != required_physical_kind);
    bool b0 = (!address_generator && metadata_physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE && metadata_physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN && metadata_physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY && metadata_physical_class != actual_class);
    bool vector_register = physical_kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER && (physical_reg_physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM || physical_reg_physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM || physical_reg_physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM);
    bool variable_encoded_width = physical_kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE || physical_kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE || physical_kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE;
    bool b1 = ((metadata_kind == BUSTER_X86_METADATA_OPERAND_RELATIVE || metadata_kind == BUSTER_X86_METADATA_OPERAND_ABSOLUTE) && physical_width && pattern_relative_width && physical_width != (u16)pattern_relative_width * 8);
    // Some imported vector forms annotate a vector register with its scalar
    // element width (for example the AVX2 gather rows), while other rows use
    // the architectural vector width.  The physical class already carries
    // the architectural width; scalar memory operands retain the element
    // width check below.  A MASK register is another architectural-width
    // class: its k-register is always 64 bits, while a MASK_B/N/R schema may
    // append an element qualifier (for example `mskw:u8`).  That suffix is
    // an element width, not a request for an 8-bit physical register.  Keep
    // the architectural class and width validation, but do not compare the
    // element qualifier with the physical k-register width.
    bool architectural_mask_register = physical_kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER && physical_reg_physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK;
    // A symbolic immediate carries an explicit requested field width.  Keep
    // that width authoritative during source selection: otherwise PUSH's
    // imm32 spelling is indistinguishable from its shorter imm8 sibling and
    // the shortest-form tie breaker silently truncates the relocation field.
    // Numeric immediates remain variable-width so value fitting can select the
    // canonical shortest encoding.
    bool b2 = (metadata_kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE && physical_has_symbol && width && metadata_physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN && metadata_physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY && !(metadata_physical_width_flags & width_flags));
    bool b3 = (architectural_mask_register && width && width != 64);
    bool b4 = (!address_generator && !architectural_mask_register && !vector_register && !variable_encoded_width && width && metadata_physical_width_flags && metadata_physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN && !(metadata_physical_width_flags & width_flags));

    return !((kind_pair_reject | (u32)b0) | ((u32)b1 | (u32)b2) | ((u32)b3 | (u32)b4));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_fixed_register_matches(BusterX86MetadataOperand metadata,
                                                                          BusterX86MetadataPhysicalOperand physical);
BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_effective_field_source(BusterX86MetadataOperand metadata);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_x87_operand(BusterX86MetadataOperand metadata);
BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_effective_field_source_pattern(BusterX86MetadataOperand metadata,
                                                                                 BusterX86MetadataPatternSemantics pattern);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_atom_contains(BusterX86MetadataString atom, String8 needle);
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_atom_equal(BusterX86MetadataString atom, String8 literal);

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_explicit_fixed_implicit_operand(
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataForm form, BusterX86MetadataOperand metadata, u32 actual_index)
{
    // Most implicit operands are architectural defaults and remain omitted
    // from the physical query. ACE-1's BSRMOV direction is the one typed
    // exception: consume only the raw XED_REG_BSR0 identity at its topology
    // position. Other fixed implicit registers (for example an AL accumulator)
    // remain non-source-spellable when include_implicit is false.
    bool explicit_bsr0 = metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED &&
                         buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_BSR0"));
    // Shift/rotate-by-CL rows expose CL as an implicit XED operand even
    // though source spellings (and codegen physical queries) carry it
    // explicitly.  Project that one fixed register when the caller supplies
    // the architectural CL identity; an omitted CL remains the ordinary
    // implicit form.
    bool explicit_cl = buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_CL"));
    // X87 two-register source spellings expose ST0 as the architectural
    // fixed operand even though XED stores it as hidden.  Consume it when
    // the caller preserves the pair; a one-register source query continues
    // to omit ST0 and selects the ordinary ST0-destination form.
    bool explicit_x87_st0 = query.operand_count == 2 &&
                            buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST0"));
    // FNSTSW's register spelling exposes the architectural AX destination in
    // source syntax even though XED stores it as a fixed implicit operand.
    // Consume that one fixed AX when callers query `fnstsw ax`; memory forms
    // and ordinary hidden accumulators remain implicit.
    bool explicit_fnstsw_ax = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("FNSTSW")) &&
                              buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_AX"));
    return !query.include_implicit && !metadata.visible &&
           (!query.source_semantics || form.operand_count > 1) &&
           metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && actual_index < query.operand_count &&
           (explicit_bsr0 || explicit_cl || explicit_x87_st0 || explicit_fnstsw_ax) &&
           buster_x86_metadata_emit_fixed_register_matches(metadata, query.operands[actual_index]);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_explicit_implicit_one_operand(
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataForm form, BusterX86MetadataOperand metadata,
    BusterX86MetadataPatternSemantics pattern, u32 actual_index)
{
    if (query.include_implicit || metadata.visible || metadata.kind != BUSTER_X86_METADATA_OPERAND_IMMEDIATE ||
        !(metadata.access & BUSTER_X86_METADATA_ACCESS_IMPLICIT) || !pattern.has_implicit_one ||
        (query.source_semantics && form.operand_count <= 1) || actual_index >= query.operand_count)
        return false;
    BusterX86MetadataPhysicalOperand physical = query.operands[actual_index];
    if (physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE) return false;
    if (physical.has_unsigned_value) return physical.unsigned_value == 1;
    return physical.has_value && physical.value == 1;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_writemask_operand(BusterX86MetadataOperand metadata)
{
    if (metadata.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK) return false;
    // MASK_R/MASK_N/MASK_B are architectural k operands (for example the
    // three data operands of KADDW).  Only the unsuffixed MASK1/MASKNOT0
    // records are the implicit EVEX aaa decorator field.
    return !buster_x86_metadata_emit_atom_contains(metadata.atom, S8("_R")) &&
           !buster_x86_metadata_emit_atom_contains(metadata.atom, S8("_N")) &&
           !buster_x86_metadata_emit_atom_contains(metadata.atom, S8("_B"));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_architectural_mask_binding(BusterX86MetadataPhysicalBinding binding)
{
    return binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
           binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
           !(binding.prepared_flags_valid
                 ? (binding.prepared_flags & BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_WRITEMASK)
                 : buster_x86_metadata_emit_is_writemask_operand(binding.metadata));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_bind_form(BusterX86MetadataPhysicalQuery query, BusterX86MetadataForm const* form_pointer,
                                                              BusterX86MetadataPhysicalBinding* bindings, u32* binding_count,
                                                              u32* diagnostic_operand,
                                                              BusterX86MetadataExactPlanRecord const* plan)
{
#define form (*form_pointer)
    BusterX86MetadataPatternSemantics pattern_storage = {0};
    BusterX86MetadataPatternSemantics const* pattern_view = plan ? plan->pattern : 0;
    bool pattern_valid = false;
    if (plan)
    {
        pattern_valid = pattern_view != 0;
    }
    else
    {
        // Borrow the prewarmed parse where it exists; parsing into local
        // storage copies the whole record on every emitted instruction.
        pattern_view = buster_x86_metadata_pattern_semantics_borrow(form, &pattern_valid);
        if (!pattern_view)
        {
            pattern_valid = buster_x86_metadata_emit_parse_pattern(form, &pattern_storage);
            pattern_view = &pattern_storage;
        }
    }
    // Keep the generic implementation below expressed in terms of the
    // value-like `pattern` name while letting prepared plans borrow the
    // immutable cached object instead of copying it on every emission.
#define pattern (*pattern_view)
    if (plan && plan->exact_bind_simple)
    {
        if (!pattern_valid || query.operand_count != plan->operand_count)
        {
            if (diagnostic_operand)
                *diagnostic_operand = query.operand_count < plan->operand_count ? query.operand_count : plan->operand_count;
            return false;
        }
        for (u32 operand_index = 0; operand_index < plan->operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand metadata = plan->operands[operand_index];
            BusterX86MetadataPhysicalOperand physical = query.operands[operand_index];
            if (!buster_x86_metadata_emit_operand_matches(metadata, physical, pattern) ||
                !buster_x86_metadata_emit_fixed_register_matches(metadata, physical) ||
                (plan->effective_field_sources[operand_index] == BUSTER_X86_METADATA_FIELD_SOURCE_MASK &&
                 query.attributes.has_mask_register && physical.reg.index != query.attributes.mask_register))
            {
                if (diagnostic_operand) *diagnostic_operand = operand_index;
                return false;
            }
            bindings[operand_index] = (BusterX86MetadataPhysicalBinding){
                .metadata = metadata,
                .physical = physical,
                .has_physical = true,
                .actual_index = (u8)operand_index,
                .prepared_flags = 0,
                .prepared_flags_valid = true,
                .effective_field_source = plan->effective_field_sources[operand_index],
                .effective_field_source_valid = true,
            };
        }
        *binding_count = plan->operand_count;
        return true;
    }
    BusterX86MetadataFormFacts const* facts = plan ? 0 : buster_x86_metadata_form_facts_for(form);
    // The same simple binding shape, for callers that hold no prepared plan.
    // The operand views and compact per-generated-operand facts are already
    // prewarmed, so this reads them back rather than reserving sixteen source
    // slots in every form.  The pattern-aware source is the same spelling an
    // exact plan stores in its own `effective_field_sources`.
    if (facts && (facts->shape_flags & BUSTER_X86_METADATA_FORM_FACT2_BIND_SIMPLE))
    {
        if (!pattern_valid || query.operand_count != facts->operand_count)
        {
            if (diagnostic_operand)
                *diagnostic_operand = query.operand_count < facts->operand_count ? query.operand_count : facts->operand_count;
            return false;
        }
        for (u32 operand_index = 0; operand_index < facts->operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand metadata = {0};
            if (!buster_x86_metadata_operand(form.id, operand_index, &metadata)) return false;
            BusterX86MetadataFormOperandFacts const* operand_facts =
                &buster_x86_metadata_form_operand_facts[form.operand_first + operand_index];
            BusterX86MetadataPhysicalOperand physical = query.operands[operand_index];
            if (!buster_x86_metadata_emit_operand_matches(metadata, physical, pattern) ||
                !buster_x86_metadata_emit_fixed_register_matches(metadata, physical) ||
                (operand_facts->pattern_field_source == BUSTER_X86_METADATA_FIELD_SOURCE_MASK &&
                 query.attributes.has_mask_register && physical.reg.index != query.attributes.mask_register))
            {
                if (diagnostic_operand) *diagnostic_operand = operand_index;
                return false;
            }
            bindings[operand_index] = (BusterX86MetadataPhysicalBinding){
                .metadata = metadata,
                .physical = physical,
                .has_physical = true,
                .actual_index = (u8)operand_index,
                .prepared_flags = 0,
                .prepared_flags_valid = true,
                .effective_field_source = operand_facts->pattern_field_source,
                .effective_field_source_valid = true,
            };
        }
        *binding_count = facts->operand_count;
        return true;
    }
    bool moffs_form = pattern_valid && (plan   ? plan->moffs_form
                                        : facts ? (facts->flags & BUSTER_X86_METADATA_FORM_FACT_MOFFS) != 0
                                                : buster_x86_metadata_emit_is_moffs(form, pattern));
    bool maskmov_form = pattern_valid && (plan   ? plan->maskmov_form
                                          : facts ? (facts->flags & BUSTER_X86_METADATA_FORM_FACT_MASKMOV) != 0
                                                  : buster_x86_metadata_emit_is_maskmov(form, pattern));
    bool moffs_source_accumulator = pattern_valid &&
                                    buster_x86_metadata_emit_moffs_source_accumulator(query, form, pattern);
    u32 actual_index = 0;
    u32 count = 0;
    bool form_vsib = (form.field_flags & BUSTER_X86_METADATA_FIELD_VSIB) != 0;
    bool has_vsib_default = false;
    BusterX86MetadataPhysicalRegister vsib_default = {0};
    if (form_vsib)
    {
        for (u32 query_index = 0; query_index < query.operand_count; query_index += 1)
        {
            BusterX86MetadataPhysicalOperand candidate = query.operands[query_index];
            if (candidate.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && candidate.memory.vsib &&
                candidate.memory.has_index)
            {
                has_vsib_default = true;
                vsib_default = candidate.memory.index;
                break;
            }
        }
    }
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        u8 prepared_flags = 0;
        u8 prepared_flags_valid = plan ? 1 : 0;
        if (plan)
        {
            if (operand_index >= plan->operand_count) return false;
            metadata = plan->operands[operand_index];
            prepared_flags = plan->operand_flags[operand_index];
        }
        else if (!buster_x86_metadata_operand(form.id, operand_index, &metadata))
        {
            return false;
        }
        BusterX86MetadataFormOperandFacts const* operand_facts =
            facts ? &buster_x86_metadata_form_operand_facts[form.operand_first + operand_index] : 0;
        if (pattern_valid && !plan)
            metadata.field_source = facts ? operand_facts->pattern_field_source
                                          : buster_x86_metadata_emit_effective_field_source_pattern(metadata, pattern);
        u8 effective_field_source = plan   ? plan->effective_field_sources[operand_index]
                                    : facts ? operand_facts->effective_field_source
                                            : buster_x86_metadata_emit_effective_field_source(metadata);
        bool moffs_fixed_accumulator = moffs_source_accumulator &&
                                       (prepared_flags_valid
                                            ? (prepared_flags & BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MOFFS_FIXED_ACCUMULATOR)
                                            : ((metadata.kind == BUSTER_X86_METADATA_OPERAND_BASE && !metadata.visible) ||
                                               (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && !metadata.visible &&
                                                metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED)));
        bool moffs_supplemental = prepared_flags_valid
                                      ? (prepared_flags & BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MOFFS_SUPPLEMENTAL) != 0
                                      : buster_x86_metadata_emit_is_moffs_supplemental(form, pattern, metadata);
        bool maskmov_supplemental = prepared_flags_valid
                                       ? (prepared_flags & BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MASKMOV_SUPPLEMENTAL) != 0
                                       : buster_x86_metadata_emit_is_maskmov_supplemental(form, pattern, metadata);
        if (moffs_form && moffs_supplemental && !moffs_fixed_accumulator) continue;
        if (maskmov_form && maskmov_supplemental) continue;
        bool explicit_fixed_implicit = !query.include_implicit && !metadata.visible && !moffs_fixed_accumulator &&
                                       ((!query.source_semantics || form.operand_count > 1) &&
                                        (prepared_flags_valid
                                             ? (prepared_flags & BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_FIXED_BSR0) != 0 &&
                                                   actual_index < query.operand_count &&
                                                   buster_x86_metadata_emit_fixed_register_matches(metadata, query.operands[actual_index])
                                             : buster_x86_metadata_emit_explicit_fixed_implicit_operand(query, form, metadata, actual_index)));
        bool explicit_implicit_one = !prepared_flags_valid && pattern_valid &&
                                     buster_x86_metadata_emit_explicit_implicit_one_operand(query, form, metadata, pattern, actual_index);
        explicit_fixed_implicit = explicit_fixed_implicit || explicit_implicit_one;
        if (!query.include_implicit && !metadata.visible && !moffs_fixed_accumulator && !explicit_fixed_implicit) continue;
        bool writemask_operand = prepared_flags_valid
                                     ? (prepared_flags & BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_WRITEMASK) != 0
                                     : buster_x86_metadata_emit_is_writemask_operand(metadata);
        bool mask_default = writemask_operand &&
                            (actual_index >= query.operand_count ||
                             buster_x86_metadata_emit_operand_class(query.operands[actual_index]) != BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK);
        BusterX86MetadataPhysicalOperand physical = {0};
        u8 has_physical = 0;
        u8 consumed_index = (u8)actual_index;
        if (mask_default)
        {
            physical.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER;
            physical.reg = (BusterX86MetadataPhysicalRegister){
                .index = query.attributes.has_mask_register ? query.attributes.mask_register : 0,
                .width = 64,
                .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK,
            };
            has_physical = 1;
        }
        else if (form_vsib && has_vsib_default && metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
                 effective_field_source == BUSTER_X86_METADATA_FIELD_SOURCE_VVVV &&
                 (actual_index >= query.operand_count ||
                  query.operands[actual_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY))
        {
            // XED exposes the VSIB index as a visible register operand, while
            // the physical API carries it in memory.index.  Accept that
            // canonical physical representation without manufacturing a
            // second operand; an explicitly supplied register is still
            // consumed and checked below.
            physical.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER;
            physical.width = vsib_default.width;
            physical.reg = vsib_default;
            has_physical = 1;
        }
        else if (actual_index < query.operand_count &&
                 buster_x86_metadata_emit_operand_matches(metadata, query.operands[actual_index], pattern_valid ? pattern :
                                                                                                              (BusterX86MetadataPatternSemantics){0}))
        {
            physical = query.operands[actual_index];
            actual_index += 1;
            has_physical = 1;
        }
        if (!has_physical)
        {
            if (diagnostic_operand) *diagnostic_operand = operand_index;
            return false;
        }
        if (!buster_x86_metadata_emit_fixed_register_matches(metadata, physical))
        {
            if (diagnostic_operand) *diagnostic_operand = operand_index;
            return false;
        }
        bool x87_operand = prepared_flags_valid
                               ? (prepared_flags & BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_X87) != 0
                               : buster_x86_metadata_emit_is_x87_operand(metadata);
        if (x87_operand &&
            (physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER || physical.reg.width != 80 ||
             physical.reg.index >= 8))
        {
            if (diagnostic_operand) *diagnostic_operand = operand_index;
            return false;
        }
        if (effective_field_source == BUSTER_X86_METADATA_FIELD_SOURCE_MASK &&
            query.attributes.has_mask_register && physical.reg.index != query.attributes.mask_register)
        {
            if (diagnostic_operand) *diagnostic_operand = operand_index;
            return false;
        }
        if (count >= 16) return false;
        bindings[count++] = (BusterX86MetadataPhysicalBinding){
            .metadata = metadata,
            .physical = physical,
            .has_physical = has_physical,
            .actual_index = consumed_index,
            .prepared_flags = prepared_flags,
            .prepared_flags_valid = prepared_flags_valid,
            .effective_field_source = effective_field_source,
            .effective_field_source_valid = true,
        };
    }
    if (actual_index != query.operand_count)
    {
        if (diagnostic_operand) *diagnostic_operand = actual_index;
        return false;
    }
    *binding_count = count;
#undef pattern
    return true;
#undef form
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_form_operand_count(BusterX86MetadataPhysicalQuery query,
                                                                      BusterX86MetadataForm form, u32* count)
{
    // A simple-binding row exposes every metadata operand and has no moffs,
    // maskmov, VSIB or writemask handling, so nothing below can skip or
    // default an operand and the count is just the row's own.  Selection calls
    // this for every candidate it considers, and the general path re-parses the
    // pattern and re-derives those facts per operand to reach the same answer.
    bool pattern_valid_borrowed = false;
    BusterX86MetadataFormFacts const* simple_facts = buster_x86_metadata_form_facts_for(form);
    if (simple_facts && (simple_facts->shape_flags & BUSTER_X86_METADATA_FORM_FACT2_BIND_SIMPLE) &&
        !query.include_implicit)
    {
        *count = form.operand_count;
        return true;
    }
    BusterX86MetadataPatternSemantics pattern_storage = {0};
    BusterX86MetadataPatternSemantics const* pattern_view = buster_x86_metadata_pattern_semantics_borrow(form, &pattern_valid_borrowed);
    bool pattern_valid = false;
    if (pattern_view)
    {
        pattern_valid = pattern_valid_borrowed;
    }
    else
    {
        pattern_valid = buster_x86_metadata_emit_parse_pattern(form, &pattern_storage);
        pattern_view = &pattern_storage;
    }
#define pattern (*pattern_view)
    bool moffs_form = pattern_valid && buster_x86_metadata_emit_is_moffs(form, pattern);
    bool maskmov_form = pattern_valid && buster_x86_metadata_emit_is_maskmov(form, pattern);
    bool moffs_source_accumulator = pattern_valid &&
                                    buster_x86_metadata_emit_moffs_source_accumulator(query, form, pattern);
    u32 result = 0;
    u32 actual_index = 0;
    bool form_vsib = (form.field_flags & BUSTER_X86_METADATA_FIELD_VSIB) != 0;
    bool has_vsib_default = false;
    if (form_vsib)
    {
        for (u32 query_index = 0; query_index < query.operand_count; query_index += 1)
        {
            BusterX86MetadataPhysicalOperand candidate = query.operands[query_index];
            if (candidate.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && candidate.memory.vsib &&
                candidate.memory.has_index)
            {
                has_vsib_default = true;
                break;
            }
        }
    }
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &metadata)) return false;
        if (pattern_valid)
            metadata.field_source = buster_x86_metadata_emit_effective_field_source_pattern(metadata, pattern);
        bool moffs_fixed_accumulator = moffs_source_accumulator &&
                                       ((metadata.kind == BUSTER_X86_METADATA_OPERAND_BASE && !metadata.visible) ||
                                        (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && !metadata.visible &&
                                         metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED));
        if (moffs_form && buster_x86_metadata_emit_is_moffs_supplemental(form, pattern, metadata) && !moffs_fixed_accumulator) continue;
        if (maskmov_form && buster_x86_metadata_emit_is_maskmov_supplemental(form, pattern, metadata)) continue;
        if (!query.include_implicit && !metadata.visible && !moffs_fixed_accumulator &&
            !buster_x86_metadata_emit_explicit_fixed_implicit_operand(query, form, metadata, actual_index) &&
            !(pattern_valid && buster_x86_metadata_emit_explicit_implicit_one_operand(query, form, metadata, pattern, actual_index)))
            continue;
        bool mask_default = buster_x86_metadata_emit_is_writemask_operand(metadata) &&
                            (actual_index >= query.operand_count ||
                             buster_x86_metadata_emit_operand_class(query.operands[actual_index]) !=
                                 BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK);
        bool vsib_index_default = form_vsib && has_vsib_default && metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
                                  buster_x86_metadata_emit_effective_field_source(metadata) == BUSTER_X86_METADATA_FIELD_SOURCE_VVVV &&
                                  (actual_index >= query.operand_count ||
                                   query.operands[actual_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY);
        if (!mask_default && !vsib_index_default)
        {
            result += 1;
            actual_index += 1;
        }
    }
    *count = result;
    return true;
#undef pattern
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_atom_contains(BusterX86MetadataString atom, String8 needle)
{
    if (!needle.length || atom.length < needle.length) return false;
    String8 span = buster_x86_metadata_string_span(atom);
    if (span.length < needle.length) return false;
    for (u32 offset = 0; offset + needle.length <= span.length; offset += 1)
    {
        bool equal = true;
        for (u32 index = 0; index < needle.length; index += 1)
        {
            if (span.pointer[offset + index] != needle.pointer[index])
            {
                equal = false;
                break;
            }
        }
        if (equal) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_effective_field_source(BusterX86MetadataOperand metadata)
{
    // Fixed architectural names may contain the same `_B`/`_N` spelling
    // used by XED's vector field annotations (BSR0 is the `_B` example).
    // Preserve an explicit fixed source before applying those generic atom
    // suffix heuristics.
    if (metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED)
        return BUSTER_X86_METADATA_FIELD_SOURCE_FIXED;
    // MOV moffs uses OrAX() for its hidden accumulator operand.  The atom is
    // a fixed architectural choice even though the generated field source is
    // REG; keep this exception closed to that one hidden register spelling.
    if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && !metadata.visible &&
        buster_x86_metadata_emit_atom_equal(metadata.atom, S8("OrAX()")))
        return BUSTER_X86_METADATA_FIELD_SOURCE_FIXED;
    if (buster_x86_metadata_emit_atom_contains(metadata.atom, S8("_B"))) return BUSTER_X86_METADATA_FIELD_SOURCE_RM;
    if (buster_x86_metadata_emit_atom_contains(metadata.atom, S8("_N"))) return BUSTER_X86_METADATA_FIELD_SOURCE_VVVV;
    if (metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
        buster_x86_metadata_emit_atom_contains(metadata.atom, S8("_R")))
        return BUSTER_X86_METADATA_FIELD_SOURCE_REG;
    if (metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK) return BUSTER_X86_METADATA_FIELD_SOURCE_MASK;
    if (metadata.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE) return BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE;
    if (metadata.kind == BUSTER_X86_METADATA_OPERAND_RELATIVE) return BUSTER_X86_METADATA_FIELD_SOURCE_RELATIVE;
    return metadata.field_source;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_x87_operand(BusterX86MetadataOperand metadata)
{
    if (metadata.kind != BUSTER_X86_METADATA_OPERAND_REGISTER ||
        metadata.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL)
        return false;
    return buster_x86_metadata_emit_atom_equal(metadata.atom, S8("X87()")) ||
           buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST0")) ||
           buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST1")) ||
           buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST2")) ||
           buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST3")) ||
           buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST4")) ||
           buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST5")) ||
           buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST6")) ||
           buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST7"));
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_effective_field_source_pattern(BusterX86MetadataOperand metadata,
                                                                                 BusterX86MetadataPatternSemantics pattern)
{
    u8 field_source = buster_x86_metadata_emit_effective_field_source(metadata);
    // XED describes x87 stack operands as REG even though the encoding keeps
    // the fixed ST(0) selector in ModRM.reg and carries the visible ST(i) in
    // ModRM.rm.  Infer that topology from the pattern, rather than naming
    // individual x87 instructions.
    if (buster_x86_metadata_emit_is_x87_operand(metadata) &&
        field_source == BUSTER_X86_METADATA_FIELD_SOURCE_REG &&
        !buster_x86_metadata_emit_reg_is_unconstrained(pattern) &&
        pattern.rm_fixed == BUSTER_X86_METADATA_PATTERN_FIXED_ANY)
        field_source = BUSTER_X86_METADATA_FIELD_SOURCE_RM;
    return field_source;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_atom_equal(BusterX86MetadataString atom, String8 literal)
{
    if (atom.length != literal.length || (!literal.pointer && literal.length)) return false;
    String8 span = buster_x86_metadata_string_span(atom);
    if (span.length != atom.length) return false;
    for (u32 index = 0; index < span.length; index += 1)
    {
        if ((u8)span.pointer[index] != (u8)literal.pointer[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_moffs(BusterX86MetadataForm form,
                                                            BusterX86MetadataPatternSemantics pattern)
{
    // The source snapshot uses MEMDISPv() for the four legacy MOV moffs
    // forms.  Keep this predicate deliberately closed: MASKMOV has an
    // implicit DI memory operand and must not inherit moffs' absolute-offset
    // path merely because it also carries OVERRIDE_SEG0().
    return buster_x86_metadata_string_input_equal(form.iclass.offset, S8("MOV")) && pattern.opcode_count == 1 &&
           pattern.opcode[0] >= 0xa0 && pattern.opcode[0] <= 0xa3 && pattern.has_memory && !pattern.has_modrm &&
           !pattern.has_sib && pattern.displacement_count == 1 && !pattern.immediate_count && !pattern.relative_count;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_maskmov(BusterX86MetadataForm form,
                                                              BusterX86MetadataPatternSemantics pattern)
{
    // MASKMOVQ/MASKMOVDQU encode two visible vector registers in REG/RM
    // while XED also exposes the architectural destination [DI] as hidden
    // MEM0/BASE0/SEG0 records.  The latter are encoding semantics only: the
    // source spelling has no explicit memory operand and MOD=3 selects the
    // register-register form.
    bool maskmov_q = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("MASKMOVQ"));
    bool maskmov_dqu = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("MASKMOVDQU"));
    if ((!maskmov_q && !maskmov_dqu) || pattern.opcode_count != 2 || pattern.opcode[0] != 0x0f || pattern.opcode[1] != 0xf7 ||
        pattern.map != BUSTER_X86_METADATA_MAP_0F || !pattern.has_modrm || pattern.mod_kind != BUSTER_X86_METADATA_PATTERN_MOD_REGISTER ||
        pattern.reg_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY || pattern.rm_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY ||
        pattern.has_sib || pattern.displacement_count || pattern.immediate_count || pattern.relative_count ||
        pattern.trailing_count || !pattern.has_segment_override || pattern.segment_override_index != 0)
        return false;
    if (maskmov_q != (pattern.mandatory_prefix == 0)) return false;
    if (maskmov_dqu != (pattern.mandatory_prefix == 0x66)) return false;
    if (form.operand_count != 5 || !(form.field_flags & BUSTER_X86_METADATA_FIELD_MODRM) ||
        !(form.field_flags & BUSTER_X86_METADATA_FIELD_MEMORY) || !(form.field_flags & BUSTER_X86_METADATA_FIELD_REGISTER))
        return false;
    u32 visible_register_count = 0;
    bool hidden_memory = false;
    bool hidden_base = false;
    bool hidden_segment = false;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &metadata)) return false;
        if (metadata.visible)
        {
            if (metadata.kind != BUSTER_X86_METADATA_OPERAND_REGISTER) return false;
            visible_register_count += 1;
        }
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY && buster_x86_metadata_emit_atom_equal(metadata.atom, S8("MEM0")))
            hidden_memory = true;
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_BASE && buster_x86_metadata_emit_atom_equal(metadata.atom, S8("ArDI()")))
            hidden_base = true;
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_SEGMENT &&
                 buster_x86_metadata_emit_atom_equal(metadata.atom, S8("FINAL_DSEG()")))
            hidden_segment = true;
        else
            return false;
    }
    return visible_register_count == 2 && hidden_memory && hidden_base && hidden_segment;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_maskmov_supplemental(BusterX86MetadataForm form,
                                                                           BusterX86MetadataPatternSemantics pattern,
                                                                           BusterX86MetadataOperand metadata)
{
    if (!buster_x86_metadata_emit_is_maskmov(form, pattern) || metadata.visible) return false;
    return (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY && buster_x86_metadata_emit_atom_equal(metadata.atom, S8("MEM0"))) ||
           (metadata.kind == BUSTER_X86_METADATA_OPERAND_BASE && buster_x86_metadata_emit_atom_equal(metadata.atom, S8("ArDI()"))) ||
           (metadata.kind == BUSTER_X86_METADATA_OPERAND_SEGMENT &&
            buster_x86_metadata_emit_atom_equal(metadata.atom, S8("FINAL_DSEG()")));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_is_moffs_supplemental(BusterX86MetadataForm form,
                                                                         BusterX86MetadataPatternSemantics pattern,
                                                                         BusterX86MetadataOperand metadata)
{
    return buster_x86_metadata_emit_is_moffs(form, pattern) &&
           (metadata.kind == BUSTER_X86_METADATA_OPERAND_BASE || metadata.kind == BUSTER_X86_METADATA_OPERAND_SEGMENT ||
            metadata.kind == BUSTER_X86_METADATA_OPERAND_PSEUDO ||
            (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && !metadata.visible &&
             metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_moffs_source_accumulator(BusterX86MetadataPhysicalQuery query,
                                                                            BusterX86MetadataForm form,
                                                                            BusterX86MetadataPatternSemantics pattern)
{
    // The public physical API normally supplies visible operands only.  The
    // assembly front door also retains the explicitly written accumulator for
    // MOV moffs, so bind exactly one extra register to the schema's fixed BASE
    // operand while leaving the ordinary metadata query contract unchanged.
    if (!query.source_semantics || !buster_x86_metadata_emit_is_moffs(form, pattern) || query.operand_count != 2 || !query.operands)
        return false;
    return (query.operands[0].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            query.operands[1].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY) ||
           (query.operands[0].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
            query.operands[1].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER);
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_segment_prefix(u8 segment)
{
    switch (segment)
    {
    case BUSTER_X86_METADATA_SEGMENT_ES: return 0x26;
    case BUSTER_X86_METADATA_SEGMENT_CS: return 0x2e;
    case BUSTER_X86_METADATA_SEGMENT_SS: return 0x36;
    case BUSTER_X86_METADATA_SEGMENT_DS: return 0x3e;
    case BUSTER_X86_METADATA_SEGMENT_FS: return 0x64;
    case BUSTER_X86_METADATA_SEGMENT_GS: return 0x65;
    default: return 0;
    }
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_canonical_segment_override(BusterX86MetadataForm form,
                                                                                BusterX86MetadataPatternSemantics pattern)
{
    if (buster_x86_metadata_emit_is_maskmov(form, pattern))
    {
        // MASKMOV's destination address is hidden DI with the default data
        // segment. MEM0/BASE0/SEG0 are supplemental records, so a typed
        // implicit-segment attribute carries an optional source override.
        return true;
    }
    bool string_category = buster_x86_metadata_string_input_equal(form.category.offset, S8("STRINGOP")) ||
                           buster_x86_metadata_string_input_equal(form.category.offset, S8("IOSTRINGOP"));
    bool xlat = buster_x86_metadata_string_input_equal(form.category.offset, S8("MISC")) &&
                buster_x86_metadata_string_input_equal(form.iclass.offset, S8("XLAT"));
    bool string_opcode = pattern.opcode_count == 1 &&
                         ((pattern.opcode[0] >= 0xA4 && pattern.opcode[0] <= 0xA7) ||
                          (pattern.opcode[0] >= 0xAA && pattern.opcode[0] <= 0xAF) ||
                          (pattern.opcode[0] >= 0x6C && pattern.opcode[0] <= 0x6F));
    if (!buster_x86_metadata_emit_is_moffs(form, pattern) && ((!string_category || !string_opcode) && !xlat)) return false;
    if (buster_x86_metadata_emit_is_moffs(form, pattern))
    {
        // moffs has no ModRM/SIB or trailing fields; its one displacement is
        // written directly after the opcode by the byte emitter below.
        return !pattern.trailing_count && !pattern.has_modrm && !pattern.has_sib && pattern.displacement_count == 1 &&
               !pattern.immediate_count && !pattern.relative_count;
    }
    if (pattern.trailing_count || pattern.has_modrm || pattern.has_sib || pattern.displacement_count ||
        pattern.immediate_count || pattern.relative_count || !pattern.has_memory)
        return false;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &operand) || operand.visible) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_canonical_cet_no_track(BusterX86MetadataForm form,
                                                                           BusterX86MetadataPatternSemantics pattern)
{
    // CET_NO_TRACK is present on exactly the indirect near CALL/JMP legacy
    // rows.  Keep this shape check independent of generated row order so an
    // unrelated future use cannot become source-selectable without an
    // explicit schema decision.
    bool call_form = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("CALL_NEAR")) &&
                     buster_x86_metadata_string_input_equal(form.category.offset, S8("CALL")) && pattern.reg_fixed == 2;
    bool jmp_form = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("JMP")) &&
                    buster_x86_metadata_string_input_equal(form.category.offset, S8("UNCOND_BR")) && pattern.reg_fixed == 4;
    if (!call_form && !jmp_form) return false;
    if (form.prefix_kind != BUSTER_X86_METADATA_PREFIX_LEGACY || form.encoder_family != BUSTER_X86_METADATA_ENCODER_LEGACY ||
        form.map != BUSTER_X86_METADATA_MAP_LEGACY || form.mandatory_prefix || pattern.opcode_count != 1 ||
        pattern.opcode[0] != 0xff || pattern.trailing_count || pattern.has_sib || !pattern.has_modrm ||
        pattern.has_dynamic_opcode || pattern.immediate_count || pattern.relative_count || pattern.displacement_count ||
        pattern.has_unsupported_token)
        return false;
    bool memory_shape = pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_MEMORY;
    bool register_shape = pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_REGISTER;
    if ((!memory_shape && !register_shape) || pattern.has_memory != memory_shape) return false;
    u32 visible_count = 0;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &operand)) return false;
        if (!operand.visible) continue;
        visible_count += 1;
        if (visible_count != 1) return false;
        if (memory_shape ? operand.kind != BUSTER_X86_METADATA_OPERAND_MEMORY
                         : operand.kind != BUSTER_X86_METADATA_OPERAND_REGISTER)
            return false;
    }
    return visible_count == 1;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_canonical_hidden_segment_override(
    BusterX86MetadataForm form, BusterX86MetadataPatternSemantics pattern)
{
    // Moffs rows carry a visible absolute memory operand.  Their segment
    // prefix is emitted from PhysicalMemory.segment and must never be
    // supplied through the hidden-operand attribute.
    if (buster_x86_metadata_emit_is_moffs(form, pattern)) return false;
    return buster_x86_metadata_emit_canonical_segment_override(form, pattern);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_canonical_df64(BusterX86MetadataForm form,
                                                                  BusterX86MetadataPatternSemantics pattern)
{
    return pattern.df64 && buster_x86_metadata_string_input_equal(form.iclass.offset, S8("ENTER")) &&
           buster_x86_metadata_string_input_equal(form.category.offset, S8("MISC")) && pattern.opcode_count == 1 &&
           pattern.opcode[0] == 0xc8 && pattern.immediate_count == 2 && pattern.immediate_widths[0] == 2 &&
           pattern.immediate_widths[1] == 1 && buster_x86_metadata_emit_string_has(form.attributes, S8("ATT_OPERAND_ORDER_EXCEPTION"));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_atom_prefix(BusterX86MetadataString atom, String8 prefix)
{
    if (atom.length <= prefix.length) return false;
    String8 span = buster_x86_metadata_string_span(atom);
    if (span.length < prefix.length) return false;
    for (u32 index = 0; index < prefix.length; index += 1)
    {
        if ((u8)span.pointer[index] != (u8)prefix.pointer[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_fixed_register_matches(BusterX86MetadataOperand metadata,
                                                                          BusterX86MetadataPhysicalOperand physical)
{
    if (metadata.field_source != BUSTER_X86_METADATA_FIELD_SOURCE_FIXED &&
        !buster_x86_metadata_emit_atom_contains(metadata.atom, S8("XED_REG_")))
        return true;
    if (physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER) return false;
    if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("OrAX()")))
    {
        return physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && physical.reg.index == 0 &&
               (physical.reg.width == 16 || physical.reg.width == 32 || physical.reg.width == 64) && !physical.reg.high_byte;
    }
    u8 expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN;
    u16 expected_index = UINT16_MAX;
    u16 expected_width = BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
    bool expected_high_byte = false;
    if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_AL")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 0;
        expected_width = 8;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_CL")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 1;
        expected_width = 8;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_DL")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 2;
        expected_width = 8;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_BL")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 3;
        expected_width = 8;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_AH")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 4;
        expected_width = 8;
        expected_high_byte = true;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_CH")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 5;
        expected_width = 8;
        expected_high_byte = true;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_DH")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 6;
        expected_width = 8;
        expected_high_byte = true;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_BH")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 7;
        expected_width = 8;
        expected_high_byte = true;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_SPL")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 4;
        expected_width = 8;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_BPL")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 5;
        expected_width = 8;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_SIL")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 6;
        expected_width = 8;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_DIL")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 7;
        expected_width = 8;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_AX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 0;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_CX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 1;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_DX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 2;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_BX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 3;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_SP")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 4;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_BP")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 5;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_SI")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 6;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_DI")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 7;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_EAX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 0;
        expected_width = 32;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ECX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 1;
        expected_width = 32;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_EDX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 2;
        expected_width = 32;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_EBX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 3;
        expected_width = 32;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ESP")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 4;
        expected_width = 32;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_EBP")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 5;
        expected_width = 32;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ESI")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 6;
        expected_width = 32;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_EDI")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 7;
        expected_width = 32;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_RAX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 0;
        expected_width = 64;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_RCX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 1;
        expected_width = 64;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_RDX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 2;
        expected_width = 64;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_RBX")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 3;
        expected_width = 64;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_RSP")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 4;
        expected_width = 64;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_RBP")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 5;
        expected_width = 64;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_RSI")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 6;
        expected_width = 64;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_RDI")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        expected_index = 7;
        expected_width = 64;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ES")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
        expected_index = 0;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_CS")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
        expected_index = 1;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_SS")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
        expected_index = 2;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_DS")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
        expected_index = 3;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_FS")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
        expected_index = 4;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_GS")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
        expected_index = 5;
        expected_width = 16;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST0")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        expected_index = 0;
        expected_width = 80;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST1")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        expected_index = 1;
        expected_width = 80;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST2")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        expected_index = 2;
        expected_width = 80;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST3")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        expected_index = 3;
        expected_width = 80;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST4")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        expected_index = 4;
        expected_width = 80;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST5")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        expected_index = 5;
        expected_width = 80;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST6")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        expected_index = 6;
        expected_width = 80;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_ST7")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        expected_index = 7;
        expected_width = 80;
    }
    else if (buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_BSR0")))
    {
        expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL;
        expected_index = 0;
        expected_width = 64;
    }
    if (expected_index == UINT16_MAX)
    {
        // XED spells the extended fixed GPRs as R8/R8D/R8W/R8B through
        // R31/... .  Keep the identity check exact without a mnemonic table
        // exception for each architectural register.
        String8 gpr_prefix = S8("XED_REG_R");
        String8 atom_span = buster_x86_metadata_string_span(metadata.atom);
        bool prefix_match = metadata.atom.length > gpr_prefix.length && atom_span.length == metadata.atom.length;
        u32 prefix_index = 0;
        for (; prefix_match && prefix_index < gpr_prefix.length; prefix_index += 1)
            prefix_match &= (u8)atom_span.pointer[prefix_index] == (u8)gpr_prefix.pointer[prefix_index];
        if (prefix_match)
        {
            u32 parsed = 0;
            u32 digit_count = 0;
            u32 index = (u32)gpr_prefix.length;
            for (; index < metadata.atom.length; index += 1)
            {
                u8 character = (u8)atom_span.pointer[index];
                if (character < '0' || character > '9') break;
                u32 digit = (u32)character - (u32)'0';
                if (parsed > (UINT16_MAX - digit) / 10u) break;
                parsed = parsed * 10u + digit;
                digit_count += 1;
            }
            if (digit_count && index <= metadata.atom.length)
            {
                u16 width = 64;
                if (index + 1 == metadata.atom.length)
                {
                    u8 suffix = (u8)atom_span.pointer[index];
                    width = suffix == 'D' ? 32 : suffix == 'W' ? 16 : suffix == 'B' ? 8 : 0;
                }
                else if (index != metadata.atom.length)
                    width = 0;
                if (width)
                {
                    expected_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
                    expected_index = (u16)parsed;
                    expected_width = width;
                }
            }
        }
    }
    if (expected_index == UINT16_MAX)
    {
        String8 prefix = {0};
        u8 prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN;
        u16 prefix_width = BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
        if (buster_x86_metadata_emit_atom_prefix(metadata.atom, S8("XED_REG_XMM")))
        {
            prefix = S8("XED_REG_XMM");
            prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM;
            prefix_width = 128;
        }
        else if (buster_x86_metadata_emit_atom_prefix(metadata.atom, S8("XED_REG_YMM")))
        {
            prefix = S8("XED_REG_YMM");
            prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM;
            prefix_width = 256;
        }
        else if (buster_x86_metadata_emit_atom_prefix(metadata.atom, S8("XED_REG_ZMM")))
        {
            prefix = S8("XED_REG_ZMM");
            prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM;
            prefix_width = 512;
        }
        else if (buster_x86_metadata_emit_atom_prefix(metadata.atom, S8("XED_REG_K")))
        {
            prefix = S8("XED_REG_K");
            prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK;
            prefix_width = 64;
        }
        else if (buster_x86_metadata_emit_atom_prefix(metadata.atom, S8("XED_REG_TMM")))
        {
            prefix = S8("XED_REG_TMM");
            prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM;
            prefix_width = 1024;
        }
        else if (buster_x86_metadata_emit_atom_prefix(metadata.atom, S8("XED_REG_MMX")))
        {
            prefix = S8("XED_REG_MMX");
            prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX;
            prefix_width = 64;
        }
        else if (buster_x86_metadata_emit_atom_prefix(metadata.atom, S8("XED_REG_BND")))
        {
            prefix = S8("XED_REG_BND");
            prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_BND;
            prefix_width = 128;
        }
        else if (buster_x86_metadata_emit_atom_prefix(metadata.atom, S8("XED_REG_CR")))
        {
            prefix = S8("XED_REG_CR");
            prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL;
            prefix_width = 64;
        }
        else if (buster_x86_metadata_emit_atom_prefix(metadata.atom, S8("XED_REG_DR")))
        {
            prefix = S8("XED_REG_DR");
            prefix_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG;
            prefix_width = 64;
        }
        if (prefix.length)
        {
            u32 parsed = 0;
            String8 digits_span = buster_x86_metadata_string_span(metadata.atom);
            for (u32 index = (u32)prefix.length; index < metadata.atom.length; index += 1)
            {
                u8 character = index < digits_span.length ? (u8)digits_span.pointer[index] : 0;
                if (character < '0' || character > '9')
                {
                    parsed = UINT32_MAX;
                    break;
                }
                u32 digit = (u32)character - (u32)'0';
                if (parsed > (UINT16_MAX - digit) / 10u)
                {
                    parsed = UINT32_MAX;
                    break;
                }
                parsed = parsed * 10u + digit;
            }
            if (parsed != UINT32_MAX)
            {
                expected_class = prefix_class;
                expected_index = (u16)parsed;
                expected_width = prefix_width;
            }
        }
    }
    if (expected_index == UINT16_MAX) return false;
    return physical.reg.physical_class == expected_class && physical.reg.index == expected_index &&
           physical.reg.width == expected_width && physical.reg.high_byte == expected_high_byte;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalBinding* buster_x86_metadata_emit_field_binding(
    BusterX86MetadataPhysicalBinding* bindings, u32 binding_count, u8 field_source)
{
    for (u32 index = 0; index < binding_count; index += 1)
    {
        u8 binding_field_source = bindings[index].effective_field_source_valid
                                      ? bindings[index].effective_field_source
                                      : buster_x86_metadata_emit_effective_field_source(bindings[index].metadata);
        if (binding_field_source != field_source || !bindings[index].has_physical)
            continue;
        if ((field_source == BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE &&
             bindings[index].physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE) ||
            (field_source == BUSTER_X86_METADATA_FIELD_SOURCE_RELATIVE &&
             bindings[index].physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE))
            continue;
        return bindings + index;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalBinding* buster_x86_metadata_emit_nth_immediate_binding(
    BusterX86MetadataPhysicalBinding* bindings, u32 binding_count, u32 ordinal)
{
    u32 found = 0;
    for (u32 index = 0; index < binding_count; index += 1)
    {
        u8 binding_field_source = bindings[index].effective_field_source_valid
                                      ? bindings[index].effective_field_source
                                      : buster_x86_metadata_emit_effective_field_source(bindings[index].metadata);
        if (!bindings[index].has_physical || binding_field_source != BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE ||
            bindings[index].physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE)
            continue;
        if (found == ordinal) return bindings + index;
        found += 1;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalBinding* buster_x86_metadata_emit_memory_binding(
    BusterX86MetadataPhysicalBinding* bindings, u32 binding_count)
{
    for (u32 index = 0; index < binding_count; index += 1)
    {
        if (bindings[index].has_physical && bindings[index].physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
            return bindings + index;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_form_iclass_equal(BusterX86MetadataForm form, String8 iclass)
{
    return buster_x86_metadata_emit_atom_equal(form.iclass, iclass);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_operand_width_is_variable(BusterX86MetadataOperand metadata)
{
    u16 flags = metadata.physical_width_flags & (BUSTER_X86_METADATA_PHYSICAL_WIDTH_8 |
                                                  BUSTER_X86_METADATA_PHYSICAL_WIDTH_16 |
                                                  BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 |
                                                  BUSTER_X86_METADATA_PHYSICAL_WIDTH_64 |
                                                  BUSTER_X86_METADATA_PHYSICAL_WIDTH_80 |
                                                  BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 |
                                                  BUSTER_X86_METADATA_PHYSICAL_WIDTH_256 |
                                                  BUSTER_X86_METADATA_PHYSICAL_WIDTH_512 |
                                                  BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024);
    return flags != 0 && (flags & (u16)(flags - 1)) != 0;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_legacy_mmx_memory_shape(
    BusterX86MetadataForm form, BusterX86MetadataPhysicalBinding* bindings, u32 binding_count)
{
    String8 isa_set = buster_x86_metadata_string_span(form.isa_set);
    if (form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED ||
        form.encoder_family != BUSTER_X86_METADATA_ENCODER_LEGACY ||
        form.field_flags != (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_MEMORY |
                             BUSTER_X86_METADATA_FIELD_REGISTER) ||
        form.decorator_flags || form.apx_flags || form.amx_flags ||
        !(buster_x86_metadata_input_string_equal(isa_set, S8("PENTIUMMMX")) ||
          buster_x86_metadata_input_string_equal(isa_set, S8("SSE2MMX"))) ||
        !bindings ||
        binding_count != 2)
        return false;
    bool has_mmx_destination = false;
    bool has_memory_source = false;
    for (u32 index = 0; index < binding_count; index += 1)
    {
        BusterX86MetadataPhysicalBinding binding = bindings[index];
        if (!binding.has_physical || !binding.metadata.visible || binding.metadata.slot != 0)
            return false;
        if (binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            binding.metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
            binding.metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX &&
            binding.metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_REG &&
            binding.metadata.access == (BUSTER_X86_METADATA_ACCESS_READ | BUSTER_X86_METADATA_ACCESS_WRITE) &&
            binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX &&
            binding.physical.reg.width == 64)
        {
            if (has_mmx_destination) return false;
            has_mmx_destination = true;
        }
        else if (binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
                 binding.metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY &&
                 binding.metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY &&
                 binding.metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_RM &&
                 binding.metadata.access == BUSTER_X86_METADATA_ACCESS_READ &&
                 !binding.physical.memory.vsib && !binding.physical.memory.has_segment)
        {
            if (has_memory_source) return false;
            has_memory_source = true;
        }
        else
        {
            return false;
        }
    }
    return has_mmx_destination && has_memory_source;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_form_operand_semantics(BusterX86MetadataForm form,
                                                                            BusterX86MetadataPhysicalBinding* bindings,
                                                                            u32 binding_count)
{
    BusterX86MetadataPatternSemantics pattern = {0};
    if (!buster_x86_metadata_emit_parse_pattern(form, &pattern) || pattern.unresolved_blocker)
    {
        return false;
    }
    bool apx_fixed_width_no_w = buster_x86_metadata_emit_apx_fixed_width_no_w(form, pattern);
    bool is_movzx = buster_x86_metadata_emit_form_iclass_equal(form, S8("MOVZX"));
    bool is_movsx = buster_x86_metadata_emit_form_iclass_equal(form, S8("MOVSX"));
    bool is_movsxd = buster_x86_metadata_emit_form_iclass_equal(form, S8("MOVSXD"));
    bool is_bswap = buster_x86_metadata_emit_form_iclass_equal(form, S8("BSWAP"));
    bool is_vcmp = buster_x86_metadata_emit_atom_prefix(form.iclass, S8("VCMP"));
    bool is_vpermil2 = buster_x86_metadata_emit_form_iclass_equal(form, S8("VPERMIL2PS")) ||
                       buster_x86_metadata_emit_form_iclass_equal(form, S8("VPERMIL2PD"));
    bool is_lwpins = buster_x86_metadata_emit_form_iclass_equal(form, S8("LWPINS"));
    u16 common_width = 0;
    u32 unresolved_memory_count = 0;
    bool has_vector_register = false;
    u16 data_width[2] = {0};
    u32 data_width_count = 0;
    u16 lwpins_width[2] = {0};
    u32 lwpins_width_count = 0;
    // Memory bindings can precede the vector register they semantically
    // depend on (EVEX scatter is the canonical example).  Establish this
    // order-independent fact only for a bound VSIB memory operand; ordinary
    // unsized scalar memory retains the existing source-order validation.
    bool has_bound_vsib_memory = false;
    u32 vsib_memory_index = 0;
    for (; vsib_memory_index < binding_count; vsib_memory_index += 1)
    {
        BusterX86MetadataPhysicalBinding binding = bindings[vsib_memory_index];
        if (binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
            binding.physical.memory.vsib)
        {
            has_bound_vsib_memory = true;
            break;
        }
    }
    if (has_bound_vsib_memory)
    {
        u32 vsib_vector_index = 0;
        for (; vsib_vector_index < binding_count; vsib_vector_index += 1)
        {
            BusterX86MetadataPhysicalBinding binding = bindings[vsib_vector_index];
            if (binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                (binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM ||
                 binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM ||
                 binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM))
            {
                has_vector_register = true;
                break;
            }
        }
    }
    bool legacy_mmx_memory_shape = buster_x86_metadata_emit_legacy_mmx_memory_shape(form, bindings, binding_count);
    for (u32 index = 0; index < binding_count; index += 1)
    {
        BusterX86MetadataPhysicalBinding binding = bindings[index];
        if (binding.physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            binding.physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            if (is_vcmp && binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE)
            {
                if ((binding.physical.has_unsigned_value && binding.physical.unsigned_value > 31) ||
                    (binding.physical.has_value && (binding.physical.value < 0 || binding.physical.value > 31)))
                {
                    return false;
                }
            }
            if (is_vpermil2 && binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE &&
                ((binding.physical.has_unsigned_value && binding.physical.unsigned_value > 15) ||
                 (binding.physical.has_value && (binding.physical.value < 0 || binding.physical.value > 15))))
            {
                return false;
            }
            continue;
        }
        if (binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            (binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM ||
             binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM ||
             binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM))
        {
            has_vector_register = true;
        }
        if (binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && binding.physical.memory.source_width > 64)
        {
            u16 tuple_memory_width = buster_x86_metadata_emit_tuple_memory_width(pattern);
            if (tuple_memory_width && binding.physical.memory.source_width != tuple_memory_width) return false;
            u16 source_width_flags = buster_x86_metadata_emit_width_flags(binding.physical.memory.source_width);
            u16 schema_width_flags = binding.metadata.physical_width_flags;
            u16 aggregate_schema_flags = schema_width_flags &
                                          (BUSTER_X86_METADATA_PHYSICAL_WIDTH_80 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 |
                                           BUSTER_X86_METADATA_PHYSICAL_WIDTH_256 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_512 |
                                           BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024);
            bool tuple_width_match = tuple_memory_width && binding.physical.memory.source_width == tuple_memory_width;
            if (!source_width_flags ||
                (aggregate_schema_flags ? !(aggregate_schema_flags & source_width_flags)
                                        : !tuple_width_match &&
                                              (!pattern.vector_length || binding.physical.memory.source_width != pattern.vector_length) &&
                                              !(apx_fixed_width_no_w && binding.physical.memory.source_width == 512)))
            {
                return false;
            }
        }
        bool data_operand = binding.metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR ||
                            binding.metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY;
        if (!data_operand)
        {
            continue;
        }
        bool variable_width = buster_x86_metadata_emit_operand_width_is_variable(binding.metadata);
        u16 width = buster_x86_metadata_emit_operand_width(binding.physical);
        if (!width && legacy_mmx_memory_shape && binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
            width = 64;
        if (!width && binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
            binding.physical.memory.source_width > 64)
        {
            u16 aggregate_schema_flags = binding.metadata.physical_width_flags &
                                          (BUSTER_X86_METADATA_PHYSICAL_WIDTH_80 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 |
                                           BUSTER_X86_METADATA_PHYSICAL_WIDTH_256 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_512 |
                                           BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024);
            if (aggregate_schema_flags & buster_x86_metadata_emit_width_flags(binding.physical.memory.source_width))
            {
                // An aggregate memory qualifier (for example xmmword ptr)
                // carries the source width separately from the scalar
                // encoding width.  Treat a schema-approved aggregate width
                // as resolved for source operand semantics; the encoder still
                // uses the metadata fields and never encodes this width as a
                // scalar register width.
                width = binding.physical.memory.source_width;
            }
        }
        if (!width)
        {
            if (binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
                binding.metadata.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN &&
                binding.metadata.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY)
            {
                u16 aggregate_schema_flags = binding.metadata.physical_width_flags &
                                              (BUSTER_X86_METADATA_PHYSICAL_WIDTH_80 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 |
                                               BUSTER_X86_METADATA_PHYSICAL_WIDTH_256 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_512 |
                                               BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024);
                if (!variable_width && !has_vector_register && !aggregate_schema_flags && !apx_fixed_width_no_w)
                {
                    return false;
                }
                if (!aggregate_schema_flags && !apx_fixed_width_no_w) unresolved_memory_count += 1;
            }
            continue;
        }
        if (variable_width &&
            ((binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
              binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR) ||
             binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY))
        {
            if (common_width && common_width != width)
            {
                if (!is_movzx && !is_movsx && !is_movsxd)
                {
                    return false;
                }
            }
            else if (!common_width)
            {
                common_width = width;
            }
        }
        if ((is_movzx || is_movsx || is_movsxd) && data_width_count < BUSTER_ARRAY_LENGTH(data_width))
        {
            data_width[data_width_count++] = width;
        }
        if (is_lwpins && lwpins_width_count < BUSTER_ARRAY_LENGTH(lwpins_width))
        {
            lwpins_width[lwpins_width_count++] = width;
        }
        if (is_bswap && binding.physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            binding.physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && width != 32 && width != 64)
        {
            return false;
        }
    }
    if (unresolved_memory_count && !common_width && !has_vector_register)
    {
        return false;
    }
    if (is_movzx || is_movsx || is_movsxd)
    {
        if (data_width_count != 2)
        {
            return false;
        }
        if (is_movsxd)
        {
            if (data_width[0] != 64 || data_width[1] != 32)
            {
                return false;
            }
        }
        else if (data_width[0] <= data_width[1])
        {
            return false;
        }
    }
    if (is_lwpins && (lwpins_width_count != 2 || lwpins_width[0] != 64 || lwpins_width[1] != 32))
    {
        return false;
    }
    if (form.encoder_family == BUSTER_X86_METADATA_ENCODER_AMX || buster_x86_metadata_emit_atom_prefix(form.iclass, S8("TDP")))
    {
        for (u32 left = 0; left < binding_count; left += 1)
        {
            if (bindings[left].physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
                bindings[left].physical.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM)
            {
                continue;
            }
            for (u32 right = left + 1; right < binding_count; right += 1)
            {
                if (bindings[right].physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                    bindings[right].physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM &&
                    bindings[left].physical.reg.index == bindings[right].physical.reg.index)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

// Indirect CALL/JMP rows carry the architectural stack and instruction-pointer
// effects as suppressed metadata operands.  Those effects are not source
// operands, and their normalized pattern contains XED control tokens that do
// not have a source-width proof.  Keep source semantics authoritative for the
// visible GPR while projecting only this exact hidden-control topology out of
// the semantic-width pass.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_hidden_control_projection(BusterX86MetadataPhysicalQuery query,
                                                                              BusterX86MetadataForm form)
{
    bool call_form = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("CALL")) ||
                     buster_x86_metadata_string_input_equal(form.iclass.offset, S8("CALL_NEAR"));
    bool jmp_form = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("JMP"));
    if (query.include_implicit || query.operand_count != 1 || (!call_form && !jmp_form))
        return false;
    BusterX86MetadataPhysicalOperand visible = query.operands[0];
    if (visible.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
        visible.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR ||
        form.prefix_kind != BUSTER_X86_METADATA_PREFIX_REX2)
    {
        // CET_NO_TRACK near-indirect JMP rows carry a suppressed RIP
        // operand in the normalized form.  The source-visible operand is
        // solely the memory address; project only the architectural /4 row
        // and leave the ordinary /5 indirect JMP row on the normal path.
        if (visible.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY || !jmp_form ||
            form.prefix_kind != BUSTER_X86_METADATA_PREFIX_REX2 || form.operand_count != 2)
            return false;
        BusterX86MetadataPatternSemantics pattern = {0};
        if (!buster_x86_metadata_emit_parse_pattern(form, &pattern) || !pattern.has_modrm || pattern.reg_fixed != 4 ||
            !pattern.opcode_count || pattern.opcode[0] != 0xff)
            return false;
        return true;
    }
    // The REX2 indirect rows are the only CALL/JMP forms with one visible
    // GPR and suppressed stack/RIP effects.  Their normalized operand counts
    // are stable shape evidence (CALL: visible+stack+RIP, JMP: visible+RIP);
    // avoid re-reading the suppressed XED atoms here because those records
    // intentionally have no source-width proof.
    return (call_form && form.operand_count == 3) || (jmp_form && form.operand_count == 2);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_df64_operands_match(BusterX86MetadataPatternSemantics pattern,
                                                                        BusterX86MetadataPhysicalBinding* bindings,
                                                                        u32 binding_count)
{
    if (!pattern.df64) return true;
    // DF64 gives variable GPR/memory operands a 64-bit default in long mode.
    // The legacy stack forms have no 32-bit encoding: 16-bit remains an
    // explicit operand-size override, while a 32-bit physical operand is a
    // contradictory query and must not fall through to the generic width
    // heuristic.
    for (u32 index = 0; index < binding_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand physical = bindings[index].physical;
        u8 physical_class = buster_x86_metadata_emit_operand_class(physical);
        if (physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
            physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY)
            continue;
        if (!pattern.has_w && buster_x86_metadata_emit_operand_width(physical) == 32) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_write_byte(BusterX86MetadataEncodeScratch* scratch, u8 value)
{
    if (scratch->byte_count >= BUSTER_ARRAY_LENGTH(scratch->bytes)) return false;
    scratch->bytes[scratch->byte_count++] = value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_write_le(BusterX86MetadataEncodeScratch* scratch, u64 value, u8 width)
{
    if (width) scratch->value_field_count += 1;
    for (u8 index = 0; index < width; index += 1)
    {
        if (!buster_x86_metadata_emit_write_byte(scratch, (u8)(value >> (index * 8)))) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_relocation(BusterX86MetadataEncodeScratch* scratch, String8 symbol, u8 kind,
                                                               u8 width, s64 addend)
{
    if (scratch->relocation_count >= BUSTER_ARRAY_LENGTH(scratch->relocations)) return false;
    scratch->relocations[scratch->relocation_count++] = (BusterX86MetadataRelocation){
        .offset = scratch->byte_count,
        .width = width,
        .kind = kind,
        .addend = addend,
        .symbol = symbol,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_register_valid(BusterX86MetadataPhysicalRegister reg,
                                                                  BusterX86MetadataEncoderFamily family)
{
    u16 limit = family == BUSTER_X86_METADATA_ENCODER_REX2 || family == BUSTER_X86_METADATA_ENCODER_EVEX ? 32 : 16;
    switch (reg.physical_class)
    {
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR:
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM:
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM:
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM:
        return reg.index < limit;
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK: return reg.index < 8;
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM:
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX: return reg.index < 8;
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_BND: return reg.index < 4;
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL:
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG: return reg.index < 16;
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT: return reg.index < 6;
    default: return true;
    }
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_any_high_byte(BusterX86MetadataPhysicalBinding* bindings, u32 binding_count)
{
    for (u32 index = 0; index < binding_count; index += 1)
    {
        if (bindings[index].has_physical && bindings[index].physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            bindings[index].physical.reg.high_byte)
            return true;
    }
    return false;
}

typedef struct BusterX86MetadataAddressEncoding BusterX86MetadataAddressEncoding;
struct BusterX86MetadataAddressEncoding
{
    u8 mod;
    u8 rm;
    u8 sib;
    u8 displacement_width;
    u16 base_index;
    u16 index_index;
    s64 displacement;
    bool has_sib;
    bool rip_relative;
    bool has_symbol;
    bool address32_absolute;
    u8 reserved[5];
};

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_string_has(BusterX86MetadataString string, String8 needle)
{
    if (!needle.length || string.length < needle.length) return false;
    String8 span = buster_x86_metadata_string_span(string);
    if (span.length < needle.length) return false;
    for (u32 offset = 0; offset + needle.length <= span.length; offset += 1)
    {
        bool equal = true;
        for (u32 index = 0; index < needle.length; index += 1)
        {
            if (span.pointer[offset + index] != needle.pointer[index])
            {
                equal = false;
                break;
            }
        }
        if (equal) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_iform_requires_dfv(BusterX86MetadataForm form)
{
    // DFV is a normalized XED operand role.  The checked-in generated operand
    // rows intentionally omit it, so use the role-bearing iform as the
    // schema source rather than recognizing CCMP/CTEST mnemonics.
    return buster_x86_metadata_emit_string_has(form.iform, S8("_DFV_"));
}

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_emit_element_size_bits(BusterX86MetadataForm form)
{
    if (buster_x86_metadata_emit_string_has(form.element_size, S8("128_BITS"))) return 128;
    if (buster_x86_metadata_emit_string_has(form.element_size, S8("64_BITS"))) return 64;
    if (buster_x86_metadata_emit_string_has(form.element_size, S8("32_BITS"))) return 32;
    if (buster_x86_metadata_emit_string_has(form.element_size, S8("16_BITS"))) return 16;
    if (buster_x86_metadata_emit_string_has(form.element_size, S8("8_BITS"))) return 8;
    if (buster_x86_metadata_emit_string_has(form.element_size, S8("4_BITS"))) return 4;
    return 8;
}

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_emit_tuple_memory_width(BusterX86MetadataPatternSemantics pattern)
{
    if (!pattern.has_tuple_control || !pattern.vector_length) return 0;
    if (pattern.tuple_control_kind == BUSTER_X86_METADATA_PATTERN_TUPLE_MEM128) return 128;
    u16 divisor = 0;
    switch (pattern.tuple_control_kind)
    {
    case BUSTER_X86_METADATA_TUPLE_FULL: divisor = 1; break;
    case BUSTER_X86_METADATA_TUPLE_HALF: divisor = 2; break;
    case BUSTER_X86_METADATA_TUPLE_QUARTER: divisor = 4; break;
    case BUSTER_X86_METADATA_TUPLE_EIGHTH: divisor = 8; break;
    default: return 0;
    }
    if (!divisor || pattern.vector_length % divisor) return 0;
    return (u16)(pattern.vector_length / divisor);
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_tuple_scale(BusterX86MetadataForm const* form,
                                                              BusterX86MetadataPatternSemantics const* pattern)
{
    if (!form->displacement_scale) return 1;
    if (pattern->tuple_control_kind == BUSTER_X86_METADATA_PATTERN_TUPLE_MEM128) return 16;
    u16 element_bits = pattern->has_element_size_control ? pattern->element_size_bits
                                                         : buster_x86_metadata_emit_element_size_bits(*form);
    u32 element = element_bits && element_bits % 8 == 0 ? element_bits / 8 : 0;
    if (pattern->vector_length && pattern->vector_length % 8) return 0;
    u32 vector = pattern->vector_length ? pattern->vector_length / 8 : 16;
    u32 result = 0;
    switch (form->tuple_kind)
    {
    case BUSTER_X86_METADATA_TUPLE_FULL: result = vector; break;
    case BUSTER_X86_METADATA_TUPLE_HALF: result = vector / 2; break;
    case BUSTER_X86_METADATA_TUPLE_QUARTER: result = vector / 4; break;
    case BUSTER_X86_METADATA_TUPLE_EIGHTH: result = vector / 8; break;
    case BUSTER_X86_METADATA_TUPLE_SCALAR:
    case BUSTER_X86_METADATA_TUPLE_TUPLE1: result = element; break;
    case BUSTER_X86_METADATA_TUPLE_TUPLE1_4X: result = element * 4; break;
    case BUSTER_X86_METADATA_TUPLE_TUPLE1_BYTE: result = 1; break;
    case BUSTER_X86_METADATA_TUPLE_TUPLE1_WORD: result = 2; break;
    case BUSTER_X86_METADATA_TUPLE_TUPLE2: result = element * 2; break;
    case BUSTER_X86_METADATA_TUPLE_TUPLE4: result = element * 4; break;
    case BUSTER_X86_METADATA_TUPLE_TUPLE8: result = element * 8; break;
    default: result = 1; break;
    }
    if (!result || result > UINT8_MAX) return 0;
    return (u8)result;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_broadcast_elements(BusterX86MetadataForm form,
                                                                     BusterX86MetadataPatternSemantics pattern)
{
    // broadcast_elements is the architectural 1-to-N count, not merely the
    // EVEX.b boolean.  The compact row must provide both a vector length and
    // an element size before this API can claim a count.
    if (!(form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST) || !pattern.vector_length)
        return 0;
    if (pattern.tuple_control_kind == BUSTER_X86_METADATA_TUPLE_SCALAR) return 1;
    u16 element_size_bits = pattern.has_element_size_control ? pattern.element_size_bits
                                                              : buster_x86_metadata_emit_element_size_bits(form);
    if (!element_size_bits || pattern.vector_length < element_size_bits || pattern.vector_length % element_size_bits) return 0;
    u32 elements = pattern.vector_length / element_size_bits;
    return elements <= UINT8_MAX ? (u8)elements : 0;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_address(BusterX86MetadataPhysicalMemory memory,
                                                           BusterX86MetadataForm const* form,
                                                           BusterX86MetadataPatternSemantics const* pattern,
                                                           bool force_disp32,
                                                           BusterX86MetadataAddressEncoding* result)
{
    BusterX86MetadataAddressEncoding address = {0};
    u8 address_size = memory.address_size ? memory.address_size : 64;
    u16 register_limit = form->prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 || form->prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX ? 32 : 16;
    if (address_size == 16 || (address_size != 32 && address_size != 64)) return false;
    if (pattern->required_address_size && address_size != pattern->required_address_size) return false;
    if (pattern->forbid_address_override && address_size != 64) return false;
    if (memory.has_base && memory.base.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR) return false;
    if (memory.has_index && !memory.vsib && memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR) return false;
    if (memory.has_index && memory.vsib && memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM &&
        memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM &&
        memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM)
        return false;
    if (memory.vsib && !memory.has_index) return false;
    if (memory.has_base && memory.base.index >= register_limit) return false;
    if (memory.has_index && memory.index.index >= register_limit) return false;
    if (memory.has_index && !memory.vsib && memory.index.index == 4) return false;
    if (memory.has_base && (memory.base.high_byte || memory.base.width != address_size)) return false;
    if (memory.has_index)
    {
        if (memory.index.high_byte) return false;
        if (memory.vsib)
        {
            u16 expected_width = memory.index.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM
                                     ? 128
                                     : memory.index.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM ? 256 : 512;
            if ((memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM &&
                 memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM &&
                 memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM) ||
                memory.index.width != expected_width)
                return false;
        }
        else if (memory.index.width != address_size)
            return false;
    }
    if (memory.scale != 0 && memory.scale != 1 && memory.scale != 2 && memory.scale != 4 && memory.scale != 8) return false;
    if (memory.rip_relative && (address_size != 64 || memory.has_base || memory.has_index)) return false;
    if (memory.has_index && memory.vsib && form->prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY)
        return false;
    if (!memory.has_index && memory.scale > 1) return false;
    if (memory.rip_relative)
    {
        if (!memory.has_symbol && (memory.displacement < INT32_MIN || memory.displacement > INT32_MAX)) return false;
        address.mod = 0;
        address.rm = 5;
        address.displacement_width = 4;
        address.displacement = memory.displacement;
        address.rip_relative = true;
        address.has_symbol = memory.has_symbol;
    }
    else if (!memory.has_base)
    {
        bool unsigned_address32 = address_size == 32;
        if (!memory.has_symbol &&
            (unsigned_address32 ? (memory.displacement < 0 || memory.displacement > (s64)UINT32_MAX)
                                 : (memory.displacement < INT32_MIN || memory.displacement > INT32_MAX)))
            return false;
        address.mod = 0;
        address.rm = 4;
        address.has_sib = true;
        u8 scale = memory.scale ? memory.scale : 1;
        u8 scale_bits = scale == 1 ? 0 : scale == 2 ? 1 : scale == 4 ? 2 : 3;
        u8 index_field = memory.has_index ? (u8)(memory.index.index & 7) : 4;
        address.sib = (u8)((scale_bits << 6) | (index_field << 3) | 5);
        address.displacement_width = 4;
        address.displacement = memory.displacement;
        address.has_symbol = memory.has_symbol;
        address.address32_absolute = unsigned_address32;
    }
    else
    {
        // The machine-only incoming-argument bridge intentionally retains
        // the historical RBP+disp32 shape.  This policy is never reachable
        // through the checked/prevalidated APIs, whose ordinary disp8
        // relaxation remains unchanged.
        if (force_disp32 && (memory.rip_relative || !memory.has_base)) return false;
        address.base_index = memory.base.index;
        address.index_index = memory.has_index ? memory.index.index : 0;
        u8 scale = memory.scale ? memory.scale : 1;
        u8 scale_bits = scale == 1 ? 0 : scale == 2 ? 1 : scale == 4 ? 2 : 3;
        address.has_sib = pattern->force_sib || memory.has_index || scale != 1 || (memory.base.index & 7) == 4;
        address.rm = address.has_sib ? 4 : (u8)(memory.base.index & 7);
        if (address.has_sib)
        {
            u8 index_field = memory.has_index ? (u8)(memory.index.index & 7) : 4;
            address.sib = (u8)((scale_bits << 6) | (index_field << 3) | (memory.base.index & 7));
        }
        bool has_displacement = memory.has_displacement || memory.displacement != 0 || memory.has_symbol;
        if (force_disp32)
        {
            if (!memory.has_symbol && (memory.displacement < INT32_MIN || memory.displacement > INT32_MAX)) return false;
            address.mod = 2;
            address.displacement_width = 4;
            address.displacement = memory.displacement;
        }
        else if (!has_displacement && (memory.base.index & 7) == 5)
        {
            address.mod = 1;
            address.displacement_width = 1;
            address.displacement = 0;
        }
        else if (!has_displacement)
        {
            address.mod = 0;
            address.displacement_width = 0;
        }
        else
        {
            u8 forced_width = form->displacement_width && !form->relocation_base ? form->displacement_width : 0;
            u8 tuple_scale = buster_x86_metadata_emit_tuple_scale(form, pattern);
            if (form->displacement_scale && !tuple_scale) return false;
            bool compressed_displacement = form->displacement_scale && tuple_scale > 1;
            if (forced_width == 1 || compressed_displacement)
            {
                if (!memory.has_symbol && tuple_scale > 1 && memory.displacement % tuple_scale == 0)
                {
                    s64 compressed = memory.displacement / tuple_scale;
                    if (compressed >= -128 && compressed <= 127)
                    {
                        address.mod = 1;
                        address.displacement_width = 1;
                        address.displacement = compressed;
                    }
                    else if (forced_width == 1)
                        return false;
                }
                else if (forced_width == 1)
                    return false;
            }
            if (!address.displacement_width && !compressed_displacement && !memory.has_symbol && memory.displacement >= -128 &&
                memory.displacement <= 127 && forced_width == 0)
            {
                address.mod = 1;
                address.displacement_width = 1;
                address.displacement = memory.displacement;
            }
            else if (!address.displacement_width)
            {
                if (memory.displacement < INT32_MIN || memory.displacement > INT32_MAX) return false;
                address.mod = 2;
                address.displacement_width = 4;
                address.displacement = memory.displacement;
            }
        }
        address.has_symbol = memory.has_symbol;
    }
    if (pattern->mod_kind != BUSTER_X86_METADATA_PATTERN_MOD_ANY &&
        pattern->mod_kind != BUSTER_X86_METADATA_PATTERN_MOD_MEMORY && address.mod != pattern->mod_kind)
        return false;
    if (pattern->rm_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY && address.rm != pattern->rm_fixed) return false;
    if (pattern->has_sib && !address.has_sib) return false;
    *result = address;
    return true;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_mandatory_pp(u8 prefix)
{
    return prefix == 0x66 ? 1 : prefix == 0xf3 ? 2 : prefix == 0xf2 ? 3 : 0;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_absolute_relocation_kind(u8 width)
{
    return width == 1 ? BUSTER_X86_METADATA_RELOCATION_ABSOLUTE8
         : width == 2 ? BUSTER_X86_METADATA_RELOCATION_ABSOLUTE16
         : width == 4 ? BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32
                      : BUSTER_X86_METADATA_RELOCATION_ABSOLUTE64;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_absolute_address_relocation_kind(u8 width, bool address32_absolute)
{
    if (width == 4) return address32_absolute ? BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_ZERO_EXTENDED
                                               : BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_SIGN_EXTENDED;
    return buster_x86_metadata_emit_absolute_relocation_kind(width);
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_emit_pc_relocation_kind(u8 width)
{
    return width == 1 ? BUSTER_X86_METADATA_RELOCATION_PC8
         : width == 2 ? BUSTER_X86_METADATA_RELOCATION_PC16
         : width == 4 ? BUSTER_X86_METADATA_RELOCATION_PC32
                      : BUSTER_X86_METADATA_RELOCATION_PC64;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_signed_fits(s64 value, u8 width)
{
    if (width == 1) return value >= -128 && value <= 127;
    if (width == 2) return value >= -32768 && value <= 32767;
    if (width == 4) return value >= INT32_MIN && value <= INT32_MAX;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_unsigned_fits(u64 value, u8 width)
{
    if (width == 1) return value <= 255;
    if (width == 2) return value <= 65535;
    if (width == 4) return value <= UINT32_MAX;
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_unsigned_operand_fits(BusterX86MetadataPhysicalOperand operand, u8 width)
{
    if (operand.has_unsigned_value) return buster_x86_metadata_emit_unsigned_fits(operand.unsigned_value, width);
    if (!operand.has_value || operand.value < 0) return false;
    return buster_x86_metadata_emit_unsigned_fits((u64)operand.value, width);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_checked_add_s64(s64 left, s64 right, s64* result)
{
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) return false;
    if (result) *result = left + right;
    return true;
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_emit_diagnostic_u64(s64* diagnostic_value, u64 value)
{
    if (!diagnostic_value) return;
    *diagnostic_value = value > (u64)INT64_MAX ? INT64_MAX : (s64)value;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataEncodeStatus buster_x86_metadata_emit_form_to_scratch(
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataForm form, BusterX86MetadataEncodeScratch* scratch,
    u32* diagnostic_operand, s64* diagnostic_value, BusterX86MetadataExactPlanRecord const* plan,
    BusterX86MetadataMachineExactToken const* machine_token, bool force_disp32, bool policy_prevalidated)
{
    BusterX86MetadataPatternSemantics pattern_storage = {0};
    BusterX86MetadataPatternSemantics const* pattern_view = plan ? plan->pattern : 0;
    bool pattern_valid = false;
    if (plan)
    {
        pattern_valid = pattern_view != 0;
    }
    else
    {
        // Borrow the prewarmed parse where it exists; parsing into local
        // storage copies the whole record on every emitted instruction.
        pattern_view = buster_x86_metadata_pattern_semantics_borrow(form, &pattern_valid);
        if (!pattern_view)
        {
            pattern_valid = buster_x86_metadata_emit_parse_pattern(form, &pattern_storage);
            pattern_view = &pattern_storage;
        }
    }
    // The standard VEX AMX tile-memory rows predate XED's displacement field
    // annotation.  Their ModRM/SIB schema is still the ordinary x86 memory
    // topology: a nonzero displacement selects MOD=01/10 while the SIB is
    // retained for the tile address form.  Keep this narrowly scoped to
    // standard AMX tile-memory rows and relax only the stale fixed-MOD token;
    // address range, SIB, and all other pattern controls remain authoritative.
    BusterX86MetadataPatternSemantics amx_standard_pattern = {0};
    if (pattern_valid && form.encoder_family == BUSTER_X86_METADATA_ENCODER_AMX &&
        form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX &&
        (form.amx_flags & BUSTER_X86_METADATA_AMX_TILE_MEMORY) &&
        (form.field_flags & (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_SIB |
                             BUSTER_X86_METADATA_FIELD_MEMORY)) ==
            (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_SIB |
             BUSTER_X86_METADATA_FIELD_MEMORY) &&
        !(form.field_flags & BUSTER_X86_METADATA_FIELD_DISPLACEMENT))
    {
        amx_standard_pattern = *pattern_view;
        amx_standard_pattern.mod_kind = BUSTER_X86_METADATA_PATTERN_MOD_ANY;
        pattern_view = &amx_standard_pattern;
    }
    // Prepared exact plans borrow their immutable parsed pattern.  The only
    // former mutation (dynamic SRM opcode materialization) is emitted through
    // a local override below, so the worker path no longer copies the whole
    // pattern record on every instruction.
#define pattern (*pattern_view)
    if (!pattern_valid) return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
    if (pattern.unresolved_blocker) return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
    // Callers without a prepared plan take their per-form facts from the
    // prewarmed table instead of recomputing a string comparison per field on
    // every emitted instruction.  Both fall back to the original derivation.
    BusterX86MetadataFormFacts const* facts = plan ? 0 : buster_x86_metadata_form_facts_for(form);
    bool moffs_form = plan   ? plan->moffs_form
                      : facts ? (facts->flags & BUSTER_X86_METADATA_FORM_FACT_MOFFS) != 0
                              : buster_x86_metadata_emit_is_moffs(form, pattern);
    if (query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE &&
        (!pattern.has_segment_override ||
         !(plan   ? plan->canonical_hidden_segment_override
           : facts ? (facts->flags & BUSTER_X86_METADATA_FORM_FACT_HIDDEN_SEGMENT_OVERRIDE) != 0
                   : buster_x86_metadata_emit_canonical_hidden_segment_override(form, pattern))))
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (query.attributes.branch_hint != BUSTER_X86_METADATA_BRANCH_HINT_NONE && !pattern.has_branch_hint_control)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (pattern.has_branch_hint_control && (query.attributes.lock || query.attributes.rep || query.attributes.repne))
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (!buster_x86_metadata_prefetchit_address_valid(query)) return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
    u8 pattern_control_blocker = plan   ? plan->pattern_control_blocker
                                 : facts ? facts->pattern_control_blocker
                                         : buster_x86_metadata_emit_pattern_control_blocker(form, pattern);
    if (pattern_control_blocker != BUSTER_X86_METADATA_BLOCKER_NONE)
        return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
    bool canonical_notrack = pattern.has_cet_no_track &&
                             (plan   ? plan->canonical_notrack
                              : facts ? (facts->flags & BUSTER_X86_METADATA_FORM_FACT_NOTRACK) != 0
                                      : buster_x86_metadata_emit_canonical_cet_no_track(form, pattern));
    if (query.attributes.notrack && !canonical_notrack)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (query.attributes.notrack && (query.attributes.lock || query.attributes.rep || query.attributes.repne))
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    bool loop_form = plan   ? plan->loop_form != 0
                     : facts ? (facts->flags & BUSTER_X86_METADATA_FORM_FACT_LOOP) != 0
                             : buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOP")) ||
                                   buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPE")) ||
                                   buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPNE"));
    if (loop_form && query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64 && query.address_size == 16)
        return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
    bool jecxz_form = plan   ? plan->jecxz_form != 0
                      : facts ? (facts->flags & BUSTER_X86_METADATA_FORM_FACT_JECXZ) != 0
                              : buster_x86_metadata_string_input_equal(form.iclass.offset, S8("JECXZ"));
    bool implicit_eamode32 = jecxz_form && pattern.required_address_size == 32 && !pattern.has_memory &&
                             query.address_size == 64 && query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64;
    if (pattern.required_address_size && query.address_size && query.address_size != pattern.required_address_size &&
        !implicit_eamode32)
        return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
    // A caller-provided address-size conflict is structural and independent
    // of the row's ISA gate.  Check it before feature filtering so an
    // unrelated disabled candidate cannot turn this invalid query into a
    // misleading unsupported-feature result.
    if (query.address_size)
    {
        for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
        {
            BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
            if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && operand.memory.address_size &&
                operand.memory.address_size != query.address_size)
                return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        }
    }
    bool requires_dfv = plan   ? plan->requires_dfv
                        : facts ? (facts->flags & BUSTER_X86_METADATA_FORM_FACT_REQUIRES_DFV) != 0
                                : buster_x86_metadata_form_iform_requires_dfv(form);
    if (requires_dfv != query.attributes.has_dfv || (requires_dfv && query.attributes.dfv >= 16))
        return BUSTER_X86_METADATA_ENCODE_INVALID_INPUT;
    form.prefix_kind = pattern.prefix_kind;
    bool query_uses_egpr = false;
    for (u32 index = 0; index < query.operand_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand physical = query.operands[index];
        if (physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && physical.reg.index >= 16)
            query_uses_egpr = true;
        if (physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            query_uses_egpr |= physical.memory.has_base && physical.memory.base.index >= 16;
            query_uses_egpr |= physical.memory.has_index && !physical.memory.vsib && physical.memory.index.index >= 16;
        }
    }
    BusterX86GeneratedForm generated = buster_x86_metadata_form_record(form.id);
    if (pattern.not16 && query.execution_mode != BUSTER_X86_METADATA_EXECUTION_MODE_64 &&
        query.execution_mode != BUSTER_X86_METADATA_EXECUTION_MODE_32)
        return BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
    // A machine token has this policy validated when the token is prepared.  A
    // prevalidated caller has it validated by the checked selection that gave
    // it this form for this exact query, features included; re-deriving the
    // row's ISA gate on every emission repeats a long string comparison.
    if (!machine_token && !policy_prevalidated &&
        (!buster_x86_metadata_form_coverage_allowed(generated, (BusterX86MetadataResolveQuery){
                                                         .include_privileged = query.include_privileged,
                                                         .include_not64 = query.include_not64,
                                                     }) ||
         !buster_x86_metadata_form_execution_mode_matches(generated, (BusterX86MetadataResolveQuery){
                                                               .execution_mode = query.execution_mode,
                                                               .include_not64 = query.include_not64,
                                                           }) ||
         !buster_x86_metadata_form_feature_available(generated, query.features)))
        return BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
    if (query_uses_egpr &&
        !(machine_token ? (machine_token->policy_flags & BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_ALLOWS_APX) != 0
                        : buster_x86_metadata_feature_input_allows_apx(query.features)))
        return BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
    if (query_uses_egpr && (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY ||
                            form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX))
    {
        // Existing primary/0f legacy forms are APX-extendable through REX2.
        // The source pattern remains the authority for map/opcode semantics;
        // only the prefix family is promoted when the typed query actually
        // names an EGPR.  Explicit norex2 rows and maps outside the REX2
        // contract remain precise prefix blockers.
        u8 promotion_map = (u8)form.map;
        if (pattern.map) promotion_map = (u8)pattern.map;
        if (pattern.no_rex2 || (promotion_map != BUSTER_X86_METADATA_MAP_LEGACY &&
                                promotion_map != BUSTER_X86_METADATA_MAP_0F))
            return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
        form.prefix_kind = BUSTER_X86_METADATA_PREFIX_REX2;
        form.encoder_family = BUSTER_X86_METADATA_ENCODER_REX2;
    }
    if (!machine_token && form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 &&
        !buster_x86_metadata_feature_input_allows_apx(query.features))
        return BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
    if (query.attributes.broadcast_elements)
    {
        u8 expected_broadcast_elements = buster_x86_metadata_emit_broadcast_elements(form, pattern);
        if (!expected_broadcast_elements || query.attributes.broadcast_elements != expected_broadcast_elements)
            return BUSTER_X86_METADATA_ENCODE_DECORATOR;
    }
    if ((query.attributes.decorator_flags & (u16)~form.decorator_flags) ||
        (query.attributes.zeroing && !(form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_ZEROING)) ||
        (query.attributes.sae && !(form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_SAE)) ||
        (query.attributes.rounding_mode != BUSTER_X86_METADATA_ROUNDING_NONE &&
         !(form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_ROUNDING)) ||
        // SAE and embedded rounding are distinct architectural controls.
        // Rows whose pattern carries a rounding selector require the source
        // to choose one of its rounding modes; conversely, SAE-only rows must
        // reject a synthetic rounding mode even when a sibling form carries
        // the same opcode and decorator bits.
        (query.source_semantics && pattern.has_rounding_control && query.attributes.sae &&
         query.attributes.rounding_mode == BUSTER_X86_METADATA_ROUNDING_NONE) ||
        (query.source_semantics && query.attributes.rounding_mode != BUSTER_X86_METADATA_ROUNDING_NONE &&
         !pattern.has_rounding_control) ||
        (query.attributes.broadcast_elements && !(form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST)) ||
        (query.attributes.apx_flags & (u16)~form.apx_flags) || (query.attributes.amx_flags & (u16)~form.amx_flags))
        return BUSTER_X86_METADATA_ENCODE_DECORATOR;
    if ((pattern.mask_control == 1 &&
         ((query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_MASK) ||
          (query.attributes.has_mask_register && query.attributes.mask_register != 0))) ||
        (pattern.mask_control == 2 &&
         (!query.attributes.has_mask_register || query.attributes.mask_register == 0 ||
          !(query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_MASK))))
        return BUSTER_X86_METADATA_ENCODE_DECORATOR;
    if ((pattern.zeroing_control == 1 && query.attributes.zeroing) ||
        (pattern.zeroing_control == 2 && !query.attributes.zeroing))
        return BUSTER_X86_METADATA_ENCODE_DECORATOR;
    if (pattern.rounding_length && query.attributes.rounding_mode != BUSTER_X86_METADATA_ROUNDING_NONE &&
        pattern.vector_length != pattern.rounding_length)
        return BUSTER_X86_METADATA_ENCODE_DECORATOR;
    if (query.attributes.no_flags && !(form.apx_flags & BUSTER_X86_METADATA_APX_NF))
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    bool requested_ndd = (query.attributes.apx_flags & BUSTER_X86_METADATA_APX_NDD) != 0;
    bool requested_nf = query.attributes.no_flags || (query.attributes.apx_flags & BUSTER_X86_METADATA_APX_NF) != 0;
    if (pattern.has_nd && pattern.nd_value != requested_ndd) return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (pattern.has_nf && pattern.nf_value != requested_nf) return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 && requested_nf)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    u8 encoding_map = (u8)form.map;
    if (pattern.map) encoding_map = (u8)pattern.map;
    // Some legacy normalized rows retain the literal 0F opcode in their
    // pattern while the compact map field remains LEGACY.  REX2 consumes
    // that byte as M0, so recover the 0F map from the normalized opcode
    // stream before deciding which opcode bytes remain to be emitted.
    if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 && encoding_map == BUSTER_X86_METADATA_MAP_LEGACY &&
        pattern.opcode_count && pattern.opcode[0] == 0x0f)
        encoding_map = BUSTER_X86_METADATA_MAP_0F;
    if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 && encoding_map != BUSTER_X86_METADATA_MAP_LEGACY &&
        encoding_map != BUSTER_X86_METADATA_MAP_0F)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;

    // emit_bind_form fills each binding before publishing binding_count; all
    // consumers below are bounded by that count.  Avoid clearing the full
    // fixed-size array on every exact row.
    BusterX86MetadataPhysicalBinding bindings[16];
    u32 binding_count = 0;
    if (!buster_x86_metadata_emit_bind_form(query, &form, bindings, &binding_count, diagnostic_operand, plan))
        return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
    if (!buster_x86_metadata_emit_df64_operands_match(pattern, bindings, binding_count))
        return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
    bool hidden_control_projection = buster_x86_metadata_emit_hidden_control_projection(query, form);
    if (query.source_semantics && !hidden_control_projection &&
        !buster_x86_metadata_emit_form_operand_semantics(form, bindings, binding_count))
        return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
    for (u32 index = 0; index < binding_count; index += 1)
    {
        if (bindings[index].physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            !buster_x86_metadata_emit_register_valid(bindings[index].physical.reg, (BusterX86MetadataEncoderFamily)form.encoder_family))
            return BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING;
    }
    BusterX86MetadataPhysicalBinding* reg_binding;
    BusterX86MetadataPhysicalBinding* rm_binding;
    BusterX86MetadataPhysicalBinding* vvvv_binding;
    BusterX86MetadataPhysicalBinding* mask_binding;
    BusterX86MetadataPhysicalBinding* memory_binding;
    bool simple_bindings = plan ? plan->exact_bind_simple != 0
                                : facts && (facts->shape_flags & BUSTER_X86_METADATA_FORM_FACT2_BIND_SIMPLE) != 0;
    if (simple_bindings)
    {
        u8 field_reg = plan ? plan->exact_bind_reg : facts->bind_reg;
        u8 field_rm = plan ? plan->exact_bind_rm : facts->bind_rm;
        u8 field_vvvv = plan ? plan->exact_bind_vvvv : facts->bind_vvvv;
        u8 field_mask = plan ? plan->exact_bind_mask : facts->bind_mask;
        u8 field_memory = plan ? plan->exact_bind_memory : facts->bind_memory;
        reg_binding = field_reg == UINT8_MAX ? 0 : bindings + field_reg;
        rm_binding = field_rm == UINT8_MAX ? 0 : bindings + field_rm;
        vvvv_binding = field_vvvv == UINT8_MAX ? 0 : bindings + field_vvvv;
        mask_binding = field_mask == UINT8_MAX ? 0 : bindings + field_mask;
        memory_binding = field_memory == UINT8_MAX ? 0 : bindings + field_memory;
    }
    else
    {
        reg_binding = buster_x86_metadata_emit_field_binding(bindings, binding_count, BUSTER_X86_METADATA_FIELD_SOURCE_REG);
        rm_binding = buster_x86_metadata_emit_field_binding(bindings, binding_count, BUSTER_X86_METADATA_FIELD_SOURCE_RM);
        vvvv_binding = buster_x86_metadata_emit_field_binding(bindings, binding_count, BUSTER_X86_METADATA_FIELD_SOURCE_VVVV);
        mask_binding = buster_x86_metadata_emit_field_binding(bindings, binding_count, BUSTER_X86_METADATA_FIELD_SOURCE_MASK);
        memory_binding = buster_x86_metadata_emit_memory_binding(bindings, binding_count);
    }
    if (!reg_binding && pattern.has_reg_range)
        return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
    if (memory_binding && pattern.has_vsib_control)
    {
        BusterX86MetadataPhysicalRegister expected_index = {0};
        bool have_expected_index = false;
        if (pattern.vsib_vector_length)
        {
            expected_index.width = pattern.vsib_vector_length;
            expected_index.physical_class = pattern.vsib_vector_length == 128 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM
                                            : pattern.vsib_vector_length == 256 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM
                                                                                : BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM;
            have_expected_index = true;
        }
        if (!have_expected_index || !memory_binding->physical.memory.has_index ||
            memory_binding->physical.memory.index.physical_class != expected_index.physical_class ||
            memory_binding->physical.memory.index.width != expected_index.width)
            return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
    }
    BusterX86MetadataPhysicalBinding* selector_binding = 0;
    for (u32 index = 0; index < binding_count; index += 1)
    {
        bool selector = bindings[index].prepared_flags_valid
                            ? (bindings[index].prepared_flags & BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_SELECTOR) != 0
                            : buster_x86_metadata_emit_atom_contains(bindings[index].metadata.atom, S8("_SE"));
        if (selector)
        {
            selector_binding = bindings + index;
            break;
        }
    }
    if (pattern.selector_immediate &&
        (!selector_binding || selector_binding->physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
         selector_binding->physical.reg.index >= 16))
        return selector_binding ? BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING : BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
    BusterX86MetadataPhysicalMemory memory = {0};
    s64 memory_relocation_addend = 0;
    bool has_memory = memory_binding != 0;
    if (has_memory)
    {
        memory = memory_binding->physical.memory;
        bool form_vsib = (form.field_flags & BUSTER_X86_METADATA_FIELD_VSIB) != 0;
        if (memory.vsib != form_vsib || (memory.vsib && !memory.has_index)) return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        if (query.address_size && memory.address_size && query.address_size != memory.address_size)
            return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        if (!memory.address_size) memory.address_size = query.address_size ? query.address_size : 64;
        bool address32_absolute = memory.address_size == 32 && !memory.rip_relative && !memory.has_base;
        if (moffs_form)
        {
            // moffs is an absolute offset, not a ModRM address.  Its width
            // follows the selected address size, and base/index/RIP-relative
            // shapes are deliberately rejected so this path cannot widen to
            // the implicit-DI MASKMOV rows.
            if ((memory.address_size != 16 && memory.address_size != 32 && memory.address_size != 64) ||
                memory.has_base || memory.has_index || memory.rip_relative || memory.vsib || memory.scale > 1)
                return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        }
        if (!moffs_form && !memory.has_symbol && (memory.rip_relative || !memory.has_base) &&
            (memory.displacement < INT32_MIN ||
             (address32_absolute ? memory.displacement > (s64)UINT32_MAX : memory.displacement > INT32_MAX)))
        {
            if (diagnostic_value) *diagnostic_value = memory.displacement;
            return BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE;
        }
        if (moffs_form && !memory.has_symbol && memory.address_size != 64 &&
            (memory.displacement < 0 ||
             (memory.address_size == 16 ? memory.displacement > UINT16_MAX : memory.displacement > (s64)UINT32_MAX)))
        {
            if (diagnostic_value) *diagnostic_value = memory.displacement;
            return BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE;
        }
        if (memory.has_symbol && !buster_x86_metadata_emit_checked_add_s64(memory.addend, memory.displacement, &memory_relocation_addend))
        {
            if (diagnostic_value) *diagnostic_value = memory.addend > 0 ? INT64_MAX : INT64_MIN;
            return BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE;
        }
    }
    bool uses_apx_egpr = false;
    for (u32 index = 0; index < binding_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand physical = bindings[index].physical;
        if (physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && physical.reg.index >= 16)
            uses_apx_egpr = true;
    }
    if (has_memory)
    {
        // EVEX extends vector register names to 31.  APX also extends every
        // existing EVEX instruction's GPR operands and ordinary GPR address
        // fields to r16-r31; VSIB vector indices use the EVEX V' field
        // instead.  The feature check below is therefore required for every
        // EGPR use, but the form does not need an APX-specific metadata bit.
        uses_apx_egpr |= memory.has_base && memory.base.index >= 16;
        uses_apx_egpr |= memory.has_index && !memory.vsib && memory.index.index >= 16;
    }
    if (uses_apx_egpr &&
        !(machine_token ? (machine_token->policy_flags & BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_ALLOWS_APX) != 0
                        : buster_x86_metadata_feature_input_allows_apx(query.features)))
        return BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
    BusterX86MetadataAddressEncoding address = {0};
    if (has_memory && !moffs_form && !buster_x86_metadata_emit_address(memory, &form, &pattern, force_disp32, &address))
        return BUSTER_X86_METADATA_ENCODE_ADDRESSING;

    // The register-register 0x87 XCHG form is schema-proven symmetric:
    // both visible fields are read/write GPRs of the same width.  Canonical
    // assemblers put the lower register in REG and the higher register in
    // RM for the APX REX2 form.  Normalize only this exact symmetric field
    // shape; XADD, CMPXCHG, and memory forms retain their declared roles.
    if (!has_memory && reg_binding && rm_binding && pattern.has_modrm && pattern.opcode_count &&
        pattern.opcode[0] == 0x87 &&
        (plan   ? plan->dataxfer_category
         : facts ? (facts->flags & BUSTER_X86_METADATA_FORM_FACT_DATAXFER) != 0
                 : form.category.length == 8 && buster_x86_metadata_emit_string_has(form.category, S8("DATAXFER"))) &&
        reg_binding->metadata.access == (BUSTER_X86_METADATA_ACCESS_READ | BUSTER_X86_METADATA_ACCESS_WRITE) &&
        rm_binding->metadata.access == (BUSTER_X86_METADATA_ACCESS_READ | BUSTER_X86_METADATA_ACCESS_WRITE) &&
        reg_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
        rm_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
        reg_binding->physical.reg.physical_class == rm_binding->physical.reg.physical_class &&
        reg_binding->physical.reg.width == rm_binding->physical.reg.width &&
        reg_binding->physical.reg.high_byte == rm_binding->physical.reg.high_byte &&
        reg_binding->physical.reg.index > rm_binding->physical.reg.index)
    {
        BusterX86MetadataPhysicalOperand physical = reg_binding->physical;
        reg_binding->physical = rm_binding->physical;
        rm_binding->physical = physical;
    }

    u16 reg_index = reg_binding && reg_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                        ? reg_binding->physical.reg.index
                        : 0;
    u16 rm_index = rm_binding && rm_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                       ? rm_binding->physical.reg.index
                       : 0;
    u16 vvvv_index = vvvv_binding && vvvv_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                         ? vvvv_binding->physical.reg.index
                         : 0;
    if (pattern.no_vector_source && vvvv_binding)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (pattern.has_ubit && form.prefix_kind != BUSTER_X86_METADATA_PREFIX_EVEX &&
        (u8)(vvvv_index < 16) != pattern.ubit_value)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    u16 mask_index = mask_binding && mask_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                         ? mask_binding->physical.reg.index
                         : query.attributes.mask_register;
    if (!buster_x86_metadata_emit_reg_is_unconstrained(pattern) && reg_binding &&
        reg_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
        !buster_x86_metadata_emit_reg_matches(pattern, (u8)(reg_index & 7)))
        return BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING;
    if (pattern.rm_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY && !has_memory && rm_binding &&
        rm_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
        (rm_index & 7) != pattern.rm_fixed)
        return BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING;
    BusterX86MetadataPhysicalBinding* srm_field_binding = rm_binding ? rm_binding :
        (pattern.has_dynamic_opcode ? reg_binding : 0);
    if (pattern.srm_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY && srm_field_binding &&
        srm_field_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER)
    {
        u16 srm_index = srm_field_binding->physical.reg.index;
        bool srm_matches = (srm_index & 7) == pattern.srm_fixed && (!pattern.srm_not_equal || srm_index < 8);
        if (pattern.srm_not_equal ? srm_matches : !srm_matches) return BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING;
    }
    if (has_memory && pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_REGISTER) return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
    if (!has_memory && pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_MEMORY) return BUSTER_X86_METADATA_ENCODE_ADDRESSING;

    // Binary opcode tokens also use the dynamic-opcode slot for fixed
    // encodings such as NOP's `0b1001_0 SRM[0b000]`.  Only SRM[rrr] carries a
    // visible register that replaces the low opcode bits; a fixed SRM value
    // must not manufacture a binding or require an operand.
    bool dynamic_opcode_register = pattern.has_dynamic_opcode && !pattern.has_modrm && pattern.has_srm_register;
    BusterX86MetadataPhysicalBinding* dynamic_opcode_binding = dynamic_opcode_register
                                                                    ? (rm_binding ? rm_binding : reg_binding)
                                                                    : 0;
    u16 dynamic_opcode_index = dynamic_opcode_binding &&
                                       dynamic_opcode_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                                   ? dynamic_opcode_binding->physical.reg.index
                                   : 0;
    bool has_r = reg_binding && !dynamic_opcode_register && ((reg_index & 8) != 0);
    bool has_x = has_memory && memory.has_index && ((memory.index.index & 8) != 0);
    bool has_b = has_memory ? (memory.has_base && ((memory.base.index & 8) != 0))
                            : (dynamic_opcode_register ? ((dynamic_opcode_index & 8) != 0) : ((rm_index & 8) != 0));
    bool has_r4 = reg_binding && !dynamic_opcode_register && ((reg_index & 16) != 0);
    bool has_x4 = has_memory && memory.has_index && ((memory.index.index & 16) != 0);
    bool has_b4 = has_memory ? (memory.has_base && ((memory.base.index & 16) != 0))
                             : (dynamic_opcode_register ? ((dynamic_opcode_index & 16) != 0) : ((rm_index & 16) != 0));
    if (pattern.rex_b_control == BUSTER_X86_METADATA_REX_CONTROL_FORBID && has_b)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (pattern.rex_b_control == BUSTER_X86_METADATA_REX_CONTROL_REQUIRE && !has_b)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (pattern.rex_b4_control == BUSTER_X86_METADATA_REX_CONTROL_FORBID && has_b4)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (pattern.rex_b4_control == BUSTER_X86_METADATA_REX_CONTROL_REQUIRE && !has_b4)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    // APX privileged/system forms use EVEX only to carry EGPR extension
    // bits.  Their source schema fixes W=0 even though the visible GPR is
    // 64-bit.  Keep this predicate tied to the exact fixed-width ISA sets
    // and pattern shape; ordinary APX arithmetic remains width-derived.
    bool apx_stack_pair = (buster_x86_metadata_string_input_equal(form.iclass.offset, S8("PUSH2")) ||
                           buster_x86_metadata_string_input_equal(form.iclass.offset, S8("POP2"))) &&
                          pattern.has_nd && pattern.nd_value;
    bool apx_evex_fixed_width_no_w = form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX &&
                                     (form.apx_flags & BUSTER_X86_METADATA_APX) != 0 && !pattern.has_w &&
                                     pattern.no_vector_source && pattern.vector_length == 128 && pattern.has_modrm &&
                                     ((buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_ENQCMD")) &&
                                       pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_MEMORY && pattern.has_memory) ||
                                      (buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_MOVDIR64B")) &&
                                       pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_MEMORY && pattern.has_memory) ||
                                      (buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_INVPCID")) &&
                                       pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_MEMORY && pattern.has_memory) ||
                                      (buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_VMX")) &&
                                       pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_MEMORY && pattern.has_memory) ||
                                      (buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("APX_F_MSR_IMM")) &&
                                       pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_REGISTER && pattern.has_ubit &&
                                      pattern.ubit_value == 1 && pattern.immediate_width == 4));
    apx_evex_fixed_width_no_w = apx_evex_fixed_width_no_w ||
                                (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX &&
                                 (form.apx_flags & BUSTER_X86_METADATA_APX) != 0 && apx_stack_pair);
    bool rex_w = pattern.w != 0;
    bool has_mmx_operand = false;
    bool has_xmm_operand = false;
    for (u32 index = 0; index < binding_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand physical = bindings[index].physical;
        if (physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX)
        {
            has_mmx_operand = true;
        }
        if (physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM)
            has_xmm_operand = true;
    }
    // MOVSXD's XED row carries `norexw_prefix` even though its GPRz
    // destination still requires REX.W when selected at 64-bit width.  The
    // destination register is the semantic width authority for this one
    // legacy row; retain the no-W spelling for a 32-bit destination.
    bool movsxd_form = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("MOVSXD"));
    // x87 real-memory forms use distinct opcodes for 32/64/80-bit elements;
    // unlike integer scalar forms, their 64-bit element never means REX.W.
    // Keep the physical width available for form selection while excluding
    // the generic scalar-width prefix heuristic from every X87 row.
    bool x87_form = buster_x86_metadata_string_input_equal(form.extension.offset, S8("X87"));
    // MPX BND forms carry fixed-width bounds and use their legacy prefixes
    // solely for opcode selection. Their qword address operand must not
    // synthesize REX.W; only base/index extension bits may require REX.
    bool mpx_form = buster_x86_metadata_string_input_equal(form.extension.offset, S8("MPX"));
    // APX REX2's unary qword IMUL memory row is encoded by F7 /5, whose
    // normalized pattern omits the W token even though the source memory
    // width selects the qword operation.  Derive REX2.W only for that exact
    // fixed-width topology; byte/word/dword unary forms stay W=0.
    bool apx_rex2_unary_imul_qword = form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 &&
                                     buster_x86_metadata_string_input_equal(form.iclass.offset, S8("IMUL")) &&
                                     pattern.has_modrm && pattern.opcode_count && pattern.opcode[0] == 0xf7 &&
                                     pattern.reg_fixed == 5 && binding_count == 1 &&
                                     bindings[0].physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
                                     (bindings[0].physical.width ? bindings[0].physical.width
                                                                   : bindings[0].physical.memory.source_width) == 64;
    bool apx_rex2_mov_memory_qword = form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 &&
                                     buster_x86_metadata_string_input_equal(form.iclass.offset, S8("MOV")) &&
                                     pattern.has_modrm;
    for (u32 index = 0; index < binding_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand physical = bindings[index].physical;
        if (query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64 &&
            (pattern.has_modrm || pattern.has_dynamic_opcode || moffs_form) &&
            physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && physical.reg.width == 64)
            if ((!pattern.has_w || movsxd_form) && !apx_evex_fixed_width_no_w) rex_w = true;
        // Variable-width scalar forms derive REX.W from a 64-bit data
        // register.  A folded memory source carries that same semantic width
        // in the physical operand rather than in a register binding; without
        // this arm, e.g. `sub qword ptr [r8], imm8` selected the right form
        // but emitted 41 83 instead of 49 83.  Explicit W controls and the
        // fixed-width APX EVEX rows remain authoritative.
        // Folded memory width is a scalar legacy/REX width authority.  VEX,
        // XOP, EVEX, and APX vector forms carry their W bit exclusively in
        // the metadata pattern; deriving it from a qword memory operand
        // would turn VMOVAPD's canonical VEX.W=0 prefix into W=1.
        bool scalar_memory_width_rex_w = form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY ||
                                         form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX;
        u16 schema_widths = bindings[index].metadata.physical_width_flags;
        bool variable_scalar_memory_width = (schema_widths & BUSTER_X86_METADATA_PHYSICAL_WIDTH_32) &&
                                            (schema_widths & BUSTER_X86_METADATA_PHYSICAL_WIDTH_64);
        if (query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64 &&
            (pattern.has_modrm || pattern.has_dynamic_opcode || moffs_form) &&
            physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && !pattern.has_w && !apx_evex_fixed_width_no_w &&
            !has_mmx_operand && !has_xmm_operand && !x87_form && !mpx_form && !moffs_form && scalar_memory_width_rex_w &&
            variable_scalar_memory_width)
        {
            u16 memory_width = physical.width ? physical.width : physical.memory.source_width;
            if (memory_width == 64) rex_w = true;
        }
        if ((apx_rex2_unary_imul_qword || apx_rex2_mov_memory_qword) &&
            physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
            (physical.width ? physical.width : physical.memory.source_width) == 64)
            rex_w = true;
    }
    // DF64 rows encode their 64-bit default without a synthetic REX.W.  An
    // explicit W token remains authoritative for the small subset whose XED
    // row actually requires it (for example the PUSHF/POPF aliases).
    if (pattern.df64 && !pattern.has_w && !apx_rex2_unary_imul_qword && !apx_rex2_mov_memory_qword) rex_w = false;
    // IMMUNE_REXW rows intentionally ignore a derived REX.W.  The typed
    // query has no separate "prefix was explicitly requested" bit, so a
    // width heuristic must not turn a valid fixed-width form into a failure.
    if (pattern.immune_rexw && !apx_rex2_unary_imul_qword && !apx_rex2_mov_memory_qword) rex_w = false;
    if (form.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY && query.source_semantics &&
        (buster_x86_metadata_block_memory_source_authoritative(form, query) ||
         buster_x86_metadata_aggregate_memory_source_authoritative(form, query)))
        rex_w = false;
    bool vector_family = form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX || form.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP ||
                         form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX;
    if (vector_family && buster_x86_metadata_emit_any_high_byte(bindings, binding_count))
        return BUSTER_X86_METADATA_ENCODE_HIGH_BYTE_WITH_REX;
    bool needs_low_byte_rex = false;
    for (u32 index = 0; index < binding_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand physical = bindings[index].physical;
        if (physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && physical.reg.width == 8 &&
            !physical.reg.high_byte && physical.reg.index >= 4 && physical.reg.index < 8)
            needs_low_byte_rex = true;
    }
    // PREFIX_REX identifies the legacy x86-64 row family; it does not by
    // itself require a byte.  In particular, norexw rows such as CMPXCHG8B
    // must remain unprefixed when no register/address bit or low-byte escape
    // requires REX.  REX2 is a distinct two-byte encoding and remains
    // mandatory for its form family.
    bool needs_rex = rex_w || has_r || has_x || has_b || needs_low_byte_rex || form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2;
    if (buster_x86_metadata_emit_any_high_byte(bindings, binding_count) && needs_rex)
        return BUSTER_X86_METADATA_ENCODE_HIGH_BYTE_WITH_REX;
    if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX || form.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP)
    {
        if (has_r4 || has_x4 || has_b4 || vvvv_index >= 16 || pattern.vector_length == 512)
            return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    }
    if (pattern.no_rexr_prefix && has_r)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY || form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX ||
        form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2)
    {
        if ((query.attributes.decorator_flags & (BUSTER_X86_METADATA_DECORATOR_MASK | BUSTER_X86_METADATA_DECORATOR_ZEROING |
                                                   BUSTER_X86_METADATA_DECORATOR_BROADCAST | BUSTER_X86_METADATA_DECORATOR_ROUNDING |
                                                   BUSTER_X86_METADATA_DECORATOR_SAE)) ||
            (query.attributes.apx_flags && form.prefix_kind != BUSTER_X86_METADATA_PREFIX_REX2))
            return BUSTER_X86_METADATA_ENCODE_DECORATOR;
    }

    u8 mandatory_prefix = (u8)form.mandatory_prefix;
    if (pattern.mandatory_prefix) mandatory_prefix = (u8)pattern.mandatory_prefix;
    u8 pp = buster_x86_metadata_emit_mandatory_pp(mandatory_prefix);
    u16 data_width = buster_x86_metadata_emit_data_operand_width(bindings, binding_count);
    // FISTTP's m16 form belongs to the SSE3 ISA set, but its DF encoding
    // still uses the architectural 16-bit field without an operand-size
    // override.  Treat it like the other x87 fixed-width rows when deriving
    // a generic 0x66 prefix from the physical memory width.
    bool x87_no_operand_size_override = x87_form ||
                                        (buster_x86_metadata_string_input_equal(form.iclass.offset, S8("FISTTP")) &&
                                         data_width == 16);
    // In a MODE16 row the default operand size is already 16 bits.  Do not
    // synthesize a 66 byte for the no66 spelling; MODE32 retains the normal
    // 32-bit default and therefore keeps the existing width-derived rule.
    bool operand_size_override = data_width == 16 && mandatory_prefix != 0x66 && !x87_no_operand_size_override &&
                                 pattern.mode_control != BUSTER_X86_METADATA_PATTERN_MODE_16 &&
                                 (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY ||
                                  form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX ||
                                  form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2);
    if (operand_size_override && pattern.forbid_operand_size_override)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    bool immune_to_66 = pattern.immune66 ||
                        (pattern.immune66_loop64 && query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64);
    if (operand_size_override && immune_to_66) return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if ((query.attributes.rep && mandatory_prefix == 0xf2) || (query.attributes.repne && mandatory_prefix == 0xf3))
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    bool lock_alias_allowed = buster_x86_metadata_emit_lock_alias_allowed(form, pattern, has_memory);
    if ((pattern.lock_control == 1 && !query.attributes.lock) ||
        (pattern.lock_control == 2 && query.attributes.lock && !lock_alias_allowed) ||
        (query.attributes.lock && pattern.lock_control != 1 && !lock_alias_allowed) ||
        (query.attributes.lock && !has_memory))
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (pattern.rep_not_f3 && query.attributes.rep) return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if ((pattern.rep_control == 1 && !query.attributes.rep) || (pattern.rep_control == 2 && !query.attributes.repne) ||
        (pattern.rep_control == 3 && (query.attributes.rep || query.attributes.repne)) ||
        (pattern.rep_control == 1 && query.attributes.repne) || (pattern.rep_control == 2 && query.attributes.rep))
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (pattern.rep_control == 0 && (query.attributes.rep || query.attributes.repne) && !pattern.rep_not_f3)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX || form.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP ||
        form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX)
    {
        if (query.attributes.lock || query.attributes.rep || query.attributes.repne) return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    }
    if (pattern.has_remove_segment && has_memory && memory.has_segment)
        return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
    if (query.attributes.notrack && has_memory && memory.has_segment)
        return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    if (query.attributes.notrack && !buster_x86_metadata_emit_write_byte(scratch, 0x3e))
        return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    if (query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE)
    {
        u8 segment_prefix = buster_x86_metadata_emit_segment_prefix(query.attributes.implicit_segment);
        if (!segment_prefix || !buster_x86_metadata_emit_write_byte(scratch, segment_prefix))
            return segment_prefix ? BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY : BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
    }
    if (has_memory && memory.has_segment)
    {
        u8 segment_prefix = buster_x86_metadata_emit_segment_prefix(memory.segment);
        if (!segment_prefix || !buster_x86_metadata_emit_write_byte(scratch, segment_prefix))
            return segment_prefix ? BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY : BUSTER_X86_METADATA_ENCODE_ADDRESSING;
    }
    if (has_memory && memory.address_size != 64)
    {
        if (!buster_x86_metadata_emit_write_byte(scratch, 0x67)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    else if (!has_memory && query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64 &&
             (query.address_size == 32 || (jecxz_form && pattern.required_address_size == 32)))
    {
        // EAMODE is an encoding constraint even when the normalized row has
        // only suppressed implicit operands.  In 64-bit execution, the
        // address-size override remains part of the instruction bytes.
        if (!buster_x86_metadata_emit_write_byte(scratch, 0x67)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY || form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX ||
        form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2)
    {
        if (pattern.has_branch_hint_control && query.attributes.branch_hint != BUSTER_X86_METADATA_BRANCH_HINT_NONE)
        {
            u8 branch_hint_prefix = query.attributes.branch_hint == BUSTER_X86_METADATA_BRANCH_HINT_NOT_TAKEN ? 0x2e : 0x3e;
            if (!buster_x86_metadata_emit_write_byte(scratch, branch_hint_prefix))
                return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
        if (query.attributes.lock && !buster_x86_metadata_emit_write_byte(scratch, 0xf0)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        if (query.attributes.rep && mandatory_prefix != 0xf3 && !buster_x86_metadata_emit_write_byte(scratch, 0xf3))
            return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        if (query.attributes.repne && mandatory_prefix != 0xf2 && !buster_x86_metadata_emit_write_byte(scratch, 0xf2))
            return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        if (operand_size_override && !buster_x86_metadata_emit_write_byte(scratch, 0x66))
            return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        if (mandatory_prefix && !buster_x86_metadata_emit_write_byte(scratch, mandatory_prefix)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2)
        {
            u8 rex2 = (u8)((rex_w ? 0x08 : 0) | (has_r ? 0x04 : 0) | (has_x ? 0x02 : 0) | (has_b ? 0x01 : 0) |
                           (has_r4 ? 0x40 : 0) | (has_x4 ? 0x20 : 0) | (has_b4 ? 0x10 : 0) |
                           (encoding_map == BUSTER_X86_METADATA_MAP_0F ? 0x80 : 0));
            if (!buster_x86_metadata_emit_write_byte(scratch, 0xd5) || !buster_x86_metadata_emit_write_byte(scratch, rex2))
                return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
        else if (needs_rex)
        {
            u8 rex = (u8)(0x40 | (rex_w ? 8 : 0) | (has_r ? 4 : 0) | (has_x ? 2 : 0) | (has_b ? 1 : 0));
            if (!buster_x86_metadata_emit_write_byte(scratch, rex)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
    }
    else if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX || form.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP)
    {
        u8 map = (u8)form.map;
        if (pattern.map) map = (u8)pattern.map;
        u8 l = pattern.vector_length == 256 ? 1 : 0;
        u8 v = (u8)(vvvv_index & 0xf);
        u8 p1 = (u8)((rex_w ? 0x80u : 0u) | ((u32)((u8)((~v) & 0xf)) << 3) | ((u32)l << 2) | (u32)pp);
        if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX && map == BUSTER_X86_METADATA_MAP_0F && !rex_w && !has_x && !has_b)
        {
            if (!buster_x86_metadata_emit_write_byte(scratch, 0xc5) ||
                !buster_x86_metadata_emit_write_byte(scratch, (u8)((has_r ? 0x00u : 0x80u) | ((u32)((u8)((~v) & 0xf)) << 3) |
                                                                    ((u32)l << 2) | (u32)pp)))
                return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
        else
        {
            u8 p0 = (u8)(((has_r ? 0u : 1u) << 7) | ((has_x ? 0u : 1u) << 6) | ((has_b ? 0u : 1u) << 5) | map);
            if (!buster_x86_metadata_emit_write_byte(scratch, form.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP ? 0x8f : 0xc4) ||
                !buster_x86_metadata_emit_write_byte(scratch, p0) || !buster_x86_metadata_emit_write_byte(scratch, p1))
                return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
    }
    else if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX)
    {
        u8 map = (u8)form.map;
        if (pattern.map) map = (u8)pattern.map;
        u8 v = (u8)(vvvv_index & 0xf);
        u8 aaa = (u8)(mask_index & 7);
        u8 b = (query.attributes.broadcast_elements || query.attributes.sae ||
                query.attributes.rounding_mode != BUSTER_X86_METADATA_ROUNDING_NONE)
                   ? 1
                   : 0;
        // XED's fixed-round rows encode EVEX.b=1 even when the source form
        // has no caller-visible SAE or rounding decorator.  Keep ordinary
        // decorator validation above authoritative, but carry this parsed
        // fixed-round/BCRC contract into the prefix bits generically.
        if (pattern.has_bcrc && pattern.bcrc_value == 1 && pattern.rounding_length && !pattern.has_sae_control &&
            !pattern.has_rounding_control)
            b = 1;
        if (pattern.has_bcrc && b != pattern.bcrc_value)
            return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
        u8 p0 = 0;
        u8 p1 = 0;
        u8 p2 = 0;
        bool apx_evex = (form.apx_flags & BUSTER_X86_METADATA_APX) != 0;
        if (apx_evex)
        {
            bool has_architectural_mask = false;
            for (u32 binding_index = 0; binding_index < binding_count; binding_index += 1)
            {
                has_architectural_mask |= buster_x86_metadata_emit_is_architectural_mask_binding(bindings[binding_index]);
            }
            u16 apx_width = vvvv_binding && vvvv_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                                ? vvvv_binding->physical.reg.width
                                : reg_binding && reg_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                                      ? reg_binding->physical.reg.width
                                      : rm_binding && rm_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER
                                      ? rm_binding->physical.reg.width
                                            : has_memory ? buster_x86_metadata_emit_operand_width(memory_binding->physical) : data_width;
            // APX KMOV rows use the pattern's W/mandatory-prefix fields to
            // select the encoded element width.  The visible MASK operands
            // are architectural k registers, so their 64-bit physical width
            // must not force W=1 or erase the pattern's pp (V66/VF2/VF3).
            // Keep both fields when a row has W1+V66 (the ordinary width
            // heuristic can represent only one of them at a time).
            p0 = (u8)(0xf0 | (map & 0x0f));
            if (reg_index & 8) p0 &= (u8)~0x80;
            if (reg_index & 16) p0 &= (u8)~0x10;
            if (!has_memory && (rm_index & 8)) p0 &= (u8)~0x20;
            if (!has_memory && (rm_index & 16)) p0 |= 0x08;
            if (has_memory && memory.has_base && (memory.base.index & 8)) p0 &= (u8)~0x20;
            if (has_memory && memory.has_base && (memory.base.index & 16)) p0 |= 0x08;
            if (has_memory && memory.has_index && (memory.index.index & 8)) p0 &= (u8)~0x40;
            p1 = has_architectural_mask
                     ? (u8)((pattern.w ? 0x80 : 0) | pp)
                     : (u8)(!apx_evex_fixed_width_no_w && apx_width == 64 ? 0x80 : apx_width == 16 ? 0x01 : 0);
            if (pattern.has_scc)
                p1 |= (u8)(0x04 | ((query.attributes.dfv & 0xf) << 3));
            else if (pattern.has_nd && pattern.nd_value)
                p1 |= (u8)((((~vvvv_index) & 0xf) << 3) | 0x04);
            else
                p1 |= apx_evex_fixed_width_no_w ? (u8)(0x7c | pp) : 0x7c;
            if (has_memory && memory.has_index && !memory.vsib && (memory.index.index & 16)) p1 &= (u8)~0x04;
            if (pattern.has_scc)
                p2 = (u8)pattern.scc_value;
            else if (pattern.has_nd && pattern.nd_value)
                p2 = (u8)(0x10 | (requested_nf ? 0x04 : 0) | (vvvv_index < 16 ? 0x08 : 0));
            else
                p2 = (u8)(0x08 | (requested_nf ? 0x04 : 0));
        }
        else
        {
            u8 l = pattern.vector_length == 512 ? 2 : pattern.vector_length == 256 ? 1 : 0;
            p0 = (u8)(((has_r ? 0u : 1u) << 7) | ((has_x ? 0u : 1u) << 6) | ((has_b ? 0u : 1u) << 5) |
                       ((has_r4 ? 0u : 1u) << 4) | map);
            bool apx_extended_reg = reg_binding && reg_binding->physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                                    reg_binding->physical.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
                                    reg_index >= 16;
            // EVEXR4_ONE is the raw encoded R'=1 constraint used by the
            // ordinary EVEX forms.  APX reuses R' to extend a GPR REG field;
            // the earlier APX feature check makes that promotion legal.
            if (pattern.has_evex_r4 && has_r4 != (pattern.evex_r4_value == 0) && !apx_extended_reg)
                return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
            if (!has_memory && has_b4) p0 &= (u8)~0x40;
            if (has_memory && memory.has_base && (memory.base.index & 16)) p0 |= 0x08;
            p1 = (u8)((rex_w ? 0x80 : 0) | (((~v) & 0xf) << 3) | 0x04 | pp);
            if (has_memory && memory.has_index && !memory.vsib && (memory.index.index & 16)) p1 &= (u8)~0x04;
            p2 = 0;
            if (query.attributes.zeroing) p2 = 0x80;
            if (query.attributes.rounding_mode != BUSTER_X86_METADATA_ROUNDING_NONE)
                p2 |= (u8)(((query.attributes.rounding_mode - 1) & 3) << 5);
            else if (!query.attributes.sae)
                p2 |= (u8)(l << 5);
            bool high_vector_index = has_memory && memory.vsib && memory.has_index ? memory.index.index >= 16 : vvvv_index >= 16;
            p2 |= (u8)(((u32)b << 4) | (high_vector_index ? 0u : 0x08u) | (u32)aaa);
        }
        if (!buster_x86_metadata_emit_write_byte(scratch, 0x62) || !buster_x86_metadata_emit_write_byte(scratch, p0) ||
            !buster_x86_metadata_emit_write_byte(scratch, p1) || !buster_x86_metadata_emit_write_byte(scratch, p2))
            return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }

    u8 opcode_start = 0;
    if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 && encoding_map == BUSTER_X86_METADATA_MAP_0F && pattern.opcode_count &&
        pattern.opcode[0] == 0x0f)
        opcode_start = 1;
    if (pattern.has_dynamic_opcode && dynamic_opcode_register)
    {
        BusterX86MetadataPhysicalBinding* srm_binding = rm_binding ? rm_binding : reg_binding;
        if (!srm_binding || srm_binding->physical.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER)
            return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
        if (!pattern.opcode_count) return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
    }
    for (u32 index = opcode_start; index < pattern.opcode_count; index += 1)
    {
        u8 opcode = pattern.opcode[index];
        if (pattern.has_dynamic_opcode && dynamic_opcode_register && index + 1 == pattern.opcode_count)
        {
            BusterX86MetadataPhysicalBinding* srm_binding = rm_binding ? rm_binding : reg_binding;
            opcode = (u8)(pattern.srm_base | (srm_binding->physical.reg.index & 7));
        }
        if (!buster_x86_metadata_emit_write_byte(scratch, opcode)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    if (moffs_form)
    {
        u8 width = (u8)(memory.address_size / 8);
        if (memory.has_symbol)
        {
            u8 kind = buster_x86_metadata_emit_absolute_address_relocation_kind(width, memory.address_size == 32);
            if (!buster_x86_metadata_emit_relocation(scratch, memory.symbol, kind, width, memory_relocation_addend))
                return BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY;
            if (!buster_x86_metadata_emit_write_le(scratch, 0, width)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
        else if (!buster_x86_metadata_emit_write_le(scratch, (u64)memory.displacement, width))
            return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    if (pattern.has_modrm)
    {
        u8 reg_field = (u8)(reg_index & 7);
        if (pattern.reg_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY) reg_field = (u8)pattern.reg_fixed;
        if (pattern.has_reg_range) reg_field = (u8)(reg_index & 7);
        if (has_memory)
        {
            if (!buster_x86_metadata_emit_write_byte(scratch, (u8)((address.mod << 6) | ((reg_field & 7) << 3) | address.rm)))
                return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
            if (address.has_sib && !buster_x86_metadata_emit_write_byte(scratch, address.sib)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
            if (address.displacement_width)
            {
                if (address.has_symbol)
                {
                    u8 kind = address.rip_relative ? buster_x86_metadata_emit_pc_relocation_kind(address.displacement_width)
                                                    : buster_x86_metadata_emit_absolute_address_relocation_kind(address.displacement_width,
                                                                                                                   address.address32_absolute);
                    if (!buster_x86_metadata_emit_relocation(scratch, memory.symbol, kind, address.displacement_width,
                                                             memory_relocation_addend))
                        return BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY;
                    if (!buster_x86_metadata_emit_write_le(scratch, 0, address.displacement_width))
                        return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
                }
                else if (!buster_x86_metadata_emit_write_le(scratch, (u64)address.displacement, address.displacement_width))
                    return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
            }
        }
        else
        {
            u8 rm_field = (u8)(rm_index & 7);
            if (pattern.rm_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY) rm_field = (u8)pattern.rm_fixed;
            if (!buster_x86_metadata_emit_write_byte(scratch, (u8)(0xc0 | ((reg_field & 7) << 3) | (rm_field & 7))))
                return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
    }
    for (u32 index = 0; index < pattern.trailing_count; index += 1)
    {
        if (!buster_x86_metadata_emit_write_byte(scratch, pattern.trailing[index])) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    u8 field_immediate = simple_bindings ? (plan ? plan->exact_bind_immediate : facts->bind_immediate) : UINT8_MAX;
    BusterX86MetadataPhysicalBinding* immediate_binding =
        simple_bindings && pattern.immediate_count <= 1 && field_immediate != UINT8_MAX
            ? bindings + field_immediate
            : buster_x86_metadata_emit_field_binding(bindings, binding_count, BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE);
    if (!immediate_binding)
    {
        for (u32 index = 0; index < binding_count; index += 1)
        {
            if (bindings[index].physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE)
            {
                immediate_binding = bindings + index;
                break;
            }
        }
    }
    bool selector_immediate_combined = false;
    if (pattern.selector_immediate)
    {
        u8 selector_byte = (u8)(selector_binding->physical.reg.index << 4);
        // SE_IMM8 is a packed selector in the AMD VPERMIL2 family: the
        // selector register occupies the high nibble and the explicit
        // 4-bit immediate occupies the low nibble of the same trailing byte.
        // Forms such as VPCMOV have no immediate operand and retain the
        // register-only spelling.
        if (immediate_binding)
        {
            BusterX86MetadataPhysicalOperand immediate = immediate_binding->physical;
            if (immediate.has_symbol || (!immediate.has_value && !immediate.has_unsigned_value))
                return BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE;
            u64 value = immediate.has_unsigned_value ? immediate.unsigned_value : (u64)immediate.value;
            if ((immediate.has_value && immediate.value < 0) || value > 0xf)
                return BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE;
            selector_byte |= (u8)value;
            selector_immediate_combined = true;
        }
        if (!buster_x86_metadata_emit_write_byte(scratch, selector_byte))
            return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    if (pattern.immediate_count > BUSTER_ARRAY_LENGTH(pattern.immediate_widths))
        return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
    u32 immediate_count = (u32)pattern.immediate_count;
    if (selector_immediate_combined) immediate_count = 0;
    // ONE() is an implicit constant encoded in the opcode.  When source
    // projection consumed its explicit value-1 operand, retain the binding
    // for validation but never turn it into a trailing immediate byte.
    if (!immediate_count && immediate_binding && !pattern.has_implicit_one && !selector_immediate_combined) immediate_count = 1;
    for (u32 immediate_index = 0; immediate_index < immediate_count; immediate_index += 1)
    {
        BusterX86MetadataPhysicalBinding* binding = pattern.immediate_count
                                                         ? buster_x86_metadata_emit_nth_immediate_binding(bindings, binding_count,
                                                                                                           immediate_index)
                                                         : immediate_binding;
        if (!binding)
        {
            if (diagnostic_operand) *diagnostic_operand = immediate_index;
            return pattern.immediate_count ? BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH
                                            : BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
        }
        u8 width = (u8)pattern.immediate_width;
        u8 signed_immediate = (u8)pattern.immediate_signed;
        u8 variable = (u8)pattern.immediate_variable;
        if (pattern.immediate_count)
        {
            width = (u8)pattern.immediate_widths[immediate_index];
            signed_immediate = (u8)pattern.immediate_signeds[immediate_index];
            variable = (u8)pattern.immediate_variables[immediate_index];
        }
        if (!width && variable)
            width = buster_x86_metadata_emit_variable_width(data_width, form.immediate_width, !signed_immediate);
        if (!width) width = form.immediate_width;
        if (!width) width = buster_x86_metadata_emit_operand_width(binding->physical) == 8 ? 1 : 4;
        BusterX86MetadataPhysicalOperand immediate = binding->physical;
        if (!immediate.has_symbol && !immediate.has_value && !immediate.has_unsigned_value)
            return BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE;
        if (!immediate.has_symbol &&
            !(signed_immediate ?
                  (immediate.has_value && buster_x86_metadata_emit_signed_fits(immediate.value, width))
                                      : buster_x86_metadata_emit_unsigned_operand_fits(immediate, width)))
        {
            if (immediate.has_unsigned_value) buster_x86_metadata_emit_diagnostic_u64(diagnostic_value, immediate.unsigned_value);
            else if (diagnostic_value) *diagnostic_value = immediate.value;
            return BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE;
        }
        if (immediate.has_symbol)
        {
            if (!buster_x86_metadata_emit_relocation(scratch, immediate.symbol, buster_x86_metadata_emit_absolute_relocation_kind(width), width,
                                                     immediate.addend))
                return BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY;
            if (!buster_x86_metadata_emit_write_le(scratch, 0, width)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
        else if (!buster_x86_metadata_emit_write_le(scratch, immediate.has_unsigned_value ? immediate.unsigned_value : (u64)immediate.value,
                                                    width))
            return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    u8 field_relative = simple_bindings ? (plan ? plan->exact_bind_relative : facts->bind_relative) : UINT8_MAX;
    BusterX86MetadataPhysicalBinding* relative_binding =
        simple_bindings && pattern.relative_count <= 1 && field_relative != UINT8_MAX
            ? bindings + field_relative
            : buster_x86_metadata_emit_field_binding(bindings, binding_count, BUSTER_X86_METADATA_FIELD_SOURCE_RELATIVE);
    if (!relative_binding)
    {
        for (u32 index = 0; index < binding_count; index += 1)
        {
            if (bindings[index].physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE)
            {
                relative_binding = bindings + index;
                break;
            }
        }
    }
    if (pattern.relative_count > 1) return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
    if (relative_binding)
    {
        u8 width = pattern.relative_width;
        if (!width && pattern.relative_variable)
            width = buster_x86_metadata_emit_variable_width(buster_x86_metadata_emit_operand_width(relative_binding->physical),
                                                              form.displacement_width, false);
        if (!width) width = form.displacement_width;
        if (!width) return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
        BusterX86MetadataPhysicalOperand relative = relative_binding->physical;
        if (relative.width && relative.width != (u16)width * 8)
        {
            if (diagnostic_operand) *diagnostic_operand = relative_binding->actual_index;
            if (diagnostic_value) *diagnostic_value = relative.width;
            return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        }
        if (!relative.has_symbol && !relative.has_value) return BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE;
        if (relative.has_unsigned_value)
        {
            return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        }
        if (!relative.has_symbol && !buster_x86_metadata_emit_signed_fits(relative.value, width))
        {
            if (diagnostic_value) *diagnostic_value = relative.value;
            return BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE;
        }
        if (relative.has_symbol)
        {
            if (!buster_x86_metadata_emit_relocation(scratch, relative.symbol, buster_x86_metadata_emit_pc_relocation_kind(width), width,
                                                     relative.addend))
                return BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY;
            if (!buster_x86_metadata_emit_write_le(scratch, 0, width)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
        else if (!buster_x86_metadata_emit_write_le(scratch, (u64)relative.value, width))
            return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    BusterX86MetadataPhysicalBinding* absolute_binding = 0;
    for (u32 index = 0; index < binding_count; index += 1)
    {
        if (bindings[index].physical.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE ||
            bindings[index].metadata.kind == BUSTER_X86_METADATA_OPERAND_ABSOLUTE)
        {
            absolute_binding = bindings + index;
            break;
        }
    }
    if (absolute_binding)
    {
        u8 width = pattern.relative_width;
        if (!width && pattern.relative_variable)
            width = buster_x86_metadata_emit_variable_width(buster_x86_metadata_emit_operand_width(absolute_binding->physical),
                                                              form.displacement_width, false);
        if (!width) width = form.displacement_width;
        if (!width) return BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
        BusterX86MetadataPhysicalOperand absolute = absolute_binding->physical;
        if (absolute.width && absolute.width != (u16)width * 8)
        {
            if (diagnostic_operand) *diagnostic_operand = absolute_binding->actual_index;
            if (diagnostic_value) *diagnostic_value = absolute.width;
            return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        }
        if (!absolute.has_symbol && !absolute.has_value && !absolute.has_unsigned_value)
            return BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE;
        if (!absolute.has_symbol && !buster_x86_metadata_emit_unsigned_operand_fits(absolute, width))
        {
            if (absolute.has_unsigned_value) buster_x86_metadata_emit_diagnostic_u64(diagnostic_value, absolute.unsigned_value);
            else if (diagnostic_value) *diagnostic_value = absolute.value;
            return BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE;
        }
        if (absolute.has_symbol)
        {
            if (!buster_x86_metadata_emit_relocation(scratch, absolute.symbol, buster_x86_metadata_emit_absolute_relocation_kind(width), width,
                                                     absolute.addend))
                return BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY;
            if (!buster_x86_metadata_emit_write_le(scratch, 0, width)) return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        }
        else if (!buster_x86_metadata_emit_write_le(scratch, absolute.has_unsigned_value ? absolute.unsigned_value : (u64)absolute.value, width))
            return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    for (u32 index = 0; index < scratch->relocation_count; index += 1)
    {
        BusterX86MetadataRelocation* relocation = scratch->relocations + index;
        if (relocation->kind >= BUSTER_X86_METADATA_RELOCATION_PC8 && relocation->kind <= BUSTER_X86_METADATA_RELOCATION_PC64)
        {
            u64 distance = scratch->byte_count - relocation->offset;
            if (distance > (u64)INT64_MAX || relocation->addend < INT64_MIN + (s64)distance)
                return BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE;
            relocation->addend -= (s64)distance;
        }
    }
#undef pattern
    return buster_x86_metadata_instruction_length_status(scratch->byte_count);
}

BusterX86MetadataEncodeStatus buster_x86_metadata_instruction_length_status(u32 byte_count)
{
    return byte_count > 15 ? BUSTER_X86_METADATA_ENCODE_INSTRUCTION_LENGTH : BUSTER_X86_METADATA_ENCODE_SUCCESS;
}

// Keep the machine projection intentionally conservative.  The projection is
// useful only when every byte-changing choice is either immutable metadata or
// one of the small scalar fields handled by the direct writer below.  A second
// template projection handles zero-operand and fixed-width relative rows by
// asking this same metadata transform for canonical bytes during serial
// preparation.  Any richer pattern (vector prefixes, dynamic opcodes, aliases,
// decorators, symbols, VSIB, and so on) leaves machine_fast_kind at NONE and
// uses the checked transform unchanged.
BUSTER_GLOBAL_LOCAL void buster_x86_metadata_machine_fast_prepare(BusterX86MetadataExactPlanRecord* plan,
                                                                    BusterX86MetadataForm form,
                                                                    BusterX86MetadataPatternSemantics pattern)
{
    plan->machine_fast_kind = BUSTER_X86_METADATA_MACHINE_FAST_NONE;
    plan->machine_fast_binding_count = 0;
    plan->machine_fast_reg_binding = UINT8_MAX;
    plan->machine_fast_rm_binding = UINT8_MAX;
    plan->machine_fast_memory_binding = UINT8_MAX;
    plan->machine_fast_immediate_binding = UINT8_MAX;
    plan->machine_fast_register_count = 0;
    plan->machine_fast_data_width_count = 0;
    plan->machine_fast_template_count = 0;
    plan->machine_fast_template_relative_width = 0;
    plan->machine_fast_template_relative_offset = 0;
    plan->machine_fast_template_has_relative = 0;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(plan->machine_fast_template); index += 1)
    {
        plan->machine_fast_template[index] = 0;
    }
    for (u32 index = 0; index < BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY; index += 1)
    {
        plan->machine_fast_register_bindings[index] = UINT8_MAX;
        plan->machine_fast_data_width_bindings[index] = UINT8_MAX;
        plan->machine_fast_binding_kind[index] = BUSTER_X86_METADATA_MACHINE_FAST_BINDING_NONE;
        plan->machine_fast_binding_width_flags[index] = BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
    }
    for (u32 index = 0; index < BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY; index += 1)
    {
        plan->machine_fast_binding_metadata[index] = UINT8_MAX;
        plan->machine_fast_binding_source[index] = BUSTER_X86_METADATA_FIELD_SOURCE_NONE;
    }

    // Fixed zero/relative rows have no register, addressing, decorator, or
    // feature-dependent byte choices left for a worker.  Produce the bytes
    // through the ordinary metadata transform once here and retain only the
    // resulting immutable template.  A synthetic trusted token suppresses
    // target-feature filtering during preparation; the real machine token
    // has already enforced that policy before workers can reach this path.
    bool template_shape = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                          !form.decorator_flags && !form.apx_flags && !form.amx_flags && !pattern.unresolved_blocker &&
                          !pattern.has_unsupported_token && pattern.opcode_count &&
                          pattern.opcode_count <= BUSTER_ARRAY_LENGTH(pattern.opcode) &&
                          pattern.trailing_count <= BUSTER_ARRAY_LENGTH(pattern.trailing) &&
                          !pattern.has_dynamic_opcode && !pattern.has_sib && !pattern.force_sib &&
                          !pattern.selector_immediate && !pattern.has_implicit_one && !pattern.has_modep5 &&
                          !pattern.has_address_control && !pattern.has_decorator_control &&
                          !pattern.has_apx_control && !pattern.has_amx_control && !pattern.has_vsib_control &&
                          !pattern.has_segment_override && !pattern.has_remove_segment && !pattern.has_cet_control &&
                          !pattern.has_cet_no_track && !pattern.lock_control &&
                          !pattern.rep_control && !pattern.rep_not_f3 && !pattern.has_mpx_mode && !pattern.has_p4_control &&
                          !pattern.has_tuple_control && !pattern.has_tile_control && !pattern.has_element_size_control &&
                          !pattern.has_sae_control && !pattern.has_rounding_control && !pattern.mask_control &&
                          !pattern.zeroing_control && !pattern.rounding_length &&
                          (!pattern.mode_control || pattern.mode_control == BUSTER_X86_METADATA_PATTERN_MODE_64) &&
                          !pattern.operand_size_control &&
                          (!pattern.has_force64_control || (form.mode_flags & BUSTER_X86_METADATA_MODE_64)) &&
                          !pattern.has_encdelete_control &&
                          !pattern.relative_variable && !pattern.immediate_variable && !pattern.has_reg_range &&
                          !pattern.has_vsib_control && !pattern.required_address_size;
    u32 visible_operand_count = 0;
    u8 relative_metadata_index = UINT8_MAX;
    bool hidden_operands_safe = true;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        if (plan->operands[operand_index].visible)
        {
            visible_operand_count += 1;
            if (plan->operands[operand_index].kind == BUSTER_X86_METADATA_OPERAND_RELATIVE)
                relative_metadata_index = (u8)operand_index;
        }
        else
        {
            BusterX86MetadataOperand metadata = plan->operands[operand_index];
            bool hidden_register_source = metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
                                          (plan->effective_field_sources[operand_index] == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED ||
                                           (metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_REG &&
                                            plan->effective_field_sources[operand_index] == BUSTER_X86_METADATA_FIELD_SOURCE_REG)) &&
                                          (plan->operand_flags[operand_index] & ~BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MOFFS_FIXED_ACCUMULATOR) == 0;
            if (!hidden_register_source)
            {
                hidden_operands_safe = false;
            }
        }
    }
    bool template_shape_ready = template_shape && !plan->pattern_control_blocker && hidden_operands_safe;
    bool template_zero = template_shape_ready && visible_operand_count == 0 && !pattern.relative_count && !pattern.immediate_count &&
                         !pattern.has_modrm && !pattern.has_memory;
    bool template_relative = template_shape_ready && visible_operand_count == 1 && relative_metadata_index != UINT8_MAX &&
                             pattern.relative_count == 1 && !pattern.immediate_count && !pattern.has_modrm && !pattern.has_memory &&
                             !pattern.trailing_count && form.displacement_width && form.displacement_width <= 8;
    if (template_zero || template_relative)
    {
        String8 wildcard_feature = S8("*");
        BusterX86MetadataPhysicalOperand relative_operand = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
            .width = (u16)(form.displacement_width * 8),
            .value = 0,
            .has_value = true,
        };
        BusterX86MetadataMachineExactToken trusted_template_token = {
            .policy_flags = BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_POLICY_VALID | BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_ALLOWS_APX,
        };
        BusterX86MetadataPhysicalQuery template_query = {
            .mnemonic = buster_x86_metadata_string_span(form.iclass),
            .operands = template_relative ? &relative_operand : 0,
            .operand_count = template_relative ? 1u : 0u,
            .features = {.names = &wildcard_feature, .count = 1},
            .address_size = 64,
            .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        };
        BusterX86MetadataEncodeScratch scratch = {0};
        u32 diagnostic_operand = 0;
        s64 diagnostic_value = 0;
        BusterX86MetadataEncodeStatus template_status = buster_x86_metadata_emit_form_to_scratch(
            template_query, form, &scratch, &diagnostic_operand, &diagnostic_value, plan, &trusted_template_token, false, false);
        if (template_status == BUSTER_X86_METADATA_ENCODE_SUCCESS && !scratch.relocation_count && scratch.byte_count <= 15)
        {
            u8 relative_offset = 0;
            bool relative_layout_valid = true;
            if (template_relative)
            {
                if (scratch.byte_count < form.displacement_width)
                {
                    relative_layout_valid = false;
                }
                else
                {
                    // The sole relative field is structurally final because
                    // this shape has no trailing metadata fields.  Keep the
                    // offset derived from the metadata-produced length rather
                    // than duplicating an opcode-specific byte layout.
                    relative_offset = (u8)(scratch.byte_count - form.displacement_width);
                }
            }
            if (relative_layout_valid)
            {
                plan->machine_fast_kind = BUSTER_X86_METADATA_MACHINE_FAST_TEMPLATE;
                plan->machine_fast_template_count = (u8)scratch.byte_count;
                plan->machine_fast_template_has_relative = template_relative;
                if (template_relative)
                {
                    plan->machine_fast_template_relative_width = form.displacement_width;
                    plan->machine_fast_template_relative_offset = relative_offset;
                }
                memcpy(plan->machine_fast_template, scratch.bytes, scratch.byte_count);
                return;
            }
        }
    }

    if (form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED ||
        (form.prefix_kind != BUSTER_X86_METADATA_PREFIX_LEGACY && form.prefix_kind != BUSTER_X86_METADATA_PREFIX_REX) ||
        (form.encoder_family != BUSTER_X86_METADATA_ENCODER_LEGACY && form.encoder_family != BUSTER_X86_METADATA_ENCODER_REX) ||
        pattern.prefix_kind != form.prefix_kind ||
        form.decorator_flags || form.apx_flags || form.amx_flags || pattern.unresolved_blocker || pattern.has_unsupported_token ||
        !pattern.opcode_count || pattern.opcode_count > BUSTER_ARRAY_LENGTH(pattern.opcode) ||
        pattern.trailing_count > BUSTER_ARRAY_LENGTH(pattern.trailing) || pattern.has_dynamic_opcode ||
        pattern.has_reg_range || pattern.has_sib || pattern.force_sib || pattern.selector_immediate || pattern.has_implicit_one ||
        pattern.has_ignore_66 || pattern.relative_count || pattern.relative_variable || pattern.immediate_count > 1 ||
        (pattern.immediate_variable && (!form.immediate_width || !pattern.immediate_signed)) ||
        pattern.mode_control || pattern.operand_size_control || pattern.has_prefix_control ||
        pattern.has_branch_hint_control || pattern.has_force64_control || pattern.has_cet_control || pattern.has_cet_no_track ||
        pattern.has_encdelete_control || pattern.has_address_control || pattern.has_decorator_control || pattern.has_apx_control ||
        pattern.has_amx_control || pattern.has_vsib_control || pattern.has_prefix_kind || pattern.explicit_evex_selector ||
        pattern.has_nd || pattern.has_nf || pattern.has_bcrc || pattern.has_ubit || pattern.has_lzcnt_control ||
        pattern.has_tzcnt_control || pattern.has_cldemote_control || pattern.has_ibhf_control || pattern.has_prefetchrst_control ||
        pattern.has_prefetchit_control || pattern.has_explicit_vector_length || pattern.has_scc || pattern.has_evex_r4 ||
        pattern.lock_control || pattern.rep_control || pattern.rep_not_f3 || pattern.has_modep5 || pattern.has_rep_selector ||
        pattern.has_mpx_mode || pattern.has_segment_override || pattern.has_remove_segment || pattern.rex_b_control ||
        pattern.rex_b4_control || pattern.has_p4_control || pattern.immune66 || pattern.immune66_loop64 || pattern.immune_rexw ||
        pattern.df64 || (pattern.required_address_size && pattern.required_address_size != 64) || pattern.forbid_address_override ||
        pattern.forbid_operand_size_override || pattern.forbid_mandatory_prefix || pattern.not16 || pattern.no_vector_source ||
        pattern.apx_fixed_width_no_w_isa || pattern.no_scc || pattern.short_ud0 || pattern.has_tuple_control || pattern.has_tile_control ||
        pattern.has_element_size_control || pattern.has_sae_control || pattern.has_rounding_control || pattern.mask_control ||
        pattern.zeroing_control || pattern.rounding_length)
        return;

    u32 binding_count = 0;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = plan->operands[operand_index];
        if (!metadata.visible)
        {
            if (!hidden_operands_safe)
            {
                return;
            }
            continue;
        }
        if (plan->operand_flags[operand_index] || binding_count >= BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY)
        {
            return;
        }
        u8 source = plan->effective_field_sources[operand_index];
        bool scalar_register = metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
                               metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        bool scalar_memory = (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY ||
                              metadata.kind == BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR) &&
                             metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY;
        bool scalar_immediate = metadata.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE &&
                                metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_IMMEDIATE;
        if ((!scalar_register && !scalar_memory && !scalar_immediate) ||
            (scalar_register && source != BUSTER_X86_METADATA_FIELD_SOURCE_REG &&
             source != BUSTER_X86_METADATA_FIELD_SOURCE_RM) ||
            (scalar_memory && source != BUSTER_X86_METADATA_FIELD_SOURCE_RM) ||
            (scalar_immediate && source != BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE))
            return;
        u8 binding = (u8)binding_count;
        u8 binding_kind = BUSTER_X86_METADATA_MACHINE_FAST_BINDING_IMMEDIATE;
        if (scalar_register) binding_kind = BUSTER_X86_METADATA_MACHINE_FAST_BINDING_REGISTER;
        else if (scalar_memory)
        {
            binding_kind = BUSTER_X86_METADATA_MACHINE_FAST_BINDING_MEMORY;
            if (metadata.kind == BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR)
                binding_kind = BUSTER_X86_METADATA_MACHINE_FAST_BINDING_ADDRESS_GENERATOR;
        }
        plan->machine_fast_binding_kind[binding] = binding_kind;
        plan->machine_fast_binding_width_flags[binding] = metadata.physical_width_flags;
        plan->machine_fast_binding_metadata[binding] = (u8)operand_index;
        plan->machine_fast_binding_source[binding] = source;
        if (binding_kind == BUSTER_X86_METADATA_MACHINE_FAST_BINDING_REGISTER ||
            binding_kind == BUSTER_X86_METADATA_MACHINE_FAST_BINDING_MEMORY)
        {
            if (plan->machine_fast_data_width_count >= BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY) return;
            plan->machine_fast_data_width_bindings[plan->machine_fast_data_width_count++] = binding;
        }
        if (source == BUSTER_X86_METADATA_FIELD_SOURCE_REG)
        {
            if (plan->machine_fast_reg_binding != UINT8_MAX) return;
            plan->machine_fast_reg_binding = binding;
            if (plan->machine_fast_register_count >= BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY) return;
            plan->machine_fast_register_bindings[plan->machine_fast_register_count++] = binding;
        }
        else if (source == BUSTER_X86_METADATA_FIELD_SOURCE_RM)
        {
            if (plan->machine_fast_rm_binding != UINT8_MAX) return;
            plan->machine_fast_rm_binding = binding;
            if (scalar_memory) plan->machine_fast_memory_binding = binding;
            if (scalar_register)
            {
                if (plan->machine_fast_register_count >= BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY) return;
                plan->machine_fast_register_bindings[plan->machine_fast_register_count++] = binding;
            }
        }
        else if (source == BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE)
        {
            if (plan->machine_fast_immediate_binding != UINT8_MAX) return;
            plan->machine_fast_immediate_binding = binding;
        }
        binding_count += 1;
    }
    if (binding_count != visible_operand_count ||
        (!pattern.has_modrm && (plan->machine_fast_reg_binding != UINT8_MAX || plan->machine_fast_rm_binding != UINT8_MAX)) ||
        (pattern.has_modrm && plan->machine_fast_reg_binding == UINT8_MAX &&
         pattern.reg_fixed == BUSTER_X86_METADATA_PATTERN_FIXED_ANY) ||
        (pattern.has_modrm && plan->machine_fast_rm_binding == UINT8_MAX &&
         pattern.rm_fixed == BUSTER_X86_METADATA_PATTERN_FIXED_ANY) ||
        (pattern.has_modrm && pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_REGISTER &&
         plan->machine_fast_memory_binding != UINT8_MAX) ||
        (pattern.has_modrm && pattern.mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_MEMORY &&
         plan->machine_fast_memory_binding == UINT8_MAX))
        return;
    plan->machine_fast_binding_count = (u8)binding_count;
    plan->machine_fast_kind = BUSTER_X86_METADATA_MACHINE_FAST_SCALAR;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_machine_fast(
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataForm const* form, BusterX86MetadataExactPlanRecord const* plan,
    BusterX86MetadataEmitResult* result, u8* output, u32 output_capacity, bool force_disp32)
{
    if (!plan || (plan->machine_fast_kind != BUSTER_X86_METADATA_MACHINE_FAST_SCALAR &&
                  plan->machine_fast_kind != BUSTER_X86_METADATA_MACHINE_FAST_TEMPLATE))
        return false;
    // Machine exact queries are normalized by the public entry point.  Keep a
    // strict check here so this helper cannot accidentally become a second
    // public policy path if that ABI grows new controls later.
    if (query.address_size != 64 || query.execution_mode != BUSTER_X86_METADATA_EXECUTION_MODE_64 || query.include_privileged ||
        query.include_not64 || query.include_implicit || query.source_semantics || query.attributes.decorator_flags ||
        query.attributes.apx_flags || query.attributes.amx_flags || query.attributes.has_mask_register || query.attributes.zeroing ||
        query.attributes.sae || query.attributes.no_flags || query.attributes.lock || query.attributes.rep || query.attributes.repne ||
        query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE || query.attributes.branch_hint != BUSTER_X86_METADATA_BRANCH_HINT_NONE ||
        query.attributes.notrack || query.attributes.has_dfv)
        return false;

    if (plan->machine_fast_kind == BUSTER_X86_METADATA_MACHINE_FAST_TEMPLATE)
    {
        u32 expected_operand_count = plan->machine_fast_template_has_relative ? 1u : 0u;
        if (query.operand_count != expected_operand_count)
        {
            result->status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
            result->diagnostic_operand = query.operand_count < expected_operand_count ? query.operand_count : expected_operand_count;
            return true;
        }
        BusterX86MetadataPhysicalOperand const* relative = expected_operand_count ? query.operands : 0;
        if (relative)
        {
            if (relative->kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE ||
                (relative->width && relative->width != (u16)plan->machine_fast_template_relative_width * 8))
            {
                result->status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
                result->diagnostic_operand = 0;
                return true;
            }
            if (relative->has_symbol || relative->symbol.pointer || relative->symbol.length || relative->has_unsigned_value)
                return false;
            if (!relative->has_value)
            {
                result->status = BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE;
                result->diagnostic_operand = 0;
                return true;
            }
            if (!buster_x86_metadata_emit_signed_fits(relative->value, plan->machine_fast_template_relative_width))
            {
                result->status = BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE;
                result->diagnostic_operand = 0;
                result->diagnostic_value = relative->value;
                return true;
            }
        }
        u32 byte_count = plan->machine_fast_template_count;
        result->required_byte_count = byte_count;
        result->required_relocation_count = 0;
        result->status = buster_x86_metadata_instruction_length_status(byte_count);
        if (result->status != BUSTER_X86_METADATA_ENCODE_SUCCESS) return true;
        if (output_capacity < byte_count)
        {
            result->status = BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
            return true;
        }
        if (byte_count) memcpy(output, plan->machine_fast_template, byte_count);
        if (relative)
        {
            u32 offset = plan->machine_fast_template_relative_offset;
            u8 width = plan->machine_fast_template_relative_width;
            u64 value = (u64)relative->value;
            for (u8 index = 0; index < width; index += 1)
            {
                output[offset + index] = (u8)(value >> (index * 8));
            }
        }
        result->byte_count = byte_count;
        result->status = BUSTER_X86_METADATA_ENCODE_SUCCESS;
        return true;
    }
    if (query.operand_count != plan->machine_fast_binding_count)
    {
        result->status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        result->diagnostic_operand = query.operand_count < plan->machine_fast_binding_count ? query.operand_count
                                                                                             : plan->machine_fast_binding_count;
        return true;
    }

    bool has_qword_register = false;
    bool needs_low_byte_rex = false;
    for (u32 binding_index = 0; binding_index < plan->machine_fast_binding_count; binding_index += 1)
    {
        u8 metadata_index = plan->machine_fast_binding_metadata[binding_index];
        if (metadata_index >= plan->operand_count || plan->machine_fast_binding_source[binding_index] == BUSTER_X86_METADATA_FIELD_SOURCE_NONE)
            return false;
        BusterX86MetadataPhysicalOperand const* physical = query.operands + binding_index;
        u8 binding_kind = plan->machine_fast_binding_kind[binding_index];
        if (binding_kind == BUSTER_X86_METADATA_MACHINE_FAST_BINDING_REGISTER)
        {
            if (physical->kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
                physical->reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR)
            {
                result->status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
                result->diagnostic_operand = metadata_index;
                return true;
            }
            u16 width = physical->width ? physical->width : physical->reg.width;
            u16 width_flags = plan->machine_fast_binding_width_flags[binding_index];
            if (width && width_flags && width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN &&
                width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY &&
                !(width_flags & buster_x86_metadata_emit_width_flags(width)))
            {
                result->status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
                result->diagnostic_operand = metadata_index;
                return true;
            }
            if (physical->reg.index >= 16 || physical->reg.high_byte)
                return false;
            has_qword_register |= physical->reg.width == 64;
            needs_low_byte_rex |= physical->reg.width == 8 && !physical->reg.high_byte &&
                                  physical->reg.index >= 4 && physical->reg.index < 8;
        }
        else if (binding_kind == BUSTER_X86_METADATA_MACHINE_FAST_BINDING_MEMORY ||
                 binding_kind == BUSTER_X86_METADATA_MACHINE_FAST_BINDING_ADDRESS_GENERATOR)
        {
            if (physical->kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
            {
                result->status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
                result->diagnostic_operand = metadata_index;
                return true;
            }
            if (binding_kind == BUSTER_X86_METADATA_MACHINE_FAST_BINDING_MEMORY)
            {
                u16 width = physical->width;
                u16 width_flags = plan->machine_fast_binding_width_flags[binding_index];
                if (width && width_flags && width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN &&
                    width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY &&
                    !(width_flags & buster_x86_metadata_emit_width_flags(width)))
                {
                    result->status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
                    result->diagnostic_operand = metadata_index;
                    return true;
                }
            }
            BusterX86MetadataPhysicalMemory memory = physical->memory;
            if (!memory.has_base || memory.has_symbol || memory.symbol.pointer || memory.symbol.length || memory.has_index || memory.rip_relative ||
                memory.vsib || memory.scale > 1 || memory.has_segment || memory.segment != BUSTER_X86_METADATA_SEGMENT_NONE ||
                (memory.address_size && memory.address_size != 64) || (memory.has_base && memory.base.index >= 16))
                return false;
        }
        else if (binding_kind == BUSTER_X86_METADATA_MACHINE_FAST_BINDING_IMMEDIATE)
        {
            if (physical->kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE)
            {
                result->status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
                result->diagnostic_operand = metadata_index;
                return true;
            }
            if (physical->has_symbol || physical->symbol.pointer || physical->symbol.length ||
                (physical->has_value && physical->has_unsigned_value))
                return false;
            if (!physical->has_value && !physical->has_unsigned_value) {
                result->status = BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE;
                result->diagnostic_operand = metadata_index;
                return true;
            }
        }
        else
        {
            result->status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
            result->diagnostic_operand = metadata_index;
            return true;
        }
    }

    BusterX86MetadataPhysicalOperand const* reg_binding =
        plan->machine_fast_reg_binding == UINT8_MAX ? 0 : query.operands + plan->machine_fast_reg_binding;
    BusterX86MetadataPhysicalOperand const* rm_binding =
        plan->machine_fast_rm_binding == UINT8_MAX ? 0 : query.operands + plan->machine_fast_rm_binding;
    BusterX86MetadataPhysicalOperand const* memory_binding =
        plan->machine_fast_memory_binding == UINT8_MAX ? 0 : query.operands + plan->machine_fast_memory_binding;
    BusterX86MetadataPhysicalOperand const* immediate_binding =
        plan->machine_fast_immediate_binding == UINT8_MAX ? 0 : query.operands + plan->machine_fast_immediate_binding;
    bool has_memory = memory_binding != 0;
    bool has_register_rm = rm_binding && rm_binding->kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER;
    // Fixed ModRM fields are still checked against visible register operands
    // by the generic transform.  Fall back for a mismatch so the prepared
    // writer cannot silently replace an invalid operand with the fixed field.
    if (reg_binding && reg_binding->kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
        !buster_x86_metadata_emit_reg_matches(*plan->pattern, (u8)(reg_binding->reg.index & 7)))
        return false;
    if (!has_memory && has_register_rm && plan->pattern->rm_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY &&
        (rm_binding->reg.index & 7) != plan->pattern->rm_fixed)
        return false;
    if (plan->pattern->mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_MEMORY && !has_memory)
    {
        result->status = BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        return true;
    }
    if (plan->pattern->mod_kind == BUSTER_X86_METADATA_PATTERN_MOD_REGISTER && has_memory)
    {
        result->status = BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        return true;
    }

    BusterX86MetadataAddressEncoding address = {0};
    bool address_ready = !has_memory;
    if (has_memory)
    {
        BusterX86MetadataPhysicalMemory memory = memory_binding->memory;
        bool simple_address = memory.base.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
                              memory.base.width == 64 && !memory.base.high_byte && !form->displacement_scale &&
                              !(form->displacement_width && !form->relocation_base);
        if (simple_address)
        {
            u8 base = (u8)(memory.base.index & 7);
            address.has_sib = base == 4;
            address.rm = address.has_sib ? 4 : base;
            address.sib = address.has_sib ? 0x24 : 0;
            bool has_displacement = memory.has_displacement || memory.displacement != 0;
            if (force_disp32)
            {
                if (memory.displacement >= INT32_MIN && memory.displacement <= INT32_MAX)
                {
                    address.mod = 2;
                    address.displacement_width = 4;
                    address.displacement = memory.displacement;
                    address_ready = true;
                }
            }
            else if (!has_displacement && base == 5)
            {
                address.mod = 1;
                address.displacement_width = 1;
                address_ready = true;
            }
            else if (!has_displacement)
            {
                address.mod = 0;
                address_ready = true;
            }
            else if (memory.displacement >= -128 && memory.displacement <= 127)
            {
                address.mod = 1;
                address.displacement_width = 1;
                address.displacement = memory.displacement;
                address_ready = true;
            }
            else if (memory.displacement >= INT32_MIN && memory.displacement <= INT32_MAX)
            {
                address.mod = 2;
                address.displacement_width = 4;
                address.displacement = memory.displacement;
                address_ready = true;
            }
            if (address_ready && plan->pattern->mod_kind != BUSTER_X86_METADATA_PATTERN_MOD_ANY &&
                plan->pattern->mod_kind != BUSTER_X86_METADATA_PATTERN_MOD_MEMORY &&
                address.mod != plan->pattern->mod_kind)
                address_ready = false;
            if (address_ready && plan->pattern->rm_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY &&
                address.rm != plan->pattern->rm_fixed)
                address_ready = false;
        }
        if (!address_ready)
            address_ready = buster_x86_metadata_emit_address(memory, form, plan->pattern, force_disp32, &address);
    }
    if (has_memory && !address_ready)
    {
        result->status = BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        return true;
    }
    if (!has_memory && plan->machine_fast_rm_binding != UINT8_MAX && !has_register_rm)
    {
        result->status = BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        return true;
    }

    u16 data_width = 0;
    u32 width_index = 0;
    if (plan->machine_fast_data_width_count)
    {
        u8 binding_index = plan->machine_fast_data_width_bindings[0];
        BusterX86MetadataPhysicalOperand physical = query.operands[binding_index];
        u16 width = physical.width;
        if (!width && plan->machine_fast_binding_kind[binding_index] == BUSTER_X86_METADATA_MACHINE_FAST_BINDING_REGISTER)
            width = physical.reg.width;
        if (width == 16 || width == 32 || width == 64)
        {
            data_width = width;
        }
        width_index = data_width ? plan->machine_fast_data_width_count : 1;
    }
    for (; width_index < plan->machine_fast_data_width_count; width_index += 1)
    {
        u8 binding_index = plan->machine_fast_data_width_bindings[width_index];
        BusterX86MetadataPhysicalOperand physical = query.operands[binding_index];
        u16 width = physical.width;
        if (!width && plan->machine_fast_binding_kind[binding_index] == BUSTER_X86_METADATA_MACHINE_FAST_BINDING_REGISTER)
            width = physical.reg.width;
        if (width == 16 || width == 32 || width == 64)
        {
            data_width = width;
            break;
        }
    }
    bool x87_form = buster_x86_metadata_string_input_equal(form->extension.offset, S8("X87"));
    bool x87_no_operand_size_override = x87_form ||
                                        (buster_x86_metadata_string_input_equal(form->iclass.offset, S8("FISTTP")) &&
                                         data_width == 16);
    bool rex_w = plan->pattern->w != 0;
    bool has_r = false;
    bool has_b = false;
    if (plan->pattern->has_modrm && has_qword_register && !x87_form) rex_w = true;
    if (form->encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY && query.source_semantics &&
        (buster_x86_metadata_block_memory_source_authoritative(*form, query) ||
         buster_x86_metadata_aggregate_memory_source_authoritative(*form, query)))
        rex_w = false;
    // Signed variable immediates use the form's fixed width for 32/64-bit
    // scalar data, but the generic transform narrows them to the data width
    // for 16-bit operands.  Keep that less-common width on the checked path;
    // the machine recipes using this projection are 32/64-bit only.
    if (plan->pattern->immediate_variable && data_width == 16) return false;
    if (reg_binding && reg_binding->kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER) has_r = (reg_binding->reg.index & 8) != 0;
    if (has_memory) has_b = memory_binding->memory.has_base && (memory_binding->memory.base.index & 8) != 0;
    else if (rm_binding && rm_binding->kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER) has_b = (rm_binding->reg.index & 8) != 0;
    bool needs_rex = rex_w || has_r || has_b || needs_low_byte_rex;
    u8 mandatory_prefix = plan->pattern->mandatory_prefix ? plan->pattern->mandatory_prefix : form->mandatory_prefix;
    bool operand_size_override = data_width == 16 && mandatory_prefix != 0x66 && !x87_no_operand_size_override &&
                                 (form->prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY || form->prefix_kind == BUSTER_X86_METADATA_PREFIX_REX);

    u8 bytes[16];
    u32 byte_count = 0;
#define BUSTER_X86_METADATA_FAST_PUSH_BYTE(value) \
    do { if (byte_count >= BUSTER_ARRAY_LENGTH(bytes)) { result->status = BUSTER_X86_METADATA_ENCODE_INSTRUCTION_LENGTH; return true; } \
         bytes[byte_count++] = (u8)(value); } while (0)
#define BUSTER_X86_METADATA_FAST_PUSH_LE(value, width) \
    do { \
        u64 fast_value = (u64)(value); \
        u8 fast_width = (u8)(width); \
        if (byte_count + fast_width > BUSTER_ARRAY_LENGTH(bytes)) \
        { \
            result->status = BUSTER_X86_METADATA_ENCODE_INSTRUCTION_LENGTH; \
            return true; \
        } \
        u8 fast_index = 0; \
        for (; fast_index < fast_width; fast_index += 1) \
        { \
            bytes[byte_count++] = (u8)(fast_value >> (fast_index * 8)); \
        } \
    } while (0)
    if (operand_size_override) BUSTER_X86_METADATA_FAST_PUSH_BYTE(0x66);
    if (mandatory_prefix) BUSTER_X86_METADATA_FAST_PUSH_BYTE(mandatory_prefix);
    if (needs_rex) BUSTER_X86_METADATA_FAST_PUSH_BYTE(0x40 | (rex_w ? 8 : 0) | (has_r ? 4 : 0) | (has_b ? 1 : 0));
    for (u32 opcode_index = 0; opcode_index < plan->pattern->opcode_count; opcode_index += 1)
    {
        BUSTER_X86_METADATA_FAST_PUSH_BYTE(plan->pattern->opcode[opcode_index]);
    }

    if (plan->pattern->has_modrm)
    {
        u8 reg_field = reg_binding && reg_binding->kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ? (u8)(reg_binding->reg.index & 7) : 0;
        if (plan->pattern->reg_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY) reg_field = plan->pattern->reg_fixed;
        if (has_memory)
        {
            BUSTER_X86_METADATA_FAST_PUSH_BYTE((address.mod << 6) | ((reg_field & 7) << 3) | address.rm);
            if (address.has_sib) BUSTER_X86_METADATA_FAST_PUSH_BYTE(address.sib);
            if (address.displacement_width) BUSTER_X86_METADATA_FAST_PUSH_LE(address.displacement, address.displacement_width);
        }
        else
        {
            u8 rm_field = rm_binding ? (u8)(rm_binding->reg.index & 7) : 0;
            if (plan->pattern->rm_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY) rm_field = plan->pattern->rm_fixed;
            BUSTER_X86_METADATA_FAST_PUSH_BYTE(0xc0 | ((reg_field & 7) << 3) | (rm_field & 7));
        }
    }
    for (u32 trailing_index = 0; trailing_index < plan->pattern->trailing_count; trailing_index += 1)
    {
        BUSTER_X86_METADATA_FAST_PUSH_BYTE(plan->pattern->trailing[trailing_index]);
    }
    if (immediate_binding)
    {
        u8 width = plan->pattern->immediate_count ? plan->pattern->immediate_widths[0] : plan->pattern->immediate_width;
        bool signed_immediate = plan->pattern->immediate_count ? plan->pattern->immediate_signeds[0] : plan->pattern->immediate_signed;
        if (!width) width = form->immediate_width;
        if (!width) width = immediate_binding->width == 8 ? 1 : 4;
        if (!width || width > 8) return false;
        if (!immediate_binding->has_symbol &&
            !(signed_immediate ? (immediate_binding->has_value && buster_x86_metadata_emit_signed_fits(immediate_binding->value, width))
                               : buster_x86_metadata_emit_unsigned_operand_fits(*immediate_binding, width)))
        {
            if (immediate_binding->has_unsigned_value) buster_x86_metadata_emit_diagnostic_u64(&result->diagnostic_value, immediate_binding->unsigned_value);
            else result->diagnostic_value = immediate_binding->value;
            result->diagnostic_operand = plan->machine_fast_binding_metadata[plan->machine_fast_immediate_binding];
            result->status = BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE;
            return true;
        }
        BUSTER_X86_METADATA_FAST_PUSH_LE(immediate_binding->has_unsigned_value ? immediate_binding->unsigned_value : (u64)immediate_binding->value, width);
    }
#undef BUSTER_X86_METADATA_FAST_PUSH_LE
#undef BUSTER_X86_METADATA_FAST_PUSH_BYTE
    result->required_byte_count = byte_count;
    result->required_relocation_count = 0;
    result->status = buster_x86_metadata_instruction_length_status(byte_count);
    if (result->status != BUSTER_X86_METADATA_ENCODE_SUCCESS) return true;
    if (output_capacity < byte_count)
    {
        result->status = BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        return true;
    }
    if (byte_count) memcpy(output, bytes, byte_count);
    result->byte_count = byte_count;
    result->status = BUSTER_X86_METADATA_ENCODE_SUCCESS;
    return true;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataString buster_x86_metadata_emit_required_feature(
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataForm form);

BUSTER_GLOBAL_LOCAL BusterX86MetadataString buster_x86_metadata_apx_feature_string(void)
{
    for (u32 form_id = 0; form_id < BUSTER_X86_GENERATED_FORM_COUNT; form_id += 1)
    {
        BusterX86GeneratedForm generated = buster_x86_metadata_form_record(form_id);
        if (buster_x86_metadata_string_input_equal(generated.isa_set_offset, S8("APX_F")))
        {
            BusterX86MetadataString result = {0};
            if (buster_x86_metadata_string_offset_terminated(generated.isa_set_offset, &result.length))
            {
                result.offset = generated.isa_set_offset;
                return result;
            }
        }
    }
    return (BusterX86MetadataString){0};
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_prefetchit_address_valid(BusterX86MetadataPhysicalQuery query)
{
    bool prefetchit = buster_x86_metadata_input_string_equal(query.mnemonic, S8("prefetchit0")) ||
                      buster_x86_metadata_input_string_equal(query.mnemonic, S8("prefetchit1"));
    if (!prefetchit) return true;
    if (query.operand_count != 1 || query.operands[0].kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
    {
        return false;
    }
    BusterX86MetadataPhysicalMemory memory = query.operands[0].memory;
    u8 address_size = memory.address_size ? memory.address_size : query.address_size ? query.address_size : 64;
    return address_size == 64 && memory.rip_relative && !memory.has_base && !memory.has_index;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_eamode_alias_forms(BusterX86MetadataForm first,
                                                                 BusterX86MetadataForm second)
{
    BusterX86MetadataPatternSemantics first_pattern;
    BusterX86MetadataPatternSemantics second_pattern;
    if (!buster_x86_metadata_emit_parse_pattern(first, &first_pattern) ||
        !buster_x86_metadata_emit_parse_pattern(second, &second_pattern) ||
        !first_pattern.required_address_size || !second_pattern.required_address_size ||
        first_pattern.required_address_size == second_pattern.required_address_size)
        return false;

    first_pattern.required_address_size = 0;
    second_pattern.required_address_size = 0;
    if (first_pattern.opcode_count != second_pattern.opcode_count || first_pattern.trailing_count != second_pattern.trailing_count)
        return false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(first_pattern.opcode); index += 1)
    {
        if (first_pattern.opcode[index] != second_pattern.opcode[index]) return false;
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(first_pattern.trailing); index += 1)
    {
        if (first_pattern.trailing[index] != second_pattern.trailing[index]) return false;
    }
    if (memcmp(first_pattern.immediate_widths, second_pattern.immediate_widths, sizeof(first_pattern.immediate_widths)) != 0 ||
        memcmp(first_pattern.immediate_signeds, second_pattern.immediate_signeds, sizeof(first_pattern.immediate_signeds)) != 0 ||
        memcmp(first_pattern.immediate_variables, second_pattern.immediate_variables, sizeof(first_pattern.immediate_variables)) != 0)
        return false;
    if (first_pattern.mandatory_prefix != second_pattern.mandatory_prefix ||
        first_pattern.vector_length != second_pattern.vector_length || first_pattern.mod_kind != second_pattern.mod_kind ||
        first_pattern.reg_fixed != second_pattern.reg_fixed || first_pattern.has_reg_range != second_pattern.has_reg_range ||
        first_pattern.reg_min != second_pattern.reg_min || first_pattern.reg_max != second_pattern.reg_max ||
        first_pattern.rm_fixed != second_pattern.rm_fixed ||
        first_pattern.srm_fixed != second_pattern.srm_fixed || first_pattern.srm_not_equal != second_pattern.srm_not_equal ||
        first_pattern.srm_shift != second_pattern.srm_shift || first_pattern.srm_base != second_pattern.srm_base ||
        first_pattern.prefix_kind != second_pattern.prefix_kind || first_pattern.immediate_width != second_pattern.immediate_width ||
        first_pattern.immediate_signed != second_pattern.immediate_signed ||
        first_pattern.immediate_variable != second_pattern.immediate_variable ||
        first_pattern.selector_immediate != second_pattern.selector_immediate ||
        first_pattern.has_implicit_one != second_pattern.has_implicit_one || first_pattern.has_ignore_66 != second_pattern.has_ignore_66 ||
        first_pattern.relative_width != second_pattern.relative_width || first_pattern.relative_variable != second_pattern.relative_variable ||
        first_pattern.displacement_width != second_pattern.displacement_width || first_pattern.w != second_pattern.w ||
        first_pattern.has_w != second_pattern.has_w || first_pattern.map != second_pattern.map ||
        first_pattern.immediate_count != second_pattern.immediate_count || first_pattern.relative_count != second_pattern.relative_count ||
        first_pattern.displacement_count != second_pattern.displacement_count || first_pattern.has_modrm != second_pattern.has_modrm ||
        first_pattern.explicit_modrm != second_pattern.explicit_modrm || first_pattern.has_sib != second_pattern.has_sib ||
        first_pattern.has_memory != second_pattern.has_memory || first_pattern.has_register != second_pattern.has_register ||
        first_pattern.has_dynamic_opcode != second_pattern.has_dynamic_opcode ||
        first_pattern.has_srm_register != second_pattern.has_srm_register ||
        first_pattern.has_unsupported_token != second_pattern.has_unsupported_token ||
        first_pattern.has_prefix_control != second_pattern.has_prefix_control ||
        first_pattern.has_branch_hint_control != second_pattern.has_branch_hint_control ||
        first_pattern.has_force64_control != second_pattern.has_force64_control ||
        first_pattern.has_cet_control != second_pattern.has_cet_control || first_pattern.cet_value != second_pattern.cet_value ||
        first_pattern.has_cet_no_track != second_pattern.has_cet_no_track ||
        first_pattern.has_encdelete_control != second_pattern.has_encdelete_control ||
        first_pattern.has_address_control != second_pattern.has_address_control ||
        first_pattern.has_decorator_control != second_pattern.has_decorator_control ||
        first_pattern.has_apx_control != second_pattern.has_apx_control || first_pattern.has_amx_control != second_pattern.has_amx_control ||
        first_pattern.has_vsib_control != second_pattern.has_vsib_control || first_pattern.has_prefix_kind != second_pattern.has_prefix_kind ||
        first_pattern.explicit_evex_selector != second_pattern.explicit_evex_selector || first_pattern.has_nd != second_pattern.has_nd ||
        first_pattern.nd_value != second_pattern.nd_value || first_pattern.has_nf != second_pattern.has_nf ||
        first_pattern.nf_value != second_pattern.nf_value || first_pattern.has_bcrc != second_pattern.has_bcrc ||
        first_pattern.bcrc_value != second_pattern.bcrc_value || first_pattern.has_ubit != second_pattern.has_ubit ||
        first_pattern.ubit_value != second_pattern.ubit_value ||
        first_pattern.has_lzcnt_control != second_pattern.has_lzcnt_control ||
        first_pattern.lzcnt_control_value != second_pattern.lzcnt_control_value ||
        first_pattern.has_tzcnt_control != second_pattern.has_tzcnt_control ||
        first_pattern.tzcnt_control_value != second_pattern.tzcnt_control_value ||
        first_pattern.has_cldemote_control != second_pattern.has_cldemote_control ||
        first_pattern.cldemote_control_value != second_pattern.cldemote_control_value ||
        first_pattern.has_ibhf_control != second_pattern.has_ibhf_control ||
        first_pattern.ibhf_control_value != second_pattern.ibhf_control_value ||
        first_pattern.has_prefetchrst_control != second_pattern.has_prefetchrst_control ||
        first_pattern.prefetchrst_control_value != second_pattern.prefetchrst_control_value ||
        first_pattern.has_prefetchit_control != second_pattern.has_prefetchit_control ||
        first_pattern.prefetchit_control_value != second_pattern.prefetchit_control_value ||
        first_pattern.has_explicit_vector_length != second_pattern.has_explicit_vector_length ||
        first_pattern.has_scc != second_pattern.has_scc || first_pattern.scc_value != second_pattern.scc_value ||
        first_pattern.has_evex_r4 != second_pattern.has_evex_r4 || first_pattern.evex_r4_value != second_pattern.evex_r4_value ||
        first_pattern.force_sib != second_pattern.force_sib || first_pattern.lock_control != second_pattern.lock_control ||
        first_pattern.rep_control != second_pattern.rep_control || first_pattern.rep_not_f3 != second_pattern.rep_not_f3 ||
        first_pattern.has_modep5 != second_pattern.has_modep5 || first_pattern.modep5_value != second_pattern.modep5_value ||
        first_pattern.has_rep_selector != second_pattern.has_rep_selector ||
        first_pattern.rep_selector_value != second_pattern.rep_selector_value ||
        first_pattern.has_segment_override != second_pattern.has_segment_override ||
        first_pattern.segment_override_index != second_pattern.segment_override_index ||
        first_pattern.has_remove_segment != second_pattern.has_remove_segment || first_pattern.rex_b_control != second_pattern.rex_b_control ||
        first_pattern.rex_b4_control != second_pattern.rex_b4_control || first_pattern.has_p4_control != second_pattern.has_p4_control ||
        first_pattern.p4_value != second_pattern.p4_value ||
        first_pattern.immune66 != second_pattern.immune66 ||
        first_pattern.immune66_loop64 != second_pattern.immune66_loop64 ||
        first_pattern.immune_rexw != second_pattern.immune_rexw ||
        first_pattern.df64 != second_pattern.df64 ||
        first_pattern.required_address_size != second_pattern.required_address_size ||
        first_pattern.forbid_address_override != second_pattern.forbid_address_override ||
        first_pattern.forbid_operand_size_override != second_pattern.forbid_operand_size_override ||
        first_pattern.forbid_mandatory_prefix != second_pattern.forbid_mandatory_prefix || first_pattern.not16 != second_pattern.not16 ||
        first_pattern.no_rexr_prefix != second_pattern.no_rexr_prefix ||
        first_pattern.no_rex2 != second_pattern.no_rex2 ||
        first_pattern.no_vector_source != second_pattern.no_vector_source ||
        first_pattern.apx_fixed_width_no_w_isa != second_pattern.apx_fixed_width_no_w_isa ||
        first_pattern.no_scc != second_pattern.no_scc || first_pattern.short_ud0 != second_pattern.short_ud0 ||
        first_pattern.has_tuple_control != second_pattern.has_tuple_control || first_pattern.tuple_control_kind != second_pattern.tuple_control_kind ||
        first_pattern.has_tile_control != second_pattern.has_tile_control ||
        first_pattern.vsib_vector_length != second_pattern.vsib_vector_length ||
        first_pattern.has_element_size_control != second_pattern.has_element_size_control ||
        first_pattern.element_size_bits != second_pattern.element_size_bits || first_pattern.has_sae_control != second_pattern.has_sae_control ||
        first_pattern.has_rounding_control != second_pattern.has_rounding_control || first_pattern.mask_control != second_pattern.mask_control ||
        first_pattern.zeroing_control != second_pattern.zeroing_control || first_pattern.rounding_length != second_pattern.rounding_length ||
        first_pattern.unresolved_blocker != second_pattern.unresolved_blocker)
        return false;
    if (first.operand_count != second.operand_count || first.encoder_family != second.encoder_family ||
        first.prefix_kind != second.prefix_kind || first.map != second.map ||
        first.fixed_byte_count != second.fixed_byte_count || first.mandatory_prefix != second.mandatory_prefix ||
        first.field_flags != second.field_flags || first.decorator_flags != second.decorator_flags ||
        first.apx_flags != second.apx_flags || first.amx_flags != second.amx_flags ||
        first.displacement_width != second.displacement_width || first.displacement_scale != second.displacement_scale ||
        first.immediate_width != second.immediate_width || first.immediate_signed != second.immediate_signed ||
        first.relocation_base != second.relocation_base || first.tuple_kind != second.tuple_kind ||
        first.element_size_offset != second.element_size_offset)
        return false;
    return memcmp(first.fixed_bytes, second.fixed_bytes, first.fixed_byte_count) == 0;
}

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_single_scalar_width(u16 flags)
{
    u16 scalar_flags = flags & (BUSTER_X86_METADATA_PHYSICAL_WIDTH_8 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_16 |
                                 BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_64);
    if (!scalar_flags || (scalar_flags & (u16)(scalar_flags - 1))) return 0;
    if (scalar_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_8) return 8;
    if (scalar_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_16) return 16;
    if (scalar_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_32) return 32;
    if (scalar_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_64) return 64;
    return 0;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_standalone_sae_capable_impl(BusterX86MetadataForm form)
{
    BusterX86MetadataPatternSemantics pattern = {0};
    if (!buster_x86_metadata_emit_parse_pattern(form, &pattern)) return false;
    return form.encoder_family == BUSTER_X86_METADATA_ENCODER_EVEX && !form.apx_flags && !form.amx_flags &&
           pattern.has_sae_control && pattern.rounding_length && !pattern.has_rounding_control && pattern.mask_control != 2 &&
           !pattern.has_vsib_control;
}

bool buster_x86_metadata_form_standalone_sae_capable(BusterX86MetadataForm form)
{
    return buster_x86_metadata_form_standalone_sae_capable_impl(form);
}

// AT&T memory operands intentionally omit the Intel-style scalar qualifier
// for EVEX broadcasts.  Infer that qualifier only while evaluating the
// candidate form whose schema supplies one unambiguous scalar width.  The
// copied physical array keeps the inference transactional: candidates that
// are not ordinary 64-bit typed broadcasts, and all later candidates, see
// the original query unchanged.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_prepare_typed_memory_query(
    BusterX86MetadataForm form, BusterX86MetadataPhysicalQuery query,
    BusterX86MetadataPhysicalOperand* candidate_operands)
{
    if (!candidate_operands || form.encoder_family != BUSTER_X86_METADATA_ENCODER_EVEX || form.apx_flags || form.amx_flags ||
        query.address_size != 64 || !(query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST) ||
        query.attributes.sae || query.attributes.rounding_mode != BUSTER_X86_METADATA_ROUNDING_NONE ||
        query.operand_count > 16)
        return false;
    u32 metadata_memory_count = 0;
    u16 expected_width = 0;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &metadata)) return false;
        if (metadata.kind != BUSTER_X86_METADATA_OPERAND_MEMORY &&
            metadata.kind != BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR)
            continue;
        metadata_memory_count += 1;
        u16 width = buster_x86_metadata_single_scalar_width(metadata.physical_width_flags);
        if (!width || (expected_width && expected_width != width)) return false;
        expected_width = width;
    }
    if (metadata_memory_count != 1 || !expected_width) return false;
    u32 physical_memory_count = 0;
    for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
        if (operand.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY) continue;
        physical_memory_count += 1;
        if (operand.memory.vsib || operand.memory.has_segment ||
            (operand.memory.address_size && operand.memory.address_size != 64))
            return false;
        if (operand.width || operand.memory.source_width) return false;
    }
    if (physical_memory_count != 1) return false;
    memcpy(candidate_operands, query.operands, query.operand_count * sizeof(*candidate_operands));
    for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
    {
        if (candidate_operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            candidate_operands[operand_index].width = expected_width;
            break;
        }
    }
    return true;
}

bool buster_x86_metadata_typed_decorator_authoritative(BusterX86MetadataForm form,
                                                        BusterX86MetadataPhysicalQuery query)
{
    u16 typed_flags = BUSTER_X86_METADATA_DECORATOR_MASK | BUSTER_X86_METADATA_DECORATOR_ZEROING |
                      BUSTER_X86_METADATA_DECORATOR_BROADCAST | BUSTER_X86_METADATA_DECORATOR_ROUNDING |
                      BUSTER_X86_METADATA_DECORATOR_SAE;
    u32 memory_count = 0;
    u32 ordinary_memory_count = 0;
    u32 vector_register_count = 0;
    u32 mask_register_count = 0;
    u32 form_mask_operand_count = 0;
    u32 form_nonmask_visible_count = 0;
    if (form.encoder_family != BUSTER_X86_METADATA_ENCODER_EVEX || form.apx_flags || form.amx_flags ||
        query.address_size != 64 || !query.operands || !(query.attributes.decorator_flags & typed_flags) ||
        query.attributes.apx_flags || query.attributes.amx_flags || query.attributes.has_dfv ||
        query.attributes.no_flags || query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE ||
        (query.attributes.decorator_flags & (u16)~form.decorator_flags))
        return false;
    if (form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_MASK)
    {
        for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
        {
            BusterX86MetadataOperand metadata = {0};
            if (!buster_x86_metadata_operand(form.id, metadata_index, &metadata)) return false;
            if (!metadata.visible) continue;
            if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
                metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK)
                form_mask_operand_count += 1;
            else
                form_nonmask_visible_count += 1;
        }
    }
    for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            memory_count += 1;
            if (!operand.memory.vsib && !operand.memory.has_segment &&
                (!operand.memory.address_size || operand.memory.address_size == 64))
                ordinary_memory_count += 1;
        }
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                 (operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM ||
                  operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM ||
                  operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM))
            vector_register_count += 1;
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                 operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK)
            mask_register_count += 1;
    }
    if (query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST)
        return memory_count == 1 && ordinary_memory_count == 1 && !query.attributes.sae &&
               query.attributes.rounding_mode == BUSTER_X86_METADATA_ROUNDING_NONE;
    if (query.attributes.sae || query.attributes.rounding_mode != BUSTER_X86_METADATA_ROUNDING_NONE)
    {
        // A visible MASKmskw operand is the ordinary masked EVEX decorator
        // topology.  Keep its typed SAE/rounding forms authoritative even
        // when the fixed-round maskless pattern discriminator below does not
        // apply; the metadata binding proves the mask slot and vector roles.
        if (mask_register_count == 1 && (form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_MASK))
            return memory_count == 0 && vector_register_count >= 2;
        // XED's MASKmskw slot is sometimes implicit k0 in the public
        // spelling.  The source query then omits that physical mask while
        // retaining the form's mask topology; accept only the parsed fixed
        // SAE capability with an exact non-mask visible-operand count.
        if (mask_register_count == 0 && form_mask_operand_count == 1 &&
            form_nonmask_visible_count == query.operand_count &&
            buster_x86_metadata_form_standalone_sae_capable_impl(form) && memory_count == 0 && vector_register_count >= 2)
        {
            for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
            {
                BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
                if (operand.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
                    (operand.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
                     operand.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM &&
                     operand.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM &&
                     operand.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM))
                    return false;
            }
            return true;
        }
        if (!buster_x86_metadata_form_standalone_sae_capable_impl(form) || memory_count != 0 || query.operand_count != 2 ||
            vector_register_count == 0)
            return false;
        for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
        {
            BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
            if (operand.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
                (operand.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM &&
                 operand.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR))
                return false;
        }
        return true;
    }
    return false;
}

bool buster_x86_metadata_legacy_xmm_memory_authoritative(BusterX86MetadataForm form,
                                                         BusterX86MetadataPhysicalQuery query)
{
    if (form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED ||
        form.encoder_family != BUSTER_X86_METADATA_ENCODER_LEGACY ||
        form.field_flags != (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_MEMORY |
                              BUSTER_X86_METADATA_FIELD_REGISTER) ||
        form.decorator_flags || form.apx_flags || form.amx_flags || !query.operands ||
        query.operand_count != 2 || query.address_size != 64 || query.execution_mode != BUSTER_X86_METADATA_EXECUTION_MODE_64 ||
        query.attributes.decorator_flags || query.attributes.apx_flags || query.attributes.amx_flags ||
        query.attributes.has_dfv || query.attributes.no_flags || query.attributes.lock || query.attributes.rep ||
        query.attributes.repne || query.attributes.notrack || query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE ||
        query.attributes.branch_hint != BUSTER_X86_METADATA_BRANCH_HINT_NONE)
        return false;

    u32 physical_xmm_count = 0;
    u32 physical_memory_count = 0;
    for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM && operand.reg.width == 128)
            physical_xmm_count += 1;
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && !operand.memory.vsib &&
                 !operand.memory.has_segment && (!operand.memory.address_size || operand.memory.address_size == 64))
            physical_memory_count += 1;
        else return false;
    }
    if (physical_xmm_count != 1 || physical_memory_count != 1) return false;

    u32 visible_count = 0;
    u32 register_destination_count = 0;
    u32 memory_source_count = 0;
    for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form.id, metadata_index, &metadata)) return false;
        if (!metadata.visible) continue;
        visible_count += 1;
        if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
            metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM &&
            metadata.physical_width_flags == BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 &&
            metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_REG &&
            (metadata.access & BUSTER_X86_METADATA_ACCESS_WRITE) && !(metadata.access & BUSTER_X86_METADATA_ACCESS_SUPPRESSED))
            register_destination_count += 1;
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY &&
                 metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY &&
                 metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_RM &&
                 (metadata.access & BUSTER_X86_METADATA_ACCESS_READ) && !(metadata.access & BUSTER_X86_METADATA_ACCESS_WRITE) &&
                 !(metadata.access & BUSTER_X86_METADATA_ACCESS_SUPPRESSED))
            memory_source_count += 1;
        else return false;
    }
    return visible_count == 2 && register_destination_count == 1 && memory_source_count == 1;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_block_memory_source_topology_internal(BusterX86MetadataForm form,
                                                                                     BusterX86MetadataPhysicalQuery query,
                                                                                     u16* scalar_width)
{
    if (scalar_width) *scalar_width = 0;
    if (form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED ||
        (form.encoder_family != BUSTER_X86_METADATA_ENCODER_LEGACY && form.encoder_family != BUSTER_X86_METADATA_ENCODER_EVEX) ||
        (form.field_flags & (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_MEMORY |
                             BUSTER_X86_METADATA_FIELD_REGISTER)) !=
            (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_MEMORY | BUSTER_X86_METADATA_FIELD_REGISTER) ||
        (form.field_flags & ~(BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_MEMORY |
                              BUSTER_X86_METADATA_FIELD_REGISTER | BUSTER_X86_METADATA_FIELD_DISPLACEMENT)) ||
        form.decorator_flags || form.amx_flags || !query.operands || query.operand_count != 2 ||
        query.address_size != 64 || query.execution_mode != BUSTER_X86_METADATA_EXECUTION_MODE_64 ||
        query.attributes.lock || query.attributes.rep || query.attributes.repne || query.attributes.notrack ||
        query.attributes.decorator_flags || query.attributes.apx_flags || query.attributes.amx_flags || query.attributes.has_dfv ||
        query.attributes.no_flags || query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE ||
        query.attributes.branch_hint != BUSTER_X86_METADATA_BRANCH_HINT_NONE)
        return false;

    u32 physical_register_count = 0;
    u32 physical_memory_count = 0;
    for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && operand.reg.width == 64)
            physical_register_count += 1;
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && !operand.memory.vsib &&
                 !operand.memory.has_segment && (!operand.memory.address_size || operand.memory.address_size == 64))
            physical_memory_count += 1;
        else
            return false;
    }
    if (physical_register_count != 1 || physical_memory_count != 1) return false;

    u32 visible_count = 0;
    u32 visible_register_count = 0;
    u32 visible_memory_count = 0;
    u32 suppressed_write_memory_count = 0;
    u16 expected_width = 0;
    for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form.id, metadata_index, &metadata)) return false;
        u16 width = buster_x86_metadata_single_scalar_width(metadata.physical_width_flags);
        if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY && width)
        {
            if (expected_width && expected_width != width) return false;
            expected_width = width;
        }
        if (!metadata.visible)
        {
            if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY && metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY &&
                metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_RM &&
                (metadata.access & BUSTER_X86_METADATA_ACCESS_WRITE) &&
                (metadata.access & BUSTER_X86_METADATA_ACCESS_SUPPRESSED))
                suppressed_write_memory_count += 1;
            continue;
        }
        visible_count += 1;
        if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
            metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
            metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_REG &&
            (metadata.access & BUSTER_X86_METADATA_ACCESS_READ) &&
            !(metadata.access & BUSTER_X86_METADATA_ACCESS_WRITE) &&
            !(metadata.access & BUSTER_X86_METADATA_ACCESS_SUPPRESSED))
            visible_register_count += 1;
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY &&
                 metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY &&
                 metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_RM &&
                 (metadata.access & BUSTER_X86_METADATA_ACCESS_READ) &&
                 !(metadata.access & BUSTER_X86_METADATA_ACCESS_WRITE) &&
                 !(metadata.access & BUSTER_X86_METADATA_ACCESS_SUPPRESSED))
            visible_memory_count += 1;
        else
            return false;
    }
    if (visible_count != 2 || visible_register_count != 1 || visible_memory_count != 1 || suppressed_write_memory_count != 1 ||
        !expected_width)
        return false;
    if (scalar_width) *scalar_width = expected_width;
    return true;
}

bool buster_x86_metadata_block_memory_source_topology(BusterX86MetadataForm form,
                                                       BusterX86MetadataPhysicalQuery query)
{
    return buster_x86_metadata_block_memory_source_topology_internal(form, query, 0);
}

bool buster_x86_metadata_block_memory_source_authoritative(BusterX86MetadataForm form,
                                                            BusterX86MetadataPhysicalQuery query)
{
    u16 scalar_width = 0;
    if (!buster_x86_metadata_block_memory_source_topology_internal(form, query, &scalar_width)) return false;
    for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
        if (operand.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY) continue;
        // The source parser leaves an unsized block-transfer memory width at
        // zero.  The generated form records its scalar element as zd:u32,
        // while the public aggregate qualifier is the architectural 512-bit
        // block.  Both source spellings are therefore valid projections;
        // every other explicit width remains an operand mismatch.
        if (operand.memory.source_width && operand.memory.source_width != 512) return false;
        if (operand.width && operand.width != scalar_width) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_aggregate_memory_source_topology_internal(BusterX86MetadataForm form,
                                                                                         BusterX86MetadataPhysicalQuery query,
                                                                                         u16* scalar_width)
{
    if (scalar_width) *scalar_width = 0;
    if ((form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
         form.coverage_class != BUSTER_X86_METADATA_COVERAGE_PRIVILEGED) ||
        (form.encoder_family != BUSTER_X86_METADATA_ENCODER_LEGACY &&
         form.encoder_family != BUSTER_X86_METADATA_ENCODER_EVEX &&
         form.encoder_family != BUSTER_X86_METADATA_ENCODER_SYSTEM) ||
        (form.field_flags & (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_MEMORY |
                             BUSTER_X86_METADATA_FIELD_REGISTER)) !=
            (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_MEMORY |
             BUSTER_X86_METADATA_FIELD_REGISTER) ||
        (form.field_flags & ~(BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_MEMORY |
                              BUSTER_X86_METADATA_FIELD_REGISTER | BUSTER_X86_METADATA_FIELD_DISPLACEMENT)) ||
        form.decorator_flags || form.amx_flags || !query.operands ||
        query.operand_count != 2 || query.address_size != 64 ||
        query.execution_mode != BUSTER_X86_METADATA_EXECUTION_MODE_64 ||
        query.attributes.lock || query.attributes.rep || query.attributes.repne || query.attributes.notrack ||
        query.attributes.decorator_flags || query.attributes.apx_flags || query.attributes.amx_flags ||
        query.attributes.has_dfv || query.attributes.no_flags ||
        query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE ||
        query.attributes.branch_hint != BUSTER_X86_METADATA_BRANCH_HINT_NONE)
        return false;

    u32 physical_register_count = 0;
    u32 physical_memory_count = 0;
    for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && operand.reg.width == 64)
            physical_register_count += 1;
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && !operand.memory.vsib &&
                 !operand.memory.has_segment && (!operand.memory.address_size || operand.memory.address_size == 64))
            physical_memory_count += 1;
        else
            return false;
    }
    if (physical_register_count != 1 || physical_memory_count != 1) return false;

    u32 visible_count = 0;
    u32 visible_register_count = 0;
    u32 visible_memory_count = 0;
    u16 expected_width = 0;
    for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form.id, metadata_index, &metadata) || !metadata.visible) return false;
        visible_count += 1;
        if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
            metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
            metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_REG &&
            (metadata.access & BUSTER_X86_METADATA_ACCESS_READ) &&
            !(metadata.access & (BUSTER_X86_METADATA_ACCESS_WRITE | BUSTER_X86_METADATA_ACCESS_SUPPRESSED)))
        {
            if (metadata.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN &&
                metadata.physical_width_flags != BUSTER_X86_METADATA_PHYSICAL_WIDTH_64)
                return false;
            visible_register_count += 1;
        }
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY &&
                 metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY &&
                 metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_RM &&
                 (metadata.access & BUSTER_X86_METADATA_ACCESS_READ) &&
                 !(metadata.access & (BUSTER_X86_METADATA_ACCESS_WRITE | BUSTER_X86_METADATA_ACCESS_SUPPRESSED)))
        {
            u16 width = buster_x86_metadata_single_scalar_width(metadata.physical_width_flags);
            if (width != 32) return false;
            if (expected_width && expected_width != width) return false;
            expected_width = width;
            visible_memory_count += 1;
        }
        else
            return false;
    }
    if (visible_count != 2 || visible_register_count != 1 || visible_memory_count != 1 || !expected_width) return false;
    if (scalar_width) *scalar_width = expected_width;
    return true;
}

bool buster_x86_metadata_aggregate_memory_source_topology(BusterX86MetadataForm form,
                                                            BusterX86MetadataPhysicalQuery query)
{
    return buster_x86_metadata_aggregate_memory_source_topology_internal(form, query, 0);
}

bool buster_x86_metadata_aggregate_memory_source_authoritative(BusterX86MetadataForm form,
                                                                 BusterX86MetadataPhysicalQuery query)
{
    u16 scalar_width = 0;
    if (!buster_x86_metadata_aggregate_memory_source_topology_internal(form, query, &scalar_width)) return false;
    for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
        if (operand.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY) continue;
        if (operand.memory.source_width && operand.memory.source_width != 512) return false;
        if (operand.width && operand.width != scalar_width) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_query_has_mmx_memory(BusterX86MetadataPhysicalQuery query)
{
    bool has_mmx = false;
    bool has_memory = false;
    for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
            has_memory = true;
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                 operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX)
            has_mmx = true;
    }
    return has_mmx && has_memory;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_is_mmx_movq_transfer_alias(BusterX86MetadataForm form,
                                                                          BusterX86MetadataPhysicalQuery query)
{
    // XED publishes legacy MMX MOVQ memory aliases for 0F6E/0F7E with a
    // REX.W token.  Those opcodes are the GPR/XMM transfer spellings; MMX
    // memory load/store are canonically 0F6F/0F7F and never carry REX.W.
    if (!buster_x86_metadata_query_has_mmx_memory(query) ||
        !buster_x86_metadata_string_input_equal(form.iclass.offset, S8("MOVQ")))
        return false;
    BusterX86MetadataPatternSemantics pattern = {0};
    if (!buster_x86_metadata_emit_parse_pattern(form, &pattern) || pattern.opcode_count != 2 || !pattern.has_w || !pattern.w)
        return false;
    return pattern.opcode[0] == 0x0f && (pattern.opcode[1] == 0x6e || pattern.opcode[1] == 0x7e);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_is_undocumented_shift_alias(BusterX86MetadataForm form,
                                                                           BusterX86MetadataPatternSemantics const* pattern_view)
{
    BusterX86MetadataPatternSemantics pattern = *pattern_view;
    // A few imported memory rows omit XED's UNDOCUMENTED attribute even
    // though they duplicate the reserved group-2 /6 encoding.  Keep the
    // architectural shape rule with the general undocumented-row filter so
    // source selection cannot expose SHL /6 (D0/D1 ONE or D2/D3 CL).
    return buster_x86_metadata_string_input_equal(form.iclass.offset, S8("SHL")) && pattern.reg_fixed == 6 &&
           (pattern.has_implicit_one || (pattern.opcode_count && (pattern.opcode[0] == 0xd2 || pattern.opcode[0] == 0xd3)));
}

// XED exposes XCHG's accumulator opcode+rd spellings with only the
// non-accumulator register visible; the accumulator is the hidden OrAX
// operand.  The checked physical interface, however, normally receives both
// source operands.  Project a two-register pair onto that one visible
// operand whenever exactly one side is the accumulator so selection and
// emission can use the architectural shortest form (90+rd).  Byte XCHG has
// no opcode+rd spelling and intentionally remains on the generic ModRM form.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_xchg_accumulator_projection(
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataPhysicalOperand* projected_operand,
    BusterX86MetadataPhysicalQuery* projected_query)
{
    if (!projected_operand || !projected_query || query.operand_count != 2 || query.include_implicit) return false;
    if (query.attributes.lock || query.attributes.rep || query.attributes.repne || query.attributes.has_mask_register ||
        query.attributes.decorator_flags || query.attributes.apx_flags || query.attributes.amx_flags || query.attributes.has_dfv ||
        query.attributes.implicit_segment != BUSTER_X86_METADATA_SEGMENT_NONE || query.attributes.branch_hint != BUSTER_X86_METADATA_BRANCH_HINT_NONE ||
        query.attributes.notrack || query.attributes.sae || query.attributes.rounding_mode != BUSTER_X86_METADATA_ROUNDING_NONE)
        return false;
    if (!buster_x86_metadata_input_string_equal(query.mnemonic, S8("XCHG"))) return false;
    BusterX86MetadataPhysicalOperand first = query.operands[0];
    BusterX86MetadataPhysicalOperand second = query.operands[1];
    if (first.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
        second.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
        first.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR ||
        second.reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR ||
        first.reg.high_byte || second.reg.high_byte)
        return false;
    u16 first_width = first.width ? first.width : first.reg.width;
    u16 second_width = second.width ? second.width : second.reg.width;
    if (first_width != second_width || (first_width != 16 && first_width != 32 && first_width != 64)) return false;
    bool first_accumulator = first.reg.index == 0;
    bool second_accumulator = second.reg.index == 0;
    if (first_accumulator == second_accumulator) return false;
    *projected_operand = first_accumulator ? second : first;
    *projected_query = query;
    projected_query->operands = projected_operand;
    projected_query->operand_count = 1;
    return true;
}

// Linker PLT padding uses a byte-sized physical memory token for NOP even
// though the architectural 0F 1F /0 form is an untyped 32-bit r/m NOP. Keep
// that linker token canonical by projecting only the exact one-memory NOP
// shape; ordinary NOP queries and all other mnemonics retain their widths.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_nop_memory_projection(
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataPhysicalOperand* projected_operand,
    BusterX86MetadataPhysicalQuery* projected_query)
{
    if (!projected_operand || !projected_query || query.operand_count != 1 || !query.operands ||
        !buster_x86_metadata_input_string_equal(query.mnemonic, S8("NOP")) ||
        query.operands[0].kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY || query.operands[0].width != 8 ||
        query.operands[0].memory.source_width != 8)
        return false;
    *projected_operand = query.operands[0];
    projected_operand->width = 32;
    projected_operand->memory.source_width = 32;
    *projected_query = query;
    projected_query->operands = projected_operand;
    return true;
}

// The source parser accepts unsized memory for CMPXCHG8B/16B, while their
// architectural rows carry fixed qword/double-quadword memory widths.  Keep
// that source form checked by projecting only the exact one-memory spelling;
// ordinary sized memory and all other mnemonics retain their requested width.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_cmpxchg_memory_projection(
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataPhysicalOperand* projected_operand,
    BusterX86MetadataPhysicalQuery* projected_query)
{
    if (!projected_operand || !projected_query || query.operand_count != 1 || !query.operands ||
        query.operands[0].kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY || query.operands[0].width)
        return false;
    u16 width = 0;
    if (buster_x86_metadata_input_string_equal(query.mnemonic, S8("CMPXCHG8B")))
        width = 64;
    else if (buster_x86_metadata_input_string_equal(query.mnemonic, S8("CMPXCHG16B")))
        width = 128;
    else
        return false;
    *projected_operand = query.operands[0];
    projected_operand->width = width;
    projected_operand->memory.source_width = width;
    *projected_query = query;
    projected_query->operands = projected_operand;
    return true;
}

// APX NDD rows carry one additional source operand (or the second explicit
// register of PUSH2/POP2), while the parser's physical query intentionally
// leaves APX_NDD unset unless an explicit {ndd} decorator was written.  Infer
// the form-controlled flag only when a metadata candidate proves both the NDD
// pattern and the exact visible operand count.  This keeps ordinary two-op
// legacy/APX forms unchanged and does not infer an APX capability without a
// matching generated NDD row.
BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_apx_ndd_projection(BusterX86MetadataPhysicalQuery query,
                                                                  BusterX86MetadataPhysicalQuery* projected_query)
{
    if (!projected_query || (query.attributes.apx_flags & BUSTER_X86_METADATA_APX_NDD) || !query.operand_count)
        return false;
    BusterX86MetadataCandidateRange candidates = buster_x86_metadata_lookup_mnemonic(query.mnemonic);
    // An ordinary legacy form can expose the same physical arity as an APX
    // NDD row when XED hides a fixed source (for example SHL's implicit CL or
    // ONE).  In that topology the source query is unambiguously the legacy
    // spelling; do not project it into an APX NDD request merely because an
    // APX sibling happens to have the same count.
    for (u32 position = 0; position < candidates.count; position += 1)
    {
        u32 form_id = 0;
        if (!buster_x86_metadata_candidate_at(candidates, position, &form_id)) continue;
        BusterX86MetadataForm form = {0};
        if (!buster_x86_metadata_form(form_id, &form) || (form.apx_flags & BUSTER_X86_METADATA_APX_NDD)) continue;
        u32 expected_operand_count = 0;
        if (buster_x86_metadata_emit_form_operand_count(query, form, &expected_operand_count) &&
            expected_operand_count == query.operand_count)
            return false;
    }
    for (u32 position = 0; position < candidates.count; position += 1)
    {
        u32 form_id = 0;
        if (!buster_x86_metadata_candidate_at(candidates, position, &form_id)) continue;
        BusterX86MetadataForm form = {0};
        if (!buster_x86_metadata_form(form_id, &form) || !(form.apx_flags & BUSTER_X86_METADATA_APX_NDD)) continue;
        BusterX86MetadataPatternSemantics pattern = {0};
        if (!buster_x86_metadata_emit_parse_pattern(form, &pattern) || !pattern.has_nd || !pattern.nd_value) continue;
        u32 expected_operand_count = 0;
        if (!buster_x86_metadata_emit_form_operand_count(query, form, &expected_operand_count) ||
            expected_operand_count != query.operand_count)
            continue;
        *projected_query = query;
        projected_query->attributes.apx_flags |= BUSTER_X86_METADATA_APX_NDD;
        return true;
    }
    return false;
}

BusterX86MetadataSelectResult buster_x86_metadata_select_form(BusterX86MetadataPhysicalQuery query)
{
    BusterX86MetadataSelectResult result = {
        .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
        .form_id = UINT32_MAX,
        .failure_form_id = UINT32_MAX,
        .selected_memory_operand = UINT8_MAX,
    };
    if (!buster_x86_metadata_emit_physical_query_valid(query)) return result;
    BusterX86MetadataPhysicalOperand nop_projected_operand = {0};
    BusterX86MetadataPhysicalQuery nop_projected_query = {0};
    if (buster_x86_metadata_nop_memory_projection(query, &nop_projected_operand, &nop_projected_query))
        return buster_x86_metadata_select_form(nop_projected_query);
    BusterX86MetadataPhysicalOperand cmpxchg_projected_operand = {0};
    BusterX86MetadataPhysicalQuery cmpxchg_projected_query = {0};
    if (buster_x86_metadata_cmpxchg_memory_projection(query, &cmpxchg_projected_operand, &cmpxchg_projected_query))
        return buster_x86_metadata_select_form(cmpxchg_projected_query);
    BusterX86MetadataPhysicalQuery apx_projected_query = {0};
    if (buster_x86_metadata_apx_ndd_projection(query, &apx_projected_query))
    {
        BusterX86MetadataSelectResult projected = buster_x86_metadata_select_form(apx_projected_query);
        if (projected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS ||
            projected.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE)
            return projected;
    }
    BusterX86MetadataPhysicalOperand projected_operand = {0};
    BusterX86MetadataPhysicalQuery projected_query = {0};
    if (buster_x86_metadata_xchg_accumulator_projection(query, &projected_operand, &projected_query))
    {
        BusterX86MetadataSelectResult projected = buster_x86_metadata_select_form(projected_query);
        if (projected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS) return projected;
    }
    result.status = BUSTER_X86_METADATA_ENCODE_UNKNOWN_MNEMONIC;
    BusterX86MetadataCandidateRange candidates = buster_x86_metadata_lookup_mnemonic(query.mnemonic);
    if (!candidates.count) return result;
    // A source-qualified scalar width must not fall through to an unrelated
    // candidate when metadata proves a block-transfer topology for the
    // same mnemonic.  The topology owns the public aggregate width (512) and
    // the unsized spelling; an explicit scalar or other aggregate qualifier
    // is a generic operand mismatch, not permission to select a shadow form.
    if (query.source_semantics && query.operands)
    {
        bool invalid_block_memory_width = false;
        for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
        {
            BusterX86MetadataPhysicalOperand operand = query.operands[operand_index];
            if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
                operand.memory.source_width && operand.memory.source_width != 512)
            {
                for (u32 position = 0; position < candidates.count; position += 1)
                {
                    u32 topology_form_id = 0;
                    BusterX86MetadataForm topology_form = {0};
                    if (buster_x86_metadata_candidate_at(candidates, position, &topology_form_id) &&
                        buster_x86_metadata_form(topology_form_id, &topology_form) &&
                        buster_x86_metadata_block_memory_source_topology_internal(topology_form, query, 0))
                    {
                        invalid_block_memory_width = true;
                        break;
                    }
                }
                break;
            }
        }
        if (invalid_block_memory_width)
        {
            result.status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
            return result;
        }
    }
    bool saw_matching_count = false;
    bool saw_shape = false;
    bool saw_allowed = false;
    BusterX86MetadataEncodeStatus first_failure = BUSTER_X86_METADATA_ENCODE_AMBIGUOUS;
    bool failure_recorded = false;
    bool shape_diagnostic_recorded = false;
    u32 first_failure_operand = 0;
    s64 first_failure_value = 0;
    u32 first_shape_operand = 0;
    s64 first_shape_value = 0;
    BusterX86MetadataString first_required_feature = {0};
    BusterX86MetadataForm first_failure_form = {0};
    bool first_failure_form_recorded = false;
    bool selected_implicit_one = false;
    bool selected_x87_no_rexw = false;
    // The three memory-topology probes below run once per candidate, but each
    // one first rejects on conditions that depend only on the query, which is
    // loop-invariant.  Hoist those here so a query that can satisfy neither
    // shape skips all three calls outright instead of re-deriving the same
    // answer for every candidate considered.  These are the callees' own
    // query-side tests, so this is a necessary condition and can only skip a
    // call that would have returned false.  The two families are mutually
    // exclusive by construction: the topology pair requires no decorator at
    // all, the typed probe requires the broadcast decorator.
    bool topology_query_possible =
        query.operands && query.operand_count == 2 && query.address_size == 64 &&
        query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64 && !query.attributes.lock &&
        !query.attributes.rep && !query.attributes.repne && !query.attributes.notrack &&
        !query.attributes.decorator_flags && !query.attributes.apx_flags && !query.attributes.amx_flags &&
        !query.attributes.has_dfv && !query.attributes.no_flags &&
        query.attributes.implicit_segment == BUSTER_X86_METADATA_SEGMENT_NONE &&
        query.attributes.branch_hint == BUSTER_X86_METADATA_BRANCH_HINT_NONE;
    bool typed_query_possible = query.address_size == 64 && query.operand_count <= 16 &&
                                (query.attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST) != 0 &&
                                !query.attributes.sae &&
                                query.attributes.rounding_mode == BUSTER_X86_METADATA_ROUNDING_NONE;
    for (u32 position = 0; position < candidates.count; position += 1)
    {
        u32 form_id = 0;
        if (!buster_x86_metadata_candidate_at(candidates, position, &form_id)) continue;
        BusterX86MetadataForm form = {0};
        if (!buster_x86_metadata_form(form_id, &form)) continue;
        if (buster_x86_metadata_is_mmx_movq_transfer_alias(form, query)) continue;
        // XED retains undocumented duplicate encodings (for example SHL's
        // ModRM /6 alias beside the architectural /4 form).  The checked
        // selector is the canonical source of instruction bytes, so it must
        // never expose those rows; explicit exact-form identities remain
        // available for provenance and machine plans.
        // Borrowed, not copied out: this runs once per candidate considered and
        // the record is 176 bytes, of which four fields are read here.  The
        // storage below is only reached when the cache cannot serve the form.
        BusterX86MetadataPatternSemantics filter_storage = {0};
        BusterX86MetadataPatternSemantics const* filter_view = 0;
        bool filter_parsed = false;
        filter_view = buster_x86_metadata_pattern_semantics_borrow(form, &filter_parsed);
        if (!filter_view)
        {
            filter_parsed = buster_x86_metadata_emit_parse_pattern(form, &filter_storage);
            filter_view = &filter_storage;
        }
        if (!filter_parsed) continue;
        // The public one-memory NOP spelling is the architectural 0F 1F /0
        // padding form.  XED also indexes decode-only 0F 18/19 aliases under
        // the NOP mnemonic; keep those aliases out of source/link selection.
        if (buster_x86_metadata_input_string_equal(query.mnemonic, S8("NOP")) && query.operand_count == 1 &&
            query.operands && query.operands[0].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
            filter_view->opcode_count >= 2 && filter_view->opcode[0] == 0x0f && filter_view->opcode[1] != 0x1f)
            continue;
        bool x87_free_pop = buster_x86_metadata_string_input_equal(form.extension.offset, S8("X87")) &&
                            buster_x86_metadata_string_input_equal(form.iclass.offset, S8("FFREEP"));
        if ((!x87_free_pop && buster_x86_metadata_emit_string_has(form.attributes, S8("UNDOCUMENTED"))) ||
            buster_x86_metadata_is_undocumented_shift_alias(form, filter_view))
            continue;
        BusterX86GeneratedForm generated = buster_x86_metadata_form_record(form_id);
        BusterX86MetadataResolveQuery filter_query = {
            .include_privileged = query.include_privileged,
            .include_not64 = query.include_not64,
            .execution_mode = query.execution_mode,
        };
        if (!buster_x86_metadata_form_coverage_allowed(generated, filter_query) ||
            !buster_x86_metadata_form_execution_mode_matches(generated, filter_query))
        {
            continue;
        }
        saw_allowed = true;
        // The unprefixed LOOP-family spelling has a documented MODEP5=0
        // form and a MODEP5=1 REP=0 alias with identical bytes.  Keep source
        // selection on the ordinary no-control row when no REP modifier was
        // requested; MODEP5=1 remains available for the explicit REP/REPNE
        // spellings below and stays visible to the coverage ledger.
        bool loop_form = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOP")) ||
                         buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPE")) ||
                         buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPNE"));
        if (loop_form && !query.attributes.rep && !query.attributes.repne)
        {
            BusterX86MetadataPatternSemantics pattern = {0};
            if (buster_x86_metadata_emit_parse_pattern(form, &pattern) && pattern.has_modep5 && pattern.modep5_value != 0)
                continue;
        }
        // Deliberately uninitialized: both writers below memcpy the query's
        // whole operand prefix before touching it, and `candidate_query`
        // adopts the array only when one of them ran.  Zero-initializing all
        // sixteen entries is 2,304 bytes of dead stores per candidate.
        BusterX86MetadataPhysicalOperand candidate_operands[16];
        BusterX86MetadataPhysicalQuery candidate_query = query;
        u16 block_memory_width = 0;
        bool inferred_memory_width = false;
        bool aggregate_memory_topology =
            topology_query_possible &&
            buster_x86_metadata_aggregate_memory_source_topology_internal(form, query, &block_memory_width);
        bool block_memory_topology =
            topology_query_possible && !aggregate_memory_topology &&
            buster_x86_metadata_block_memory_source_topology_internal(form, query, &block_memory_width);
        if (aggregate_memory_topology || block_memory_topology)
        {
            memcpy(candidate_operands, query.operands, query.operand_count * sizeof(*candidate_operands));
            for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
            {
                if (candidate_operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
                {
                    candidate_operands[operand_index].width = block_memory_width;
                    candidate_operands[operand_index].memory.source_width = 0;
                    break;
                }
            }
            inferred_memory_width = true;
        }
        else
        {
            inferred_memory_width = typed_query_possible &&
                                    buster_x86_metadata_prepare_typed_memory_query(form, query, candidate_operands);
        }
        if (inferred_memory_width)
            candidate_query.operands = candidate_operands;
        u32 expected_operand_count = 0;
        if (!buster_x86_metadata_emit_form_operand_count(candidate_query, form, &expected_operand_count))
        {
            if (!failure_recorded)
            {
                first_failure = BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
                failure_recorded = true;
                first_failure_form = form;
                first_failure_form_recorded = true;
                result.failure_form_id = form_id;
            }
            continue;
        }
        if (expected_operand_count != query.operand_count) continue;
        saw_matching_count = true;
        BusterX86MetadataPhysicalBinding shape_bindings[16] = {0};
        u32 shape_binding_count = 0;
        u32 shape_diagnostic_operand = 0;
        if (!buster_x86_metadata_emit_bind_form(candidate_query, &form, shape_bindings, &shape_binding_count, &shape_diagnostic_operand, 0))
        {
            if (!shape_diagnostic_recorded)
            {
                shape_diagnostic_recorded = true;
                first_shape_operand = shape_diagnostic_operand;
                first_shape_value = 0;
            }
            continue;
        }
        saw_shape = true;
        BusterX86MetadataEncodeScratch scratch = {0};
        u32 diagnostic_operand = 0;
        s64 diagnostic_value = 0;
        BusterX86MetadataEncodeStatus status = buster_x86_metadata_emit_form_to_scratch(candidate_query, form, &scratch, &diagnostic_operand,
                                                                                           &diagnostic_value, 0, 0, false, false);
        if (status != BUSTER_X86_METADATA_ENCODE_SUCCESS)
        {
            BusterX86MetadataString required_feature = {0};
            if (status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE)
            {
                required_feature = buster_x86_metadata_emit_required_feature(candidate_query, form);
            }
            bool typed_candidate = buster_x86_metadata_typed_decorator_authoritative(form, candidate_query);
            bool previous_failure_typed = first_failure_form_recorded &&
                                          buster_x86_metadata_typed_decorator_authoritative(first_failure_form, query);
            bool prefer_failure = !failure_recorded || first_failure == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH ||
                                  (status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE && required_feature.length &&
                                   typed_candidate && !previous_failure_typed);
            if (status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                first_failure == BUSTER_X86_METADATA_ENCODE_ADDRESSING && required_feature.length &&
                first_failure_form_recorded && buster_x86_metadata_eamode_alias_forms(first_failure_form, form))
            {
                // Only otherwise-equivalent EAMODE aliases may let a feature
                // diagnostic supersede an address-size mismatch.  An
                // unrelated feature-disabled form must not hide a structural
                // addressing error.
                prefer_failure = true;
            }
            if (prefer_failure)
            {
                first_failure = status;
                first_failure_operand = diagnostic_operand;
                first_failure_value = diagnostic_value;
                first_required_feature = required_feature;
                failure_recorded = true;
                first_failure_form = form;
                first_failure_form_recorded = true;
                result.failure_form_id = form_id;
            }
            continue;
        }
        result.candidate_count += 1;
        BusterX86MetadataPatternSemantics candidate_storage = {0};
        bool candidate_parsed = false;
        BusterX86MetadataPatternSemantics const* candidate_view =
            buster_x86_metadata_pattern_semantics_borrow(form, &candidate_parsed);
        if (!candidate_view)
        {
            candidate_parsed = buster_x86_metadata_emit_parse_pattern(form, &candidate_storage);
            candidate_view = &candidate_storage;
        }
        bool candidate_implicit_one = candidate_parsed && candidate_view->has_implicit_one;
        bool prefer_implicit_one = scratch.byte_count == result.selected_byte_count && candidate_implicit_one && !selected_implicit_one;
        bool candidate_x87_no_rexw = buster_x86_metadata_string_input_equal(form.extension.offset, S8("X87")) &&
                                     candidate_view->has_modrm && !candidate_view->w;
        bool prefer_x87_no_rexw = scratch.byte_count == result.selected_byte_count && candidate_x87_no_rexw &&
                                 !selected_x87_no_rexw;
        // FMA4's generated XED rows intentionally duplicate each source
        // topology with both VEX.W values.  The architectural/default
        // spelling is W=1 (LLVM and the legacy assembler table agree), while
        // W=0 is only the alternate topology used when the memory operand is
        // in slot two.  When both register forms tie on size, prefer W=1;
        // operand binding still selects W=0 for the slot-two memory form.
        bool candidate_fma4_w1 = buster_x86_metadata_string_input_equal(form.extension.offset, S8("FMA4")) &&
                                 candidate_view->has_w && candidate_view->w;
        BusterX86MetadataForm selected_form = {0};
        BusterX86MetadataPatternSemantics selected_storage = {0};
        BusterX86MetadataPatternSemantics const* selected_view = 0;
        bool selected_parsed = false;
        bool selected_fma4_w1 = false;
        if (result.form_id != UINT32_MAX && buster_x86_metadata_form(result.form_id, &selected_form))
        {
            selected_view = buster_x86_metadata_pattern_semantics_borrow(selected_form, &selected_parsed);
            if (!selected_view)
            {
                selected_parsed = buster_x86_metadata_emit_parse_pattern(selected_form, &selected_storage);
                selected_view = &selected_storage;
            }
            selected_fma4_w1 = selected_parsed &&
                               buster_x86_metadata_string_input_equal(selected_form.extension.offset, S8("FMA4")) &&
                               selected_view->has_w && selected_view->w;
        }
        bool prefer_fma4_w1 = scratch.byte_count == result.selected_byte_count && candidate_fma4_w1 && !selected_fma4_w1;
        bool aggregate_block_query = false;
        for (u32 aggregate_operand_index = 0; aggregate_operand_index < query.operand_count; aggregate_operand_index += 1)
        {
            if (query.operands[aggregate_operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
                query.operands[aggregate_operand_index].memory.source_width == 512)
            {
                aggregate_block_query = true;
                break;
            }
        }
        bool candidate_block_apx = aggregate_block_query &&
                                   (buster_x86_metadata_block_memory_source_topology_internal(form, query, 0) ||
                                    buster_x86_metadata_aggregate_memory_source_topology_internal(form, query, 0)) &&
                                   (form.apx_flags & BUSTER_X86_METADATA_APX) != 0;
        bool selected_block_apx = false;
        if (aggregate_block_query && result.form_id != UINT32_MAX)
        {
            BusterX86MetadataForm current_form = {0};
            selected_block_apx = buster_x86_metadata_form(result.form_id, &current_form) &&
                                 (buster_x86_metadata_block_memory_source_topology_internal(current_form, query, 0) ||
                                  buster_x86_metadata_aggregate_memory_source_topology_internal(current_form, query, 0)) &&
                                 (current_form.apx_flags & BUSTER_X86_METADATA_APX) != 0;
        }
        bool prefer_block_apx = candidate_block_apx && !selected_block_apx;
        bool retain_block_apx = aggregate_block_query && selected_block_apx && !candidate_block_apx;
        if (result.form_id == UINT32_MAX || prefer_block_apx ||
            (!retain_block_apx && (scratch.byte_count < result.selected_byte_count || prefer_implicit_one ||
            prefer_x87_no_rexw || prefer_fma4_w1 ||
            (scratch.byte_count == result.selected_byte_count && candidate_implicit_one == selected_implicit_one &&
             candidate_x87_no_rexw == selected_x87_no_rexw && !prefer_fma4_w1 && form_id < result.form_id))))
        {
            result.form_id = form_id;
            result.stable_hash = form.stable_hash;
            result.selected_byte_count = scratch.byte_count;
            selected_implicit_one = candidate_implicit_one;
            selected_x87_no_rexw = candidate_x87_no_rexw;
            result.selected_memory_width = 0;
            result.selected_memory_operand = UINT8_MAX;
            if (inferred_memory_width)
            {
                for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
                {
                    if (candidate_operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
                    {
                        result.selected_memory_width = candidate_operands[operand_index].width;
                        result.selected_memory_operand = (u8)operand_index;
                        break;
                    }
                }
            }
        }
    }
    if (result.candidate_count) result.status = BUSTER_X86_METADATA_ENCODE_SUCCESS;
    else if (!saw_allowed) result.status = BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
    else if (!saw_matching_count) result.status = BUSTER_X86_METADATA_ENCODE_WRONG_OPERAND_COUNT;
    else if (!saw_shape)
    {
        result.status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        result.diagnostic_operand = shape_diagnostic_recorded ? first_shape_operand : 0;
        result.diagnostic_value = shape_diagnostic_recorded ? first_shape_value : 0;
    }
    else
    {
        result.status = first_failure;
        result.diagnostic_operand = first_failure_operand;
        result.diagnostic_value = first_failure_value;
    }
    if (result.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE) result.required_feature = first_required_feature;
    return result;
}

BusterX86MetadataEmitResult buster_x86_metadata_encode(BusterX86MetadataEncodeQuery query)
{
    BusterX86MetadataEmitResult result = {
        .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
        .form_id = UINT32_MAX,
    };
    if ((query.output_capacity && !query.output) || (query.relocation_capacity && !query.relocations) ||
        query.physical.operand_count > 16)
        return result;
    BusterX86MetadataPhysicalOperand nop_projected_operand = {0};
    BusterX86MetadataPhysicalQuery physical_query = query.physical;
    buster_x86_metadata_nop_memory_projection(query.physical, &nop_projected_operand, &physical_query);
    BusterX86MetadataPhysicalOperand cmpxchg_projected_operand = {0};
    buster_x86_metadata_cmpxchg_memory_projection(query.physical, &cmpxchg_projected_operand, &physical_query);
    BusterX86MetadataSelectResult selection = buster_x86_metadata_select_form(physical_query);
    result.status = selection.status;
    result.form_id = selection.form_id;
    result.stable_hash = selection.stable_hash;
    result.diagnostic_operand = selection.diagnostic_operand;
    result.diagnostic_value = selection.diagnostic_value;
    result.required_feature = selection.required_feature;
    if (selection.status != BUSTER_X86_METADATA_ENCODE_SUCCESS) return result;

    // Selection may infer a fixed memory element width from the form schema
    // when the caller supplied an unsized address.  Reuse that exact physical
    // query for emission rather than asking the form emitter to rediscover a
    // width and risking a different candidate or an operand mismatch.
    BusterX86MetadataPhysicalQuery physical = physical_query;
    BusterX86MetadataPhysicalOperand operands[16] = {0};
    if (selection.selected_memory_width && selection.selected_memory_operand < physical.operand_count)
    {
        memcpy(operands, physical.operands, physical.operand_count * sizeof(*operands));
        operands[selection.selected_memory_operand].width = selection.selected_memory_width;
        BusterX86MetadataForm selected_form = {0};
        u16 selected_scalar_width = 0;
        if (buster_x86_metadata_form(selection.form_id, &selected_form) &&
            (buster_x86_metadata_block_memory_source_topology_internal(selected_form, physical, &selected_scalar_width) ||
             buster_x86_metadata_aggregate_memory_source_topology_internal(selected_form, physical, &selected_scalar_width)) &&
            operands[selection.selected_memory_operand].memory.source_width == 512)
            operands[selection.selected_memory_operand].memory.source_width = 0;
        physical.operands = operands;
    }
    BusterX86MetadataPhysicalQuery apx_projected_query = {0};
    if (buster_x86_metadata_apx_ndd_projection(physical, &apx_projected_query)) physical = apx_projected_query;
    BusterX86MetadataPhysicalOperand projected_operand = {0};
    BusterX86MetadataPhysicalQuery projected_query = {0};
    if (selection.form_id != UINT32_MAX &&
        buster_x86_metadata_xchg_accumulator_projection(physical, &projected_operand, &projected_query))
        physical = projected_query;
    BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = physical,
        .form_id = selection.form_id,
        .output = query.output,
        .output_capacity = query.output_capacity,
        .relocations = query.relocations,
        .relocation_capacity = query.relocation_capacity,
    });
    // Selection diagnostics identify the canonical candidate even when the
    // second, structural emission pass rejects a malformed physical query or
    // runs out of output/relocation capacity.  Preserve that identity and
    // diagnostic context across the combined bridge instead of returning a
    // partially reset emit result.
    if (emitted.form_id == UINT32_MAX) emitted.form_id = selection.form_id;
    if (!emitted.stable_hash) emitted.stable_hash = selection.stable_hash;
    if (!emitted.diagnostic_operand && selection.diagnostic_operand) emitted.diagnostic_operand = selection.diagnostic_operand;
    if (!emitted.diagnostic_value && selection.diagnostic_value) emitted.diagnostic_value = selection.diagnostic_value;
    if (!emitted.required_feature.length && selection.required_feature.length) emitted.required_feature = selection.required_feature;
    return emitted;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_emit_form_mnemonic_matches(String8 mnemonic, u32 form_id)
{
    BusterX86MetadataCandidateRange candidates = buster_x86_metadata_lookup_mnemonic(mnemonic);
    for (u32 position = 0; position < candidates.count; position += 1)
    {
        u32 candidate_id = 0;
        if (!buster_x86_metadata_candidate_at(candidates, position, &candidate_id)) return false;
        if (candidate_id == form_id) return true;
    }
    BusterX86MetadataForm form = {0};
    if (buster_x86_metadata_form(form_id, &form) && buster_x86_metadata_string_input_equal(form.iclass.offset, mnemonic)) return true;
    return false;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataString buster_x86_metadata_emit_required_feature(
    BusterX86MetadataPhysicalQuery query, BusterX86MetadataForm form)
{
    BusterX86GeneratedForm generated = buster_x86_metadata_form_record(form.id);
    BusterX86MetadataResolveQuery policy_query = {
        .execution_mode = query.execution_mode,
        .include_privileged = query.include_privileged,
        .include_not64 = query.include_not64,
    };
    // A combined encode status is also used for policy and mode rejection.
    // Do not report an ISA feature for those cases: required_feature is only
    // meaningful when the target-feature check itself failed.
    if (!buster_x86_metadata_form_coverage_allowed(generated, policy_query) ||
        !buster_x86_metadata_form_execution_mode_matches(generated, policy_query))
        return (BusterX86MetadataString){0};
    if (!buster_x86_metadata_form_feature_available(generated, query.features))
        return form.isa_set.length ? form.isa_set : form.extension;
    if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 && !buster_x86_metadata_feature_input_allows_apx(query.features))
        return form.isa_set.length ? form.isa_set : form.extension;
    bool uses_egpr = false;
    for (u32 index = 0; index < query.operand_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = query.operands[index];
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && operand.reg.index >= 16)
            uses_egpr = true;
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            uses_egpr |= operand.memory.has_base && operand.memory.base.index >= 16;
            uses_egpr |= operand.memory.has_index && !operand.memory.vsib && operand.memory.index.index >= 16;
        }
    }
    if (uses_egpr && !buster_x86_metadata_feature_input_allows_apx(query.features))
    {
        // A legacy/REX row may be promoted to REX2 by the typed emitter, so
        // the missing feature is APX even though the packed row itself still
        // carries the baseline ISA spelling.  EVEX rows likewise use APX for
        // EGPR address/data extension; ordinary ISA requirements were
        // checked above and are not the cause of this rejection.
        if (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY || form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX ||
            form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 || form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX)
            return buster_x86_metadata_apx_feature_string();
    }
    return (BusterX86MetadataString){0};
}

// The form is borrowed, not copied.  It is a ~256 byte row and this is on the
// path of every emitted instruction; only buster_x86_metadata_emit_form_to_scratch
// needs a mutable copy, because it rewrites the prefix kind and encoder family
// and then hands the rewritten row to its own callees.
BUSTER_GLOBAL_LOCAL BusterX86MetadataEmitResult buster_x86_metadata_emit_form_with_form(BusterX86MetadataEmitQuery const* query_pointer,
                                                                                         BusterX86MetadataForm const* form_pointer,
                                                                                         bool check_mnemonic,
                                                                                         BusterX86MetadataExactPlanRecord const* plan,
                                                                                         BusterX86MetadataMachineExactToken const* machine_token,
                                                                                         bool force_disp32, bool policy_prevalidated)
{
#define query (*query_pointer)
#define form (*form_pointer)
    BusterX86MetadataEmitResult result = {
        .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
        .form_id = form.id,
    };
    if (check_mnemonic && !buster_x86_metadata_emit_form_mnemonic_matches(query.physical.mnemonic, form.id))
    {
        result.status = BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM;
        return result;
    }
    result.stable_hash = form.stable_hash;
    // Decoder aliases are retained in the generated snapshot for provenance
    // and exact-form diagnostics, but their bytes overlap feature-selected
    // public instructions.  Never let either mnemonic or exact-form APIs
    // turn that overlap into a source/direct emission capability.
    if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS)
    {
        result.status = BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
        return result;
    }
    // The emitter writes bytes and relocations before reading either array;
    // only their counters need an initial value for empty/error results.
    BusterX86MetadataEncodeScratch scratch;
    scratch.byte_count = 0;
    scratch.relocation_count = 0;
    scratch.value_field_count = 0;
    result.status = buster_x86_metadata_emit_form_to_scratch(query.physical, form, &scratch, &result.diagnostic_operand,
                                                              &result.diagnostic_value, plan, machine_token, force_disp32,
                                                              policy_prevalidated);
    if (result.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE && !machine_token)
        result.required_feature = buster_x86_metadata_emit_required_feature(query.physical, form);
    result.required_byte_count = scratch.byte_count;
    result.required_relocation_count = scratch.relocation_count;
    result.value_field_count = scratch.value_field_count;
    if (result.status != BUSTER_X86_METADATA_ENCODE_SUCCESS) return result;
    if (query.output_capacity < scratch.byte_count)
    {
        result.status = BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
        return result;
    }
    if (query.relocation_capacity < scratch.relocation_count)
    {
        result.status = BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY;
        return result;
    }
    if (scratch.byte_count) memcpy(query.output, scratch.bytes, scratch.byte_count);
    if (scratch.relocation_count) memcpy(query.relocations, scratch.relocations, scratch.relocation_count * sizeof(*scratch.relocations));
    result.byte_count = scratch.byte_count;
    result.relocation_count = scratch.relocation_count;
    result.status = BUSTER_X86_METADATA_ENCODE_SUCCESS;
    return result;
#undef form
#undef query
}

BusterX86MetadataEmitResult buster_x86_metadata_emit_form(BusterX86MetadataEmitQuery query)
{
    BusterX86MetadataEmitResult result = {
        .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
        .form_id = query.form_id,
    };
    if (!buster_x86_metadata_emit_physical_query_valid(query.physical) ||
        (query.output_capacity && !query.output) || (query.relocation_capacity && !query.relocations))
        return result;
    BusterX86MetadataForm form = {0};
    if (!buster_x86_metadata_form(query.form_id, &form))
    {
        result.status = BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM;
        return result;
    }
    return buster_x86_metadata_emit_form_with_form(&query, &form, true, 0, 0, false, false);
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataEmitResult buster_x86_metadata_emit_form_exact_policy(BusterX86MetadataEmitQuery query,
                                                                                            BusterX86MetadataFormKey key,
                                                                                            bool policy_prevalidated)
{
    BusterX86MetadataEmitResult result = {
        .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
        .form_id = key.form_id,
    };
    // Borrow the prewarmed row where possible.  The identity check the copying
    // lookup performs is reproduced here against the borrowed row, so a stale
    // key is still rejected; only the copy is avoided.
    BusterX86MetadataForm storage = {0};
    BusterX86MetadataForm const* form_pointer = buster_x86_metadata_normalized_form_borrow(key.form_id);
    if (form_pointer)
    {
        if (!key.stable_hash || !buster_x86_metadata_form_record_valid(key.form_id) ||
            form_pointer->stable_hash != key.stable_hash)
            form_pointer = 0;
    }
    if (!form_pointer)
    {
        if (!buster_x86_metadata_lookup_form_key(key, &storage))
        {
            result.status = BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM;
            return result;
        }
        form_pointer = &storage;
    }
    if ((query.output_capacity && !query.output) || (query.relocation_capacity && !query.relocations)) return result;
    // Exact callers deliberately do not provide a source mnemonic.  The
    // form's canonical iclass is supplied only to the shared structural
    // validator and form transform; it is never looked up or compared to the
    // caller's spelling.  Clearing source_semantics suppresses source-level
    // operand diagnostics while retaining physical shape/range checks.
    BusterX86MetadataEmitQuery normalized = query;
    normalized.form_id = key.form_id;
    normalized.physical.mnemonic = buster_x86_metadata_string_span(form_pointer->iclass);
    normalized.physical.source_semantics = false;
    if (!buster_x86_metadata_emit_physical_query_valid(normalized.physical)) return result;
    return buster_x86_metadata_emit_form_with_form(&normalized, form_pointer, false, 0, 0, false, policy_prevalidated);
}

BusterX86MetadataEmitResult buster_x86_metadata_emit_form_exact(BusterX86MetadataEmitQuery query, BusterX86MetadataFormKey key)
{
    return buster_x86_metadata_emit_form_exact_policy(query, key, false);
}

BusterX86MetadataEmitResult buster_x86_metadata_emit_form_selected(BusterX86MetadataEmitQuery query, BusterX86MetadataFormKey key)
{
    return buster_x86_metadata_emit_form_exact_policy(query, key, true);
}

BusterX86MetadataEmitResult buster_x86_metadata_emit_form_key(BusterX86MetadataEmitQuery query,
                                                                BusterX86MetadataFormKey key)
{
    return buster_x86_metadata_emit_form_exact(query, key);
}

BusterX86MetadataEmitResult buster_x86_metadata_emit_exact(BusterX86MetadataEmitQuery query,
                                                             BusterX86MetadataFormKey key)
{
    return buster_x86_metadata_emit_form_exact(query, key);
}

BusterX86MetadataEmitResult buster_x86_metadata_emit_exact_query(BusterX86MetadataExactQuery query)
{
    // `reserved` is part of the public ABI padding contract.  Rejecting a
    // non-zero value keeps future callers from accidentally depending on an
    // interpretation that this snapshot does not define.
    if (query.reserved)
    {
        return (BusterX86MetadataEmitResult){
            .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
            .form_id = query.key.form_id,
        };
    }
    // Keep this ABI deliberately smaller than BusterX86MetadataEmitQuery:
    // compiler callers already possess a durable key and physical values, so
    // there is no source mnemonic, duplicate form ID, or source-semantics
    // switch to project.  The existing exact entry point remains the single
    // implementation of key validation, structural checks, and emission.
    return buster_x86_metadata_emit_form_exact((BusterX86MetadataEmitQuery){
                                                   .physical = {
                                                       .operands = query.operands,
                                                       .operand_count = query.operand_count,
                                                       .features = query.features,
                                                       .attributes = query.attributes,
                                                       .address_size = query.address_size,
                                                       .execution_mode = query.execution_mode,
                                                       .include_privileged = query.include_privileged,
                                                       .include_not64 = query.include_not64,
                                                       .include_implicit = query.include_implicit,
                                                       .source_semantics = false,
                                                   },
                                                   .output = query.output,
                                                   .output_capacity = query.output_capacity,
                                                   .relocations = query.relocations,
                                                   .relocation_capacity = query.relocation_capacity,
                                               },
                                               query.key);
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataExactPlanRecord const*
buster_x86_metadata_exact_plan_record(BusterX86MetadataExactPlan plan)
{
    if (!buster_x86_metadata_prewarmed || plan.form_id >= BUSTER_X86_GENERATED_FORM_COUNT || !plan.stable_hash) return 0;
    if (!buster_x86_metadata_normalized_forms_cached[plan.form_id] ||
        !buster_x86_metadata_pattern_semantics_cached[plan.form_id] ||
        !buster_x86_metadata_pattern_semantics_results[plan.form_id])
        return 0;
    u16 slot_plus_one = buster_x86_metadata_exact_plan_slots[plan.form_id];
    // The slot map is immutable after serial preparation, but keep malformed
    // state fail-closed before forming a pointer into the record table.
    if (!slot_plus_one || slot_plus_one > buster_x86_metadata_exact_plan_count ||
        slot_plus_one > BUSTER_X86_METADATA_EXACT_PLAN_CAPACITY)
        return 0;
    BusterX86MetadataExactPlanRecord const* record = &buster_x86_metadata_exact_plan_records[slot_plus_one - 1];
    if (!record->ready || record->identity.form_id != plan.form_id || record->identity.stable_hash != plan.stable_hash ||
        !record->form || !record->pattern || record->operand_count > BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY ||
        record->operand_count != record->form->operand_count ||
        record->form->id != plan.form_id || record->form->stable_hash != plan.stable_hash)
        return 0;
    return record;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataExactPlanRecord const*
buster_x86_metadata_machine_exact_plan_record(BusterX86MetadataMachineExactToken token)
{
    if (!buster_x86_metadata_prewarmed || !token.slot_plus_one || token.slot_plus_one > buster_x86_metadata_exact_plan_count ||
        token.slot_plus_one > BUSTER_X86_METADATA_EXACT_PLAN_CAPACITY ||
        (token.policy_flags & (u8)~BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_FLAGS_ALL) ||
        !(token.policy_flags & BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_POLICY_VALID))
        return 0;
    BusterX86MetadataExactPlanRecord const* record = &buster_x86_metadata_exact_plan_records[token.slot_plus_one - 1];
    if (!record->ready || !record->identity.stable_hash || record->identity.form_id >= BUSTER_X86_GENERATED_FORM_COUNT || !record->form ||
        !record->pattern || record->operand_count > BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY ||
        record->operand_count != record->form->operand_count || record->form->id != record->identity.form_id ||
        record->form->stable_hash != record->identity.stable_hash)
        return 0;
    u8 integrity = (u8)(record->machine_exact_integrity ^
                        (token.policy_flags & BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_ALLOWS_APX));
    if (token.integrity != integrity ||
        (record->form->prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 &&
         !(token.policy_flags & BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_ALLOWS_APX)))
        return 0;
    return record;
}

bool buster_x86_metadata_exact_plan_for_key(BusterX86MetadataFormKey key, BusterX86MetadataExactPlan* result)
{
    if (!result) return false;
    BusterX86MetadataExactPlan plan = {.form_id = key.form_id, .stable_hash = key.stable_hash};
    if (!buster_x86_metadata_exact_plan_record(plan)) return false;
    *result = plan;
    return true;
}

bool buster_x86_metadata_machine_exact_token_for_plan(BusterX86MetadataExactPlan plan,
                                                       BusterX86MetadataFeatureInput features,
                                                       BusterX86MetadataMachineExactToken* result)
{
    if (!result) return false;
    *result = (BusterX86MetadataMachineExactToken){0};
    // This is a pure read of the immutable post-prewarm plan table.  Machine
    // prewarm calls it serially before publishing tokens; keeping the lookup
    // read-only also lets differential metadata tests resolve an already
    // prepared plan after the test gang has started without introducing a
    // worker mutation or a lazy cache.
    BusterX86MetadataExactPlanRecord const* record = buster_x86_metadata_exact_plan_record(plan);
    if (!record) return false;
    BusterX86MetadataResolveQuery policy_query = {
        .features = features,
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .include_privileged = false,
        .include_not64 = false,
        .include_implicit = false,
    };
    if (!buster_x86_metadata_resolution_query_valid(policy_query, 0)) return false;
    BusterX86GeneratedForm generated = buster_x86_metadata_form_record(record->form->id);
    bool allows_apx = buster_x86_metadata_feature_input_allows_apx(features);
    if (!buster_x86_metadata_form_coverage_allowed(generated, policy_query) ||
        !buster_x86_metadata_form_execution_mode_matches(generated, policy_query) ||
        !buster_x86_metadata_form_feature_available(generated, features) ||
        (record->form->prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 && !allows_apx))
        return false;
    u16 slot_plus_one = buster_x86_metadata_exact_plan_slots[plan.form_id];
    if (!slot_plus_one || slot_plus_one > buster_x86_metadata_exact_plan_count ||
        slot_plus_one > BUSTER_X86_METADATA_EXACT_PLAN_CAPACITY ||
        record != &buster_x86_metadata_exact_plan_records[slot_plus_one - 1])
        return false;
    result->slot_plus_one = slot_plus_one;
    result->policy_flags = BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_POLICY_VALID;
    if (allows_apx)
        result->policy_flags |= BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_ALLOWS_APX;
    result->integrity = buster_x86_metadata_machine_exact_token_integrity(
        slot_plus_one, result->policy_flags, record->identity.form_id, record->identity.stable_hash);
    return true;
}

BusterX86MetadataEmitResult buster_x86_metadata_emit_exact_prevalidated(BusterX86MetadataExactPlan plan,
                                                                          BusterX86MetadataExactQuery query)
{
    BusterX86MetadataEmitResult result = {
        .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
        .form_id = query.key.form_id,
    };
    if (query.reserved) return result;
    // Validate the caller's durable key against the value identity before
    // touching the immutable record.  A mismatched/stale query therefore
    // fails closed without dereferencing any plan-owned pointers.
    if (query.key.form_id != plan.form_id || query.key.stable_hash != plan.stable_hash)
    {
        result.status = BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM;
        return result;
    }
    BusterX86MetadataExactPlanRecord const* record = buster_x86_metadata_exact_plan_record(plan);
    if (!record)
    {
        result.status = BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM;
        return result;
    }
    if ((query.output_capacity && !query.output) || (query.relocation_capacity && !query.relocations)) return result;

    BusterX86MetadataEmitQuery normalized = {
        .physical = {
            .mnemonic = buster_x86_metadata_string_span(record->form->iclass),
            .operands = query.operands,
            .operand_count = query.operand_count,
            .features = query.features,
            .attributes = query.attributes,
            .address_size = query.address_size,
            .execution_mode = query.execution_mode,
            .include_privileged = query.include_privileged,
            .include_not64 = query.include_not64,
            .include_implicit = query.include_implicit,
            .source_semantics = false,
        },
        .output = query.output,
        .output_capacity = query.output_capacity,
        .relocations = query.relocations,
        .relocation_capacity = query.relocation_capacity,
    };
    if (!buster_x86_metadata_emit_physical_query_valid(normalized.physical)) return result;
    return buster_x86_metadata_emit_form_with_form(&normalized, record->form, false, record, 0, false, false);
}

BusterX86MetadataEmitResult buster_x86_metadata_emit_exact_machine(BusterX86MetadataMachineExactToken token,
                                                                    BusterX86MetadataMachineExactQuery query)
{
    BusterX86MetadataEmitResult result = {
        .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
        .form_id = UINT32_MAX,
    };
    BusterX86MetadataExactPlanRecord const* record = buster_x86_metadata_machine_exact_plan_record(token);
    if (record)
    {
        result.form_id = record->identity.form_id;
        result.stable_hash = record->identity.stable_hash;
        if (!((query.operand_count && !query.operands) || query.operand_count > BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY ||
                (query.output_capacity && !query.output) || (query.relocation_capacity && !query.relocations) ||
                query.mask_register_plus_one > 8 || (query.zeroing && !query.mask_register_plus_one)))
        {
            bool has_mask_register = query.mask_register_plus_one != 0;
            u8 mask_register = has_mask_register ? (u8)(query.mask_register_plus_one - 1) : 0;
            u16 decorator_flags = has_mask_register ? BUSTER_X86_METADATA_DECORATOR_MASK : 0;
            if (query.zeroing) decorator_flags |= BUSTER_X86_METADATA_DECORATOR_ZEROING;

            BusterX86MetadataEmitQuery normalized = {
                .physical = {
                    .mnemonic = buster_x86_metadata_string_span(record->form->iclass),
                    .operands = query.operands,
                    .operand_count = query.operand_count,
                    .attributes = {
                        .decorator_flags = decorator_flags,
                        .mask_register = mask_register,
                        .has_mask_register = has_mask_register,
                        .zeroing = query.zeroing,
                        .lock = query.force_lock,
                    },
                    .address_size = 64,
                    .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
                    .include_privileged = false,
                    .include_not64 = false,
                    .include_implicit = false,
                    .source_semantics = false,
                },
                .output = query.output,
                .output_capacity = query.output_capacity,
                .relocations = query.relocations,
                .relocation_capacity = query.relocation_capacity,
            };

            BusterX86MetadataEmitResult fast_result = result;
            bool use_fast_result = buster_x86_metadata_emit_machine_fast(normalized.physical, record->form, record, &fast_result, query.output, query.output_capacity, query.force_disp32);

            if (use_fast_result)
            {
                result = fast_result;
            }
            else
            {
                result = buster_x86_metadata_emit_form_with_form(&normalized, record->form, false, record, &token, query.force_disp32, false);
            }
        }
    }
    else
    {
        result.status = BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_coverage_structural_blocker(BusterX86MetadataForm form)
{
    if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS) return BUSTER_X86_METADATA_BLOCKER_DECODE_ALIAS;
    if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64) return BUSTER_X86_METADATA_BLOCKER_NOT64;
    if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED) form.coverage_class = BUSTER_X86_METADATA_COVERAGE_NORMALIZED;
    if (form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED)
        return form.coverage_class == BUSTER_X86_METADATA_COVERAGE_UNCLASSIFIED ? BUSTER_X86_METADATA_BLOCKER_UNCLASSIFIED
                                                                                 : BUSTER_X86_METADATA_BLOCKER_RESERVED_SNAPSHOT;
    BusterX86MetadataPatternSemantics pattern = {0};
    if (!buster_x86_metadata_emit_parse_pattern(form, &pattern)) return BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS;
    if (!pattern.opcode_count) return BUSTER_X86_METADATA_BLOCKER_OPCODE_FIELDS;
    if (pattern.unresolved_blocker) return pattern.unresolved_blocker;
    bool moffs_form = buster_x86_metadata_emit_is_moffs(form, pattern);
    u8 control_blocker = buster_x86_metadata_emit_pattern_control_blocker(form, pattern);
    if (control_blocker != BUSTER_X86_METADATA_BLOCKER_NONE) return control_blocker;
    if (pattern.immediate_count > BUSTER_ARRAY_LENGTH(pattern.immediate_widths) || pattern.relative_count > 1)
        return BUSTER_X86_METADATA_BLOCKER_IMMEDIATE_FIELDS;
    if (pattern.force_sib && !pattern.has_modrm) return BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS;
    if ((form.field_flags & BUSTER_X86_METADATA_FIELD_SIB) && !(form.field_flags & BUSTER_X86_METADATA_FIELD_MODRM))
        return BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &operand)) return BUSTER_X86_METADATA_BLOCKER_OPERAND_SEMANTICS;
        if (moffs_form && buster_x86_metadata_emit_is_moffs_supplemental(form, pattern, operand)) continue;
        if (pattern.no_vector_source && buster_x86_metadata_emit_effective_field_source(operand) == BUSTER_X86_METADATA_FIELD_SOURCE_VVVV)
            return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
        if (!operand.visible) continue;
        if (operand.kind == BUSTER_X86_METADATA_OPERAND_BASE || operand.kind == BUSTER_X86_METADATA_OPERAND_SEGMENT ||
            (operand.kind == BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR && !pattern.has_mpx_mode && !pattern.has_remove_segment) ||
            operand.kind == BUSTER_X86_METADATA_OPERAND_PSEUDO)
            return BUSTER_X86_METADATA_BLOCKER_OPERAND_SEMANTICS;
        if (operand.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN)
            return BUSTER_X86_METADATA_BLOCKER_OPERAND_SEMANTICS;
    }
    return BUSTER_X86_METADATA_BLOCKER_NONE;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_coverage_form_blocker(BusterX86MetadataForm form)
{
    if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS) return BUSTER_X86_METADATA_BLOCKER_DECODE_ALIAS;
    if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64) return BUSTER_X86_METADATA_BLOCKER_NOT64;
    if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED) return BUSTER_X86_METADATA_BLOCKER_PRIVILEGED;
    return buster_x86_metadata_coverage_structural_blocker(form);
}

BUSTER_GLOBAL_LOCAL u16 buster_x86_metadata_coverage_width(u16 flags, u8 physical_class, BusterX86MetadataPatternSemantics pattern)
{
    if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM) return 128;
    if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM) return 256;
    if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM) return 512;
    if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM) return 1024;
    if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK) return 64;
    if (pattern.mandatory_prefix == 0x66 && (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_16)) return 16;
    if (pattern.has_w && !pattern.w && (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_32)) return 32;
    if (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_64) return 64;
    if (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_32) return 32;
    if (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_16) return 16;
    if (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_8) return 8;
    if (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_80) return 80;
    if (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_128) return 128;
    if (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_256) return 256;
    if (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_512) return 512;
    if (flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024) return 1024;
    return physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR ? 64 : 0;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_coverage_register(BusterX86MetadataOperand metadata,
                                                                BusterX86MetadataPatternSemantics pattern,
                                                                BusterX86MetadataPhysicalOperand* result)
{
    u8 physical_class = metadata.physical_class;
    if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN ||
        physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE || physical_class >= BUSTER_X86_METADATA_PHYSICAL_CLASS_COUNT)
        return false;
    u16 width = buster_x86_metadata_coverage_width(metadata.physical_width_flags, physical_class, pattern);
    if (!width) return false;
    BusterX86MetadataPhysicalRegister reg = {
        .index = buster_x86_metadata_emit_atom_contains(metadata.atom, S8("_SE")) ? 1 : 0,
        .width = width,
        .physical_class = physical_class,
    };
    if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR)
    {
        // Canonical coverage queries must satisfy typed B/B4 controls instead
        // of accidentally choosing rax and classifying a valid row as a
        // prefix blocker.  The controls describe the RM/dynamic-opcode field
        // for the residual XCHG rows, whose visible operand is this GPR.
        if (pattern.rex_b4_control == BUSTER_X86_METADATA_REX_CONTROL_REQUIRE)
            reg.index |= 16;
        else if (pattern.rex_b_control == BUSTER_X86_METADATA_REX_CONTROL_REQUIRE)
            reg.index |= 8;
        if (pattern.rex_b4_control == BUSTER_X86_METADATA_REX_CONTROL_FORBID) reg.index &= (u16)~16u;
        if (pattern.rex_b_control == BUSTER_X86_METADATA_REX_CONTROL_FORBID) reg.index &= (u16)~8u;
    }
    bool fixed = metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED ||
                 buster_x86_metadata_emit_atom_contains(metadata.atom, S8("XED_REG_"));
    if (fixed)
    {
        u16 index = 0;
        u16 candidate_width = width;
        for (; index < 32; index += 1)
        {
            reg.index = index;
            reg.width = candidate_width;
            reg.high_byte = false;
            if (buster_x86_metadata_emit_fixed_register_matches(metadata, (BusterX86MetadataPhysicalOperand){
                                                                    .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                                                                    .width = candidate_width,
                                                                    .reg = reg,
                                                                }))
                break;
            if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && candidate_width == 8 && index >= 4 && index < 8)
            {
                reg.high_byte = true;
                if (buster_x86_metadata_emit_fixed_register_matches(metadata, (BusterX86MetadataPhysicalOperand){
                                                                        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                                                                        .width = candidate_width,
                                                                        .reg = reg,
                                                                    }))
                    break;
                reg.high_byte = false;
            }
        }
        if (index == 32) return false;
    }
    else
    {
        // Fixed ModRM REG/RM rows expose both fields as ordinary visible
        // register operands (for example the CET/IBHF NOP cohort).  Select
        // the exact field value for coverage instead of defaulting every
        // operand to rax and turning a representable row into a register
        // encoding blocker.  The effective source handles operand aliases
        // and x87's REG/RM inversion without making the two-register spelling
        // part of the public source resolver contract.
        u8 field_source = buster_x86_metadata_emit_effective_field_source_pattern(metadata, pattern);
        if (field_source == BUSTER_X86_METADATA_FIELD_SOURCE_REG &&
            !buster_x86_metadata_emit_reg_is_unconstrained(pattern))
            reg.index = pattern.has_reg_range ? pattern.reg_min : pattern.reg_fixed;
        else if (field_source == BUSTER_X86_METADATA_FIELD_SOURCE_RM &&
                 pattern.rm_fixed != BUSTER_X86_METADATA_PATTERN_FIXED_ANY)
            reg.index = pattern.rm_fixed;
    }
    *result = (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
        .width = reg.width,
        .reg = reg,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_coverage_canonical_query(BusterX86MetadataForm form,
                                                                        BusterX86MetadataPhysicalQuery* query,
                                                                        BusterX86MetadataPhysicalOperand operands[16],
                                                                        String8 features[1], char8 mnemonic_buffer[128])
{
    BusterX86MetadataPatternSemantics pattern = {0};
    if (!buster_x86_metadata_emit_parse_pattern(form, &pattern) || pattern.unresolved_blocker) return false;
    bool moffs_form = buster_x86_metadata_emit_is_moffs(form, pattern);
    if (form.iclass.length >= 128) return false;
    for (u32 index = 0; index < form.iclass.length; index += 1)
    {
        mnemonic_buffer[index] = (char8)buster_x86_metadata_string_byte(form.iclass, index);
    }
    String8 mnemonic = {.pointer = mnemonic_buffer, .length = form.iclass.length};
    u32 operand_count = 0;
    bool canonical_has_memory = false;
    u8 address_size = pattern.required_address_size ? pattern.required_address_size : 64;
    if (!pattern.required_address_size && (form.mode_flags & BUSTER_X86_METADATA_MODE_EA32) &&
        !(form.mode_flags & BUSTER_X86_METADATA_MODE_EA64))
        address_size = 32;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form.id, operand_index, &metadata)) return false;
        if (moffs_form && buster_x86_metadata_emit_is_moffs_supplemental(form, pattern, metadata)) continue;
        if (!metadata.visible) continue;
        if (operand_count >= 16) return false;
        if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER)
        {
            if (!buster_x86_metadata_coverage_register(metadata, pattern, operands + operand_count)) return false;
            if (pattern.srm_not_equal && metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
                operands[operand_count].reg.index == 0)
                operands[operand_count].reg.index = 1;
        }
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY ||
                 (metadata.kind == BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR && (pattern.has_mpx_mode || pattern.has_remove_segment)))
        {
            canonical_has_memory = true;
            u16 width = buster_x86_metadata_coverage_width(metadata.physical_width_flags,
                                                            BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY, pattern);
            if (!width) width = 64;
            bool vsib = (form.field_flags & BUSTER_X86_METADATA_FIELD_VSIB) != 0;
            BusterX86MetadataPhysicalMemory memory = {
                .address_size = address_size,
                .scale = 1,
                .has_base = !moffs_form,
                .has_index = vsib,
                .vsib = vsib,
                .has_displacement = moffs_form || (form.field_flags & BUSTER_X86_METADATA_FIELD_DISPLACEMENT) != 0,
                .base = {.index = 0, .width = address_size, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
            };
            // BNDLDX/BNDSTX carry three distinct fixed ModRM modes.  Their
            // raw rows do not advertise a displacement field, but MOD=1/2
            // still require a canonical displacement so the coverage audit
            // exercises those exact byte shapes instead of always selecting
            // MOD=0.  A zero byte is enough for MOD=1; 0x100 forces MOD=2.
            if (pattern.has_mpx_mode &&
                (buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDLDX")) ||
                 buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDSTX"))) &&
                (pattern.mod_kind == 1 || pattern.mod_kind == 2))
            {
                memory.has_displacement = true;
                memory.displacement = pattern.mod_kind == 1 ? 0 : 0x100;
            }
            bool prefetchit = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("PREFETCHIT0")) ||
                              buster_x86_metadata_string_input_equal(form.iclass.offset, S8("PREFETCHIT1"));
            if (prefetchit)
            {
                memory.has_base = false;
                memory.rip_relative = true;
            }
            if (vsib)
            {
                u8 vector_class = pattern.vsib_vector_length == 512 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM
                                  : pattern.vsib_vector_length == 256 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM
                                                                     : BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM;
                memory.index = (BusterX86MetadataPhysicalRegister){
                    .index = 0,
                    .width = vector_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM
                                 ? 512
                                 : vector_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM ? 256 : 128,
                    .physical_class = vector_class,
                };
            }
            operands[operand_count] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                .width = width,
                .memory = memory,
            };
        }
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE)
        {
            u16 width = buster_x86_metadata_coverage_width(metadata.physical_width_flags,
                                                            BUSTER_X86_METADATA_PHYSICAL_CLASS_IMMEDIATE, pattern);
            operands[operand_count] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
                .width = width,
                .value = 0,
                .has_value = true,
            };
        }
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_RELATIVE)
        {
            operands[operand_count] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
                .width = form.displacement_width * 8,
                .value = 0,
                .has_value = true,
            };
        }
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_ABSOLUTE)
        {
            operands[operand_count] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE,
                .width = form.displacement_width * 8,
                .value = 0,
                .has_value = true,
            };
        }
        else return false;
        operand_count += 1;
    }
    BusterX86MetadataPhysicalAttributes attributes = {0};
    if (pattern.has_cet_no_track) attributes.notrack = true;
    if (pattern.lock_control == 1) attributes.lock = true;
    if (pattern.rep_control == 1) attributes.rep = true;
    if (pattern.rep_control == 2) attributes.repne = true;
    if ((form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST) && canonical_has_memory)
    {
        u8 broadcast_elements = buster_x86_metadata_emit_broadcast_elements(form, pattern);
        if (!broadcast_elements) return false;
        attributes.decorator_flags |= BUSTER_X86_METADATA_DECORATOR_BROADCAST;
        attributes.broadcast_elements = broadcast_elements;
    }
    if (pattern.mask_control == 2 || pattern.zeroing_control == 2)
    {
        attributes.decorator_flags |= BUSTER_X86_METADATA_DECORATOR_MASK;
        attributes.has_mask_register = true;
        attributes.mask_register = 1;
    }
    if (pattern.zeroing_control == 2)
    {
        attributes.decorator_flags |= BUSTER_X86_METADATA_DECORATOR_ZEROING;
        attributes.zeroing = true;
    }
    if (pattern.has_sae_control)
    {
        attributes.decorator_flags |= BUSTER_X86_METADATA_DECORATOR_SAE;
        attributes.sae = true;
    }
    if (pattern.has_rounding_control)
    {
        attributes.decorator_flags |= BUSTER_X86_METADATA_DECORATOR_ROUNDING;
        attributes.rounding_mode = BUSTER_X86_METADATA_ROUNDING_NEAREST;
    }
    if (pattern.has_nd && pattern.nd_value) attributes.apx_flags |= BUSTER_X86_METADATA_APX_NDD;
    if (pattern.has_nf && pattern.nf_value)
    {
        attributes.apx_flags |= BUSTER_X86_METADATA_APX_NF;
        attributes.no_flags = true;
    }
    if (buster_x86_metadata_form_iform_requires_dfv(form))
    {
        attributes.has_dfv = true;
        attributes.dfv = 0;
    }
    features[0] = S8("*");
    bool legacy_repeat_cohort = pattern.rep_control != 0 &&
                                form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                form.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY &&
                                buster_x86_metadata_string_input_equal(form.extension.offset, S8("BASE"));
    bool mode66_residual = buster_x86_metadata_emit_mode66_residual(form, pattern);
    bool mpx_mode_cohort = pattern.has_mpx_mode &&
                           form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                           form.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY &&
                           buster_x86_metadata_string_input_equal(form.extension.offset, S8("MPX")) &&
                           buster_x86_metadata_string_input_equal(form.iclass.offset, S8("BNDMOV"));
    bool legacy_mode_cohort = legacy_repeat_cohort || mode66_residual || mpx_mode_cohort;
    bool include_not64 = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64 ||
                         (form.mode_flags & BUSTER_X86_METADATA_MODE_NOT64) != 0;
    u8 execution_mode = buster_x86_metadata_form_declared_execution_mode((BusterX86GeneratedForm){
        .coverage_class = form.coverage_class,
        .mode_flags = form.mode_flags,
    }, legacy_mode_cohort);
    if (include_not64 && execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64)
        execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_ANY;
    *query = (BusterX86MetadataPhysicalQuery){
        .mnemonic = mnemonic,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = features, .count = 1},
        .attributes = attributes,
        .address_size = address_size,
        .execution_mode = execution_mode,
        .include_privileged = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED,
        .include_not64 = include_not64,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_coverage_encode_blocker(BusterX86MetadataEncodeStatus status)
{
    switch (status)
    {
    case BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING:
    case BUSTER_X86_METADATA_ENCODE_HIGH_BYTE_WITH_REX:
    case BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION: return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    case BUSTER_X86_METADATA_ENCODE_ADDRESSING:
    case BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE: return BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS;
    case BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE:
    case BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE: return BUSTER_X86_METADATA_BLOCKER_IMMEDIATE_FIELDS;
    case BUSTER_X86_METADATA_ENCODE_DECORATOR: return BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS;
    case BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE:
    case BUSTER_X86_METADATA_ENCODE_INSTRUCTION_LENGTH: return BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS;
    case BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH:
    case BUSTER_X86_METADATA_ENCODE_WRONG_OPERAND_COUNT:
    case BUSTER_X86_METADATA_ENCODE_AMBIGUOUS: return BUSTER_X86_METADATA_BLOCKER_OPERAND_SEMANTICS;
    case BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA: return BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS;
    default: return BUSTER_X86_METADATA_BLOCKER_OPERAND_SEMANTICS;
    }
}

BusterX86MetadataCoverageAuditResult buster_x86_metadata_coverage_audit(BusterX86MetadataCoverageLedgerEntry* entries,
                                                                          u32 entry_capacity)
{
    BusterX86MetadataCoverageAuditResult result = {
        .required_entry_count = buster_x86_metadata_form_count(),
    };
    bool storage_complete = entries != 0 && entry_capacity >= result.required_entry_count;
    bool retrieval_failed = false;
    u32 write_count = entries ? BUSTER_MIN(entry_capacity, result.required_entry_count) : 0;
    for (u32 form_id = 0; form_id < result.required_entry_count; form_id += 1)
    {
        BusterX86MetadataForm form = {0};
        if (!buster_x86_metadata_form(form_id, &form))
        {
            retrieval_failed = true;
            continue;
        }
        if (form.id != form_id) result.duplicate_form_id = true;
        u8 structural_blocker = buster_x86_metadata_coverage_structural_blocker(form);
        bool normalized = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED;
        bool policy_excluded = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED ||
                               form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64 ||
                               form.coverage_class == BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS;
        bool encoder_capable = false;
        u8 blocker = structural_blocker;
        if (structural_blocker == BUSTER_X86_METADATA_BLOCKER_NONE)
        {
            BusterX86MetadataPhysicalOperand operands[16] = {0};
            String8 features[1] = {0};
            char8 mnemonic_buffer[128] = {0};
            BusterX86MetadataPhysicalQuery physical = {0};
            if (buster_x86_metadata_coverage_canonical_query(form, &physical, operands, features, mnemonic_buffer))
            {
                u8 bytes[32] = {0};
                BusterX86MetadataRelocation relocations[BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY] = {0};
                BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                    .physical = physical,
                    .form_id = form_id,
                    .output = bytes,
                    .output_capacity = BUSTER_ARRAY_LENGTH(bytes),
                    .relocations = relocations,
                    .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                });
                if (emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS)
                {
                    encoder_capable = true;
                    blocker = policy_excluded ? buster_x86_metadata_coverage_form_blocker(form)
                                               : BUSTER_X86_METADATA_BLOCKER_NONE;
                }
                else blocker = buster_x86_metadata_coverage_encode_blocker(emitted.status);
            }
            else blocker = BUSTER_X86_METADATA_BLOCKER_OPERAND_SEMANTICS;
        }
        u8 disposition = normalized && encoder_capable && blocker == BUSTER_X86_METADATA_BLOCKER_NONE
                             ? BUSTER_X86_METADATA_COVERAGE_EMITTED
                             : BUSTER_X86_METADATA_COVERAGE_BLOCKED;
        if (normalized)
        {
            result.normalized_entry_count += 1;
            result.family_counts[form.encoder_family] += 1;
            result.family_blocked_counts[form.encoder_family] += blocker != BUSTER_X86_METADATA_BLOCKER_NONE;
            result.family_emitted_counts[form.encoder_family] += blocker == BUSTER_X86_METADATA_BLOCKER_NONE;
            result.schema_inexpressible_count += !encoder_capable;
        }
        if (encoder_capable) result.encoder_capable_count += 1;
        if (policy_excluded) result.policy_excluded_count += 1;
        if (form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
            form.coverage_class != BUSTER_X86_METADATA_COVERAGE_PRIVILEGED)
            result.explicitly_unsupported_count += 1;
        result.disposition_counts[disposition] += 1;
        result.blocker_counts[blocker] += 1;
        if (disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED) result.emitted_count += 1;
        else result.blocked_count += 1;
        if (form_id < write_count)
        {
            entries[form_id] = (BusterX86MetadataCoverageLedgerEntry){
                .form_id = form_id,
                .stable_hash = form.stable_hash,
                .coverage_class = form.coverage_class,
                .encoder_family = form.encoder_family,
                .disposition = disposition,
                .blocker = blocker,
                .encoder_capable = encoder_capable,
                .policy_excluded = policy_excluded,
            };
            result.entry_count += 1;
        }
        BusterX86MetadataCandidateRange hash_candidates = buster_x86_metadata_lookup_form_hash(form.stable_hash);
        if (hash_candidates.count > 1) result.duplicate_stable_hash = true;
    }
    result.complete = storage_complete && !retrieval_failed && result.entry_count == result.required_entry_count;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 buster_x86_metadata_completion_digest_byte(u64 hash, u8 value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

BUSTER_GLOBAL_LOCAL u64 buster_x86_metadata_completion_digest_u16(u64 hash, u16 value)
{
    hash = buster_x86_metadata_completion_digest_byte(hash, (u8)value);
    return buster_x86_metadata_completion_digest_byte(hash, (u8)(value >> 8));
}

BUSTER_GLOBAL_LOCAL u64 buster_x86_metadata_completion_digest_u32(u64 hash, u32 value)
{
    hash = buster_x86_metadata_completion_digest_byte(hash, (u8)value);
    hash = buster_x86_metadata_completion_digest_byte(hash, (u8)(value >> 8));
    hash = buster_x86_metadata_completion_digest_byte(hash, (u8)(value >> 16));
    return buster_x86_metadata_completion_digest_byte(hash, (u8)(value >> 24));
}

BUSTER_GLOBAL_LOCAL u64 buster_x86_metadata_completion_digest_u64(u64 hash, u64 value)
{
    hash = buster_x86_metadata_completion_digest_u32(hash, (u32)value);
    return buster_x86_metadata_completion_digest_u32(hash, (u32)(value >> 32));
}

BUSTER_GLOBAL_LOCAL u64 buster_x86_metadata_completion_digest_string(u64 hash, BusterX86MetadataString value)
{
    u32 index = 0;
    hash = buster_x86_metadata_completion_digest_u32(hash, value.offset);
    hash = buster_x86_metadata_completion_digest_u32(hash, value.length);
    for (; index < value.length; index += 1)
        hash = buster_x86_metadata_completion_digest_byte(hash, buster_x86_metadata_string_byte(value, index));
    return hash;
}

BUSTER_GLOBAL_LOCAL u64 buster_x86_metadata_completion_digest_operand(u64 hash, BusterX86MetadataOperand value)
{
    hash = buster_x86_metadata_completion_digest_string(hash, value.atom);
    hash = buster_x86_metadata_completion_digest_string(hash, value.width);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.slot);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.visible);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.kind);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.access);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.field_source);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.physical_class);
    return buster_x86_metadata_completion_digest_u16(hash, value.physical_width_flags);
}

BUSTER_GLOBAL_LOCAL u64 buster_x86_metadata_completion_digest_form(u64 hash, BusterX86MetadataForm value)
{
    u32 index = 0;
    hash = buster_x86_metadata_completion_digest_u32(hash, value.id);
    hash = buster_x86_metadata_completion_digest_u64(hash, value.stable_hash);
#define BUSTER_X86_METADATA_DIGEST_FORM_STRING(field) hash = buster_x86_metadata_completion_digest_string(hash, value.field)
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(source);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(iclass);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(iform);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(isa_set);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(category);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(extension);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(attributes);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(cpl);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(exceptions);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(flags);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(disasm);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(disasm_intel);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(disasm_att);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(real_opcode);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(uname);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(comment);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(version);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(pattern);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(operands);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(operand_annotation);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(tuple);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(element_size);
    BUSTER_X86_METADATA_DIGEST_FORM_STRING(reason);
#undef BUSTER_X86_METADATA_DIGEST_FORM_STRING
    hash = buster_x86_metadata_completion_digest_u32(hash, value.operand_first);
    hash = buster_x86_metadata_completion_digest_u16(hash, value.operand_count);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.coverage_class);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.encoder_family);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.test_class);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.prefix_kind);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.map);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.fixed_byte_count);
    for (; index < BUSTER_ARRAY_LENGTH(value.fixed_bytes); index += 1)
        hash = buster_x86_metadata_completion_digest_byte(hash, value.fixed_bytes[index]);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.mandatory_prefix);
    hash = buster_x86_metadata_completion_digest_u16(hash, value.field_flags);
    hash = buster_x86_metadata_completion_digest_u16(hash, value.decorator_flags);
    hash = buster_x86_metadata_completion_digest_u16(hash, value.apx_flags);
    hash = buster_x86_metadata_completion_digest_u16(hash, value.amx_flags);
    hash = buster_x86_metadata_completion_digest_u16(hash, value.mode_flags);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.displacement_width);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.displacement_scale);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.immediate_width);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.immediate_signed);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.relocation_base);
    hash = buster_x86_metadata_completion_digest_byte(hash, value.tuple_kind);
    hash = buster_x86_metadata_completion_digest_u32(hash, value.tuple_offset);
    hash = buster_x86_metadata_completion_digest_u32(hash, value.element_size_offset);
    hash = buster_x86_metadata_completion_digest_u32(hash, value.token_count);
    return buster_x86_metadata_completion_digest_u16(hash, value.reason_id);
}

u64 buster_x86_metadata_coverage_digest(BusterX86MetadataCoverageLedgerEntry const* entries, u32 entry_count, u32 entry_capacity)
{
    u32 form_count = 0;
    u64 hash = 0;
    u32 form_id = 0;
    form_count = buster_x86_metadata_form_count();
    if (entry_count > form_count) entry_count = form_count;
    hash = UINT64_C(14695981039346656037);
    hash = buster_x86_metadata_completion_digest_u32(hash, entry_count);
    for (; form_id < entry_count; form_id += 1)
    {
        BusterX86MetadataForm form = {0};
        BusterX86MetadataCoverageLedgerEntry entry = {0};
        u32 entry_reserved_index = 0;
        u32 operand_index = 0;
        u32 visible_count = 0;
        u32 operand_kind_counts[BUSTER_X86_METADATA_OPERAND_KIND_COUNT] = {0};
        u32 operand_kind_index = 0;
        BusterX86MetadataOperand operand = {0};
        bool retrieved = false;
        if (!buster_x86_metadata_form(form_id, &form)) continue;
        if (entries && form_id < entry_capacity) entry = entries[form_id];
        hash = buster_x86_metadata_completion_digest_form(hash, form);
        hash = buster_x86_metadata_completion_digest_u32(hash, entry.form_id);
        hash = buster_x86_metadata_completion_digest_u64(hash, entry.stable_hash);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.coverage_class);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.encoder_family);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.disposition);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.blocker);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.encoder_capable);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.policy_excluded);
        for (; entry_reserved_index < BUSTER_ARRAY_LENGTH(entry.reserved); entry_reserved_index += 1)
            hash = buster_x86_metadata_completion_digest_byte(hash, entry.reserved[entry_reserved_index]);
        for (; operand_index < form.operand_count; operand_index += 1)
        {
            operand = (BusterX86MetadataOperand){0};
            hash = buster_x86_metadata_completion_digest_u32(hash, operand_index);
            retrieved = buster_x86_metadata_operand(form_id, operand_index, &operand);
            hash = buster_x86_metadata_completion_digest_byte(hash, retrieved);
            if (retrieved) hash = buster_x86_metadata_completion_digest_operand(hash, operand);
        }

        // Keep one authoritative structural digest stream. This compact
        // classification tuple is part of the checked-in ledger contract and
        // follows the complete decoded row and operands above.
        operand_index = 0;
        for (; operand_index < form.operand_count; operand_index += 1)
        {
            operand = (BusterX86MetadataOperand){0};
            if (!buster_x86_metadata_operand(form_id, operand_index, &operand)) continue;
            visible_count += operand.visible != 0;
            if (operand.kind < BUSTER_X86_METADATA_OPERAND_KIND_COUNT) operand_kind_counts[operand.kind] += 1;
        }
        hash = buster_x86_metadata_completion_digest_u32(hash, form.id);
        hash = buster_x86_metadata_completion_digest_u64(hash, form.stable_hash);
        hash = buster_x86_metadata_completion_digest_byte(hash, form.coverage_class);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.disposition);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.blocker);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.encoder_family);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.encoder_capable != 0);
        hash = buster_x86_metadata_completion_digest_byte(hash, entry.policy_excluded != 0);
        hash = buster_x86_metadata_completion_digest_u16(hash, form.field_flags);
        hash = buster_x86_metadata_completion_digest_u16(hash, form.decorator_flags);
        hash = buster_x86_metadata_completion_digest_u16(hash, form.apx_flags);
        hash = buster_x86_metadata_completion_digest_u16(hash, form.amx_flags);
        hash = buster_x86_metadata_completion_digest_u16(hash, form.mode_flags);
        hash = buster_x86_metadata_completion_digest_byte(hash, form.prefix_kind);
        hash = buster_x86_metadata_completion_digest_byte(hash, form.map);
        hash = buster_x86_metadata_completion_digest_byte(hash, (u8)visible_count);
        hash = buster_x86_metadata_completion_digest_u16(hash, form.operand_count);
        for (; operand_kind_index < BUSTER_X86_METADATA_OPERAND_KIND_COUNT; operand_kind_index += 1)
            hash = buster_x86_metadata_completion_digest_u32(hash, operand_kind_counts[operand_kind_index]);
    }
    return hash;
}

bool buster_x86_metadata_canonical_query(u32 form_id, BusterX86MetadataPhysicalQuery* query,
                                         BusterX86MetadataPhysicalOperand operands[16], String8 features[1],
                                         char8 mnemonic_buffer[128])
{
    BusterX86MetadataForm form = {0};
    return query && operands && features && mnemonic_buffer && buster_x86_metadata_form(form_id, &form) &&
           buster_x86_metadata_coverage_canonical_query(form, query, operands, features, mnemonic_buffer);
}
#if BUSTER_INCLUDE_TESTS
bool buster_x86_metadata_test_canonical_query(u32 form_id, BusterX86MetadataPhysicalQuery* query,
                                              BusterX86MetadataPhysicalOperand operands[16], String8 features[1],
                                              char8 mnemonic_buffer[128])
{
    return buster_x86_metadata_canonical_query(form_id, query, operands, features, mnemonic_buffer);
}
#endif

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
        case BUSTER_X86_METADATA_INDEX_MNEMONIC: return buster_x86_metadata_mnemonic_range(index);
        case BUSTER_X86_METADATA_INDEX_ICLASS: return buster_x86_metadata_iclass_range(index);
        case BUSTER_X86_METADATA_INDEX_IFORM: return buster_x86_metadata_iform_range(index);
        default: return (BusterX86GeneratedTextRange){0};
    }
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_text_candidate_at(u32 kind, u32 index)
{
    switch (kind)
    {
        case BUSTER_X86_METADATA_INDEX_MNEMONIC: return buster_x86_metadata_mnemonic_candidate(index);
        case BUSTER_X86_METADATA_INDEX_ICLASS: return buster_x86_metadata_iclass_candidate(index);
        case BUSTER_X86_METADATA_INDEX_IFORM: return buster_x86_metadata_iform_candidate(index);
        default: return UINT32_MAX;
    }
}

BUSTER_GLOBAL_LOCAL int buster_x86_metadata_compare_pool_string(u32 offset, const char8* pointer, u32 length)
{
    u32 index = 0;
    while (index < length)
    {
        char8 left = buster_x86_metadata_pool_byte((u64)offset + index);
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
    return buster_x86_metadata_pool_byte((u64)offset + length) == 0 ? 0 : 1;
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
        char8 first = buster_x86_metadata_pool_byte((u64)first_offset + index);
        char8 second = buster_x86_metadata_pool_byte((u64)second_offset + index);
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
        key[index] = buster_x86_metadata_pool_byte((u64)key_offset + index);
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
               buster_x86_metadata_is_space(buster_x86_metadata_pool_byte((u64)source_offsets[source_index] + start)))
        {
            start += 1;
        }
        u32 end = start;
        while (end < source_length &&
               !buster_x86_metadata_is_space(buster_x86_metadata_pool_byte((u64)source_offsets[source_index] + end)))
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
            char8 character = buster_x86_metadata_pool_byte((u64)source_offsets[source_index] + start + index);
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
            char8 character = buster_x86_metadata_pool_byte((u64)form->iclass_offset + index);
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
                char8 left = buster_x86_metadata_pool_byte((u64)previous_key + index);
                char8 right = buster_x86_metadata_pool_byte((u64)range.key_offset + index);
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
            BusterX86GeneratedForm form = buster_x86_metadata_form_record(id);
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
    return kind == BUSTER_X86_METADATA_INDEX_FORM_HASH ? buster_x86_metadata_form_hash_range(index)
                                                       : buster_x86_metadata_coverage_hash_range(index);
}

BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_hash_candidate_at(u32 kind, u32 index)
{
    return kind == BUSTER_X86_METADATA_INDEX_FORM_HASH ? buster_x86_metadata_form_hash_candidate(index)
                                                       : buster_x86_metadata_coverage_hash_candidate(index);
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
                BusterX86GeneratedForm form = buster_x86_metadata_form_record(id);
                if (form.stable_hash != range.key)
                {
                    return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY, range_index, id);
                }
            }
            else
            {
                BusterX86GeneratedCoverage coverage = buster_x86_metadata_coverage_record(id);
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
    BusterX86MetadataPatternSemantics pattern = {0};
    buster_x86_metadata_emit_parse_pattern(*destination, &pattern);
    // Older packed snapshots classified UISA_VMODRM_* rows as ordinary
    // MODRM+memory fields even though the pattern carries their VSIB index
    // contract.  Normalize that source-semantic field here, generically for
    // every such row, so selection and emission share the same typed address
    // path without mnemonic-specific exceptions.
    if (pattern.has_vsib_control) destination->field_flags |= BUSTER_X86_METADATA_FIELD_VSIB;
    // Only override stale packed prefix/family metadata when the source
    // pattern carries an unambiguous vector-family discriminator that the
    // compact row lost.  norex2_prefix is a constraint on an otherwise
    // legacy/REX form, not a request to downgrade its REX family, and AMX is
    // a distinct architectural family even though its byte prefix is VEX or
    // EVEX.
    bool pattern_prefix_override = pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 ||
                                   pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP || pattern.explicit_evex_selector ||
                                   pattern.no_rexr_prefix;
    if (pattern.has_prefix_kind && pattern_prefix_override)
    {
        destination->prefix_kind = pattern.prefix_kind;
        // EVV is authoritative even when a later VNP token appears.  Some
        // packed rows inherited the VEX family from the old importer; keep
        // the normalized family in lockstep with the parsed prefix for every
        // vector/prefix family, not just XOP/REX2.
        if (source.encoder_family != BUSTER_X86_METADATA_ENCODER_AMX)
            destination->encoder_family = pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX ? BUSTER_X86_METADATA_ENCODER_REX
                                      : pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 ? BUSTER_X86_METADATA_ENCODER_REX2
                                      : pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX ? BUSTER_X86_METADATA_ENCODER_VEX
                                      : pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP ? BUSTER_X86_METADATA_ENCODER_XOP
                                      : pattern.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX ? BUSTER_X86_METADATA_ENCODER_EVEX
                                                                                                 : BUSTER_X86_METADATA_ENCODER_LEGACY;
    }
    if (pattern.no_rex2 && source.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2)
    {
        destination->prefix_kind = pattern.has_w ? BUSTER_X86_METADATA_PREFIX_REX : BUSTER_X86_METADATA_PREFIX_LEGACY;
        destination->encoder_family = pattern.has_w ? BUSTER_X86_METADATA_ENCODER_REX : BUSTER_X86_METADATA_ENCODER_LEGACY;
    }
}

// buster_x86_metadata_copy_form re-tokenizes the form's whole pattern string
// to normalize prefix/family metadata, and its callers run it per query --
// filtering alone parses every candidate form's pattern on every iteration.
// The result depends only on the decoded record and its id, so normalize each
// form once and copy the cached value out afterwards.  The cache arrays are
// declared beside the pattern-semantics cache above emit_parse_pattern.
BUSTER_GLOBAL_LOCAL void buster_x86_metadata_normalized_form(u32 form_id, BusterX86MetadataForm* result)
{
    if (form_id >= BUSTER_X86_GENERATED_FORM_COUNT)
    {
        *result = (BusterX86MetadataForm){0};
        return;
    }
    if (!buster_x86_metadata_normalized_forms_cached[form_id])
    {
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        buster_x86_metadata_copy_form(buster_x86_metadata_form_record(form_id), form_id, &buster_x86_metadata_normalized_forms[form_id]);
        buster_x86_metadata_normalized_forms_cached[form_id] = true;
    }
    *result = buster_x86_metadata_normalized_forms[form_id];
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_is_64_bit(BusterX86GeneratedForm form)
{
    u16 mode = form.mode_flags;
    u16 mode_bits = mode & (BUSTER_X86_GENERATED_MODE_16 | BUSTER_X86_GENERATED_MODE_32 | BUSTER_X86_GENERATED_MODE_64);
    return !(mode & BUSTER_X86_GENERATED_MODE_NOT64) && (!mode_bits || (mode_bits & BUSTER_X86_GENERATED_MODE_64));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_matches_filter(u32 form_id, BusterX86GeneratedForm form, BusterX86MetadataFilter filter)
{
    BusterX86MetadataForm normalized = {0};
    buster_x86_metadata_normalized_form(form_id, &normalized);
    u8 coverage_class = form.coverage_class;
    if (filter.require_64_bit && !buster_x86_metadata_form_is_64_bit(form)) return false;
    if (filter.exclude_not64 && (coverage_class == BUSTER_X86_GENERATED_COVERAGE_NOT64 || form.mode_flags & BUSTER_X86_GENERATED_MODE_NOT64)) return false;
    if (filter.privileged_only && coverage_class != BUSTER_X86_GENERATED_COVERAGE_PRIVILEGED) return false;
    if (filter.exclude_privileged && coverage_class == BUSTER_X86_GENERATED_COVERAGE_PRIVILEGED) return false;
    if (filter.exclude_reserved && coverage_class == BUSTER_X86_GENERATED_COVERAGE_RESERVED) return false;
    if (filter.exclude_unsupported_token && coverage_class == BUSTER_X86_GENERATED_COVERAGE_UNSUPPORTED_TOKEN) return false;
    if (filter.has_coverage_class_mask && !(filter.coverage_class_mask & (1u << coverage_class))) return false;
    if (filter.has_prefix_kind && normalized.prefix_kind != filter.prefix_kind) return false;
    if (filter.has_encoder_family && normalized.encoder_family != filter.encoder_family) return false;
    if (filter.has_isa_set && !buster_x86_metadata_string_equal_offsets(form.isa_set_offset, filter.isa_set.offset)) return false;
    if (filter.has_operand_count && form.operand_count != filter.operand_count) return false;
    u32 visible_count = 0;
    if (filter.has_visible_operand_count || filter.operand_shape_count)
    {
        if (filter.operand_shape_count > BUSTER_X86_METADATA_MAX_OPERAND_SHAPE ||
            (filter.operand_shape_count && form.operand_count != filter.operand_shape_count)) return false;
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterX86GeneratedOperand operand = buster_x86_metadata_operand_record(form.operand_first + operand_index);
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
    if (BUSTER_X86_GENERATED_SCHEMA_VERSION != 3 || !BUSTER_X86_GENERATED_PACKED_BASE64)
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
        BusterX86GeneratedOperand operand = buster_x86_metadata_operand_record(index);
        if (!buster_x86_metadata_validate_operand_record(&operand, index, result)) return false;
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_FORM_COUNT; index += 1)
    {
        BusterX86GeneratedForm form = buster_x86_metadata_form_record(index);
        if (!buster_x86_metadata_validate_form_record(&form, index, result)) return false;
    }
    for (u32 index = 0; index < BUSTER_X86_GENERATED_COVERAGE_COUNT; index += 1)
    {
        BusterX86GeneratedCoverage coverage = buster_x86_metadata_coverage_record(index);
        if (!buster_x86_metadata_validate_coverage_record(&coverage, index, result)) return false;
        if (coverage.normalized_form_id != index)
        {
            return buster_x86_metadata_validation_fail(result, BUSTER_X86_METADATA_VALIDATION_COVERAGE_FORM_ID, index,
                                                       coverage.normalized_form_id);
        }
        BusterX86GeneratedForm form = buster_x86_metadata_form_record(coverage.normalized_form_id);
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
        count += buster_x86_metadata_form_record(index).coverage_class == BUSTER_X86_GENERATED_COVERAGE_NORMALIZED;
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
        BusterX86GeneratedCoverage coverage = buster_x86_metadata_coverage_record(index);
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
    return index < string.length ? (u8)buster_x86_metadata_pool_byte((u64)string.offset + index) : 0;
}

bool buster_x86_metadata_form(u32 form_id, BusterX86MetadataForm* result)
{
    if (!result || form_id >= BUSTER_X86_GENERATED_FORM_COUNT) return false;
    if (!buster_x86_metadata_form_record_valid(form_id)) return false;
    buster_x86_metadata_normalized_form(form_id, result);
    return true;
}

bool buster_x86_metadata_form_key(u32 form_id, BusterX86MetadataFormKey* result)
{
    BusterX86MetadataForm form = {0};
    if (!result || !buster_x86_metadata_form(form_id, &form)) return false;
    *result = (BusterX86MetadataFormKey){.form_id = form_id, .stable_hash = form.stable_hash};
    return true;
}

bool buster_x86_metadata_form_key_from_id(u32 form_id, BusterX86MetadataFormKey* result)
{
    return buster_x86_metadata_form_key(form_id, result);
}

bool buster_x86_metadata_form_key_valid(BusterX86MetadataFormKey key)
{
    BusterX86MetadataForm form = {0};
    return key.stable_hash != 0 && buster_x86_metadata_form(key.form_id, &form) && form.stable_hash == key.stable_hash;
}

bool buster_x86_metadata_form_key_from_stable_hash(u64 stable_hash, BusterX86MetadataFormKey* result)
{
    if (!result || !stable_hash) return false;
    BusterX86MetadataCandidateRange candidates = buster_x86_metadata_lookup_form_hash(stable_hash);
    if (candidates.count != 1) return false;
    u32 form_id = 0;
    if (!buster_x86_metadata_candidate_at(candidates, 0, &form_id)) return false;
    return buster_x86_metadata_form_key(form_id, result) && result->stable_hash == stable_hash;
}

bool buster_x86_metadata_lookup_form_key(BusterX86MetadataFormKey key, BusterX86MetadataForm* result)
{
    BusterX86MetadataForm form = {0};
    if (!result || !key.stable_hash || !buster_x86_metadata_form(key.form_id, &form) || form.stable_hash != key.stable_hash) return false;
    *result = form;
    return true;
}

bool buster_x86_metadata_form_is_moffs(u32 form_id)
{
    BusterX86MetadataForm form = {0};
    BusterX86MetadataPatternSemantics pattern = {0};
    return buster_x86_metadata_form(form_id, &form) && buster_x86_metadata_emit_parse_pattern(form, &pattern) &&
           buster_x86_metadata_emit_is_moffs(form, pattern);
}

bool buster_x86_metadata_form_requires_dfv(u32 form_id)
{
    BusterX86MetadataForm form = {0};
    return buster_x86_metadata_form(form_id, &form) && buster_x86_metadata_form_iform_requires_dfv(form);
}

bool buster_x86_metadata_operand(u32 form_id, u32 operand_index, BusterX86MetadataOperand* result)
{
    if (!result || form_id >= BUSTER_X86_GENERATED_FORM_COUNT) return false;
    BusterX86GeneratedForm form = buster_x86_metadata_form_record(form_id);
    if (!buster_x86_metadata_form_record_valid(form_id) || operand_index >= form.operand_count ||
        form.operand_first > BUSTER_X86_GENERATED_OPERAND_COUNT || operand_index >= BUSTER_X86_GENERATED_OPERAND_COUNT - form.operand_first)
    {
        return false;
    }
    u32 record_index = form.operand_first + operand_index;
    u16 owner_plus_one = (u16)(form_id + 1);
    if (buster_x86_metadata_operand_views_ready &&
        buster_x86_metadata_operand_view_owner_plus_one[record_index] == owner_plus_one)
    {
        *result = buster_x86_metadata_operand_views[record_index];
        return true;
    }
    BusterX86GeneratedOperand operand = buster_x86_metadata_operand_record(record_index);
    bool valid = buster_x86_metadata_validate_operand_record(&operand, record_index, 0);
    BusterX86MetadataOperand view = {0};
    if (valid)
    {
        u8 physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE;
        u16 physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN;
        buster_x86_metadata_physical_operand_view(form, operand, &physical_class, &physical_width_flags);
        view = (BusterX86MetadataOperand){
            .atom = buster_x86_metadata_string_unchecked(operand.atom_offset), .width = buster_x86_metadata_string_unchecked(operand.width_offset),
            .slot = operand.slot, .visible = operand.visible, .kind = operand.kind, .access = operand.access, .field_source = operand.field_source,
            .physical_class = physical_class, .physical_width_flags = physical_width_flags,
        };
    }
    // Filled on the serial prewarm pass, which walks every form and operand,
    // and read without synchronization afterwards.
    if (!buster_x86_metadata_operand_views_ready)
    {
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        buster_x86_metadata_operand_views[record_index] = view;
        buster_x86_metadata_operand_view_owner_plus_one[record_index] =
            valid ? owner_plus_one : BUSTER_X86_METADATA_OPERAND_VIEW_UNKNOWN;
    }
    if (!valid) return false;
    *result = view;
    return true;
}

bool buster_x86_metadata_exact_plan_prepare(BusterX86MetadataFormKey key, BusterX86MetadataExactPlan* result)
{
    if (!result || !buster_x86_metadata_prewarmed || !key.stable_hash || key.form_id >= BUSTER_X86_GENERATED_FORM_COUNT) return false;
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    u16 slot_plus_one = buster_x86_metadata_exact_plan_slots[key.form_id];
    if (slot_plus_one)
    {
        if (!buster_x86_metadata_normalized_forms_cached[key.form_id] ||
            !buster_x86_metadata_pattern_semantics_cached[key.form_id] ||
            !buster_x86_metadata_pattern_semantics_results[key.form_id])
            return false;
        if (slot_plus_one > buster_x86_metadata_exact_plan_count ||
            slot_plus_one > BUSTER_X86_METADATA_EXACT_PLAN_CAPACITY)
            return false;
        BusterX86MetadataExactPlanRecord const* existing = &buster_x86_metadata_exact_plan_records[slot_plus_one - 1];
        if (!existing->ready || existing->identity.form_id != key.form_id || existing->identity.stable_hash != key.stable_hash ||
            !existing->form || !existing->pattern || existing->operand_count > BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY ||
            existing->operand_count != existing->form->operand_count ||
            existing->form->id != key.form_id || existing->form->stable_hash != key.stable_hash)
            return false;
        *result = existing->identity;
        return true;
    }
    if (buster_x86_metadata_exact_plan_count >= BUSTER_X86_METADATA_EXACT_PLAN_CAPACITY) return false;
    BusterX86MetadataForm form = {0};
    if (!buster_x86_metadata_lookup_form_key(key, &form) || form.operand_count > BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY)
        return false;
    BusterX86MetadataPatternSemantics pattern = {0};
    if (!buster_x86_metadata_emit_parse_pattern(form, &pattern)) return false;

    u16 slot = buster_x86_metadata_exact_plan_count;
    BusterX86MetadataExactPlanRecord* plan = &buster_x86_metadata_exact_plan_records[slot];
    *plan = (BusterX86MetadataExactPlanRecord){0};
    plan->identity = (BusterX86MetadataExactPlan){.form_id = key.form_id, .stable_hash = key.stable_hash};
    plan->form = &buster_x86_metadata_normalized_forms[key.form_id];
    plan->pattern = &buster_x86_metadata_pattern_semantics_cache[key.form_id];
    plan->operands = &buster_x86_metadata_operand_views[form.operand_first];
    plan->operand_count = form.operand_count;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (!buster_x86_metadata_operand(key.form_id, operand_index, &operand))
        {
            *plan = (BusterX86MetadataExactPlanRecord){0};
            return false;
        }
        plan->effective_field_sources[operand_index] =
            buster_x86_metadata_emit_effective_field_source_pattern(operand, pattern);
    }
    plan->pattern_control_blocker = buster_x86_metadata_emit_pattern_control_blocker(form, pattern);
    plan->moffs_form = buster_x86_metadata_emit_is_moffs(form, pattern);
    plan->maskmov_form = buster_x86_metadata_emit_is_maskmov(form, pattern);
    plan->requires_dfv = buster_x86_metadata_form_iform_requires_dfv(form);
    plan->loop_form = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOP")) ||
                      buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPE")) ||
                      buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPNE"));
    plan->jecxz_form = buster_x86_metadata_string_input_equal(form.iclass.offset, S8("JECXZ"));
    plan->canonical_hidden_segment_override =
        pattern.has_segment_override && buster_x86_metadata_emit_canonical_hidden_segment_override(form, pattern);
    plan->canonical_notrack = pattern.has_cet_no_track && buster_x86_metadata_emit_canonical_cet_no_track(form, pattern);
    plan->dataxfer_category = form.category.length == 8 && buster_x86_metadata_emit_string_has(form.category, S8("DATAXFER"));
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = plan->operands[operand_index];
        u8 flags = 0;
        if (plan->moffs_form && buster_x86_metadata_emit_is_moffs_supplemental(form, pattern, metadata))
            flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MOFFS_SUPPLEMENTAL;
        if (plan->maskmov_form && buster_x86_metadata_emit_is_maskmov_supplemental(form, pattern, metadata))
            flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MASKMOV_SUPPLEMENTAL;
        if (buster_x86_metadata_emit_is_writemask_operand(metadata)) flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_WRITEMASK;
        if (buster_x86_metadata_emit_is_x87_operand(metadata)) flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_X87;
        if (buster_x86_metadata_emit_atom_contains(metadata.atom, S8("_SE"))) flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_SELECTOR;
        if ((metadata.kind == BUSTER_X86_METADATA_OPERAND_BASE && !metadata.visible) ||
            (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && !metadata.visible &&
             metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED))
            flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MOFFS_FIXED_ACCUMULATOR;
        if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && !metadata.visible &&
            metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED &&
            buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_BSR0")))
            flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_FIXED_BSR0;
        plan->operand_flags[operand_index] = flags;
    }
    // Most exact rows expose every metadata operand in the same order as the
    // physical query.  Record that shape once so emission can skip the
    // generic hidden-operand, writemask-default, and VSIB dispatch while
    // retaining the same operand matcher and fixed-register checks.
    plan->exact_bind_reg = UINT8_MAX;
    plan->exact_bind_rm = UINT8_MAX;
    plan->exact_bind_vvvv = UINT8_MAX;
    plan->exact_bind_mask = UINT8_MAX;
    plan->exact_bind_memory = UINT8_MAX;
    plan->exact_bind_immediate = UINT8_MAX;
    plan->exact_bind_relative = UINT8_MAX;
    plan->exact_bind_simple = form.operand_count <= BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY &&
                              !plan->moffs_form && !plan->maskmov_form &&
                              !(form.field_flags & BUSTER_X86_METADATA_FIELD_VSIB);
    for (u32 operand_index = 0; plan->exact_bind_simple && operand_index < form.operand_count; operand_index += 1)
    {
        plan->exact_bind_simple &= plan->operands[operand_index].visible != 0 &&
                                   plan->operand_flags[operand_index] == 0;
    }
    if (plan->exact_bind_simple)
    {
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand metadata = plan->operands[operand_index];
            u8 source = plan->effective_field_sources[operand_index];
            if (source == BUSTER_X86_METADATA_FIELD_SOURCE_REG && plan->exact_bind_reg == UINT8_MAX)
                plan->exact_bind_reg = (u8)operand_index;
            else if (source == BUSTER_X86_METADATA_FIELD_SOURCE_RM && plan->exact_bind_rm == UINT8_MAX)
                plan->exact_bind_rm = (u8)operand_index;
            else if (source == BUSTER_X86_METADATA_FIELD_SOURCE_VVVV && plan->exact_bind_vvvv == UINT8_MAX)
                plan->exact_bind_vvvv = (u8)operand_index;
            else if (source == BUSTER_X86_METADATA_FIELD_SOURCE_MASK && plan->exact_bind_mask == UINT8_MAX)
                plan->exact_bind_mask = (u8)operand_index;
            if ((metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY ||
                 metadata.kind == BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR) &&
                plan->exact_bind_memory == UINT8_MAX)
                plan->exact_bind_memory = (u8)operand_index;
            if (source == BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE && plan->exact_bind_immediate == UINT8_MAX)
                plan->exact_bind_immediate = (u8)operand_index;
            if (source == BUSTER_X86_METADATA_FIELD_SOURCE_RELATIVE && plan->exact_bind_relative == UINT8_MAX)
                plan->exact_bind_relative = (u8)operand_index;
        }
    }
    buster_x86_metadata_machine_fast_prepare(plan, form, pattern);
    // The token integrity helper is pure over the immutable plan identity and
    // the ordinary VALID policy.  Publish its base value with the rest of the
    // plan; machine_exact_plan_record() still performs every fail-closed
    // identity/shape/policy check before consuming it.
    plan->machine_exact_integrity = buster_x86_metadata_machine_exact_token_integrity(
        (u16)(slot + 1), BUSTER_X86_METADATA_MACHINE_EXACT_TOKEN_POLICY_VALID, plan->identity.form_id, plan->identity.stable_hash);
    // Publish this bit last.  The prewarm completion flag is published only
    // after this serial preparation returns, so workers see immutable plan
    // records and their pointed-to normalized/pattern caches.
    plan->ready = true;
    buster_x86_metadata_exact_plan_slots[key.form_id] = slot + 1;
    buster_x86_metadata_exact_plan_count = slot + 1;
    *result = plan->identity;
    return true;
}

// Decodes the tables and fills every demand-filled cache above them, on the
// calling thread. Walking the forms and their operands reaches every key a
// later query can ask for: normalized forms and pattern semantics per form,
// plus physical register views per distinct operand pair. Exact plans are
// deliberately sparse and are prepared explicitly by the machine prewarm
// caller after this routine returns, so ordinary metadata users do not pay a
// duplicate operand-store footprint. Nothing here is required for correctness
// on one thread -- the lazy paths do the same work on first use -- and
// everything here is required before a gang may query the module, because
// those paths write shared state that later reads see through plain loads.
void buster_x86_metadata_prewarm(void)
{
    if (buster_x86_metadata_prewarmed)
    {
        return;
    }
    // The demand-filled caches below are intentionally unsynchronized. Keep
    // this check on the first fill, before any decode/normalization work, so a
    // caller that forgot the serial prewarm contract is reported immediately.
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    buster_x86_metadata_decode_tables();
    bool form_operand_ranges_partition = buster_x86_metadata_form_operand_ranges_partition();
    for (u32 form_id = 0; form_id < BUSTER_X86_GENERATED_FORM_COUNT; form_id += 1)
    {
        BusterX86MetadataForm form = {0};
        if (!buster_x86_metadata_form(form_id, &form))
        {
            continue;
        }
        BusterX86MetadataPatternSemantics pattern = {0};
        bool pattern_valid = buster_x86_metadata_emit_parse_pattern(form, &pattern);
        BusterX86MetadataFormFacts facts = {0};
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand operand = {0};
            if (!buster_x86_metadata_operand(form_id, operand_index, &operand)) continue;
            BusterX86MetadataFormOperandFacts* operand_facts =
                buster_x86_metadata_form_operand_facts_for(form, operand_index);
            if (!operand_facts) continue;
            // Mirror the plan-less binding loop exactly: the pattern-aware
            // source replaces the declared one, and the effective source is
            // then read from the rewritten operand.
            if (pattern_valid) operand.field_source = buster_x86_metadata_emit_effective_field_source_pattern(operand, pattern);
            operand_facts->pattern_field_source = operand.field_source;
            operand_facts->effective_field_source = buster_x86_metadata_emit_effective_field_source(operand);
        }
        // The pattern-derived facts are only meaningful for a form whose
        // pattern parsed; emission rejects the rest before it reads them.
        if (pattern_valid)
        {
            facts.pattern_control_blocker = buster_x86_metadata_emit_pattern_control_blocker(form, pattern);
            if (buster_x86_metadata_emit_is_moffs(form, pattern)) facts.flags |= BUSTER_X86_METADATA_FORM_FACT_MOFFS;
            if (buster_x86_metadata_emit_is_maskmov(form, pattern)) facts.flags |= BUSTER_X86_METADATA_FORM_FACT_MASKMOV;
            if (buster_x86_metadata_emit_canonical_hidden_segment_override(form, pattern))
                facts.flags |= BUSTER_X86_METADATA_FORM_FACT_HIDDEN_SEGMENT_OVERRIDE;
            if (buster_x86_metadata_emit_canonical_cet_no_track(form, pattern))
                facts.flags |= BUSTER_X86_METADATA_FORM_FACT_NOTRACK;
        }
        if (buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOP")) ||
            buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPE")) ||
            buster_x86_metadata_string_input_equal(form.iclass.offset, S8("LOOPNE")))
            facts.flags |= BUSTER_X86_METADATA_FORM_FACT_LOOP;
        if (buster_x86_metadata_string_input_equal(form.iclass.offset, S8("JECXZ")))
            facts.flags |= BUSTER_X86_METADATA_FORM_FACT_JECXZ;
        if (buster_x86_metadata_form_iform_requires_dfv(form)) facts.flags |= BUSTER_X86_METADATA_FORM_FACT_REQUIRES_DFV;
        if (form.category.length == 8 && buster_x86_metadata_emit_string_has(form.category, S8("DATAXFER")))
            facts.flags |= BUSTER_X86_METADATA_FORM_FACT_DATAXFER;
        // Per-operand flags and the simple binding shape, derived exactly as
        // buster_x86_metadata_exact_plan_prepare derives them so a prepared
        // plan and this table cannot disagree about a row.
        bool moffs_form = (facts.flags & BUSTER_X86_METADATA_FORM_FACT_MOFFS) != 0;
        bool maskmov_form = (facts.flags & BUSTER_X86_METADATA_FORM_FACT_MASKMOV) != 0;
        bool bind_simple = pattern_valid && form.operand_count <= BUSTER_X86_METADATA_EXACT_PLAN_OPERAND_CAPACITY &&
                           !moffs_form && !maskmov_form && !(form.field_flags & BUSTER_X86_METADATA_FIELD_VSIB);
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand metadata = {0};
            if (!buster_x86_metadata_operand(form_id, operand_index, &metadata))
            {
                bind_simple = false;
                break;
            }
            u8 operand_flags = 0;
            if (moffs_form && buster_x86_metadata_emit_is_moffs_supplemental(form, pattern, metadata))
                operand_flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MOFFS_SUPPLEMENTAL;
            if (maskmov_form && buster_x86_metadata_emit_is_maskmov_supplemental(form, pattern, metadata))
                operand_flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MASKMOV_SUPPLEMENTAL;
            if (buster_x86_metadata_emit_is_writemask_operand(metadata)) operand_flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_WRITEMASK;
            if (buster_x86_metadata_emit_is_x87_operand(metadata)) operand_flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_X87;
            if (buster_x86_metadata_emit_atom_contains(metadata.atom, S8("_SE"))) operand_flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_SELECTOR;
            if ((metadata.kind == BUSTER_X86_METADATA_OPERAND_BASE && !metadata.visible) ||
                (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && !metadata.visible &&
                 metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED))
                operand_flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_MOFFS_FIXED_ACCUMULATOR;
            if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER && !metadata.visible &&
                metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED &&
                buster_x86_metadata_emit_atom_equal(metadata.atom, S8("XED_REG_BSR0")))
                operand_flags |= BUSTER_X86_METADATA_PLAN_OPERAND_FLAG_FIXED_BSR0;
            if (!metadata.visible || operand_flags != 0) bind_simple = false;
        }
        facts.operand_count = (u8)form.operand_count;
        facts.bind_reg = UINT8_MAX;
        facts.bind_rm = UINT8_MAX;
        facts.bind_vvvv = UINT8_MAX;
        facts.bind_mask = UINT8_MAX;
        facts.bind_memory = UINT8_MAX;
        facts.bind_immediate = UINT8_MAX;
        facts.bind_relative = UINT8_MAX;
        if (bind_simple)
        {
            facts.shape_flags |= BUSTER_X86_METADATA_FORM_FACT2_BIND_SIMPLE;
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
            {
                BusterX86MetadataOperand metadata = {0};
                if (!buster_x86_metadata_operand(form_id, operand_index, &metadata)) continue;
                BusterX86MetadataFormOperandFacts const* operand_facts =
                    buster_x86_metadata_form_operand_facts_for(form, operand_index);
                if (!operand_facts)
                {
                    break;
                }
                u8 source = operand_facts->pattern_field_source;
                if (source == BUSTER_X86_METADATA_FIELD_SOURCE_REG && facts.bind_reg == UINT8_MAX)
                    facts.bind_reg = (u8)operand_index;
                else if (source == BUSTER_X86_METADATA_FIELD_SOURCE_RM && facts.bind_rm == UINT8_MAX)
                    facts.bind_rm = (u8)operand_index;
                else if (source == BUSTER_X86_METADATA_FIELD_SOURCE_VVVV && facts.bind_vvvv == UINT8_MAX)
                    facts.bind_vvvv = (u8)operand_index;
                else if (source == BUSTER_X86_METADATA_FIELD_SOURCE_MASK && facts.bind_mask == UINT8_MAX)
                    facts.bind_mask = (u8)operand_index;
                if ((metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY ||
                     metadata.kind == BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR) &&
                    facts.bind_memory == UINT8_MAX)
                    facts.bind_memory = (u8)operand_index;
                if (source == BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE && facts.bind_immediate == UINT8_MAX)
                    facts.bind_immediate = (u8)operand_index;
                if (source == BUSTER_X86_METADATA_FIELD_SOURCE_RELATIVE && facts.bind_relative == UINT8_MAX)
                    facts.bind_relative = (u8)operand_index;
            }
        }
        buster_x86_metadata_form_facts[form_id] = facts;
    }
    // Publish only after decode, normalization, pattern parsing, and every
    // physical operand-view cache insertion have completed.
    buster_x86_metadata_operand_views_ready = true;
    buster_x86_metadata_form_facts_ready = form_operand_ranges_partition;
    buster_x86_metadata_prewarmed = true;
}

bool buster_x86_metadata_coverage(u32 coverage_id, BusterX86MetadataCoverage* result)
{
    if (!result || coverage_id >= BUSTER_X86_GENERATED_COVERAGE_COUNT) return false;
    BusterX86GeneratedCoverage coverage = buster_x86_metadata_coverage_record(coverage_id);
    if (!buster_x86_metadata_coverage_record_valid(coverage_id) || coverage.normalized_form_id != coverage_id) return false;
    BusterX86GeneratedForm form = buster_x86_metadata_form_record(coverage.normalized_form_id);
    if (!buster_x86_metadata_form_record_valid(coverage.normalized_form_id) || coverage.source_hash != form.stable_hash ||
        coverage.source_offset != form.source_offset || coverage.reason_id != form.reason_id || coverage.reason_offset != form.reason_offset ||
        coverage.coverage_class != form.coverage_class || coverage.encoder_family != form.encoder_family || coverage.test_class != form.test_class)
    {
        return false;
    }
    // The generated snapshot predates the XOPV importer correction and has
    // those XMAP8/9/A rows tagged as VEX.  Keep the raw snapshot integrity
    // checks above, then expose the same pattern-derived normalized family as
    // buster_x86_metadata_form() and the capability ledger.
    BusterX86MetadataForm normalized = {0};
    buster_x86_metadata_normalized_form(coverage.normalized_form_id, &normalized);
    *result = (BusterX86MetadataCoverage){
        .id = coverage_id, .source_hash = coverage.source_hash, .source = buster_x86_metadata_string_unchecked(coverage.source_offset),
        .normalized_form_id = coverage.normalized_form_id, .coverage_class = coverage.coverage_class,
        .encoder_family = normalized.encoder_family, .test_class = coverage.test_class, .reason_id = coverage.reason_id,
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

// Intel and AT&T spellings retain the historical x87 wait/no-wait aliases,
// while the generated snapshot publishes only the FNST*/FNSAVE forms.  Keep
// aliases at the metadata lookup boundary so every selector/emitter path
// shares one canonical form and byte sequence.
BUSTER_GLOBAL_LOCAL String8 buster_x86_metadata_mnemonic_alias_target(String8 mnemonic)
{
    if (buster_x86_metadata_input_string_equal(mnemonic, S8("FSTCW"))) return S8("FNSTCW");
    if (buster_x86_metadata_input_string_equal(mnemonic, S8("FSTENV"))) return S8("FNSTENV");
    if (buster_x86_metadata_input_string_equal(mnemonic, S8("FSAVE"))) return S8("FNSAVE");
    if (buster_x86_metadata_input_string_equal(mnemonic, S8("FSTSW"))) return S8("FNSTSW");
    return mnemonic;
}

BusterX86MetadataCandidateRange buster_x86_metadata_lookup_mnemonic(String8 mnemonic)
{
    return buster_x86_metadata_lookup_text(BUSTER_X86_METADATA_INDEX_MNEMONIC,
                                           buster_x86_metadata_mnemonic_alias_target(mnemonic));
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
        id = buster_x86_metadata_form_hash_candidate(candidates.first + position);
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
    u32 id = buster_x86_metadata_coverage_hash_candidate(candidates.first + position);
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
        BusterX86GeneratedForm form = buster_x86_metadata_form_record(id);
        if (buster_x86_metadata_form_matches_filter(id, form, iterator->filter))
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

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_input_string_equal(String8 left, String8 right)
{
    if (left.length != right.length) return false;
    for (u32 index = 0; index < left.length; index += 1)
    {
        if (buster_x86_metadata_lowercase_character(left.pointer[index]) !=
            buster_x86_metadata_lowercase_character(right.pointer[index]))
            return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_pool_string_has_token(u32 offset, String8 token)
{
    if (!token.length) return false;
    u32 length = 0;
    if (!buster_x86_metadata_string_offset_terminated(offset, &length)) return false;
    String8 span = buster_x86_metadata_pool_span(offset, length);
    if (span.length != length || span.length < token.length) return false;
    u32 cursor = 0;
    while (cursor < span.length)
    {
        while (cursor < span.length && buster_x86_metadata_is_space(span.pointer[cursor])) cursor += 1;
        if (cursor == span.length) break;
        u32 end = cursor;
        while (end < span.length && !buster_x86_metadata_is_space(span.pointer[end])) end += 1;
        if (end - cursor == token.length)
        {
            bool equal = true;
            for (u32 index = 0; index < token.length; index += 1)
            {
                equal &= buster_x86_metadata_lowercase_character(span.pointer[cursor + index]) ==
                         buster_x86_metadata_lowercase_character(token.pointer[index]);
            }
            if (equal) return true;
        }
        cursor = end;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_is_fixed_not16_nop(BusterX86MetadataForm form)
{
    return form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
           form.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY &&
           buster_x86_metadata_string_input_equal(form.extension.offset, S8("BASE")) &&
           (buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("I86")) ||
            buster_x86_metadata_string_input_equal(form.isa_set.offset, S8("FAT_NOP"))) &&
           buster_x86_metadata_string_input_equal(form.category.offset, S8("WIDENOP")) &&
           buster_x86_metadata_string_input_equal(form.attributes.offset, S8("NOP")) &&
           form.operand_count == 0 && form.prefix_kind == BUSTER_X86_METADATA_PREFIX_LEGACY &&
           (form.map == BUSTER_X86_METADATA_MAP_LEGACY || form.map == BUSTER_X86_METADATA_MAP_0F) &&
           form.field_flags == 0 && form.decorator_flags == 0 &&
           form.apx_flags == 0 && form.amx_flags == 0 && form.mode_flags == 0 && form.displacement_width == 0 &&
           form.displacement_scale == 0 && form.immediate_width == 0 && form.immediate_signed == 0 &&
           form.relocation_base == 0 && form.tuple_kind == BUSTER_X86_METADATA_TUPLE_NONE && form.fixed_byte_count != 0 &&
           (form.mandatory_prefix == 0 || form.mandatory_prefix == 0x66);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_generated_form_is_fixed_not16_nop(BusterX86GeneratedForm form)
{
    return form.coverage_class == BUSTER_X86_GENERATED_COVERAGE_NORMALIZED &&
           form.encoder_family == BUSTER_X86_GENERATED_ENCODER_LEGACY &&
           buster_x86_metadata_string_input_equal(form.extension_offset, S8("BASE")) &&
           (buster_x86_metadata_string_input_equal(form.isa_set_offset, S8("I86")) ||
            buster_x86_metadata_string_input_equal(form.isa_set_offset, S8("FAT_NOP"))) &&
           buster_x86_metadata_string_input_equal(form.category_offset, S8("WIDENOP")) &&
           buster_x86_metadata_string_input_equal(form.attributes_offset, S8("NOP")) && form.operand_count == 0 &&
           form.prefix_kind == BUSTER_X86_GENERATED_PREFIX_LEGACY &&
           (form.map == BUSTER_X86_GENERATED_MAP_LEGACY || form.map == BUSTER_X86_GENERATED_MAP_0F) &&
           form.field_flags == 0 && form.decorator_flags == 0 && form.apx_flags == 0 && form.amx_flags == 0 &&
           form.mode_flags == 0 && form.displacement_width == 0 && form.displacement_scale == 0 && form.immediate_width == 0 &&
           form.immediate_signed == 0 && form.relocation_base == 0 && form.tuple_kind == BUSTER_X86_GENERATED_TUPLE_NONE &&
           form.fixed_byte_count != 0 && (form.mandatory_prefix == 0 || form.mandatory_prefix == 0x66) &&
           buster_x86_metadata_pool_string_has_token(form.pattern_offset, S8("not16"));
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_query_string_valid(String8 string)
{
    return string.length <= UINT32_MAX && (!string.length || string.pointer != 0);
}

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_string_input_equal(u32 offset, String8 input)
{
    u32 length = 0;
    if (!buster_x86_metadata_string_offset_terminated(offset, &length) || length != input.length) return false;
    String8 pooled = buster_x86_metadata_pool_span(offset, length);
    if (pooled.length != length) return false;
    for (u32 index = 0; index < length; index += 1)
    {
        char8 left = pooled.pointer[index];
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
        char8 left = buster_x86_metadata_pool_byte((u64)offset + index);
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
        char8 left = buster_x86_metadata_pool_byte((u64)offset + first + index);
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
            char8 left = buster_x86_metadata_pool_byte((u64)offset + first + index);
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

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_feature_input_allows_apx(BusterX86MetadataFeatureInput input)
{
    // feature_input_contains_literal deliberately treats '*' as the explicit
    // caller-authorized feature bypass.  Keep this check canonical and
    // case-insensitive so APX gating is identical to the generated ISA
    // feature matcher.
    // Public callers may name the generated APX_F family directly (the
    // spelling used by x86_64 fixed-operation helpers), while the feature
    // matcher itself uses the canonical `apx` capability token.  Treat the
    // family alias as the same APX authorization for legacy REX2 promotion;
    // APX_F_N3 rows still enforce their secondary conjunction in the normal
    // form-feature matcher.
    return buster_x86_metadata_feature_input_contains_literal(input, S8("apx")) ||
           buster_x86_metadata_feature_input_contains_literal(input, S8("apx_f"));
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
    // The ladder below compares one pooled requirement spelling against a long
    // list of canonical names.  Going through the pool offset for each
    // comparison repeats the terminator lookup, the bounds check and the pool
    // decode entry every time, so resolve the span once and compare against
    // that.  The shadowing macro keeps each comparison spelled as before; the
    // underlying comparison is the same case-insensitive one.
    u32 canonical_length = 0;
    if (!buster_x86_metadata_string_offset_terminated(offset, &canonical_length)) return false;
    String8 canonical_name = buster_x86_metadata_pool_span(offset, canonical_length);
    if (canonical_name.length != canonical_length) return false;
#define buster_x86_metadata_pool_string_equal_literal(unused_offset, literal) \
    buster_x86_metadata_input_string_equal(canonical_name, (literal))
    // These imported names have an explicit canonical spelling.  Resolve
    // them before the raw fallback so a generated conjunction such as
    // APX_F_VMX cannot be authorized by passing that one token alone.
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AMD_INVLPGB")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("invlpgb"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("ENQCMD")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("enqcmd"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("FRED")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("fred"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("HRESET")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("hreset"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("INVPCID")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("invpcid"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("KEYLOCKER")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("keylocker"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("LKGS")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("lkgs"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("MONITOR")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("monitor"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("MSRLIST")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("msrlist"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("MSR_IMM")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("msr-imm"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("PBNDKB")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("pbndkb"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("PCONFIG")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("pconfig"), S8(""), S8(""), S8(""));
    // The generated SGX_ENCLV ISA family is the same public SGX feature.
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SGX")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("SGX_ENCLV")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sgx"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SNP")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("snp"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SMAP")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("smap"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SVM")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("svm"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("TDX")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("tdx"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("VTX")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("vmx"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("WBNOINVD")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("wbnoinvd"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("WRMSRNS")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("wrmsrns"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("XSAVE")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("xsave"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("XSAVES")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("xsaves"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("F16C")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("f16c"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("FMA")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("fma"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SSSE3")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("SSSE3MMX")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("ssse3"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SSE4")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sse4.1"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SSE42")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sse4.2"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("BMI2")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("bmi2"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("ADX")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("adx"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("MOVBE")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("movbe"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("RDRAND")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("rdrand"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("RDSEED")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("rdseed"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SHA")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sha"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SHA512")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sha512"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SM3")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sm3"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SM4")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sm4"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("WAITPKG")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("waitpkg"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("PKU")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("pku"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("PTWRITE")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("ptwrite"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SERIALIZE")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("serialize"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("CLFLUSHOPT")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("clflushopt"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("CLWB")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("clwb"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("RDWRFSGS")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("fsgsbase"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("RTM")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("rtm"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("TSX_LDTRK")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("tsxldtrk"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("UINTR")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("uintr"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("PREFETCHWT1")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("prefetchwt1"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("ACE_1")))
    {
        // The driver exposes the canonical CLI spelling, while direct
        // metadata clients historically supplied the generated ISA-set name.
        // Accept both exact spellings (case-insensitively), but do not let the
        // category (AMX_TILE) or extension (ACE) authorize this gate.
        return buster_x86_metadata_feature_input_contains_literal(input, S8("ace-1")) ||
               buster_x86_metadata_feature_input_contains_literal(input, S8("ACE_1"));
    }
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("CMPXCHG16B")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("cx16"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("SSE3X87")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sse3"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("FCMOV")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("FCOMI")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("X87")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("PENTIUMMMX")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("SSE2MMX")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("sse2"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("LONGMODE"))) return true;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("APX_F_CET")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("apx"), S8("shstk"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("APX_F_ENQCMD")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("apx"), S8("enqcmd"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("APX_F_INVPCID")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("apx"), S8("invpcid"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("APX_F_MSR_IMM")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("apx"), S8("msr-imm"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("APX_F_MOVDIR64B")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("apx"), S8("movdir64b"), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("APX_F_VMX")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("apx"), S8("vmx"), S8(""), S8(""));

    // Existing metadata clients may still pass an exact generated spelling
    // for predicates whose canonical mapping is not part of this privileged
    // feature seam.  Keep that compatibility fallback after the explicit
    // conjunctions above.
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
        else if (buster_x86_metadata_pool_string_contains(offset, S8("ADX"))) secondary = S8("adx");
        else if (buster_x86_metadata_pool_string_contains(offset, S8("MOVBE"))) secondary = S8("movbe");
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
    // AVX10.2's newly imported FP8/minmax/saturating-convert families use
    // AVX512-style ISA names even though AVX10.2 is the architectural gate.
    // The 512-bit variants additionally require the AVX10-512 width marker;
    // 128/256-bit and scalar variants deliberately do not.  Keep this rule
    // keyed to the generated ISA family and width suffix so it applies to
    // every matching form without a mnemonic or form inventory.
    if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_FP8_CONVERT_")) ||
        buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_MINMAX_")) ||
        buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_SAT_CVT_")) ||
        buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_SAT_CVT_DS_")))
    {
        return buster_x86_metadata_feature_input_contains_all(
            input, S8("avx10.2"), S8(""),
            buster_x86_metadata_pool_string_has_suffix(offset, S8("512")) ? S8("avx10-512") : S8(""), S8(""));
    }

    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX2")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("avx2"), S8(""), S8(""), S8(""));
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("AVX2GATHER")))
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
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("ICACHE_PREFETCH")))
        return buster_x86_metadata_feature_input_contains_all(input, S8("prefetchi"), S8(""), S8(""), S8(""));

    // AVX-512 forms are width-sensitive.  Require the corresponding
    // canonical subfeature, and require AVX512VL for the 128/256 encodings.
    String8 avx512_feature = S8("");
    if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512F_"))) avx512_feature = S8("avx512f");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512BW_"))) avx512_feature = S8("avx512bw");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512CD_"))) avx512_feature = S8("avx512cd");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512DQ_"))) avx512_feature = S8("avx512dq");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512ER_"))) avx512_feature = S8("avx512er");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512PF_"))) avx512_feature = S8("avx512pf");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_IFMA_"))) avx512_feature = S8("avx512ifma");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_VBMI_"))) avx512_feature = S8("avx512vbmi");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_VBMI2_"))) avx512_feature = S8("avx512vbmi2");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_VNNI_"))) avx512_feature = S8("avx512vnni");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_VNNI_INT8_"))) avx512_feature = S8("avx-vnni-int8");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_VNNI_INT16_"))) avx512_feature = S8("avx-vnni-int16");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_BITALG_"))) avx512_feature = S8("avx512bitalg");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_VPOPCNTDQ_"))) avx512_feature = S8("avx512vpopcntdq");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_VP2INTERSECT_")))
        avx512_feature = S8("avx512vp2intersect");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_4FMAPS_"))) avx512_feature = S8("avx5124fmaps");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_4VNNIW_"))) avx512_feature = S8("avx5124vnniw");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_GFNI_"))) avx512_feature = S8("gfni");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_VAES_"))) avx512_feature = S8("vaes");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_VPCLMULQDQ_"))) avx512_feature = S8("vpclmulqdq");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_BF16_"))) avx512_feature = S8("avx512bf16");
    else if (buster_x86_metadata_pool_string_has_prefix(offset, S8("AVX512_FP16_"))) avx512_feature = S8("avx512fp16");
    if (avx512_feature.length)
        return buster_x86_metadata_feature_input_contains_avx512_width(offset, input, avx512_feature);
    return false;
#undef buster_x86_metadata_pool_string_equal_literal
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
    // The generated CET ISA family contains both indirect-branch tracking and
    // shadow-stack instructions.  Keep those capabilities separate: ENDBR32/
    // ENDBR64 use `ibt`, while every other CET row uses `shstk`.
    if (buster_x86_metadata_string_input_equal(requirement, S8("CET")))
    {
        if (buster_x86_metadata_feature_input_contains_literal(input, S8("*"))) return true;
        bool endbr = buster_x86_metadata_string_input_equal(form.iclass_offset, S8("ENDBR32")) ||
                     buster_x86_metadata_string_input_equal(form.iclass_offset, S8("ENDBR64"));
        return buster_x86_metadata_feature_input_contains_literal(input, endbr ? S8("ibt") : S8("shstk"));
    }
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
            if (buster_x86_metadata_pool_byte((u64)offset + index) == '_')
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
        while (candidate_end && buster_x86_metadata_pool_byte((u64)offset + candidate_end - 1) >= '0' &&
               buster_x86_metadata_pool_byte((u64)offset + candidate_end - 1) <= '9')
        {
            candidate_end -= 1;
        }
        while (input_end && input.pointer[input_end - 1] >= '0' && input.pointer[input_end - 1] <= '9') input_end -= 1;
    }
    if (candidate_end != input_end) return false;
    for (u32 index = 0; index < candidate_end; index += 1)
    {
        char8 left = buster_x86_metadata_pool_byte((u64)offset + index);
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
        buster_x86_metadata_pool_string_equal_literal(offset, S8("mem16int")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("bf16")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("zi16")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("z2i16")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("z2u16")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_16;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("d")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("i32")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("u32")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("f32")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("mem32int")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("zi32")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("zd")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_32;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("q")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("i64")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("u64")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("f64")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("zi64")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("mem64int")) ||
        buster_x86_metadata_pool_string_equal_literal(offset, S8("m64int")) ||
        // XED uses x87-specific real-memory width tokens rather than the
        // generic q/d/b spellings.  Keep those tokens width-aware so FLD
        // source selection cannot accept the first ModRM alias (m32real)
        // for a qword (m64real) or extended-real (mem80real) query.
        buster_x86_metadata_pool_string_equal_literal(offset, S8("m64real")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("mem32real")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_32;
    if (buster_x86_metadata_pool_string_equal_literal(offset, S8("mem80real")))
        return BUSTER_X86_METADATA_PHYSICAL_WIDTH_80;
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

// The register view is a pure function of the operand's atom and width
// offsets into the immutable decoded pool (variable-width GPR atoms read the
// width token), but the resolver below answers it with ~60 case-insensitive
// literal probes. Memoize per (atom, width) pair: the distinct pairs number a
// few hundred, and coverage queries ask for the same ones per operand of
// every form.
enum
{
    BUSTER_X86_METADATA_PHYSICAL_VIEW_SLOT_COUNT = 2048,
};

typedef struct BusterX86MetadataPhysicalViewSlot BusterX86MetadataPhysicalViewSlot;
struct BusterX86MetadataPhysicalViewSlot
{
    u32 atom_offset;
    u32 width_offset;
    u16 width_flags;
    u8 physical_class;
    u8 used;
};

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalViewSlot buster_x86_metadata_physical_view_slots[BUSTER_X86_METADATA_PHYSICAL_VIEW_SLOT_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_x86_metadata_physical_view_fill;

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_physical_register_view_resolve(BusterX86GeneratedOperand operand, u8* physical_class,
                                                                              u16* physical_width_flags);

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_physical_register_view(BusterX86GeneratedOperand operand, u8* physical_class,
                                                                      u16* physical_width_flags)
{
    u32 atom_offset = operand.atom_offset;
    u32 width_offset = operand.width_offset;
    u32 mask = BUSTER_X86_METADATA_PHYSICAL_VIEW_SLOT_COUNT - 1;
    u32 slot_index = ((atom_offset * 2654435761u) ^ (width_offset * 40503u)) & mask;
    BusterX86MetadataPhysicalViewSlot* slot = buster_x86_metadata_physical_view_slots + slot_index;
    while (slot->used)
    {
        if (slot->atom_offset == atom_offset && slot->width_offset == width_offset)
        {
            *physical_class = slot->physical_class;
            *physical_width_flags = slot->width_flags;
            return;
        }
        slot_index = (slot_index + 1) & mask;
        slot = buster_x86_metadata_physical_view_slots + slot_index;
    }
    buster_x86_metadata_physical_register_view_resolve(operand, physical_class, physical_width_flags);
    if (buster_x86_metadata_physical_view_fill < BUSTER_X86_METADATA_PHYSICAL_VIEW_SLOT_COUNT / 2)
    {
        // Only the insert needs the process to be serial: a miss that does not
        // store resolves into the caller's own outputs and races with nothing.
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        buster_x86_metadata_physical_view_fill += 1;
        slot->used = 1;
        slot->atom_offset = atom_offset;
        slot->width_offset = width_offset;
        slot->physical_class = *physical_class;
        slot->width_flags = *physical_width_flags;
    }
}

BUSTER_GLOBAL_LOCAL void buster_x86_metadata_physical_register_view_resolve(BusterX86GeneratedOperand operand, u8* physical_class,
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
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("VGPR32")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_32;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("VGPRy")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 | BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
    }
    else if (buster_x86_metadata_pool_string_has_prefix(operand.atom_offset, S8("VGPR64")))
    {
        *physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR;
        *physical_width_flags = BUSTER_X86_METADATA_PHYSICAL_WIDTH_64;
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
        BusterX86GeneratedOperand operand = buster_x86_metadata_operand_record(form.operand_first + operand_index);
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

BUSTER_GLOBAL_LOCAL bool buster_x86_metadata_form_address_size_matches(BusterX86GeneratedForm form, u8 address_size)
{
    if (address_size == BUSTER_X86_METADATA_ADDRESS_SIZE_ANY) return true;
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

BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_form_declared_execution_mode(BusterX86GeneratedForm form, bool legacy_mode_cohort)
{
    u16 mode_bits = form.mode_flags &
                    (BUSTER_X86_GENERATED_MODE_16 | BUSTER_X86_GENERATED_MODE_32 | BUSTER_X86_GENERATED_MODE_64 |
                     BUSTER_X86_GENERATED_MODE_NOT64);
    // Legacy REP rows and the exact normalized MODE16/MODE32 residual cohort
    // are audited in their declared execution mode. Multi-mode rows retain
    // the ordinary 64-bit canonical query; NOT64 rows are admitted only
    // through the explicit include_not64/ANY policy below.
    if (legacy_mode_cohort && (mode_bits & BUSTER_X86_GENERATED_MODE_16) &&
        !(mode_bits & (BUSTER_X86_GENERATED_MODE_32 | BUSTER_X86_GENERATED_MODE_64 | BUSTER_X86_GENERATED_MODE_NOT64)))
        return BUSTER_X86_METADATA_EXECUTION_MODE_16;
    if (legacy_mode_cohort && (mode_bits & BUSTER_X86_GENERATED_MODE_32) &&
        !(mode_bits & (BUSTER_X86_GENERATED_MODE_16 | BUSTER_X86_GENERATED_MODE_64 | BUSTER_X86_GENERATED_MODE_NOT64)))
        return BUSTER_X86_METADATA_EXECUTION_MODE_32;
    return BUSTER_X86_METADATA_EXECUTION_MODE_64;
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
    // Preserve the REP-prefix execution-mode contract: non-64 rows are
    // rejected from a default 64-bit query, and include_not64 only admits
    // those rows through an ANY query (never by broadening mode64).
    bool non64_only = explicit_not64 || (has_explicit_mode && !has_64);
    bool fixed_not16 = buster_x86_metadata_generated_form_is_fixed_not16_nop(form);
    // `not16` is satisfied only by an explicitly selected 32- or 64-bit
    // execution mode.  ANY is an inspection mode, not evidence that the
    // instruction is legal outside 16-bit execution.
    if (fixed_not16 && query.execution_mode != BUSTER_X86_METADATA_EXECUTION_MODE_64 &&
        query.execution_mode != BUSTER_X86_METADATA_EXECUTION_MODE_32)
        return false;
    if (explicit_not64 && !query.include_not64) return false;
    if (query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_64)
    {
        if (explicit_not64 || (has_explicit_mode && !has_64)) return false;
    }
    else if (query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_ANY)
    {
        if (non64_only && !query.include_not64) return false;
    }
    else if (query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_16)
    {
        if (has_explicit_mode && !(mode_bits & BUSTER_X86_GENERATED_MODE_16)) return false;
    }
    else if (query.execution_mode == BUSTER_X86_METADATA_EXECUTION_MODE_32)
    {
        if (has_explicit_mode && !(mode_bits & BUSTER_X86_GENERATED_MODE_32)) return false;
    }
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
        BusterX86GeneratedForm form = buster_x86_metadata_form_record(form_id);
        if (!buster_x86_metadata_form_record_valid(form_id))
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
        if (!buster_x86_metadata_form_address_size_matches(form, query.address_size) ||
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
        BusterX86GeneratedForm form = buster_x86_metadata_form_record(patch.index);
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
        BusterX86GeneratedOperand operand = buster_x86_metadata_operand_record(patch.index);
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
    BusterX86GeneratedCoverage coverage = buster_x86_metadata_coverage_record(patch.index);
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
    BusterX86GeneratedForm form = buster_x86_metadata_form_record(coverage.normalized_form_id);
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

bool buster_x86_metadata_test_eamode_alias_forms(u32 first_form_id, u32 second_form_id)
{
    BusterX86MetadataForm first = {0};
    BusterX86MetadataForm second = {0};
    return buster_x86_metadata_form(first_form_id, &first) && buster_x86_metadata_form(second_form_id, &second) &&
           buster_x86_metadata_eamode_alias_forms(first, second);
}

bool buster_x86_metadata_test_fixed_bsrinit_no_zeroing(void)
{
    BusterX86MetadataForm form = {0};
    BusterX86MetadataPatternSemantics pattern = {0};
    if (!buster_x86_metadata_form(30, &form) || !buster_x86_metadata_emit_parse_pattern(form, &pattern) ||
        !buster_x86_metadata_emit_fixed_bsrinit_no_zeroing(form, pattern))
        return false;
    BusterX86MetadataPatternSemantics bcrc = pattern;
    BusterX86MetadataPatternSemantics ubit = pattern;
    BusterX86MetadataForm non_bsrinit_form = form;
    BusterX86MetadataForm other = {0};
    if (!buster_x86_metadata_form(31, &other)) return false;
    bcrc.has_bcrc = 1;
    ubit.has_ubit = 1;
    non_bsrinit_form.iclass = other.iclass;
    return !buster_x86_metadata_emit_fixed_bsrinit_no_zeroing(form, bcrc) &&
           !buster_x86_metadata_emit_fixed_bsrinit_no_zeroing(form, ubit) &&
           !buster_x86_metadata_emit_fixed_bsrinit_no_zeroing(non_bsrinit_form, pattern);
}

bool buster_x86_metadata_test_feature_available(u32 form_id, String8 const* names, u32 count)
{
    if ((count && !names) || form_id >= BUSTER_X86_GENERATED_FORM_COUNT) return false;
    BusterX86GeneratedForm form = buster_x86_metadata_form_record(form_id);
    return buster_x86_metadata_form_record_valid(form_id) &&
           buster_x86_metadata_form_feature_available(form, (BusterX86MetadataFeatureInput){.names = names, .count = count});
}

bool buster_x86_metadata_test_standalone_sae_pattern(u32 form_id)
{
    BusterX86MetadataForm form = {0};
    return buster_x86_metadata_form(form_id, &form) && buster_x86_metadata_form_standalone_sae_capable(form);
}

bool buster_x86_metadata_test_machine_fast_plan(u32 form_id)
{
    if (form_id >= BUSTER_X86_GENERATED_FORM_COUNT || !buster_x86_metadata_prewarmed) return false;
    u16 slot_plus_one = buster_x86_metadata_exact_plan_slots[form_id];
    if (!slot_plus_one || slot_plus_one > buster_x86_metadata_exact_plan_count) return false;
    BusterX86MetadataExactPlanRecord const* plan = &buster_x86_metadata_exact_plan_records[slot_plus_one - 1];
    return plan->ready && (plan->machine_fast_kind == BUSTER_X86_METADATA_MACHINE_FAST_SCALAR ||
                          plan->machine_fast_kind == BUSTER_X86_METADATA_MACHINE_FAST_TEMPLATE);
}

u32 buster_x86_metadata_test_exact_plan_count(void)
{
    return buster_x86_metadata_prewarmed ? buster_x86_metadata_exact_plan_count : 0;
}
#endif
