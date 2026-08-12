#include <buster/tests/compiler/assembly/aarch64_memory_semantics_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_memory_semantics.h>

static bool
a64_memory_test_find_canonical(u64 digest, BusterAarch64CanonicalFormInfo* result)
{
    if (!result) { return false;
}
    for (u32 index = 0; index < buster_aarch64_canonical_form_count(); index += 1)
    {
        BusterAarch64CanonicalFormInfo form = {0};
        if (buster_aarch64_canonical_form(index, &form) && form.arm_row_digest == digest)
        {
            *result = form;
            return true;
        }
    }
    return false;
}

typedef struct A64MemoryAuditCounters A64MemoryAuditCounters;
struct A64MemoryAuditCounters
{
    u32 rows;
    u32 source_rows;
    u32 seed_rows;
    u32 representative_ok;
    u32 representative_range;
    u32 representative_reserved;
    u32 representative_target_mismatch;
    u32 representative_other;
    u32 generated_words;
    u32 legal_candidates;
    u32 exercised_rows;
};

static u32
a64_memory_audit_mix(u32 row_index, u32 field_index, u32 salt)
{
    u32 value = UINT32_C(0x9e3779b9) ^ (row_index * UINT32_C(0x45d9f3b)) ^
                (field_index * UINT32_C(0x27d4eb2d)) ^ (salt * UINT32_C(0x165667b1));
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    return value ^ (value >> 16);
}

static BusterA64MemoryArrangement
a64_memory_alternate_arrangement(BusterA64MemoryArrangement arrangement)
{
    switch (arrangement)
    {
        case BUSTER_A64_MEMORY_ARRANGEMENT_8B: return BUSTER_A64_MEMORY_ARRANGEMENT_16B;
        case BUSTER_A64_MEMORY_ARRANGEMENT_16B: return BUSTER_A64_MEMORY_ARRANGEMENT_8B;
        case BUSTER_A64_MEMORY_ARRANGEMENT_4H: return BUSTER_A64_MEMORY_ARRANGEMENT_8H;
        case BUSTER_A64_MEMORY_ARRANGEMENT_8H: return BUSTER_A64_MEMORY_ARRANGEMENT_4H;
        case BUSTER_A64_MEMORY_ARRANGEMENT_2S: return BUSTER_A64_MEMORY_ARRANGEMENT_4S;
        case BUSTER_A64_MEMORY_ARRANGEMENT_4S: return BUSTER_A64_MEMORY_ARRANGEMENT_2S;
        case BUSTER_A64_MEMORY_ARRANGEMENT_1D: return BUSTER_A64_MEMORY_ARRANGEMENT_2D;
        case BUSTER_A64_MEMORY_ARRANGEMENT_2D: return BUSTER_A64_MEMORY_ARRANGEMENT_1D;
        default: return BUSTER_A64_MEMORY_ARRANGEMENT_INVALID;
    }
}

/* A candidate is legal only after the independent canonical decoder accepts
 * the word, the typed memory decoder accepts it, and the typed encoder emits
 * the identical word.  Raw field re-encoding is only candidate generation. */
static bool
a64_memory_audit_word(Target target, BusterA64MemoryRowInfo const* row,
                      BusterAarch64CanonicalFormInfo const* canonical, u32 word)
{
    if (!row || !canonical) { return false;
}
    BusterAarch64CanonicalDecodeResult canonical_decoded = {0};
    if (buster_aarch64_canonical_decode(target, word, &canonical_decoded) != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS ||
        canonical_decoded.arm_row_digest != row->source_digest) {
        return false;
}
    u32 raw_values[32] = {0};
    if (canonical->field_count > BUSTER_ARRAY_LENGTH(raw_values) ||
        !buster_aarch64_canonical_raw_decode(canonical->form_index, word, raw_values, canonical->field_count)) {
        return false;
}

    BusterA64MemoryResult decoded = {0};
    if (buster_a64_memory_decode_row(target, row->row_index, word, &decoded) != BUSTER_A64_MEMORY_STATUS_OK) { return false;
}
    if (decoded.operand_count > BUSTER_A64_MEMORY_MAX_OPERANDS) { return false;
}
    BusterA64MemoryInstruction instruction = {.row_index = row->row_index, .operand_count = (u8)decoded.operand_count};
    for (u32 operand_index = 0; operand_index < decoded.operand_count; operand_index += 1) {
        instruction.operands[operand_index] = decoded.operands[operand_index];
}
    u32 encoded = 0;
    return buster_a64_memory_encode(target, &instruction, &encoded) == BUSTER_A64_MEMORY_STATUS_OK && encoded == word;
}

static void
a64_memory_audit_try_word(Target target, BusterA64MemoryRowInfo const* row,
                          BusterAarch64CanonicalFormInfo const* canonical, u32 word,
                          u32* seen_count, u32* seen_words, u32* generated_words, u32* legal_candidates)
{
    if (!seen_count || !seen_words || !generated_words || !legal_candidates || *seen_count >= 256u) { return;
}
    for (u32 index = 0; index < *seen_count; index += 1) {
        if (seen_words[index] == word) { return;
}
}
    seen_words[*seen_count] = word;
    *seen_count += 1;
    *generated_words += 1;
    if (a64_memory_audit_word(target, row, canonical, word)) { *legal_candidates += 1;
}
}

static u32
a64_memory_audit_row(Target target, BusterA64MemoryRowInfo const* row,
                     BusterAarch64CanonicalFormInfo const* canonical, u32* generated_words)
{
    if (!row || !canonical || !generated_words || canonical->field_count > 32u) { return 0;
}
    u32 base_values[32] = {0};
    if (!buster_aarch64_canonical_raw_decode(canonical->form_index, canonical->representative_word,
                                             base_values, canonical->field_count)) {
        return 0;
}
    u32 seen_words[256] = {0};
    u32 seen_count = 0;
    u32 legal_candidates = 0;
    u32 representative_word = canonical->representative_word;
    a64_memory_audit_try_word(target, row, canonical, representative_word, &seen_count, seen_words,
                              generated_words, &legal_candidates);
    for (u32 field_index = 0; field_index < canonical->field_count; field_index += 1)
    {
        BusterAarch64CanonicalFieldInfo field = {0};
        if (!buster_aarch64_canonical_field(canonical->form_index, field_index, &field) || field.width == 0) { continue;
}
        u32 values[8] = {0};
        u32 value_count = 0;
        u32 mask = field.source_mask;
        if (field.width <= 4)
        {
            u32 limit = UINT32_C(1) << field.width;
            for (u32 value = 0; value < limit && value < BUSTER_ARRAY_LENGTH(values); value += 1) { values[value_count++] = value;
}
        }
        else
        {
            values[value_count++] = base_values[field_index];
            values[value_count++] = 0;
            values[value_count++] = mask;
            if (base_values[field_index] < mask) { values[value_count++] = base_values[field_index] + 1;
}
            if (base_values[field_index] != 0) { values[value_count++] = base_values[field_index] - 1;
}
            values[value_count++] = a64_memory_audit_mix(row->row_index, field_index, 1) & mask;
        }
        for (u32 value_index = 0; value_index < value_count; value_index += 1)
        {
            u32 candidate_values[32] = {0};
            for (u32 copy_index = 0; copy_index < canonical->field_count; copy_index += 1) { candidate_values[copy_index] = base_values[copy_index];
}
            candidate_values[field_index] = values[value_index] & mask;
            u32 word = 0;
            if (buster_aarch64_canonical_raw_encode(canonical->form_index, candidate_values, canonical->field_count, &word)) {
                a64_memory_audit_try_word(target, row, canonical, word, &seen_count, seen_words, generated_words, &legal_candidates);
}
        }
    }
    return legal_candidates;
}

UnitTestResult
aarch64_memory_semantics_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, buster_a64_memory_schema_version() == 1u);
    BUSTER_TEST(arguments, buster_a64_memory_row_count() == 559u);
    BUSTER_TEST(arguments, buster_a64_memory_transform_row_count() == 234u);
    BUSTER_TEST(arguments, buster_a64_memory_feature_gated_row_count() == 423u);
    BUSTER_TEST(arguments, buster_a64_memory_validate());

    BusterA64SemanticVMValue arrangement = buster_a64_memory_value_arrangement(BUSTER_A64_MEMORY_ARRANGEMENT_4S);
    BUSTER_TEST(arguments, arrangement.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT && arrangement.aux == BUSTER_A64_MEMORY_ARRANGEMENT_4S);
    BusterA64SemanticVMValue list = buster_a64_memory_value_list(28, 4, BUSTER_A64_MEMORY_ARRANGEMENT_4S);
    BUSTER_TEST(arguments, list.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST);
    BUSTER_TEST(arguments, buster_a64_memory_value_list(29, 4, BUSTER_A64_MEMORY_ARRANGEMENT_4S).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_memory_value_gpr(31, 64, true, false).kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER);
    BUSTER_TEST(arguments, buster_a64_memory_value_gpr(31, 64, true, true).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);

    Target target = {.cpu_arch = CPU_ARCH_AARCH64, .cpu_model = CPU_MODEL_A64_APPLE_M1, .os = OPERATING_SYSTEM_MACOS};
    bool arrangement_bindings_ok = true;
    bool cross_arrangement_rejected = true;
    u32 arrangement_binding_count = 0;
    for (u32 row_index = 0; row_index < buster_a64_memory_row_count(); row_index += 1)
    {
        BusterA64MemoryRowInfo row = {0};
        BusterAarch64CanonicalFormInfo canonical = {0};
        if (!buster_a64_memory_row(row_index, &row) || !a64_memory_test_find_canonical(row.source_digest, &canonical))
        {
            arrangement_bindings_ok = false;
            continue;
        }
        BusterA64MemoryResult decoded = {0};
        BusterA64MemoryStatus decode_status = buster_a64_memory_decode_row(target, row_index, canonical.representative_word, &decoded);
        for (u32 operand_index = 0; operand_index < row.operand_count; operand_index += 1)
        {
            BusterA64MemoryArrangementBinding binding = {0};
            if (!buster_a64_memory_arrangement_binding(row_index, operand_index, &binding)) { continue;
}
            arrangement_binding_count += 1;
            arrangement_bindings_ok = arrangement_bindings_ok && decode_status == BUSTER_A64_MEMORY_STATUS_OK &&
                                      binding.selector_index < decoded.operand_count &&
                                      ((binding.direction == 1 && binding.selector_index == operand_index + 1) ||
                                       (binding.direction == -1 && operand_index != 0 && (u32)binding.selector_index + 1u == operand_index)) &&
                                      decoded.operands[binding.selector_index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT &&
                                      decoded.operands[operand_index].aux == decoded.operands[binding.selector_index].aux;
            if (decode_status != BUSTER_A64_MEMORY_STATUS_OK || binding.selector_index >= decoded.operand_count) { continue;
}
            BusterA64MemoryArrangement alternate = a64_memory_alternate_arrangement((BusterA64MemoryArrangement)decoded.operands[operand_index].aux);
            if (alternate == BUSTER_A64_MEMORY_ARRANGEMENT_INVALID) { continue;
}
            BusterA64MemoryInstruction mismatched = {.row_index = row_index, .operand_count = (u8)decoded.operand_count};
            for (u32 index = 0; index < decoded.operand_count; index += 1) { mismatched.operands[index] = decoded.operands[index];
}
            mismatched.operands[operand_index].aux = alternate;
            u32 preserved_word = UINT32_C(0xa5a5a5a5);
            BusterA64MemoryStatus mismatch_status = buster_a64_memory_encode(target, &mismatched, &preserved_word);
            cross_arrangement_rejected = cross_arrangement_rejected && mismatch_status != BUSTER_A64_MEMORY_STATUS_OK &&
                                         preserved_word == UINT32_C(0xa5a5a5a5);
        }
    }
    BUSTER_TEST(arguments, arrangement_binding_count == buster_a64_memory_arrangement_binding_count());
    BUSTER_TEST(arguments, arrangement_bindings_ok);
    BUSTER_TEST(arguments, cross_arrangement_rejected);

    A64MemoryAuditCounters census = {.rows = buster_a64_memory_row_count()};
    bool all_sources = true;
    for (u32 row_index = 0; row_index < census.rows; row_index += 1)
    {
        BusterA64MemoryRowInfo row = {0};
        BusterAarch64CanonicalFormInfo canonical = {0};
        if (!buster_a64_memory_row(row_index, &row) || !a64_memory_test_find_canonical(row.source_digest, &canonical))
        {
            all_sources = false;
            continue;
        }
        census.source_rows += 1;
        u32 generated_before = census.generated_words;
        BusterA64MemoryResult representative = {0};
        BusterA64MemoryStatus status = buster_a64_memory_decode_row(target, row_index, canonical.representative_word, &representative);
        if (status == BUSTER_A64_MEMORY_STATUS_OK) { census.representative_ok += 1;
        } else if (status == BUSTER_A64_MEMORY_STATUS_RANGE) { census.representative_range += 1;
        } else if (status == BUSTER_A64_MEMORY_STATUS_RESERVED) { census.representative_reserved += 1;
        } else if (status == BUSTER_A64_MEMORY_STATUS_TARGET_MISMATCH) { census.representative_target_mismatch += 1;
        } else { census.representative_other += 1;
}
        u32 legal = a64_memory_audit_row(target, &row, &canonical, &census.generated_words);
        if (census.generated_words != generated_before) { census.seed_rows += 1;
}
        census.legal_candidates += legal;
        if (legal != 0) { census.exercised_rows += 1;
}
    }
    arguments->show(arguments, S8("A64_MEMORY_CENSUS rows={u32} source_rows={u32} seed_rows={u32} representative_ok={u32} representative_range={u32} representative_reserved={u32} representative_target_mismatch={u32} representative_other={u32} generated_words={u32} legal_candidates={u32} exercised_rows={u32}\n"),
                    census.rows, census.source_rows, census.seed_rows, census.representative_ok, census.representative_range,
                    census.representative_reserved, census.representative_target_mismatch, census.representative_other,
                    census.generated_words, census.legal_candidates, census.exercised_rows);
    BUSTER_TEST(arguments, all_sources && census.source_rows == census.rows);
    BUSTER_TEST(arguments, census.seed_rows == census.rows);
    BUSTER_TEST(arguments, census.representative_ok >= 450u);
    BUSTER_TEST(arguments, census.exercised_rows >= 450u);
    BUSTER_TEST(arguments, census.legal_candidates > census.exercised_rows);
    return result;
}

#endif
