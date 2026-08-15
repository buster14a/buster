#include <buster/tests/compiler/assembly/x86_64_metadata_test.h>
#include <buster/tests/compiler/link/link_test.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/string.h>
#if BUSTER_INCLUDE_TESTS

#if BUSTER_CPU_ARCH_X86_64
typedef struct X86_64MetadataShaInventoryCase X86_64MetadataShaInventoryCase;
struct X86_64MetadataShaInventoryCase
{
    u32 form_id;
    u64 stable_hash;
};

BUSTER_GLOBAL_LOCAL X86_64MetadataShaInventoryCase const x86_64_metadata_sha_inventory[] = {
    {8845, UINT64_C(0xee97de2b222473f0)}, {8846, UINT64_C(0x162cc7cd02282552)},
    {8847, UINT64_C(0xcccd57c10c8a3ebe)}, {8848, UINT64_C(0x447901e09888d774)},
    {8849, UINT64_C(0xa246c74e160d72be)}, {8850, UINT64_C(0x4b96b86a50fe74b4)},
    {8851, UINT64_C(0xfe0a75e19c857d33)}, {8852, UINT64_C(0xacf8ab20ea69dfd9)},
    {8853, UINT64_C(0xda318bc38fc8830c)}, {8854, UINT64_C(0xdce6b464e6d7e6fd)},
    {8855, UINT64_C(0x2dcf6f8ac01bf425)}, {8856, UINT64_C(0xec4e803f801b861e)},
    {8857, UINT64_C(0xf5b0cda333e330b9)}, {8858, UINT64_C(0x3ea943b7a373cef2)},
};
#endif

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_string_equal(BusterX86MetadataString first, String8 second)
{
    if (first.length != second.length || (!second.pointer && second.length)) return false;
    for (u32 index = 0; index < first.length; index += 1)
    {
        if (buster_x86_metadata_string_byte(first, index) != (u8)second.pointer[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u64 x86_64_metadata_test_completion_hash_byte(u64 hash, u8 value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

BUSTER_GLOBAL_LOCAL u64 x86_64_metadata_test_completion_hash_u16(u64 hash, u16 value)
{
    hash = x86_64_metadata_test_completion_hash_byte(hash, (u8)value);
    return x86_64_metadata_test_completion_hash_byte(hash, (u8)(value >> 8));
}

BUSTER_GLOBAL_LOCAL u64 x86_64_metadata_test_completion_hash_u32(u64 hash, u32 value)
{
    hash = x86_64_metadata_test_completion_hash_byte(hash, (u8)value);
    hash = x86_64_metadata_test_completion_hash_byte(hash, (u8)(value >> 8));
    hash = x86_64_metadata_test_completion_hash_byte(hash, (u8)(value >> 16));
    return x86_64_metadata_test_completion_hash_byte(hash, (u8)(value >> 24));
}

BUSTER_GLOBAL_LOCAL u64 x86_64_metadata_test_completion_hash_u64(u64 hash, u64 value)
{
    hash = x86_64_metadata_test_completion_hash_u32(hash, (u32)value);
    return x86_64_metadata_test_completion_hash_u32(hash, (u32)(value >> 32));
}

BUSTER_GLOBAL_LOCAL u64 x86_64_metadata_test_completion_hash_string(u64 hash, BusterX86MetadataString value)
{
    hash = x86_64_metadata_test_completion_hash_u32(hash, value.offset);
    hash = x86_64_metadata_test_completion_hash_u32(hash, value.length);
    for (u32 index = 0; index < value.length; index += 1)
        hash = x86_64_metadata_test_completion_hash_byte(hash, buster_x86_metadata_string_byte(value, index));
    return hash;
}

BUSTER_GLOBAL_LOCAL u64 x86_64_metadata_test_completion_hash_operand(u64 hash, BusterX86MetadataOperand value)
{
    hash = x86_64_metadata_test_completion_hash_string(hash, value.atom);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.width);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.slot);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.visible);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.kind);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.access);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.field_source);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(value.reserved); index += 1)
        hash = x86_64_metadata_test_completion_hash_byte(hash, value.reserved[index]);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.physical_class);
    return x86_64_metadata_test_completion_hash_u16(hash, value.physical_width_flags);
}

BUSTER_GLOBAL_LOCAL u64 x86_64_metadata_test_completion_hash_form(u64 hash, BusterX86MetadataForm value)
{
    hash = x86_64_metadata_test_completion_hash_u32(hash, value.id);
    hash = x86_64_metadata_test_completion_hash_u64(hash, value.stable_hash);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.source);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.iclass);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.iform);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.isa_set);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.category);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.extension);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.attributes);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.cpl);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.exceptions);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.flags);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.disasm);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.disasm_intel);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.disasm_att);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.real_opcode);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.uname);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.comment);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.version);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.pattern);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.operands);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.operand_annotation);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.tuple);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.element_size);
    hash = x86_64_metadata_test_completion_hash_string(hash, value.reason);
    hash = x86_64_metadata_test_completion_hash_u32(hash, value.operand_first);
    hash = x86_64_metadata_test_completion_hash_u16(hash, value.operand_count);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.coverage_class);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.encoder_family);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.test_class);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.prefix_kind);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.map);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.fixed_byte_count);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(value.fixed_bytes); index += 1)
        hash = x86_64_metadata_test_completion_hash_byte(hash, value.fixed_bytes[index]);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.mandatory_prefix);
    hash = x86_64_metadata_test_completion_hash_u16(hash, value.field_flags);
    hash = x86_64_metadata_test_completion_hash_u16(hash, value.decorator_flags);
    hash = x86_64_metadata_test_completion_hash_u16(hash, value.apx_flags);
    hash = x86_64_metadata_test_completion_hash_u16(hash, value.amx_flags);
    hash = x86_64_metadata_test_completion_hash_u16(hash, value.mode_flags);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.displacement_width);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.displacement_scale);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.immediate_width);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.immediate_signed);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.relocation_base);
    hash = x86_64_metadata_test_completion_hash_byte(hash, value.tuple_kind);
    hash = x86_64_metadata_test_completion_hash_u32(hash, value.tuple_offset);
    hash = x86_64_metadata_test_completion_hash_u32(hash, value.element_size_offset);
    hash = x86_64_metadata_test_completion_hash_u32(hash, value.token_count);
    return x86_64_metadata_test_completion_hash_u16(hash, value.reason_id);
}

BUSTER_GLOBAL_LOCAL u32 x86_64_metadata_test_completion_status_index(BusterX86MetadataForm form,
                                                                      BusterX86MetadataCoverageLedgerEntry entry)
{
    if (entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED) return BUSTER_X86_COMPLETION_COHORT_EMITTED;
    if (entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED) return BUSTER_X86_COMPLETION_COHORT_BLOCKED;
    return form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED ? BUSTER_X86_COMPLETION_COHORT_NORMALIZED
                                                                           : BUSTER_X86_COMPLETION_COHORT_BLOCKED;
}

BUSTER_GLOBAL_LOCAL BusterX86CompletionLedger x86_64_metadata_test_completion_ledger(
    BusterX86MetadataCoverageLedgerEntry const* entries, u32 entry_count)
{
    BusterX86CompletionLedger result = {0};
    result.form_count = entry_count;
    result.digest = UINT64_C(14695981039346656037);
    result.digest = x86_64_metadata_test_completion_hash_u32(result.digest, entry_count);
    for (u32 form_id = 0; form_id < entry_count; form_id += 1)
    {
        BusterX86MetadataForm form = {0};
        BusterX86MetadataCoverageLedgerEntry entry = entries ? entries[form_id] : (BusterX86MetadataCoverageLedgerEntry){0};
        if (!buster_x86_metadata_form(form_id, &form)) continue;

        result.normalized_count += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED;
        result.emitted_count += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
        result.blocked_count += entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED;
        if (entry.encoder_family < BUSTER_X86_METADATA_ENCODER_COUNT)
        {
            result.family_all_counts[entry.encoder_family] += 1;
            result.family_all_emitted_counts[entry.encoder_family] += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
            result.family_all_blocked_counts[entry.encoder_family] += entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED;
            if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED)
            {
                result.family_counts[entry.encoder_family] += 1;
                result.family_emitted_counts[entry.encoder_family] += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
                result.family_blocked_counts[entry.encoder_family] += entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED;
            }
        }
        if (entry.blocker < BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT) result.blocker_counts[entry.blocker] += 1;
        result.duplicate_form_id_count += form.id != form_id;
        result.zero_stable_hash_count += form.stable_hash == 0;
        result.duplicate_stable_hash_count += buster_x86_metadata_lookup_form_hash(form.stable_hash).count > 1;
        result.emitted_nonzero_blocker_count += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                                 entry.blocker != BUSTER_X86_METADATA_BLOCKER_NONE;

        // Serialize the complete decoded row and its ordered operand records.
        // Every value is fed in an explicit fixed-width little-endian form;
        // no struct padding, host pointers, or arena addresses enter the
        // durable snapshot digest.
        result.digest = x86_64_metadata_test_completion_hash_form(result.digest, form);
        result.digest = x86_64_metadata_test_completion_hash_u32(result.digest, entry.form_id);
        result.digest = x86_64_metadata_test_completion_hash_u64(result.digest, entry.stable_hash);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.coverage_class);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.encoder_family);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.disposition);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.blocker);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.encoder_capable);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.policy_excluded);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(entry.reserved); index += 1)
            result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.reserved[index]);

        u32 status_index = x86_64_metadata_test_completion_status_index(form, entry);
        u32 visible_count = 0;
        u32 operand_kind_counts[BUSTER_X86_METADATA_OPERAND_KIND_COUNT] = {0};
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand operand = {0};
            bool operand_retrieved = buster_x86_metadata_operand(form_id, operand_index, &operand);
            result.digest = x86_64_metadata_test_completion_hash_u32(result.digest, operand_index);
            result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, operand_retrieved);
            if (!operand_retrieved) continue;
            result.digest = x86_64_metadata_test_completion_hash_operand(result.digest, operand);
            result.operand_count += 1;
            if (operand.kind < BUSTER_X86_METADATA_OPERAND_KIND_COUNT) result.operand_kind_counts[operand.kind] += 1;
            if (operand.visible)
            {
                visible_count += 1;
                if (operand.kind < BUSTER_X86_METADATA_OPERAND_KIND_COUNT)
                    result.visible_operand_kind_counts[operand.kind] += 1;
            }
            if (operand.kind < BUSTER_X86_METADATA_OPERAND_KIND_COUNT) operand_kind_counts[operand.kind] += 1;
        }
        if (visible_count <= BUSTER_X86_COMPLETION_MAX_VISIBLE_OPERANDS)
            result.visible_count_distribution[visible_count] += 1;

        u32 field_mask = form.field_flags;
        u32 decorator_mask = form.decorator_flags;
        u32 apx_mask = form.apx_flags;
        u32 amx_mask = form.amx_flags;
        for (u32 cohort = 1; cohort < BUSTER_X86_COMPLETION_COHORT_COUNT; cohort += 1)
        {
            bool include = cohort == status_index || (cohort == BUSTER_X86_COMPLETION_COHORT_NORMALIZED &&
                                                      form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED);
            if (!include) continue;
            for (u32 bit = 0; bit < 9; bit += 1)
                result.field_cohorts[cohort][bit] += (field_mask & (1u << bit)) != 0;
            for (u32 bit = 0; bit < 5; bit += 1)
                result.decorator_cohorts[cohort][bit] += (decorator_mask & (1u << bit)) != 0;
            for (u32 bit = 0; bit < 6; bit += 1)
                result.apx_cohorts[cohort][bit] += (apx_mask & (1u << bit)) != 0;
            for (u32 bit = 0; bit < 4; bit += 1)
                result.amx_cohorts[cohort][bit] += (amx_mask & (1u << bit)) != 0;
        }
        for (u32 bit = 0; bit < 9; bit += 1)
            result.field_cohorts[BUSTER_X86_COMPLETION_COHORT_ALL][bit] += (field_mask & (1u << bit)) != 0;
        for (u32 bit = 0; bit < 5; bit += 1)
            result.decorator_cohorts[BUSTER_X86_COMPLETION_COHORT_ALL][bit] += (decorator_mask & (1u << bit)) != 0;
        for (u32 bit = 0; bit < 6; bit += 1)
            result.apx_cohorts[BUSTER_X86_COMPLETION_COHORT_ALL][bit] += (apx_mask & (1u << bit)) != 0;
        for (u32 bit = 0; bit < 4; bit += 1)
            result.amx_cohorts[BUSTER_X86_COMPLETION_COHORT_ALL][bit] += (amx_mask & (1u << bit)) != 0;

        result.digest = x86_64_metadata_test_completion_hash_u32(result.digest, form.id);
        result.digest = x86_64_metadata_test_completion_hash_u64(result.digest, form.stable_hash);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, form.coverage_class);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.disposition);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.blocker);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.encoder_family);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.encoder_capable != 0);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, entry.policy_excluded != 0);
        result.digest = x86_64_metadata_test_completion_hash_u16(result.digest, form.field_flags);
        result.digest = x86_64_metadata_test_completion_hash_u16(result.digest, form.decorator_flags);
        result.digest = x86_64_metadata_test_completion_hash_u16(result.digest, form.apx_flags);
        result.digest = x86_64_metadata_test_completion_hash_u16(result.digest, form.amx_flags);
        result.digest = x86_64_metadata_test_completion_hash_u16(result.digest, form.mode_flags);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, form.prefix_kind);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, form.map);
        result.digest = x86_64_metadata_test_completion_hash_byte(result.digest, (u8)visible_count);
        result.digest = x86_64_metadata_test_completion_hash_u16(result.digest, form.operand_count);
        for (u32 kind = 0; kind < BUSTER_X86_METADATA_OPERAND_KIND_COUNT; kind += 1)
            result.digest = x86_64_metadata_test_completion_hash_u32(result.digest, operand_kind_counts[kind]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_string_contains(BusterX86MetadataString value, String8 needle)
{
    if (!needle.length || value.length < needle.length) return false;
    String8 span = buster_x86_metadata_string_span(value);
    if (span.length < needle.length) return false;
    for (u32 offset = 0; offset + needle.length <= span.length; offset += 1)
    {
        if ((u8)span.pointer[offset] != (u8)needle.pointer[0])
        {
            continue;
        }
        bool equal = true;
        for (u32 index = 1; index < needle.length && equal; index += 1)
            equal = (u8)span.pointer[offset + index] == (u8)needle.pointer[index];
        if (equal) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_pattern_has_token(BusterX86MetadataString value, String8 token)
{
    if (!token.length || value.length < token.length) return false;
    String8 span = buster_x86_metadata_string_span(value);
    if (span.length < token.length) return false;
    for (u32 offset = 0; offset + token.length <= span.length; offset += 1)
    {
        if (offset && (u8)span.pointer[offset - 1] != ' ') continue;
        if (offset + token.length < span.length && (u8)span.pointer[offset + token.length] != ' ') continue;
        bool equal = true;
        for (u32 index = 0; index < token.length; index += 1)
            equal &= (u8)span.pointer[offset + index] == (u8)token.pointer[index];
        if (equal) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_iform_id(String8 iform, u32* form_id)
{
    BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_iform(iform);
    return range.count == 1 && buster_x86_metadata_candidate_at(range, 0, form_id);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_find_token_form(String8 iclass, String8 token, u8 visible_operand_kind,
                                                               u32* form_id, BusterX86MetadataForm* form_result)
{
    BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_iclass(iclass);
    for (u32 position = 0; position < range.count; position += 1)
    {
        u32 candidate_id = 0;
        BusterX86MetadataForm candidate = {0};
        if (!buster_x86_metadata_candidate_at(range, position, &candidate_id) ||
            !buster_x86_metadata_form(candidate_id, &candidate) ||
            candidate.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED ||
            !x86_64_metadata_test_pattern_has_token(candidate.pattern, token))
            continue;
        bool shape_matches = false;
        for (u32 operand_index = 0; operand_index < candidate.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand operand = {0};
            if (!buster_x86_metadata_operand(candidate_id, operand_index, &operand))
            {
                shape_matches = false;
                break;
            }
            if (operand.visible)
            {
                if (operand.kind != visible_operand_kind)
                {
                    shape_matches = false;
                    break;
                }
                shape_matches = true;
            }
        }
        if (shape_matches)
        {
            if (form_id) *form_id = candidate_id;
            if (form_result) *form_result = candidate;
            return true;
        }
    }
    return false;
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

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_emx_operand_hidden(u32 form_id, u32 expected_visible_count)
{
    BusterX86MetadataForm form = {0};
    if (!buster_x86_metadata_form(form_id, &form) ||
        (form.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST) != 0 ||
        x86_64_metadata_test_visible_operand_count(form_id, form.operand_count) != expected_visible_count)
        return false;
    bool found_emx = false;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand operand = {0};
        if (!buster_x86_metadata_operand(form_id, operand_index, &operand)) return false;
        if (x86_64_metadata_test_string_contains(operand.atom, S8("EMX_BROADCAST_")))
        {
            found_emx = true;
            if (operand.visible || (operand.access & BUSTER_X86_METADATA_ACCESS_IMPLICIT) == 0) return false;
        }
    }
    return found_emx;
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
enum
{
    X86_64_METADATA_TEST_MAX_THREAD_COUNT = 8,
};

typedef struct X86_64MetadataConcurrentLookupState X86_64MetadataConcurrentLookupState;
struct X86_64MetadataConcurrentLookupState
{
    AtomicU64 start;
    AtomicU64 failed;
};

BUSTER_GLOBAL_LOCAL void x86_64_metadata_test_concurrent_lookup(void* argument)
{
    X86_64MetadataConcurrentLookupState* state = (X86_64MetadataConcurrentLookupState*)argument;
    while (!atomic_u64_add(&state->start, 0))
    {
    }
    for (u32 iteration = 0; iteration < 64; iteration += 1)
    {
        BusterX86MetadataCandidateRange range = buster_x86_metadata_lookup_mnemonic(S8("mov"));
        u32 form_id = 0;
        if (!range.count || !x86_64_metadata_test_candidate(range, 0, &form_id) || form_id >= buster_x86_metadata_form_count())
        {
            atomic_u64_increment(&state->failed);
            return;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_concurrent_lookup_stress(u32 requested_thread_count)
{
    // The contract the threads below depend on: every table and demand-filled
    // cache this module answers from is written while the process is still
    // serial, so the lookups are pure reads. Without it the first lookup in
    // each thread would race the decode, and the module says so through
    // BUSTER_CHECK_SERIAL_INITIALIZATION rather than producing torn answers.
    buster_x86_metadata_prewarm();
    // Repeated prewarm entry is intentionally cheap and must not rewalk the
    // generated forms or mutate the caches after the first completion.
    buster_x86_metadata_prewarm();
    X86_64MetadataConcurrentLookupState state = {0};
    OsThreadHandle* threads[X86_64_METADATA_TEST_MAX_THREAD_COUNT] = {0};
    u32 thread_count = 0;
    bool created = true;
    BUSTER_CHECK(requested_thread_count && requested_thread_count <= BUSTER_ARRAY_LENGTH(threads));
    for (u32 index = 0; index < requested_thread_count; index += 1)
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
    atomic_u64_increment(&state.start);
    for (u32 index = 0; index < thread_count; index += 1) created &= os_thread_join(threads[index]);
    return created && thread_count == requested_thread_count && !atomic_u64_add(&state.failed, 0);
}

typedef struct X86_64MetadataConcurrentExactState X86_64MetadataConcurrentExactState;
struct X86_64MetadataConcurrentExactState
{
    AtomicU64 start;
    AtomicU64 failed;
    BusterX86MetadataMachineExactToken token;
    BusterX86MetadataPhysicalOperand const* operands;
    u32 operand_count;
    u8 expected_bytes[16];
    u32 expected_byte_count;
    BusterX86MetadataEncodeStatus expected_status;
};

BUSTER_GLOBAL_LOCAL void x86_64_metadata_test_concurrent_exact_emit(void* argument)
{
    X86_64MetadataConcurrentExactState* state = (X86_64MetadataConcurrentExactState*)argument;
    while (!atomic_u64_add(&state->start, 0))
    {
    }
    for (u32 iteration = 0; iteration < 96; iteration += 1)
    {
        u8 output[16] = {0};
        BusterX86MetadataEmitResult result = buster_x86_metadata_emit_exact_machine(
            state->token, (BusterX86MetadataMachineExactQuery){
                              .operands = state->operands,
                              .operand_count = state->operand_count,
                              .output = output,
                              .output_capacity = BUSTER_ARRAY_LENGTH(output),
                          });
        if (result.status != state->expected_status || result.byte_count != state->expected_byte_count ||
            (result.byte_count && memcmp(output, state->expected_bytes, result.byte_count) != 0))
        {
            atomic_u64_increment(&state->failed);
            return;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_concurrent_exact_emit_stress(u32 requested_thread_count)
{
    // MOV r64,r64 is a compact representative of the migrated DIRECT
    // cohort.  The checked result is captured before the gang starts; every
    // worker then exercises the immutable token/binding plan only.
    BusterX86MetadataFormKey key = {.form_id = 9842u, .stable_hash = UINT64_C(0x3ab69ab9d0d06329)};
    BusterX86MetadataExactPlan plan = {0};
    String8 wildcard_features[1] = {S8("*")};
    BusterX86MetadataPhysicalOperand operands[2] = {
        {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
            .width = 64,
            .reg = {.index = 0, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
        },
        {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
            .width = 64,
            .reg = {.index = 1, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
        },
    };
    if (!buster_x86_metadata_exact_plan_for_key(key, &plan) || !buster_x86_metadata_test_machine_fast_plan(key.form_id)) return false;
    BusterX86MetadataMachineExactToken token = {0};
    if (!buster_x86_metadata_machine_exact_token_for_plan(
            plan, (BusterX86MetadataFeatureInput){.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)}, &token))
        return false;
    u8 checked_bytes[16] = {0};
    BusterX86MetadataEmitResult checked = buster_x86_metadata_emit_exact_query((BusterX86MetadataExactQuery){
        .key = key,
        .operands = operands,
        .operand_count = BUSTER_ARRAY_LENGTH(operands),
        .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .output = checked_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(checked_bytes),
    });
    u8 machine_bytes[16] = {0};
    BusterX86MetadataEmitResult machine = buster_x86_metadata_emit_exact_machine(
        token, (BusterX86MetadataMachineExactQuery){
                   .operands = operands,
                   .operand_count = BUSTER_ARRAY_LENGTH(operands),
                   .output = machine_bytes,
                   .output_capacity = BUSTER_ARRAY_LENGTH(machine_bytes),
               });
    if (checked.status != BUSTER_X86_METADATA_ENCODE_SUCCESS ||
        machine.status != checked.status || machine.byte_count != checked.byte_count ||
        memcmp(machine_bytes, checked_bytes, checked.byte_count) != 0)
        return false;

    X86_64MetadataConcurrentExactState state = {
        .token = token,
        .operands = operands,
        .operand_count = BUSTER_ARRAY_LENGTH(operands),
        .expected_byte_count = checked.byte_count,
        .expected_status = checked.status,
    };
    memcpy(state.expected_bytes, checked_bytes, checked.byte_count);
    OsThreadHandle* threads[X86_64_METADATA_TEST_MAX_THREAD_COUNT] = {0};
    u32 thread_count = 0;
    bool created = true;
    BUSTER_CHECK(requested_thread_count && requested_thread_count <= BUSTER_ARRAY_LENGTH(threads));
    for (u32 index = 0; index < requested_thread_count; index += 1)
    {
        threads[index] = os_thread_create((ThreadCreateOptions){
            .callback = &x86_64_metadata_test_concurrent_exact_emit,
            .argument = &state,
        });
        if (!threads[index])
        {
            created = false;
            break;
        }
        thread_count += 1;
    }
    atomic_u64_increment(&state.start);
    for (u32 index = 0; index < thread_count; index += 1) created &= os_thread_join(threads[index]);
    return created && thread_count == requested_thread_count && !atomic_u64_add(&state.failed, 0);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_concurrent_exact_emit_stress_case(
    u32 requested_thread_count, BusterX86MetadataFormKey key, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count)
{
    String8 wildcard_features[1] = {S8("*")};
    BusterX86MetadataExactPlan plan = {0};
    BusterX86MetadataMachineExactToken token = {0};
    if (!buster_x86_metadata_exact_plan_for_key(key, &plan) || !buster_x86_metadata_test_machine_fast_plan(key.form_id) ||
        !buster_x86_metadata_machine_exact_token_for_plan(
            plan, (BusterX86MetadataFeatureInput){.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)}, &token))
        return false;
    u8 checked_bytes[16] = {0};
    BusterX86MetadataEmitResult checked = buster_x86_metadata_emit_exact_query((BusterX86MetadataExactQuery){
        .key = key,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .output = checked_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(checked_bytes),
    });
    if (checked.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || checked.byte_count > BUSTER_ARRAY_LENGTH(checked_bytes)) return false;
    X86_64MetadataConcurrentExactState state = {
        .token = token,
        .operands = operands,
        .operand_count = operand_count,
        .expected_byte_count = checked.byte_count,
        .expected_status = checked.status,
    };
    memcpy(state.expected_bytes, checked_bytes, checked.byte_count);
    OsThreadHandle* threads[X86_64_METADATA_TEST_MAX_THREAD_COUNT] = {0};
    u32 thread_count = 0;
    bool created = true;
    BUSTER_CHECK(requested_thread_count && requested_thread_count <= BUSTER_ARRAY_LENGTH(threads));
    for (u32 index = 0; index < requested_thread_count; index += 1)
    {
        threads[index] = os_thread_create((ThreadCreateOptions){
            .callback = &x86_64_metadata_test_concurrent_exact_emit,
            .argument = &state,
        });
        if (!threads[index])
        {
            created = false;
            break;
        }
        thread_count += 1;
    }
    atomic_u64_increment(&state.start);
    for (u32 index = 0; index < thread_count; index += 1) created &= os_thread_join(threads[index]);
    return created && thread_count == requested_thread_count && !atomic_u64_add(&state.failed, 0);
}
#endif

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_reg(u8 physical_class, u16 index, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
        .width = width,
        .reg = {.index = index, .width = width, .physical_class = physical_class},
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_mem_base(u16 base, u16 width, s64 displacement)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .base = {.index = base, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
            .displacement = displacement,
            .address_size = 64,
            .scale = 1,
            .has_base = true,
            .has_displacement = displacement != 0,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_mem_index(u16 index, u8 scale, u16 width,
                                                                                              s64 displacement)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .index = {.index = index, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
            .displacement = displacement,
            .address_size = 64,
            .scale = scale,
            .has_index = true,
            .has_displacement = displacement != 0,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_mem_rip(String8 symbol, s64 addend, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .symbol = symbol,
            .addend = addend,
            .address_size = 64,
            .has_symbol = true,
            .rip_relative = true,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_imm(s64 value, u16 width);
BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_relative(s64 value, u16 width);
BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_absolute(String8 symbol, u16 width);
BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalQuery x86_64_metadata_test_physical_query(
    String8 mnemonic, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count);

typedef struct X86_64MetadataCanonicalGateCase X86_64MetadataCanonicalGateCase;
struct X86_64MetadataCanonicalGateCase
{
    u32 form_id;
    u8 required_count;
    String8 required[3];
};

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_build_gate_query(
    u32 form_id, BusterX86MetadataPhysicalQuery* query, BusterX86MetadataPhysicalOperand operands[16],
    char8 mnemonic_buffer[128])
{
    BusterX86MetadataForm form = {0};
    if (!query || !operands || !mnemonic_buffer || !buster_x86_metadata_form(form_id, &form) || form.iclass.length >= 128) return false;
    for (u32 character = 0; character < form.iclass.length; character += 1)
        mnemonic_buffer[character] = (char8)buster_x86_metadata_string_byte(form.iclass, character);
    u32 operand_count = 0;
    for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form_id, operand_index, &metadata)) return false;
        if (!metadata.visible) continue;
        if (operand_count >= 16) return false;
        u16 width = metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024 ? 1024
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_512 ? 512
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_256 ? 256
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 ? 128
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_80 ? 80
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_64 ? 64
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 ? 32
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_16 ? 16
                  : metadata.physical_width_flags & BUSTER_X86_METADATA_PHYSICAL_WIDTH_8 ? 8
                                                                                           : 64;
        if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER)
            operands[operand_count] = x86_64_metadata_test_physical_reg(metadata.physical_class, 0, width);
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY ||
                 metadata.kind == BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR)
            operands[operand_count] = x86_64_metadata_test_physical_mem_base(0, width, 0);
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE)
            operands[operand_count] = x86_64_metadata_test_physical_imm(0, width);
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_RELATIVE)
            operands[operand_count] = x86_64_metadata_test_physical_relative(0, width);
        else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_ABSOLUTE)
            operands[operand_count] = x86_64_metadata_test_physical_absolute(S8("gate"), width);
        else
            return false;
        operand_count += 1;
    }
    *query = x86_64_metadata_test_physical_query(
        (String8){.pointer = mnemonic_buffer, .length = form.iclass.length}, operands, operand_count,
        (BusterX86MetadataPhysicalAttributes){0}, 0, 0);
    query->include_privileged = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED;
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_canonical_gate(X86_64MetadataCanonicalGateCase test_case)
{
    BusterX86MetadataPhysicalOperand operands[16] = {0};
    char8 mnemonic_buffer[128] = {0};
    BusterX86MetadataPhysicalQuery query = {0};
    if (!x86_64_metadata_test_build_gate_query(test_case.form_id, &query, operands, mnemonic_buffer)) return false;

    String8 required_features[3] = {test_case.required[0], test_case.required[1], test_case.required[2]};
    query.features.names = required_features;
    query.features.count = test_case.required_count;
    bool requires_apx = false;
    for (u32 feature_index = 0; feature_index < test_case.required_count; feature_index += 1)
        requires_apx |= required_features[feature_index].length == 3 && required_features[feature_index].pointer[0] == 'a' &&
                        required_features[feature_index].pointer[1] == 'p' && required_features[feature_index].pointer[2] == 'x';
    if (requires_apx)
    {
        for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
        {
            if (operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                operands[operand_index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR)
                operands[operand_index].reg.index = 16;
        }
    }
    BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(query);
    if (selected.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || selected.form_id != test_case.form_id) return false;

    for (u32 removed_index = 0; removed_index < test_case.required_count; removed_index += 1)
    {
        String8 reduced_features[3] = {0};
        u32 reduced_count = 0;
        for (u32 feature_index = 0; feature_index < test_case.required_count; feature_index += 1)
        {
            if (feature_index != removed_index) reduced_features[reduced_count++] = required_features[feature_index];
        }
        query.features.names = reduced_features;
        query.features.count = reduced_count;
        BusterX86MetadataSelectResult missing = buster_x86_metadata_select_form(query);
        if (missing.status != BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_raw_conjunction_rejected(u32 form_id, String8 raw_feature)
{
    BusterX86MetadataPhysicalOperand operands[16] = {0};
    char8 mnemonic_buffer[128] = {0};
    BusterX86MetadataPhysicalQuery query = {0};
    if (!x86_64_metadata_test_build_gate_query(form_id, &query, operands, mnemonic_buffer)) return false;
    String8 raw_features[1] = {raw_feature};
    query.features.names = raw_features;
    query.features.count = BUSTER_ARRAY_LENGTH(raw_features);
    return buster_x86_metadata_select_form(query).status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_imm(s64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
        .width = width,
        .value = value,
        .has_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_imm_u64(u64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
        .width = width,
        .unsigned_value = value,
        .has_unsigned_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_relative(s64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
        .width = width,
        .value = value,
        .has_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand x86_64_metadata_test_physical_absolute(String8 symbol, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE,
        .width = width,
        .symbol = symbol,
        .has_symbol = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalQuery x86_64_metadata_test_physical_query(
    String8 mnemonic, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count)
{
    return (BusterX86MetadataPhysicalQuery){
        .mnemonic = mnemonic,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = features, .count = feature_count},
        .attributes = attributes,
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataEmitResult x86_64_metadata_test_emit_form(
    String8 mnemonic, u32 form_id, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count, u8* output,
    u32 output_capacity, BusterX86MetadataRelocation* relocations, u32 relocation_capacity)
{
    BusterX86MetadataPhysicalQuery physical = x86_64_metadata_test_physical_query(mnemonic, operands, operand_count, attributes,
                                                                                     features, feature_count);
    return buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = physical,
        .form_id = form_id,
        .output = output,
        .output_capacity = output_capacity,
        .relocations = relocations,
        .relocation_capacity = relocation_capacity,
    });
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_bytes_equal(u8 const* actual, u32 actual_count, u8 const* expected, u32 expected_count)
{
    if (actual_count != expected_count || (expected_count && (!actual || !expected))) return false;
    for (u32 index = 0; index < expected_count; index += 1)
    {
        if (actual[index] != expected[index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_emit_exact(
    String8 mnemonic, u32 form_id, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count, u8 const* expected,
    u32 expected_count)
{
    u8 output[32] = {0};
    BusterX86MetadataRelocation relocations[8] = {0};
    BusterX86MetadataEmitResult result = x86_64_metadata_test_emit_form(mnemonic, form_id, operands, operand_count, attributes,
                                                                          features, feature_count, output, sizeof(output), relocations,
                                                                          BUSTER_ARRAY_LENGTH(relocations));
    return result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && result.relocation_count == 0 &&
           x86_64_metadata_test_bytes_equal(output, result.byte_count, expected, expected_count);
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataEmitResult x86_64_metadata_test_emit_named_exact(
    BusterX86MetadataFormKey key, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count, u8* output,
    u32 output_capacity, BusterX86MetadataRelocation* relocations, u32 relocation_capacity)
{
    BusterX86MetadataPhysicalQuery physical = x86_64_metadata_test_physical_query(
        (String8){0}, operands, operand_count, attributes, features, feature_count);
    return buster_x86_metadata_emit_form_exact((BusterX86MetadataEmitQuery){
        .physical = physical,
        .form_id = key.form_id,
        .output = output,
        .output_capacity = output_capacity,
        .relocations = relocations,
        .relocation_capacity = relocation_capacity,
    }, key);
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataEmitResult x86_64_metadata_test_emit_exact_query(
    BusterX86MetadataFormKey key, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count, u8* output,
    u32 output_capacity, BusterX86MetadataRelocation* relocations, u32 relocation_capacity)
{
    return buster_x86_metadata_emit_exact_query((BusterX86MetadataExactQuery){
        .key = key,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = features, .count = feature_count},
        .attributes = attributes,
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .output = output,
        .output_capacity = output_capacity,
        .relocations = relocations,
        .relocation_capacity = relocation_capacity,
    });
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_emit_result_equal(BusterX86MetadataEmitResult checked,
                                                                  BusterX86MetadataEmitResult fast)
{
    return checked.status == fast.status && checked.form_id == fast.form_id && checked.stable_hash == fast.stable_hash &&
           checked.byte_count == fast.byte_count && checked.relocation_count == fast.relocation_count &&
           checked.required_byte_count == fast.required_byte_count && checked.required_relocation_count == fast.required_relocation_count &&
           checked.diagnostic_operand == fast.diagnostic_operand && checked.diagnostic_value == fast.diagnostic_value &&
           checked.required_feature.offset == fast.required_feature.offset && checked.required_feature.length == fast.required_feature.length;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_exact_plan_case(
    BusterX86MetadataFormKey key, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count)
{
    BusterX86MetadataExactPlan plan = {0};
    BusterX86MetadataExactPlan looked_up = {0};
    // Plans are prepared by the serial machine/codegen prewarm hook.  The
    // metadata module runs in worker lanes, so tests must only perform the
    // immutable lookup here (calling prepare would violate that contract).
    if (!buster_x86_metadata_exact_plan_for_key(key, &plan) ||
        !buster_x86_metadata_exact_plan_for_key(key, &looked_up) ||
        plan.form_id != looked_up.form_id || plan.stable_hash != looked_up.stable_hash)
        return false;
    BusterX86MetadataMachineExactToken machine_token = {0};
    if (!buster_x86_metadata_machine_exact_token_for_plan(plan,
                                                          (BusterX86MetadataFeatureInput){.names = features, .count = feature_count},
                                                          &machine_token))
        return false;

    u8 checked_bytes[32] = {0};
    u8 fast_bytes[32] = {0};
    u8 machine_bytes[32] = {0};
    BusterX86MetadataRelocation checked_relocations[8] = {0};
    BusterX86MetadataRelocation fast_relocations[8] = {0};
    BusterX86MetadataRelocation machine_relocations[8] = {0};
    BusterX86MetadataExactQuery checked_query = {
        .key = key,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = features, .count = feature_count},
        .attributes = attributes,
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .output = checked_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(checked_bytes),
        .relocations = checked_relocations,
        .relocation_capacity = BUSTER_ARRAY_LENGTH(checked_relocations),
    };
    BusterX86MetadataExactQuery fast_query = checked_query;
    fast_query.output = fast_bytes;
    fast_query.relocations = fast_relocations;
    BusterX86MetadataMachineExactQuery machine_query = {
        .operands = operands,
        .operand_count = operand_count,
        .output = machine_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(machine_bytes),
        .relocations = machine_relocations,
        .relocation_capacity = BUSTER_ARRAY_LENGTH(machine_relocations),
    };
    BusterX86MetadataEmitResult checked = buster_x86_metadata_emit_exact_query(checked_query);
    BusterX86MetadataEmitResult fast = buster_x86_metadata_emit_exact_prevalidated(plan, fast_query);
    BusterX86MetadataEmitResult machine = buster_x86_metadata_emit_exact_machine(machine_token, machine_query);
    bool result_equal = x86_64_metadata_test_emit_result_equal(checked, fast) &&
                        x86_64_metadata_test_emit_result_equal(checked, machine);
    bool bytes_equal = x86_64_metadata_test_bytes_equal(fast_bytes, fast.byte_count, checked_bytes, checked.byte_count);
    bytes_equal &= x86_64_metadata_test_bytes_equal(machine_bytes, machine.byte_count, checked_bytes, checked.byte_count);
    bool relocations_equal = fast.relocation_count == 0 ||
                             memcmp(fast_relocations, checked_relocations,
                                    fast.relocation_count * sizeof(*fast_relocations)) == 0;
    relocations_equal &= machine.relocation_count == 0 ||
                         memcmp(machine_relocations, checked_relocations,
                                machine.relocation_count * sizeof(*machine_relocations)) == 0;

    // The fast ABI must preserve the checked entry point's failure ordering:
    // a stale query key is rejected before output/capacity validation, while
    // a valid key with no output room reports the same required size.
    BusterX86MetadataExactQuery mismatched_checked = checked_query;
    mismatched_checked.key.stable_hash ^= UINT64_C(1);
    BusterX86MetadataExactQuery mismatched_fast = fast_query;
    mismatched_fast.key.stable_hash ^= UINT64_C(1);
    BusterX86MetadataEmitResult checked_mismatched = buster_x86_metadata_emit_exact_query(mismatched_checked);
    BusterX86MetadataEmitResult fast_mismatched = buster_x86_metadata_emit_exact_prevalidated(plan, mismatched_fast);
    bool mismatched_equal = x86_64_metadata_test_emit_result_equal(checked_mismatched, fast_mismatched);

    BusterX86MetadataExactQuery capacity_checked = checked_query;
    capacity_checked.output_capacity = 0;
    BusterX86MetadataExactQuery capacity_fast = fast_query;
    capacity_fast.output_capacity = 0;
    BusterX86MetadataEmitResult checked_capacity = buster_x86_metadata_emit_exact_query(capacity_checked);
    BusterX86MetadataEmitResult fast_capacity = buster_x86_metadata_emit_exact_prevalidated(plan, capacity_fast);
    bool capacity_equal = x86_64_metadata_test_emit_result_equal(checked_capacity, fast_capacity);
    return result_equal && bytes_equal && relocations_equal && mismatched_equal && capacity_equal;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_exact_plan_missing_feature(BusterX86MetadataFormKey key)
{
    BusterX86MetadataExactPlan plan = {0};
    if (!buster_x86_metadata_exact_plan_for_key(key, &plan)) return false;
    u8 checked_bytes[8] = {0};
    u8 fast_bytes[8] = {0};
    BusterX86MetadataExactQuery checked_query = {
        .key = key,
        .features = {0},
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .output = checked_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(checked_bytes),
    };
    BusterX86MetadataExactQuery fast_query = checked_query;
    fast_query.output = fast_bytes;
    BusterX86MetadataEmitResult checked = buster_x86_metadata_emit_exact_query(checked_query);
    BusterX86MetadataEmitResult fast = buster_x86_metadata_emit_exact_prevalidated(plan, fast_query);
    return x86_64_metadata_test_emit_result_equal(checked, fast);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_machine_fast_scalar_case(
    BusterX86MetadataFormKey key, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count, bool force_disp32)
{
    String8 wildcard_features[1] = {S8("*")};
    BusterX86MetadataExactPlan plan = {0};
    BusterX86MetadataMachineExactToken token = {0};
    if (!buster_x86_metadata_exact_plan_for_key(key, &plan) ||
        !buster_x86_metadata_test_machine_fast_plan(key.form_id) ||
        !buster_x86_metadata_machine_exact_token_for_plan(
            plan, (BusterX86MetadataFeatureInput){.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)}, &token))
        return false;

    u8 checked_bytes[32] = {0};
    u8 machine_bytes[32] = {0};
    BusterX86MetadataRelocation checked_relocations[8] = {0};
    BusterX86MetadataRelocation machine_relocations[8] = {0};
    BusterX86MetadataEmitResult checked = buster_x86_metadata_emit_exact_query((BusterX86MetadataExactQuery){
        .key = key,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .output = checked_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(checked_bytes),
        .relocations = checked_relocations,
        .relocation_capacity = BUSTER_ARRAY_LENGTH(checked_relocations),
    });
    BusterX86MetadataEmitResult machine = buster_x86_metadata_emit_exact_machine(
        token, (BusterX86MetadataMachineExactQuery){
                   .operands = operands,
                   .operand_count = operand_count,
                   .force_disp32 = force_disp32,
                   .output = machine_bytes,
                   .output_capacity = BUSTER_ARRAY_LENGTH(machine_bytes),
                   .relocations = machine_relocations,
                   .relocation_capacity = BUSTER_ARRAY_LENGTH(machine_relocations),
               });
    bool equal = x86_64_metadata_test_emit_result_equal(checked, machine) &&
                 x86_64_metadata_test_bytes_equal(checked_bytes, checked.byte_count, machine_bytes, machine.byte_count) &&
                 checked.relocation_count == machine.relocation_count &&
                 (!checked.relocation_count ||
                  memcmp(checked_relocations, machine_relocations, checked.relocation_count * sizeof(*checked_relocations)) == 0);
    BusterX86MetadataExactQuery checked_capacity_query = {
        .key = key,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .output = checked_bytes,
        .output_capacity = 0,
    };
    BusterX86MetadataEmitResult checked_capacity = buster_x86_metadata_emit_exact_query(checked_capacity_query);
    BusterX86MetadataEmitResult machine_capacity = buster_x86_metadata_emit_exact_machine(
        token, (BusterX86MetadataMachineExactQuery){
                   .operands = operands,
                   .operand_count = operand_count,
                   .force_disp32 = force_disp32,
                   .output = machine_bytes,
                   .output_capacity = 0,
               });
    return equal && x86_64_metadata_test_emit_result_equal(checked_capacity, machine_capacity);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_machine_fast_expected_bytes(
    BusterX86MetadataFormKey key, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    u8 const* expected, u32 expected_count)
{
    String8 wildcard_features[1] = {S8("*")};
    BusterX86MetadataExactPlan plan = {0};
    BusterX86MetadataMachineExactToken token = {0};
    if (!buster_x86_metadata_exact_plan_for_key(key, &plan) ||
        !buster_x86_metadata_machine_exact_token_for_plan(
            plan, (BusterX86MetadataFeatureInput){.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)}, &token))
        return false;
    u8 checked_bytes[32] = {0};
    u8 machine_bytes[32] = {0};
    BusterX86MetadataEmitResult checked = buster_x86_metadata_emit_exact_query((BusterX86MetadataExactQuery){
        .key = key,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .output = checked_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(checked_bytes),
    });
    BusterX86MetadataEmitResult machine = buster_x86_metadata_emit_exact_machine(token, (BusterX86MetadataMachineExactQuery){
        .operands = operands,
        .operand_count = operand_count,
        .output = machine_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(machine_bytes),
    });
    return checked.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && machine.status == checked.status &&
           checked.byte_count == expected_count && machine.byte_count == expected_count &&
           memcmp(checked_bytes, expected, expected_count) == 0 && memcmp(machine_bytes, expected, expected_count) == 0;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_mem128_forms(void)
{
    // Keep every generated NELEM_MEM128 row covered.  The public tuple kind
    // remains FULL for compatibility, while the pattern token and attribute
    // carry the fixed 128-bit memory/16-byte displacement semantics.
    u32 form_ids[] = {
        6456, 6460, 6470, 6474, 6492, 6496, 6500, 6504, 6508,
        6510, 6514, 6534, 6538, 6542, 6546, 6550, 6560, 6564,
        6582, 6586, 6590, 7701, 7705, 7713, 7717, 7725, 7729,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(form_ids); index += 1)
    {
        BusterX86MetadataForm form = {0};
        if (!buster_x86_metadata_form(form_ids[index], &form) ||
            form.tuple_kind != BUSTER_X86_METADATA_TUPLE_FULL ||
            !x86_64_metadata_test_pattern_has_token(form.pattern, S8("NELEM_MEM128()")) ||
            !x86_64_metadata_test_string_contains(form.attributes, S8("DISP8_MEM128")))
            return false;
    }
    return true;
}

typedef enum X86_64MetadataSourceReachabilityClass
{
    X86_64_METADATA_SOURCE_REACHABILITY_SUCCESS,
    X86_64_METADATA_SOURCE_REACHABILITY_SYNTAX_CONSTRUCTION,
    X86_64_METADATA_SOURCE_REACHABILITY_POLICY_FEATURE,
    X86_64_METADATA_SOURCE_REACHABILITY_AMBIGUITY,
    X86_64_METADATA_SOURCE_REACHABILITY_IMPLICIT_HIDDEN,
    X86_64_METADATA_SOURCE_REACHABILITY_PUBLIC_GAP,
} X86_64MetadataSourceReachabilityClass;

typedef struct X86_64MetadataSourceReachabilityResult X86_64MetadataSourceReachabilityResult;
struct X86_64MetadataSourceReachabilityResult
{
    u32 form_id;
    X86_64MetadataSourceReachabilityClass classification;
    bool canonical_query;
    bool direct_emission;
    bool source_encoded;
    bool bytes_match;
    u32 physical_operand_count;
    u32 diagnostic_kind;
    u32 direct_byte_count;
    u32 direct_first_byte;
    u32 source_byte_count;
    u32 source_first_byte;
    u32 mismatch_index;
    u32 mismatch_direct_byte;
    u32 mismatch_source_byte;
    String8 source;
};

typedef struct X86_64MetadataSourceReachabilityCase X86_64MetadataSourceReachabilityCase;
struct X86_64MetadataSourceReachabilityCase
{
    u32 form_id;
    String8 source;
    bool memory_source;
    bool immediate_source;
    s64 immediate_value;
    u64 immediate_unsigned_value;
    bool immediate_unsigned;
};

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_mask_is_decorator(u32 form_id, u32 operand_index);
BUSTER_GLOBAL_LOCAL String8 x86_64_metadata_test_source_register(Arena* arena,
                                                                 BusterX86MetadataPhysicalRegister register_value);

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_register_is_fixed(BusterX86MetadataOperand metadata)
{
    return metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED ||
           x86_64_metadata_test_string_contains(metadata.atom, S8("XED_REG_"));
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_register_is_bsr0(BusterX86MetadataOperand metadata)
{
    return metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
           x86_64_metadata_test_string_equal(metadata.atom, S8("XED_REG_BSR0"));
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_schema_equal(u32 first_form_id, u32 second_form_id)
{
    BusterX86MetadataForm first = {0};
    BusterX86MetadataForm second = {0};
    if (!buster_x86_metadata_form(first_form_id, &first) || !buster_x86_metadata_form(second_form_id, &second)) return false;
    if (first.operand_count != second.operand_count) return false;
    for (u32 operand_index = 0; operand_index < first.operand_count; operand_index += 1)
    {
        BusterX86MetadataOperand first_operand = {0};
        BusterX86MetadataOperand second_operand = {0};
        if (!buster_x86_metadata_operand(first_form_id, operand_index, &first_operand) ||
            !buster_x86_metadata_operand(second_form_id, operand_index, &second_operand))
            return false;
        if (first_operand.slot != second_operand.slot || first_operand.visible != second_operand.visible ||
            first_operand.kind != second_operand.kind || first_operand.access != second_operand.access ||
            first_operand.field_source != second_operand.field_source ||
            first_operand.physical_class != second_operand.physical_class ||
            first_operand.physical_width_flags != second_operand.physical_width_flags ||
            !x86_64_metadata_test_string_equal(first_operand.atom, buster_x86_metadata_string_span(second_operand.atom)) ||
            !x86_64_metadata_test_string_equal(first_operand.width, buster_x86_metadata_string_span(second_operand.width)))
            return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_alias_matches(BusterX86MetadataPhysicalQuery physical, u32 form_id,
                                                                    u8 const* source_bytes, u32 source_byte_count)
{
    if (!source_bytes || !source_byte_count) return false;
    BusterX86MetadataForm form = {0};
    if (!buster_x86_metadata_form(form_id, &form)) return false;
    BusterX86MetadataCandidateRange candidates = buster_x86_metadata_lookup_iclass(buster_x86_metadata_string_span(form.iclass));
    for (u32 candidate_index = 0; candidate_index < candidates.count; candidate_index += 1)
    {
        u32 candidate_id = UINT32_MAX;
        BusterX86MetadataForm candidate = {0};
        if (!buster_x86_metadata_candidate_at(candidates, candidate_index, &candidate_id) || candidate_id == form_id ||
            !buster_x86_metadata_form(candidate_id, &candidate) || candidate.encoder_family == BUSTER_X86_METADATA_ENCODER_AMX ||
            !x86_64_metadata_test_schema_equal(form_id, candidate_id))
            continue;
        u8 alias_bytes[32] = {0};
        BusterX86MetadataRelocation alias_relocations[8] = {0};
        BusterX86MetadataPhysicalQuery alias_query = physical;
        BusterX86MetadataEmitResult alias = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = alias_query,
            .form_id = candidate_id,
            .output = alias_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(alias_bytes),
            .relocations = alias_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(alias_relocations),
        });
        if (alias.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && alias.relocation_count == 0 && alias.byte_count == source_byte_count &&
            memcmp(alias_bytes, source_bytes, source_byte_count) == 0)
            return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL String8 x86_64_metadata_test_source_register_atom(Arena* arena,
                                                                       BusterX86MetadataPhysicalRegister register_value,
                                                                       BusterX86MetadataString atom)
{
    if (x86_64_metadata_test_register_is_bsr0((BusterX86MetadataOperand){
            .kind = BUSTER_X86_METADATA_OPERAND_REGISTER,
            .atom = atom,
        }))
        return S8("bsr0");
    if (x86_64_metadata_test_string_contains(atom, S8("XED_REG_ST")))
        return string_format(arena, S8("st({u8})"), register_value.index);
    if (x86_64_metadata_test_string_equal(atom, S8("X87()")))
        return register_value.index ? string_format(arena, S8("st({u8})"), register_value.index) : S8("st");
    return x86_64_metadata_test_source_register(arena, register_value);
}

BUSTER_GLOBAL_LOCAL String8 x86_64_metadata_test_source_register(Arena* arena,
                                                                 BusterX86MetadataPhysicalRegister register_value)
{
    static String8 const gpr16[] = {
        S8_INITIALIZER("ax"), S8_INITIALIZER("cx"), S8_INITIALIZER("dx"), S8_INITIALIZER("bx"),
        S8_INITIALIZER("sp"), S8_INITIALIZER("bp"), S8_INITIALIZER("si"), S8_INITIALIZER("di"),
    };
    static String8 const gpr32[] = {
        S8_INITIALIZER("eax"), S8_INITIALIZER("ecx"), S8_INITIALIZER("edx"), S8_INITIALIZER("ebx"),
        S8_INITIALIZER("esp"), S8_INITIALIZER("ebp"), S8_INITIALIZER("esi"), S8_INITIALIZER("edi"),
    };
    static String8 const gpr64[] = {
        S8_INITIALIZER("rax"), S8_INITIALIZER("rcx"), S8_INITIALIZER("rdx"), S8_INITIALIZER("rbx"),
        S8_INITIALIZER("rsp"), S8_INITIALIZER("rbp"), S8_INITIALIZER("rsi"), S8_INITIALIZER("rdi"),
    };
    static String8 const gpr8[] = {
        S8_INITIALIZER("al"), S8_INITIALIZER("cl"), S8_INITIALIZER("dl"), S8_INITIALIZER("bl"),
    };
    static String8 const segment[] = {
        S8_INITIALIZER("es"), S8_INITIALIZER("cs"), S8_INITIALIZER("ss"), S8_INITIALIZER("ds"),
        S8_INITIALIZER("fs"), S8_INITIALIZER("gs"),
    };
    switch (register_value.physical_class)
    {
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR:
        if (register_value.width == 8 && register_value.index < 4)
            return gpr8[register_value.index];
        if (register_value.width == 8) return string_format(arena, S8("r{u16}b"), register_value.index);
        if (register_value.width == 16) return register_value.index < 8
                                                    ? gpr16[register_value.index]
                                                    : string_format(arena, S8("r{u16}w"), register_value.index);
        if (register_value.width == 32) return register_value.index < 8
                                                    ? gpr32[register_value.index]
                                                    : string_format(arena, S8("r{u16}d"), register_value.index);
        return register_value.index < 8 ? gpr64[register_value.index] : string_format(arena, S8("r{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM: return string_format(arena, S8("xmm{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM: return string_format(arena, S8("ymm{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM: return string_format(arena, S8("zmm{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK: return string_format(arena, S8("k{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM: return string_format(arena, S8("tmm{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX: return string_format(arena, S8("mm{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_BND: return string_format(arena, S8("bnd{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL: return string_format(arena, S8("cr{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG: return string_format(arena, S8("dr{u16}"), register_value.index);
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT: return register_value.index < BUSTER_ARRAY_LENGTH(segment) ? segment[register_value.index] : (String8){0};
    case BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL: return S8("bsr0");
    default: return (String8){0};
    }
}

BUSTER_GLOBAL_LOCAL String8 x86_64_metadata_test_source_immediate(Arena* arena,
                                                                   BusterX86MetadataPhysicalOperand operand)
{
    if (operand.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE) return (String8){0};
    if (operand.has_unsigned_value) return string_format(arena, S8("0x{u64:x,no_prefix}"), operand.unsigned_value);
    if (!operand.has_value) return (String8){0};
    if (operand.value >= 0) return string_format(arena, S8("0x{u64:x,no_prefix}"), (u64)operand.value);
    if (operand.value == INT64_MIN) return string_format(arena, S8("-0x8000000000000000"));
    return string_format(arena, S8("-0x{u64:x,no_prefix}"), (u64)-operand.value);
}

typedef struct X86_64MetadataSourceQuery X86_64MetadataSourceQuery;
struct X86_64MetadataSourceQuery
{
    BusterX86MetadataPhysicalQuery physical;
    BusterX86MetadataPhysicalOperand operands[16];
    BusterX86MetadataOperand metadata[16];
};

BUSTER_GLOBAL_LOCAL X86_64MetadataSourceReachabilityResult x86_64_metadata_test_source_reachability_case(
    Arena* arena, Target target, X86_64MetadataSourceReachabilityCase test_case);

BUSTER_GLOBAL_LOCAL String8 x86_64_metadata_test_source_memory(Arena* arena,
                                                                BusterX86MetadataPhysicalOperand operand,
                                                                BusterX86MetadataPhysicalAttributes attributes)
{
    BusterX86MetadataPhysicalMemory memory = operand.memory;
    String8 qualifier = {0};
    u16 width = memory.source_width ? memory.source_width : operand.width;
    if (width == 8) qualifier = S8("byte ptr ");
    else if (width == 16) qualifier = S8("word ptr ");
    else if (width == 32) qualifier = S8("dword ptr ");
    else if (width == 64) qualifier = S8("qword ptr ");
    else if (width == 80) qualifier = S8("tbyte ptr ");
    else if (width == 128) qualifier = S8("xmmword ptr ");
    else if (width == 256) qualifier = S8("ymmword ptr ");
    else if (width == 512) qualifier = S8("zmmword ptr ");
    else return (String8){0};

    String8 source = qualifier;
    if (memory.has_segment)
    {
        String8 segment = memory.segment == BUSTER_X86_METADATA_SEGMENT_FS ? S8("fs:")
                         : memory.segment == BUSTER_X86_METADATA_SEGMENT_GS ? S8("gs:")
                                                                           : (String8){0};
        if (!segment.length) return (String8){0};
        source = string_format(arena, S8("{S8}{S8}"), source, segment);
    }
    source = string_format(arena, S8("{S8}["), source);
    bool wrote_term = false;
    if (memory.has_base)
    {
        String8 base = x86_64_metadata_test_source_register(arena, memory.base);
        if (!base.length) return (String8){0};
        source = string_format(arena, S8("{S8}{S8}"), source, base);
        wrote_term = true;
    }
    if (memory.rip_relative)
    {
        if (wrote_term || memory.has_index) return (String8){0};
        source = string_format(arena, S8("{S8}rip"), source);
        wrote_term = true;
    }
    if (memory.has_index)
    {
        String8 index = x86_64_metadata_test_source_register(arena, memory.index);
        if (!index.length || !memory.scale) return (String8){0};
        if (wrote_term) source = string_format(arena, S8("{S8} + {S8}*{u8}"), source, index, memory.scale);
        else source = string_format(arena, S8("{S8}{S8}*{u8}"), source, index, memory.scale);
        wrote_term = true;
    }
    if (memory.has_displacement || (!wrote_term && !memory.has_symbol))
    {
        s64 displacement = memory.displacement;
        if (displacement < 0)
        {
            if (displacement == INT64_MIN) return (String8){0};
            s64 magnitude = -displacement;
            source = wrote_term ? string_format(arena, S8("{S8} - {s64}"), source, magnitude)
                                : string_format(arena, S8("{S8}-{s64}"), source, magnitude);
        }
        else
        {
            source = wrote_term ? string_format(arena, S8("{S8} + {s64}"), source, displacement)
                                : string_format(arena, S8("{S8}{s64}"), source, displacement);
        }
        wrote_term = true;
    }
    if (memory.has_symbol)
    {
        if (!memory.symbol.length || wrote_term) return (String8){0};
        source = string_format(arena, S8("{S8}{S8}"), source, memory.symbol);
        wrote_term = true;
    }
    if (!wrote_term) return (String8){0};
    source = string_format(arena, S8("{S8}]"), source);
    if (attributes.decorator_flags & BUSTER_X86_METADATA_DECORATOR_BROADCAST)
    {
        if (!attributes.broadcast_elements) return (String8){0};
        source = string_format(arena, S8("{S8}{{1to{u8}}}"), source, attributes.broadcast_elements);
    }
    return source;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_memory_query(Arena* arena, u32 form_id,
                                                                   BusterX86MetadataPhysicalQuery canonical,
                                                                   X86_64MetadataSourceQuery* result)
{
    (void)arena;
    BusterX86MetadataForm form = {0};
    if (!result || !buster_x86_metadata_form(form_id, &form) || canonical.operand_count > 16) return false;
    u32 output_count = 0;
    for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form_id, metadata_index, &metadata)) return false;
        if (!metadata.visible) continue;
        if (output_count >= canonical.operand_count || output_count >= BUSTER_ARRAY_LENGTH(result->operands)) return false;
        if (metadata.kind != BUSTER_X86_METADATA_OPERAND_REGISTER && metadata.kind != BUSTER_X86_METADATA_OPERAND_MEMORY &&
            metadata.kind != BUSTER_X86_METADATA_OPERAND_IMMEDIATE)
            return false;
        result->metadata[output_count] = metadata;
        result->operands[output_count] = canonical.operands[output_count];
        output_count += 1;
    }
    if (output_count != canonical.operand_count) return false;

    // The canonical query intentionally uses zero displacement.  Source
    // parsing cannot retain an explicit zero addend, so try a small family of
    // non-zero spellings and let the direct emitter remain the constraint
    // oracle for MODRM/SIB, tuple, and address-size controls.
    static s64 const displacements[] = {0, 1, -1, 0x100, -0x100};
    for (u32 displacement_index = 0; displacement_index < BUSTER_ARRAY_LENGTH(displacements); displacement_index += 1)
    {
        BusterX86MetadataPhysicalOperand trial[16] = {0};
        memcpy(trial, result->operands, output_count * sizeof(*trial));
        bool changed_memory = false;
        for (u32 operand_index = 0; operand_index < output_count; operand_index += 1)
        {
            BusterX86MetadataPhysicalOperand* operand = trial + operand_index;
            if (operand->kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY) continue;
            if (operand->memory.rip_relative || operand->memory.has_symbol) continue;
            operand->memory.displacement = displacements[displacement_index];
            operand->memory.has_displacement = displacements[displacement_index] != 0;
            changed_memory = true;
        }
        if (!changed_memory) continue;
        u8 bytes[32] = {0};
        BusterX86MetadataRelocation relocations[8] = {0};
        BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = (BusterX86MetadataPhysicalQuery){
                .mnemonic = canonical.mnemonic,
                .operands = trial,
                .operand_count = output_count,
                .features = canonical.features,
                .attributes = canonical.attributes,
                .address_size = canonical.address_size,
                .execution_mode = canonical.execution_mode,
                .include_privileged = canonical.include_privileged,
                .include_not64 = canonical.include_not64,
                .include_implicit = canonical.include_implicit,
                .source_semantics = canonical.source_semantics,
            },
            .form_id = form_id,
            .output = bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(bytes),
            .relocations = relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
        });
        if (emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.relocation_count == 0)
        {
            memcpy(result->operands, trial, output_count * sizeof(*trial));
            result->physical = canonical;
            result->physical.operands = result->operands;
            result->physical.operand_count = output_count;
            return true;
        }
    }
    result->physical = canonical;
    result->physical.operands = result->operands;
    result->physical.operand_count = output_count;
    return true;
}

BUSTER_GLOBAL_LOCAL String8 x86_64_metadata_test_source_memory_operands(Arena* arena, u32 form_id,
                                                                          BusterX86MetadataPhysicalQuery query)
{
    BusterX86MetadataForm form = {0};
    if (!buster_x86_metadata_form(form_id, &form)) return (String8){0};
    String8 source = query.mnemonic;
    if (query.attributes.lock) source = string_format(arena, S8("lock {S8}"), source);
    else if (query.attributes.rep) source = string_format(arena, S8("rep {S8}"), source);
    else if (query.attributes.repne) source = string_format(arena, S8("repne {S8}"), source);
    else if (query.attributes.notrack) source = string_format(arena, S8("notrack {S8}"), source);
    if (query.attributes.no_flags) source = string_format(arena, S8("{{nf}} {S8}"), source);
    bool wrote_operand = false;
    u32 operand_index = 0;
    for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form_id, metadata_index, &metadata)) return (String8){0};
        if (!metadata.visible) continue;
        if (operand_index >= query.operand_count) return (String8){0};
        if (x86_64_metadata_test_mask_is_decorator(form_id, operand_index))
        {
            operand_index += 1;
            continue;
        }
        if (query.attributes.has_mask_register && query.operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            query.operands[operand_index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
            query.operands[operand_index].reg.index == query.attributes.mask_register)
        {
            operand_index += 1;
            continue;
        }
        BusterX86MetadataPhysicalOperand operand = query.operands[operand_index++];
        String8 spelling = operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY
                               ? x86_64_metadata_test_source_memory(arena, operand, query.attributes)
                               : operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE
                                     ? x86_64_metadata_test_source_immediate(arena, operand)
                                     : x86_64_metadata_test_source_register_atom(arena, operand.reg, metadata.atom);
        if (!spelling.length) return (String8){0};
        if (!wrote_operand) source = string_format(arena, S8("{S8} {S8}"), source, spelling);
        else source = string_format(arena, S8("{S8}, {S8}"), source, spelling);
        if (!wrote_operand && query.attributes.has_mask_register)
        {
            source = string_format(arena, S8("{S8} {{k{u8}}}"), source, query.attributes.mask_register);
            if (query.attributes.zeroing) source = string_format(arena, S8("{S8} {{z}}"), source);
        }
        wrote_operand = true;
    }
    if (query.attributes.sae)
        source = string_format(arena, S8("{S8}, {{{S8}}}"), source,
                               query.attributes.rounding_mode == BUSTER_X86_METADATA_ROUNDING_NEAREST ? S8("rn-sae") : S8("sae"));
    return string_format(arena, S8("{S8}\n"), source);
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_query_is_apx(BusterX86MetadataForm form)
{
    return form.encoder_family == BUSTER_X86_METADATA_ENCODER_REX2 ||
           form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 ||
           (form.apx_flags & BUSTER_X86_METADATA_APX) != 0;
}

BUSTER_GLOBAL_LOCAL u16 x86_64_metadata_test_source_register_candidate(u8 physical_class, u16 width, u32 ordinal,
                                                                         u32 attempt, bool apx, bool evex)
{
    static u8 const low_gpr[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    static u8 const high_gpr[] = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    static u8 const vector_low[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    static u8 const vector_high[] = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    static u8 const short_registers[] = {1, 2, 3, 4, 5, 6, 7};
    u8 const* candidates = low_gpr;
    u32 candidate_count = BUSTER_ARRAY_LENGTH(low_gpr);
    if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR)
    {
        if (apx)
        {
            candidates = high_gpr;
            candidate_count = BUSTER_ARRAY_LENGTH(high_gpr);
        }
    }
    else if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM ||
             physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM ||
             physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM)
    {
        if (width >= 128 && (apx || evex))
        {
            candidates = vector_high;
            candidate_count = BUSTER_ARRAY_LENGTH(vector_high);
        }
        else
        {
            candidates = vector_low;
            candidate_count = BUSTER_ARRAY_LENGTH(vector_low);
        }
    }
    else if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK)
    {
        candidates = short_registers;
        candidate_count = BUSTER_ARRAY_LENGTH(short_registers);
    }
    else if (physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM ||
             physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX ||
             physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_BND ||
             physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL)
    {
        candidates = short_registers;
        candidate_count = BUSTER_ARRAY_LENGTH(short_registers);
    }
    if (!candidate_count) return 0;
    return candidates[(ordinal + attempt) % candidate_count];
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_query(Arena* arena, u32 form_id,
                                                             BusterX86MetadataPhysicalQuery canonical,
                                                             X86_64MetadataSourceQuery* result)
{
    (void)arena;
    BusterX86MetadataForm form = {0};
    if (!result || !buster_x86_metadata_form(form_id, &form) || canonical.operand_count > 16) return false;
    u32 canonical_index = 0;
    u32 output_count = 0;
    bool apx = x86_64_metadata_test_source_query_is_apx(form);
    for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form_id, metadata_index, &metadata)) return false;
        if (!metadata.visible) continue;
        BusterX86MetadataPhysicalOperand operand = {0};
        if (canonical_index >= canonical.operand_count || output_count >= BUSTER_ARRAY_LENGTH(result->operands)) return false;
        operand = canonical.operands[canonical_index++];
        if (output_count >= BUSTER_ARRAY_LENGTH(result->metadata)) return false;
        result->metadata[output_count] = metadata;
        result->operands[output_count++] = operand;
    }
    if (canonical_index != canonical.operand_count) return false;

    // Try a bounded family of role-distinct choices.  The direct emitter is
    // the constraint oracle: fixed REG/RM/SRM fields, no-rex controls, and
    // architecture-specific register limits all remain authoritative.
    for (u32 attempt = 0; attempt < 16; attempt += 1)
    {
        BusterX86MetadataPhysicalOperand trial[16] = {0};
        memcpy(trial, result->operands, output_count * sizeof(*trial));
        u32 ordinal = 0;
        for (u32 index = 0; index < output_count; index += 1)
        {
            BusterX86MetadataPhysicalOperand* operand = trial + index;
            if (operand->kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER ||
                operand->reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK ||
                x86_64_metadata_test_register_is_fixed(result->metadata[index]) ||
                x86_64_metadata_test_register_is_bsr0(result->metadata[index]))
                continue;
            operand->reg.index = x86_64_metadata_test_source_register_candidate(operand->reg.physical_class, operand->reg.width,
                                                                                 ordinal, attempt, apx,
                                                                                 form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX);
            operand->width = operand->reg.width;
            ordinal += 1;
        }
        u8 bytes[32] = {0};
        BusterX86MetadataRelocation relocations[8] = {0};
        BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = (BusterX86MetadataPhysicalQuery){
                .mnemonic = canonical.mnemonic,
                .operands = trial,
                .operand_count = output_count,
                .features = canonical.features,
                .attributes = canonical.attributes,
                .address_size = canonical.address_size,
                .execution_mode = canonical.execution_mode,
                .include_privileged = canonical.include_privileged,
                .include_not64 = canonical.include_not64,
                .include_implicit = canonical.include_implicit,
                .source_semantics = canonical.source_semantics,
            },
            .form_id = form_id,
            .output = bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(bytes),
            .relocations = relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
        });
        if (emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.relocation_count == 0)
        {
            memcpy(result->operands, trial, output_count * sizeof(*trial));
            result->physical = canonical;
            result->physical.operands = result->operands;
            result->physical.operand_count = output_count;
            return true;
        }
    }
    // Some classes (notably tile operands and fixed-role combinations) have
    // no bounded role-distinct spelling that the public assembler accepts.
    // Keep the canonical query in that case so the reachability result is a
    // real public-source diagnostic rather than a mutator-construction gap.
    result->physical = canonical;
    result->physical.operands = result->operands;
    result->physical.operand_count = output_count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_apply_immediate(
    X86_64MetadataSourceReachabilityCase test_case, BusterX86MetadataPhysicalOperand* operands, u32 operand_count)
{
    if (!test_case.immediate_source || !operands) return true;
    bool found = false;
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand* operand = operands + operand_index;
        if (operand->kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE) continue;
        if (test_case.immediate_unsigned)
        {
            operand->has_value = false;
            operand->has_unsigned_value = true;
            operand->unsigned_value = test_case.immediate_unsigned_value;
        }
        else
        {
            operand->has_unsigned_value = false;
            operand->has_value = true;
            operand->value = test_case.immediate_value;
        }
        found = true;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_memory_skeleton(UnitTestArguments* arguments)
{
    Target target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_INTEL_DIAMOND_RAPIDS,
        .os = OPERATING_SYSTEM_LINUX,
    };
    static u32 const forms[] = {3239, 6460, 7725, 3777};
    bool success = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(forms); index += 1)
    {
        u32 form_id = forms[index];
        X86_64MetadataSourceReachabilityResult result = x86_64_metadata_test_source_reachability_case(
            arguments->arena, target, (X86_64MetadataSourceReachabilityCase){.form_id = form_id, .memory_source = true});
        bool form_success = result.classification == X86_64_METADATA_SOURCE_REACHABILITY_SUCCESS;
        success &= form_success;
        arguments->show(arguments, S8("X86_SOURCE_MEMORY_SKELETON form={u32} success={u32} diag={u32} source={S8}\n"), form_id,
                        form_success, result.diagnostic_kind, result.source);
    }
    return success;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_immediate_skeleton(UnitTestArguments* arguments)
{
    Target target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_INTEL_DIAMOND_RAPIDS,
        .os = OPERATING_SYSTEM_LINUX,
    };
    static X86_64MetadataSourceReachabilityCase const cases[] = {
        // ADD exercises a classic signed imm8 source spelling.
        {.form_id = 9247, .immediate_source = true, .immediate_value = 0x7f},
        // The lower signed imm8 boundary must retain its negative spelling.
        {.form_id = 9247, .immediate_source = true, .immediate_value = -128},
        // MOV exercises a full-width immediate and its 64-bit hexadecimal spelling.
        {.form_id = 10018, .immediate_source = true, .immediate_unsigned = true,
         .immediate_unsigned_value = UINT64_C(0x1122334455667788)},
        // APX NDD exercises a memory plus signed imm8 source form.
        {.form_id = 552, .memory_source = true, .immediate_source = true, .immediate_value = 5},
        // APX also exercises the lower signed imm8 boundary with memory.
        {.form_id = 552, .memory_source = true, .immediate_source = true, .immediate_value = -128},
        // EVEX exercises a control immediate predicate.
        {.form_id = 7740, .immediate_source = true, .immediate_unsigned = true, .immediate_unsigned_value = 7},
    };
    bool success = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        X86_64MetadataSourceReachabilityResult result =
            x86_64_metadata_test_source_reachability_case(arguments->arena, target, cases[index]);
        bool case_success = result.classification == X86_64_METADATA_SOURCE_REACHABILITY_SUCCESS;
        success &= case_success;
        arguments->show(arguments, S8("X86_SOURCE_IMMEDIATE_SKELETON form={u32} success={u32} diag={u32} source={S8} direct_bytes={u32}:{u32} source_bytes={u32}:{u32} match={u32}\n"),
                        result.form_id, case_success, result.diagnostic_kind, result.source, result.direct_byte_count,
                        result.direct_first_byte, result.source_byte_count, result.source_first_byte, result.bytes_match);
    }
    return success;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_att_memory_case(
    UnitTestArguments* arguments, Target target, String8 mnemonic, u32 form_id,
    BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count,
    String8 source, u8 const* expected, u32 expected_count)
{
    BusterX86MetadataPhysicalQuery physical = x86_64_metadata_test_physical_query(
        mnemonic, operands, operand_count, attributes, features, feature_count);
    u8 direct_bytes[32] = {0};
    BusterX86MetadataRelocation direct_relocations[8] = {0};
    BusterX86MetadataEmitResult direct = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = physical,
        .form_id = form_id,
        .output = direct_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(direct_bytes),
        .relocations = direct_relocations,
        .relocation_capacity = BUSTER_ARRAY_LENGTH(direct_relocations),
    });
    BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(physical);
    AssemblyEncodeResult encoded = assembly_encode(arguments->arena, source,
                                                    (AssemblyEncodeOptions){.target = target,
                                                                             .syntax = ASSEMBLY_SYNTAX_ATT});
    bool direct_matches = direct.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && direct.relocation_count == 0 &&
                          x86_64_metadata_test_bytes_equal(direct_bytes, direct.byte_count, expected, expected_count);
    bool source_matches = encoded.diagnostic_count == 0 && encoded.relocation_count == 0 &&
                          x86_64_metadata_test_bytes_equal(encoded.bytes.pointer, (u32)encoded.bytes.length, expected,
                                                           expected_count);
    bool bytes_match = encoded.diagnostic_count == 0 && encoded.relocation_count == 0 &&
                       encoded.bytes.length == direct.byte_count &&
                       x86_64_metadata_test_bytes_equal(encoded.bytes.pointer, (u32)encoded.bytes.length, direct_bytes,
                                                        direct.byte_count);
    arguments->show(arguments, S8("X86_SOURCE_ATT_MEMORY form={u32} selected={u32} select_status={u32} status={u32} reloc={u32} direct={u32} source={u32} match={u32} bytes={u32} direct_first={u32} source_first={u32}\n"),
                    form_id, selected.form_id, selected.status, direct.status, direct.relocation_count, direct_matches,
                    source_matches, bytes_match, direct.byte_count, direct.byte_count ? direct_bytes[0] : 0,
                    encoded.bytes.length ? encoded.bytes.pointer[0] : 0);
    return direct_matches && source_matches && bytes_match;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_att_memory_skeleton(UnitTestArguments* arguments)
{
    Target target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    target.cpu_features_explicit = true;
    target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2,
        TARGET_CPU_FEATURE_X86_AVX,
        TARGET_CPU_FEATURE_X86_AVX512F,
        TARGET_CPU_FEATURE_X86_AVX512VL,
        TARGET_CPU_FEATURE_X86_APX,
    }, 5);
    String8 wildcard[1] = {S8("*")};
    String8 avx512[2] = {S8("avx512f"), S8("avx512vl")};
    String8 avx512_apx[3] = {S8("avx512f"), S8("avx512vl"), S8("apx")};

    // Classic base/index/scale/displacement: AT&T reverses the visible
    // operands and the q suffix supplies the memory width.
    BusterX86MetadataPhysicalOperand classic_memory = x86_64_metadata_test_physical_mem_base(3, 64, 16);
    classic_memory.memory.has_index = true;
    classic_memory.memory.index = (BusterX86MetadataPhysicalRegister){
        .index = 1, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
    classic_memory.memory.scale = 4;
    BusterX86MetadataPhysicalOperand classic_operands[2] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64), classic_memory};
    u8 classic_bytes[] = {0x48, 0x8b, 0x44, 0x8b, 0x10};
    bool classic = x86_64_metadata_test_source_att_memory_case(
        arguments, target, S8("MOV"), 9845, classic_operands, BUSTER_ARRAY_LENGTH(classic_operands),
        (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
        S8("movq 16(%rbx,%rcx,4), %rax\n"), classic_bytes, BUSTER_ARRAY_LENGTH(classic_bytes));

    // EVEX tuple displacement: 16 bytes is represented by a compressed
    // disp8 in the VPSLLD memory form, while AT&T still writes the source
    // memory operand first.
    BusterX86MetadataPhysicalOperand vpslld_memory = x86_64_metadata_test_physical_mem_base(0, 0, 16);
    vpslld_memory.memory.source_width = 128;
    BusterX86MetadataPhysicalOperand vpslld_operands[3] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 1, 256), vpslld_memory};
    u8 vpslld_bytes[] = {0x62, 0xf1, 0x75, 0x28, 0xf2, 0x40, 0x01};
    bool vpslld = x86_64_metadata_test_source_att_memory_case(
        arguments, target, S8("VPSLLD"), 6460, vpslld_operands, BUSTER_ARRAY_LENGTH(vpslld_operands),
        (BusterX86MetadataPhysicalAttributes){0}, avx512, BUSTER_ARRAY_LENGTH(avx512),
        S8("vpslld 16(%rax), %ymm1, %ymm0\n"), vpslld_bytes, BUSTER_ARRAY_LENGTH(vpslld_bytes));

    // EVEX broadcast decorator on a scalar memory element widened to zmm.
    BusterX86MetadataPhysicalOperand vaddps_operands[3] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
        x86_64_metadata_test_physical_mem_base(0, 32, 0),
    };
    BusterX86MetadataPhysicalAttributes broadcast = {
        .decorator_flags = BUSTER_X86_METADATA_DECORATOR_BROADCAST,
        .broadcast_elements = 16,
    };
    u8 vaddps_bytes[] = {0x62, 0xf1, 0x74, 0x58, 0x58, 0x00};
    bool vaddps = x86_64_metadata_test_source_att_memory_case(
        arguments, target, S8("VADDPS"), 6940, vaddps_operands, BUSTER_ARRAY_LENGTH(vaddps_operands), broadcast,
        avx512, BUSTER_ARRAY_LENGTH(avx512), S8("vaddps (%rax){1to16}, %zmm1, %zmm0\n"), vaddps_bytes,
        BUSTER_ARRAY_LENGTH(vaddps_bytes));

    // APX extends the EVEX memory base to EGPR r16; retain the explicit APX
    // feature gate in both metadata and the source assembler target.
    BusterX86MetadataPhysicalOperand egpr_operands[2] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
        x86_64_metadata_test_physical_mem_base(16, 32, 0),
    };
    u8 egpr_bytes[] = {0x62, 0xf9, 0x7d, 0x08, 0x6f, 0x00};
    bool egpr = x86_64_metadata_test_source_att_memory_case(
        arguments, target, S8("VMOVDQA32"), 5584, egpr_operands, BUSTER_ARRAY_LENGTH(egpr_operands),
        (BusterX86MetadataPhysicalAttributes){0}, avx512_apx, BUSTER_ARRAY_LENGTH(avx512_apx),
        S8("vmovdqa32 (%r16), %xmm0\n"), egpr_bytes, BUSTER_ARRAY_LENGTH(egpr_bytes));

    // Explicit FS segment prefix on a classic memory source; no relocation
    // is needed, so the direct byte oracle remains exact here as well.
    BusterX86MetadataPhysicalOperand segment_memory = x86_64_metadata_test_physical_mem_base(3, 64, 0);
    segment_memory.memory.has_segment = true;
    segment_memory.memory.segment = BUSTER_X86_METADATA_SEGMENT_FS;
    BusterX86MetadataPhysicalOperand segment_operands[2] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64), segment_memory};
    u8 segment_bytes[] = {0x64, 0x48, 0x8b, 0x03};
    bool segment = x86_64_metadata_test_source_att_memory_case(
        arguments, target, S8("MOV"), 9845, segment_operands, BUSTER_ARRAY_LENGTH(segment_operands),
        (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
        S8("movq %fs:(%rbx), %rax\n"), segment_bytes, BUSTER_ARRAY_LENGTH(segment_bytes));
    return classic && vpslld && vaddps && egpr && segment;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_mask_is_decorator(u32 form_id, u32 operand_index)
{
    BusterX86MetadataForm form = {0};
    if (!buster_x86_metadata_form(form_id, &form)) return false;
    u32 visible_index = 0;
    for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form_id, metadata_index, &metadata)) return false;
        if (!metadata.visible) continue;
        if (visible_index == operand_index)
        {
            if (metadata.kind != BUSTER_X86_METADATA_OPERAND_REGISTER ||
                metadata.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK)
                return false;
            return !x86_64_metadata_test_string_contains(metadata.atom, S8("_R")) &&
                   !x86_64_metadata_test_string_contains(metadata.atom, S8("_N")) &&
                   !x86_64_metadata_test_string_contains(metadata.atom, S8("_B"));
        }
        visible_index += 1;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL String8 x86_64_metadata_test_source_register_only(Arena* arena, u32 form_id, String8 mnemonic,
                                                                        BusterX86MetadataPhysicalQuery query)
{
    BusterX86MetadataForm form = {0};
    if (!buster_x86_metadata_form(form_id, &form)) return (String8){0};
    String8 source = mnemonic;
    if (query.attributes.lock) source = string_format(arena, S8("lock {S8}"), source);
    else if (query.attributes.rep) source = string_format(arena, S8("rep {S8}"), source);
    else if (query.attributes.repne) source = string_format(arena, S8("repne {S8}"), source);
    else if (query.attributes.notrack) source = string_format(arena, S8("notrack {S8}"), source);
    if (query.attributes.no_flags) source = string_format(arena, S8("{{nf}} {S8}"), source);
    bool wrote_operand = false;
    bool has_bsr0 = false;
    for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
    {
        BusterX86MetadataOperand metadata = {0};
        if (!buster_x86_metadata_operand(form_id, metadata_index, &metadata)) return (String8){0};
        has_bsr0 |= !metadata.visible && x86_64_metadata_test_register_is_bsr0(metadata);
    }
    if (has_bsr0 && query.operand_count == 1)
    {
        // BSRMOVH/L have both operand orders in the metadata.  Looking for
        // BSR0 anywhere in the iform is not enough: the trailing forms carry
        // the same atom in their name.  The leading spelling is the one
        // whose hidden operand precedes the visible register.
        bool bsr0_first = x86_64_metadata_test_string_contains(form.iform, S8("BSRMOVH_BSR0")) ||
                          x86_64_metadata_test_string_contains(form.iform, S8("BSRMOVL_BSR0"));
        if (bsr0_first)
        {
            source = string_format(arena, S8("{S8} bsr0"), source);
            wrote_operand = true;
        }
    }
    if (query.attributes.has_dfv)
    {
        source = string_format(arena, S8("{S8} 0"), source);
        wrote_operand = true;
    }
    for (u32 index = 0; index < query.operand_count; index += 1)
    {
        if (x86_64_metadata_test_mask_is_decorator(form_id, index)) continue;
        if (query.attributes.has_mask_register && query.operands[index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
            query.operands[index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
            query.operands[index].reg.index == query.attributes.mask_register)
            continue;
        BusterX86MetadataOperand metadata = {0};
        u32 visible_index = 0;
        for (u32 metadata_index = 0; metadata_index < form.operand_count; metadata_index += 1)
        {
            BusterX86MetadataOperand candidate = {0};
            if (!buster_x86_metadata_operand(form_id, metadata_index, &candidate)) return (String8){0};
            if (!candidate.visible) continue;
            if (visible_index == index)
            {
                metadata = candidate;
                break;
            }
            visible_index += 1;
        }
        String8 spelling = query.operands[index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE
                               ? x86_64_metadata_test_source_immediate(arena, query.operands[index])
                               : x86_64_metadata_test_source_register_atom(arena, query.operands[index].reg, metadata.atom);
        if (!spelling.length) return (String8){0};
        if (!wrote_operand) source = string_format(arena, S8("{S8} {S8}"), source, spelling);
        else source = string_format(arena, S8("{S8}, {S8}"), source, spelling);
        if (!wrote_operand && query.attributes.has_mask_register)
        {
            String8 mask = string_format(arena, S8("k{u8}"), query.attributes.mask_register);
            source = string_format(arena, S8("{S8} {{{S8}}}"), source, mask);
            if (query.attributes.zeroing) source = string_format(arena, S8("{S8} {{z}}"), source);
        }
        wrote_operand = true;
    }
    if (query.attributes.sae)
        source = string_format(arena, S8("{S8}, {{{S8}}}"), source,
                               query.attributes.rounding_mode == BUSTER_X86_METADATA_ROUNDING_NEAREST ? S8("rn-sae") : S8("sae"));
    if (has_bsr0 && query.operand_count == 1 &&
        !(x86_64_metadata_test_string_contains(form.iform, S8("BSRMOVH_BSR0")) ||
          x86_64_metadata_test_string_contains(form.iform, S8("BSRMOVL_BSR0"))))
        source = string_format(arena, S8("{S8}, bsr0"), source);
    return string_format(arena, S8("{S8}\n"), source);
}

BUSTER_GLOBAL_LOCAL X86_64MetadataSourceReachabilityResult x86_64_metadata_test_source_reachability_case(
    Arena* arena, Target target, X86_64MetadataSourceReachabilityCase test_case)
{
    X86_64MetadataSourceReachabilityResult result = {.form_id = test_case.form_id,
                                                      .classification = X86_64_METADATA_SOURCE_REACHABILITY_PUBLIC_GAP};
    BusterX86MetadataPhysicalOperand operands[16] = {0};
    String8 features[1] = {0};
    char8 mnemonic_buffer[128] = {0};
    BusterX86MetadataPhysicalQuery physical = {0};
    result.canonical_query = buster_x86_metadata_test_canonical_query(test_case.form_id, &physical, operands, features,
                                                                       mnemonic_buffer);
    result.physical_operand_count = physical.operand_count;
    if (!result.canonical_query)
    {
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_SYNTAX_CONSTRUCTION;
        return result;
    }

    if (test_case.immediate_source && !x86_64_metadata_test_source_apply_immediate(test_case, operands,
                                                                                    physical.operand_count))
    {
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_SYNTAX_CONSTRUCTION;
        return result;
    }

    X86_64MetadataSourceQuery source_query = {0};
    bool query_built = true;
    if (!test_case.source.length)
        query_built = test_case.memory_source
                          ? x86_64_metadata_test_source_memory_query(arena, test_case.form_id, physical, &source_query)
                          : x86_64_metadata_test_source_query(arena, test_case.form_id, physical, &source_query);
    if (!test_case.source.length && !query_built)
    {
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_SYNTAX_CONSTRUCTION;
        return result;
    }
    if (!test_case.source.length) physical = source_query.physical;
    u8 direct_bytes[32] = {0};
    BusterX86MetadataRelocation direct_relocations[8] = {0};
    BusterX86MetadataEmitResult direct = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = physical,
        .form_id = test_case.form_id,
        .output = direct_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(direct_bytes),
        .relocations = direct_relocations,
        .relocation_capacity = BUSTER_ARRAY_LENGTH(direct_relocations),
    });
    result.direct_emission = direct.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && direct.relocation_count == 0;
    result.direct_byte_count = direct.byte_count;
    result.direct_first_byte = direct.byte_count ? direct_bytes[0] : 0;
    if (!result.direct_emission)
    {
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_PUBLIC_GAP;
        return result;
    }

    if (!test_case.source.length && test_case.memory_source) physical = source_query.physical;
    String8 source = test_case.source.length ? test_case.source
                     : test_case.memory_source ? x86_64_metadata_test_source_memory_operands(arena, test_case.form_id, physical)
                                               : x86_64_metadata_test_source_register_only(arena, test_case.form_id, physical.mnemonic, physical);
    result.source = source;
    if (!source.length)
    {
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_SYNTAX_CONSTRUCTION;
        return result;
    }
    AssemblyEncodeResult encoded = assembly_encode(arena, source,
                                                    (AssemblyEncodeOptions){.target = target,
                                                                             .syntax = ASSEMBLY_SYNTAX_INTEL});
    result.source_encoded = encoded.diagnostic_count == 0;
    result.diagnostic_kind =
        encoded.diagnostic_count ? (u32)encoded.diagnostics[0].kind : (u32)ASSEMBLY_DIAGNOSTIC_COUNT;
    result.source_byte_count = (u32)encoded.bytes.length;
    result.source_first_byte = encoded.bytes.length ? encoded.bytes.pointer[0] : 0;
    result.mismatch_index = UINT32_MAX;
    u32 shared_byte_count = BUSTER_MIN((u32)encoded.bytes.length, direct.byte_count);
    for (u32 byte_index = 0; byte_index < shared_byte_count; byte_index += 1)
    {
        if (encoded.bytes.pointer[byte_index] != direct_bytes[byte_index])
        {
            result.mismatch_index = byte_index;
            result.mismatch_direct_byte = direct_bytes[byte_index];
            result.mismatch_source_byte = encoded.bytes.pointer[byte_index];
            break;
        }
    }
    result.bytes_match = result.source_encoded && encoded.bytes.length == direct.byte_count &&
                         memcmp(encoded.bytes.pointer, direct_bytes, direct.byte_count) == 0;
    if (result.bytes_match)
    {
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_SUCCESS;
    }
    else if (encoded.diagnostic_count && encoded.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE)
    {
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_POLICY_FEATURE;
    }
    else if (encoded.diagnostic_count && encoded.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS)
    {
        result.classification = physical.operand_count == 0 ? X86_64_METADATA_SOURCE_REACHABILITY_IMPLICIT_HIDDEN
                                                              : X86_64_METADATA_SOURCE_REACHABILITY_AMBIGUITY;
    }
    else if (!encoded.diagnostic_count &&
             x86_64_metadata_test_source_alias_matches(physical, test_case.form_id, encoded.bytes.pointer, (u32)encoded.bytes.length))
    {
        // A public spelling can deliberately select a different encoding only
        // when the candidate has the same complete operand schema (including
        // hidden records) and emits the exact source bytes for this query.
        // Otherwise preserve the mismatch as a real public-source gap.
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_AMBIGUITY;
    }
    else if (!encoded.diagnostic_count)
    {
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_PUBLIC_GAP;
    }
    else
    {
        result.classification = X86_64_METADATA_SOURCE_REACHABILITY_SYNTAX_CONSTRUCTION;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_reachability_skeleton(UnitTestArguments* arguments)
{
    Target target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_INTEL_DIAMOND_RAPIDS,
        .os = OPERATING_SYSTEM_LINUX,
    };
    static X86_64MetadataSourceReachabilityCase const cases[] = {
        // Snapshot form IDs are stable for the checked-in generated table.
        {.form_id = 9842, .source = S8_INITIALIZER("mov rax, rax\n")},
        {.form_id = 6939, .source = S8_INITIALIZER("vaddps zmm0, zmm0, zmm0, {rn-sae}\n")},
        {.form_id = 567, .source = S8_INITIALIZER("add rax, rax, rax\n")},
    };
    bool all_success = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        X86_64MetadataSourceReachabilityResult reachability =
            x86_64_metadata_test_source_reachability_case(arguments->arena, target, cases[index]);
        all_success &= reachability.classification == X86_64_METADATA_SOURCE_REACHABILITY_SUCCESS;
        arguments->show(arguments, S8("X86_SOURCE_REACHABILITY form={u32} canonical={u32} direct={u32} operands={u32} source={u32} match={u32} diag={u32} direct_bytes={u32}:{u32} source_bytes={u32}:{u32} mismatch={u32}:{u32}:{u32} class={u32}\n"),
                        reachability.form_id, reachability.canonical_query, reachability.direct_emission,
                        reachability.physical_operand_count, reachability.source_encoded, reachability.bytes_match,
                        reachability.diagnostic_kind, reachability.direct_byte_count, reachability.direct_first_byte,
                        reachability.source_byte_count, reachability.source_first_byte, reachability.mismatch_index,
                        reachability.mismatch_direct_byte, reachability.mismatch_source_byte, reachability.classification);
    }
    return all_success;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_decorator_case(
    UnitTestArguments* arguments, Target target, String8 mnemonic, u32 form_id,
    BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataPhysicalAttributes attributes, String8 const* features, u32 feature_count,
    String8 intel_source, String8 att_source, u8 const* expected, u32 expected_count)
{
    BusterX86MetadataPhysicalQuery physical = x86_64_metadata_test_physical_query(
        mnemonic, operands, operand_count, attributes, features, feature_count);
    u8 direct_bytes[32] = {0};
    BusterX86MetadataRelocation direct_relocations[8] = {0};
    BusterX86MetadataEmitResult direct = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = physical,
        .form_id = form_id,
        .output = direct_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(direct_bytes),
        .relocations = direct_relocations,
        .relocation_capacity = BUSTER_ARRAY_LENGTH(direct_relocations),
    });
    AssemblyEncodeResult intel = assembly_encode(
        arguments->arena, intel_source, (AssemblyEncodeOptions){.target = target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult att = assembly_encode(
        arguments->arena, att_source, (AssemblyEncodeOptions){.target = target, .syntax = ASSEMBLY_SYNTAX_ATT});
    bool direct_matches = direct.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && direct.relocation_count == 0 &&
                          x86_64_metadata_test_bytes_equal(direct_bytes, direct.byte_count, expected, expected_count);
    bool intel_matches = intel.diagnostic_count == 0 && intel.relocation_count == 0 &&
                         x86_64_metadata_test_bytes_equal(intel.bytes.pointer, (u32)intel.bytes.length, expected, expected_count);
    bool att_matches = att.diagnostic_count == 0 && att.relocation_count == 0 &&
                       x86_64_metadata_test_bytes_equal(att.bytes.pointer, (u32)att.bytes.length, expected, expected_count);
    u32 intel_class = intel_matches ? X86_64_METADATA_SOURCE_REACHABILITY_SUCCESS
                                    : intel.diagnostic_count ? X86_64_METADATA_SOURCE_REACHABILITY_SYNTAX_CONSTRUCTION
                                                              : X86_64_METADATA_SOURCE_REACHABILITY_PUBLIC_GAP;
    u32 att_class = att_matches ? X86_64_METADATA_SOURCE_REACHABILITY_SUCCESS
                                : att.diagnostic_count ? X86_64_METADATA_SOURCE_REACHABILITY_SYNTAX_CONSTRUCTION
                                                        : X86_64_METADATA_SOURCE_REACHABILITY_PUBLIC_GAP;
    arguments->show(arguments, S8("X86_SOURCE_DECORATOR form={u32} direct={u32} intel={u32} att={u32} intel_class={u32} att_class={u32} bytes={u32}\n"),
                    form_id, direct_matches, intel_matches, att_matches, intel_class, att_class, expected_count);
    return direct_matches && intel_matches && att_matches;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_decorator_reachability(UnitTestArguments* arguments)
{
    Target target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    target.cpu_features_explicit = true;
    target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2,
        TARGET_CPU_FEATURE_X86_AVX,
        TARGET_CPU_FEATURE_X86_AVX512F,
        TARGET_CPU_FEATURE_X86_AVX512VL,
        TARGET_CPU_FEATURE_X86_AVX512BW,
        TARGET_CPU_FEATURE_X86_AVX512DQ,
        TARGET_CPU_FEATURE_X86_APX,
        TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF,
    }, 8);
    String8 wildcard[1] = {S8("*")};
    bool success = true;

    // EVEX mask+zeroing: Intel writes decorators after the destination while
    // AT&T writes them after the destination's final operand.
    BusterX86MetadataPhysicalOperand masked_add[3] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 2, 512),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 3, 512),
    };
    BusterX86MetadataPhysicalAttributes mask_zero = {
        .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK | BUSTER_X86_METADATA_DECORATOR_ZEROING,
        .has_mask_register = true,
        .mask_register = 1,
        .zeroing = true,
    };
    u8 mask_zero_bytes[] = {0x62, 0xf1, 0x6c, 0xc9, 0x58, 0xc3};
    success &= x86_64_metadata_test_source_decorator_case(
        arguments, target, S8("VADDPS"), 6938, masked_add, BUSTER_ARRAY_LENGTH(masked_add), mask_zero, wildcard,
        BUSTER_ARRAY_LENGTH(wildcard), S8("vaddps zmm0 {k1}{z}, zmm2, zmm3\n"),
        S8("vaddps %zmm3, %zmm2, %zmm0 {%k1}{z}\n"), mask_zero_bytes, BUSTER_ARRAY_LENGTH(mask_zero_bytes));

    // SAE on a mask-producing compare has distinct Intel/AT&T placement.
    BusterX86MetadataPhysicalOperand sae_compare[4] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 1, 64),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 2, 512),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 3, 512),
        x86_64_metadata_test_physical_imm(7, 8),
    };
    BusterX86MetadataPhysicalAttributes sae = {
        .decorator_flags = BUSTER_X86_METADATA_DECORATOR_SAE,
        .sae = true,
    };
    u8 sae_bytes[] = {0x62, 0xf1, 0x6c, 0x18, 0xc2, 0xcb, 0x07};
    success &= x86_64_metadata_test_source_decorator_case(
        arguments, target, S8("VCMPPS"), 6967, sae_compare, BUSTER_ARRAY_LENGTH(sae_compare), sae, wildcard,
        BUSTER_ARRAY_LENGTH(wildcard), S8("vcmpps k1, zmm2, zmm3, {sae}, 7\n"),
        S8("vcmpps $7, {sae}, %zmm3, %zmm2, %k1\n"), sae_bytes, BUSTER_ARRAY_LENGTH(sae_bytes));

    // The metadata table exposes all four explicit EVEX rounding controls on
    // the fixed-round VADDPS form.  Keep one source pair per mode so each
    // control value is independently checked in both dialects.
    BusterX86MetadataPhysicalOperand rounded_add[3] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 2, 512),
    };
    static struct
    {
        u8 mode;
        String8 decorator;
        u8 bytes[6];
    } const rounding_cases[] = {
        {BUSTER_X86_METADATA_ROUNDING_NEAREST, S8_INITIALIZER("rn-sae"), {0x62, 0xf1, 0x74, 0x18, 0x58, 0xc2}},
        {BUSTER_X86_METADATA_ROUNDING_DOWN, S8_INITIALIZER("rd-sae"), {0x62, 0xf1, 0x74, 0x38, 0x58, 0xc2}},
        {BUSTER_X86_METADATA_ROUNDING_UP, S8_INITIALIZER("ru-sae"), {0x62, 0xf1, 0x74, 0x58, 0x58, 0xc2}},
        {BUSTER_X86_METADATA_ROUNDING_ZERO, S8_INITIALIZER("rz-sae"), {0x62, 0xf1, 0x74, 0x78, 0x58, 0xc2}},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rounding_cases); index += 1)
    {
        BusterX86MetadataPhysicalAttributes rounding = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING,
            .rounding_mode = rounding_cases[index].mode,
        };
        String8 intel_source = string_format(arguments->arena, S8("vaddps zmm0, zmm1, zmm2, {{{S8}}}\n"),
                                              rounding_cases[index].decorator);
        String8 att_source = string_format(arguments->arena, S8("vaddps {{{S8}}}, %zmm2, %zmm1, %zmm0\n"),
                                            rounding_cases[index].decorator);
        success &= x86_64_metadata_test_source_decorator_case(
            arguments, target, S8("VADDPS"), 6939, rounded_add, BUSTER_ARRAY_LENGTH(rounded_add), rounding, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), intel_source, att_source, rounding_cases[index].bytes,
            BUSTER_ARRAY_LENGTH(rounding_cases[index].bytes));
    }

    // APX no-flags (`{nf}`) is a control prefix rather than a mask decorator.
    BusterX86MetadataPhysicalOperand no_flags_add[2] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 32),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 17, 32),
    };
    u8 no_flags_bytes[] = {0x62, 0xec, 0x7c, 0x0c, 0x01, 0xc8};
    success &= x86_64_metadata_test_source_decorator_case(
        arguments, target, S8("ADD"), 561, no_flags_add, BUSTER_ARRAY_LENGTH(no_flags_add),
        (BusterX86MetadataPhysicalAttributes){.no_flags = true}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
        S8("{nf} add r16d, r17d\n"), S8("{nf} addl %r17d, %r16d\n"), no_flags_bytes,
        BUSTER_ARRAY_LENGTH(no_flags_bytes));

    // APX DFV/SCC carries the data-flow value as a pseudo-operand in Intel
    // syntax and as an immediate prefix in AT&T syntax.
    BusterX86MetadataPhysicalOperand ccmp[2] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 8),
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 14, 8),
    };
    u8 ccmp_bytes[] = {0x62, 0x74, 0x14, 0x02, 0x38, 0xf2};
    success &= x86_64_metadata_test_source_decorator_case(
        arguments, target, S8("CCMPB"), 779, ccmp, BUSTER_ARRAY_LENGTH(ccmp),
        (BusterX86MetadataPhysicalAttributes){.has_dfv = true, .dfv = 2}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
        S8("ccmpb 2, dl, r14b\n"), S8("ccmpb $2, %r14b, %dl\n"), ccmp_bytes,
        BUSTER_ARRAY_LENGTH(ccmp_bytes));
    return success;
}

BUSTER_GLOBAL_LOCAL u8 x86_64_metadata_test_relocation_width(AssemblyRelocationKind kind)
{
    switch (kind)
    {
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE8:
    case ASSEMBLY_RELOCATION_X86_PC8: return 1;
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE16:
    case ASSEMBLY_RELOCATION_X86_PC16: return 2;
    case ASSEMBLY_RELOCATION_X86_PC32:
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE32:
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED:
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE32_ZERO_EXTENDED: return 4;
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE64:
    case ASSEMBLY_RELOCATION_X86_PC64: return 8;
    default: return 0;
    }
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_metadata_relocation_kind(u8 metadata_kind, AssemblyRelocationKind* result)
{
    if (!result) return false;
    switch ((BusterX86MetadataRelocationKind)metadata_kind)
    {
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE8:
        *result = ASSEMBLY_RELOCATION_X86_ABSOLUTE8;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE16:
        *result = ASSEMBLY_RELOCATION_X86_ABSOLUTE16;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32:
        *result = ASSEMBLY_RELOCATION_X86_ABSOLUTE32;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE64:
        *result = ASSEMBLY_RELOCATION_X86_ABSOLUTE64;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_SIGN_EXTENDED:
        *result = ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_ZERO_EXTENDED:
        *result = ASSEMBLY_RELOCATION_X86_ABSOLUTE32_ZERO_EXTENDED;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_PC8:
        *result = ASSEMBLY_RELOCATION_X86_PC8;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_PC16:
        *result = ASSEMBLY_RELOCATION_X86_PC16;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_PC32:
        *result = ASSEMBLY_RELOCATION_X86_PC32;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_PC64:
        *result = ASSEMBLY_RELOCATION_X86_PC64;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_KIND_COUNT: break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_relocation_case(
    Arena* arena, Target target, String8 source, AssemblySyntax syntax, u32 form_id,
    BusterX86MetadataPhysicalQuery direct_query, AssemblyRelocationKind expected_kind)
{
    u8 direct_bytes[64] = {0};
    BusterX86MetadataRelocation direct_relocations[8] = {0};
    BusterX86MetadataEmitResult direct = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = direct_query,
        .form_id = form_id,
        .output = direct_bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(direct_bytes),
        .relocations = direct_relocations,
        .relocation_capacity = BUSTER_ARRAY_LENGTH(direct_relocations),
    });
    AssemblyEncodeResult encoded = assembly_encode(arena, source, (AssemblyEncodeOptions){.target = target, .syntax = syntax});
    bool success = direct.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && encoded.diagnostic_count == 0;
    if (!success) return false;

    bool expects_relocation = expected_kind != ASSEMBLY_RELOCATION_COUNT;
    success &= direct.relocation_count == (expects_relocation ? 1u : 0u);
    success &= encoded.relocation_count == (expects_relocation ? 1u : 0u);
    if (!success) return false;
    if (expects_relocation)
    {
        BusterX86MetadataRelocation metadata = direct_relocations[0];
        AssemblyRelocation source_relocation = encoded.relocations[0];
        AssemblyRelocationKind metadata_kind = ASSEMBLY_RELOCATION_COUNT;
        success &= x86_64_metadata_test_metadata_relocation_kind(metadata.kind, &metadata_kind);
        success &= metadata_kind == expected_kind && source_relocation.kind == expected_kind;
        success &= metadata.width == x86_64_metadata_test_relocation_width(expected_kind);
        success &= source_relocation.offset == metadata.offset && source_relocation.addend == metadata.addend;
        if (source_relocation.symbol >= encoded.symbol_count || !encoded.symbols) return false;
        success &= string_equal(encoded.symbols[source_relocation.symbol].name, metadata.symbol);
        if (!success) return false;
    }

    // Compare the resolved instruction bytes, but deliberately exclude the
    // relocation field itself.  The target semantics above (kind, width,
    // offset, addend, and symbol name) are the oracle for that field rather
    // than its zero-filled placeholder spelling.
    success &= encoded.bytes.length == direct.byte_count;
    if (!success) return false;
    for (u32 byte_index = 0; byte_index < direct.byte_count; byte_index += 1)
    {
        bool in_relocation = false;
        if (expects_relocation)
        {
            u32 offset = direct_relocations[0].offset;
            u32 width = direct_relocations[0].width;
            in_relocation = byte_index >= offset && byte_index < offset + width;
        }
        if (!in_relocation && encoded.bytes.pointer[byte_index] != direct_bytes[byte_index]) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_source_relative_absolute_skeleton(UnitTestArguments* arguments)
{
    Target target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_INTEL_DIAMOND_RAPIDS,
        .os = OPERATING_SYSTEM_LINUX,
    };
    String8 wildcard[1] = {S8("*")};
    bool all_success = true;

    // A CS branch-hint prefix forces the metadata source path and retains the
    // short conditional form.  The local label checks the fully resolved
    // displacement in both source dialects.
    BusterX86MetadataPhysicalOperand short_local = x86_64_metadata_test_physical_relative(-3, 8);
    BusterX86MetadataPhysicalQuery short_local_query = x86_64_metadata_test_physical_query(
        S8("JZ"), &short_local, 1,
        (BusterX86MetadataPhysicalAttributes){.branch_hint = BUSTER_X86_METADATA_BRANCH_HINT_NOT_TAKEN}, wildcard,
        BUSTER_ARRAY_LENGTH(wildcard));
    static String8 const short_local_source = S8_INITIALIZER("target:\ncs jz target\n");
    static AssemblySyntax const syntaxes[] = {ASSEMBLY_SYNTAX_INTEL, ASSEMBLY_SYNTAX_ATT};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(syntaxes); index += 1)
    {
        bool case_success = x86_64_metadata_test_source_relocation_case(
            arguments->arena, target, short_local_source, syntaxes[index], 9805, short_local_query,
            ASSEMBLY_RELOCATION_COUNT);
        all_success &= case_success;
        arguments->show(arguments, S8("X86_SOURCE_RELATIVE_LOCAL syntax={u32} form={u32} success={u32}\n"),
                        syntaxes[index], 9805, case_success);
    }

    // An unresolved symbolic displacement must remain near-width: the linker
    // cannot prove that an external target fits in an 8-bit field.  The source
    // adapter therefore upgrades this spelling to canonical near JZ while
    // retaining the addend and PC32 relocation.
    BusterX86MetadataPhysicalOperand short_symbol = {
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
        .width = 32,
        .symbol = S8("external"),
        .addend = 7,
        .has_symbol = true,
    };
    BusterX86MetadataPhysicalQuery short_symbol_query = x86_64_metadata_test_physical_query(
        S8("JZ"), &short_symbol, 1,
        (BusterX86MetadataPhysicalAttributes){.branch_hint = BUSTER_X86_METADATA_BRANCH_HINT_NOT_TAKEN}, wildcard,
        BUSTER_ARRAY_LENGTH(wildcard));
    BusterX86MetadataSelectResult short_symbol_select = buster_x86_metadata_select_form(short_symbol_query);
    bool short_symbol_form_selected = short_symbol_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                      short_symbol_select.selected_byte_count == 7;
    all_success &= short_symbol_form_selected;
    static String8 const short_symbol_source = S8_INITIALIZER("cs jz external+7\n");
    if (short_symbol_form_selected)
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(syntaxes); index += 1)
        {
            bool case_success = x86_64_metadata_test_source_relocation_case(
                arguments->arena, target, short_symbol_source, syntaxes[index], short_symbol_select.form_id,
                short_symbol_query, ASSEMBLY_RELOCATION_X86_PC32);
            all_success &= case_success;
            arguments->show(arguments, S8("X86_SOURCE_RELATIVE_ADDEND syntax={u32} form={u32} success={u32}\n"),
                            syntaxes[index], short_symbol_select.form_id, case_success);
        }
    }

    // A resolved local JMP uses the canonical short form when its
    // displacement fits.  The unresolved spelling below remains near-width
    // for the linker.
    BusterX86MetadataPhysicalOperand near_local = x86_64_metadata_test_physical_relative(-2, 8);
    BusterX86MetadataPhysicalQuery near_local_query = x86_64_metadata_test_physical_query(
        S8("JMP"), &near_local, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
    BusterX86MetadataSelectResult near_local_select = buster_x86_metadata_select_form(near_local_query);
    bool near_local_form_selected = near_local_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                    near_local_select.selected_byte_count == 2;
    all_success &= near_local_form_selected;
    if (near_local_form_selected)
    {
        static String8 const near_local_source = S8_INITIALIZER("target:\njmp target\n");
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(syntaxes); index += 1)
        {
            bool case_success = x86_64_metadata_test_source_relocation_case(
                arguments->arena, target, near_local_source, syntaxes[index], near_local_select.form_id,
                near_local_query, ASSEMBLY_RELOCATION_COUNT);
            all_success &= case_success;
            arguments->show(arguments, S8("X86_SOURCE_RELATIVE_NEAR_LOCAL syntax={u32} form={u32} success={u32}\n"),
                            syntaxes[index], near_local_select.form_id, case_success);
        }
    }
    BusterX86MetadataPhysicalOperand near_symbol = {
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
        .width = 32,
        .symbol = S8("external"),
        .addend = 7,
        .has_symbol = true,
    };
    BusterX86MetadataPhysicalQuery near_symbol_query = x86_64_metadata_test_physical_query(
        S8("JMP"), &near_symbol, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
    BusterX86MetadataSelectResult near_symbol_select = buster_x86_metadata_select_form(near_symbol_query);
    bool near_symbol_form_selected = near_symbol_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                     near_symbol_select.selected_byte_count == 5;
    all_success &= near_symbol_form_selected;
    if (near_symbol_form_selected)
    {
        static String8 const near_symbol_source = S8_INITIALIZER("jmp external+7\n");
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(syntaxes); index += 1)
        {
            bool case_success = x86_64_metadata_test_source_relocation_case(
                arguments->arena, target, near_symbol_source, syntaxes[index], near_symbol_select.form_id,
                near_symbol_query, ASSEMBLY_RELOCATION_X86_PC32);
            all_success &= case_success;
            arguments->show(arguments, S8("X86_SOURCE_RELATIVE_NEAR_ADDEND syntax={u32} form={u32} success={u32}\n"),
                            syntaxes[index], near_symbol_select.form_id, case_success);
        }
    }

    // JMPABS is an absolute (not PC-relative) metadata operand.  AT&T marks
    // that expression with '$'; Intel leaves it bare.  The ABSOLUTE64 kind
    // and addend make this distinction explicit at the public source boundary.
    BusterX86MetadataPhysicalOperand absolute_symbol = {
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE,
        .width = 64,
        .symbol = S8("absolute_target"),
        .addend = 7,
        .has_symbol = true,
    };
    BusterX86MetadataPhysicalQuery absolute_query = x86_64_metadata_test_physical_query(
        S8("JMPABS"), &absolute_symbol, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
        BUSTER_ARRAY_LENGTH(wildcard));
    static String8 const absolute_sources[] = {
        S8_INITIALIZER("jmpabs absolute_target+7\n"),
        S8_INITIALIZER("jmpabs $absolute_target+7\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(syntaxes); index += 1)
    {
        bool case_success = x86_64_metadata_test_source_relocation_case(
            arguments->arena, target, absolute_sources[index], syntaxes[index], 2931, absolute_query,
            ASSEMBLY_RELOCATION_X86_ABSOLUTE64);
        all_success &= case_success;
        arguments->show(arguments, S8("X86_SOURCE_ABSOLUTE syntax={u32} form={u32} success={u32}\n"),
                        syntaxes[index], 2931, case_success);
    }
    return all_success;
}

BUSTER_GLOBAL_LOCAL bool x86_64_metadata_test_register_only_census(UnitTestArguments* arguments)
{
    u32 emitted = 0;
    u32 register_only = 0;
    u32 class_counts[X86_64_METADATA_SOURCE_REACHABILITY_PUBLIC_GAP + 1] = {0};
    u32 ledger_capacity = buster_x86_metadata_form_count();
    BusterX86MetadataCoverageLedgerEntry* ledger =
        arena_allocate(arguments->arena, BusterX86MetadataCoverageLedgerEntry, ledger_capacity);
    BusterX86MetadataCoverageAuditResult audit = buster_x86_metadata_coverage_audit(ledger, ledger_capacity);
    BusterX86CompletionLedger completion = x86_64_metadata_test_completion_ledger(ledger, audit.entry_count);
    BusterX86CompletionLedger empty_completion = x86_64_metadata_test_completion_ledger(0, 0);
    // The post-rebase coverage audit folds the canonical form dispositions
    // (including the baseline legacy-width and MMX rows) into this digest.
    // Keep the complete census tied to that current metadata snapshot.
    bool completion_totals_match = completion.digest == UINT64_C(0x35d2a51adf4091c2) &&
                                   completion.form_count == 11013 && completion.normalized_count == 10607 &&
                                   completion.emitted_count == 10607 && completion.blocked_count == 406 && completion.operand_count == 32813 &&
                                   completion.duplicate_form_id_count == 0 && completion.duplicate_stable_hash_count == 0 &&
                                   completion.zero_stable_hash_count == 0 && completion.emitted_nonzero_blocker_count == 0;
    static u32 const expected_operand_totals[] = {0, 24461, 5412, 2243, 84, 1, 246, 244, 9, 113};
    static u32 const expected_operand_visible[] = {0, 22643, 5170, 2043, 84, 1, 4, 0, 9, 6};
    static u32 const expected_visible_distribution[] = {412, 858, 3420, 3375, 2603, 345};
    static u32 const expected_field_cohorts[][9] = {
        {10609, 10, 80, 5377, 10416, 3276, 2216, 87, 87},
        {10275, 10, 80, 5213, 10042, 3244, 2159, 46, 46},
        {10275, 10, 80, 5213, 10042, 3244, 2159, 46, 46},
        {334, 0, 0, 164, 374, 32, 57, 41, 41},
    };
    static u32 const expected_decorator_cohorts[][5] = {
        {4061, 3527, 1145, 345, 345}, {4056, 3524, 1145, 325, 325},
        {4056, 3524, 1145, 325, 325}, {5, 3, 0, 20, 20},
    };
    static u32 const expected_apx_cohorts[][6] = {
        {2473, 2450, 1622, 806, 640, 75}, {2465, 2442, 1614, 806, 640, 69},
        {2465, 2442, 1614, 806, 640, 69}, {8, 8, 8, 0, 0, 6},
    };
    static u32 const expected_amx_cohorts[][4] = {
        {49, 10, 4, 2}, {49, 10, 4, 2}, {49, 10, 4, 2}, {0, 0, 0, 0},
    };
    bool completion_vectors_match = true;
    for (u32 kind = 0; kind < BUSTER_X86_METADATA_OPERAND_KIND_COUNT; kind += 1)
        completion_vectors_match &= completion.operand_kind_counts[kind] == expected_operand_totals[kind] &&
                                    completion.visible_operand_kind_counts[kind] == expected_operand_visible[kind];
    for (u32 visible = 0; visible < BUSTER_ARRAY_LENGTH(expected_visible_distribution); visible += 1)
        completion_vectors_match &= completion.visible_count_distribution[visible] == expected_visible_distribution[visible];
    for (u32 cohort = 0; cohort < BUSTER_X86_COMPLETION_COHORT_COUNT; cohort += 1)
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_field_cohorts[0]); index += 1)
            completion_vectors_match &= completion.field_cohorts[cohort][index] == expected_field_cohorts[cohort][index];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_decorator_cohorts[0]); index += 1)
            completion_vectors_match &= completion.decorator_cohorts[cohort][index] == expected_decorator_cohorts[cohort][index];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_apx_cohorts[0]); index += 1)
            completion_vectors_match &= completion.apx_cohorts[cohort][index] == expected_apx_cohorts[cohort][index];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_amx_cohorts[0]); index += 1)
            completion_vectors_match &= completion.amx_cohorts[cohort][index] == expected_amx_cohorts[cohort][index];
    }
    static u32 const expected_family_all[] = {1929, 202, 5, 1698, 196, 6812, 49, 122};
    static u32 const expected_family_all_emitted[] = {1784, 197, 5, 1644, 176, 6728, 49, 24};
    static u32 const expected_family_all_blocked[] = {145, 5, 0, 54, 20, 84, 0, 98};
    static u32 const expected_family_normalized[] = {1784, 197, 5, 1644, 176, 6728, 49, 24};
    static u32 const expected_family_normalized_emitted[] = {1784, 197, 5, 1644, 176, 6728, 49, 24};
    static u32 const expected_family_normalized_blocked[] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (u32 family = 0; family < BUSTER_X86_METADATA_ENCODER_COUNT; family += 1)
        completion_vectors_match &= completion.family_all_counts[family] == expected_family_all[family] &&
                                    completion.family_all_emitted_counts[family] == expected_family_all_emitted[family] &&
                                    completion.family_all_blocked_counts[family] == expected_family_all_blocked[family] &&
                                    completion.family_counts[family] == expected_family_normalized[family] &&
                                    completion.family_emitted_counts[family] == expected_family_normalized_emitted[family] &&
                                    completion.family_blocked_counts[family] == expected_family_normalized_blocked[family];
    static u32 const expected_blockers[] = {10607, 270, 108, 1, 0, 0, 0, 0, 0, 0, 0, 0, 27};
    for (u32 blocker = 0; blocker < BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT; blocker += 1)
        completion_vectors_match &= completion.blocker_counts[blocker] == expected_blockers[blocker];
    bool completion_structural_ok = completion_totals_match && completion_vectors_match &&
                                    empty_completion.digest == UINT64_C(0x4d25767f9dce13f5);
    arguments->show(arguments, S8("X86_COMPLETION_LEDGER digest={u64} forms={u32} normalized={u32} emitted={u32} blocked={u32} operands={u32} dup_form={u32} dup_hash={u32} zero_hash={u32} emitted_blocker={u32}\n"),
                    completion.digest, completion.form_count, completion.normalized_count, completion.emitted_count,
                    completion.blocked_count, completion.operand_count, completion.duplicate_form_id_count,
                    completion.duplicate_stable_hash_count, completion.zero_stable_hash_count, completion.emitted_nonzero_blocker_count);
    for (u32 kind = 0; kind < BUSTER_X86_METADATA_OPERAND_KIND_COUNT; kind += 1)
        arguments->show(arguments, S8("X86_COMPLETION_OPERAND kind={u32} total={u32} visible={u32}\n"), kind,
                        completion.operand_kind_counts[kind], completion.visible_operand_kind_counts[kind]);
    for (u32 visible = 0; visible <= BUSTER_X86_COMPLETION_MAX_VISIBLE_OPERANDS; visible += 1)
        if (completion.visible_count_distribution[visible])
            arguments->show(arguments, S8("X86_COMPLETION_VISIBLE count={u32} rows={u32}\n"), visible,
                            completion.visible_count_distribution[visible]);
    for (u32 cohort = 0; cohort < BUSTER_X86_COMPLETION_COHORT_COUNT; cohort += 1)
    {
        arguments->show(arguments, S8("X86_COMPLETION_COHORT cohort={u32} field={u32},{u32},{u32},{u32},{u32},{u32},{u32},{u32},{u32} decorator={u32},{u32},{u32},{u32},{u32} apx={u32},{u32},{u32},{u32},{u32},{u32} amx={u32},{u32},{u32},{u32}\n"),
                        cohort, completion.field_cohorts[cohort][0], completion.field_cohorts[cohort][1],
                        completion.field_cohorts[cohort][2], completion.field_cohorts[cohort][3], completion.field_cohorts[cohort][4],
                        completion.field_cohorts[cohort][5], completion.field_cohorts[cohort][6], completion.field_cohorts[cohort][7],
                        completion.field_cohorts[cohort][8], completion.decorator_cohorts[cohort][0], completion.decorator_cohorts[cohort][1],
                        completion.decorator_cohorts[cohort][2], completion.decorator_cohorts[cohort][3], completion.decorator_cohorts[cohort][4],
                        completion.apx_cohorts[cohort][0], completion.apx_cohorts[cohort][1], completion.apx_cohorts[cohort][2],
                        completion.apx_cohorts[cohort][3], completion.apx_cohorts[cohort][4], completion.apx_cohorts[cohort][5],
                        completion.amx_cohorts[cohort][0], completion.amx_cohorts[cohort][1], completion.amx_cohorts[cohort][2],
                        completion.amx_cohorts[cohort][3]);
    }
    for (u32 family = 0; family < BUSTER_X86_METADATA_ENCODER_COUNT; family += 1)
        arguments->show(arguments, S8("X86_COMPLETION_FAMILY family={u32} all={u32},{u32},{u32} normalized={u32},{u32},{u32}\n"), family,
                        completion.family_all_counts[family], completion.family_all_emitted_counts[family], completion.family_all_blocked_counts[family],
                        completion.family_counts[family], completion.family_emitted_counts[family], completion.family_blocked_counts[family]);
    for (u32 blocker = 0; blocker < BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT; blocker += 1)
        arguments->show(arguments, S8("X86_COMPLETION_BLOCKER blocker={u32} count={u32}\n"), blocker, completion.blocker_counts[blocker]);
    arguments->show(arguments, S8("X86_COMPLETION_INVARIANTS duplicate_form={u32} duplicate_hash={u32} zero_hash={u32} emitted_blocker={u32}\n"),
                    completion.duplicate_form_id_count, completion.duplicate_stable_hash_count, completion.zero_stable_hash_count,
                    completion.emitted_nonzero_blocker_count);
    for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
    {
        if (ledger[form_id].disposition != BUSTER_X86_METADATA_COVERAGE_EMITTED) continue;
        emitted += 1;
        BusterX86MetadataPhysicalOperand operands[16] = {0};
        String8 features[1] = {0};
        char8 mnemonic_buffer[128] = {0};
        BusterX86MetadataPhysicalQuery query = {0};
        if (!buster_x86_metadata_test_canonical_query(form_id, &query, operands, features, mnemonic_buffer)) continue;
        bool register_only_form = true;
        for (u32 operand_index = 0; operand_index < query.operand_count; operand_index += 1)
            register_only_form &= query.operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER;
        if (!register_only_form) continue;
        register_only += 1;
        X86_64MetadataSourceReachabilityResult reachability = x86_64_metadata_test_source_reachability_case(
            arguments->arena, (Target){.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_INTEL_DIAMOND_RAPIDS,
                                       .os = OPERATING_SYSTEM_LINUX},
            (X86_64MetadataSourceReachabilityCase){.form_id = form_id});
        class_counts[reachability.classification] += 1;
        if (reachability.classification != X86_64_METADATA_SOURCE_REACHABILITY_SUCCESS &&
            class_counts[reachability.classification] <= 3)
        {
            BusterX86MetadataForm failed_form = {0};
            buster_x86_metadata_form(form_id, &failed_form);
            arguments->show(arguments, S8("X86_SOURCE_REGISTER_FAILURE form={u32} class={u32} diag={u32} iclass={S8} iform={S8} source={S8} mismatch={u32}:{u32}:{u32}\n"),
                            form_id, reachability.classification, reachability.diagnostic_kind,
                            buster_x86_metadata_string_span(failed_form.iclass), buster_x86_metadata_string_span(failed_form.iform),
                            reachability.source, reachability.mismatch_index, reachability.mismatch_direct_byte,
                            reachability.mismatch_source_byte);
        }
    }
    arguments->show(arguments, S8("X86_SOURCE_REGISTER_CENSUS emitted={u32} register_only={u32} success={u32} syntax={u32} policy={u32} ambiguity={u32} implicit={u32} gap={u32}\n"),
                    emitted, register_only, class_counts[0], class_counts[1], class_counts[2], class_counts[3],
                    class_counts[4], class_counts[5]);
    return completion_structural_ok && audit.complete && emitted == audit.emitted_count && class_counts[0] + class_counts[1] + class_counts[2] +
               class_counts[3] + class_counts[4] + class_counts[5] == register_only;
}

UnitTestResult x86_64_metadata_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};

#if BUSTER_CPU_ARCH_X86_64
    // SHA is a first-class canonical ISA set, with both register and memory
    // forms. Keep the complete generated inventory pinned by id/hash so a
    // future metadata refresh cannot silently drop one of the 14 rows.
    char8 sha_inventory_text[1024] = {0};
    u32 sha_inventory_text_length = 0;
    bool sha_inventory_valid = BUSTER_ARRAY_LENGTH(x86_64_metadata_sha_inventory) == 14;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(x86_64_metadata_sha_inventory); index += 1)
    {
        X86_64MetadataShaInventoryCase expected = x86_64_metadata_sha_inventory[index];
        BusterX86MetadataForm form = {0};
        bool form_valid = buster_x86_metadata_form(expected.form_id, &form) &&
                          form.id == expected.form_id && form.stable_hash == expected.stable_hash &&
                          x86_64_metadata_test_string_equal(form.isa_set, S8("SHA"));
        sha_inventory_valid &= form_valid;
        String8 line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"), expected.form_id,
                                     expected.stable_hash);
        bool line_fits = sha_inventory_text_length + line.length <= sizeof(sha_inventory_text);
        sha_inventory_valid &= line_fits;
        if (line_fits)
        {
            memcpy(sha_inventory_text + sha_inventory_text_length, line.pointer, line.length);
            sha_inventory_text_length += (u32)line.length;
        }
    }
    u8 sha_inventory_digest[32] = {0};
    link_sha256(arguments->arena, (u8 const*)sha_inventory_text, sha_inventory_text_length, sha_inventory_digest);
    static u8 const expected_sha_inventory_digest[32] = {
        0xcf, 0x0c, 0x39, 0xef, 0x0e, 0x44, 0x53, 0x9a,
        0x8e, 0x3a, 0xfa, 0x8d, 0xb3, 0x05, 0x0d, 0x99,
        0xed, 0x6c, 0x18, 0x08, 0x18, 0x62, 0xae, 0x07,
        0xe2, 0x0d, 0x95, 0x19, 0x4f, 0x6a, 0xc3, 0xee,
    };
    BUSTER_TEST(arguments, sha_inventory_valid && sha_inventory_text_length == 308);
    BUSTER_TEST(arguments, memcmp(sha_inventory_digest, expected_sha_inventory_digest,
                                  sizeof(sha_inventory_digest)) == 0);
    String8 sha_features[] = {S8("sha")};
    String8 no_sha_features[] = {S8("sse2")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(x86_64_metadata_sha_inventory); index += 1)
    {
        u32 form_id = x86_64_metadata_sha_inventory[index].form_id;
        BUSTER_TEST(arguments, buster_x86_metadata_test_feature_available(form_id, sha_features, BUSTER_ARRAY_LENGTH(sha_features)));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_feature_available(form_id, no_sha_features, BUSTER_ARRAY_LENGTH(no_sha_features)));
    }
#endif

    {
        // The generated snapshot keeps lock-prefixed spellings as separate
        // rows, but the three ordinary memory aliases below are also valid
        // source identities for a checked `lock` query.  Verify that the
        // alias emits exactly the architectural lock row, while its adjacent
        // register form remains rejected.
        typedef struct X86_64MetadataLockAliasCase X86_64MetadataLockAliasCase;
        struct X86_64MetadataLockAliasCase
        {
            u32 alias_form_id;
            u32 lock_form_id;
            u8 byte_count;
            u8 bytes[5];
        };
        static X86_64MetadataLockAliasCase const lock_alias_cases[] = {
            {9530, 9529, 5, {0xf0, 0x48, 0x0f, 0xc7, 0x08}},
            {10278, 10277, 4, {0xf0, 0x0f, 0xb0, 0x00, 0x00}},
            {10281, 10280, 5, {0xf0, 0x48, 0x0f, 0xb1, 0x00}},
        };
        bool lock_aliases_valid = true;
        String8 wildcard_features[1] = {S8("*")};
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(lock_alias_cases); case_index += 1)
        {
            X86_64MetadataLockAliasCase test_case = lock_alias_cases[case_index];
            BusterX86MetadataFormKey alias_key = {0};
            BusterX86MetadataFormKey lock_key = {0};
            BusterX86MetadataPhysicalOperand alias_operands[16] = {0};
            BusterX86MetadataPhysicalOperand lock_operands[16] = {0};
            String8 alias_features[1] = {0};
            String8 lock_features[1] = {0};
            char8 alias_mnemonic[128] = {0};
            char8 lock_mnemonic[128] = {0};
            BusterX86MetadataPhysicalQuery alias_query = {0};
            BusterX86MetadataPhysicalQuery lock_query = {0};
            bool forms_ready = buster_x86_metadata_form_key(test_case.alias_form_id, &alias_key) &&
                                buster_x86_metadata_form_key(test_case.lock_form_id, &lock_key) &&
                                buster_x86_metadata_test_canonical_query(test_case.alias_form_id, &alias_query, alias_operands,
                                                                          alias_features, alias_mnemonic) &&
                                buster_x86_metadata_test_canonical_query(test_case.lock_form_id, &lock_query, lock_operands,
                                                                          lock_features, lock_mnemonic);
            lock_aliases_valid &= forms_ready;
            if (!forms_ready) continue;
            alias_query.features.names = wildcard_features;
            alias_query.features.count = BUSTER_ARRAY_LENGTH(wildcard_features);
            alias_query.attributes.lock = true;
            lock_query.features.names = wildcard_features;
            lock_query.features.count = BUSTER_ARRAY_LENGTH(wildcard_features);
            lock_query.attributes.lock = true;
            u8 alias_bytes[16] = {0};
            u8 checked_bytes[16] = {0};
            u8 lock_bytes[16] = {0};
            BusterX86MetadataEmitResult alias_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = alias_query,
                .form_id = test_case.alias_form_id,
                .output = alias_bytes,
                .output_capacity = BUSTER_ARRAY_LENGTH(alias_bytes),
            });
            BusterX86MetadataEmitResult checked_result = buster_x86_metadata_emit_exact_query((BusterX86MetadataExactQuery){
                .key = alias_key,
                .operands = alias_operands,
                .operand_count = alias_query.operand_count,
                .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
                .attributes = {.lock = true},
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
                .output = checked_bytes,
                .output_capacity = BUSTER_ARRAY_LENGTH(checked_bytes),
            });
            BusterX86MetadataEmitResult lock_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = lock_query,
                .form_id = test_case.lock_form_id,
                .output = lock_bytes,
                .output_capacity = BUSTER_ARRAY_LENGTH(lock_bytes),
            });
            lock_aliases_valid &= alias_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                  checked_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                  lock_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                  alias_result.byte_count == test_case.byte_count &&
                                  checked_result.byte_count == test_case.byte_count &&
                                  lock_result.byte_count == test_case.byte_count &&
                                  memcmp(alias_bytes, test_case.bytes, test_case.byte_count) == 0 &&
                                  memcmp(checked_bytes, test_case.bytes, test_case.byte_count) == 0 &&
                                  memcmp(lock_bytes, test_case.bytes, test_case.byte_count) == 0;
        }
        BUSTER_TEST(arguments, lock_aliases_valid);

        // Register-only CMPXCHG rows are not lockable memory atomics.  Keep
        // both checked entry points fail-closed and leave caller output alone.
        bool register_lock_rejected = true;
        static u32 const register_form_ids[] = {10279, 10282};
        for (u32 form_index = 0; form_index < BUSTER_ARRAY_LENGTH(register_form_ids); form_index += 1)
        {
            u32 form_id = register_form_ids[form_index];
            BusterX86MetadataFormKey key = {0};
            BusterX86MetadataPhysicalOperand operands[16] = {0};
            String8 features[1] = {0};
            char8 mnemonic[128] = {0};
            BusterX86MetadataPhysicalQuery physical = {0};
            bool ready = buster_x86_metadata_form_key(form_id, &key) &&
                         buster_x86_metadata_test_canonical_query(form_id, &physical, operands, features, mnemonic);
            register_lock_rejected &= ready;
            if (!ready) continue;
            u8 direct_output = 0xa5;
            physical.features.names = wildcard_features;
            physical.features.count = BUSTER_ARRAY_LENGTH(wildcard_features);
            physical.attributes.lock = true;
            BusterX86MetadataEmitResult direct_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = physical, .form_id = form_id, .output = &direct_output, .output_capacity = 1});
            u8 checked_output = 0x5a;
            BusterX86MetadataEmitResult checked_result = buster_x86_metadata_emit_exact_query((BusterX86MetadataExactQuery){
                .key = key,
                .operands = operands,
                .operand_count = physical.operand_count,
                .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
                .attributes = {.lock = true},
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
                .output = &checked_output,
                .output_capacity = 1,
            });
            register_lock_rejected &= direct_result.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION &&
                                      checked_result.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION &&
                                      direct_output == 0xa5 && checked_output == 0x5a;
        }
        BUSTER_TEST(arguments, register_lock_rejected);

        // Exercise the machine-token bridge's dynamic force-lock policy on a
        // prepared non-lockable form.  The positive alias token cases are
        // installed by the atomic machine migration, while this rejection
        // proves the bridge cannot manufacture F0 for ordinary MOV rows.
        BusterX86MetadataFormKey mov_key = {0};
        BusterX86MetadataExactPlan mov_plan = {0};
        BusterX86MetadataPhysicalOperand mov_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64),
        };
        BusterX86MetadataMachineExactToken mov_token = {0};
        bool machine_ready = buster_x86_metadata_form_key(9842, &mov_key) &&
                             buster_x86_metadata_exact_plan_for_key(mov_key, &mov_plan) &&
                             buster_x86_metadata_machine_exact_token_for_plan(
                                 mov_plan, (BusterX86MetadataFeatureInput){.names = wildcard_features, .count = 1}, &mov_token);
        u8 machine_output = 0xc3;
        BusterX86MetadataEmitResult machine_result = machine_ready
                                                          ? buster_x86_metadata_emit_exact_machine(
                                                                mov_token, (BusterX86MetadataMachineExactQuery){
                                                                               .operands = mov_operands,
                                                                               .operand_count = BUSTER_ARRAY_LENGTH(mov_operands),
                                                                               .force_lock = true,
                                                                               .output = &machine_output,
                                                                               .output_capacity = 1})
                                                          : (BusterX86MetadataEmitResult){.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT};
        BUSTER_TEST(arguments, machine_ready && machine_result.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION &&
                                   machine_output == 0xc3);
        u8 invalid_mask_output = 0xc4;
        BusterX86MetadataEmitResult invalid_mask_result = machine_ready
                                                              ? buster_x86_metadata_emit_exact_machine(
                                                                    mov_token, (BusterX86MetadataMachineExactQuery){
                                                                                   .operands = mov_operands,
                                                                                   .operand_count = BUSTER_ARRAY_LENGTH(mov_operands),
                                                                                   .mask_register_plus_one = 9,
                                                                                   .output = &invalid_mask_output,
                                                                                   .output_capacity = 1})
                                                              : (BusterX86MetadataEmitResult){.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT};
        u8 invalid_zeroing_output = 0xc5;
        BusterX86MetadataEmitResult invalid_zeroing_result = machine_ready
                                                                 ? buster_x86_metadata_emit_exact_machine(
                                                                       mov_token, (BusterX86MetadataMachineExactQuery){
                                                                                      .operands = mov_operands,
                                                                                      .operand_count = BUSTER_ARRAY_LENGTH(mov_operands),
                                                                                      .zeroing = true,
                                                                                      .output = &invalid_zeroing_output,
                                                                                      .output_capacity = 1})
                                                                 : (BusterX86MetadataEmitResult){.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT};
        BUSTER_TEST(arguments, machine_ready && invalid_mask_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   invalid_mask_output == 0xc4 &&
                                   invalid_zeroing_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   invalid_zeroing_output == 0xc5);
    }

    {
        // Public checked EVEX queries retain their full decorator ABI.  Keep
        // these cases separate from the machine bridge so worker lanes never
        // prepare a new exact plan; the machine-token positive fixture is
        // installed by the atomic family migration.
        typedef struct X86_64MetadataCheckedMaskCase X86_64MetadataCheckedMaskCase;
        struct X86_64MetadataCheckedMaskCase
        {
            BusterX86MetadataPhysicalAttributes attributes;
            BusterX86MetadataEncodeStatus status;
            u8 byte_count;
            u8 bytes[6];
        };
        static X86_64MetadataCheckedMaskCase const checked_mask_cases[] = {
            {{0}, BUSTER_X86_METADATA_ENCODE_SUCCESS, 6, {0x62, 0xf1, 0x7d, 0x08, 0x6f, 0xc1}},
            {{.decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK, .has_mask_register = true, .mask_register = 1},
             BUSTER_X86_METADATA_ENCODE_SUCCESS, 6, {0x62, 0xf1, 0x7d, 0x09, 0x6f, 0xc1}},
            {{.decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK | BUSTER_X86_METADATA_DECORATOR_ZEROING,
              .has_mask_register = true,
              .mask_register = 1,
              .zeroing = true},
             BUSTER_X86_METADATA_ENCODE_SUCCESS, 6, {0x62, 0xf1, 0x7d, 0x89, 0x6f, 0xc1}},
            {{.decorator_flags = BUSTER_X86_METADATA_DECORATOR_ZEROING, .zeroing = true},
             BUSTER_X86_METADATA_ENCODE_INVALID_INPUT, 0, {0}},
            {{.decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK, .has_mask_register = true, .mask_register = 8},
             BUSTER_X86_METADATA_ENCODE_INVALID_INPUT, 0, {0}},
        };
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataFormKey evex_key = {0};
        BusterX86MetadataPhysicalOperand evex_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        bool checked_mask_cases_valid = buster_x86_metadata_form_key(5583, &evex_key);
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(checked_mask_cases); case_index += 1)
        {
            X86_64MetadataCheckedMaskCase test_case = checked_mask_cases[case_index];
            u8 output[16];
            memset(output, 0xa7, sizeof(output));
            BusterX86MetadataEmitResult checked_result = checked_mask_cases_valid
                                                              ? x86_64_metadata_test_emit_exact_query(
                                                                    evex_key, evex_operands, BUSTER_ARRAY_LENGTH(evex_operands),
                                                                    test_case.attributes, wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features),
                                                                    output, BUSTER_ARRAY_LENGTH(output), 0, 0)
                                                              : (BusterX86MetadataEmitResult){.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT};
            bool bytes_match = test_case.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                               checked_result.byte_count == test_case.byte_count &&
                               memcmp(output, test_case.bytes, test_case.byte_count) == 0;
            bool invalid_untouched = test_case.status != BUSTER_X86_METADATA_ENCODE_SUCCESS && output[0] == 0xa7;
            checked_mask_cases_valid &= checked_result.status == test_case.status && (bytes_match || invalid_untouched);
        }
        BUSTER_TEST(arguments, checked_mask_cases_valid);
    }

    {
        // A folded qword data operand carries the width on the memory
        // binding, not on a GPR binding.  Keep the checked and prepared exact
        // paths differential here: both must derive REX.W for SUB r/m64,
        // imm8 and retain the extended base bit for R8.
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand operands[2] = {
            x86_64_metadata_test_physical_mem_base(8, 64, 0),
            x86_64_metadata_test_physical_imm(1, 8),
        };
        BusterX86MetadataFormKey key = {0};
        bool key_ready = buster_x86_metadata_form_key(9330, &key);
        u8 checked_bytes[16] = {0};
        u8 exact_bytes[16] = {0};
        BusterX86MetadataEmitResult checked = x86_64_metadata_test_emit_form(
            S8("SUB"), 9330, operands, BUSTER_ARRAY_LENGTH(operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
            BUSTER_ARRAY_LENGTH(wildcard_features), checked_bytes, BUSTER_ARRAY_LENGTH(checked_bytes), 0, 0);
        BusterX86MetadataEmitResult exact = key_ready
                                                ? x86_64_metadata_test_emit_named_exact(
                                                      key, operands, BUSTER_ARRAY_LENGTH(operands),
                                                      (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                                      BUSTER_ARRAY_LENGTH(wildcard_features), exact_bytes,
                                                      BUSTER_ARRAY_LENGTH(exact_bytes), 0, 0)
                                                : (BusterX86MetadataEmitResult){.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT};
        static u8 const expected[] = {0x49, 0x83, 0x28, 0x01};
        bool sub_qword_rexw = key_ready && checked.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                              exact.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                              checked.byte_count == BUSTER_ARRAY_LENGTH(expected) && exact.byte_count == checked.byte_count &&
                              memcmp(checked_bytes, expected, BUSTER_ARRAY_LENGTH(expected)) == 0 &&
                              memcmp(exact_bytes, expected, BUSTER_ARRAY_LENGTH(expected)) == 0;
        BUSTER_TEST(arguments, sub_qword_rexw);
    }

    {
        // MOVSXD uses a `norexw_prefix` XED row, but a 64-bit GPRz
        // destination still requires REX.W.  Keep the generic checked path
        // and its prepared exact projection byte-identical.
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_mem_base(6, 32, 0),
        };
        BusterX86MetadataFormKey key = {0};
        bool key_ready = buster_x86_metadata_form_key(9740, &key);
        u8 checked_bytes[16] = {0};
        u8 exact_bytes[16] = {0};
        BusterX86MetadataEmitResult checked = x86_64_metadata_test_emit_form(
            S8("MOVSXD"), 9740, operands, BUSTER_ARRAY_LENGTH(operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
            BUSTER_ARRAY_LENGTH(wildcard_features), checked_bytes, BUSTER_ARRAY_LENGTH(checked_bytes), 0, 0);
        BusterX86MetadataEmitResult exact = key_ready
                                                ? x86_64_metadata_test_emit_named_exact(
                                                      key, operands, BUSTER_ARRAY_LENGTH(operands),
                                                      (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                                      BUSTER_ARRAY_LENGTH(wildcard_features), exact_bytes,
                                                      BUSTER_ARRAY_LENGTH(exact_bytes), 0, 0)
                                                : (BusterX86MetadataEmitResult){.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT};
        static u8 const expected[] = {0x48, 0x63, 0x06};
        bool movsxd_rexw = key_ready && checked.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                           exact.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && checked.byte_count == BUSTER_ARRAY_LENGTH(expected) &&
                           exact.byte_count == checked.byte_count && memcmp(checked_bytes, expected, BUSTER_ARRAY_LENGTH(expected)) == 0 &&
                           memcmp(exact_bytes, expected, BUSTER_ARRAY_LENGTH(expected)) == 0;
        BUSTER_TEST(arguments, movsxd_rexw);
    }

    {
        // Source selection must skip XED's undocumented duplicate SHL /6
        // row.  The architectural memory form uses ModRM /4 and therefore
        // emits 48 d1 64 24 08 for `shlq $1, 8(%rsp)`.
        BusterX86MetadataPhysicalOperand operands[2] = {
            x86_64_metadata_test_physical_mem_base(4, 64, 8),
            x86_64_metadata_test_physical_imm(1, 8),
        };
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalQuery source_query = x86_64_metadata_test_physical_query(
            S8("shl"), operands, BUSTER_ARRAY_LENGTH(operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
            BUSTER_ARRAY_LENGTH(wildcard_features));
        source_query.source_semantics = true;
        BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(source_query);
        BusterX86MetadataForm selected_form = {0};
        bool selected_form_ready = selected.form_id != UINT32_MAX && buster_x86_metadata_form(selected.form_id, &selected_form);
        u8 output[16] = {0};
        BusterX86MetadataEmitResult emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = source_query,
            .output = output,
            .output_capacity = BUSTER_ARRAY_LENGTH(output),
        });
        static u8 const expected[] = {0x48, 0xd1, 0x64, 0x24, 0x08};
        bool canonical_shl = selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && selected_form_ready &&
                             !x86_64_metadata_test_string_contains(selected_form.attributes, S8("UNDOCUMENTED")) &&
                             emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.byte_count == BUSTER_ARRAY_LENGTH(expected) &&
                             memcmp(output, expected, BUSTER_ARRAY_LENGTH(expected)) == 0;
        BUSTER_TEST(arguments, canonical_shl);
    }

    {
        // CL is represented as an implicit XED operand, but the checked
        // physical interface accepts the source-visible control register.
        // Keep both source and codegen-style projections on the canonical
        // D3 /4 spelling.
        BusterX86MetadataPhysicalOperand operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 8),
        };
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalQuery query = x86_64_metadata_test_physical_query(
            S8("shl"), operands, BUSTER_ARRAY_LENGTH(operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
            BUSTER_ARRAY_LENGTH(wildcard_features));
        u8 source_bytes[16] = {0};
        BusterX86MetadataEmitResult source = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = (BusterX86MetadataPhysicalQuery){
                .mnemonic = query.mnemonic,
                .operands = query.operands,
                .operand_count = query.operand_count,
                .features = query.features,
                .attributes = query.attributes,
                .address_size = query.address_size,
                .execution_mode = query.execution_mode,
                .source_semantics = true,
            },
            .output = source_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(source_bytes),
        });
        u8 codegen_bytes[16] = {0};
        query.source_semantics = false;
        BusterX86MetadataEmitResult codegen = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = query,
            .output = codegen_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(codegen_bytes),
        });
        static u8 const expected[] = {0x48, 0xd3, 0xe0};
        bool shift_cl = source.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                        codegen.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                        source.byte_count == BUSTER_ARRAY_LENGTH(expected) && codegen.byte_count == source.byte_count &&
                        memcmp(source_bytes, expected, BUSTER_ARRAY_LENGTH(expected)) == 0 &&
                        memcmp(codegen_bytes, expected, BUSTER_ARRAY_LENGTH(expected)) == 0;
        BUSTER_TEST(arguments, shift_cl);

        // Codegen queries intentionally carry no feature list.  A fixed
        // low-GPR shift must stay on the baseline D3/C1/D1 rows; the APX
        // NDD siblings have the same physical arity when CL or ONE is hidden
        // and must not preempt the legacy candidate with a feature failure.
        BusterX86MetadataPhysicalOperand no_feature_cl_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 8),
        };
        BusterX86MetadataPhysicalOperand no_feature_imm_operands[2] = {
            no_feature_cl_operands[0],
            x86_64_metadata_test_physical_imm(5, 8),
        };
        BusterX86MetadataPhysicalOperand no_feature_one_operands[2] = {
            no_feature_cl_operands[0],
            x86_64_metadata_test_physical_imm(1, 8),
        };
        BusterX86MetadataPhysicalQuery no_feature_cl_query = x86_64_metadata_test_physical_query(
            S8("SHL"), no_feature_cl_operands, BUSTER_ARRAY_LENGTH(no_feature_cl_operands),
            (BusterX86MetadataPhysicalAttributes){0}, 0, 0);
        BusterX86MetadataPhysicalQuery no_feature_imm_query = x86_64_metadata_test_physical_query(
            S8("SHL"), no_feature_imm_operands, BUSTER_ARRAY_LENGTH(no_feature_imm_operands),
            (BusterX86MetadataPhysicalAttributes){0}, 0, 0);
        BusterX86MetadataPhysicalQuery no_feature_one_query = x86_64_metadata_test_physical_query(
            S8("SHL"), no_feature_one_operands, BUSTER_ARRAY_LENGTH(no_feature_one_operands),
            (BusterX86MetadataPhysicalAttributes){0}, 0, 0);
        u8 no_feature_cl_bytes[8] = {0};
        u8 no_feature_imm_bytes[8] = {0};
        u8 no_feature_one_bytes[8] = {0};
        BusterX86MetadataEmitResult no_feature_cl = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = no_feature_cl_query, .output = no_feature_cl_bytes,
                                           .output_capacity = BUSTER_ARRAY_LENGTH(no_feature_cl_bytes)});
        BusterX86MetadataEmitResult no_feature_imm = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = no_feature_imm_query, .output = no_feature_imm_bytes,
                                           .output_capacity = BUSTER_ARRAY_LENGTH(no_feature_imm_bytes)});
        BusterX86MetadataEmitResult no_feature_one = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = no_feature_one_query, .output = no_feature_one_bytes,
                                           .output_capacity = BUSTER_ARRAY_LENGTH(no_feature_one_bytes)});
        static u8 const no_feature_cl_expected[] = {0x48, 0xd3, 0xe0};
        static u8 const no_feature_imm_expected[] = {0x48, 0xc1, 0xe0, 0x05};
        static u8 const no_feature_one_expected[] = {0x48, 0xd1, 0xe0};
        bool no_feature_shifts = no_feature_cl.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && no_feature_cl.form_id == 9428 &&
                                 no_feature_cl.byte_count == BUSTER_ARRAY_LENGTH(no_feature_cl_expected) &&
                                 memcmp(no_feature_cl_bytes, no_feature_cl_expected, BUSTER_ARRAY_LENGTH(no_feature_cl_expected)) == 0 &&
                                 no_feature_imm.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && no_feature_imm.form_id == 9380 &&
                                 no_feature_imm.byte_count == BUSTER_ARRAY_LENGTH(no_feature_imm_expected) &&
                                 memcmp(no_feature_imm_bytes, no_feature_imm_expected, BUSTER_ARRAY_LENGTH(no_feature_imm_expected)) == 0 &&
                                 no_feature_one.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && no_feature_one.form_id == 9407 &&
                                 no_feature_one.byte_count == BUSTER_ARRAY_LENGTH(no_feature_one_expected) &&
                                 memcmp(no_feature_one_bytes, no_feature_one_expected, BUSTER_ARRAY_LENGTH(no_feature_one_expected)) == 0;
        BUSTER_TEST(arguments, no_feature_shifts);
    }

    {
        // Scalar SSE memory width is an element-size selector, not a REX.W
        // request.  MOVSD's qword load/store therefore retain the canonical
        // F2 0F 10/11 spelling even though the physical memory operand is
        // 64 bits wide.
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand load_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 64),
            x86_64_metadata_test_physical_mem_base(5, 64, 8),
        };
        BusterX86MetadataPhysicalOperand store_operands[2] = {
            load_operands[1],
            load_operands[0],
        };
        BusterX86MetadataPhysicalQuery load_query = x86_64_metadata_test_physical_query(
            S8("MOVSD"), load_operands, BUSTER_ARRAY_LENGTH(load_operands), (BusterX86MetadataPhysicalAttributes){0},
            wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features));
        BusterX86MetadataPhysicalQuery store_query = x86_64_metadata_test_physical_query(
            S8("MOVSD"), store_operands, BUSTER_ARRAY_LENGTH(store_operands), (BusterX86MetadataPhysicalAttributes){0},
            wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features));
        u8 load_output[16] = {0};
        u8 store_output[16] = {0};
        BusterX86MetadataSelectResult load_selected = buster_x86_metadata_select_form(load_query);
        BusterX86MetadataSelectResult store_selected = buster_x86_metadata_select_form(store_query);
        BusterX86MetadataEmitResult load_emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = load_query, .output = load_output, .output_capacity = BUSTER_ARRAY_LENGTH(load_output)});
        BusterX86MetadataEmitResult store_emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = store_query, .output = store_output, .output_capacity = BUSTER_ARRAY_LENGTH(store_output)});
        static u8 const expected_load[] = {0xf2, 0x0f, 0x10, 0x45, 0x08};
        static u8 const expected_store[] = {0xf2, 0x0f, 0x11, 0x45, 0x08};
        bool movsd_memory = load_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                            store_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                            load_emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                            store_emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                            load_emitted.byte_count == BUSTER_ARRAY_LENGTH(expected_load) &&
                            store_emitted.byte_count == BUSTER_ARRAY_LENGTH(expected_store) &&
                            memcmp(load_output, expected_load, BUSTER_ARRAY_LENGTH(expected_load)) == 0 &&
                            memcmp(store_output, expected_store, BUSTER_ARRAY_LENGTH(expected_store)) == 0;
        BUSTER_TEST(arguments, movsd_memory);
    }

    {
        // XCHG's accumulator opcode+rd forms expose only the non-accumulator
        // register in XED.  The checked selector projects a two-register
        // source pair onto that hidden-accumulator form and keeps generic
        // ModRM bytes for non-accumulator pairs (and for byte XCHG).
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand operands[2] = {0};
        BusterX86MetadataPhysicalQuery query = {0};
        u8 output[16] = {0};
        BusterX86MetadataEmitResult emitted = {0};
        BusterX86MetadataSelectResult selected = {0};
        static u8 const expected_ax_cx[] = {0x66, 0x91};
        static u8 const expected_eax_ebx[] = {0x93};
        static u8 const expected_rax_r8[] = {0x49, 0x90};
        static u8 const expected_rcx_rdx[] = {0x48, 0x87, 0xca};
        operands[0] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 16);
        operands[1] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 16);
        query = x86_64_metadata_test_physical_query(S8("XCHG"), operands, 2,
                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                                    BUSTER_ARRAY_LENGTH(wildcard_features));
        selected = buster_x86_metadata_select_form(query);
        emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = query, .output = output, .output_capacity = BUSTER_ARRAY_LENGTH(output)});
        bool xchg_ax_cx = selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                          emitted.byte_count == BUSTER_ARRAY_LENGTH(expected_ax_cx) &&
                          memcmp(output, expected_ax_cx, BUSTER_ARRAY_LENGTH(expected_ax_cx)) == 0;
        BUSTER_TEST(arguments, xchg_ax_cx);

        operands[0] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 16);
        operands[1] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 16);
        query = x86_64_metadata_test_physical_query(S8("XCHG"), operands, 2,
                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                                    BUSTER_ARRAY_LENGTH(wildcard_features));
        selected = buster_x86_metadata_select_form(query);
        emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = query, .output = output, .output_capacity = BUSTER_ARRAY_LENGTH(output)});
        bool xchg_cx_ax = selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                          emitted.byte_count == BUSTER_ARRAY_LENGTH(expected_ax_cx) &&
                          memcmp(output, expected_ax_cx, BUSTER_ARRAY_LENGTH(expected_ax_cx)) == 0;
        BUSTER_TEST(arguments, xchg_cx_ax);

        operands[0] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32);
        operands[1] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 3, 32);
        query = x86_64_metadata_test_physical_query(S8("XCHG"), operands, 2,
                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                                    BUSTER_ARRAY_LENGTH(wildcard_features));
        selected = buster_x86_metadata_select_form(query);
        emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = query, .output = output, .output_capacity = BUSTER_ARRAY_LENGTH(output)});
        bool xchg_eax_ebx = selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                            emitted.byte_count == BUSTER_ARRAY_LENGTH(expected_eax_ebx) &&
                            memcmp(output, expected_eax_ebx, BUSTER_ARRAY_LENGTH(expected_eax_ebx)) == 0;
        BUSTER_TEST(arguments, xchg_eax_ebx);

        operands[0] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        operands[1] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 64);
        query = x86_64_metadata_test_physical_query(S8("XCHG"), operands, 2,
                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                                    BUSTER_ARRAY_LENGTH(wildcard_features));
        selected = buster_x86_metadata_select_form(query);
        emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = query, .output = output, .output_capacity = BUSTER_ARRAY_LENGTH(output)});
        bool xchg_rax_r8 = selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                           emitted.byte_count == BUSTER_ARRAY_LENGTH(expected_rax_r8) &&
                           memcmp(output, expected_rax_r8, BUSTER_ARRAY_LENGTH(expected_rax_r8)) == 0;
        BUSTER_TEST(arguments, xchg_rax_r8);

        operands[0] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64);
        operands[1] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64);
        query = x86_64_metadata_test_physical_query(S8("XCHG"), operands, 2,
                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                                    BUSTER_ARRAY_LENGTH(wildcard_features));
        selected = buster_x86_metadata_select_form(query);
        emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = query, .output = output, .output_capacity = BUSTER_ARRAY_LENGTH(output)});
        bool xchg_generic = selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                            emitted.byte_count == BUSTER_ARRAY_LENGTH(expected_rcx_rdx) &&
                            memcmp(output, expected_rcx_rdx, BUSTER_ARRAY_LENGTH(expected_rcx_rdx)) == 0;
        BUSTER_TEST(arguments, xchg_generic);
    }

    {
        // A non-sign-extended 64-bit literal must select MOV's B8+rd
        // immediate form instead of failing the shorter C7 imm32 row.
        BusterX86MetadataPhysicalOperand operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_imm_u64(UINT64_C(0x1122334455667788), 64),
        };
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalQuery query = x86_64_metadata_test_physical_query(
            S8("mov"), operands, BUSTER_ARRAY_LENGTH(operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
            BUSTER_ARRAY_LENGTH(wildcard_features));
        u8 output[16] = {0};
        BusterX86MetadataEmitResult emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = query,
            .output = output,
            .output_capacity = BUSTER_ARRAY_LENGTH(output),
        });
        static u8 const expected[] = {0x48, 0xb8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        bool mov_imm64 = emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.byte_count == BUSTER_ARRAY_LENGTH(expected) &&
                         memcmp(output, expected, BUSTER_ARRAY_LENGTH(expected)) == 0;
        BUSTER_TEST(arguments, mov_imm64);
    }

    {
        // MMX MOVQ memory transfers use 0F6F/0F7F, never the REX.W GPR
        // aliases 0F6E/0F7E.  Width inference for MMX must also avoid a
        // synthetic REX.W on PADDQ's qword memory source.
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand load_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX, 0, 64),
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
        };
        load_operands[1].memory.address_size = 32;
        load_operands[1].memory.base.width = 32;
        BusterX86MetadataPhysicalQuery load_query = x86_64_metadata_test_physical_query(
            S8("movq"), load_operands, BUSTER_ARRAY_LENGTH(load_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
            BUSTER_ARRAY_LENGTH(wildcard_features));
        load_query.address_size = 32;
        u8 load_bytes[16] = {0};
        BusterX86MetadataEmitResult load = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = load_query,
            .output = load_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(load_bytes),
        });

        BusterX86MetadataPhysicalOperand store_operands[2] = {
            load_operands[1],
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX, 0, 64),
        };
        BusterX86MetadataPhysicalQuery store_query = x86_64_metadata_test_physical_query(
            S8("movq"), store_operands, BUSTER_ARRAY_LENGTH(store_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
            BUSTER_ARRAY_LENGTH(wildcard_features));
        store_query.address_size = 32;
        u8 store_bytes[16] = {0};
        BusterX86MetadataEmitResult store = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = store_query,
            .output = store_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(store_bytes),
        });

        BusterX86MetadataPhysicalOperand paddq_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX, 0, 64),
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
        };
        BusterX86MetadataPhysicalQuery paddq_query = x86_64_metadata_test_physical_query(
            S8("paddq"), paddq_operands, BUSTER_ARRAY_LENGTH(paddq_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
            BUSTER_ARRAY_LENGTH(wildcard_features));
        u8 paddq_bytes[16] = {0};
        BusterX86MetadataEmitResult paddq = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = paddq_query,
            .output = paddq_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(paddq_bytes),
        });
        static u8 const expected_load[] = {0x67, 0x0f, 0x6f, 0x00};
        static u8 const expected_store[] = {0x67, 0x0f, 0x7f, 0x00};
        static u8 const expected_paddq[] = {0x0f, 0xd4, 0x00};
        bool mmx_memory = load.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                          load.byte_count == BUSTER_ARRAY_LENGTH(expected_load) &&
                          memcmp(load_bytes, expected_load, BUSTER_ARRAY_LENGTH(expected_load)) == 0 &&
                          store.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                          store.byte_count == BUSTER_ARRAY_LENGTH(expected_store) &&
                          memcmp(store_bytes, expected_store, BUSTER_ARRAY_LENGTH(expected_store)) == 0 &&
                          paddq.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                          paddq.byte_count == BUSTER_ARRAY_LENGTH(expected_paddq) &&
                          memcmp(paddq_bytes, expected_paddq, BUSTER_ARRAY_LENGTH(expected_paddq)) == 0;
        BUSTER_TEST(arguments, mmx_memory);
    }

    {
        // VEX vector width is encoded by the metadata W bit, not inferred
        // from a qword memory operand.  VMOVAPD's YMM store therefore keeps
        // the canonical W=0 prefix (c4 41 7d), even though the physical
        // memory width is 64 bits in this folded-width probe.
        BusterX86MetadataPhysicalOperand operands[2] = {
            x86_64_metadata_test_physical_mem_base(13, 64, 0),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 15, 256),
        };
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalQuery query = x86_64_metadata_test_physical_query(
            S8("vmovapd"), operands, BUSTER_ARRAY_LENGTH(operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
            BUSTER_ARRAY_LENGTH(wildcard_features));
        u8 output[16] = {0};
        BusterX86MetadataEmitResult emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = query,
            .output = output,
            .output_capacity = BUSTER_ARRAY_LENGTH(output),
        });
        static u8 const expected[] = {0xc4, 0x41, 0x7d, 0x29, 0x7d, 0x00};
        bool vmovapd_store = emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                             emitted.byte_count == BUSTER_ARRAY_LENGTH(expected) &&
                             memcmp(output, expected, BUSTER_ARRAY_LENGTH(expected)) == 0;
        BUSTER_TEST(arguments, vmovapd_store);
    }

    {
        // The sparse plan table is populated by the serial prewarm contract;
        // each representative migrated shape must remain byte/result
        // equivalent to the checked exact query.
        String8 wildcard_features[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand plan_mov_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64),
        };
        BusterX86MetadataPhysicalOperand plan_add_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32),
        };
        BusterX86MetadataPhysicalOperand plan_jmp_operand = x86_64_metadata_test_physical_relative(-5, 32);
        BusterX86MetadataPhysicalOperand plan_lea_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_mem_base(3, 64, 0),
        };
        BusterX86MetadataFormKey mfence_plan_key = {0};
        BusterX86MetadataFormKey int3_plan_key = {0};
        BusterX86MetadataFormKey mov_plan_key = {0};
        BusterX86MetadataFormKey add_plan_key = {0};
        BusterX86MetadataFormKey jmp_plan_key = {0};
        BusterX86MetadataFormKey lea_plan_key = {0};
        bool plan_keys_ready = buster_x86_metadata_form_key(9610, &mfence_plan_key) &&
                               buster_x86_metadata_form_key(10027, &int3_plan_key) &&
                               buster_x86_metadata_form_key(9842, &mov_plan_key) &&
                               buster_x86_metadata_form_key(9620, &add_plan_key) &&
                               buster_x86_metadata_form_key(10060, &jmp_plan_key) &&
                               buster_x86_metadata_form_key(9849, &lea_plan_key);
        BUSTER_TEST(arguments, plan_keys_ready);
        // Composite conversion families add their own durable sequence-step
        // forms.  They must remain addressable after direct rows, family
        // variants, and earlier sequence steps have occupied the sparse plan
        // table; an implementation storage cap must not silently drop these
        // valid keys during serial prewarm.  These are the current conversion
        // steps used by the machine-family registry (not retired prototype
        // rows from the earlier scalar expansion).
        static BusterX86MetadataFormKey const conversion_plan_keys[] = {
            {10436u, UINT64_C(0x8632f672b995b8ef)},
            {10463u, UINT64_C(0x6b1b449f0ed1e10e)},
            {10440u, UINT64_C(0x443fee16922dd4d2)},
            {10467u, UINT64_C(0xdb7e2aa7c56853cf)},
        };
        bool conversion_plans_ready = true;
        for (u32 conversion_index = 0; conversion_index < BUSTER_ARRAY_LENGTH(conversion_plan_keys); conversion_index += 1)
        {
            BusterX86MetadataExactPlan conversion_plan = {0};
            conversion_plans_ready &= buster_x86_metadata_exact_plan_for_key(conversion_plan_keys[conversion_index], &conversion_plan);
        }
        BUSTER_TEST(arguments, conversion_plans_ready);
        BUSTER_TEST(arguments, buster_x86_metadata_test_exact_plan_count() > 64);
        if (plan_keys_ready)
        {
            BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                       mfence_plan_key, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                       BUSTER_ARRAY_LENGTH(wildcard_features)));
            BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                       int3_plan_key, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                       BUSTER_ARRAY_LENGTH(wildcard_features)));
            BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                       mov_plan_key, plan_mov_operands, BUSTER_ARRAY_LENGTH(plan_mov_operands),
                                       (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                       BUSTER_ARRAY_LENGTH(wildcard_features)));
            BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                       add_plan_key, plan_add_operands, BUSTER_ARRAY_LENGTH(plan_add_operands),
                                       (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                       BUSTER_ARRAY_LENGTH(wildcard_features)));
            BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                       jmp_plan_key, &plan_jmp_operand, 1, (BusterX86MetadataPhysicalAttributes){0},
                                       wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features)));
            BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                       lea_plan_key, plan_lea_operands, BUSTER_ARRAY_LENGTH(plan_lea_operands),
                                       (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                       BUSTER_ARRAY_LENGTH(wildcard_features)));
            BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_missing_feature(mfence_plan_key));

            // VZEROALL and VZEROUPPER are direct machine zero-operand rows,
            // but are not yet selected by a registry recipe.  Keep their
            // durable identities checked while exercising the public checked
            // transform directly; exact-plan preparation belongs exclusively
            // to the serial machine prewarm path.
            BusterX86MetadataFormKey vzeroall_key = {0};
            BusterX86MetadataFormKey vzeroupper_key = {0};
            bool vzero_keys_ready =
                buster_x86_metadata_form_key(3202u, &vzeroall_key) &&
                buster_x86_metadata_form_key(3203u, &vzeroupper_key) &&
                vzeroall_key.stable_hash == UINT64_C(0xbf8c4d3212ddd41d) &&
                vzeroupper_key.stable_hash == UINT64_C(0xca45be9dfde7d0ad);
            BUSTER_TEST(arguments, vzero_keys_ready);
            static u8 const vzeroall_bytes[] = {0xc5, 0xfc, 0x77};
            static u8 const vzeroupper_bytes[] = {0xc5, 0xf8, 0x77};
            if (vzero_keys_ready)
            {
                BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(
                                           S8("VZEROALL"), vzeroall_key.form_id, 0, 0,
                                           (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                           BUSTER_ARRAY_LENGTH(wildcard_features), vzeroall_bytes,
                                           BUSTER_ARRAY_LENGTH(vzeroall_bytes)));
                BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(
                                           S8("VZEROUPPER"), vzeroupper_key.form_id, 0, 0,
                                           (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                           BUSTER_ARRAY_LENGTH(wildcard_features), vzeroupper_bytes,
                                           BUSTER_ARRAY_LENGTH(vzeroupper_bytes)));
            }

            // The prepared machine projection covers the scalar layouts that
            // dominate the direct/FAMILY bridge: register-register,
            // immediate, and base-only memory forms.  Keep a displacement
            // that naturally selects disp32 so the force-disp32 machine
            // policy is checked against the same generic bytes as well.
            BusterX86MetadataFormKey add_imm8_key = {9316u, UINT64_C(0xc4d75f09ceeb4f69)};
            BusterX86MetadataFormKey mov_memory_key = {9845u, UINT64_C(0xca30e68cfa1406bc)};
            BusterX86MetadataFormKey mov_memory8_key = {9840u, UINT64_C(0xe7a77cae08617d2d)};
            BusterX86MetadataPhysicalOperand add_imm8_operands[2] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
                x86_64_metadata_test_physical_imm(7, 8),
            };
            BusterX86MetadataPhysicalOperand mov_memory_operands[2] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
                x86_64_metadata_test_physical_mem_base(3, 64, 128),
            };
            BusterX86MetadataPhysicalOperand mov_memory8_operands[2] = {
                x86_64_metadata_test_physical_mem_base(3, 8, 128),
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8),
            };
            BusterX86MetadataPhysicalOperand mov_rsp_zero_operands[2] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 10, 64),
                x86_64_metadata_test_physical_mem_base(4, 64, 0),
            };
            BusterX86MetadataPhysicalOperand mov_rsp_disp8_operands[2] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 10, 64),
                x86_64_metadata_test_physical_mem_base(4, 64, 16),
            };
            BusterX86MetadataPhysicalOperand mov_rsp_disp32_operands[2] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 10, 64),
                x86_64_metadata_test_physical_mem_base(4, 64, 128),
            };
            BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                       add_imm8_key, add_imm8_operands, BUSTER_ARRAY_LENGTH(add_imm8_operands), false));
            BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                       mov_memory_key, mov_memory_operands, BUSTER_ARRAY_LENGTH(mov_memory_operands), true));
            BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                       mov_memory8_key, mov_memory8_operands, BUSTER_ARRAY_LENGTH(mov_memory8_operands), true));
            BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                       mov_memory_key, mov_rsp_zero_operands, BUSTER_ARRAY_LENGTH(mov_rsp_zero_operands), false));
            BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                       mov_memory_key, mov_rsp_disp8_operands, BUSTER_ARRAY_LENGTH(mov_rsp_disp8_operands), false));
            BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                       mov_memory_key, mov_rsp_disp32_operands, BUSTER_ARRAY_LENGTH(mov_rsp_disp32_operands), false));
            static u8 const mov_rsp_zero_bytes[] = {0x4c, 0x8b, 0x14, 0x24};
            static u8 const mov_rsp_disp8_bytes[] = {0x4c, 0x8b, 0x54, 0x24, 0x10};
            static u8 const mov_rsp_disp32_bytes[] = {0x4c, 0x8b, 0x94, 0x24, 0x80, 0x00, 0x00, 0x00};
            BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_expected_bytes(
                                       mov_memory_key, mov_rsp_zero_operands, BUSTER_ARRAY_LENGTH(mov_rsp_zero_operands),
                                       mov_rsp_zero_bytes, BUSTER_ARRAY_LENGTH(mov_rsp_zero_bytes)));
            BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_expected_bytes(
                                       mov_memory_key, mov_rsp_disp8_operands, BUSTER_ARRAY_LENGTH(mov_rsp_disp8_operands),
                                       mov_rsp_disp8_bytes, BUSTER_ARRAY_LENGTH(mov_rsp_disp8_bytes)));
            BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_expected_bytes(
                                       mov_memory_key, mov_rsp_disp32_operands, BUSTER_ARRAY_LENGTH(mov_rsp_disp32_operands),
                                       mov_rsp_disp32_bytes, BUSTER_ARRAY_LENGTH(mov_rsp_disp32_bytes)));

            // DIV/IDIV register rows carry implicit RAX/RDX operands in the
            // metadata schema.  The scalar projection binds only the visible
            // GPR while retaining those hidden fixed registers in the
            // metadata-generated ModRM proof.  Exercise low/high registers
            // at both widths against the checked machine bridge.
            BusterX86MetadataFormKey div_register_key = {0};
            BusterX86MetadataFormKey idiv_register_key = {0};
            bool div_register_keys_ready = buster_x86_metadata_form_key(9468u, &div_register_key) &&
                                           buster_x86_metadata_form_key(9470u, &idiv_register_key) &&
                                           buster_x86_metadata_test_machine_fast_plan(div_register_key.form_id) &&
                                           buster_x86_metadata_test_machine_fast_plan(idiv_register_key.form_id);
            BUSTER_TEST(arguments, div_register_keys_ready);
            if (div_register_keys_ready)
            {
                BusterX86MetadataPhysicalOperand div32_low =
                    x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32);
                BusterX86MetadataPhysicalOperand div32_high =
                    x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 15, 32);
                BusterX86MetadataPhysicalOperand div64_low =
                    x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
                BusterX86MetadataPhysicalOperand div64_high =
                    x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 15, 64);
                BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                           div_register_key, &div32_low, 1, false));
                BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                           div_register_key, &div32_high, 1, false));
                BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                           div_register_key, &div64_low, 1, false));
                BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                           div_register_key, &div64_high, 1, false));
                BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                           idiv_register_key, &div32_low, 1, false));
                BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                           idiv_register_key, &div32_high, 1, false));
                BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                           idiv_register_key, &div64_low, 1, false));
                BUSTER_TEST(arguments, x86_64_metadata_test_machine_fast_scalar_case(
                                           idiv_register_key, &div64_high, 1, false));
            }
            BUSTER_TEST(arguments, buster_x86_metadata_test_machine_fast_plan(jmp_plan_key.form_id));

            // Zero-operand and fixed-width relative rows use the prepared
            // metadata template projection.  Resolve the same rows through
            // metadata selection, then compare checked/prevalidated/machine
            // output so the worker shortcut remains byte-identical without a
            // second opcode authority.
            BusterX86MetadataPhysicalOperand template_jmp_operand = x86_64_metadata_test_physical_relative(-7, 32);
            BusterX86MetadataPhysicalQuery template_jmp_query = {
                .mnemonic = S8("JMP"),
                .operands = &template_jmp_operand,
                .operand_count = 1,
                .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            };
            BusterX86MetadataSelectResult template_jmp_selected = buster_x86_metadata_select_form(template_jmp_query);
            BusterX86MetadataFormKey template_jmp_key = {0};
            bool template_jmp_ready = template_jmp_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                      buster_x86_metadata_form_key(template_jmp_selected.form_id, &template_jmp_key) &&
                                      buster_x86_metadata_test_machine_fast_plan(template_jmp_key.form_id);
            BUSTER_TEST(arguments, template_jmp_ready);
            if (template_jmp_ready)
            {
                BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                           template_jmp_key, &template_jmp_operand, 1, (BusterX86MetadataPhysicalAttributes){0},
                                           wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features)));
                template_jmp_operand.value = INT32_MIN;
                BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                           template_jmp_key, &template_jmp_operand, 1, (BusterX86MetadataPhysicalAttributes){0},
                                           wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features)));
                template_jmp_operand.value = INT32_MAX;
                BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                           template_jmp_key, &template_jmp_operand, 1, (BusterX86MetadataPhysicalAttributes){0},
                                           wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features)));
            }

            BusterX86MetadataPhysicalQuery template_ret_query = {
                .mnemonic = S8("RET"),
                .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            };
            BusterX86MetadataSelectResult template_ret_selected = buster_x86_metadata_select_form(template_ret_query);
            BusterX86MetadataFormKey template_ret_key = {0};
            bool template_ret_ready = template_ret_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                      buster_x86_metadata_form_key(template_ret_selected.form_id, &template_ret_key) &&
                                      buster_x86_metadata_test_machine_fast_plan(template_ret_key.form_id);
            BUSTER_TEST(arguments, template_ret_ready);
            if (template_ret_ready)
            {
                BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                           template_ret_key, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                           BUSTER_ARRAY_LENGTH(wildcard_features)));
            }

            // Conditional short branches exercise both signed displacement
            // boundaries.  The shared exact-plan helper also checks zero
            // output capacity, so these cases cover range and capacity
            // parity for the prepared template path.
            BusterX86MetadataPhysicalOperand template_jcc_operand = x86_64_metadata_test_physical_relative(-128, 8);
            BusterX86MetadataPhysicalQuery template_jcc_query = {
                .mnemonic = S8("JNS"),
                .operands = &template_jcc_operand,
                .operand_count = 1,
                .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            };
            BusterX86MetadataSelectResult template_jcc_selected = buster_x86_metadata_select_form(template_jcc_query);
            BusterX86MetadataFormKey template_jcc_key = {0};
            bool template_jcc_ready = template_jcc_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                      buster_x86_metadata_form_key(template_jcc_selected.form_id, &template_jcc_key) &&
                                      buster_x86_metadata_test_machine_fast_plan(template_jcc_key.form_id);
            BUSTER_TEST(arguments, template_jcc_ready);
            if (template_jcc_ready)
            {
                BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                           template_jcc_key, &template_jcc_operand, 1, (BusterX86MetadataPhysicalAttributes){0},
                                           wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features)));
                template_jcc_operand.value = 127;
                BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                           template_jcc_key, &template_jcc_operand, 1, (BusterX86MetadataPhysicalAttributes){0},
                                           wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features)));
            }

            BusterX86MetadataPhysicalOperand template_jcc_near_operand = x86_64_metadata_test_physical_relative(INT32_MIN, 32);
            BusterX86MetadataPhysicalQuery template_jcc_near_query = {
                .mnemonic = S8("JNZ"),
                .operands = &template_jcc_near_operand,
                .operand_count = 1,
                .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            };
            BusterX86MetadataSelectResult template_jcc_near_selected = buster_x86_metadata_select_form(template_jcc_near_query);
            BusterX86MetadataFormKey template_jcc_near_key = {0};
            bool template_jcc_near_ready = template_jcc_near_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                           buster_x86_metadata_form_key(template_jcc_near_selected.form_id, &template_jcc_near_key) &&
                                           buster_x86_metadata_test_machine_fast_plan(template_jcc_near_key.form_id);
            BUSTER_TEST(arguments, template_jcc_near_ready);
            if (template_jcc_near_ready)
            {
                BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                           template_jcc_near_key, &template_jcc_near_operand, 1,
                                           (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                           BUSTER_ARRAY_LENGTH(wildcard_features)));
                template_jcc_near_operand.value = INT32_MAX;
                BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                           template_jcc_near_key, &template_jcc_near_operand, 1,
                                           (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                           BUSTER_ARRAY_LENGTH(wildcard_features)));
            }

            // VZEROUPPER is a fixed AVX byte row selected by the machine
            // shape cache.  Keep a checked-vs-machine comparison here so the
            // zero template remains tied to metadata emission rather than a
            // duplicated opcode spelling.
            if (vzero_keys_ready)
            {
                BUSTER_TEST(arguments, buster_x86_metadata_test_machine_fast_plan(vzeroupper_key.form_id));
                BUSTER_TEST(arguments, x86_64_metadata_test_exact_plan_case(
                                           vzeroupper_key, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard_features,
                                           BUSTER_ARRAY_LENGTH(wildcard_features)));
            }

            BusterX86MetadataExactPlan mfence_plan = {0};
            BusterX86MetadataMachineExactToken mfence_token = {0};
            BUSTER_TEST(arguments, buster_x86_metadata_exact_plan_for_key(mfence_plan_key, &mfence_plan));
            BUSTER_TEST(arguments, !buster_x86_metadata_machine_exact_token_for_plan(
                                       mfence_plan, (BusterX86MetadataFeatureInput){0}, &mfence_token));
            BUSTER_TEST(arguments, buster_x86_metadata_machine_exact_token_for_plan(
                                       mfence_plan,
                                       (BusterX86MetadataFeatureInput){
                                           .names = wildcard_features,
                                           .count = BUSTER_ARRAY_LENGTH(wildcard_features),
                                       },
                                       &mfence_token));

            BusterX86MetadataExactPlan reserved_plan = {0};
            if (buster_x86_metadata_exact_plan_for_key(mfence_plan_key, &reserved_plan))
            {
                BusterX86MetadataExactQuery reserved_query = {
                    .key = mfence_plan_key,
                    .reserved = 1,
                };
                reserved_query.key.form_id += 1;
                BusterX86MetadataEmitResult reserved_result =
                    buster_x86_metadata_emit_exact_prevalidated(reserved_plan, reserved_query);
                BUSTER_TEST(arguments, reserved_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                           reserved_result.form_id == reserved_query.key.form_id);

                BusterX86MetadataMachineExactToken forged_token = {
                    .slot_plus_one = UINT16_MAX,
                    .policy_flags = 1,
                };
                u8 forged_output = 0xa5;
                BusterX86MetadataEmitResult forged_token_result = buster_x86_metadata_emit_exact_machine(
                    forged_token, (BusterX86MetadataMachineExactQuery){.output = &forged_output, .output_capacity = 1});
                BUSTER_TEST(arguments, forged_token_result.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM &&
                                           forged_output == 0xa5);

                forged_token = (BusterX86MetadataMachineExactToken){
                    .slot_plus_one = mfence_token.slot_plus_one,
                    .policy_flags = 0,
                    .integrity = mfence_token.integrity,
                };
                forged_token_result = buster_x86_metadata_emit_exact_machine(
                    forged_token, (BusterX86MetadataMachineExactQuery){.output = &forged_output, .output_capacity = 1});
                BUSTER_TEST(arguments, forged_token_result.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM &&
                                           forged_output == 0xa5);

                forged_token = mfence_token;
                forged_token.policy_flags ^= 2;
                forged_token_result = buster_x86_metadata_emit_exact_machine(
                    forged_token, (BusterX86MetadataMachineExactQuery){.output = &forged_output, .output_capacity = 1});
                BUSTER_TEST(arguments, forged_token_result.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM &&
                                           forged_output == 0xa5);
            }
        }

        BusterX86MetadataExactPlan forged_plan = {.form_id = 9610, .stable_hash = UINT64_C(1)};
        BusterX86MetadataEmitResult forged_result = buster_x86_metadata_emit_exact_prevalidated(
            forged_plan, (BusterX86MetadataExactQuery){
                              .key = mfence_plan_key,
                              .features = {.names = wildcard_features, .count = BUSTER_ARRAY_LENGTH(wildcard_features)},
                              .address_size = 64,
                              .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
                              .output = (u8[8]){0},
                              .output_capacity = 8,
                          });
        BUSTER_TEST(arguments, forged_result.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM);
    }

    BUSTER_TEST(arguments, x86_64_metadata_test_source_reachability_skeleton(arguments));
    BUSTER_TEST(arguments, x86_64_metadata_test_source_decorator_reachability(arguments));
    BUSTER_TEST(arguments, x86_64_metadata_test_source_memory_skeleton(arguments));
    BUSTER_TEST(arguments, x86_64_metadata_test_source_immediate_skeleton(arguments));
    BUSTER_TEST(arguments, x86_64_metadata_test_source_relative_absolute_skeleton(arguments));
    BUSTER_TEST(arguments, x86_64_metadata_test_source_att_memory_skeleton(arguments));
    BUSTER_TEST(arguments, x86_64_metadata_test_register_only_census(arguments));

    {
        // X87 control rows use their distinct opcodes for the data element;
        // a generic 16-bit operand-size prefix would change the instruction.
        // The source aliases below all resolve through the same FNST*/FNSAVE
        // metadata rows and therefore must retain identical canonical bytes.
        String8 x87_features[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand x87_mem16 = x86_64_metadata_test_physical_mem_base(0, 16, 0);
        BusterX86MetadataPhysicalOperand x87_mem14 = x86_64_metadata_test_physical_mem_base(0, 112, 0);
        BusterX86MetadataPhysicalOperand x87_mem94 = x86_64_metadata_test_physical_mem_base(0, 752, 0);
        BusterX86MetadataPhysicalOperand x87_st4 = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
            .width = 80,
            .reg = {.index = 4, .width = 80, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL},
        };
        BusterX86MetadataPhysicalOperand x87_ax =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 16);
        BusterX86MetadataPhysicalQuery fldcw_query = x86_64_metadata_test_physical_query(
            S8("FLDCW"), &x87_mem16, 1, (BusterX86MetadataPhysicalAttributes){0}, x87_features,
            BUSTER_ARRAY_LENGTH(x87_features));
        BusterX86MetadataPhysicalQuery fnstcw_query = fldcw_query;
        fnstcw_query.mnemonic = S8("FNSTCW");
        BusterX86MetadataPhysicalQuery fstcw_query = fnstcw_query;
        fstcw_query.mnemonic = S8("FSTCW");
        BusterX86MetadataPhysicalQuery fnstsw_mem_query = fnstcw_query;
        fnstsw_mem_query.mnemonic = S8("FNSTSW");
        BusterX86MetadataPhysicalQuery fstsw_mem_query = fnstsw_mem_query;
        fstsw_mem_query.mnemonic = S8("FSTSW");
        BusterX86MetadataPhysicalQuery fnstsw_ax_query = fnstsw_mem_query;
        fnstsw_ax_query.mnemonic = S8("FNSTSW");
        fnstsw_ax_query.operands = &x87_ax;
        BusterX86MetadataPhysicalQuery fstsw_ax_query = fnstsw_ax_query;
        fstsw_ax_query.mnemonic = S8("FSTSW");
        BusterX86MetadataPhysicalQuery fstenv_query = fnstcw_query;
        fstenv_query.mnemonic = S8("FSTENV");
        fstenv_query.operands = &x87_mem14;
        BusterX86MetadataPhysicalQuery fsave_query = fnstcw_query;
        fsave_query.mnemonic = S8("FSAVE");
        fsave_query.operands = &x87_mem94;
        BusterX86MetadataPhysicalOperand x87_mem14_r8 = x86_64_metadata_test_physical_mem_base(8, 112, 0);
        BusterX86MetadataPhysicalQuery fstenv_r8_query = fstenv_query;
        fstenv_r8_query.operands = &x87_mem14_r8;
        BusterX86MetadataPhysicalQuery ffreep_query = fnstcw_query;
        ffreep_query.mnemonic = S8("FFREEP");
        ffreep_query.operands = &x87_st4;
        u8 fldcw_bytes[8] = {0};
        u8 fnstcw_bytes[8] = {0};
        u8 fstcw_bytes[8] = {0};
        u8 fnstsw_mem_bytes[8] = {0};
        u8 fstsw_mem_bytes[8] = {0};
        u8 fnstsw_ax_bytes[8] = {0};
        u8 fstsw_ax_bytes[8] = {0};
        u8 fstenv_bytes[8] = {0};
        u8 fstenv_r8_bytes[8] = {0};
        u8 fsave_bytes[8] = {0};
        u8 ffreep_bytes[8] = {0};
        BusterX86MetadataEmitResult fldcw = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fldcw_query, .output = fldcw_bytes, .output_capacity = sizeof(fldcw_bytes)});
        BusterX86MetadataEmitResult fnstcw = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fnstcw_query, .output = fnstcw_bytes, .output_capacity = sizeof(fnstcw_bytes)});
        BusterX86MetadataEmitResult fstcw = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fstcw_query, .output = fstcw_bytes, .output_capacity = sizeof(fstcw_bytes)});
        BusterX86MetadataEmitResult fnstsw_mem = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fnstsw_mem_query, .output = fnstsw_mem_bytes, .output_capacity = sizeof(fnstsw_mem_bytes)});
        BusterX86MetadataEmitResult fstsw_mem = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fstsw_mem_query, .output = fstsw_mem_bytes, .output_capacity = sizeof(fstsw_mem_bytes)});
        BusterX86MetadataEmitResult fnstsw_ax = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fnstsw_ax_query, .output = fnstsw_ax_bytes, .output_capacity = sizeof(fnstsw_ax_bytes)});
        BusterX86MetadataEmitResult fstsw_ax = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fstsw_ax_query, .output = fstsw_ax_bytes, .output_capacity = sizeof(fstsw_ax_bytes)});
        BusterX86MetadataEmitResult fstenv = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fstenv_query, .output = fstenv_bytes, .output_capacity = sizeof(fstenv_bytes)});
        BusterX86MetadataEmitResult fstenv_r8 = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fstenv_r8_query, .output = fstenv_r8_bytes, .output_capacity = sizeof(fstenv_r8_bytes)});
        BusterX86MetadataEmitResult fsave = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = fsave_query, .output = fsave_bytes, .output_capacity = sizeof(fsave_bytes)});
        BusterX86MetadataEmitResult ffreep = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = ffreep_query, .output = ffreep_bytes, .output_capacity = sizeof(ffreep_bytes)});
        BUSTER_TEST(arguments, fldcw.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && fldcw.byte_count == 2 && fldcw.form_id == 9113 &&
                                   fldcw_bytes[0] == 0xd9 && fldcw_bytes[1] == 0x28);
        BUSTER_TEST(arguments, fnstcw.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && fnstcw.byte_count == 2 && fnstcw.form_id == 9121 &&
                                   fnstcw_bytes[0] == 0xd9 && fnstcw_bytes[1] == 0x38 && fstcw.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   fstcw.byte_count == 2 && fstcw.form_id == 9121 && fstcw_bytes[0] == 0xd9 && fstcw_bytes[1] == 0x38);
        BUSTER_TEST(arguments, fnstsw_mem.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && fnstsw_mem.byte_count == 2 &&
                                   fnstsw_mem.form_id == 9213 && fstsw_mem.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   fstsw_mem.byte_count == 2 && fstsw_mem.form_id == 9213 && fnstsw_mem_bytes[0] == 0xdd &&
                                   fnstsw_mem_bytes[1] == 0x38 && fstsw_mem_bytes[0] == 0xdd && fstsw_mem_bytes[1] == 0x38);
        BUSTER_TEST(arguments, fnstsw_ax.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && fnstsw_ax.byte_count == 2 &&
                                   fnstsw_ax.form_id == 9242 && fstsw_ax.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   fstsw_ax.byte_count == 2 && fstsw_ax.form_id == 9242 && fnstsw_ax_bytes[0] == 0xdf &&
                                   fnstsw_ax_bytes[1] == 0xe0 && fstsw_ax_bytes[0] == 0xdf && fstsw_ax_bytes[1] == 0xe0);
        BUSTER_TEST(arguments, fstenv.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && fstenv.byte_count == 2 && fstenv.form_id == 9120 &&
                                   fsave.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && fsave.byte_count == 2 && fsave.form_id == 9212 &&
                                   fstenv_bytes[0] == 0xd9 && fstenv_bytes[1] == 0x30 && fsave_bytes[0] == 0xdd && fsave_bytes[1] == 0x30 &&
                                   ffreep.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && ffreep.byte_count == 2 && ffreep.form_id == 9241 &&
                                   ffreep_bytes[0] == 0xdf && ffreep_bytes[1] == 0xc4);
        BUSTER_TEST(arguments, fstenv_r8.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && fstenv_r8.form_id == 9120 &&
                                   fstenv_r8.byte_count == 3 && fstenv_r8_bytes[0] == 0x41 && fstenv_r8_bytes[1] == 0xd9 &&
                                   fstenv_r8_bytes[2] == 0x30);
    }

    {
        // This is the complete normalized residual cohort: the sixteen X87
        // environment rows followed by the thirty-two BASE legacy rows.
        // LLVM/GNU probes agree on the opcode and 66-byte choices for the
        // accepted FLDENV/FNSTENV/FRSTOR/FNSAVE, PUSHA/POPA/BOUND, CBW/CWD,
        // PUSHF/POPF, and IRET spellings.  Their current assemblers reject the
        // legacy aliases pushad, popad, and iretd, so those entries use the
        // authoritative XED bytes.  The X87 16-bit-address oracle is also
        // outside this emitter's 32/64-bit address-size contract; the direct
        // cases use its canonical 64-bit-address operand shape and check the
        // opcode/prefix semantics.
        static struct
        {
            u32 form_id;
            u8 byte_count;
            u8 bytes[3];
            bool x87;
        } const mode66_cases[] = {
            {9106, 2, {0xd9, 0x20}, true},  {9107, 3, {0x66, 0xd9, 0x20}, true},
            {9109, 3, {0x66, 0xd9, 0x20}, true}, {9110, 2, {0xd9, 0x20}, true},
            {9114, 2, {0xd9, 0x30}, true},  {9115, 3, {0x66, 0xd9, 0x30}, true},
            {9117, 3, {0x66, 0xd9, 0x30}, true}, {9118, 2, {0xd9, 0x30}, true},
            {9199, 2, {0xdd, 0x20}, true},  {9200, 3, {0x66, 0xdd, 0x20}, true},
            {9202, 3, {0x66, 0xdd, 0x20}, true}, {9203, 2, {0xdd, 0x20}, true},
            {9206, 2, {0xdd, 0x30}, true},  {9207, 3, {0x66, 0xdd, 0x30}, true},
            {9209, 3, {0x66, 0xdd, 0x30}, true}, {9210, 2, {0xdd, 0x30}, true},
            {9726, 1, {0x60}, false},         {9727, 2, {0x66, 0x60}, false},
            {9728, 2, {0x66, 0x60}, false},   {9729, 1, {0x60}, false},
            {9730, 1, {0x61}, false},         {9731, 2, {0x66, 0x61}, false},
            {9732, 2, {0x66, 0x61}, false},   {9733, 1, {0x61}, false},
            {9734, 2, {0x62, 0x00}, false},   {9735, 3, {0x66, 0x62, 0x00}, false},
            {9736, 3, {0x66, 0x62, 0x00}, false}, {9737, 2, {0x62, 0x00}, false},
            {9858, 1, {0x98}, false},         {9859, 2, {0x66, 0x98}, false},
            {9862, 2, {0x66, 0x98}, false},   {9863, 1, {0x98}, false},
            {9865, 1, {0x99}, false},         {9866, 2, {0x66, 0x99}, false},
            {9869, 2, {0x66, 0x99}, false},   {9870, 1, {0x99}, false},
            {9875, 1, {0x9c}, false},         {9876, 2, {0x66, 0x9c}, false},
            {9878, 1, {0x9c}, false},         {9879, 2, {0x66, 0x9c}, false},
            {9882, 1, {0x9d}, false},         {9883, 2, {0x66, 0x9d}, false},
            {9885, 2, {0x66, 0x9d}, false},   {9886, 1, {0x9d}, false},
            {10030, 1, {0xcf}, false},        {10031, 2, {0x66, 0xcf}, false},
            {10033, 2, {0x66, 0xcf}, false},  {10034, 1, {0xcf}, false},
        };
        String8 wildcard_features[1] = {S8("*")};
        bool exact_bytes = BUSTER_ARRAY_LENGTH(mode66_cases) == 48;
        bool raw_predicates = true;
        bool class_counts = true;
        bool mode_guard = true;
        u32 x87_count = 0;
        u32 base_count = 0;
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(mode66_cases); case_index += 1)
        {
            BusterX86MetadataForm form = {0};
            bool retrieved = buster_x86_metadata_form(mode66_cases[case_index].form_id, &form);
            bool mode16 = retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("mode16"));
            bool mode32 = retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("mode32"));
            bool mode64 = retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("mode64"));
            bool not64 = retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("not64"));
            bool prefix66 = retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("66_prefix"));
            bool prefix_no66 = retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("no66_prefix"));
            String8 const forbidden_tokens[] = {
                S8("repe"), S8("repne"), S8("norep"), S8("OVERRIDE_SEG"), S8("ADDR32"), S8("DF64()"),
                S8("REX2"), S8("EVEX"), S8("APX"), S8("AMX"), S8("SCC"), S8("BCRC"), S8("UBIT"),
            };
            bool no_adjacent_control = retrieved;
            for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(forbidden_tokens); token_index += 1)
                no_adjacent_control &= !x86_64_metadata_test_string_contains(form.pattern, forbidden_tokens[token_index]);
            bool expected_extension = retrieved &&
                                      x86_64_metadata_test_string_equal(form.extension, mode66_cases[case_index].x87 ? S8("X87") : S8("BASE"));
            raw_predicates &= retrieved && form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                              form.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY && (mode16 ^ mode32) && !mode64 && !not64 &&
                              (prefix66 ^ prefix_no66) && (prefix66 == (form.mandatory_prefix == 0x66)) && no_adjacent_control &&
                              expected_extension;
            BusterX86MetadataPhysicalOperand operands[16] = {0};
            char8 mnemonic_buffer[128] = {0};
            BusterX86MetadataPhysicalQuery query = {0};
            bool built = x86_64_metadata_test_build_gate_query(mode66_cases[case_index].form_id, &query, operands, mnemonic_buffer);
            query.features.names = wildcard_features;
            query.features.count = BUSTER_ARRAY_LENGTH(wildcard_features);
            query.execution_mode = mode16 ? BUSTER_X86_METADATA_EXECUTION_MODE_16 : BUSTER_X86_METADATA_EXECUTION_MODE_32;
            query.include_not64 = false;
            u8 output[16] = {0};
            BusterX86MetadataRelocation relocations[2] = {0};
            BusterX86MetadataEmitResult emitted = {0};
            if (built)
            {
                emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                    .physical = query,
                    .form_id = mode66_cases[case_index].form_id,
                    .output = output,
                    .output_capacity = sizeof(output),
                    .relocations = relocations,
                    .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                });
            }
            exact_bytes &= built && emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.relocation_count == 0 &&
                           x86_64_metadata_test_bytes_equal(output, emitted.byte_count, mode66_cases[case_index].bytes,
                                                            mode66_cases[case_index].byte_count);
            x87_count += mode66_cases[case_index].x87;
            base_count += !mode66_cases[case_index].x87;
            BusterX86MetadataPhysicalQuery ordinary = query;
            ordinary.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64;
            ordinary.include_not64 = false;
            BusterX86MetadataEmitResult ordinary_result = {0};
            BusterX86MetadataPhysicalQuery wrong_mode = query;
            wrong_mode.execution_mode = mode16 ? BUSTER_X86_METADATA_EXECUTION_MODE_32 : BUSTER_X86_METADATA_EXECUTION_MODE_16;
            BusterX86MetadataEmitResult wrong_mode_result = {0};
            BusterX86MetadataPhysicalQuery include_only = query;
            include_only.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64;
            include_only.include_not64 = true;
            BusterX86MetadataEmitResult include_only_result = {0};
            BusterX86MetadataPhysicalQuery any_without_include = query;
            any_without_include.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_ANY;
            any_without_include.include_not64 = false;
            BusterX86MetadataEmitResult any_without_include_result = {0};
            BusterX86MetadataPhysicalQuery any_inspection = query;
            any_inspection.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_ANY;
            any_inspection.include_not64 = true;
            BusterX86MetadataEmitResult any_inspection_result = {0};
            if (built)
            {
                ordinary_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                    .physical = ordinary,
                    .form_id = mode66_cases[case_index].form_id,
                    .output = output,
                    .output_capacity = sizeof(output),
                    .relocations = relocations,
                    .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                });
                wrong_mode_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                    .physical = wrong_mode,
                    .form_id = mode66_cases[case_index].form_id,
                    .output = output,
                    .output_capacity = sizeof(output),
                    .relocations = relocations,
                    .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                });
                include_only_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                    .physical = include_only,
                    .form_id = mode66_cases[case_index].form_id,
                    .output = output,
                    .output_capacity = sizeof(output),
                    .relocations = relocations,
                    .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                });
                any_without_include_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                    .physical = any_without_include,
                    .form_id = mode66_cases[case_index].form_id,
                    .output = output,
                    .output_capacity = sizeof(output),
                    .relocations = relocations,
                    .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                });
                any_inspection_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                    .physical = any_inspection,
                    .form_id = mode66_cases[case_index].form_id,
                    .output = output,
                    .output_capacity = sizeof(output),
                    .relocations = relocations,
                    .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                });
            }
            mode_guard &= built && ordinary_result.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                          wrong_mode_result.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                          include_only_result.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                          any_without_include_result.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                          any_inspection_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS;
        }
        class_counts &= x87_count == 16 && base_count == 32;
        BUSTER_TEST(arguments, raw_predicates);
        BUSTER_TEST(arguments, exact_bytes);
        BUSTER_TEST(arguments, class_counts);
        BUSTER_TEST(arguments, mode_guard);
    }

    X86_64MetadataCanonicalGateCase const canonical_gate_cases[] = {
        {8014, 1, {S8("enqcmd")}},
        {8015, 1, {S8("fred")}},
        {8099, 1, {S8("hreset")}},
        {424, 1, {S8("invlpgb")}},
        {8100, 1, {S8("invpcid")}},
        {8714, 1, {S8("keylocker")}},
        {8017, 1, {S8("lkgs")}},
        {9599, 1, {S8("monitor")}},
        {8820, 1, {S8("msrlist")}},
        {8818, 1, {S8("msr-imm")}},
        {8822, 1, {S8("pbndkb")}},
        {8824, 1, {S8("pconfig")}},
        {8844, 1, {S8("sgx")}},
        {8888, 1, {S8("smap")}},
        {433, 1, {S8("snp")}},
        {8890, 1, {S8("tdx")}},
        {9066, 1, {S8("wbnoinvd")}},
        {9067, 1, {S8("wrmsrns")}},
        {10971, 1, {S8("xsave")}},
        {11009, 1, {S8("xsaves")}},
        {10993, 1, {S8("vmx")}},
        {454, 1, {S8("svm")}},
        {1750, 2, {S8("apx"), S8("enqcmd")}},
        {1848, 2, {S8("apx"), S8("invpcid")}},
        {2932, 2, {S8("apx"), S8("msr-imm")}},
        {1847, 2, {S8("apx"), S8("vmx")}},
        {1886, 2, {S8("apx"), S8("movdir64b")}},
        {9162, 1, {S8("sse2")}},
        {9163, 1, {S8("sse2")}},
        {9164, 1, {S8("sse2")}},
        {9165, 1, {S8("sse2")}},
        {9172, 1, {S8("sse2")}},
        {9173, 1, {S8("sse2")}},
        {9174, 1, {S8("sse2")}},
        {9175, 1, {S8("sse2")}},
        {9181, 1, {S8("sse2")}},
        {9182, 1, {S8("sse2")}},
        {9243, 1, {S8("sse2")}},
        {9244, 1, {S8("sse2")}},
    };
    for (u32 gate_index = 0; gate_index < BUSTER_ARRAY_LENGTH(canonical_gate_cases); gate_index += 1)
        BUSTER_TEST(arguments, x86_64_metadata_test_canonical_gate(canonical_gate_cases[gate_index]));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(1750, S8("APX_F_ENQCMD")));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(1848, S8("APX_F_INVPCID")));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(2932, S8("APX_F_MSR_IMM")));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(1847, S8("APX_F_VMX")));
    BUSTER_TEST(arguments, x86_64_metadata_test_raw_conjunction_rejected(1886, S8("APX_F_MOVDIR64B")));
    BUSTER_TEST(arguments, buster_x86_metadata_test_eamode_alias_forms(423, 424) &&
                               !buster_x86_metadata_test_eamode_alias_forms(423, 10993));

    {
        // XED's long-mode address-size mapping keeps the EAMODE16 PadLock
        // row (9011) out of the encoder.  The valid long-mode spelling is
        // the EAMODE32 alias (9012), which requires 67 and emits
        // 67 F3 0F A6 C0.  Keep the hidden operand topology and dialect
        // lookup checks here while making the source/decoder contract
        // explicit.
        BusterX86MetadataForm montmul_eamode16 = {0};
        BusterX86MetadataForm montmul_eamode32 = {0};
        bool montmul_records = buster_x86_metadata_form(9011, &montmul_eamode16) &&
                               buster_x86_metadata_form(9012, &montmul_eamode32);
        bool montmul_hidden_topology = montmul_records && montmul_eamode16.operand_count == 6;
        for (u32 operand_index = 0; montmul_hidden_topology && operand_index < montmul_eamode16.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand operand = {0};
            montmul_hidden_topology &= buster_x86_metadata_operand(9011, operand_index, &operand) && !operand.visible &&
                                       (operand.access & BUSTER_X86_METADATA_ACCESS_SUPPRESSED) != 0;
        }
        BusterX86MetadataCandidateRange montmul_lower = buster_x86_metadata_lookup_mnemonic(S8("montmul"));
        BusterX86MetadataCandidateRange montmul_upper = buster_x86_metadata_lookup_mnemonic(S8("MONTMUL"));
        bool montmul_aliases = montmul_lower.count == montmul_upper.count && montmul_lower.count >= 2;
        bool montmul_lower_has_9011 = false;
        bool montmul_lower_has_9012 = false;
        bool montmul_upper_has_9011 = false;
        bool montmul_upper_has_9012 = false;
        for (u32 position = 0; position < montmul_lower.count; position += 1)
        {
            u32 form_id = 0;
            if (x86_64_metadata_test_candidate(montmul_lower, position, &form_id))
            {
                montmul_lower_has_9011 |= form_id == 9011;
                montmul_lower_has_9012 |= form_id == 9012;
            }
        }
        for (u32 position = 0; position < montmul_upper.count; position += 1)
        {
            u32 form_id = 0;
            if (x86_64_metadata_test_candidate(montmul_upper, position, &form_id))
            {
                montmul_upper_has_9011 |= form_id == 9011;
                montmul_upper_has_9012 |= form_id == 9012;
            }
        }
        montmul_aliases &= montmul_lower_has_9011 && montmul_lower_has_9012 && montmul_upper_has_9011 && montmul_upper_has_9012;
        BUSTER_TEST(arguments, montmul_records && montmul_eamode16.stable_hash == UINT64_C(0x43ea6607300874ad) &&
                                   montmul_eamode16.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64 &&
                                   montmul_eamode16.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY &&
                                   montmul_eamode16.mode_flags == BUSTER_X86_METADATA_MODE_EA16 &&
                                   montmul_eamode32.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                   montmul_eamode32.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY &&
                                   montmul_eamode32.mode_flags == BUSTER_X86_METADATA_MODE_EA32 && montmul_hidden_topology &&
                                   montmul_aliases && x86_64_metadata_test_pattern_has_token(montmul_eamode16.pattern, S8("eamode16")) &&
                                   x86_64_metadata_test_pattern_has_token(montmul_eamode32.pattern, S8("eamode32")));

        String8 montmul_features[1] = {S8("*")};
        u8 montmul_output[8] = {0};
        BusterX86MetadataRelocation montmul_relocations[1] = {0};
        BusterX86MetadataPhysicalQuery montmul_query = x86_64_metadata_test_physical_query(
            S8("montmul"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, montmul_features,
            BUSTER_ARRAY_LENGTH(montmul_features));
        BusterX86MetadataSelectResult montmul_default = buster_x86_metadata_select_form(montmul_query);
        u32 montmul_default_ids[4] = {0};
        BusterX86MetadataResolveResult montmul_default_resolved = buster_x86_metadata_resolve(
            (BusterX86MetadataResolveQuery){
                .mnemonic = S8("montmul"),
                .features = {.names = montmul_features, .count = BUSTER_ARRAY_LENGTH(montmul_features)},
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            },
            montmul_default_ids, BUSTER_ARRAY_LENGTH(montmul_default_ids));
        BusterX86MetadataEmitResult montmul_eamode16_emit = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = montmul_query,
            .form_id = 9011,
            .output = montmul_output,
            .output_capacity = sizeof(montmul_output),
            .relocations = montmul_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(montmul_relocations),
        });
        BUSTER_TEST(arguments, montmul_default.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   montmul_default_resolved.status != BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   montmul_eamode16_emit.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA);

        montmul_query.address_size = 32;
        BusterX86MetadataSelectResult montmul_addr32 = buster_x86_metadata_select_form(montmul_query);
        u32 montmul_addr32_ids[4] = {0};
        BusterX86MetadataResolveResult montmul_addr32_resolved = buster_x86_metadata_resolve(
            (BusterX86MetadataResolveQuery){
                .mnemonic = S8("montmul"),
                .features = {.names = montmul_features, .count = BUSTER_ARRAY_LENGTH(montmul_features)},
                .address_size = 32,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            },
            montmul_addr32_ids, BUSTER_ARRAY_LENGTH(montmul_addr32_ids));
        BusterX86MetadataEmitResult montmul_eamode32_emit = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = montmul_query,
            .form_id = 9012,
            .output = montmul_output,
            .output_capacity = sizeof(montmul_output),
            .relocations = montmul_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(montmul_relocations),
        });
        BUSTER_TEST(arguments, montmul_addr32.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   montmul_addr32.form_id == 9012 && montmul_addr32.selected_byte_count == 5 &&
                                   montmul_addr32_resolved.status == BUSTER_X86_METADATA_RESOLVE_SUCCESS &&
                                   montmul_addr32_resolved.candidate_count == 1 && montmul_addr32_ids[0] == 9012 &&
                                   montmul_eamode32_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   montmul_eamode32_emit.form_id == 9012 && montmul_eamode32_emit.byte_count == 5 &&
                                   montmul_eamode32_emit.relocation_count == 0 &&
                                   x86_64_metadata_test_bytes_equal(montmul_output, montmul_eamode32_emit.byte_count,
                                                                     (u8 const[]){0x67, 0xf3, 0x0f, 0xa6, 0xc0}, 5));

        montmul_query.address_size = 16;
        BusterX86MetadataSelectResult montmul_addr16 = buster_x86_metadata_select_form(montmul_query);
        montmul_query.address_size = 32;
        montmul_query.attributes.repne = true;
        BusterX86MetadataSelectResult montmul_wrong_prefix = buster_x86_metadata_select_form(montmul_query);
        BusterX86MetadataEmitResult montmul_wrong_prefix_emit = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = montmul_query,
            .form_id = 9012,
            .output = montmul_output,
            .output_capacity = sizeof(montmul_output),
            .relocations = montmul_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(montmul_relocations),
        });
        montmul_query.attributes = (BusterX86MetadataPhysicalAttributes){0};
        String8 wrong_montmul_features[1] = {S8("sse2")};
        montmul_query.features.names = wrong_montmul_features;
        montmul_query.features.count = 1;
        BusterX86MetadataSelectResult montmul_wrong_feature = buster_x86_metadata_select_form(montmul_query);
        BusterX86MetadataEmitResult montmul_wrong_feature_emit = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = montmul_query,
            .form_id = 9012,
            .output = montmul_output,
            .output_capacity = sizeof(montmul_output),
            .relocations = montmul_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(montmul_relocations),
        });
        BusterX86MetadataPhysicalOperand montmul_wrong_operand =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32);
        montmul_query.features.names = montmul_features;
        montmul_query.features.count = 1;
        montmul_query.operands = &montmul_wrong_operand;
        montmul_query.operand_count = 1;
        BusterX86MetadataSelectResult montmul_wrong_operands = buster_x86_metadata_select_form(montmul_query);
        BUSTER_TEST(arguments, montmul_addr16.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);
        BUSTER_TEST(arguments, montmul_wrong_prefix.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   montmul_wrong_prefix_emit.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);
        BUSTER_TEST(arguments, montmul_wrong_feature.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   montmul_wrong_feature_emit.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);
        BUSTER_TEST(arguments, montmul_wrong_operands.status == BUSTER_X86_METADATA_ENCODE_WRONG_OPERAND_COUNT);

        BusterX86MetadataPhysicalOperand jcxz_operands[16] = {0};
        char8 jcxz_mnemonic_buffer[128] = {0};
        BusterX86MetadataPhysicalQuery jcxz_query = {0};
        bool jcxz_built = x86_64_metadata_test_build_gate_query(10051, &jcxz_query, jcxz_operands, jcxz_mnemonic_buffer);
        jcxz_query.features.names = montmul_features;
        jcxz_query.features.count = 1;
        jcxz_query.include_not64 = false;
        jcxz_query.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64;
        BusterX86MetadataSelectResult jcxz_default = buster_x86_metadata_select_form(jcxz_query);
        BusterX86MetadataPhysicalOperand monitor_operands[16] = {0};
        char8 monitor_mnemonic_buffer[128] = {0};
        BusterX86MetadataPhysicalQuery monitor_query = {0};
        bool monitor_built = x86_64_metadata_test_build_gate_query(9598, &monitor_query, monitor_operands, monitor_mnemonic_buffer);
        monitor_query.include_privileged = false;
        BusterX86MetadataSelectResult monitor_default = buster_x86_metadata_select_form(monitor_query);
        BUSTER_TEST(arguments, jcxz_built && jcxz_default.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   monitor_built && monitor_default.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);
    }

    BusterX86MetadataCounts counts = buster_x86_metadata_counts();
    {
        typedef struct X86_64MetadataAliasInventoryCase X86_64MetadataAliasInventoryCase;
        struct X86_64MetadataAliasInventoryCase
        {
            u32 form_id;
            u64 stable_hash;
            u8 coverage_class;
            u16 reason_id;
            u8 test_class;
        };
        static X86_64MetadataAliasInventoryCase const inventory[] = {
            {7963, UINT64_C(9760411889977040051), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7965, UINT64_C(9415441538438589370), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7966, UINT64_C(17207840402057639609), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7967, UINT64_C(16100944905773296210), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7968, UINT64_C(9984935062474368315), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7970, UINT64_C(15604930873871107343), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7971, UINT64_C(10850275643501731349), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7972, UINT64_C(7816956366636167268), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7973, UINT64_C(2414193575843013120), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7974, UINT64_C(12477954903810431918), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7975, UINT64_C(16234885271030626349), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {7978, UINT64_C(12568587553290984177), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {8106, UINT64_C(7673034833317256367), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {8107, UINT64_C(3274967469198123399), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {8689, UINT64_C(14813386923769267956), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {8690, UINT64_C(3837008830770907182), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {8692, UINT64_C(14272704612556719270), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {8814, UINT64_C(11284571631391134300), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {8815, UINT64_C(9911688756507210979), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {8816, UINT64_C(3985021897032612900), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {8817, UINT64_C(4076819320050101737), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {9011, UINT64_C(4893836126148129965), BUSTER_X86_METADATA_COVERAGE_NOT64,
             BUSTER_X86_METADATA_REASON_MODE_NOT64, BUSTER_X86_METADATA_TEST_NOT64_SCHEMA},
            {9572, UINT64_C(9209903826378290749), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {9573, UINT64_C(6800483434327952346), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {9574, UINT64_C(13951958529522862627), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {9854, UINT64_C(9098192276389485439), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {10045, UINT64_C(7714867975667879422), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {10049, UINT64_C(18015428549483481338), BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
             BUSTER_X86_METADATA_REASON_DECODE_ALIAS, BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA},
            {10051, UINT64_C(7650631457957071542), BUSTER_X86_METADATA_COVERAGE_NOT64,
             BUSTER_X86_METADATA_REASON_MODE_NOT64, BUSTER_X86_METADATA_TEST_NOT64_SCHEMA},
        };
        bool inventory_exact = BUSTER_ARRAY_LENGTH(inventory) == 29;
        bool alias_apis_closed = true;
        u32 alias_count = 0;
        u32 not64_count = 0;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(inventory); index += 1)
        {
            X86_64MetadataAliasInventoryCase expected = inventory[index];
            BusterX86MetadataForm form = {0};
            bool retrieved = buster_x86_metadata_form(expected.form_id, &form);
            inventory_exact &= retrieved && form.id == expected.form_id && form.stable_hash == expected.stable_hash &&
                               form.coverage_class == expected.coverage_class && form.reason_id == expected.reason_id &&
                               form.test_class == expected.test_class;
            alias_count += expected.coverage_class == BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS;
            not64_count += expected.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64;
            if (expected.coverage_class == BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS)
            {
                BusterX86MetadataPhysicalOperand operands[16] = {0};
                char8 mnemonic_buffer[128] = {0};
                BusterX86MetadataPhysicalQuery query = {0};
                bool built = x86_64_metadata_test_build_gate_query(expected.form_id, &query, operands, mnemonic_buffer);
                u8 output[32] = {0};
                BusterX86MetadataRelocation relocations[2] = {0};
                BusterX86MetadataEmitQuery emit_query = {
                    .physical = query,
                    .form_id = expected.form_id,
                    .output = output,
                    .output_capacity = BUSTER_ARRAY_LENGTH(output),
                    .relocations = relocations,
                    .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                };
                BusterX86MetadataEmitResult direct = built ? buster_x86_metadata_emit_form(emit_query)
                                                            : (BusterX86MetadataEmitResult){0};
                BusterX86MetadataEmitResult exact = built ? buster_x86_metadata_emit_form_exact(
                    emit_query, (BusterX86MetadataFormKey){.form_id = expected.form_id, .stable_hash = expected.stable_hash})
                                                           : (BusterX86MetadataEmitResult){0};
                BusterX86MetadataSelectResult selected = built ? buster_x86_metadata_select_form(query)
                                                                : (BusterX86MetadataSelectResult){0};
                alias_apis_closed &= built && direct.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                     exact.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                     selected.form_id != expected.form_id;
            }
            else
            {
                inventory_exact &= (form.mode_flags & BUSTER_X86_METADATA_MODE_EA16) != 0 &&
                                   (form.mode_flags & (BUSTER_X86_METADATA_MODE_EA32 | BUSTER_X86_METADATA_MODE_EA64)) == 0;
            }
        }
        BUSTER_TEST(arguments, inventory_exact && alias_count == 27 && not64_count == 2);
        BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS] == 27 &&
                                   counts.reason_counts[BUSTER_X86_METADATA_REASON_DECODE_ALIAS] == 27);
        BUSTER_TEST(arguments, alias_apis_closed);
    }
    BusterX86MetadataValidationResult validation = {0};
    BUSTER_TEST(arguments, buster_x86_metadata_schema_version() == 3);
    BUSTER_TEST(arguments, buster_x86_metadata_form_count() == 11013);
    BUSTER_TEST(arguments, buster_x86_metadata_normalized_form_count() == 10607);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage_count() == 11013);
    BUSTER_TEST(arguments, buster_x86_metadata_operand_count() == 32813);
    BUSTER_TEST(arguments, buster_x86_metadata_string_pool_size() == 1726254);
    BUSTER_TEST(arguments, buster_x86_metadata_validate(&validation) && validation.valid &&
                               validation.error == BUSTER_X86_METADATA_VALIDATION_NONE);
    BUSTER_TEST(arguments, counts.total_form_count == 11013 && counts.normalized_form_count == 10607 && counts.coverage_count == 11013);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_DIRECT] == 0);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_NORMALIZED] == 10607);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_NOT64] == 270);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_PRIVILEGED] == 109);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_RESERVED] == 0);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_UNSUPPORTED_TOKEN] == 0);
    BUSTER_TEST(arguments, counts.coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_UNCLASSIFIED] == 0);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_NONE] == 10607);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_MODE_NOT64] == 270);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_CPL0] == 109);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_UNKNOWN_PATTERN_TOKEN] == 0);
    BUSTER_TEST(arguments, counts.reason_counts[BUSTER_X86_METADATA_REASON_UNKNOWN_OPERAND_TOKEN] == 0);

    {
        // ACE-1's BSRMOV rows are the complete norexr_r4 cohort in this
        // snapshot. The token constrains EVEX R'; REG[0b000] fixes low R,
        // while BSR0 remains schema-hidden but may be spelled explicitly by
        // source syntax to select the directional form.
        static u32 const ace_r4_form_ids[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        bool ace_rows_consistent = true;
        bool ace_ids_are_complete = true;
        u32 ace_r4_token_count = 0;
        u32 ace_visible_operand_count = 0;
        u32 ace_implicit_operand_count = 0;
        for (u32 form_id = 0; form_id < buster_x86_metadata_form_count(); form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form))
            {
                ace_rows_consistent = false;
                continue;
            }
            if (x86_64_metadata_test_pattern_has_token(form.pattern, S8("norexr_r4")))
            {
                ace_r4_token_count += 1;
                ace_ids_are_complete &= form_id >= ace_r4_form_ids[0] && form_id <= ace_r4_form_ids[BUSTER_ARRAY_LENGTH(ace_r4_form_ids) - 1];
            }
        }
        for (u32 ace_index = 0; ace_index < BUSTER_ARRAY_LENGTH(ace_r4_form_ids); ace_index += 1)
        {
            u32 form_id = ace_r4_form_ids[ace_index];
            BusterX86MetadataForm form = {0};
            ace_rows_consistent &= buster_x86_metadata_form(form_id, &form) && form.id == form_id &&
                                   form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                   form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX &&
                                   form.encoder_family == BUSTER_X86_METADATA_ENCODER_EVEX &&
                                   x86_64_metadata_test_string_equal(form.isa_set, S8("ACE_1")) &&
                                   x86_64_metadata_test_pattern_has_token(form.pattern, S8("norexr_r4")) &&
                                   // Low R is fixed independently by the
                                   // schema's REG[0b000] atom; norexr_r4
                                   // supplies only EVEX R4.
                                   x86_64_metadata_test_pattern_has_token(form.pattern, S8("REG[0b000]"));
            u32 visible_count = 0;
            u32 implicit_count = 0;
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
            {
                BusterX86MetadataOperand metadata = {0};
                ace_rows_consistent &= buster_x86_metadata_operand(form_id, operand_index, &metadata);
                if (!metadata.visible)
                {
                    ace_rows_consistent &= (metadata.access & BUSTER_X86_METADATA_ACCESS_IMPLICIT) != 0;
                }
                else
                {
                    visible_count += 1;
                }
                if ((metadata.access & BUSTER_X86_METADATA_ACCESS_IMPLICIT) != 0)
                {
                    implicit_count += 1;
                    ace_rows_consistent &= !metadata.visible && metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
                                           metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_FIXED &&
                                           x86_64_metadata_test_string_equal(metadata.atom, S8("XED_REG_BSR0"));
                }
            }
            ace_visible_operand_count += visible_count;
            ace_implicit_operand_count += implicit_count;
            ace_rows_consistent &= implicit_count == 1;
        }
        BusterX86MetadataForm bsrinit_form = {0};
        u32 norexr_prefix_count = 0;
        u32 norexr_prefix_form_id = UINT32_MAX;
        u32 norexr_prefix_visible_reg_count = 0;
        for (u32 form_id = 0; form_id < buster_x86_metadata_form_count(); form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            if (!x86_64_metadata_test_pattern_has_token(form.pattern, S8("norexr_prefix"))) continue;
            norexr_prefix_count += 1;
            norexr_prefix_form_id = form_id;
            for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
            {
                BusterX86MetadataOperand operand = {0};
                if (buster_x86_metadata_operand(form_id, operand_index, &operand) && operand.visible &&
                    operand.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_REG)
                    norexr_prefix_visible_reg_count += 1;
            }
        }
        BusterX86MetadataForm previous_ace_form = {0};
        BusterX86MetadataForm next_ace_form = {0};
        // The ACE EVEX row immediately preceding BSRINIT and the first VEX
        // row following it retain their existing families; only the typed
        // norexr_prefix row is normalized from stale packed metadata.
        bool neighboring_prefixes_unchanged = buster_x86_metadata_form(6, &previous_ace_form) &&
                                               previous_ace_form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX &&
                                               previous_ace_form.encoder_family == BUSTER_X86_METADATA_ENCODER_EVEX &&
                                               buster_x86_metadata_form(31, &next_ace_form) &&
                                               next_ace_form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX &&
                                               next_ace_form.encoder_family == BUSTER_X86_METADATA_ENCODER_VEX;
        BUSTER_TEST(arguments, ace_r4_token_count == BUSTER_ARRAY_LENGTH(ace_r4_form_ids) && ace_ids_are_complete &&
                                   ace_rows_consistent && ace_visible_operand_count == 12 && ace_implicit_operand_count == 10);
        BUSTER_TEST(arguments, buster_x86_metadata_form(30, &bsrinit_form) && norexr_prefix_count == 1 &&
                                   norexr_prefix_form_id == 30 && norexr_prefix_visible_reg_count == 0);
        BUSTER_TEST(arguments, bsrinit_form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX &&
                                   bsrinit_form.encoder_family == BUSTER_X86_METADATA_ENCODER_VEX &&
                                   x86_64_metadata_test_pattern_has_token(bsrinit_form.pattern, S8("norexr_prefix")) &&
                                   !x86_64_metadata_test_pattern_has_token(bsrinit_form.pattern, S8("norexr_r4")));
        BUSTER_TEST(arguments, neighboring_prefixes_unchanged);
        String8 ace_feature[] = {S8("ACE_1")};

        // BSRINIT has no visible operands: its exact BSR0 register is a
        // fixed hidden field and the VEX bytes are independent of any source
        // register.  Both the canonical ACE_1 feature spelling and the
        // wildcard feature gate must select the same five-byte form.
        u8 const bsrinit_bytes[] = {0xc4, 0xe2, 0xfb, 0x49, 0xc0};
        String8 wildcard_feature[] = {S8("*")};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSRINIT"), 30, 0, 0,
                                                                (BusterX86MetadataPhysicalAttributes){0}, ace_feature,
                                                                BUSTER_ARRAY_LENGTH(ace_feature), bsrinit_bytes,
                                                                BUSTER_ARRAY_LENGTH(bsrinit_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSRINIT"), 30, 0, 0,
                                                                (BusterX86MetadataPhysicalAttributes){0}, wildcard_feature,
                                                                BUSTER_ARRAY_LENGTH(wildcard_feature), bsrinit_bytes,
                                                                BUSTER_ARRAY_LENGTH(bsrinit_bytes)));
        BusterX86MetadataPhysicalOperand bsr0_operand =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 0, 64);
        BusterX86MetadataPhysicalOperand fake_special_operand =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 1, 64);
        BusterX86MetadataPhysicalOperand high_special_operand =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 8, 64);
        BusterX86MetadataPhysicalOperand high_gpr_operand =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 64);
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSRINIT"), 30, &bsr0_operand, 1,
                                                                (BusterX86MetadataPhysicalAttributes){0}, ace_feature,
                                                                BUSTER_ARRAY_LENGTH(ace_feature), bsrinit_bytes,
                                                                BUSTER_ARRAY_LENGTH(bsrinit_bytes)));
        BusterX86MetadataPhysicalQuery source_bsr0_query = x86_64_metadata_test_physical_query(
            S8("BSRINIT"), &bsr0_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, ace_feature,
            BUSTER_ARRAY_LENGTH(ace_feature));
        source_bsr0_query.source_semantics = true;
        BusterX86MetadataEmitQuery source_bsr0_emit_query = {0};
        source_bsr0_emit_query.physical = source_bsr0_query;
        source_bsr0_emit_query.form_id = 30;
        source_bsr0_emit_query.output = (u8[32]){0};
        source_bsr0_emit_query.output_capacity = 32;
        source_bsr0_emit_query.relocations = (BusterX86MetadataRelocation[8]){0};
        source_bsr0_emit_query.relocation_capacity = 8;
        BusterX86MetadataEmitResult source_bsr0_result = buster_x86_metadata_emit_form(source_bsr0_emit_query);
        BUSTER_TEST(arguments, source_bsr0_result.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);
        BusterX86MetadataEmitResult fake_special_result = x86_64_metadata_test_emit_form(
            S8("BSRINIT"), 30, &fake_special_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, ace_feature,
            BUSTER_ARRAY_LENGTH(ace_feature), (u8[32]){0}, 32, (BusterX86MetadataRelocation[8]){0}, 8);
        BusterX86MetadataEmitResult high_special_result = x86_64_metadata_test_emit_form(
            S8("BSRINIT"), 30, &high_special_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, ace_feature,
            BUSTER_ARRAY_LENGTH(ace_feature), (u8[32]){0}, 32, (BusterX86MetadataRelocation[8]){0}, 8);
        BusterX86MetadataEmitResult high_gpr_result = x86_64_metadata_test_emit_form(
            S8("BSRINIT"), 30, &high_gpr_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, ace_feature,
            BUSTER_ARRAY_LENGTH(ace_feature), (u8[32]){0}, 32, (BusterX86MetadataRelocation[8]){0}, 8);
        BUSTER_TEST(arguments, fake_special_result.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   high_special_result.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   high_gpr_result.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);
        BusterX86MetadataPhysicalQuery include_implicit_query = x86_64_metadata_test_physical_query(
            S8("BSRINIT"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, ace_feature, BUSTER_ARRAY_LENGTH(ace_feature));
        include_implicit_query.include_implicit = true;
        BusterX86MetadataEmitQuery include_implicit_emit_query = {0};
        include_implicit_emit_query.physical = include_implicit_query;
        include_implicit_emit_query.form_id = 30;
        include_implicit_emit_query.output = (u8[32]){0};
        include_implicit_emit_query.output_capacity = 32;
        include_implicit_emit_query.relocations = (BusterX86MetadataRelocation[8]){0};
        include_implicit_emit_query.relocation_capacity = 8;
        BusterX86MetadataEmitResult include_implicit_missing = buster_x86_metadata_emit_form(include_implicit_emit_query);
        include_implicit_query.operands = &bsr0_operand;
        include_implicit_query.operand_count = 1;
        include_implicit_emit_query.physical = include_implicit_query;
        BusterX86MetadataEmitResult include_implicit_explicit = buster_x86_metadata_emit_form(include_implicit_emit_query);
        BUSTER_TEST(arguments, include_implicit_missing.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   include_implicit_explicit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS);
        BusterX86MetadataPhysicalQuery missing_feature_query = x86_64_metadata_test_physical_query(
            S8("BSRINIT"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, 0, 0);
        BusterX86MetadataEmitQuery missing_feature_emit_query = {0};
        missing_feature_emit_query.physical = missing_feature_query;
        missing_feature_emit_query.form_id = 30;
        missing_feature_emit_query.output = (u8[32]){0};
        missing_feature_emit_query.output_capacity = 32;
        missing_feature_emit_query.relocations = (BusterX86MetadataRelocation[8]){0};
        missing_feature_emit_query.relocation_capacity = 8;
        BusterX86MetadataEmitResult missing_feature_result = buster_x86_metadata_emit_form(missing_feature_emit_query);
        BUSTER_TEST(arguments, missing_feature_result.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);
        BUSTER_TEST(arguments, buster_x86_metadata_test_fixed_bsrinit_no_zeroing());

        // ACE_1 is the target feature for these rows.  AMX_TILE is the XED
        // category, and ACE is the extension spelling; neither substitutes
        // for the ISA-set feature.  Matching is intentionally case-insensitive.
        BusterX86MetadataPhysicalOperand gate_operands[16] = {0};
        char8 gate_mnemonic[128] = {0};
        BusterX86MetadataPhysicalQuery gate_query = {0};
        bool gate_query_built = x86_64_metadata_test_build_gate_query(6, &gate_query, gate_operands, gate_mnemonic);
        String8 ace_lower_feature[] = {S8("ace_1")};
        String8 ace_cli_feature[] = {S8("ace-1")};
        String8 amx_tile_feature[] = {S8("AMX_TILE")};
        String8 ace_extension_feature[] = {S8("ACE")};
        String8 no_feature[] = {0};
        gate_query.features.names = ace_feature;
        gate_query.features.count = BUSTER_ARRAY_LENGTH(ace_feature);
        BusterX86MetadataSelectResult ace_selected = buster_x86_metadata_select_form(gate_query);
        gate_query.features.names = ace_lower_feature;
        gate_query.features.count = BUSTER_ARRAY_LENGTH(ace_lower_feature);
        BusterX86MetadataSelectResult ace_lower_selected = buster_x86_metadata_select_form(gate_query);
        gate_query.features.names = ace_cli_feature;
        gate_query.features.count = BUSTER_ARRAY_LENGTH(ace_cli_feature);
        BusterX86MetadataSelectResult ace_cli_selected = buster_x86_metadata_select_form(gate_query);
        gate_query.features.names = amx_tile_feature;
        gate_query.features.count = BUSTER_ARRAY_LENGTH(amx_tile_feature);
        BusterX86MetadataSelectResult amx_tile_rejected = buster_x86_metadata_select_form(gate_query);
        gate_query.features.names = ace_extension_feature;
        gate_query.features.count = BUSTER_ARRAY_LENGTH(ace_extension_feature);
        BusterX86MetadataSelectResult ace_extension_rejected = buster_x86_metadata_select_form(gate_query);
        gate_query.features.names = no_feature;
        gate_query.features.count = 0;
        BusterX86MetadataSelectResult no_feature_rejected = buster_x86_metadata_select_form(gate_query);
        BUSTER_TEST(arguments, gate_query_built && ace_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && ace_selected.form_id == 6 &&
                                   ace_lower_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && ace_lower_selected.form_id == 6 &&
                                   ace_cli_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && ace_cli_selected.form_id == 6 &&
                                   amx_tile_rejected.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   ace_extension_rejected.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   no_feature_rejected.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);

        // These bytes are inferred from XED's EVV/MAP6/W/VF atoms and this
        // runtime's EVEX builder. LLVM 22 rejects BSRMOVF/BSRMOVH/BSRMOVL,
        // so there is deliberately no LLVM byte oracle for this ACE-1 cohort.
        BusterX86MetadataPhysicalOperand bsr_movf_registers[] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 2, 512),
        };
        BusterX86MetadataPhysicalOperand bsr_movf_memory[] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
        };
        BusterX86MetadataPhysicalOperand bsr_movh_register[] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
        };
        BusterX86MetadataPhysicalOperand bsr_movh_memory[] = {
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
        };
        BusterX86MetadataPhysicalOperand bsr_movh_load_explicit[] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
        };
        BusterX86MetadataPhysicalOperand bsr_movh_store_explicit_memory[] = {
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 0, 64),
        };
        BusterX86MetadataPhysicalOperand bsr_movl_register[] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
        };
        BusterX86MetadataPhysicalOperand bsr_movl_memory[] = {
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
        };
        u8 const bsr_movf_register_bytes[] = {0x62, 0xf6, 0xf4, 0x48, 0x95, 0xc2};
        u8 const bsr_movf_memory_bytes[] = {0x62, 0xf6, 0xf4, 0x48, 0x95, 0x00};
        u8 const bsr_movh_load_register_bytes[] = {0x62, 0xf6, 0xff, 0x48, 0x95, 0xc1};
        u8 const bsr_movh_load_memory_bytes[] = {0x62, 0xf6, 0xff, 0x48, 0x95, 0x00};
        u8 const bsr_movh_store_register_bytes[] = {0x62, 0xf6, 0x7f, 0x48, 0x95, 0xc1};
        u8 const bsr_movh_store_memory_bytes[] = {0x62, 0xf6, 0x7f, 0x48, 0x95, 0x00};
        u8 const bsr_movl_load_register_bytes[] = {0x62, 0xf6, 0xfe, 0x48, 0x95, 0xc1};
        u8 const bsr_movl_load_memory_bytes[] = {0x62, 0xf6, 0xfe, 0x48, 0x95, 0x00};
        u8 const bsr_movl_store_register_bytes[] = {0x62, 0xf6, 0x7e, 0x48, 0x95, 0xc1};
        u8 const bsr_movl_store_memory_bytes[] = {0x62, 0xf6, 0x7e, 0x48, 0x95, 0x00};
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVF"), 6, bsr_movf_registers,
                                                     BUSTER_ARRAY_LENGTH(bsr_movf_registers), (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movf_register_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movf_register_bytes)));
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVF"), 7, bsr_movf_memory,
                                                     BUSTER_ARRAY_LENGTH(bsr_movf_memory), (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movf_memory_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movf_memory_bytes)));
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVH"), 8, bsr_movh_load_explicit,
                                                     BUSTER_ARRAY_LENGTH(bsr_movh_load_explicit),
                                                     (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movh_load_register_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movh_load_register_bytes)));
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVH"), 9, bsr_movh_memory,
                                                     BUSTER_ARRAY_LENGTH(bsr_movh_memory), (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movh_load_memory_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movh_load_memory_bytes)));
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVH"), 10, bsr_movh_register,
                                                     BUSTER_ARRAY_LENGTH(bsr_movh_register), (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movh_store_register_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movh_store_register_bytes)));
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVH"), 11, bsr_movh_store_explicit_memory,
                                                     BUSTER_ARRAY_LENGTH(bsr_movh_store_explicit_memory),
                                                     (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movh_store_memory_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movh_store_memory_bytes)));
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVL"), 12, bsr_movl_register,
                                                     BUSTER_ARRAY_LENGTH(bsr_movl_register), (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movl_load_register_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movl_load_register_bytes)));
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVL"), 13, bsr_movl_memory,
                                                     BUSTER_ARRAY_LENGTH(bsr_movl_memory), (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movl_load_memory_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movl_load_memory_bytes)));
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVL"), 14, bsr_movl_register,
                                                     BUSTER_ARRAY_LENGTH(bsr_movl_register), (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movl_store_register_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movl_store_register_bytes)));
        BUSTER_TEST(arguments,
                    x86_64_metadata_test_emit_exact(S8("BSRMOVL"), 15, bsr_movl_memory,
                                                     BUSTER_ARRAY_LENGTH(bsr_movl_memory), (BusterX86MetadataPhysicalAttributes){0},
                                                     ace_feature, BUSTER_ARRAY_LENGTH(ace_feature), bsr_movl_store_memory_bytes,
                                                     BUSTER_ARRAY_LENGTH(bsr_movl_store_memory_bytes)));
    }

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
    u64 metadata_worker_count = buster_test_worker_count(X86_64_METADATA_TEST_MAX_THREAD_COUNT);
    BUSTER_TEST(arguments, x86_64_metadata_test_concurrent_lookup_stress((u32)metadata_worker_count));
    BUSTER_TEST(arguments, x86_64_metadata_test_concurrent_exact_emit_stress((u32)metadata_worker_count));
    BusterX86MetadataPhysicalOperand concurrent_memory_operands[2] = {
        x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
        x86_64_metadata_test_physical_mem_base(3, 64, 0),
    };
    BUSTER_TEST(arguments, x86_64_metadata_test_concurrent_exact_emit_stress_case(
                               (u32)metadata_worker_count,
                               (BusterX86MetadataFormKey){9845u, UINT64_C(0xca30e68cfa1406bc)}, concurrent_memory_operands,
                               BUSTER_ARRAY_LENGTH(concurrent_memory_operands)));
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
        // Exact keys are durable only when both halves agree.  Emission uses
        // the key's row directly, accepts an absent source mnemonic, and
        // remains byte/fixup-equivalent to the ordinary form path.
        BusterX86MetadataFormKey nop_key = {0};
        BusterX86MetadataForm nop_form = {0};
        BusterX86MetadataFormKey hash_key = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form_key(9852, &nop_key) && buster_x86_metadata_lookup_form_key(nop_key, &nop_form) &&
                                   nop_form.id == nop_key.form_id && nop_form.stable_hash == nop_key.stable_hash &&
                                   buster_x86_metadata_form_key_from_stable_hash(nop_key.stable_hash, &hash_key) &&
                                   hash_key.form_id == nop_key.form_id && hash_key.stable_hash == nop_key.stable_hash &&
                                   buster_x86_metadata_form_key_valid(nop_key));
        BusterX86MetadataPhysicalOperand mov_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_imm(UINT64_C(0x12345678), 64),
        };
        String8 wildcard[1] = {S8("*")};
        u8 ordinary_bytes[16] = {0};
        u8 exact_bytes[16] = {0};
        BusterX86MetadataRelocation ordinary_relocations[2] = {0};
        BusterX86MetadataRelocation exact_relocations[2] = {0};
        BusterX86MetadataPhysicalQuery ordinary_physical = x86_64_metadata_test_physical_query(
            S8("MOV"), mov_operands, BUSTER_ARRAY_LENGTH(mov_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataEmitResult ordinary = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = ordinary_physical,
            .form_id = 10018,
            .output = ordinary_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(ordinary_bytes),
            .relocations = ordinary_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(ordinary_relocations),
        });
        BusterX86MetadataFormKey mov_key = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form_key(10018, &mov_key) &&
                                   buster_x86_metadata_form_key_from_id(10018, &hash_key) &&
                                   hash_key.form_id == mov_key.form_id && hash_key.stable_hash == mov_key.stable_hash);
        BusterX86MetadataEmitResult exact = x86_64_metadata_test_emit_named_exact(
            mov_key, mov_operands, BUSTER_ARRAY_LENGTH(mov_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), exact_bytes, BUSTER_ARRAY_LENGTH(exact_bytes), exact_relocations,
            BUSTER_ARRAY_LENGTH(exact_relocations));
        u8 exact_query_bytes[16] = {0};
        BusterX86MetadataRelocation exact_query_relocations[2] = {0};
        BusterX86MetadataEmitResult exact_query = x86_64_metadata_test_emit_exact_query(
            mov_key, mov_operands, BUSTER_ARRAY_LENGTH(mov_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), exact_query_bytes, BUSTER_ARRAY_LENGTH(exact_query_bytes), exact_query_relocations,
            BUSTER_ARRAY_LENGTH(exact_query_relocations));
        BUSTER_TEST(arguments, ordinary.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && exact.status == ordinary.status &&
                                   exact.form_id == mov_key.form_id && exact.stable_hash == mov_key.stable_hash &&
                                   exact.byte_count == ordinary.byte_count && exact.relocation_count == ordinary.relocation_count &&
                                   x86_64_metadata_test_bytes_equal(exact_bytes, exact.byte_count, ordinary_bytes, ordinary.byte_count) &&
                                   memcmp(exact_relocations, ordinary_relocations,
                                          exact.relocation_count * sizeof(*exact_relocations)) == 0);
        BUSTER_TEST(arguments, exact_query.status == exact.status && exact_query.form_id == mov_key.form_id &&
                                   exact_query.stable_hash == mov_key.stable_hash && exact_query.byte_count == exact.byte_count &&
                                   exact_query.relocation_count == exact.relocation_count &&
                                   x86_64_metadata_test_bytes_equal(exact_query_bytes, exact_query.byte_count, exact_bytes, exact.byte_count) &&
                                   memcmp(exact_query_relocations, exact_relocations,
                                          exact_query.relocation_count * sizeof(*exact_query_relocations)) == 0);
        BusterX86MetadataEmitResult output_too_small = x86_64_metadata_test_emit_exact_query(
            mov_key, mov_operands, BUSTER_ARRAY_LENGTH(mov_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), exact_query_bytes, 0, exact_query_relocations,
            BUSTER_ARRAY_LENGTH(exact_query_relocations));
        BUSTER_TEST(arguments, output_too_small.status == BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY &&
                                   output_too_small.required_byte_count == exact.byte_count && output_too_small.byte_count == 0 &&
                                   output_too_small.relocation_count == 0);
        BusterX86MetadataFormKey stale_key = mov_key;
        stale_key.stable_hash ^= UINT64_C(1);
        BusterX86MetadataEmitResult stale = x86_64_metadata_test_emit_named_exact(
            stale_key, mov_operands, BUSTER_ARRAY_LENGTH(mov_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), exact_bytes, BUSTER_ARRAY_LENGTH(exact_bytes), exact_relocations,
            BUSTER_ARRAY_LENGTH(exact_relocations));
        BusterX86MetadataEmitResult stale_query = x86_64_metadata_test_emit_exact_query(
            stale_key, mov_operands, BUSTER_ARRAY_LENGTH(mov_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), exact_query_bytes, BUSTER_ARRAY_LENGTH(exact_query_bytes), exact_query_relocations,
            BUSTER_ARRAY_LENGTH(exact_query_relocations));
        BusterX86MetadataEmitResult reserved_query = buster_x86_metadata_emit_exact_query((BusterX86MetadataExactQuery){
            .key = mov_key,
            .operands = mov_operands,
            .operand_count = BUSTER_ARRAY_LENGTH(mov_operands),
            .features = {.names = wildcard, .count = BUSTER_ARRAY_LENGTH(wildcard)},
            .address_size = 64,
            .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            .reserved = 1,
            .output = exact_query_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(exact_query_bytes),
            .relocations = exact_query_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(exact_query_relocations),
        });
        BusterX86MetadataFormKey bad_id_key = mov_key;
        bad_id_key.form_id = buster_x86_metadata_form_count();
        BusterX86MetadataEmitResult bad_id = x86_64_metadata_test_emit_named_exact(
            bad_id_key, mov_operands, BUSTER_ARRAY_LENGTH(mov_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), exact_bytes, BUSTER_ARRAY_LENGTH(exact_bytes), exact_relocations,
            BUSTER_ARRAY_LENGTH(exact_relocations));
        BUSTER_TEST(arguments, stale.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM &&
                                   stale_query.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM &&
                                   reserved_query.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   reserved_query.form_id == mov_key.form_id &&
                                   bad_id.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM &&
                                   !buster_x86_metadata_form_key_valid(stale_key) && !buster_x86_metadata_form_key_valid(bad_id_key));
    }

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
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_16));
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_32, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_32));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_32, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_ANY));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_32, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_ANY));
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, true,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_ANY));
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_32, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, true,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_ANY));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_32));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_32, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_16));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   not64_mode, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_16));
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_32, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_32));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_32, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_ANY));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_32, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_ANY));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, true,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_16, BUSTER_X86_METADATA_COVERAGE_NORMALIZED, true,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_ANY));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_NOT64, BUSTER_X86_METADATA_COVERAGE_NOT64, false,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_NOT64, BUSTER_X86_METADATA_COVERAGE_NOT64, true,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_64));
        BUSTER_TEST(arguments, buster_x86_metadata_test_execution_mode_matches(
                                   BUSTER_X86_METADATA_MODE_NOT64, BUSTER_X86_METADATA_COVERAGE_NOT64, true,
                                   BUSTER_X86_METADATA_EXECUTION_MODE_ANY));

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
                    .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_ANY,
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

    {
        // The audit is a complete capability ledger, not a best-effort
        // sample.  Capacity and row retrieval are both part of `complete`.
        u32 ledger_capacity = 11013;
        BusterX86MetadataCoverageLedgerEntry* ledger =
            arena_allocate(arguments->arena, BusterX86MetadataCoverageLedgerEntry, ledger_capacity);
        BusterX86MetadataCoverageAuditResult no_storage = buster_x86_metadata_coverage_audit(0, 0);
        BusterX86MetadataCoverageAuditResult short_storage = buster_x86_metadata_coverage_audit(ledger, 11012);
        BusterX86MetadataCoverageAuditResult audit = buster_x86_metadata_coverage_audit(ledger, ledger_capacity);
        String8 const cohort_tokens[] = {
            S8_INITIALIZER("ONE()"), S8_INITIALIZER("IGNORE66()"), S8_INITIALIZER("IMMUNE66()"),
            S8_INITIALIZER("LZCNT=1"), S8_INITIALIZER("TZCNT=1"), S8_INITIALIZER("CLDEMOTE=1"),
            S8_INITIALIZER("CET=1"), S8_INITIALIZER("PREFETCHIT=1"), S8_INITIALIZER("PREFETCHRST=1"), S8_INITIALIZER("CET_NO_TRACK()"),
        };
        u32 cohort_counts[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_all_counts[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_privileged[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_not64[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_capable[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        u32 cohort_emitted[BUSTER_ARRAY_LENGTH(cohort_tokens)] = {0};
        // Both cohort sweeps below need the same per-form token answers, so
        // resolve each form's pattern against the ten tokens once.
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(cohort_tokens) <= 16);
        u16* cohort_masks = arena_allocate(arguments->arena, u16, audit.entry_count ? audit.entry_count : 1);
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            cohort_masks[form_id] = 0;
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 cohort = 0; cohort < BUSTER_ARRAY_LENGTH(cohort_tokens); cohort += 1)
            {
                if (x86_64_metadata_test_pattern_has_token(form.pattern, cohort_tokens[cohort]))
                {
                    cohort_masks[form_id] |= (u16)(1u << cohort);
                }
            }
        }
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 cohort = 0; cohort < BUSTER_ARRAY_LENGTH(cohort_tokens); cohort += 1)
            {
                if (cohort_masks[form_id] & (1u << cohort))
                {
                    cohort_all_counts[cohort] += 1;
                    cohort_counts[cohort] += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED;
                    cohort_privileged[cohort] += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED;
                    cohort_not64[cohort] += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64;
                    cohort_capable[cohort] += ledger[form_id].encoder_capable;
                    cohort_emitted[cohort] += ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
                }
            }
        }
        static u32 const cohort_all_expected[] = {200, 105, 34, 2, 2, 1, 4, 2, 1, 4};
        static u32 const cohort_normalized_expected[] = {200, 103, 26, 2, 2, 1, 4, 2, 1, 4};
        static u32 const cohort_privileged_expected[] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
        bool cohort_rows_consistent = true;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 cohort = 0; cohort < BUSTER_ARRAY_LENGTH(cohort_tokens); cohort += 1)
            {
                if (!(cohort_masks[form_id] & (1u << cohort))) continue;
                BusterX86MetadataCoverageLedgerEntry entry = ledger[form_id];
                bool row_consistent = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                       entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                       entry.blocker == BUSTER_X86_METADATA_BLOCKER_NONE && entry.encoder_capable;
                row_consistent |= form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED &&
                                  entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                  entry.blocker == BUSTER_X86_METADATA_BLOCKER_PRIVILEGED && entry.encoder_capable && entry.policy_excluded;
                row_consistent |= form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64 &&
                                  entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                  entry.blocker == BUSTER_X86_METADATA_BLOCKER_NOT64 && !entry.encoder_capable;
                cohort_rows_consistent &= row_consistent;
            }
        }
        bool cohort_counts_match = cohort_rows_consistent;
        for (u32 cohort = 0; cohort < BUSTER_ARRAY_LENGTH(cohort_tokens); cohort += 1)
        {
            cohort_counts_match &= cohort_all_counts[cohort] == cohort_all_expected[cohort];
            cohort_counts_match &= cohort_counts[cohort] == cohort_normalized_expected[cohort];
            cohort_counts_match &= cohort_privileged[cohort] == cohort_privileged_expected[cohort];
            cohort_counts_match &= cohort_all_counts[cohort] == cohort_counts[cohort] + cohort_privileged[cohort] + cohort_not64[cohort];
            cohort_counts_match &= cohort_capable[cohort] == cohort_counts[cohort] + cohort_privileged[cohort];
            cohort_counts_match &= cohort_emitted[cohort] == cohort_counts[cohort];
        }
        BUSTER_TEST(arguments, cohort_counts_match);
        typedef struct X86_64MetadataBooleanCohortCase X86_64MetadataBooleanCohortCase;
        struct X86_64MetadataBooleanCohortCase
        {
            String8 token;
            u32 expected_ids[4];
            u8 expected_count;
        };
        static X86_64MetadataBooleanCohortCase const boolean_cohort_cases[] = {
            {S8_INITIALIZER("CLDEMOTE=0"), {7978, UINT32_MAX}, 1},
            {S8_INITIALIZER("CLDEMOTE=1"), {7969, UINT32_MAX}, 1},
            {S8_INITIALIZER("LZCNT=0"), {8106, 8107}, 2},
            {S8_INITIALIZER("LZCNT=1"), {8102, 8103}, 2},
            {S8_INITIALIZER("TZCNT=0"), {8689, 8690}, 2},
            {S8_INITIALIZER("TZCNT=1"), {8685, 8686}, 2},
            {S8_INITIALIZER("IBHF=0"), {8692, UINT32_MAX}, 1},
            {S8_INITIALIZER("IBHF=1"), {8691, UINT32_MAX}, 1},
            {S8_INITIALIZER("PREFETCHRST=0"), {9572, UINT32_MAX}, 1},
            {S8_INITIALIZER("PREFETCHRST=1"), {8780, UINT32_MAX}, 1},
            {S8_INITIALIZER("PREFETCHIT=0"), {9573, 9574}, 2},
            {S8_INITIALIZER("PREFETCHIT=1"), {8694, 8695}, 2},
            {S8_INITIALIZER("CET_NO_TRACK()"), {9483, 9484, 9487, 9488}, 4},
        };
        bool boolean_cohort_ok = true;
        for (u32 cohort_index = 0; cohort_index < BUSTER_ARRAY_LENGTH(boolean_cohort_cases); cohort_index += 1)
        {
            X86_64MetadataBooleanCohortCase cohort = boolean_cohort_cases[cohort_index];
            u32 actual_count = 0;
            for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
            {
                BusterX86MetadataForm form = {0};
                if (!buster_x86_metadata_form(form_id, &form) ||
                    !x86_64_metadata_test_pattern_has_token(form.pattern, cohort.token))
                    continue;
                actual_count += 1;
                bool expected_id = false;
                for (u32 expected_index = 0; expected_index < cohort.expected_count; expected_index += 1)
                    expected_id |= form_id == cohort.expected_ids[expected_index];
                boolean_cohort_ok &= expected_id;
                bool decode_alias = cohort_index == 0 || cohort_index == 2 || cohort_index == 4 ||
                                    cohort_index == 6 || cohort_index == 8 || cohort_index == 10;
                boolean_cohort_ok &= decode_alias ? form.coverage_class == BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS
                                                   : form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED;
                if (decode_alias)
                    boolean_cohort_ok &= !ledger[form_id].encoder_capable &&
                                         ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                         ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_DECODE_ALIAS;
                else
                    boolean_cohort_ok &= ledger[form_id].encoder_capable &&
                                         ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                         ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_NONE;
            }
            boolean_cohort_ok &= actual_count == cohort.expected_count;
            for (u32 expected_index = 0; expected_index < cohort.expected_count; expected_index += 1)
            {
                BusterX86MetadataForm form = {0};
                u32 expected_id = cohort.expected_ids[expected_index];
                boolean_cohort_ok &= buster_x86_metadata_form(expected_id, &form) &&
                                     x86_64_metadata_test_pattern_has_token(form.pattern, cohort.token);
            }
        }
        BUSTER_TEST(arguments, boolean_cohort_ok);
        // The raw indirect branch inventory is intentionally closed: there
        // are exactly four 0xff ModRM CALL/JMP rows, and each carries the
        // CET_NO_TRACK marker.  This prevents a future untagged sibling from
        // silently changing default-versus-notrack selection semantics.
        static u32 const cet_no_track_ids[] = {9483, 9484, 9487, 9488};
        u32 indirect_branch_rows = 0;
        bool indirect_branch_inventory = true;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            if (!x86_64_metadata_test_pattern_has_token(form.pattern, S8("CET_NO_TRACK()"))) continue;
            indirect_branch_rows += 1;
            bool expected = false;
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cet_no_track_ids); index += 1)
                expected |= form_id == cet_no_track_ids[index];
            indirect_branch_inventory &= expected && x86_64_metadata_test_pattern_has_token(form.pattern, S8("CET_NO_TRACK()"));
        }
        BUSTER_TEST(arguments, indirect_branch_rows == BUSTER_ARRAY_LENGTH(cet_no_track_ids) && indirect_branch_inventory);
        BUSTER_TEST(arguments, !no_storage.complete && no_storage.required_entry_count == 11013 && no_storage.entry_count == 0);
        BUSTER_TEST(arguments, !short_storage.complete && short_storage.entry_count == 11012);
        BUSTER_TEST(arguments, audit.complete && !audit.duplicate_form_id && !audit.duplicate_stable_hash &&
                                   audit.entry_count == 11013 && audit.normalized_entry_count == 10607);
        static u32 const ace_r4_form_ids[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        bool ace_r4_ledger_unlocked = true;
        for (u32 ace_index = 0; ace_index < BUSTER_ARRAY_LENGTH(ace_r4_form_ids); ace_index += 1)
        {
            BusterX86MetadataCoverageLedgerEntry entry = ledger[ace_r4_form_ids[ace_index]];
            ace_r4_ledger_unlocked &= entry.form_id == ace_r4_form_ids[ace_index] &&
                                      entry.encoder_family == BUSTER_X86_METADATA_ENCODER_EVEX &&
                                      entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                      entry.blocker == BUSTER_X86_METADATA_BLOCKER_NONE && entry.encoder_capable;
        }
        BUSTER_TEST(arguments, ace_r4_ledger_unlocked && ledger[30].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                   ledger[30].blocker == BUSTER_X86_METADATA_BLOCKER_NONE && ledger[30].encoder_capable);
        // The residual-control stack contributes sixteen rows, legacy
        // DF64/IMMUNE controls contribute seventeen, IBHF=1 contributes one
        // boolean row, the ACE R4 cohort contributes ten, BSRINIT adds one,
        // and the fixed CET/IBHF NOP cohort contributes eleven newly capable
        // rows. MASKMOV contributes two more legacy rows, and CET_NO_TRACK
        // contributes the four indirect CALL/JMP rows. These are disjoint
        // normalized rows on top of the fixed NOT16 rows; REP_MONTMUL's
        // EAMODE16 row remains schema-blocked under XED's long-mode mapping.
        BUSTER_TEST(arguments, audit.emitted_count == 10607 && audit.blocked_count == 406 &&
                                   audit.disposition_counts[BUSTER_X86_METADATA_COVERAGE_EMITTED] == 10607 &&
                                   audit.disposition_counts[BUSTER_X86_METADATA_COVERAGE_BLOCKED] == 406);
        BUSTER_TEST(arguments, audit.encoder_capable_count == 10715 && audit.policy_excluded_count == 406 &&
                                   audit.explicitly_unsupported_count == 297 && audit.schema_inexpressible_count == 0);

        u32 expected_families[BUSTER_X86_METADATA_ENCODER_COUNT] = {1784, 197, 5, 1644, 176, 6728, 49, 24};
        u32 expected_family_emitted[BUSTER_X86_METADATA_ENCODER_COUNT] = {1784, 197, 5, 1644, 176, 6728, 49, 24};
        u32 expected_family_blocked[BUSTER_X86_METADATA_ENCODER_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
        bool family_counts_match = true;
        for (u32 family = 0; family < BUSTER_X86_METADATA_ENCODER_COUNT; family += 1)
        {
            family_counts_match &= audit.family_counts[family] == expected_families[family];
            family_counts_match &= audit.family_emitted_counts[family] == expected_family_emitted[family];
            family_counts_match &= audit.family_blocked_counts[family] == expected_family_blocked[family];
        }
        BUSTER_TEST(arguments, family_counts_match);

        u32 expected_blockers[BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT] = {10607, 270, 108, 1, 0, 0, 0, 0, 0, 0, 0, 0, 27};
        bool blocker_counts_match = true;
        for (u32 blocker = 0; blocker < BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT; blocker += 1)
            blocker_counts_match &= audit.blocker_counts[blocker] == expected_blockers[blocker];
        BUSTER_TEST(arguments, blocker_counts_match);
        BusterX86MetadataCoverageLedgerEntry montmul_ledger = ledger[9011];
        BusterX86MetadataCoverageLedgerEntry montmul_eamode32_ledger = ledger[9012];
        BusterX86MetadataCoverageLedgerEntry jcxz_ledger = ledger[10051];
        BusterX86MetadataCoverageLedgerEntry monitor_ledger = ledger[9598];
        BUSTER_TEST(arguments, montmul_ledger.form_id == 9011 && montmul_ledger.stable_hash == UINT64_C(0x43ea6607300874ad) &&
                                   montmul_ledger.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64 &&
                                   montmul_ledger.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY &&
                                   montmul_ledger.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                   montmul_ledger.blocker == BUSTER_X86_METADATA_BLOCKER_NOT64 &&
                                   !montmul_ledger.encoder_capable && montmul_ledger.policy_excluded &&
                                   montmul_eamode32_ledger.form_id == 9012 &&
                                   montmul_eamode32_ledger.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                   montmul_eamode32_ledger.blocker == BUSTER_X86_METADATA_BLOCKER_NONE &&
                                   montmul_eamode32_ledger.encoder_capable && jcxz_ledger.form_id == 10051 &&
                                   jcxz_ledger.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64 &&
                                   jcxz_ledger.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                   jcxz_ledger.blocker == BUSTER_X86_METADATA_BLOCKER_NOT64 && jcxz_ledger.policy_excluded && !jcxz_ledger.encoder_capable &&
                                   monitor_ledger.form_id == 9598 && monitor_ledger.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED &&
                                   monitor_ledger.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                   monitor_ledger.blocker == BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS &&
                                   !monitor_ledger.encoder_capable && monitor_ledger.policy_excluded);

        // The moffs OVERRIDE_SEG0 cohort is deliberately closed over the
        // four A0-A3 MOV rows.  MASKMOVQ/MASKMOVDQU carry the same source
        // token but use an implicit-DI memory topology whose hidden MEM0,
        // BASE0, and SEG0 records are supplemental encoding semantics.
        static u32 const moffs_ids[] = {9891, 9892, 9893, 9894};
        static u32 const implicit_di_ids[] = {10395, 10408};
        BusterX86MetadataPhysicalOperand maskmovq_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX, 1, 64),
        };
        String8 maskmov_features[1] = {S8("*")};
        BusterX86MetadataPhysicalQuery maskmovq_query = x86_64_metadata_test_physical_query(
            S8("MASKMOVQ"), maskmovq_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, maskmov_features, 1);
        BusterX86MetadataSelectResult maskmovq_selection = buster_x86_metadata_select_form(maskmovq_query);
        BusterX86MetadataPhysicalOperand maskmovdqu_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        BusterX86MetadataPhysicalQuery maskmovdqu_query = x86_64_metadata_test_physical_query(
            S8("MASKMOVDQU"), maskmovdqu_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, maskmov_features, 1);
        BusterX86MetadataSelectResult maskmovdqu_selection = buster_x86_metadata_select_form(maskmovdqu_query);
        BUSTER_TEST(arguments, maskmovq_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   maskmovq_selection.form_id == implicit_di_ids[0] && maskmovq_selection.candidate_count == 1 &&
                                   maskmovq_selection.selected_byte_count == 3);
        BUSTER_TEST(arguments, maskmovdqu_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   maskmovdqu_selection.form_id == implicit_di_ids[1] && maskmovdqu_selection.candidate_count == 1 &&
                                   maskmovdqu_selection.selected_byte_count == 4);
        u8 maskmovq_bytes[] = {0x0f, 0xf7, 0xc1};
        u8 maskmovdqu_bytes[] = {0x66, 0x0f, 0xf7, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MASKMOVQ"), implicit_di_ids[0], maskmovq_operands, 2,
                                                                (BusterX86MetadataPhysicalAttributes){0}, maskmov_features, 1,
                                                                maskmovq_bytes, BUSTER_ARRAY_LENGTH(maskmovq_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MASKMOVDQU"), implicit_di_ids[1], maskmovdqu_operands, 2,
                                                                (BusterX86MetadataPhysicalAttributes){0}, maskmov_features, 1,
                                                                maskmovdqu_bytes, BUSTER_ARRAY_LENGTH(maskmovdqu_bytes)));
        BusterX86MetadataPhysicalQuery maskmovq_segment_query = maskmovq_query;
        maskmovq_segment_query.attributes.implicit_segment = BUSTER_X86_METADATA_SEGMENT_FS;
        u8 maskmovq_segment_bytes[8] = {0};
        BusterX86MetadataEmitResult maskmovq_segment = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = maskmovq_segment_query, .form_id = implicit_di_ids[0],
                                         .output = maskmovq_segment_bytes,
                                         .output_capacity = BUSTER_ARRAY_LENGTH(maskmovq_segment_bytes)});
        u8 expected_maskmovq_segment[] = {0x64, 0x0f, 0xf7, 0xc1};
        BUSTER_TEST(arguments, maskmovq_segment.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(maskmovq_segment_bytes, maskmovq_segment.byte_count,
                                                                     expected_maskmovq_segment,
                                                                     BUSTER_ARRAY_LENGTH(expected_maskmovq_segment)));
        maskmovq_query.address_size = 32;
        u8 maskmovq_addr32_bytes[8] = {0};
        BusterX86MetadataEmitResult maskmovq_addr32 = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = maskmovq_query, .form_id = implicit_di_ids[0],
                                         .output = maskmovq_addr32_bytes, .output_capacity = BUSTER_ARRAY_LENGTH(maskmovq_addr32_bytes)});
        u8 expected_maskmovq_addr32[] = {0x67, 0x0f, 0xf7, 0xc1};
        BUSTER_TEST(arguments, maskmovq_addr32.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(maskmovq_addr32_bytes, maskmovq_addr32.byte_count,
                                                                     expected_maskmovq_addr32, BUSTER_ARRAY_LENGTH(expected_maskmovq_addr32)));
        maskmovdqu_query.address_size = 32;
        maskmovdqu_query.attributes.implicit_segment = BUSTER_X86_METADATA_SEGMENT_GS;
        u8 maskmovdqu_addr32_bytes[8] = {0};
        BusterX86MetadataEmitResult maskmovdqu_addr32 = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = maskmovdqu_query, .form_id = implicit_di_ids[1],
                                         .output = maskmovdqu_addr32_bytes, .output_capacity = BUSTER_ARRAY_LENGTH(maskmovdqu_addr32_bytes)});
        u8 expected_maskmovdqu_addr32[] = {0x65, 0x67, 0x66, 0x0f, 0xf7, 0xc1};
        BUSTER_TEST(arguments, maskmovdqu_addr32.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(maskmovdqu_addr32_bytes, maskmovdqu_addr32.byte_count,
                                                                     expected_maskmovdqu_addr32, BUSTER_ARRAY_LENGTH(expected_maskmovdqu_addr32)));
        bool moffs_rows_exact = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(moffs_ids); index += 1)
        {
            u32 form_id = moffs_ids[index];
            BusterX86MetadataForm form = {0};
            bool retrieved = buster_x86_metadata_form(form_id, &form);
            moffs_rows_exact &= retrieved && form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                form.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY &&
                                x86_64_metadata_test_pattern_has_token(form.pattern, S8("MEMDISPv()")) &&
                                x86_64_metadata_test_pattern_has_token(form.pattern, S8("OVERRIDE_SEG0()")) &&
                                ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_NONE && ledger[form_id].encoder_capable;
        }
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(implicit_di_ids); index += 1)
        {
            u32 form_id = implicit_di_ids[index];
            BusterX86MetadataForm form = {0};
            bool retrieved = buster_x86_metadata_form(form_id, &form);
            moffs_rows_exact &= retrieved && form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                x86_64_metadata_test_pattern_has_token(form.pattern, S8("OVERRIDE_SEG0()")) &&
                                ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_NONE && ledger[form_id].encoder_capable;
        }
        u32 moffs_token_count = 0;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (buster_x86_metadata_form(form_id, &form) &&
                x86_64_metadata_test_pattern_has_token(form.pattern, S8("MEMDISPv()")))
                moffs_token_count += 1;
        }
        moffs_rows_exact &= moffs_token_count == 4;
        BUSTER_TEST(arguments, moffs_rows_exact);

        String8 wildcard[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand moffs8 = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
            .width = 8,
            .memory = {
                .displacement = INT64_C(0x1122334455667788),
                .address_size = 64,
                .scale = 1,
                .has_displacement = true,
            },
        };
        BusterX86MetadataPhysicalOperand moffs64 = moffs8;
        moffs64.width = 64;
        u8 moffs_bytes_a0[] = {0xa0, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        u8 moffs_bytes_a1[] = {0xa1, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        u8 moffs_bytes_a2[] = {0xa2, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        u8 moffs_bytes_a3[] = {0xa3, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};

        // Exercise the public select/emit front door with an absolute
        // address outside ModRM's signed-32-bit range.  The source query
        // has only its visible memory operand; the accumulator and segment
        // operands are implicit/supplemental in the moffs schema.
        BusterX86MetadataPhysicalOperand front_moffs = moffs8;
        front_moffs.memory.has_segment = true;
        front_moffs.memory.segment = BUSTER_X86_METADATA_SEGMENT_ES;
        BusterX86MetadataPhysicalQuery front_query = x86_64_metadata_test_physical_query(
            S8("MOV"), &front_moffs, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult front_selection = buster_x86_metadata_select_form(front_query);
        BusterX86MetadataPhysicalOperand explicit_moffs[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8), front_moffs,
        };
        BusterX86MetadataPhysicalQuery explicit_query = x86_64_metadata_test_physical_query(
            S8("MOV"), explicit_moffs, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        explicit_query.source_semantics = true;
        BusterX86MetadataSelectResult explicit_selection = buster_x86_metadata_select_form(explicit_query);
        u8 explicit_bytes[32] = {0};
        BusterX86MetadataEmitResult explicit_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = explicit_query, .form_id = 9891, .output = explicit_bytes,
                                         .output_capacity = BUSTER_ARRAY_LENGTH(explicit_bytes)});
        u8 front_bytes[16] = {0};
        BusterX86MetadataRelocation front_relocations[1] = {0};
        BusterX86MetadataEmitResult front_emit = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = front_query,
            .form_id = front_selection.form_id,
            .output = front_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(front_bytes),
            .relocations = front_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(front_relocations),
        });
        u8 front_expected[] = {0x26, 0xa0, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        BUSTER_TEST(arguments, front_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   front_selection.form_id == 9891 && front_selection.candidate_count >= 2 &&
                                   front_selection.diagnostic_operand == 0 && front_selection.diagnostic_value == 0 &&
                                   explicit_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   explicit_selection.form_id == 9891 && explicit_selection.candidate_count >= 1 &&
                                   explicit_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && explicit_emit.byte_count == 10 &&
                                   front_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && front_emit.byte_count == 10 &&
                                   front_emit.relocation_count == 0 &&
                                   front_emit.diagnostic_operand == 0 && front_emit.diagnostic_value == 0 &&
                                   x86_64_metadata_test_bytes_equal(front_bytes, front_emit.byte_count, front_expected,
                                                                     BUSTER_ARRAY_LENGTH(front_expected)));

        bool moffs_bytes_exact = x86_64_metadata_test_emit_exact(S8("MOV"), 9891, &moffs8, 1,
                                                                  (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                  BUSTER_ARRAY_LENGTH(wildcard), moffs_bytes_a0,
                                                                  BUSTER_ARRAY_LENGTH(moffs_bytes_a0));
        moffs_bytes_exact &= x86_64_metadata_test_emit_exact(S8("MOV"), 9892, &moffs64, 1,
                                                             (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                             BUSTER_ARRAY_LENGTH(wildcard), moffs_bytes_a1,
                                                             BUSTER_ARRAY_LENGTH(moffs_bytes_a1));
        moffs_bytes_exact &= x86_64_metadata_test_emit_exact(S8("MOV"), 9893, &moffs8, 1,
                                                             (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                             BUSTER_ARRAY_LENGTH(wildcard), moffs_bytes_a2,
                                                             BUSTER_ARRAY_LENGTH(moffs_bytes_a2));
        moffs_bytes_exact &= x86_64_metadata_test_emit_exact(S8("MOV"), 9894, &moffs64, 1,
                                                             (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                             BUSTER_ARRAY_LENGTH(wildcard), moffs_bytes_a3,
                                                             BUSTER_ARRAY_LENGTH(moffs_bytes_a3));
        BUSTER_TEST(arguments, moffs_bytes_exact);

        BusterX86MetadataPhysicalOperand segment_moffs = moffs8;
        segment_moffs.memory.has_segment = true;
        segment_moffs.memory.segment = BUSTER_X86_METADATA_SEGMENT_ES;
        u8 segment_moffs_bytes[] = {0x26, 0xa0, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 9891, &segment_moffs, 1,
                                                                  (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                  BUSTER_ARRAY_LENGTH(wildcard), segment_moffs_bytes,
                                                                  BUSTER_ARRAY_LENGTH(segment_moffs_bytes)));
        BusterX86MetadataEmitResult segment_rep = x86_64_metadata_test_emit_form(
            S8("MOV"), 9891, &segment_moffs, 1, (BusterX86MetadataPhysicalAttributes){.rep = true}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[32]){0}, 32, 0, 0);
        BUSTER_TEST(arguments, segment_rep.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION &&
                                   segment_rep.byte_count == 0 && segment_rep.relocation_count == 0 &&
                                   segment_rep.diagnostic_operand == 0 && segment_rep.diagnostic_value == 0);
        BusterX86MetadataEmitResult hidden_segment_moffs = x86_64_metadata_test_emit_form(
            S8("MOV"), 9891, &segment_moffs,
            1, (BusterX86MetadataPhysicalAttributes){.implicit_segment = BUSTER_X86_METADATA_SEGMENT_FS}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[32]){0}, 32, 0, 0);
        BUSTER_TEST(arguments, hidden_segment_moffs.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   hidden_segment_moffs.byte_count == 0 && hidden_segment_moffs.relocation_count == 0);

        BusterX86MetadataPhysicalOperand address32_moffs = moffs64;
        address32_moffs.memory.address_size = 32;
        address32_moffs.memory.displacement = 0x12345678;
        BusterX86MetadataPhysicalQuery address32_query = x86_64_metadata_test_physical_query(
            S8("MOV"), &address32_moffs, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        address32_query.address_size = 32;
        u8 address32_bytes[16] = {0};
        BusterX86MetadataEmitResult address32_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = address32_query, .form_id = 9892, .output = address32_bytes,
                                         .output_capacity = BUSTER_ARRAY_LENGTH(address32_bytes)});
        BUSTER_TEST(arguments, address32_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && address32_emit.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(address32_bytes, address32_emit.byte_count,
                                                                     (u8[]){0x67, 0xa1, 0x78, 0x56, 0x34, 0x12}, 6));

        BusterX86MetadataPhysicalOperand symbol_moffs = moffs64;
        symbol_moffs.memory.displacement = 0;
        symbol_moffs.memory.addend = 7;
        symbol_moffs.memory.symbol = S8("moffs_target");
        symbol_moffs.memory.has_symbol = true;
        u8 symbol_bytes[16] = {0};
        BusterX86MetadataRelocation symbol_relocations[2] = {0};
        BusterX86MetadataEmitResult symbol_emit = x86_64_metadata_test_emit_form(
            S8("MOV"), 9894, &symbol_moffs, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), symbol_bytes, BUSTER_ARRAY_LENGTH(symbol_bytes), symbol_relocations,
            BUSTER_ARRAY_LENGTH(symbol_relocations));
        BUSTER_TEST(arguments, symbol_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && symbol_emit.byte_count == 9 &&
                                   symbol_emit.relocation_count == 1 && symbol_relocations[0].offset == 1 &&
                                   symbol_relocations[0].width == 8 &&
                                   symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE64 &&
                                   symbol_relocations[0].addend == 7);

        // MPXMODE is a deliberately closed raw-pattern cohort.  Keep the
        // snapshot-local IDs and signatures explicit so a regenerated table
        // cannot silently widen this typed control to neighboring NOT64 or
        // unrelated rows.  GNU as/LLVM MC reject MPX in the host toolchain;
        // the byte fixtures below therefore use the XED pattern bytes as the
        // external-oracle gap for this cohort.
        static u32 const mpxmode_ids[] = {
            8781, 8783, 8785, 8787, 8789, 8791, 8793, 8795, 8796, 8797, 8798, 8799, 8800, 8801, 8802,
            8804, 8805, 8806, 8808, 8809, 8810, 8811, 8812, 8813,
        };
        static u32 const mpxmode_alias_ids[] = {8814, 8815, 8816, 8817};
        static u32 const mpxmode_adjacent_ids[] = {8782, 8784, 8786, 8788, 8790, 8792, 8794, 8803, 8807};
        bool mpxmode_rows_exact = true;
        u32 mpxmode_one_count = 0;
        u32 mpxmode_zero_count = 0;
        u32 mpxmode_memory_count = 0;
        u32 mpxmode_refining_count = 0;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(mpxmode_ids); index += 1)
        {
            u32 form_id = mpxmode_ids[index];
            BusterX86MetadataForm form = {0};
            bool retrieved = buster_x86_metadata_form(form_id, &form);
            bool mpx_extension = retrieved && x86_64_metadata_test_string_equal(form.extension, S8("MPX"));
            bool base_nop = retrieved && x86_64_metadata_test_string_equal(form.extension, S8("BASE")) &&
                            x86_64_metadata_test_string_equal(form.isa_set, S8("PPRO")) &&
                            x86_64_metadata_test_string_equal(form.iclass, S8("NOP"));
            bool mode_one = retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("MPXMODE=1"));
            bool mode_zero = retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("MPXMODE=0"));
            bool mpx_mnemonic = retrieved &&
                                (x86_64_metadata_test_string_equal(form.iclass, S8("BNDMK")) ||
                                 x86_64_metadata_test_string_equal(form.iclass, S8("BNDCL")) ||
                                 x86_64_metadata_test_string_equal(form.iclass, S8("BNDCU")) ||
                                 x86_64_metadata_test_string_equal(form.iclass, S8("BNDCN")) ||
                                 x86_64_metadata_test_string_equal(form.iclass, S8("BNDMOV")) ||
                                 x86_64_metadata_test_string_equal(form.iclass, S8("BNDLDX")) ||
                                 x86_64_metadata_test_string_equal(form.iclass, S8("BNDSTX")));
            bool mode_value_valid = (mode_one ^ mode_zero) && (mode_one ? mpx_extension || base_nop : base_nop);
            mpxmode_rows_exact &= retrieved && form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                  ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED && ledger[form_id].encoder_capable &&
                                  ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_NONE && mode_value_valid &&
                                  (base_nop || mpx_mnemonic);
            mpxmode_one_count += mode_one;
            mpxmode_zero_count += mode_zero;
            mpxmode_memory_count += retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("MOD!=3"));
            mpxmode_refining_count += retrieved &&
                                      (x86_64_metadata_test_pattern_has_token(form.pattern, S8("f2_refining_prefix")) ||
                                       x86_64_metadata_test_pattern_has_token(form.pattern, S8("f3_refining_prefix")) ||
                                       x86_64_metadata_test_pattern_has_token(form.pattern, S8("REFINING66()")));
        }
        bool mpxmode_aliases_blocked = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(mpxmode_alias_ids); index += 1)
        {
            u32 form_id = mpxmode_alias_ids[index];
            BusterX86MetadataForm form = {0};
            bool retrieved = buster_x86_metadata_form(form_id, &form);
            bool base_nop = retrieved && x86_64_metadata_test_string_equal(form.extension, S8("BASE")) &&
                            x86_64_metadata_test_string_equal(form.isa_set, S8("PPRO")) &&
                            x86_64_metadata_test_string_equal(form.iclass, S8("NOP"));
            bool mode_zero = retrieved && x86_64_metadata_test_pattern_has_token(form.pattern, S8("MPXMODE=0"));
            mpxmode_aliases_blocked &= retrieved && base_nop && mode_zero &&
                                      form.coverage_class == BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS &&
                                      ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                      ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_DECODE_ALIAS &&
                                      !ledger[form_id].encoder_capable;
        }
        bool mpxmode_adjacent_excluded = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(mpxmode_adjacent_ids); index += 1)
        {
            u32 form_id = mpxmode_adjacent_ids[index];
            BusterX86MetadataForm form = {0};
            mpxmode_adjacent_excluded &= buster_x86_metadata_form(form_id, &form) &&
                                         form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64 &&
                                         ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                         ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_NOT64;
        }
        BusterX86MetadataForm after_mpxmode = {0};
        bool no_adjacent_token = buster_x86_metadata_form(8818, &after_mpxmode) &&
                                 !x86_64_metadata_test_pattern_has_token(after_mpxmode.pattern, S8("MPXMODE=1")) &&
                                 !x86_64_metadata_test_pattern_has_token(after_mpxmode.pattern, S8("MPXMODE=0"));
        BUSTER_TEST(arguments, mpxmode_rows_exact && mpxmode_one_count == 24 && mpxmode_zero_count == 0 &&
                                   mpxmode_memory_count == 10 && mpxmode_refining_count == 16 && mpxmode_aliases_blocked &&
                                   mpxmode_adjacent_excluded &&
                                   no_adjacent_token);
        // The raw `not16` cohort is intentionally small: only the eight
        // fixed-opcode BASE NOP rows have a schema shape the encoder can
        // represent.  Keep the inventory and both adjacent rows explicit so
        // a future parser change cannot silently widen the execution-mode
        // exception to unrelated metadata.
        u32 not16_count = 0;
        bool not16_inventory = true;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form) || !x86_64_metadata_test_pattern_has_token(form.pattern, S8("not16")))
                continue;
            not16_count += 1;
            BusterX86MetadataCoverageLedgerEntry entry = ledger[form_id];
            bool fixed_nop_shape = x86_64_metadata_test_string_equal(form.extension, S8("BASE")) &&
                                   (x86_64_metadata_test_string_equal(form.isa_set, S8("I86")) ||
                                    x86_64_metadata_test_string_equal(form.isa_set, S8("FAT_NOP"))) &&
                                   x86_64_metadata_test_string_equal(form.category, S8("WIDENOP")) &&
                                   x86_64_metadata_test_string_equal(form.attributes, S8("NOP")) && form.operand_count == 0 &&
                                   form.fixed_byte_count != 0 &&
                                   (form.map == BUSTER_X86_METADATA_MAP_LEGACY || form.map == BUSTER_X86_METADATA_MAP_0F) &&
                                   (form.mandatory_prefix == 0 || form.mandatory_prefix == 0x66);
            not16_inventory &= form_id >= 10997 && form_id <= 11004 && fixed_nop_shape &&
                               form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                               entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                               entry.blocker == BUSTER_X86_METADATA_BLOCKER_NONE && entry.encoder_capable &&
                               !entry.policy_excluded;
        }
        BusterX86MetadataForm before_not16 = {0};
        BusterX86MetadataForm after_not16 = {0};
        bool adjacency = buster_x86_metadata_form(10996, &before_not16) && buster_x86_metadata_form(11005, &after_not16) &&
                         before_not16.id == 10996 && after_not16.id == 11005 &&
                         before_not16.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED &&
                         ledger[10996].disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                         ledger[10996].blocker == BUSTER_X86_METADATA_BLOCKER_PRIVILEGED && ledger[10996].policy_excluded &&
                         ledger[10996].encoder_capable &&
                         ledger[11005].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                         ledger[11005].blocker == BUSTER_X86_METADATA_BLOCKER_NONE && ledger[11005].encoder_capable;
        BUSTER_TEST(arguments, not16_count == 8 && not16_inventory && adjacency);
        // CET=0 and ENCDELETE aliases remain in the complete ledger for
        // audit/regeneration, but their decode-policy markers make them
        // unavailable to the public encoder.  Reuse this audit's ledger;
        // running another full 11k-form audit in the selector tests below
        // would duplicate a comparatively expensive scan.
        static u32 const cet0_form_ids[] = {7965, 7966, 7967, 7968};
        bool cet0_ledger_exact = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cet0_form_ids); index += 1)
        {
            u32 form_id = cet0_form_ids[index];
            cet0_ledger_exact &= form_id < audit.entry_count && ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                 !ledger[form_id].encoder_capable &&
                                 ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_DECODE_ALIAS;
        }
        static u32 const encdelete_form_ids[] = {7970, 7971, 7972, 7973, 7974, 7975};
        bool encdelete_ledger_exact = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(encdelete_form_ids); index += 1)
        {
            u32 form_id = encdelete_form_ids[index];
            encdelete_ledger_exact &= form_id < audit.entry_count && ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED &&
                                      !ledger[form_id].encoder_capable &&
                                      ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_DECODE_ALIAS;
        }
        BUSTER_TEST(arguments, audit.complete && !audit.duplicate_form_id && !audit.duplicate_stable_hash &&
                                   audit.entry_count == 11013 && audit.normalized_entry_count == 10607 && cet0_ledger_exact &&
                                   encdelete_ledger_exact);
        // These are the five additional normalized LEGACY rows whose plain
        // not_refining controls are architecturally selectable here.  The
        // six not_refining_f3 BSR/BSF rows are already covered by the
        // residual-control base commit and are deliberately not duplicated.
        static u32 const not_refining_ids[] = {8839, 8840, 9002, 10976, 10977};
        u32 parsed_not_refining = 0;
        bool refining_controls_exact = true;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            bool has_not_refining = x86_64_metadata_test_pattern_has_token(form.pattern, S8("not_refining"));
            if (!has_not_refining) continue;
            bool listed_plain = false;
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(not_refining_ids); index += 1)
                listed_plain |= form_id == not_refining_ids[index];
            bool normalized_legacy = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                     form.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY;
            bool expected_emitted = listed_plain;
            bool row_exact = normalized_legacy == expected_emitted && ledger[form_id].encoder_capable == expected_emitted &&
                              ledger[form_id].disposition == (expected_emitted
                                                                  ? BUSTER_X86_METADATA_COVERAGE_EMITTED
                                                                  : BUSTER_X86_METADATA_COVERAGE_BLOCKED);
            refining_controls_exact &= row_exact;
            parsed_not_refining += has_not_refining;
        }
        BUSTER_TEST(arguments, parsed_not_refining == 5);
        BUSTER_TEST(arguments, refining_controls_exact);

        // Byte oracles: llvm-mc -triple=x86_64 -show-encoding for the
        // canonical operands synthesized by x86_64_metadata_test_build_gate_query.
        struct
        {
            u32 form_id;
            u8 bytes[8];
            u8 byte_count;
        } const refining_byte_cases[] = {
            {8839, {0x48, 0x0f, 0xc7, 0xf0}, 4},
            {8840, {0x48, 0x0f, 0xc7, 0xf8}, 4},
            {9002, {0x0f, 0xa7, 0xc0}, 3},
            {10976, {0x48, 0x0f, 0x38, 0xf0, 0x00}, 5},
            {10977, {0x48, 0x0f, 0x38, 0xf1, 0x00}, 5},
        };
        bool refining_bytes_exact = true;
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(refining_byte_cases); case_index += 1)
        {
            u32 form_id = refining_byte_cases[case_index].form_id;
            BusterX86MetadataPhysicalOperand operands[16] = {0};
            char8 mnemonic_buffer[128] = {0};
            BusterX86MetadataPhysicalQuery query = {0};
            bool built = x86_64_metadata_test_build_gate_query(form_id, &query, operands, mnemonic_buffer);
            query.features.names = wildcard;
            query.features.count = BUSTER_ARRAY_LENGTH(wildcard);
            u8 output[32] = {0};
            BusterX86MetadataRelocation relocations[2] = {0};
            BusterX86MetadataEmitResult emitted = built
                ? buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                      .physical = query,
                      .form_id = form_id,
                      .output = output,
                      .output_capacity = BUSTER_ARRAY_LENGTH(output),
                      .relocations = relocations,
                      .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                  })
                : (BusterX86MetadataEmitResult){0};
            refining_bytes_exact &= built && emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                    emitted.relocation_count == 0 && emitted.byte_count == refining_byte_cases[case_index].byte_count &&
                                    x86_64_metadata_test_bytes_equal(output, emitted.byte_count,
                                                                      refining_byte_cases[case_index].bytes,
                                                                      refining_byte_cases[case_index].byte_count);
        }
        BUSTER_TEST(arguments, refining_bytes_exact);

        // The fixed CET/IBHF NOP rows are a metadata-only two-register
        // coverage cohort.  Keep the complete twelve-row inventory explicit:
        // eleven rows have safe fixed bytes, while the generic F8 row remains
        // blocked because its REX.W spelling is IBHF=1.
        static u32 const fixed_nop_ids[] = {7954, 7955, 7956, 7957, 7958, 7959, 7960, 7961, 7962, 7963, 7964, 8693};
        static String8 const fixed_nop_reg_tokens[] = {
            S8_INITIALIZER("REG[0b010]"), S8_INITIALIZER("REG[0b011]"), S8_INITIALIZER("REG[0b100]"),
            S8_INITIALIZER("REG[0b101]"), S8_INITIALIZER("REG[0b111]"), S8_INITIALIZER("REG[0b111]"),
            S8_INITIALIZER("REG[0b111]"), S8_INITIALIZER("REG[0b111]"), S8_INITIALIZER("REG[0b110]"),
            S8_INITIALIZER("REG[0b111]"), S8_INITIALIZER("REG[0b111]"), S8_INITIALIZER("REG[0b111]"),
        };
        static String8 const fixed_nop_rm_tokens[] = {
            S8_INITIALIZER("RM[nnn]"), S8_INITIALIZER("RM[nnn]"), S8_INITIALIZER("RM[nnn]"),
            S8_INITIALIZER("RM[nnn]"), S8_INITIALIZER("RM[0b001]"), S8_INITIALIZER("RM[0b101]"),
            S8_INITIALIZER("RM[0b110]"), S8_INITIALIZER("RM[0b111]"), S8_INITIALIZER("RM[nnn]"),
            S8_INITIALIZER("RM[0b000]"), S8_INITIALIZER("RM[0b100]"), S8_INITIALIZER("RM[0b000]"),
        };
        bool fixed_nop_inventory = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fixed_nop_ids); index += 1)
        {
            BusterX86MetadataForm form = {0};
            bool retrieved = buster_x86_metadata_form(fixed_nop_ids[index], &form);
            bool raw_signature = retrieved && form.fixed_byte_count == 2 && form.fixed_bytes[0] == 0x0f &&
                                 form.fixed_bytes[1] == 0x1e && form.mandatory_prefix == 0xf3 &&
                                 form.map == BUSTER_X86_METADATA_MAP_0F && form.operand_count == 2 &&
                                 (form.field_flags & (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_REGISTER)) ==
                                     (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_REGISTER);
            bool collision = index == 9;
            bool expected_coverage = !collision;
            fixed_nop_inventory &= retrieved && x86_64_metadata_test_string_equal(form.iclass, S8("NOP")) &&
                                   x86_64_metadata_test_string_equal(form.isa_set, S8("PPRO")) &&
                                   x86_64_metadata_test_string_equal(form.category, S8("WIDENOP")) &&
                                   x86_64_metadata_test_string_equal(form.extension, S8("BASE")) && raw_signature &&
                                   x86_64_metadata_test_pattern_has_token(form.pattern, S8("0x0F")) &&
                                   x86_64_metadata_test_pattern_has_token(form.pattern, S8("0x1E")) &&
                                   x86_64_metadata_test_pattern_has_token(form.pattern, S8("MOD[0b11]")) &&
                                   x86_64_metadata_test_pattern_has_token(form.pattern, S8("MOD=3")) &&
                                   x86_64_metadata_test_pattern_has_token(form.pattern, fixed_nop_reg_tokens[index]) &&
                                   x86_64_metadata_test_pattern_has_token(form.pattern, fixed_nop_rm_tokens[index]) &&
                                   (index == 11 ? x86_64_metadata_test_pattern_has_token(form.pattern, S8("norexw_prefix"))
                                                : !x86_64_metadata_test_pattern_has_token(form.pattern, S8("norexw_prefix"))) &&
                                   ledger[fixed_nop_ids[index]].encoder_capable == expected_coverage &&
                                   ledger[fixed_nop_ids[index]].disposition ==
                                       (expected_coverage ? BUSTER_X86_METADATA_COVERAGE_EMITTED
                                                           : BUSTER_X86_METADATA_COVERAGE_BLOCKED) &&
                                   ledger[fixed_nop_ids[index]].blocker ==
                                       (expected_coverage ? BUSTER_X86_METADATA_BLOCKER_NONE
                                                           : BUSTER_X86_METADATA_BLOCKER_DECODE_ALIAS);
        }
        BUSTER_TEST(arguments, fixed_nop_inventory);

        struct
        {
            u32 form_id;
            u8 rm;
            u8 reg;
            u8 bytes32[5];
            u8 bytes32_count;
            u8 bytes64[5];
            u8 bytes64_count;
        } const fixed_nop_byte_cases[] = {
            {7954, 0, 2, {0xf3, 0x0f, 0x1e, 0xd0, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xd0}, 5},
            {7955, 0, 3, {0xf3, 0x0f, 0x1e, 0xd8, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xd8}, 5},
            {7956, 0, 4, {0xf3, 0x0f, 0x1e, 0xe0, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xe0}, 5},
            {7957, 0, 5, {0xf3, 0x0f, 0x1e, 0xe8, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xe8}, 5},
            {7958, 1, 7, {0xf3, 0x0f, 0x1e, 0xf9, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xf9}, 5},
            {7959, 5, 7, {0xf3, 0x0f, 0x1e, 0xfd, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xfd}, 5},
            {7960, 6, 7, {0xf3, 0x0f, 0x1e, 0xfe, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xfe}, 5},
            {7961, 7, 7, {0xf3, 0x0f, 0x1e, 0xff, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xff}, 5},
            {7962, 0, 6, {0xf3, 0x0f, 0x1e, 0xf0, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xf0}, 5},
            {7964, 4, 7, {0xf3, 0x0f, 0x1e, 0xfc, 0}, 4, {0xf3, 0x48, 0x0f, 0x1e, 0xfc}, 5},
            {8693, 0, 7, {0xf3, 0x0f, 0x1e, 0xf8, 0}, 4, {0, 0, 0, 0, 0}, 0},
        };
        bool fixed_nop_bytes_exact = true;
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(fixed_nop_byte_cases); case_index += 1)
        {
            u32 form_id = fixed_nop_byte_cases[case_index].form_id;
            BusterX86MetadataPhysicalOperand operands32[2] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, fixed_nop_byte_cases[case_index].rm, 32),
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, fixed_nop_byte_cases[case_index].reg, 32),
            };
            bool case_exact = x86_64_metadata_test_emit_exact(
                S8("NOP"), form_id, operands32, BUSTER_ARRAY_LENGTH(operands32), (BusterX86MetadataPhysicalAttributes){0},
                wildcard, BUSTER_ARRAY_LENGTH(wildcard), fixed_nop_byte_cases[case_index].bytes32,
                fixed_nop_byte_cases[case_index].bytes32_count);
            if (fixed_nop_byte_cases[case_index].bytes64_count)
            {
                BusterX86MetadataPhysicalOperand operands64[2] = {
                    x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, fixed_nop_byte_cases[case_index].rm, 64),
                    x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, fixed_nop_byte_cases[case_index].reg, 64),
                };
                case_exact &= x86_64_metadata_test_emit_exact(
                    S8("NOP"), form_id, operands64, BUSTER_ARRAY_LENGTH(operands64), (BusterX86MetadataPhysicalAttributes){0},
                    wildcard, BUSTER_ARRAY_LENGTH(wildcard), fixed_nop_byte_cases[case_index].bytes64,
                    fixed_nop_byte_cases[case_index].bytes64_count);
            }
            fixed_nop_bytes_exact &= case_exact;
        }
        BUSTER_TEST(arguments, fixed_nop_bytes_exact);

        BusterX86MetadataPhysicalOperand generic_collision_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 7, 64),
        };
        BusterX86MetadataEmitResult generic_collision = x86_64_metadata_test_emit_form(
            S8("NOP"), 7963, generic_collision_operands, BUSTER_ARRAY_LENGTH(generic_collision_operands),
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 1, 0, 0);
        BUSTER_TEST(arguments, generic_collision.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA);

        // ID8693 is the explicit no-REX spelling: it is valid regardless of
        // the IBHF feature state and must never acquire the 0x48 prefix.
        String8 fixed_nop_no_boolean_features[] = {S8("i386")};
        BusterX86MetadataPhysicalOperand no_rex_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 7, 32),
        };
        u8 no_rex_bytes[] = {0xf3, 0x0f, 0x1e, 0xf8};
        bool no_rex_exact = x86_64_metadata_test_emit_exact(
                                S8("NOP"), 8693, no_rex_operands, BUSTER_ARRAY_LENGTH(no_rex_operands),
                                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), no_rex_bytes,
                                BUSTER_ARRAY_LENGTH(no_rex_bytes)) &&
                            x86_64_metadata_test_emit_exact(
                                S8("NOP"), 8693, no_rex_operands, BUSTER_ARRAY_LENGTH(no_rex_operands),
                                (BusterX86MetadataPhysicalAttributes){0}, fixed_nop_no_boolean_features,
                                BUSTER_ARRAY_LENGTH(fixed_nop_no_boolean_features), no_rex_bytes, BUSTER_ARRAY_LENGTH(no_rex_bytes));
        BUSTER_TEST(arguments, no_rex_exact);

        BusterX86MetadataPhysicalOperand wrong_operands[16] = {0};
        char8 wrong_mnemonic_buffer[128] = {0};
        BusterX86MetadataPhysicalQuery wrong_query = {0};
        bool wrong_built = x86_64_metadata_test_build_gate_query(8839, &wrong_query, wrong_operands, wrong_mnemonic_buffer);
        wrong_query.features.names = wildcard;
        wrong_query.features.count = BUSTER_ARRAY_LENGTH(wildcard);
        wrong_query.attributes.rep = true;
        u8 wrong_output[32] = {0};
        BusterX86MetadataEmitResult wrong_result = wrong_built
            ? buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                  .physical = wrong_query,
                  .form_id = 8839,
                  .output = wrong_output,
                  .output_capacity = BUSTER_ARRAY_LENGTH(wrong_output),
              })
            : (BusterX86MetadataEmitResult){0};
        BUSTER_TEST(arguments, wrong_built && wrong_result.status != BUSTER_X86_METADATA_ENCODE_SUCCESS && wrong_result.byte_count == 0);

        // Prefix-control coverage is intentionally tracked separately from
        // the broad audit totals.  Decoder aliases are policy-excluded from
        // the normalized cohort; every remaining normalized row must still
        // emit, so a newly recognized token cannot widen the encoder cohort
        // by accident.
        String8 const residual_tokens[] = {
            S8_INITIALIZER("DF64()"), S8_INITIALIZER("IMMUNE66()"),
            S8_INITIALIZER("IMMUNE66_LOOP64()"), S8_INITIALIZER("IMMUNE_REXW()"),
        };
        static u32 const residual_total_expected[] = {42, 34, 15, 4};
        static u32 const residual_normalized_expected[] = {38, 26, 13, 4};
        static u32 const residual_blocked_expected[] = {0, 0, 0, 0};
        static u32 const residual_emitted_expected[] = {38, 26, 13, 4};
        u32 residual_total[4] = {0};
        u32 residual_normalized[4] = {0};
        u32 residual_blocked[4] = {0};
        u32 residual_emitted[4] = {0};
        bool residual_rows_consistent = true;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(residual_tokens); token_index += 1)
            {
                if (!x86_64_metadata_test_pattern_has_token(form.pattern, residual_tokens[token_index])) continue;
                residual_total[token_index] += 1;
                if (form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED) continue;
                residual_normalized[token_index] += 1;
                bool is_blocked = ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED;
                residual_blocked[token_index] += is_blocked;
                residual_emitted[token_index] += !is_blocked;
                residual_rows_consistent &= !is_blocked && ledger[form_id].encoder_capable &&
                                           ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_NONE;
            }
        }
        bool residual_counts_match = residual_rows_consistent;
        for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(residual_tokens); token_index += 1)
        {
            residual_counts_match &= residual_total[token_index] == residual_total_expected[token_index];
            residual_counts_match &= residual_normalized[token_index] == residual_normalized_expected[token_index];
            residual_counts_match &= residual_blocked[token_index] == residual_blocked_expected[token_index];
            residual_counts_match &= residual_emitted[token_index] == residual_emitted_expected[token_index];
        }
        BUSTER_TEST(arguments, residual_counts_match);
        static u32 const immune66_ids[] = {
            7909, 7910, 7911, 7912, 7913, 7914, 7915, 7916, 9526, 9528, 9529, 9530,
            10952, 10953, 10954, 10955, 10958, 10959, 10960, 10961, 10964, 10965, 10966, 10967, 10968, 10969,
        };
        bool immune66_rows_stable = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(immune66_ids); index += 1)
        {
            u32 form_id = immune66_ids[index];
            immune66_rows_stable &= form_id < audit.entry_count &&
                                   ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                   ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_NONE && ledger[form_id].encoder_capable;
        }
        BUSTER_TEST(arguments, immune66_rows_stable);

        // The raw APXEVEX source carries ND=1 on 778 rows.  XED annotates
        // APX_NDD on 730 of them, while the 48 IMULZU/SET*ZU rows omit the
        // attribute and must be inferred from the pattern itself.  Every
        // ND=1 row must nevertheless expose the same NDD semantic flag, and
        // ND=0 APXEVEX rows must not acquire it through this inference.
        u32 apx_nd1_count = 0;
        u32 apx_nd1_declared_count = 0;
        u32 apx_nd1_inferred_count = 0;
        u32 apx_nd1_inferred_emitted_count = 0;
        u32 apx_nd1_inferred_imul_count = 0;
        u32 apx_nd1_inferred_setcc_count = 0;
        u32 apx_nd1_missing_flag_count = 0;
        u32 apx_nd0_unexpected_ndd_count = 0;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form) || !x86_64_metadata_test_string_equal(form.extension, S8("APXEVEX"))) continue;
            bool has_nd1 = x86_64_metadata_test_pattern_has_token(form.pattern, S8("ND=1"));
            bool has_nd0 = x86_64_metadata_test_pattern_has_token(form.pattern, S8("ND=0"));
            bool has_ndd_attribute = x86_64_metadata_test_string_contains(form.attributes, S8("APX_NDD"));
            bool has_ndd_flag = (form.apx_flags & BUSTER_X86_METADATA_APX_NDD) != 0;
            if (has_nd1)
            {
                apx_nd1_count += 1;
                apx_nd1_declared_count += has_ndd_attribute;
                apx_nd1_inferred_count += !has_ndd_attribute;
                if (!has_ndd_attribute)
                {
                    apx_nd1_inferred_emitted_count += ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
                    apx_nd1_inferred_imul_count += x86_64_metadata_test_string_equal(form.iclass, S8("IMUL"));
                    apx_nd1_inferred_setcc_count += x86_64_metadata_test_string_contains(form.iclass, S8("SET"));
                }
                apx_nd1_missing_flag_count += !has_ndd_flag;
            }
            if (has_nd0 && !has_ndd_attribute) apx_nd0_unexpected_ndd_count += has_ndd_flag;
        }
        BUSTER_TEST(arguments, apx_nd1_count == 778 && apx_nd1_declared_count == 730 && apx_nd1_inferred_count == 48 &&
                                   apx_nd1_inferred_emitted_count == 48 && apx_nd1_inferred_imul_count == 16 &&
                                   apx_nd1_inferred_setcc_count == 32 && apx_nd1_missing_flag_count == 0 &&
                                   apx_nd0_unexpected_ndd_count == 0);

        static struct
        {
            u32 form_id;
            u64 stable_hash;
        } const privileged_valid64_inventory[] = {
            {423, UINT64_C(0x000fdb86f1aadc81)},
            {424, UINT64_C(0x3d1ebefba8414734)},
            {425, UINT64_C(0x21718e05ae7ae52c)},
            {433, UINT64_C(0x72251dc6dc93950e)},
            {434, UINT64_C(0x2694fd3f416fc783)},
            {435, UINT64_C(0x83699f775c194bff)},
            {436, UINT64_C(0x28323239c95dbc87)},
            {454, UINT64_C(0x62b4a76cbfdcfa25)},
            {1750, UINT64_C(0xa97e8fa2c0259e4e)},
            {1847, UINT64_C(0x08ae34d52f01f12e)},
            {1848, UINT64_C(0x6ea84c686eb80660)},
            {1849, UINT64_C(0x2b74c486973d0549)},
            {2841, UINT64_C(0x6f4e95860ffd415c)},
            {2842, UINT64_C(0xa066f52336a3c8d4)},
            {2932, UINT64_C(0xe8cdbc59679f6759)},
            {2933, UINT64_C(0xba4853fea6c84638)},
            {7935, UINT64_C(0x40267c8c36d26c20)},
            {7944, UINT64_C(0x2fe685c7fcc2d261)},
            {7947, UINT64_C(0x9ae8ec3ad39cee8e)},
            {7948, UINT64_C(0x7ccabdbc15624f93)},
            {8014, UINT64_C(0x313df55a468d4c4e)},
            {8015, UINT64_C(0x0532aac23cd1abca)},
            {8016, UINT64_C(0x50a00ae85f53e21e)},
            {8017, UINT64_C(0x2b1e460fb338458e)},
            {8018, UINT64_C(0x2d03bc4e4a951fef)},
            {8099, UINT64_C(0xefd1dddbe29380f1)},
            {8100, UINT64_C(0xda6c941b1c643db9)},
            {8714, UINT64_C(0x9c48d051528adbbc)},
            {8818, UINT64_C(0xd727629e19927b1e)},
            {8819, UINT64_C(0xdaaccfb478023c56)},
            {8820, UINT64_C(0x2fee49937e0f70b1)},
            {8821, UINT64_C(0x8fb0189c52ba878f)},
            {8822, UINT64_C(0x33b06f287ee49f08)},
            {8824, UINT64_C(0x1aa8e61e5736a61e)},
            {8844, UINT64_C(0x6c8e58373bbce760)},
            {8888, UINT64_C(0xd0eff84140c1c18c)},
            {8889, UINT64_C(0xb79664c7df22e466)},
            {8890, UINT64_C(0xc7954cb1933ef918)},
            {8891, UINT64_C(0x740bd4f9301baeda)},
            {8892, UINT64_C(0x2fd845633b0bc38c)},
            {8893, UINT64_C(0xa47c62856f645e26)},
            {9064, UINT64_C(0x41dd5fc08f8ec534)},
            {9065, UINT64_C(0x72885d7dee189598)},
            {9066, UINT64_C(0xe36fed6c3bd872c9)},
            {9067, UINT64_C(0x2474767f901ed0fd)},
            {9496, UINT64_C(0x477a2d6c1d16e197)},
            {9497, UINT64_C(0x68d2140ab91e7c01)},
            {9498, UINT64_C(0x2052d7d6d17aaa2e)},
            {9499, UINT64_C(0x0773dda93b7427d0)},
            {9504, UINT64_C(0xc92bd45af3b599c1)},
            {9508, UINT64_C(0x97f38c8bd9dd0f7e)},
            {9509, UINT64_C(0xb5f3fc6194aba4ab)},
            {9521, UINT64_C(0x3c26e2370b8dfa23)},
            {9522, UINT64_C(0x985ae895a04329f5)},
            {9523, UINT64_C(0xb7776545562fdd93)},
            {9524, UINT64_C(0xf629709fd45ed2d1)},
            {9589, UINT64_C(0xae9065eb96d145da)},
            {9590, UINT64_C(0x8c09dad671924c24)},
            {9591, UINT64_C(0x0e896428f35aa669)},
            {9592, UINT64_C(0xdac2c95de0b3ab3f)},
            {9595, UINT64_C(0xac905f115efd43c3)},
            {9599, UINT64_C(0x3a6c3dca96c7359e)},
            {9600, UINT64_C(0x0af96989a90ec18c)},
            {9601, UINT64_C(0x5c8d272399e52dcb)},
            {9604, UINT64_C(0xcca8306a60fe6a09)},
            {9605, UINT64_C(0x04811f7a925fadbf)},
            {10069, UINT64_C(0x55e165834b72d02b)},
            {10082, UINT64_C(0x828ccf6ffb8c59b8)},
            {10083, UINT64_C(0x94dce232d97a5c8f)},
            {10084, UINT64_C(0xbea474e4102aa7f3)},
            {10122, UINT64_C(0x54cfc102161a412b)},
            {10124, UINT64_C(0xede5a7a43e0d5222)},
            {10126, UINT64_C(0x41f5fcac9b7a24e9)},
            {10128, UINT64_C(0xc5cc6c21a9fb6a08)},
            {10129, UINT64_C(0x78c5f4b02c063ce2)},
            {10131, UINT64_C(0x22e0ab5139918c10)},
            {10136, UINT64_C(0x9a91329cfd49cc15)},
            {10410, UINT64_C(0x87014095840a2c9f)},
            {10411, UINT64_C(0x47ccb4893dde9b19)},
            {10606, UINT64_C(0x2a17b4cffa7e84b7)},
            {10607, UINT64_C(0xa4b74c4baed2355b)},
            {10610, UINT64_C(0xff9b508fe1bccb9e)},
            {10611, UINT64_C(0x3acc002c3e56cd5d)},
            {10971, UINT64_C(0x74c3c5197ee56eef)},
            {10993, UINT64_C(0x98bc9422134ea928)},
            {10995, UINT64_C(0x37ad3945477649b6)},
            {11009, UINT64_C(0x9211e7f7e752f5d6)},
            {11010, UINT64_C(0xfa1cb7c603e5dca2)},
            {11011, UINT64_C(0x392090cc75f865d4)},
            {11012, UINT64_C(0xdd19095836024398)},
        };
        bool privileged_inventory_traceable = BUSTER_ARRAY_LENGTH(privileged_valid64_inventory) == 90;
        for (u32 inventory_index = 0; inventory_index < BUSTER_ARRAY_LENGTH(privileged_valid64_inventory); inventory_index += 1)
        {
            u32 form_id = privileged_valid64_inventory[inventory_index].form_id;
            BusterX86MetadataForm form = {0};
            bool retrieved = form_id < audit.entry_count && buster_x86_metadata_form(form_id, &form);
            bool valid64 = retrieved && buster_x86_metadata_test_execution_mode_matches(
                                           form.mode_flags, form.coverage_class, false, BUSTER_X86_METADATA_EXECUTION_MODE_64);
            BusterX86MetadataCoverageLedgerEntry entry = form_id < audit.entry_count ? ledger[form_id] : (BusterX86MetadataCoverageLedgerEntry){0};
            privileged_inventory_traceable &= retrieved && form.id == form_id &&
                                              form.stable_hash == privileged_valid64_inventory[inventory_index].stable_hash &&
                                              x86_64_metadata_test_string_equal(form.cpl, S8("0")) && valid64 &&
                                              form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED && entry.encoder_capable &&
                                              entry.policy_excluded && entry.blocker == BUSTER_X86_METADATA_BLOCKER_PRIVILEGED;
        }
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form) || !x86_64_metadata_test_string_equal(form.cpl, S8("0")) ||
                !buster_x86_metadata_test_execution_mode_matches(form.mode_flags, form.coverage_class, false,
                                                                  BUSTER_X86_METADATA_EXECUTION_MODE_64))
            {
                continue;
            }
            bool listed = false;
            for (u32 inventory_index = 0; inventory_index < BUSTER_ARRAY_LENGTH(privileged_valid64_inventory); inventory_index += 1)
                listed |= privileged_valid64_inventory[inventory_index].form_id == form_id;
            privileged_inventory_traceable &= listed;
        }
        BUSTER_TEST(arguments, privileged_inventory_traceable);

        u32 privileged_capable = 0;
        u32 privileged_blocked = 0;
        u32 not64_capable = 0;
        u32 privileged_total = 0;
        u32 privileged_valid64 = 0;
        u32 privileged_valid64_capable = 0;
        u32 privileged_valid64_blocked = 0;
        u32 privileged_not64 = 0;
        u32 apx_total = 0;
        u32 apx_emitted = 0;
        u32 apx_blocked = 0;
        u32 apx_scc_total = 0;
        u32 apx_scc_emitted = 0;
        u32 evex_r4_total = 0;
        u32 evex_r4_emitted = 0;
        u32 dfv_total = 0;
        u32 dfv_scc_total = 0;
        u32 emitted_bnd = 0;
        u32 emitted_control = 0;
        u32 emitted_debug = 0;
        u32 emitted_segment = 0;
        bool ledger_rows_consistent = true;
        bool dfv_scc_semantics_consistent = true;
        String8 const scc_tokens[] = {
            S8_INITIALIZER(" SCC0 "),  S8_INITIALIZER(" SCC1 "),  S8_INITIALIZER(" SCC2 "),  S8_INITIALIZER(" SCC3 "),
            S8_INITIALIZER(" SCC4 "),  S8_INITIALIZER(" SCC5 "),  S8_INITIALIZER(" SCC6 "),  S8_INITIALIZER(" SCC7 "),
            S8_INITIALIZER(" SCC8 "),  S8_INITIALIZER(" SCC9 "),  S8_INITIALIZER(" SCC10 "), S8_INITIALIZER(" SCC11 "),
            S8_INITIALIZER(" SCC12 "), S8_INITIALIZER(" SCC13 "), S8_INITIALIZER(" SCC14 "), S8_INITIALIZER(" SCC15 "),
        };
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataCoverageLedgerEntry entry = ledger[form_id];
            ledger_rows_consistent &= entry.form_id == form_id && entry.encoder_family < BUSTER_X86_METADATA_ENCODER_COUNT &&
                                     entry.disposition < BUSTER_X86_METADATA_COVERAGE_DISPOSITION_COUNT &&
                                     entry.blocker < BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT;
            ledger_rows_consistent &= entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED
                                         ? entry.blocker == BUSTER_X86_METADATA_BLOCKER_NONE && entry.encoder_capable &&
                                               entry.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED
                                         : entry.blocker != BUSTER_X86_METADATA_BLOCKER_NONE;
            if (entry.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED)
                ledger_rows_consistent &= entry.encoder_capable == (entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED);
            BusterX86MetadataForm form = {0};
            if (buster_x86_metadata_form(form_id, &form))
            {
                bool privileged_policy = x86_64_metadata_test_string_equal(form.cpl, S8("0"));
                bool valid64 = buster_x86_metadata_test_execution_mode_matches(
                    form.mode_flags, form.coverage_class, false, BUSTER_X86_METADATA_EXECUTION_MODE_64);
                privileged_capable += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED && valid64 && entry.encoder_capable;
                privileged_blocked += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_PRIVILEGED && valid64 && !entry.encoder_capable;
                not64_capable += !valid64 && entry.encoder_capable;
                if (privileged_policy)
                {
                    privileged_total += 1;
                    if (valid64)
                    {
                        privileged_valid64 += 1;
                        privileged_valid64_capable += entry.encoder_capable;
                        privileged_valid64_blocked += !entry.encoder_capable;
                    }
                    else
                    {
                        privileged_not64 += 1;
                    }
                }
                if (form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED && (form.apx_flags & BUSTER_X86_METADATA_APX))
                {
                    apx_total += 1;
                    apx_emitted += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
                    apx_blocked += entry.disposition == BUSTER_X86_METADATA_COVERAGE_BLOCKED;
                    if (x86_64_metadata_test_string_contains(form.pattern, S8("EVAPX_SCC")))
                    {
                        apx_scc_total += 1;
                        apx_scc_emitted += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
                    }
                }
            }
            if (x86_64_metadata_test_string_contains(form.pattern, S8("EVEXR4_ONE()")))
            {
                evex_r4_total += 1;
                evex_r4_emitted += entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
            }
            bool form_requires_dfv = buster_x86_metadata_form_requires_dfv(form_id);
            bool pattern_has_evapx_scc = x86_64_metadata_test_string_contains(form.pattern, S8("EVAPX_SCC()"));
            u32 parsed_scc_count = 0;
            // Every scc token contains " SCC", so one probe clears the sixteen
            // token scans for the forms (nearly all) with no SCC atom at all.
            if (x86_64_metadata_test_string_contains(form.pattern, S8(" SCC")))
            {
                for (u32 scc_index = 0; scc_index < BUSTER_ARRAY_LENGTH(scc_tokens); scc_index += 1)
                    parsed_scc_count += x86_64_metadata_test_string_contains(form.pattern, scc_tokens[scc_index]);
            }
            bool parsed_scc = parsed_scc_count == 1;
            bool form_has_apx_scc = (form.apx_flags & BUSTER_X86_METADATA_APX_SCC) != 0;
            bool row_dfv_scc_coherent = form_requires_dfv == pattern_has_evapx_scc &&
                                        pattern_has_evapx_scc == parsed_scc && parsed_scc == form_has_apx_scc;
            dfv_total += form_requires_dfv;
            dfv_scc_total += form_requires_dfv && row_dfv_scc_coherent;
            dfv_scc_semantics_consistent &= row_dfv_scc_coherent;
            if (entry.disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED && buster_x86_metadata_form(form_id, &form))
            {
                // Count visible physical classes only on emitted normalized
                // rows.  This proves which non-GPR register classes are
                // reachable from the current 8,428-form schema rather than
                // treating schema-blocked classes as public coverage.
                bool has_bnd = false;
                bool has_control = false;
                bool has_debug = false;
                bool has_segment = false;
                for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
                {
                    BusterX86MetadataOperand emitted_operand = {0};
                    if (!buster_x86_metadata_operand(form_id, operand_index, &emitted_operand) || !emitted_operand.visible)
                    {
                        continue;
                    }
                    has_bnd |= emitted_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_BND;
                    has_control |= emitted_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL;
                    has_debug |= emitted_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG;
                    has_segment |= emitted_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT;
                }
                emitted_bnd += has_bnd;
                emitted_control += has_control;
                emitted_debug += has_debug;
                emitted_segment += has_segment;
            }
        }
        BUSTER_TEST(arguments, ledger_rows_consistent);
        BUSTER_TEST(arguments, emitted_bnd == 21 && emitted_control == 0 && emitted_debug == 0 && emitted_segment == 4);
        BUSTER_TEST(arguments, privileged_capable == 90);
        BUSTER_TEST(arguments, privileged_blocked == 0);
        BUSTER_TEST(arguments, not64_capable == 154);
        BUSTER_TEST(arguments, privileged_total == 109 && privileged_valid64 == 90 && privileged_valid64_capable == 90 &&
                                   privileged_valid64_blocked == 0 && privileged_not64 == 19);
        BUSTER_TEST(arguments, apx_total == 2465);
        BUSTER_TEST(arguments, apx_emitted == 2465 && apx_blocked == 0);
        BUSTER_TEST(arguments, apx_scc_total == 640 && apx_scc_emitted == 640);
        BUSTER_TEST(arguments, evex_r4_total == 97 && evex_r4_emitted == 97);
        BUSTER_TEST(arguments, dfv_total == 640 && dfv_scc_total == 640 && dfv_scc_semantics_consistent);
        BUSTER_TEST(arguments, ledger[10069].blocker == BUSTER_X86_METADATA_BLOCKER_PRIVILEGED && ledger[10069].policy_excluded &&
                                   ledger[10069].encoder_capable);
        BusterX86MetadataForm xop_form = {0};
        BusterX86MetadataOperand xop_operand = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form(326, &xop_form) && xop_form.prefix_kind == BUSTER_X86_METADATA_PREFIX_XOP &&
                                   xop_form.encoder_family == BUSTER_X86_METADATA_ENCODER_XOP &&
                                   buster_x86_metadata_operand(326, 0, &xop_operand) &&
                                   xop_operand.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
                                   xop_operand.physical_width_flags == (BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 |
                                                                        BUSTER_X86_METADATA_PHYSICAL_WIDTH_64));
        BusterX86MetadataCoverage xop_coverage = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_coverage(326, &xop_coverage) &&
                                   xop_coverage.encoder_family == BUSTER_X86_METADATA_ENCODER_XOP);

        u32 branch_hint_count = 0;
        u32 branch_hint_emitted = 0;
        u32 branch_hint_not64 = 0;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            if (x86_64_metadata_test_string_contains(form.pattern, S8("BRANCH_HINT")))
            {
                bool exact_mode64_branch = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                                            (form.mode_flags & BUSTER_X86_METADATA_MODE_64) != 0 &&
                                            x86_64_metadata_test_string_contains(form.pattern, S8("norex2_prefix")) &&
                                            x86_64_metadata_test_string_contains(form.pattern, S8("FORCE64()")) &&
                                            x86_64_metadata_test_string_contains(form.pattern, S8("BRDISP"));
                branch_hint_count += exact_mode64_branch;
                branch_hint_emitted += exact_mode64_branch && ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED;
                branch_hint_not64 += form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64;
            }
        }
        BUSTER_TEST(arguments, branch_hint_count == 32 && branch_hint_emitted == 32 && branch_hint_not64 == 32);

        // The residual REP/string cohort is intentionally an exact raw-token
        // predicate.  These 42 adjacent mode16/mode32 pairs carry one repeat
        // selector, one operand-size selector, and at most one implicit string
        // segment override; unrelated mode16/mode32 rows remain blocked.
        static u32 const rep_prefix_pair_starts[] = {
            9752, 9755, 9758, 9761, 9765, 9769, 9776, 9779, 9782, 9785, 9789, 9793,
            9898, 9901, 9904, 9907, 9910, 9913, 9922, 9925, 9928, 9931, 9934, 9937,
            9948, 9951, 9954, 9957, 9960, 9963, 9972, 9975, 9978, 9981, 9984, 9987,
            9996, 9999, 10002, 10005, 10008, 10011,
        };
        static String8 const rep_tokens[] = {S8_INITIALIZER("repe"), S8_INITIALIZER("repne"), S8_INITIALIZER("norep")};
        static String8 const mode_tokens[] = {S8_INITIALIZER("mode16"), S8_INITIALIZER("mode32")};
        static String8 const operand_size_tokens[] = {S8_INITIALIZER("66_prefix"), S8_INITIALIZER("no66_prefix")};
        static String8 const segment_tokens[] = {S8_INITIALIZER("OVERRIDE_SEG0()"), S8_INITIALIZER("OVERRIDE_SEG1()")};
        static String8 const unrelated_tokens[] = {
            S8_INITIALIZER("mode64"), S8_INITIALIZER("not64"), S8_INITIALIZER("norexw_prefix"),
            S8_INITIALIZER("rexw_prefix"), S8_INITIALIZER("REP=0"), S8_INITIALIZER("REP=2"),
            S8_INITIALIZER("REP=3"), S8_INITIALIZER("REP!=3"), S8_INITIALIZER("BRANCH_HINT"),
            S8_INITIALIZER("lock_prefix"), S8_INITIALIZER("nolock_prefix"),
        };
        u32 rep_prefix_row_count = 0;
        u32 rep_prefix_default64_rejected = 0;
        bool rep_prefix_rows_exact = true;
        for (u32 form_id = 0; form_id < audit.entry_count; form_id += 1)
        {
            BusterX86MetadataForm form = {0};
            if (!buster_x86_metadata_form(form_id, &form)) continue;
            u32 repeat_count = 0;
            u32 mode_count = 0;
            u32 operand_size_count = 0;
            u32 segment_count = 0;
            for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(rep_tokens); token_index += 1)
                repeat_count += x86_64_metadata_test_pattern_has_token(form.pattern, rep_tokens[token_index]);
            for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(mode_tokens); token_index += 1)
                mode_count += x86_64_metadata_test_pattern_has_token(form.pattern, mode_tokens[token_index]);
            for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(operand_size_tokens); token_index += 1)
                operand_size_count += x86_64_metadata_test_pattern_has_token(form.pattern, operand_size_tokens[token_index]);
            for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(segment_tokens); token_index += 1)
                segment_count += x86_64_metadata_test_pattern_has_token(form.pattern, segment_tokens[token_index]);
            bool no_unrelated_tokens = true;
            for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(unrelated_tokens); token_index += 1)
                no_unrelated_tokens &= !x86_64_metadata_test_pattern_has_token(form.pattern, unrelated_tokens[token_index]);
            bool exact_row = form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED &&
                             form.encoder_family == BUSTER_X86_METADATA_ENCODER_LEGACY &&
                             x86_64_metadata_test_string_equal(form.extension, S8("BASE")) &&
                             repeat_count == 1 && mode_count == 1 && operand_size_count == 1 && segment_count <= 1 &&
                             no_unrelated_tokens;
            bool listed = false;
            for (u32 pair_index = 0; pair_index < BUSTER_ARRAY_LENGTH(rep_prefix_pair_starts); pair_index += 1)
                listed |= form_id == rep_prefix_pair_starts[pair_index] || form_id == rep_prefix_pair_starts[pair_index] + 1;
            if (exact_row)
            {
                rep_prefix_row_count += 1;
                rep_prefix_rows_exact &= listed;
            }
            if (listed)
            {
                u8 expected_mode = form.mode_flags & BUSTER_X86_METADATA_MODE_16
                                       ? BUSTER_X86_METADATA_EXECUTION_MODE_16
                                       : BUSTER_X86_METADATA_EXECUTION_MODE_32;
                bool default64_rejected = !buster_x86_metadata_test_execution_mode_matches(
                    form.mode_flags, form.coverage_class, false, BUSTER_X86_METADATA_EXECUTION_MODE_64);
                rep_prefix_default64_rejected += default64_rejected;
                rep_prefix_rows_exact &= exact_row &&
                                         ledger[form_id].disposition == BUSTER_X86_METADATA_COVERAGE_EMITTED &&
                                         ledger[form_id].blocker == BUSTER_X86_METADATA_BLOCKER_NONE &&
                                         ledger[form_id].encoder_capable &&
                                         default64_rejected &&
                                         buster_x86_metadata_test_execution_mode_matches(
                                             form.mode_flags, form.coverage_class, false, expected_mode);
            }
        }
        BUSTER_TEST(arguments, rep_prefix_row_count == 84 && rep_prefix_default64_rejected == 84 && rep_prefix_rows_exact);
    }

    {
        // Direct emission covers each typed repeat selector, both legacy
        // execution modes, both operand-size spellings, and both implicit
        // string-segment override classes.  The byte oracles are the GNU
        // as/LLVM mc encodings for the corresponding .code16/.code32 rows.
        typedef struct X86_64MetadataRepModeByteCase X86_64MetadataRepModeByteCase;
        struct X86_64MetadataRepModeByteCase
        {
            u32 form_id;
            String8 mnemonic;
            u8 execution_mode;
            BusterX86MetadataPhysicalAttributes attributes;
            u8 bytes[3];
            u8 byte_count;
        };
        static X86_64MetadataRepModeByteCase const cases[] = {
            {9752, S8_INITIALIZER("INSW"), BUSTER_X86_METADATA_EXECUTION_MODE_16, {.rep = true}, {0xf3, 0x6d}, 2},
            {9753, S8_INITIALIZER("INSW"), BUSTER_X86_METADATA_EXECUTION_MODE_32, {.rep = true}, {0xf3, 0x66, 0x6d}, 3},
            {9755, S8_INITIALIZER("INSW"), BUSTER_X86_METADATA_EXECUTION_MODE_16, {.repne = true}, {0xf2, 0x6d}, 2},
            {9758, S8_INITIALIZER("INSW"), BUSTER_X86_METADATA_EXECUTION_MODE_16, {0}, {0x6d}, 1},
            {9761, S8_INITIALIZER("INSD"), BUSTER_X86_METADATA_EXECUTION_MODE_16, {.rep = true}, {0xf3, 0x66, 0x6d}, 3},
            {9776, S8_INITIALIZER("OUTSW"), BUSTER_X86_METADATA_EXECUTION_MODE_16, {.rep = true}, {0xf3, 0x6f}, 2},
            {9777, S8_INITIALIZER("OUTSW"), BUSTER_X86_METADATA_EXECUTION_MODE_32, {.rep = true}, {0xf3, 0x66, 0x6f}, 3},
            {9898, S8_INITIALIZER("MOVSW"), BUSTER_X86_METADATA_EXECUTION_MODE_16, {.rep = true}, {0xf3, 0xa5}, 2},
            {9899, S8_INITIALIZER("MOVSW"), BUSTER_X86_METADATA_EXECUTION_MODE_32, {.rep = true}, {0xf3, 0x66, 0xa5}, 3},
            {9922, S8_INITIALIZER("CMPSW"), BUSTER_X86_METADATA_EXECUTION_MODE_16, {.rep = true}, {0xf3, 0xa7}, 2},
            {9954, S8_INITIALIZER("STOSW"), BUSTER_X86_METADATA_EXECUTION_MODE_16, {0}, {0xab}, 1},
        };
        String8 wildcard[1] = {S8("*")};
        bool direct_cases_pass = true;
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(cases); case_index += 1)
        {
            X86_64MetadataRepModeByteCase test_case = cases[case_index];
            BusterX86MetadataPhysicalQuery query = x86_64_metadata_test_physical_query(
                test_case.mnemonic, 0, 0, test_case.attributes, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
            query.execution_mode = test_case.execution_mode;
            u8 output[8] = {0};
            BusterX86MetadataRelocation relocations[2] = {0};
            BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = query,
                .form_id = test_case.form_id,
                .output = output,
                .output_capacity = BUSTER_ARRAY_LENGTH(output),
                .relocations = relocations,
                .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
            });
            bool case_pass = emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                             emitted.relocation_count == 0 && emitted.byte_count == test_case.byte_count &&
                             x86_64_metadata_test_bytes_equal(output, emitted.byte_count, test_case.bytes, test_case.byte_count);
            direct_cases_pass &= case_pass;
        }
        BUSTER_TEST(arguments, direct_cases_pass);

        BusterX86MetadataPhysicalQuery wrong_mode = x86_64_metadata_test_physical_query(
            S8("INSW"), 0, 0, (BusterX86MetadataPhysicalAttributes){.rep = true}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        u8 wrong_mode_output[8] = {0};
        wrong_mode.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64;
        BusterX86MetadataEmitResult wrong_mode_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wrong_mode,
            .form_id = 9752,
            .output = wrong_mode_output,
            .output_capacity = BUSTER_ARRAY_LENGTH(wrong_mode_output),
        });
        BUSTER_TEST(arguments, wrong_mode_result.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   wrong_mode_result.byte_count == 0 && wrong_mode_result.relocation_count == 0);
    }

    {
        // Hidden string memory has one typed segment-prefix attribute.  The
        // six architectural segment bytes are emitted before address-size,
        // REP, and operand-size prefixes just as they are for explicit
        // PhysicalMemory segments.
        typedef struct X86_64MetadataImplicitSegmentCase X86_64MetadataImplicitSegmentCase;
        struct X86_64MetadataImplicitSegmentCase
        {
            u32 form_id;
            String8 mnemonic;
            u8 opcode;
        };
        static X86_64MetadataImplicitSegmentCase const cases[] = {
            {9897, S8_INITIALIZER("MOVSB"), 0xa4},
            {9775, S8_INITIALIZER("OUTSB"), 0x6e},
            {9921, S8_INITIALIZER("CMPSB"), 0xa6},
            {9971, S8_INITIALIZER("LODSB"), 0xac},
            {10041, S8_INITIALIZER("XLAT"), 0xd7},
        };
        static u8 const segments[] = {
            BUSTER_X86_METADATA_SEGMENT_ES,
            BUSTER_X86_METADATA_SEGMENT_CS,
            BUSTER_X86_METADATA_SEGMENT_SS,
            BUSTER_X86_METADATA_SEGMENT_DS,
            BUSTER_X86_METADATA_SEGMENT_FS,
            BUSTER_X86_METADATA_SEGMENT_GS,
        };
        String8 wildcard[1] = {S8("*")};
        bool implicit_segment_bytes_pass = true;
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(cases); case_index += 1)
        {
            X86_64MetadataImplicitSegmentCase test_case = cases[case_index];
            for (u32 segment_index = 0; segment_index < BUSTER_ARRAY_LENGTH(segments); segment_index += 1)
            {
                BusterX86MetadataPhysicalAttributes attributes = {
                    .implicit_segment = segments[segment_index],
                };
                u8 expected[] = {
                    segments[segment_index] == BUSTER_X86_METADATA_SEGMENT_ES ? 0x26
                    : segments[segment_index] == BUSTER_X86_METADATA_SEGMENT_CS ? 0x2e
                    : segments[segment_index] == BUSTER_X86_METADATA_SEGMENT_SS ? 0x36
                    : segments[segment_index] == BUSTER_X86_METADATA_SEGMENT_DS ? 0x3e
                    : segments[segment_index] == BUSTER_X86_METADATA_SEGMENT_FS ? 0x64
                                                                                : 0x65,
                    test_case.opcode,
                };
                implicit_segment_bytes_pass &= x86_64_metadata_test_emit_exact(
                    test_case.mnemonic, test_case.form_id, 0, 0, attributes, wildcard, BUSTER_ARRAY_LENGTH(wildcard), expected,
                    BUSTER_ARRAY_LENGTH(expected));
            }
        }
        BUSTER_TEST(arguments, implicit_segment_bytes_pass);

        BusterX86MetadataPhysicalQuery ordered_query = x86_64_metadata_test_physical_query(
            S8("MOVSB"), 0, 0,
            (BusterX86MetadataPhysicalAttributes){.implicit_segment = BUSTER_X86_METADATA_SEGMENT_FS, .rep = true}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        ordered_query.address_size = 32;
        u8 ordered_output[8] = {0};
        BusterX86MetadataEmitResult ordered_result = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = ordered_query, .form_id = 9895, .output = ordered_output,
                                         .output_capacity = BUSTER_ARRAY_LENGTH(ordered_output)});
        BUSTER_TEST(arguments, ordered_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && ordered_result.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(ordered_output, ordered_result.byte_count,
                                                                     (u8[]){0x64, 0x67, 0xf3, 0xa4}, 4));

        BusterX86MetadataPhysicalQuery branch_conflict = x86_64_metadata_test_physical_query(
            S8("JZ"), 0, 0,
            (BusterX86MetadataPhysicalAttributes){.implicit_segment = BUSTER_X86_METADATA_SEGMENT_FS,
                                                  .branch_hint = BUSTER_X86_METADATA_BRANCH_HINT_NOT_TAKEN},
            wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BUSTER_TEST(arguments, buster_x86_metadata_select_form(branch_conflict).status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand explicit_memory = x86_64_metadata_test_physical_mem_base(3, 64, 0);
        BusterX86MetadataPhysicalQuery visible_segment = x86_64_metadata_test_physical_query(
            S8("MOV"), &explicit_memory, 1,
            (BusterX86MetadataPhysicalAttributes){.implicit_segment = BUSTER_X86_METADATA_SEGMENT_FS}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BUSTER_TEST(arguments, buster_x86_metadata_select_form(visible_segment).status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalQuery invalid_segment = x86_64_metadata_test_physical_query(
            S8("MOVSB"), 0, 0,
            (BusterX86MetadataPhysicalAttributes){.implicit_segment = BUSTER_X86_METADATA_SEGMENT_COUNT}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BUSTER_TEST(arguments, buster_x86_metadata_select_form(invalid_segment).status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalQuery fixed_es = x86_64_metadata_test_physical_query(
            S8("STOSB"), 0, 0,
            (BusterX86MetadataPhysicalAttributes){.implicit_segment = BUSTER_X86_METADATA_SEGMENT_FS}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BUSTER_TEST(arguments, buster_x86_metadata_select_form(fixed_es).status != BUSTER_X86_METADATA_ENCODE_SUCCESS);
    }

    {
        // Exercise every fixed-opcode `not16` row through both the direct
        // form API and mnemonic selection.  The expected bytes are the
        // architectural NOP encodings; production code obtains them from
        // `form.fixed_bytes` and `form.mandatory_prefix`, never from this
        // test-only table.
        static u32 const form_ids[] = {10997, 10998, 10999, 11000, 11001, 11002, 11003, 11004};
        static u8 const byte_counts[] = {2, 3, 4, 5, 6, 7, 8, 9};
        static u8 const expected_bytes[][9] = {
            {0x66, 0x90, 0, 0, 0, 0, 0, 0, 0},
            {0x0f, 0x1f, 0x00, 0, 0, 0, 0, 0, 0},
            {0x0f, 0x1f, 0x40, 0x00, 0, 0, 0, 0, 0},
            {0x0f, 0x1f, 0x44, 0x00, 0x00, 0, 0, 0, 0},
            {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00, 0, 0, 0},
            {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00, 0, 0},
            {0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0},
            {0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        };
        bool not16_forms_pass = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(form_ids); index += 1)
        {
            u32 form_id = form_ids[index];
            u8 expected_count = byte_counts[index];
            BusterX86MetadataForm form = {0};
            BusterX86MetadataPhysicalOperand operands[16] = {0};
            char8 mnemonic_buffer[128] = {0};
            BusterX86MetadataPhysicalQuery query = {0};
            not16_forms_pass &= buster_x86_metadata_form(form_id, &form) &&
                                x86_64_metadata_test_pattern_has_token(form.pattern, S8("not16")) &&
                                form.fixed_byte_count != 0;
            not16_forms_pass &= x86_64_metadata_test_build_gate_query(form_id, &query, operands, mnemonic_buffer);

            u8 output[32] = {0};
            BusterX86MetadataRelocation relocations[8] = {0};
            BusterX86MetadataEmitResult direct = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = query,
                .form_id = form_id,
                .output = output,
                .output_capacity = sizeof(output),
                .relocations = relocations,
                .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
            });
            not16_forms_pass &= direct.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && direct.form_id == form_id &&
                                direct.byte_count == expected_count && direct.relocation_count == 0 &&
                                x86_64_metadata_test_bytes_equal(output, direct.byte_count, expected_bytes[index], expected_count);

            BusterX86MetadataPhysicalQuery mode32_query = query;
            mode32_query.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_32;
            BusterX86MetadataEmitResult mode32_direct = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = mode32_query,
                .form_id = form_id,
                .output = output,
                .output_capacity = sizeof(output),
                .relocations = relocations,
                .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
            });
            not16_forms_pass &= mode32_direct.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                mode32_direct.byte_count == expected_count &&
                                x86_64_metadata_test_bytes_equal(output, mode32_direct.byte_count, expected_bytes[index], expected_count);

            BusterX86MetadataPhysicalQuery mode16_query = query;
            mode16_query.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_16;
            BusterX86MetadataSelectResult mode16_selected = buster_x86_metadata_select_form(mode16_query);
            not16_forms_pass &= mode16_selected.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
            BusterX86MetadataEmitResult mode16_direct = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = mode16_query,
                .form_id = form_id,
                .output = output,
                .output_capacity = sizeof(output),
                .relocations = relocations,
                .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
            });
            not16_forms_pass &= mode16_direct.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE && mode16_direct.byte_count == 0;

            BusterX86MetadataPhysicalQuery any_mode_query = query;
            any_mode_query.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_ANY;
            BusterX86MetadataSelectResult any_selected = buster_x86_metadata_select_form(any_mode_query);
            not16_forms_pass &= any_selected.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;
            BusterX86MetadataEmitResult any_direct = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = any_mode_query,
                .form_id = form_id,
                .output = output,
                .output_capacity = sizeof(output),
                .relocations = relocations,
                .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
            });
            not16_forms_pass &= any_direct.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE && any_direct.byte_count == 0;

            BusterX86MetadataPhysicalQuery prefix_query = query;
            prefix_query.attributes.rep = true;
            BusterX86MetadataEmitResult prefix_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = prefix_query,
                .form_id = form_id,
                .output = output,
                .output_capacity = sizeof(output),
                .relocations = relocations,
                .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
            });
            not16_forms_pass &= prefix_result.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION && prefix_result.byte_count == 0;

            BusterX86MetadataEmitResult short_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = query,
                .form_id = form_id,
                .output = output,
                .output_capacity = expected_count - 1,
                .relocations = relocations,
                .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
            });
            not16_forms_pass &= short_result.status == BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY && short_result.byte_count == 0 &&
                                short_result.required_byte_count == expected_count;
        }

        // The front door has no requested NOP length in its physical query,
        // so all equivalent NOP candidates must resolve to one deterministic
        // canonical form (the shortest, then lowest generated ID).  Test that
        // policy once; per-length coverage above uses the direct form API.
        BusterX86MetadataPhysicalOperand canonical_operands[16] = {0};
        char8 canonical_mnemonic_buffer[128] = {0};
        BusterX86MetadataPhysicalQuery canonical_query = {0};
        bool canonical_built = x86_64_metadata_test_build_gate_query(
            form_ids[0], &canonical_query, canonical_operands, canonical_mnemonic_buffer);
        BusterX86MetadataSelectResult canonical_selected = canonical_built ? buster_x86_metadata_select_form(canonical_query)
                                                                            : (BusterX86MetadataSelectResult){0};
        u8 canonical_output[32] = {0};
        BusterX86MetadataRelocation canonical_relocations[8] = {0};
        BusterX86MetadataEmitResult canonical_emit = canonical_built && canonical_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS
                                                         ? buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                                                               .physical = canonical_query,
                                                               .form_id = canonical_selected.form_id,
                                                               .output = canonical_output,
                                                               .output_capacity = sizeof(canonical_output),
                                                               .relocations = canonical_relocations,
                                                               .relocation_capacity = BUSTER_ARRAY_LENGTH(canonical_relocations),
                                                           })
                                                         : (BusterX86MetadataEmitResult){0};
        not16_forms_pass &= canonical_built && canonical_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                            canonical_selected.form_id == form_ids[0] && canonical_selected.candidate_count > 0 &&
                            canonical_selected.selected_byte_count == byte_counts[0] &&
                            canonical_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                            x86_64_metadata_test_bytes_equal(canonical_output, canonical_emit.byte_count, expected_bytes[0], byte_counts[0]);
        BUSTER_TEST(arguments, not16_forms_pass);
    }

    {
        // Exact bytes for every normalized row that this change unblocks.
        // The stack forms also exercise DF64's 64-bit default, its explicit
        // 16-bit override, and its contradictory 32-bit guard.
        BusterX86MetadataPhysicalOperand pop_mem64 = x86_64_metadata_test_physical_mem_base(0, 64, 0);
        BusterX86MetadataPhysicalOperand pop_reg64 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataPhysicalOperand pop_mem16 = x86_64_metadata_test_physical_mem_base(0, 16, 0);
        BusterX86MetadataPhysicalOperand pop_reg16 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 16);
        BusterX86MetadataPhysicalOperand pop_mem32 = x86_64_metadata_test_physical_mem_base(0, 32, 0);
        BusterX86MetadataPhysicalOperand pop_reg32 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32);
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("POP"), 9337, &pop_mem64, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x8f, 0x00}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("POP"), 9338, &pop_reg64, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x8f, 0xc0}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 9490, &pop_mem64, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xff, 0x30}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 9491, &pop_reg64, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xff, 0xf0}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("POP"), 9337, &pop_mem16, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x66, 0x8f, 0x00}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("POP"), 9338, &pop_reg16, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x66, 0x8f, 0xc0}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 9490, &pop_mem16, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x66, 0xff, 0x30}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 9491, &pop_reg16, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x66, 0xff, 0xf0}, 3));
        u8 guard_output[8] = {0};
        BusterX86MetadataEmitResult pop_mem32_result = x86_64_metadata_test_emit_form(
            S8("POP"), 9337, &pop_mem32, 1, (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BusterX86MetadataEmitResult pop_reg32_result = x86_64_metadata_test_emit_form(
            S8("POP"), 9338, &pop_reg32, 1, (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BusterX86MetadataEmitResult push_mem32_result = x86_64_metadata_test_emit_form(
            S8("PUSH"), 9490, &pop_mem32, 1, (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BusterX86MetadataEmitResult push_reg32_result = x86_64_metadata_test_emit_form(
            S8("PUSH"), 9491, &pop_reg32, 1, (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BUSTER_TEST(arguments, pop_mem32_result.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH &&
                                   pop_reg32_result.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH &&
                                   push_mem32_result.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH &&
                                   push_reg32_result.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);

        BusterX86MetadataPhysicalOperand push_imm32 = x86_64_metadata_test_physical_imm(0x12345678, 32);
        BusterX86MetadataPhysicalOperand push_imm8 = x86_64_metadata_test_physical_imm(0x7f, 8);
        BusterX86MetadataPhysicalOperand push_symbol32 = push_imm32;
        push_symbol32.value = 0;
        push_symbol32.has_value = false;
        push_symbol32.symbol = S8("push_target");
        push_symbol32.has_symbol = true;
        BusterX86MetadataPhysicalOperand ret_imm16 = x86_64_metadata_test_physical_imm_u64(0x1234, 16);
        BusterX86MetadataPhysicalOperand io_imm8 = x86_64_metadata_test_physical_imm_u64(0x7f, 8);
        BusterX86MetadataPhysicalOperand loop_disp8 = x86_64_metadata_test_physical_relative(0, 8);
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 9743, &push_imm32, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x68, 0x78, 0x56, 0x34, 0x12}, 5));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 9746, &push_imm8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x6a, 0x7f}, 2));
        BusterX86MetadataPhysicalQuery push_symbol_query = x86_64_metadata_test_physical_query(
            S8("PUSH"), &push_symbol32, 1, (BusterX86MetadataPhysicalAttributes){0}, (String8[1]){S8("*")}, 1);
        u8 push_symbol_bytes[8] = {0};
        BusterX86MetadataRelocation push_symbol_relocations[2] = {0};
        BusterX86MetadataSelectResult push_symbol_select = buster_x86_metadata_select_form(push_symbol_query);
        BusterX86MetadataEmitResult push_symbol_emit = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = push_symbol_query,
            .output = push_symbol_bytes,
            .output_capacity = sizeof(push_symbol_bytes),
            .relocations = push_symbol_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(push_symbol_relocations),
        });
        BUSTER_TEST(arguments, push_symbol_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && push_symbol_select.form_id == 9743 &&
                                   push_symbol_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && push_symbol_emit.form_id == 9743 &&
                                   push_symbol_emit.byte_count == 5 && push_symbol_emit.relocation_count == 1 &&
                                   push_symbol_relocations[0].offset == 1 && push_symbol_relocations[0].width == 4 &&
                                   push_symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32);
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("RET_NEAR"), 10019, &ret_imm16, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xc2, 0x34, 0x12}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("RET_NEAR"), 10020, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xc3}, 1));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LEAVE"), 10024, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xc9}, 1));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 10272, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x0f, 0xa0}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("POP"), 10273, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x0f, 0xa1}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 10658, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x0f, 0xa8}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("POP"), 10659, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0x0f, 0xa9}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("IN"), 10056, &io_imm8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xe5, 0x7f}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("OUT"), 10058, &io_imm8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xe7, 0x7f}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("IN"), 10065, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xed}, 1));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("OUT"), 10067, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xef}, 1));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LOOPNE"), 10044, &loop_disp8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xe0, 0x00}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LOOPE"), 10048, &loop_disp8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xe1, 0x00}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LOOP"), 10050, &loop_disp8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xe2, 0x00}, 2));
        BusterX86MetadataEmitResult loop_rep_result = x86_64_metadata_test_emit_form(
            S8("LOOP"), 10050, &loop_disp8, 1, (BusterX86MetadataPhysicalAttributes){.rep = true}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BUSTER_TEST(arguments, loop_rep_result.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);

        // XED's documented MODEP5/REP-selector LOOP rows have ordinary
        // legacy encodings.  The REP selector is emitted after an optional
        // address-size override, matching LLVM MC's 67/F2/F3 prefix order.
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LOOPNE"), 10042, &loop_disp8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xe0, 0x00}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LOOPNE"), 10043, &loop_disp8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){.repne = true}, 0, 0,
                                                                 (u8 const[]){0xf2, 0xe0, 0x00}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LOOPE"), 10046, &loop_disp8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xe1, 0x00}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LOOPE"), 10047, &loop_disp8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){.rep = true}, 0, 0,
                                                                 (u8 const[]){0xf3, 0xe1, 0x00}, 3));

        BusterX86MetadataPhysicalQuery loop_addr32_query = x86_64_metadata_test_physical_query(
            S8("LOOPNE"), &loop_disp8, 1, (BusterX86MetadataPhysicalAttributes){.repne = true}, 0, 0);
        loop_addr32_query.address_size = 32;
        u8 loop_addr32_output[8] = {0};
        BusterX86MetadataEmitResult loop_addr32_repne = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = loop_addr32_query, .form_id = 10043, .output = loop_addr32_output,
            .output_capacity = BUSTER_ARRAY_LENGTH(loop_addr32_output),
        });
        BUSTER_TEST(arguments, loop_addr32_repne.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(loop_addr32_output, loop_addr32_repne.byte_count,
                                                                    (u8 const[]){0x67, 0xf2, 0xe0, 0x00}, 4));

        loop_addr32_query.mnemonic = S8("LOOPE");
        loop_addr32_query.attributes = (BusterX86MetadataPhysicalAttributes){.rep = true};
        memset(loop_addr32_output, 0, sizeof(loop_addr32_output));
        BusterX86MetadataEmitResult loop_addr32_rep = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = loop_addr32_query, .form_id = 10047, .output = loop_addr32_output,
            .output_capacity = BUSTER_ARRAY_LENGTH(loop_addr32_output),
        });
        BUSTER_TEST(arguments, loop_addr32_rep.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(loop_addr32_output, loop_addr32_rep.byte_count,
                                                                    (u8 const[]){0x67, 0xf3, 0xe1, 0x00}, 4));

        BusterX86MetadataPhysicalQuery loop_plain_query = x86_64_metadata_test_physical_query(
            S8("LOOPNE"), &loop_disp8, 1, (BusterX86MetadataPhysicalAttributes){0}, 0, 0);
        loop_plain_query.source_semantics = true;
        BusterX86MetadataSelectResult loop_plain_selection = buster_x86_metadata_select_form(loop_plain_query);
        loop_plain_query.attributes = (BusterX86MetadataPhysicalAttributes){.repne = true};
        BusterX86MetadataSelectResult loop_repne_selection = buster_x86_metadata_select_form(loop_plain_query);
        loop_plain_query.mnemonic = S8("LOOPE");
        loop_plain_query.attributes = (BusterX86MetadataPhysicalAttributes){.rep = true};
        BusterX86MetadataSelectResult loop_rep_selection = buster_x86_metadata_select_form(loop_plain_query);
        BUSTER_TEST(arguments, loop_plain_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   loop_plain_selection.form_id == 10044 &&
                                   loop_repne_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   loop_repne_selection.form_id == 10043 &&
                                   loop_rep_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   loop_rep_selection.form_id == 10047);

        BusterX86MetadataPhysicalQuery loop_addr16_query = loop_plain_query;
        loop_addr16_query.mnemonic = S8("LOOPNE");
        loop_addr16_query.attributes = (BusterX86MetadataPhysicalAttributes){.repne = true};
        loop_addr16_query.address_size = 16;
        BusterX86MetadataSelectResult loop_addr16_selection = buster_x86_metadata_select_form(loop_addr16_query);
        BusterX86MetadataEmitResult loop_wrong_rep = x86_64_metadata_test_emit_form(
            S8("LOOPNE"), 10043, &loop_disp8, 1, (BusterX86MetadataPhysicalAttributes){.rep = true}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BusterX86MetadataEmitResult loop_wrong_repne = x86_64_metadata_test_emit_form(
            S8("LOOPE"), 10047, &loop_disp8, 1, (BusterX86MetadataPhysicalAttributes){.repne = true}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BusterX86MetadataEmitResult undocumented_loopne = x86_64_metadata_test_emit_form(
            S8("LOOPNE"), 10045, &loop_disp8, 1, (BusterX86MetadataPhysicalAttributes){.repne = true}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BusterX86MetadataEmitResult undocumented_loope = x86_64_metadata_test_emit_form(
            S8("LOOPE"), 10049, &loop_disp8, 1, (BusterX86MetadataPhysicalAttributes){.rep = true}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BUSTER_TEST(arguments, loop_addr16_selection.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   loop_wrong_rep.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION &&
                                   loop_wrong_repne.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION &&
                                   undocumented_loopne.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   undocumented_loope.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA);

        // CET_NO_TRACK is an optional 0x3e prefix on the four real indirect
        // CALL/JMP rows.  The same rows must retain their ordinary FF /2,/4
        // encodings when the typed source/query attribute is absent.
        BusterX86MetadataPhysicalOperand branch_mem64 = x86_64_metadata_test_physical_mem_base(0, 64, 0);
        BusterX86MetadataPhysicalOperand branch_reg64 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataPhysicalAttributes notrack_attributes = {.notrack = true};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CALL_NEAR"), 9484, &branch_reg64, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xff, 0xd0}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CALL_NEAR"), 9484, &branch_reg64, 1,
                                                                 notrack_attributes, 0, 0,
                                                                 (u8 const[]){0x3e, 0xff, 0xd0}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CALL_NEAR"), 9483, &branch_mem64, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xff, 0x10}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CALL_NEAR"), 9483, &branch_mem64, 1,
                                                                 notrack_attributes, 0, 0,
                                                                 (u8 const[]){0x3e, 0xff, 0x10}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JMP"), 9488, &branch_reg64, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xff, 0xe0}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JMP"), 9488, &branch_reg64, 1,
                                                                 notrack_attributes, 0, 0,
                                                                 (u8 const[]){0x3e, 0xff, 0xe0}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JMP"), 9487, &branch_mem64, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, 0, 0,
                                                                 (u8 const[]){0xff, 0x20}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JMP"), 9487, &branch_mem64, 1,
                                                                 notrack_attributes, 0, 0,
                                                                 (u8 const[]){0x3e, 0xff, 0x20}, 3));
        BusterX86MetadataEmitResult notrack_direct_result = x86_64_metadata_test_emit_form(
            S8("CALL_NEAR"), 9484, &branch_reg64, 1, (BusterX86MetadataPhysicalAttributes){.rep = true, .notrack = true}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BUSTER_TEST(arguments, notrack_direct_result.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);
        BusterX86MetadataEmitResult notrack_lock_result = x86_64_metadata_test_emit_form(
            S8("CALL_NEAR"), 9483, &branch_mem64, 1, (BusterX86MetadataPhysicalAttributes){.lock = true, .notrack = true}, 0, 0,
            guard_output, sizeof(guard_output), 0, 0);
        BUSTER_TEST(arguments, notrack_lock_result.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);
        BusterX86MetadataPhysicalQuery notrack_query = x86_64_metadata_test_physical_query(
            S8("CALL"), &branch_reg64, 1, notrack_attributes, 0, 0);
        notrack_query.features.names = (String8[]){S8("*")};
        notrack_query.features.count = 1;
        BusterX86MetadataSelectResult notrack_selection = buster_x86_metadata_select_form(notrack_query);
        BUSTER_TEST(arguments, notrack_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && notrack_selection.form_id == 9484 &&
                                   notrack_selection.selected_byte_count == 3);
        BusterX86MetadataPhysicalQuery default_branch_query = x86_64_metadata_test_physical_query(
            S8("CALL"), &branch_reg64, 1, (BusterX86MetadataPhysicalAttributes){0}, 0, 0);
        default_branch_query.features.names = (String8[]){S8("*")};
        default_branch_query.features.count = 1;
        BusterX86MetadataSelectResult default_branch_selection = buster_x86_metadata_select_form(default_branch_query);
        BUSTER_TEST(arguments, default_branch_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   default_branch_selection.form_id == 9484 && default_branch_selection.selected_byte_count == 2);

        // Representative ADCX and CMPXCHG8B byte checks pin the old
        // IMMUNE66 path; the audit ledger above covers all 26 rows' status.
        BusterX86MetadataPhysicalOperand adcx32_operands[] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 32),
        };
        BusterX86MetadataPhysicalOperand adcx64_operands[] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64),
        };
        String8 wildcard_features[1] = {S8("*")};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADCX"), 7909, adcx32_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard_features, 1,
                                                                 (u8 const[]){0x66, 0x0f, 0x38, 0xf6, 0xca}, 5));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADCX"), 7911, adcx64_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard_features, 1,
                                                                 (u8 const[]){0x66, 0x48, 0x0f, 0x38, 0xf6, 0xca}, 6));

        // Unsized source memory is accepted for CMPXCHG8B/16B, whose
        // architectural forms carry fixed qword/double-quadword widths.
        // The checked public encoder must infer those widths before form
        // selection while retaining LOCK and CX16 feature semantics.
        BusterX86MetadataPhysicalOperand cmpxchg8b_unsized = x86_64_metadata_test_physical_mem_base(8, 0, 0);
        BusterX86MetadataPhysicalQuery cmpxchg8b_unsized_query = x86_64_metadata_test_physical_query(
            S8("CMPXCHG8B"), &cmpxchg8b_unsized, 1,
            (BusterX86MetadataPhysicalAttributes){.lock = true}, wildcard_features, BUSTER_ARRAY_LENGTH(wildcard_features));
        cmpxchg8b_unsized_query.source_semantics = true;
        u8 cmpxchg_unsized_bytes[8] = {0};
        BusterX86MetadataEmitResult cmpxchg8b_unsized_emit = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = cmpxchg8b_unsized_query,
            .output = cmpxchg_unsized_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(cmpxchg_unsized_bytes),
        });
        BUSTER_TEST(arguments, cmpxchg8b_unsized_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                       cmpxchg8b_unsized_emit.form_id == 9526 && cmpxchg8b_unsized_emit.byte_count == 5 &&
                                       x86_64_metadata_test_bytes_equal(cmpxchg_unsized_bytes, 5,
                                                                         (u8 const[]){0xf0, 0x41, 0x0f, 0xc7, 0x08}, 5));
        String8 cmpxchg16b_features[1] = {S8("cx16")};
        BusterX86MetadataPhysicalOperand cmpxchg16b_unsized = x86_64_metadata_test_physical_mem_base(14, 0, 0);
        BusterX86MetadataPhysicalQuery cmpxchg16b_unsized_query = x86_64_metadata_test_physical_query(
            S8("CMPXCHG16B"), &cmpxchg16b_unsized, 1,
            (BusterX86MetadataPhysicalAttributes){.lock = true}, cmpxchg16b_features,
            BUSTER_ARRAY_LENGTH(cmpxchg16b_features));
        cmpxchg16b_unsized_query.source_semantics = true;
        BusterX86MetadataEmitResult cmpxchg16b_unsized_emit = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = cmpxchg16b_unsized_query,
            .output = cmpxchg_unsized_bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(cmpxchg_unsized_bytes),
        });
        BUSTER_TEST(arguments, cmpxchg16b_unsized_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                       cmpxchg16b_unsized_emit.form_id == 9529 && cmpxchg16b_unsized_emit.byte_count == 5 &&
                                       x86_64_metadata_test_bytes_equal(cmpxchg_unsized_bytes, 5,
                                                                         (u8 const[]){0xf0, 0x49, 0x0f, 0xc7, 0x0e}, 5));
    }

    {
        // Privileged/system rows are directly encodable when the caller opts
        // into the policy class.  These exact forms cover the legacy prefix,
        // ModRM, address-size, and no-REX2 shapes represented by the 90-row
        // valid-64 inventory; public assembly tests below exercise routing.
        String8 invlpgb_feature[] = {S8("invlpgb")};
        String8 monitor_feature[] = {S8("monitor")};
        String8 invpcid_feature[] = {S8("invpcid")};
        String8 vmx_feature[] = {S8("vmx")};
        u8 output[32] = {0};
        BusterX86MetadataRelocation relocations[8] = {0};

        BusterX86MetadataPhysicalQuery invlpgb_addr32_query = x86_64_metadata_test_physical_query(
            S8("INVLPGB"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, invlpgb_feature, BUSTER_ARRAY_LENGTH(invlpgb_feature));
        invlpgb_addr32_query.include_privileged = true;
        invlpgb_addr32_query.address_size = 32;
        BusterX86MetadataEmitResult invlpgb_addr32 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invlpgb_addr32_query, .form_id = 423, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invlpgb_addr32.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && invlpgb_addr32.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(output, invlpgb_addr32.byte_count,
                                                                     (u8 const[]){0x67, 0x0f, 0x01, 0xfe}, 4));
        invlpgb_addr32_query.address_size = 64;
        BusterX86MetadataEmitResult invlpgb_addr32_mismatch = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invlpgb_addr32_query, .form_id = 423, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invlpgb_addr32_mismatch.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING &&
                                   invlpgb_addr32_mismatch.byte_count == 0 && invlpgb_addr32_mismatch.relocation_count == 0);
        BusterX86MetadataPhysicalQuery invlpgb_addr64_query = invlpgb_addr32_query;
        BusterX86MetadataEmitResult invlpgb_addr64 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invlpgb_addr64_query, .form_id = 424, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invlpgb_addr64.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && invlpgb_addr64.byte_count == 3 &&
                                   x86_64_metadata_test_bytes_equal(output, invlpgb_addr64.byte_count, (u8 const[]){0x0f, 0x01, 0xfe}, 3));
        invlpgb_addr64_query.address_size = 32;
        BusterX86MetadataEmitResult invlpgb_addr64_mismatch = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invlpgb_addr64_query, .form_id = 424, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invlpgb_addr64_mismatch.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING &&
                                   invlpgb_addr64_mismatch.byte_count == 0 && invlpgb_addr64_mismatch.relocation_count == 0);

        BusterX86MetadataPhysicalQuery monitor_addr64_query = x86_64_metadata_test_physical_query(
            S8("MONITOR"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, monitor_feature, BUSTER_ARRAY_LENGTH(monitor_feature));
        monitor_addr64_query.include_privileged = true;
        BusterX86MetadataEmitResult monitor_addr64 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = monitor_addr64_query, .form_id = 9599, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, monitor_addr64.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && monitor_addr64.byte_count == 3 &&
                                   x86_64_metadata_test_bytes_equal(output, monitor_addr64.byte_count, (u8 const[]){0x0f, 0x01, 0xc8}, 3));
        BusterX86MetadataPhysicalQuery monitor_addr32_query = monitor_addr64_query;
        monitor_addr32_query.address_size = 32;
        BusterX86MetadataEmitResult monitor_addr32 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = monitor_addr32_query, .form_id = 9600, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, monitor_addr32.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && monitor_addr32.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(output, monitor_addr32.byte_count,
                                                                     (u8 const[]){0x67, 0x0f, 0x01, 0xc8}, 4));

        String8 empty_features[] = {0};
        BusterX86MetadataPhysicalQuery wbinvd_query = x86_64_metadata_test_physical_query(
            S8("WBINVD"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, empty_features, 0);
        wbinvd_query.include_privileged = true;
        BusterX86MetadataEmitResult wbinvd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wbinvd_query, .form_id = 9064, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, wbinvd.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && wbinvd.byte_count == 2 &&
                                   x86_64_metadata_test_bytes_equal(output, wbinvd.byte_count, (u8 const[]){0x0f, 0x09}, 2));
        wbinvd_query.attributes.repne = true;
        BusterX86MetadataEmitResult repne_wbinvd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wbinvd_query, .form_id = 9065, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, repne_wbinvd.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && repne_wbinvd.byte_count == 3 &&
                                   x86_64_metadata_test_bytes_equal(output, repne_wbinvd.byte_count, (u8 const[]){0xf2, 0x0f, 0x09}, 3));
        wbinvd_query.attributes.repne = false;
        wbinvd_query.attributes.rep = true;
        BusterX86MetadataEmitResult rep_wbinvd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wbinvd_query, .form_id = 9065, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, rep_wbinvd.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION && rep_wbinvd.byte_count == 0 &&
                                   rep_wbinvd.relocation_count == 0);
        String8 wbnoinvd_feature[] = {S8("wbnoinvd")};
        BusterX86MetadataPhysicalQuery wbnoinvd_query = x86_64_metadata_test_physical_query(
            S8("WBNOINVD"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wbnoinvd_feature,
            BUSTER_ARRAY_LENGTH(wbnoinvd_feature));
        wbnoinvd_query.include_privileged = true;
        BusterX86MetadataEmitResult wbnoinvd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wbnoinvd_query, .form_id = 9066, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, wbnoinvd.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && wbnoinvd.byte_count == 3 &&
                                   x86_64_metadata_test_bytes_equal(output, wbnoinvd.byte_count, (u8 const[]){0xf3, 0x0f, 0x09}, 3));

        BusterX86MetadataPhysicalOperand gpr0 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataPhysicalOperand gpr1 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64);
        BusterX86MetadataPhysicalOperand gpr2 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64);
        BusterX86MetadataPhysicalOperand cr15 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL, 15, 64);
        BusterX86MetadataPhysicalOperand dr15 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG, 15, 64);
        BusterX86MetadataPhysicalOperand mov_cr_store[] = {cr15, gpr0};
        BusterX86MetadataPhysicalOperand mov_cr_load[] = {gpr0, cr15};
        BusterX86MetadataPhysicalOperand mov_dr_store[] = {dr15, gpr0};
        BusterX86MetadataPhysicalOperand mov_dr_load[] = {gpr0, dr15};
        u8 mov_cr_store_output[32] = {0};
        u8 mov_cr_load_output[32] = {0};
        u8 mov_dr_store_output[32] = {0};
        u8 mov_dr_load_output[32] = {0};
        BusterX86MetadataPhysicalQuery mov_cr_store_query = x86_64_metadata_test_physical_query(
            S8("MOV_CR"), mov_cr_store, BUSTER_ARRAY_LENGTH(mov_cr_store), (BusterX86MetadataPhysicalAttributes){0}, empty_features, 0);
        mov_cr_store_query.include_privileged = true;
        BusterX86MetadataPhysicalQuery mov_cr_load_query = mov_cr_store_query;
        mov_cr_load_query.mnemonic = S8("MOV_CR");
        mov_cr_load_query.operands = mov_cr_load;
        BusterX86MetadataPhysicalQuery mov_dr_store_query = mov_cr_store_query;
        mov_dr_store_query.mnemonic = S8("MOV_DR");
        mov_dr_store_query.operands = mov_dr_store;
        BusterX86MetadataPhysicalQuery mov_dr_load_query = mov_dr_store_query;
        mov_dr_load_query.operands = mov_dr_load;
        BusterX86MetadataEmitResult mov_cr_store_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = mov_cr_store_query, .form_id = 10122, .output = mov_cr_store_output, .output_capacity = sizeof(mov_cr_store_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BusterX86MetadataEmitResult mov_cr_load_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = mov_cr_load_query, .form_id = 10124, .output = mov_cr_load_output, .output_capacity = sizeof(mov_cr_load_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BusterX86MetadataEmitResult mov_dr_store_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = mov_dr_store_query, .form_id = 10126, .output = mov_dr_store_output, .output_capacity = sizeof(mov_dr_store_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BusterX86MetadataEmitResult mov_dr_load_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = mov_dr_load_query, .form_id = 10128, .output = mov_dr_load_output, .output_capacity = sizeof(mov_dr_load_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, mov_cr_store_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(mov_cr_store_output, mov_cr_store_result.byte_count,
                                                                     (u8 const[]){0x44, 0x0f, 0x22, 0xf8}, 4));
        BUSTER_TEST(arguments, mov_cr_load_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(mov_cr_load_output, mov_cr_load_result.byte_count,
                                                                     (u8 const[]){0x44, 0x0f, 0x20, 0xf8}, 4));
        BUSTER_TEST(arguments, mov_dr_store_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(mov_dr_store_output, mov_dr_store_result.byte_count,
                                                                     (u8 const[]){0x44, 0x0f, 0x23, 0xf8}, 4));
        BUSTER_TEST(arguments, mov_dr_load_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(mov_dr_load_output, mov_dr_load_result.byte_count,
                                                                     (u8 const[]){0x44, 0x0f, 0x21, 0xf8}, 4));

        BusterX86MetadataPhysicalOperand invpcid_operands[] = {
            gpr1, x86_64_metadata_test_physical_mem_base(0, 128, 0)};
        BusterX86MetadataPhysicalQuery invpcid_query = x86_64_metadata_test_physical_query(
            S8("INVPCID"), invpcid_operands, BUSTER_ARRAY_LENGTH(invpcid_operands), (BusterX86MetadataPhysicalAttributes){0},
            invpcid_feature, BUSTER_ARRAY_LENGTH(invpcid_feature));
        invpcid_query.include_privileged = true;
        BusterX86MetadataEmitResult invpcid = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = invpcid_query, .form_id = 8100, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, invpcid.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && invpcid.byte_count == 5 &&
                                   x86_64_metadata_test_bytes_equal(output, invpcid.byte_count,
                                                                     (u8 const[]){0x66, 0x0f, 0x38, 0x82, 0x08}, 5));

        BusterX86MetadataPhysicalOperand vmx_memory_operands[] = {gpr1, x86_64_metadata_test_physical_mem_base(0, 128, 0)};
        u32 vmx_memory_form_ids[] = {10993, 10995};
        u8 vmx_memory_opcodes[] = {0x80, 0x81};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(vmx_memory_form_ids); index += 1)
        {
            BusterX86MetadataPhysicalQuery query = x86_64_metadata_test_physical_query(
                index == 0 ? S8("INVEPT") : S8("INVVPID"), vmx_memory_operands, BUSTER_ARRAY_LENGTH(vmx_memory_operands),
                (BusterX86MetadataPhysicalAttributes){0}, vmx_feature, BUSTER_ARRAY_LENGTH(vmx_feature));
            query.include_privileged = true;
            BusterX86MetadataEmitResult vmx_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = query, .form_id = vmx_memory_form_ids[index], .output = output, .output_capacity = sizeof(output),
                .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
            u8 expected[] = {0x66, 0x0f, 0x38, vmx_memory_opcodes[index], 0x08};
            BUSTER_TEST(arguments, vmx_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && vmx_result.byte_count == sizeof(expected) &&
                                       x86_64_metadata_test_bytes_equal(output, vmx_result.byte_count, expected, sizeof(expected)));
        }

        BusterX86MetadataPhysicalOperand vmread_memory_operands[] = {
            x86_64_metadata_test_physical_mem_base(0, 64, 0), gpr1};
        BusterX86MetadataPhysicalOperand vmwrite_memory_operands[] = {
            gpr1, x86_64_metadata_test_physical_mem_base(0, 64, 0)};
        BusterX86MetadataPhysicalOperand vmread_register_operands[] = {gpr2, gpr1};
        BusterX86MetadataPhysicalOperand vmwrite_register_operands[] = {gpr1, gpr2};
        u32 vmx_data_form_ids[] = {10606, 10607, 10610, 10611};
        BusterX86MetadataPhysicalOperand const* vmx_data_operands[] = {
            vmread_memory_operands, vmread_register_operands, vmwrite_memory_operands, vmwrite_register_operands};
        u8 vmx_data_opcodes[] = {0x78, 0x78, 0x79, 0x79};
        u8 vmx_data_modrms[] = {0x08, 0xca, 0x08, 0xca};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(vmx_data_form_ids); index += 1)
        {
            BusterX86MetadataPhysicalQuery query = x86_64_metadata_test_physical_query(
                index < 2 ? S8("VMREAD") : S8("VMWRITE"), vmx_data_operands[index], 2,
                (BusterX86MetadataPhysicalAttributes){0}, vmx_feature, BUSTER_ARRAY_LENGTH(vmx_feature));
            query.include_privileged = true;
            BusterX86MetadataEmitResult vmx_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = query, .form_id = vmx_data_form_ids[index], .output = output, .output_capacity = sizeof(output),
                .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
            u8 expected[] = {0x0f, vmx_data_opcodes[index], vmx_data_modrms[index]};
            BUSTER_TEST(arguments, vmx_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && vmx_result.byte_count == sizeof(expected) &&
                                       x86_64_metadata_test_bytes_equal(output, vmx_result.byte_count, expected, sizeof(expected)));
        }

        // APX system forms use EVEX for EGPR extension bits, but their XED
        // fixed-width schemas keep W clear.  Keep every reachable mapping
        // byte-exact here so the generic APX width heuristic cannot regress
        // one ISA set while fixing another.
        BusterX86MetadataPhysicalOperand gpr16 = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64);
        String8 apx_enqcmd_features[] = {S8("apx"), S8("enqcmd")};
        String8 apx_invpcid_features[] = {S8("apx"), S8("invpcid")};
        String8 apx_msr_imm_features[] = {S8("apx"), S8("msr-imm")};
        String8 apx_vmx_features[] = {S8("apx"), S8("vmx")};
        String8 apx_movdir64b_features[] = {S8("apx"), S8("movdir64b")};
        BusterX86MetadataPhysicalOperand apx_descriptor_memory = x86_64_metadata_test_physical_mem_base(0, 128, 0);
        BusterX86MetadataPhysicalOperand apx_enqcmd_memory = x86_64_metadata_test_physical_mem_base(0, 32, 0);
        BusterX86MetadataPhysicalOperand apx_memory_operands[] = {gpr16, apx_descriptor_memory};
        BusterX86MetadataPhysicalOperand apx_enqcmd_operands[] = {gpr16, apx_enqcmd_memory};
        BusterX86MetadataPhysicalQuery apx_enqcmd_query = x86_64_metadata_test_physical_query(
            S8("ENQCMDS"), apx_enqcmd_operands, BUSTER_ARRAY_LENGTH(apx_enqcmd_operands),
            (BusterX86MetadataPhysicalAttributes){0}, apx_enqcmd_features, BUSTER_ARRAY_LENGTH(apx_enqcmd_features));
        apx_enqcmd_query.include_privileged = true;
        BusterX86MetadataEmitResult apx_enqcmd = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = apx_enqcmd_query, .form_id = 1750, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_enqcmd.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_enqcmd.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_enqcmd.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7e, 0x08, 0xf8, 0x00}, 6));
        BusterX86MetadataPhysicalQuery apx_enqcmd_user_query = apx_enqcmd_query;
        apx_enqcmd_user_query.mnemonic = S8("ENQCMD");
        apx_enqcmd_user_query.include_privileged = false;
        BusterX86MetadataEmitResult apx_enqcmd_user = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = apx_enqcmd_user_query, .form_id = 1749, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_enqcmd_user.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_enqcmd_user.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_enqcmd_user.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7f, 0x08, 0xf8, 0x00}, 6));

        BusterX86MetadataPhysicalQuery apx_invpcid_query = x86_64_metadata_test_physical_query(
            S8("INVPCID"), apx_memory_operands, BUSTER_ARRAY_LENGTH(apx_memory_operands),
            (BusterX86MetadataPhysicalAttributes){0}, apx_invpcid_features, BUSTER_ARRAY_LENGTH(apx_invpcid_features));
        apx_invpcid_query.include_privileged = true;
        BusterX86MetadataEmitResult apx_invpcid = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = apx_invpcid_query, .form_id = 1848, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_invpcid.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_invpcid.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_invpcid.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7e, 0x08, 0xf2, 0x00}, 6));

        BusterX86MetadataEmitResult apx_invept = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = (BusterX86MetadataPhysicalQuery){
                .mnemonic = S8("INVEPT"), .operands = apx_memory_operands, .operand_count = BUSTER_ARRAY_LENGTH(apx_memory_operands),
                .features = {.names = apx_vmx_features, .count = BUSTER_ARRAY_LENGTH(apx_vmx_features)},
                .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64, .include_privileged = true},
            .form_id = 1847, .output = output, .output_capacity = sizeof(output), .relocations = relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_invept.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_invept.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_invept.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7e, 0x08, 0xf0, 0x00}, 6));
        BusterX86MetadataEmitResult apx_invvpid = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = (BusterX86MetadataPhysicalQuery){
                .mnemonic = S8("INVVPID"), .operands = apx_memory_operands, .operand_count = BUSTER_ARRAY_LENGTH(apx_memory_operands),
                .features = {.names = apx_vmx_features, .count = BUSTER_ARRAY_LENGTH(apx_vmx_features)},
                .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64, .include_privileged = true},
            .form_id = 1849, .output = output, .output_capacity = sizeof(output), .relocations = relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_invvpid.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_invvpid.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_invvpid.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7e, 0x08, 0xf1, 0x00}, 6));

        BusterX86MetadataPhysicalOperand rdmsr_apx_operands[] = {gpr16, x86_64_metadata_test_physical_imm(0x1234, 32)};
        BusterX86MetadataPhysicalOperand wrmsrns_apx_operands[] = {x86_64_metadata_test_physical_imm(0x1234, 32), gpr16};
        BusterX86MetadataPhysicalQuery rdmsr_apx_query = x86_64_metadata_test_physical_query(
            S8("RDMSR"), rdmsr_apx_operands, BUSTER_ARRAY_LENGTH(rdmsr_apx_operands), (BusterX86MetadataPhysicalAttributes){0},
            apx_msr_imm_features, BUSTER_ARRAY_LENGTH(apx_msr_imm_features));
        rdmsr_apx_query.include_privileged = true;
        BusterX86MetadataEmitResult rdmsr_apx = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = rdmsr_apx_query, .form_id = 2932, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, rdmsr_apx.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && rdmsr_apx.byte_count == 10 &&
                                   x86_64_metadata_test_bytes_equal(output, rdmsr_apx.byte_count,
                                                                     (u8 const[]){0x62, 0xff, 0x7f, 0x08, 0xf6, 0xc0, 0x34, 0x12, 0x00, 0x00}, 10));
        BusterX86MetadataPhysicalQuery wrmsrns_apx_query = rdmsr_apx_query;
        wrmsrns_apx_query.mnemonic = S8("WRMSRNS");
        wrmsrns_apx_query.operands = wrmsrns_apx_operands;
        BusterX86MetadataEmitResult wrmsrns_apx = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wrmsrns_apx_query, .form_id = 2933, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, wrmsrns_apx.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && wrmsrns_apx.byte_count == 10 &&
                                   x86_64_metadata_test_bytes_equal(output, wrmsrns_apx.byte_count,
                                                                     (u8 const[]){0x62, 0xff, 0x7e, 0x08, 0xf6, 0xc0, 0x34, 0x12, 0x00, 0x00}, 10));

        BusterX86MetadataPhysicalQuery apx_movdir64b_query = apx_enqcmd_query;
        apx_movdir64b_query.mnemonic = S8("MOVDIR64B");
        apx_movdir64b_query.features.names = apx_movdir64b_features;
        apx_movdir64b_query.features.count = BUSTER_ARRAY_LENGTH(apx_movdir64b_features);
        apx_movdir64b_query.include_privileged = false;
        BusterX86MetadataEmitResult apx_movdir64b = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = apx_movdir64b_query, .form_id = 1886, .output = output, .output_capacity = sizeof(output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, apx_movdir64b.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && apx_movdir64b.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(output, apx_movdir64b.byte_count,
                                                                     (u8 const[]){0x62, 0xe4, 0x7d, 0x08, 0xf8, 0x00}, 6));

        BusterX86MetadataPhysicalQuery wrmsr_query = x86_64_metadata_test_physical_query(
            S8("WRMSR"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, empty_features, 0);
        wrmsr_query.include_privileged = true;
        BusterX86MetadataPhysicalQuery rdmsr_query = wrmsr_query;
        rdmsr_query.mnemonic = S8("RDMSR");
        u8 wrmsr_output[32] = {0};
        u8 rdmsr_output[32] = {0};
        BusterX86MetadataEmitResult wrmsr = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = wrmsr_query, .form_id = 10129, .output = wrmsr_output, .output_capacity = sizeof(wrmsr_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BusterX86MetadataEmitResult rdmsr = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = rdmsr_query, .form_id = 10131, .output = rdmsr_output, .output_capacity = sizeof(rdmsr_output),
            .relocations = relocations, .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations)});
        BUSTER_TEST(arguments, wrmsr.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && wrmsr.byte_count == 2 &&
                                   x86_64_metadata_test_bytes_equal(wrmsr_output, wrmsr.byte_count, (u8 const[]){0x0f, 0x30}, 2));
        BUSTER_TEST(arguments, rdmsr.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && rdmsr.byte_count == 2 &&
                                   x86_64_metadata_test_bytes_equal(rdmsr_output, rdmsr.byte_count, (u8 const[]){0x0f, 0x32}, 2));
    }

    {
        // Direct MPXMODE coverage uses the XED/manual byte oracle because
        // both GNU as and LLVM MC reject MPX mnemonics on this host.  Each
        // semantic class is exercised: AGEN memory, BND/GPR register forms,
        // BNDMOV's 66/address-size controls, fixed MOD=0/1/2 BNDLDX/STX
        // shapes, and all seven BASE NOP aliases.
        String8 wildcard[1] = {S8("*")};
        String8 no_features[1] = {S8("sse2")};
        BusterX86MetadataPhysicalOperand bnd0 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_BND, 0, 128);
        BusterX86MetadataPhysicalOperand bnd1 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_BND, 1, 128);
        BusterX86MetadataPhysicalOperand gpr0 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataPhysicalOperand gpr1 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64);
        BusterX86MetadataPhysicalOperand bnd_memory = x86_64_metadata_test_physical_mem_base(0, 64, 0);
        BusterX86MetadataPhysicalOperand bnd_operands[] = {bnd0, bnd_memory};
        BusterX86MetadataPhysicalOperand bnd_store_operands[] = {bnd_memory, bnd0};
        BusterX86MetadataPhysicalOperand bnd_register_operands[] = {bnd0, gpr0};
        BusterX86MetadataPhysicalOperand bnd_pair_operands[] = {bnd0, bnd1};
        BusterX86MetadataPhysicalOperand nop_register_operands[] = {gpr0, gpr1};
        BusterX86MetadataPhysicalOperand nop_memory_operands[] = {gpr0, bnd_memory};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BNDMK"), 8781, bnd_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0xf3, 0x0f, 0x1b, 0x00}, 4));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BNDCL"), 8785, bnd_register_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0xf3, 0x48, 0x0f, 0x1a, 0xc0}, 5));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BNDMOV"), 8795, bnd_pair_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0x66, 0x0f, 0x1a, 0xc1}, 4));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BNDMOV"), 8798, bnd_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0x66, 0x0f, 0x1a, 0x00}, 4));

        BusterX86MetadataPhysicalOperand bnd_memory32 = x86_64_metadata_test_physical_mem_base(0, 32, 0);
        bnd_memory32.memory.address_size = 32;
        bnd_memory32.memory.base.width = 32;
        BusterX86MetadataPhysicalOperand bnd_mode32_operands[] = {bnd0, bnd_memory32};
        BusterX86MetadataPhysicalQuery bnd_mode32_query = x86_64_metadata_test_physical_query(
            S8("BNDMOV"), bnd_mode32_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1);
        bnd_mode32_query.address_size = 32;
        u8 bnd_mode16_output[8] = {0};
        u8 bnd_mode32_output[8] = {0};
        bnd_mode32_query.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_16;
        BusterX86MetadataEmitResult bnd_mode16 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = bnd_mode32_query, .form_id = 8796, .output = bnd_mode16_output,
            .output_capacity = BUSTER_ARRAY_LENGTH(bnd_mode16_output)});
        bnd_mode32_query.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_32;
        BusterX86MetadataEmitResult bnd_mode32 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = bnd_mode32_query, .form_id = 8797, .output = bnd_mode32_output,
            .output_capacity = BUSTER_ARRAY_LENGTH(bnd_mode32_output)});
        BUSTER_TEST(arguments, bnd_mode16.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && bnd_mode16.byte_count == 5 &&
                                   x86_64_metadata_test_bytes_equal(bnd_mode16_output, bnd_mode16.byte_count,
                                                                     (u8 const[]){0x67, 0x66, 0x0f, 0x1a, 0x00}, 5));
        BUSTER_TEST(arguments, bnd_mode32.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && bnd_mode32.byte_count == 5 &&
                                   x86_64_metadata_test_bytes_equal(bnd_mode32_output, bnd_mode32.byte_count,
                                                                     (u8 const[]){0x67, 0x66, 0x0f, 0x1a, 0x00}, 5));

        // The EA32 BNDMOV rows are declared MODE16/MODE32, not aliases for
        // the long-mode rows.  Their bytes can be inspected with the typed
        // mode or with ANY+include_not64, while mode64, ANY without the
        // policy opt-in, and include_not64 attached to mode64 all reject.
        static u32 const bnd_mode_ids[] = {8796, 8797, 8800, 8801};
        static u8 const bnd_mode_values[] = {
            BUSTER_X86_METADATA_EXECUTION_MODE_16, BUSTER_X86_METADATA_EXECUTION_MODE_32,
            BUSTER_X86_METADATA_EXECUTION_MODE_16, BUSTER_X86_METADATA_EXECUTION_MODE_32,
        };
        bool bnd_mode_contract = true;
        for (u32 mode_index = 0; mode_index < BUSTER_ARRAY_LENGTH(bnd_mode_ids); mode_index += 1)
        {
            bool load = bnd_mode_ids[mode_index] < 8800;
            BusterX86MetadataPhysicalOperand mode_operands[2] = {0};
            mode_operands[0] = load ? bnd0 : bnd_memory32;
            mode_operands[1] = load ? bnd_memory32 : bnd0;
            BusterX86MetadataPhysicalQuery mode_query = x86_64_metadata_test_physical_query(
                S8("BNDMOV"), mode_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1);
            mode_query.address_size = 32;
            mode_query.execution_mode = bnd_mode_values[mode_index];
            u8 declared_output[8] = {0};
            BusterX86MetadataEmitResult declared = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = mode_query, .form_id = bnd_mode_ids[mode_index], .output = declared_output,
                .output_capacity = BUSTER_ARRAY_LENGTH(declared_output)});
            u8 expected_opcode = load ? 0x1a : 0x1b;
            bnd_mode_contract &= declared.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && declared.byte_count == 5 &&
                                 x86_64_metadata_test_bytes_equal(
                                     declared_output, declared.byte_count, (u8 const[]){0x67, 0x66, 0x0f, expected_opcode, 0x00}, 5);

            mode_query.execution_mode = bnd_mode_values[mode_index] == BUSTER_X86_METADATA_EXECUTION_MODE_16
                                            ? BUSTER_X86_METADATA_EXECUTION_MODE_32
                                            : BUSTER_X86_METADATA_EXECUTION_MODE_16;
            BusterX86MetadataEmitResult wrong_mode = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = mode_query, .form_id = bnd_mode_ids[mode_index], .output = declared_output,
                .output_capacity = BUSTER_ARRAY_LENGTH(declared_output)});
            bnd_mode_contract &= wrong_mode.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;

            mode_query.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64;
            mode_query.include_not64 = false;
            BusterX86MetadataEmitResult mode64 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = mode_query, .form_id = bnd_mode_ids[mode_index], .output = declared_output,
                .output_capacity = BUSTER_ARRAY_LENGTH(declared_output)});
            mode_query.include_not64 = true;
            BusterX86MetadataEmitResult mode64_with_opt_in = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = mode_query, .form_id = bnd_mode_ids[mode_index], .output = declared_output,
                .output_capacity = BUSTER_ARRAY_LENGTH(declared_output)});
            bnd_mode_contract &= mode64.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                 mode64_with_opt_in.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE;

            mode_query.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_ANY;
            mode_query.include_not64 = false;
            BusterX86MetadataEmitResult any_without_opt_in = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = mode_query, .form_id = bnd_mode_ids[mode_index], .output = declared_output,
                .output_capacity = BUSTER_ARRAY_LENGTH(declared_output)});
            mode_query.include_not64 = true;
            u8 inspected_output[8] = {0};
            BusterX86MetadataEmitResult any_with_opt_in = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = mode_query, .form_id = bnd_mode_ids[mode_index], .output = inspected_output,
                .output_capacity = BUSTER_ARRAY_LENGTH(inspected_output)});
            bnd_mode_contract &= any_without_opt_in.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                 any_with_opt_in.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && any_with_opt_in.byte_count == 5 &&
                                 x86_64_metadata_test_bytes_equal(
                                     inspected_output, any_with_opt_in.byte_count, (u8 const[]){0x67, 0x66, 0x0f, expected_opcode, 0x00}, 5);
        }
        BUSTER_TEST(arguments, bnd_mode_contract);

        BusterX86MetadataPhysicalQuery mode64_load_query = x86_64_metadata_test_physical_query(
            S8("BNDMOV"), bnd_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1);
        BusterX86MetadataPhysicalQuery mode64_store_query = x86_64_metadata_test_physical_query(
            S8("BNDMOV"), bnd_store_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1);
        BusterX86MetadataSelectResult mode64_load_selection = buster_x86_metadata_select_form(mode64_load_query);
        BusterX86MetadataSelectResult mode64_store_selection = buster_x86_metadata_select_form(mode64_store_query);
        BUSTER_TEST(arguments, mode64_load_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   mode64_load_selection.form_id == 8798 && mode64_store_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   mode64_store_selection.form_id == 8802);
        BusterX86MetadataPhysicalQuery mode32_load_query = x86_64_metadata_test_physical_query(
            S8("BNDMOV"), bnd_mode32_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1);
        mode32_load_query.address_size = 32;
        BusterX86MetadataSelectResult mode32_as_64 = buster_x86_metadata_select_form(mode32_load_query);
        mode32_load_query.include_not64 = true;
        BusterX86MetadataSelectResult mode32_as_64_with_opt_in = buster_x86_metadata_select_form(mode32_load_query);
        mode32_load_query.execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_ANY;
        mode32_load_query.include_not64 = false;
        BusterX86MetadataSelectResult mode32_as_any = buster_x86_metadata_select_form(mode32_load_query);
        // Selection must not expose a non-64 row through the default mode64
        // query (even when include_not64 is set) or through ANY without its
        // inspection opt-in.  The direct form checks above pin the precise
        // feature/mode diagnostic; selection may stop earlier with a more
        // specific operand/addressing diagnostic as it filters candidates.
        BUSTER_TEST(arguments, mode32_as_64.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   mode32_as_64_with_opt_in.status != BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   mode32_as_any.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);

        BusterX86MetadataPhysicalOperand bnd_ldx_operands[] = {bnd0, bnd_memory};
        BusterX86MetadataPhysicalQuery bnd_ldx_query = x86_64_metadata_test_physical_query(
            S8("BNDLDX"), bnd_ldx_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1);
        u8 bnd_ldx_mod0_output[16] = {0};
        u8 bnd_ldx_mod1_output[16] = {0};
        u8 bnd_ldx_mod2_output[16] = {0};
        BusterX86MetadataEmitResult bnd_ldx_mod0 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = bnd_ldx_query, .form_id = 8804, .output = bnd_ldx_mod0_output,
            .output_capacity = BUSTER_ARRAY_LENGTH(bnd_ldx_mod0_output)});
        bnd_ldx_operands[1].memory.has_displacement = true;
        BusterX86MetadataEmitResult bnd_ldx_mod1 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = bnd_ldx_query, .form_id = 8805, .output = bnd_ldx_mod1_output,
            .output_capacity = BUSTER_ARRAY_LENGTH(bnd_ldx_mod1_output)});
        bnd_ldx_operands[1].memory.displacement = 0x100;
        BusterX86MetadataEmitResult bnd_ldx_mod2 = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = bnd_ldx_query, .form_id = 8806, .output = bnd_ldx_mod2_output,
            .output_capacity = BUSTER_ARRAY_LENGTH(bnd_ldx_mod2_output)});
        BUSTER_TEST(arguments, bnd_ldx_mod0.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && bnd_ldx_mod0.byte_count == 3 &&
                                   x86_64_metadata_test_bytes_equal(bnd_ldx_mod0_output, bnd_ldx_mod0.byte_count,
                                                                     (u8 const[]){0x0f, 0x1a, 0x00}, 3));
        BUSTER_TEST(arguments, bnd_ldx_mod1.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && bnd_ldx_mod1.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(bnd_ldx_mod1_output, bnd_ldx_mod1.byte_count,
                                                                     (u8 const[]){0x0f, 0x1a, 0x40, 0x00}, 4));
        BUSTER_TEST(arguments, bnd_ldx_mod2.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && bnd_ldx_mod2.byte_count == 7 &&
                                   x86_64_metadata_test_bytes_equal(bnd_ldx_mod2_output, bnd_ldx_mod2.byte_count,
                                                                     (u8 const[]){0x0f, 0x1a, 0x80, 0x00, 0x01, 0x00, 0x00}, 7));

        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BNDSTX"), 8808, bnd_store_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0x0f, 0x1b, 0x00}, 3));
        bnd_ldx_operands[1].memory.displacement = 0;
        BusterX86MetadataPhysicalOperand bnd_store_mod_operands[] = {bnd_memory, bnd0};
        bnd_store_mod_operands[0].memory.has_displacement = true;
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BNDSTX"), 8809, bnd_store_mod_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0x0f, 0x1b, 0x40, 0x00}, 4));
        bnd_ldx_operands[1].memory.displacement = 0x100;
        bnd_store_mod_operands[0].memory.displacement = 0x100;
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BNDSTX"), 8810, bnd_store_mod_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0x0f, 0x1b, 0x80, 0x00, 0x01, 0x00, 0x00}, 7));

        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("NOP"), 8811, nop_register_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0x48, 0x0f, 0x1a, 0xc8}, 4));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("NOP"), 8812, nop_register_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0x48, 0x0f, 0x1b, 0xc8}, 4));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("NOP"), 8813, nop_register_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
                                                                 (u8 const[]){0xf3, 0x48, 0x0f, 0x1b, 0xc8}, 5));
        BusterX86MetadataEmitResult mpxmode_zero_reg_load = x86_64_metadata_test_emit_form(
            S8("NOP"), 8814, nop_register_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
            (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult mpxmode_zero_reg_store = x86_64_metadata_test_emit_form(
            S8("NOP"), 8815, nop_register_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
            (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult mpxmode_zero_mem_load = x86_64_metadata_test_emit_form(
            S8("NOP"), 8816, nop_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
            (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult mpxmode_zero_mem_store = x86_64_metadata_test_emit_form(
            S8("NOP"), 8817, nop_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1,
            (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, mpxmode_zero_reg_load.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA);
        BUSTER_TEST(arguments, mpxmode_zero_reg_store.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA);
        BUSTER_TEST(arguments, mpxmode_zero_mem_load.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA);
        BUSTER_TEST(arguments, mpxmode_zero_mem_store.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA);

        BusterX86MetadataPhysicalQuery missing_mpx_query = x86_64_metadata_test_physical_query(
            S8("BNDMK"), bnd_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, no_features, 1);
        BusterX86MetadataEmitResult missing_mpx = x86_64_metadata_test_emit_form(
            S8("BNDMK"), 8781, bnd_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, no_features, 1,
            (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand adjacent_memory = x86_64_metadata_test_physical_mem_base(0, 32, 0);
        adjacent_memory.memory.address_size = 32;
        adjacent_memory.memory.base.width = 32;
        BusterX86MetadataPhysicalOperand adjacent_operands[] = {bnd0, adjacent_memory};
        BusterX86MetadataPhysicalQuery adjacent_query = x86_64_metadata_test_physical_query(
            S8("BNDMK"), adjacent_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1);
        adjacent_query.address_size = 32;
        BusterX86MetadataEmitResult adjacent = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = adjacent_query, .form_id = 8782, .output = (u8[8]){0}, .output_capacity = 8});
        BusterX86MetadataPhysicalOperand mode_mismatch_memory = x86_64_metadata_test_physical_mem_base(0, 64, 0);
        BusterX86MetadataPhysicalOperand mode_mismatch_operands[] = {bnd0, mode_mismatch_memory};
        BusterX86MetadataEmitResult mode_mismatch = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = x86_64_metadata_test_physical_query(S8("BNDMOV"), mode_mismatch_operands, 2,
                                                              (BusterX86MetadataPhysicalAttributes){0}, wildcard, 1),
            .form_id = 8796, .output = (u8[8]){0}, .output_capacity = 8});
        BUSTER_TEST(arguments, missing_mpx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   missing_mpx.required_feature.length != 0 &&
                                   missing_mpx_query.features.count == 1 && adjacent.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   mode_mismatch.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING);
    }

    {
        // Selection keeps arity and physical binding diagnostics distinct.
        // Three operands of the wrong class are not an operand-count error.
        String8 wildcard[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand wrong_count_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        BusterX86MetadataPhysicalOperand wrong_class_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64),
        };
        BusterX86MetadataSelectResult wrong_count = buster_x86_metadata_select_form(
            x86_64_metadata_test_physical_query(S8("VADDPS"), wrong_count_operands, BUSTER_ARRAY_LENGTH(wrong_count_operands),
                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard)));
        BusterX86MetadataSelectResult wrong_class = buster_x86_metadata_select_form(
            x86_64_metadata_test_physical_query(S8("VADDPS"), wrong_class_operands, BUSTER_ARRAY_LENGTH(wrong_class_operands),
                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard)));
        BUSTER_TEST(arguments, wrong_count.status == BUSTER_X86_METADATA_ENCODE_WRONG_OPERAND_COUNT &&
                                   wrong_count.required_feature.length == 0);
        BUSTER_TEST(arguments, wrong_class.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH &&
                                   wrong_class.diagnostic_operand == 0 && wrong_class.required_feature.length == 0);
    }

    {
        // PREFETCHIT0/1 are 64-bit RIP-relative-only forms.  Keep this
        // invariant in the typed selector and emitter, not only in assembly
        // source parsing, so direct metadata callers cannot encode a base
        // register form.
        String8 wildcard[1] = {S8("*")};
        String8 prefetchit_mnemonics[] = {S8("PREFETCHIT0"), S8("PREFETCHIT1")};
        u8 prefetchit_modrm[] = {0x3d, 0x35};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(prefetchit_mnemonics); index += 1)
        {
            BusterX86MetadataPhysicalOperand rip_operand =
                x86_64_metadata_test_physical_mem_rip(S8("prefetchit_direct_external"), 0, 8);
            BusterX86MetadataPhysicalOperand base_operand = x86_64_metadata_test_physical_mem_base(0, 8, 0);
            BusterX86MetadataPhysicalQuery rip_query = x86_64_metadata_test_physical_query(
                prefetchit_mnemonics[index], &rip_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                BUSTER_ARRAY_LENGTH(wildcard));
            BusterX86MetadataPhysicalQuery base_query = rip_query;
            base_query.operands = &base_operand;
            BusterX86MetadataSelectResult rip_selection = buster_x86_metadata_select_form(rip_query);
            BusterX86MetadataSelectResult base_selection = buster_x86_metadata_select_form(base_query);
            u8 output[16] = {0};
            BusterX86MetadataRelocation relocations[1] = {0};
            BusterX86MetadataEmitResult rip_emit = x86_64_metadata_test_emit_form(
                prefetchit_mnemonics[index], rip_selection.form_id, &rip_operand, 1,
                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), output,
                BUSTER_ARRAY_LENGTH(output), relocations, BUSTER_ARRAY_LENGTH(relocations));
            BusterX86MetadataEmitResult base_emit = x86_64_metadata_test_emit_form(
                prefetchit_mnemonics[index], rip_selection.form_id, &base_operand, 1,
                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), output,
                BUSTER_ARRAY_LENGTH(output), relocations, BUSTER_ARRAY_LENGTH(relocations));
            BUSTER_TEST(arguments, rip_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                       rip_selection.selected_byte_count == 7 &&
                                       base_selection.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
            BUSTER_TEST(arguments, rip_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && rip_emit.byte_count == 7 &&
                                       rip_emit.relocation_count == 1 && output[0] == 0x0f && output[1] == 0x18 &&
                                       output[2] == prefetchit_modrm[index] && output[3] == 0 && output[4] == 0 &&
                                       output[5] == 0 && output[6] == 0 && relocations[0].offset == 3 &&
                                       relocations[0].width == 4 && relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 &&
                                       relocations[0].addend == -4);
            BUSTER_TEST(arguments, base_emit.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH &&
                                       base_emit.byte_count == 0 && base_emit.relocation_count == 0);
        }
    }

    {
        // Permanent byte oracles cover the legacy/REX, REX2/APX, VEX, XOP,
        // EVEX, AMX, and system families.  The operands deliberately use the
        // public physical identity rather than metadata signatures.
        String8 wildcard[1] = {S8("*")};
        BusterX86MetadataPhysicalOperand mov_imm64[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_imm((s64)UINT64_C(0x1122334455667788), 64),
        };
        u8 mov_imm64_bytes[] = {0x48, 0xb8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10018, mov_imm64, 2, (BusterX86MetadataPhysicalAttributes){0},
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), mov_imm64_bytes,
                                                                 BUSTER_ARRAY_LENGTH(mov_imm64_bytes)));
        BusterX86MetadataPhysicalOperand mov_imm64_max[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_imm_u64(UINT64_MAX, 64),
        };
        u8 mov_imm64_max_bytes[] = {0x48, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10018, mov_imm64_max, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), mov_imm64_max_bytes,
                                                                 BUSTER_ARRAY_LENGTH(mov_imm64_max_bytes)));
        BusterX86MetadataPhysicalOperand mov_imm16[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 16),
            x86_64_metadata_test_physical_imm(0x1234, 16),
        };
        u8 mov_imm16_bytes[] = {0x66, 0xb8, 0x34, 0x12};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10018, mov_imm16, 2, (BusterX86MetadataPhysicalAttributes){0},
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), mov_imm16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(mov_imm16_bytes)));
        BusterX86MetadataPhysicalOperand mov_imm16_overflow[2] = {
            mov_imm16[0], x86_64_metadata_test_physical_imm(0x10000, 16),
        };
        BusterX86MetadataEmitResult mov_imm16_range = x86_64_metadata_test_emit_form(
            S8("MOV"), 10018, mov_imm16_overflow, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, mov_imm16_range.status == BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE &&
                                   mov_imm16_range.diagnostic_value == 0x10000);

        BusterX86MetadataPhysicalOperand push_r16 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64);
        BusterX86MetadataPhysicalOperand push_r31 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 31, 64);
        u8 push_r16_bytes[] = {0xd5, 0x18, 0x50};
        u8 push_r31_bytes[] = {0xd5, 0x19, 0x57};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSHP"), 2946, &push_r16, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), push_r16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(push_r16_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSHP"), 2946, &push_r31, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), push_r31_bytes,
                                                                 BUSTER_ARRAY_LENGTH(push_r31_bytes)));
        BusterX86MetadataPhysicalQuery pushp_query = x86_64_metadata_test_physical_query(
            S8("PUSHP"), &push_r16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult pushp_selection = buster_x86_metadata_select_form(pushp_query);
        u8 pushp_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult pushp_selected = x86_64_metadata_test_emit_form(
            S8("PUSHP"), pushp_selection.form_id, &push_r16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), pushp_selected_bytes, BUSTER_ARRAY_LENGTH(pushp_selected_bytes), 0, 0);
        BUSTER_TEST(arguments, pushp_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && pushp_selection.form_id == 2946 &&
                                   pushp_selection.selected_byte_count == 3 && pushp_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(pushp_selected_bytes, pushp_selected.byte_count,
                                                                     push_r16_bytes, BUSTER_ARRAY_LENGTH(push_r16_bytes)));
        BusterX86MetadataPhysicalOperand push16 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 16);
        BusterX86MetadataPhysicalQuery push16_query = x86_64_metadata_test_physical_query(
            S8("PUSH"), &push16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult push16_selection = buster_x86_metadata_select_form(push16_query);
        u8 push16_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult push16_selected = x86_64_metadata_test_emit_form(
            S8("PUSH"), push16_selection.form_id, &push16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), push16_selected_bytes, BUSTER_ARRAY_LENGTH(push16_selected_bytes), 0, 0);
        BUSTER_TEST(arguments, push16_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && push16_selection.selected_byte_count == 4 &&
                                   push16_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(push16_selected_bytes, push16_selected.byte_count,
                                                                     (u8[]){0x66, 0xd5, 0x10, 0x50}, 4));
        BusterX86MetadataPhysicalAttributes rex2_nf = {.no_flags = true};
        BusterX86MetadataEmitResult rex2_nf_result = x86_64_metadata_test_emit_form(
            S8("PUSHP"), 2946, &push_r16, 1, rex2_nf, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, rex2_nf_result.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);

        BusterX86MetadataPhysicalOperand rex2_map_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 17, 64),
        };
        String8 apx_features[1] = {S8("apx")};
        BusterX86MetadataPhysicalQuery imul_rex2_query = x86_64_metadata_test_physical_query(
            S8("IMUL"), rex2_map_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, apx_features,
            BUSTER_ARRAY_LENGTH(apx_features));
        BusterX86MetadataPhysicalQuery cmovo_rex2_query = imul_rex2_query;
        cmovo_rex2_query.mnemonic = S8("CMOVO");
        BusterX86MetadataSelectResult imul_rex2_selection = buster_x86_metadata_select_form(imul_rex2_query);
        BusterX86MetadataSelectResult cmovo_rex2_selection = buster_x86_metadata_select_form(cmovo_rex2_query);
        u8 imul_rex2_bytes[8] = {0};
        u8 cmovo_rex2_bytes[8] = {0};
        BusterX86MetadataEmitResult imul_rex2_result = x86_64_metadata_test_emit_form(
            S8("IMUL"), imul_rex2_selection.form_id, rex2_map_operands, 2, (BusterX86MetadataPhysicalAttributes){0},
            apx_features, BUSTER_ARRAY_LENGTH(apx_features), imul_rex2_bytes, BUSTER_ARRAY_LENGTH(imul_rex2_bytes), 0, 0);
        BusterX86MetadataEmitResult cmovo_rex2_result = x86_64_metadata_test_emit_form(
            S8("CMOVO"), cmovo_rex2_selection.form_id, rex2_map_operands, 2, (BusterX86MetadataPhysicalAttributes){0},
            apx_features, BUSTER_ARRAY_LENGTH(apx_features), cmovo_rex2_bytes, BUSTER_ARRAY_LENGTH(cmovo_rex2_bytes), 0, 0);
        BUSTER_TEST(arguments, imul_rex2_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   imul_rex2_selection.selected_byte_count == 4 &&
                                   imul_rex2_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && imul_rex2_result.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(imul_rex2_bytes, imul_rex2_result.byte_count,
                                                                    (u8[]){0xd5, 0xd8, 0xaf, 0xc1}, 4));
        BUSTER_TEST(arguments, cmovo_rex2_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   cmovo_rex2_selection.selected_byte_count == 4 &&
                                   cmovo_rex2_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && cmovo_rex2_result.byte_count == 4 &&
                                   x86_64_metadata_test_bytes_equal(cmovo_rex2_bytes, cmovo_rex2_result.byte_count,
                                                                    (u8[]){0xd5, 0xd8, 0x40, 0xc1}, 4));

        // APX NDD forms whose raw source omits the APX_NDD attribute still
        // encode through the same typed semantic flag inferred from ND=1.
        BusterX86MetadataPhysicalOperand imul_ndd_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 17, 32),
            x86_64_metadata_test_physical_imm(5, 32),
        };
        BusterX86MetadataPhysicalAttributes apx_ndd_attributes = {.apx_flags = BUSTER_X86_METADATA_APX_NDD};
        u8 imul_ndd_bytes[] = {0x62, 0xec, 0x7c, 0x18, 0x6b, 0xc1, 0x05};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("IMUL"), 1789, imul_ndd_operands,
                                                                 BUSTER_ARRAY_LENGTH(imul_ndd_operands), apx_ndd_attributes,
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), imul_ndd_bytes,
                                                                 BUSTER_ARRAY_LENGTH(imul_ndd_bytes)));
        BusterX86MetadataEmitResult imul_ndd_missing_attribute = x86_64_metadata_test_emit_form(
            S8("IMUL"), 1789, imul_ndd_operands, BUSTER_ARRAY_LENGTH(imul_ndd_operands),
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, imul_ndd_missing_attribute.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);

        // Source physical queries omit APX_NDD and suppress the architectural
        // stack/RIP effects of indirect control transfers.  The checked
        // selector must infer only the metadata-proven NDD topology and still
        // emit the canonical bytes for the visible source operands.
        BusterX86MetadataPhysicalOperand call_r16 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64);
        BusterX86MetadataPhysicalOperand jmp_r31 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 31, 64);
        BusterX86MetadataPhysicalQuery call_query = x86_64_metadata_test_physical_query(
            S8("CALL"), &call_r16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataPhysicalQuery jmp_query = x86_64_metadata_test_physical_query(
            S8("JMP"), &jmp_r31, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        call_query.source_semantics = true;
        jmp_query.source_semantics = true;
        call_query.include_privileged = true;
        jmp_query.include_privileged = true;
        BusterX86MetadataSelectResult call_selection = buster_x86_metadata_select_form(call_query);
        BusterX86MetadataSelectResult jmp_selection = buster_x86_metadata_select_form(jmp_query);
        u8 call_output[8] = {0};
        u8 jmp_output[8] = {0};
        BusterX86MetadataEmitResult call_emit = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = call_query, .output = call_output, .output_capacity = BUSTER_ARRAY_LENGTH(call_output)});
        BusterX86MetadataEmitResult jmp_emit = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = jmp_query, .output = jmp_output, .output_capacity = BUSTER_ARRAY_LENGTH(jmp_output)});
        BUSTER_TEST(arguments, call_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && call_selection.form_id == 9484 &&
                                   call_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && call_emit.form_id == 9484 &&
                                   x86_64_metadata_test_bytes_equal(call_output, call_emit.byte_count,
                                                                     (u8[]){0xd5, 0x10, 0xff, 0xd0}, 4));
        BUSTER_TEST(arguments, jmp_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && jmp_selection.form_id == 9488 &&
                                   jmp_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && jmp_emit.form_id == 9488 &&
                                   x86_64_metadata_test_bytes_equal(jmp_output, jmp_emit.byte_count,
                                                                     (u8[]){0xd5, 0x11, 0xff, 0xe7}, 4));

        BusterX86MetadataPhysicalOperand push2_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 17, 64),
        };
        BusterX86MetadataPhysicalOperand pop2_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 24, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 31, 64),
        };
        BusterX86MetadataPhysicalQuery push2_query = x86_64_metadata_test_physical_query(
            S8("PUSH2"), push2_operands, BUSTER_ARRAY_LENGTH(push2_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataPhysicalQuery pop2_query = x86_64_metadata_test_physical_query(
            S8("POP2"), pop2_operands, BUSTER_ARRAY_LENGTH(pop2_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        push2_query.source_semantics = true;
        pop2_query.source_semantics = true;
        push2_query.include_privileged = true;
        pop2_query.include_privileged = true;
        BusterX86MetadataSelectResult push2_selection = buster_x86_metadata_select_form(push2_query);
        BusterX86MetadataSelectResult pop2_selection = buster_x86_metadata_select_form(pop2_query);
        u8 push2_output[8] = {0};
        u8 pop2_output[8] = {0};
        BusterX86MetadataEmitResult push2_emit = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = push2_query, .output = push2_output, .output_capacity = BUSTER_ARRAY_LENGTH(push2_output)});
        BusterX86MetadataEmitResult pop2_emit = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = pop2_query, .output = pop2_output, .output_capacity = BUSTER_ARRAY_LENGTH(pop2_output)});
        BUSTER_TEST(arguments, push2_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && push2_selection.form_id == 2049 &&
                                   push2_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && push2_emit.form_id == 2049 &&
                                   x86_64_metadata_test_bytes_equal(push2_output, push2_emit.byte_count,
                                                                     (u8[]){0x62, 0xfc, 0x7c, 0x10, 0xff, 0xf1}, 6));
        BUSTER_TEST(arguments, pop2_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && pop2_selection.form_id == 2039 &&
                                   pop2_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && pop2_emit.form_id == 2039 &&
                                   x86_64_metadata_test_bytes_equal(pop2_output, pop2_emit.byte_count,
                                                                     (u8[]){0x62, 0xdc, 0x3c, 0x10, 0x8f, 0xc7}, 6));

        BusterX86MetadataPhysicalOperand add3_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 17, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 18, 32),
        };
        BusterX86MetadataPhysicalQuery add3_query = x86_64_metadata_test_physical_query(
            S8("ADD"), add3_operands, BUSTER_ARRAY_LENGTH(add3_operands), (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        add3_query.source_semantics = true;
        add3_query.include_privileged = true;
        BusterX86MetadataSelectResult add3_selection = buster_x86_metadata_select_form(add3_query);
        u8 add3_output[8] = {0};
        BusterX86MetadataEmitResult add3_emit = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = add3_query, .output = add3_output, .output_capacity = BUSTER_ARRAY_LENGTH(add3_output)});
        BUSTER_TEST(arguments, add3_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && add3_selection.form_id == 567 &&
                                   add3_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && add3_emit.form_id == 567 &&
                                   x86_64_metadata_test_bytes_equal(add3_output, add3_emit.byte_count,
                                                                     (u8[]){0x62, 0xec, 0x7c, 0x10, 0x01, 0xd1}, 6));

        BusterX86MetadataPhysicalOperand add_memory_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 32),
            x86_64_metadata_test_physical_mem_base(18, 32, 0),
            x86_64_metadata_test_physical_imm(5, 8),
        };
        BusterX86MetadataPhysicalQuery add_memory_query = x86_64_metadata_test_physical_query(
            S8("ADD"), add_memory_operands, BUSTER_ARRAY_LENGTH(add_memory_operands), (BusterX86MetadataPhysicalAttributes){0},
            wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        add_memory_query.source_semantics = true;
        add_memory_query.include_privileged = true;
        BusterX86MetadataSelectResult add_memory_selection = buster_x86_metadata_select_form(add_memory_query);
        u8 add_memory_output[8] = {0};
        BusterX86MetadataEmitResult add_memory_emit = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = add_memory_query, .output = add_memory_output,
                                           .output_capacity = BUSTER_ARRAY_LENGTH(add_memory_output)});
        BUSTER_TEST(arguments, add_memory_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && add_memory_selection.form_id == 552 &&
                                   add_memory_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && add_memory_emit.form_id == 552 &&
                                   x86_64_metadata_test_bytes_equal(add_memory_output, add_memory_emit.byte_count,
                                                                     (u8[]){0x62, 0xfc, 0x7c, 0x10, 0x83, 0x02, 0x05}, 7));

        BusterX86MetadataPhysicalOperand setb_ndd_reg =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 8);
        u8 setb_ndd_reg_bytes[] = {0x62, 0xfc, 0x7c, 0x18, 0x42, 0xc0};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("SETB"), 2393, &setb_ndd_reg, 1, apx_ndd_attributes,
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), setb_ndd_reg_bytes,
                                                                 BUSTER_ARRAY_LENGTH(setb_ndd_reg_bytes)));
        BusterX86MetadataPhysicalOperand setb_ndd_mem = x86_64_metadata_test_physical_mem_base(0, 8, 0);
        u8 setb_ndd_mem_bytes[] = {0x62, 0xf4, 0x7c, 0x18, 0x42, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("SETB"), 2394, &setb_ndd_mem, 1, apx_ndd_attributes,
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), setb_ndd_mem_bytes,
                                                                 BUSTER_ARRAY_LENGTH(setb_ndd_mem_bytes)));

        BusterX86MetadataPhysicalOperand vaddps_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 2, 128),
        };
        u8 vaddps_bytes[] = {0xc5, 0xf0, 0x58, 0xc2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 3053, vaddps_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vaddps_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vaddps_bytes)));

        BusterX86MetadataPhysicalOperand xchg_ecx =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32);
        BusterX86MetadataPhysicalOperand xchg_rcx =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64);
        BusterX86MetadataPhysicalOperand xchg_r8 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 64);
        u8 xchg_ecx_bytes[] = {0x91};
        u8 xchg_rcx_bytes[] = {0x48, 0x91};
        u8 xchg_r8_bytes[] = {0x49, 0x90};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("XCHG"), 9855, &xchg_ecx, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), xchg_ecx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(xchg_ecx_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("XCHG"), 9855, &xchg_rcx, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), xchg_rcx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(xchg_rcx_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("XCHG"), 9855, &xchg_r8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), xchg_r8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(xchg_r8_bytes)));

        // LEA's source schema calls the address operand AGEN rather than
        // MEM.  The public physical query deliberately models that address
        // with a memory operand, while REMOVE_SEGMENT rejects any segment
        // override before the ordinary ModRM/SIB encoder runs.
        BusterX86MetadataPhysicalOperand lea_rax =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataPhysicalOperand lea_rbx = x86_64_metadata_test_physical_mem_base(3, 64, 0);
        BusterX86MetadataPhysicalOperand lea_operands[2] = {lea_rax, lea_rbx};
        u8 lea_base_bytes[] = {0x48, 0x8d, 0x03};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LEA"), 9849, lea_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), lea_base_bytes,
                                                                 BUSTER_ARRAY_LENGTH(lea_base_bytes)));
        BusterX86MetadataPhysicalOperand lea_complex_memory = x86_64_metadata_test_physical_mem_base(9, 64, 0x20);
        lea_complex_memory.memory.has_index = true;
        lea_complex_memory.memory.index =
            (BusterX86MetadataPhysicalRegister){.index = 1, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        lea_complex_memory.memory.scale = 4;
        lea_complex_memory.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand lea_complex_operands[2] = {xchg_r8, lea_complex_memory};
        u8 lea_complex_bytes[] = {0x4d, 0x8d, 0x44, 0x89, 0x20};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("LEA"), 9849, lea_complex_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), lea_complex_bytes,
                                                                 BUSTER_ARRAY_LENGTH(lea_complex_bytes)));
        BusterX86MetadataPhysicalOperand lea_segment_memory = lea_rbx;
        lea_segment_memory.memory.has_segment = true;
        lea_segment_memory.memory.segment = BUSTER_X86_METADATA_SEGMENT_FS;
        BusterX86MetadataPhysicalOperand lea_segment_operands[2] = {lea_rax, lea_segment_memory};
        BusterX86MetadataEmitResult lea_segment_result = x86_64_metadata_test_emit_form(
            S8("LEA"), 9849, lea_segment_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, lea_segment_result.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING);
        BusterX86MetadataPhysicalQuery lea_query = x86_64_metadata_test_physical_query(
            S8("LEA"), lea_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult lea_selection = buster_x86_metadata_select_form(lea_query);
        u8 lea_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult lea_selected = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = lea_query, .form_id = lea_selection.form_id, .output = lea_selected_bytes,
                                         .output_capacity = BUSTER_ARRAY_LENGTH(lea_selected_bytes)});
        BUSTER_TEST(arguments, lea_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && lea_selection.form_id == 9849 &&
                                   lea_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(lea_selected_bytes, lea_selected.byte_count, lea_base_bytes,
                                                                     BUSTER_ARRAY_LENGTH(lea_base_bytes)));

        u8 nop_bytes[] = {0x90};
        u8 pause_bytes[] = {0xf3, 0x90};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("NOP"), 9852, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), nop_bytes,
                                                                 BUSTER_ARRAY_LENGTH(nop_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PAUSE"), 9853, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), pause_bytes,
                                                                 BUSTER_ARRAY_LENGTH(pause_bytes)));
        BusterX86MetadataEmitResult nop_p4_zero = x86_64_metadata_test_emit_form(
            S8("NOP"), 9854, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
            (u8[8]){0}, 8, 0, 0);
        BusterX86MetadataPhysicalQuery nop_query = x86_64_metadata_test_physical_query(
            S8("NOP"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult nop_selection = buster_x86_metadata_select_form(nop_query);
        BUSTER_TEST(arguments, nop_p4_zero.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   nop_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && nop_selection.form_id == 9852);
        BusterX86MetadataPhysicalOperand plt_nop_memory = x86_64_metadata_test_physical_mem_base(0, 8, 0);
        plt_nop_memory.memory.has_displacement = true;
        plt_nop_memory.memory.source_width = 8;
        BusterX86MetadataPhysicalQuery plt_nop_query = x86_64_metadata_test_physical_query(
            S8("NOP"), &plt_nop_memory, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult plt_nop_selection = buster_x86_metadata_select_form(plt_nop_query);
        u8 plt_nop_bytes[8] = {0};
        BusterX86MetadataEmitResult plt_nop = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = plt_nop_query, .output = plt_nop_bytes, .output_capacity = sizeof(plt_nop_bytes)});
        BUSTER_TEST(arguments, plt_nop_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && plt_nop_selection.form_id == 9587 &&
                                   plt_nop.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && plt_nop.form_id == 9587 && plt_nop.byte_count == 4 &&
                                   plt_nop_bytes[0] == 0x0f && plt_nop_bytes[1] == 0x1f && plt_nop_bytes[2] == 0x40 && plt_nop_bytes[3] == 0x00);

        BusterX86MetadataPhysicalOperand xchg_r8_low =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 32);
        BusterX86MetadataPhysicalOperand xchg_r16_low =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 32);
        BusterX86MetadataPhysicalOperand xchg_rax_low =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32);
        u8 xchg_rexb_bytes[] = {0x41, 0x90};
        u8 xchg_rexb4_bytes[] = {0xd5, 0x10, 0x90};
        String8 xchg_apx_wildcard[1] = {S8("*")};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("XCHG"), 9856, &xchg_r8_low, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), xchg_rexb_bytes,
                                                                 BUSTER_ARRAY_LENGTH(xchg_rexb_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("XCHG"), 9857, &xchg_r16_low, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, xchg_apx_wildcard,
                                                                 BUSTER_ARRAY_LENGTH(xchg_apx_wildcard), xchg_rexb4_bytes,
                                                                 BUSTER_ARRAY_LENGTH(xchg_rexb4_bytes)));
        BusterX86MetadataEmitResult xchg_rexb_low_rejected = x86_64_metadata_test_emit_form(
            S8("XCHG"), 9856, &xchg_rax_low, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[8]){0}, 8, 0, 0);
        BusterX86MetadataEmitResult xchg_rexb4_low_rejected = x86_64_metadata_test_emit_form(
            S8("XCHG"), 9857, &xchg_r8_low, 1, (BusterX86MetadataPhysicalAttributes){0}, xchg_apx_wildcard,
            BUSTER_ARRAY_LENGTH(xchg_apx_wildcard), (u8[8]){0}, 8, 0, 0);
        BusterX86MetadataEmitResult xchg_rexb4_high_rejected = x86_64_metadata_test_emit_form(
            S8("XCHG"), 9857,
            &(BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                .width = 32,
                .reg = {.index = 24, .width = 32, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
            },
            1, (BusterX86MetadataPhysicalAttributes){0}, xchg_apx_wildcard, BUSTER_ARRAY_LENGTH(xchg_apx_wildcard),
            (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, xchg_rexb_low_rejected.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION &&
                                   xchg_rexb4_low_rejected.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION &&
                                   xchg_rexb4_high_rejected.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);
        BusterX86MetadataPhysicalOperand xchg_rax =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataEmitResult xchg_rax_result = x86_64_metadata_test_emit_form(
            S8("XCHG"), 9855, &xchg_rax, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, xchg_rax_result.status == BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING);

        BusterX86MetadataPhysicalOperand xchg_rex2_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 17, 64),
        };
        String8 xchg_apx_features[1] = {S8("apx")};
        BusterX86MetadataPhysicalQuery xchg_rex2_query = x86_64_metadata_test_physical_query(
            S8("XCHG"), xchg_rex2_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, xchg_apx_features,
            BUSTER_ARRAY_LENGTH(xchg_apx_features));
        BusterX86MetadataSelectResult xchg_rex2_selection = buster_x86_metadata_select_form(xchg_rex2_query);
        u8 xchg_rex2_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult xchg_rex2_selected = x86_64_metadata_test_emit_form(
            S8("XCHG"), xchg_rex2_selection.form_id, xchg_rex2_operands, 2,
            (BusterX86MetadataPhysicalAttributes){0}, xchg_apx_features, BUSTER_ARRAY_LENGTH(xchg_apx_features),
            xchg_rex2_selected_bytes, BUSTER_ARRAY_LENGTH(xchg_rex2_selected_bytes), 0, 0);
        BUSTER_TEST(arguments, xchg_rex2_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   xchg_rex2_selection.selected_byte_count == 4 &&
                                   xchg_rex2_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(xchg_rex2_selected_bytes, xchg_rex2_selected.byte_count,
                                                                    (u8[]){0xd5, 0x58, 0x87, 0xc1}, 4));

        BusterX86MetadataPhysicalOperand bswap_eax =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32);
        BusterX86MetadataPhysicalOperand bswap_rax =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        BusterX86MetadataPhysicalOperand bswap_r8d =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 32);
        BusterX86MetadataPhysicalOperand bswap_r8 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 64);
        BusterX86MetadataPhysicalOperand bswap_r16 =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 64);
        u8 bswap_eax_bytes[] = {0x0f, 0xc8};
        u8 bswap_rax_bytes[] = {0x48, 0x0f, 0xc8};
        u8 bswap_r8d_bytes[] = {0x41, 0x0f, 0xc8};
        u8 bswap_r8_bytes[] = {0x49, 0x0f, 0xc8};
        u8 bswap_r16_bytes[] = {0xd5, 0x98, 0xc8};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSWAP"), 10688, &bswap_eax, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bswap_eax_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bswap_eax_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSWAP"), 10688, &bswap_rax, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bswap_rax_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bswap_rax_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSWAP"), 10688, &bswap_r8d, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bswap_r8d_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bswap_r8d_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSWAP"), 10688, &bswap_r8, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bswap_r8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bswap_r8_bytes)));
        BusterX86MetadataPhysicalQuery bswap_r16_query = x86_64_metadata_test_physical_query(
            S8("BSWAP"), &bswap_r16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult bswap_r16_selection = buster_x86_metadata_select_form(bswap_r16_query);
        u8 bswap_r16_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult bswap_r16_selected = x86_64_metadata_test_emit_form(
            S8("BSWAP"), bswap_r16_selection.form_id, &bswap_r16, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), bswap_r16_selected_bytes, BUSTER_ARRAY_LENGTH(bswap_r16_selected_bytes), 0, 0);
        BUSTER_TEST(arguments, bswap_r16_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   bswap_r16_selection.selected_byte_count == 3 &&
                                   bswap_r16_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(bswap_r16_selected_bytes, bswap_r16_selected.byte_count,
                                                                    bswap_r16_bytes, BUSTER_ARRAY_LENGTH(bswap_r16_bytes)));

        u8 ud0_short_bytes[] = {0x0f, 0xff};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("UD0"), 10412, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ud0_short_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ud0_short_bytes)));

        BusterX86MetadataPhysicalOperand bextr_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_mem_base(3, 64, 0),
            x86_64_metadata_test_physical_imm(5, 32),
        };
        u8 bextr_bytes[] = {0x8f, 0xea, 0xf8, 0x10, 0x03, 0x05, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BEXTR_XOP"), 326, bextr_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bextr_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bextr_bytes)));

        {
            // SSE4a EXTRQ/INSERTQ carry two ordered UIMM8 fields.  Keep the
            // normalized metadata selection and direct emitter on the same
            // table-driven path as every other immediate-bearing form.
            BusterX86MetadataPhysicalOperand extrq_operands[3] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
                x86_64_metadata_test_physical_imm(1, 8),
                x86_64_metadata_test_physical_imm(2, 8),
            };
            BusterX86MetadataPhysicalOperand insertq_operands[4] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
                x86_64_metadata_test_physical_imm(1, 8),
                x86_64_metadata_test_physical_imm(2, 8),
            };
            u8 extrq_bytes[] = {0x66, 0x0f, 0x78, 0xc0, 0x01, 0x02};
            u8 insertq_bytes[] = {0xf2, 0x0f, 0x78, 0xc1, 0x01, 0x02};
            BusterX86MetadataPhysicalQuery extrq_query = x86_64_metadata_test_physical_query(
                S8("EXTRQ"), extrq_operands, BUSTER_ARRAY_LENGTH(extrq_operands), (BusterX86MetadataPhysicalAttributes){0},
                wildcard, BUSTER_ARRAY_LENGTH(wildcard));
            BusterX86MetadataPhysicalQuery insertq_query = x86_64_metadata_test_physical_query(
                S8("INSERTQ"), insertq_operands, BUSTER_ARRAY_LENGTH(insertq_operands), (BusterX86MetadataPhysicalAttributes){0},
                wildcard, BUSTER_ARRAY_LENGTH(wildcard));
            BusterX86MetadataSelectResult extrq_selection = buster_x86_metadata_select_form(extrq_query);
            BusterX86MetadataSelectResult insertq_selection = buster_x86_metadata_select_form(insertq_query);
            BUSTER_TEST(arguments, extrq_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && extrq_selection.form_id == 437 &&
                                       extrq_selection.selected_byte_count == BUSTER_ARRAY_LENGTH(extrq_bytes));
            BUSTER_TEST(arguments, insertq_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && insertq_selection.form_id == 439 &&
                                       insertq_selection.selected_byte_count == BUSTER_ARRAY_LENGTH(insertq_bytes));
            BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("EXTRQ"), 437, extrq_operands,
                                                                     BUSTER_ARRAY_LENGTH(extrq_operands),
                                                                     (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                     BUSTER_ARRAY_LENGTH(wildcard), extrq_bytes,
                                                                     BUSTER_ARRAY_LENGTH(extrq_bytes)));
            BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("INSERTQ"), 439, insertq_operands,
                                                                     BUSTER_ARRAY_LENGTH(insertq_operands),
                                                                     (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                     BUSTER_ARRAY_LENGTH(wildcard), insertq_bytes,
                                                                     BUSTER_ARRAY_LENGTH(insertq_bytes)));

            BusterX86MetadataPhysicalOperand extrq_lower[3] = {extrq_operands[0],
                                                                x86_64_metadata_test_physical_imm(0, 8),
                                                                x86_64_metadata_test_physical_imm(0, 8)};
            BusterX86MetadataPhysicalOperand extrq_upper[3] = {extrq_operands[0],
                                                                x86_64_metadata_test_physical_imm(255, 8),
                                                                x86_64_metadata_test_physical_imm(255, 8)};
            BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("EXTRQ"), 437, extrq_lower,
                                                                     BUSTER_ARRAY_LENGTH(extrq_lower),
                                                                     (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                     BUSTER_ARRAY_LENGTH(wildcard),
                                                                     (u8 const[]){0x66, 0x0f, 0x78, 0xc0, 0x00, 0x00}, 6));
            BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("EXTRQ"), 437, extrq_upper,
                                                                     BUSTER_ARRAY_LENGTH(extrq_upper),
                                                                     (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                     BUSTER_ARRAY_LENGTH(wildcard),
                                                                     (u8 const[]){0x66, 0x0f, 0x78, 0xc0, 0xff, 0xff}, 6));
            BusterX86MetadataPhysicalOperand extrq_negative[3] = {extrq_operands[0],
                                                                    x86_64_metadata_test_physical_imm(-1, 8),
                                                                    extrq_operands[2]};
            BusterX86MetadataPhysicalOperand extrq_overflow[3] = {extrq_operands[0],
                                                                    extrq_operands[1],
                                                                    x86_64_metadata_test_physical_imm(256, 8)};
            BusterX86MetadataEmitResult extrq_negative_result = x86_64_metadata_test_emit_form(
                S8("EXTRQ"), 437, extrq_negative, BUSTER_ARRAY_LENGTH(extrq_negative),
                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
            BusterX86MetadataEmitResult extrq_overflow_result = x86_64_metadata_test_emit_form(
                S8("EXTRQ"), 437, extrq_overflow, BUSTER_ARRAY_LENGTH(extrq_overflow),
                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
            BUSTER_TEST(arguments, extrq_negative_result.status == BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE &&
                                       extrq_negative_result.diagnostic_value == -1);
            BUSTER_TEST(arguments, extrq_overflow_result.status == BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE &&
                                       extrq_overflow_result.diagnostic_value == 256);

            BusterX86MetadataPhysicalOperand insertq_wrong_count[3] = {insertq_operands[0], insertq_operands[1], insertq_operands[2]};
            BusterX86MetadataPhysicalOperand insertq_wrong_type[4] = {
                insertq_operands[0], x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 64),
                insertq_operands[2], insertq_operands[3],
            };
            BusterX86MetadataPhysicalQuery insertq_count_query = x86_64_metadata_test_physical_query(
                S8("INSERTQ"), insertq_wrong_count, BUSTER_ARRAY_LENGTH(insertq_wrong_count),
                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
            BusterX86MetadataPhysicalQuery insertq_type_query = x86_64_metadata_test_physical_query(
                S8("INSERTQ"), insertq_wrong_type, BUSTER_ARRAY_LENGTH(insertq_wrong_type),
                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
            BusterX86MetadataSelectResult insertq_count_selection = buster_x86_metadata_select_form(insertq_count_query);
            BusterX86MetadataSelectResult insertq_type_selection = buster_x86_metadata_select_form(insertq_type_query);
            BUSTER_TEST(arguments, insertq_count_selection.status == BUSTER_X86_METADATA_ENCODE_WRONG_OPERAND_COUNT);
            BUSTER_TEST(arguments, insertq_type_selection.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH &&
                                       insertq_type_selection.diagnostic_operand == 1);
        }

        {
            // x87 stack-register forms keep ST(0) in ModRM.reg and encode
            // the visible ST(i) operand in ModRM.rm.  These direct metadata
            // oracles cover the representative legacy opcodes and prove
            // that the topology inference is independent of the mnemonic.
            struct
            {
                String8 mnemonic;
                u32 form_id;
                u16 index;
                u8 bytes[2];
            } const x87_cases[] = {
                {S8("FADD"), 9084, 0, {0xd8, 0xc0}},
                {S8("FADD"), 9084, 1, {0xd8, 0xc1}},
                {S8("FCOM"), 9088, 2, {0xd8, 0xd2}},
                {S8("FSTP"), 9102, 2, {0xdd, 0xda}},
                {S8("FLD"), 9122, 3, {0xd9, 0xc3}},
                {S8("FXCH"), 9123, 2, {0xd9, 0xca}},
                {S8("FCMOVB"), 9162, 1, {0xda, 0xc1}},
                {S8("FUCOMI"), 9181, 1, {0xdb, 0xe9}},
                {S8("FADDP"), 9226, 2, {0xde, 0xc2}},
                {S8("FFREE"), 9214, 4, {0xdd, 0xc4}},
                {S8("FUCOMIP"), 9243, 1, {0xdf, 0xe9}},
                {S8("FCOMIP"), 9244, 1, {0xdf, 0xf1}},
            };
            for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(x87_cases); case_index += 1)
            {
                BusterX86MetadataPhysicalOperand x87_operand = x86_64_metadata_test_physical_reg(
                    BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, x87_cases[case_index].index, 80);
                BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(
                                           x87_cases[case_index].mnemonic, x87_cases[case_index].form_id, &x87_operand, 1,
                                           (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
                                           x87_cases[case_index].bytes, BUSTER_ARRAY_LENGTH(x87_cases[case_index].bytes)));
            }

            // Preserve the explicit ST(0) in two-register source spellings:
            // Intel `fmul st(3), st(0)` is the DC destination form, while
            // `fmul st(0), st(3)` remains the D8 form.  The checked selector
            // must retain this direction instead of projecting ST(0) away.
            BusterX86MetadataPhysicalOperand x87_direction_operands[2] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 3, 80),
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 0, 80),
            };
            BusterX86MetadataPhysicalQuery x87_direction_query = x86_64_metadata_test_physical_query(
                S8("FMUL"), x87_direction_operands, BUSTER_ARRAY_LENGTH(x87_direction_operands),
                (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
            x87_direction_query.source_semantics = true;
            BusterX86MetadataSelectResult x87_direction_selection = buster_x86_metadata_select_form(x87_direction_query);
            u8 x87_direction_bytes[8] = {0};
            BusterX86MetadataEmitResult x87_direction_emit = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
                .physical = x87_direction_query,
                .output = x87_direction_bytes,
                .output_capacity = BUSTER_ARRAY_LENGTH(x87_direction_bytes),
            });
            BUSTER_TEST(arguments, x87_direction_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                       x87_direction_selection.form_id == 9191 &&
                                       x87_direction_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                       x87_direction_emit.byte_count == 2 && x87_direction_bytes[0] == 0xdc &&
                                       x87_direction_bytes[1] == 0xcb);
            x87_direction_operands[0].reg.index = 0;
            x87_direction_operands[1].reg.index = 3;
            x87_direction_query.operands = x87_direction_operands;
            BusterX86MetadataSelectResult x87_reverse_direction_selection = buster_x86_metadata_select_form(x87_direction_query);
            x87_direction_emit = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
                .physical = x87_direction_query,
                .output = x87_direction_bytes,
                .output_capacity = BUSTER_ARRAY_LENGTH(x87_direction_bytes),
            });
            BUSTER_TEST(arguments, x87_reverse_direction_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                       x87_reverse_direction_selection.form_id == 9085 &&
                                       x87_direction_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                       x87_direction_emit.byte_count == 2 && x87_direction_bytes[0] == 0xd8 &&
                                       x87_direction_bytes[1] == 0xcb);

            // Real-memory x87 widths are encoded by distinct legacy opcodes,
            // not by REX.W.  The selector must therefore distinguish the
            // m32real/m64real/mem80real rows, and a RIP-relative symbol must
            // retain the ordinary PC32 relocation at the displacement field.
            struct
            {
                u16 width;
                u32 form_id;
                u8 opcode;
                u8 modrm;
            } const x87_memory_cases[] = {
                {32, 9097, 0xd9, 0x05},
                {64, 9196, 0xdd, 0x05},
                {80, 9171, 0xdb, 0x2d},
            };
            bool x87_memory_valid = true;
            for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(x87_memory_cases); case_index += 1)
            {
                BusterX86MetadataPhysicalOperand x87_memory_operand = x86_64_metadata_test_physical_mem_rip(
                    S8("external_x87"), 0, x87_memory_cases[case_index].width);
                BusterX86MetadataPhysicalQuery query = x86_64_metadata_test_physical_query(
                    S8("FLD"), &x87_memory_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                    BUSTER_ARRAY_LENGTH(wildcard));
                u8 output[16] = {0};
                BusterX86MetadataRelocation relocations[2] = {0};
                BusterX86MetadataEmitResult selected = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
                    .physical = query,
                    .output = output,
                    .output_capacity = BUSTER_ARRAY_LENGTH(output),
                    .relocations = relocations,
                    .relocation_capacity = BUSTER_ARRAY_LENGTH(relocations),
                });
                x87_memory_valid &= selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                    selected.form_id == x87_memory_cases[case_index].form_id && selected.byte_count == 6 &&
                                    output[0] == x87_memory_cases[case_index].opcode && output[1] == x87_memory_cases[case_index].modrm &&
                                    selected.relocation_count == 1 && relocations[0].offset == 2 && relocations[0].width == 4 &&
                                    relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 && relocations[0].addend == -4;
            }
            BUSTER_TEST(arguments, x87_memory_valid);

            // FCMOV/FCOMI carry generated ISA-set spellings that are not
            // target feature names.  They are baseline x87 forms, so the
            // ordinary sse2 feature input must authorize both selection and
            // emission without letting an unrelated alias stand in for it.
            struct
            {
                String8 mnemonic;
                u32 form_id;
                u8 bytes[2];
            } const x87_feature_cases[] = {
                {S8("FCMOVB"), 9162, {0xda, 0xc1}},
                {S8("FCMOVE"), 9163, {0xda, 0xc9}},
                {S8("FCMOVBE"), 9164, {0xda, 0xd1}},
                {S8("FCMOVU"), 9165, {0xda, 0xd9}},
                {S8("FCMOVNB"), 9172, {0xdb, 0xc1}},
                {S8("FCMOVNE"), 9173, {0xdb, 0xc9}},
                {S8("FCMOVNBE"), 9174, {0xdb, 0xd1}},
                {S8("FCMOVNU"), 9175, {0xdb, 0xd9}},
                {S8("FUCOMI"), 9181, {0xdb, 0xe9}},
                {S8("FCOMI"), 9182, {0xdb, 0xf1}},
                {S8("FUCOMIP"), 9243, {0xdf, 0xe9}},
                {S8("FCOMIP"), 9244, {0xdf, 0xf1}},
            };
            String8 sse2_features[1] = {S8("sse2")};
            String8 unrelated_features[1] = {S8("sse3")};
            String8 no_features[1] = {0};
            for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(x87_feature_cases); case_index += 1)
            {
                BusterX86MetadataPhysicalOperand x87_operand = x86_64_metadata_test_physical_reg(
                    BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 1, 80);
                BusterX86MetadataPhysicalQuery enabled_query = x86_64_metadata_test_physical_query(
                    x87_feature_cases[case_index].mnemonic, &x87_operand, 1,
                    (BusterX86MetadataPhysicalAttributes){0}, sse2_features, BUSTER_ARRAY_LENGTH(sse2_features));
                BusterX86MetadataPhysicalQuery missing_query = enabled_query;
                missing_query.features.names = no_features;
                missing_query.features.count = 0;
                BusterX86MetadataPhysicalQuery unrelated_query = enabled_query;
                unrelated_query.features.names = unrelated_features;
                unrelated_query.features.count = BUSTER_ARRAY_LENGTH(unrelated_features);
                BusterX86MetadataSelectResult enabled_selection = buster_x86_metadata_select_form(enabled_query);
                BusterX86MetadataSelectResult missing_selection = buster_x86_metadata_select_form(missing_query);
                BusterX86MetadataSelectResult unrelated_selection = buster_x86_metadata_select_form(unrelated_query);
                u8 enabled_output[2] = {0};
                BusterX86MetadataEmitResult enabled_emit = x86_64_metadata_test_emit_form(
                    x87_feature_cases[case_index].mnemonic, x87_feature_cases[case_index].form_id, &x87_operand, 1,
                    (BusterX86MetadataPhysicalAttributes){0}, sse2_features, BUSTER_ARRAY_LENGTH(sse2_features), enabled_output,
                    BUSTER_ARRAY_LENGTH(enabled_output), 0, 0);
                BusterX86MetadataEmitResult missing_emit = x86_64_metadata_test_emit_form(
                    x87_feature_cases[case_index].mnemonic, x87_feature_cases[case_index].form_id, &x87_operand, 1,
                    (BusterX86MetadataPhysicalAttributes){0}, no_features, 0, (u8[2]){0}, 2, 0, 0);
                BusterX86MetadataEmitResult unrelated_emit = x86_64_metadata_test_emit_form(
                    x87_feature_cases[case_index].mnemonic, x87_feature_cases[case_index].form_id, &x87_operand, 1,
                    (BusterX86MetadataPhysicalAttributes){0}, unrelated_features, BUSTER_ARRAY_LENGTH(unrelated_features), (u8[2]){0},
                    2, 0, 0);
                BUSTER_TEST(arguments, enabled_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                           enabled_selection.form_id == x87_feature_cases[case_index].form_id);
                BUSTER_TEST(arguments, enabled_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && enabled_emit.byte_count == 2 &&
                                           x86_64_metadata_test_bytes_equal(enabled_output, enabled_emit.byte_count,
                                                                             x87_feature_cases[case_index].bytes, 2));
                BUSTER_TEST(arguments, missing_selection.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                           missing_emit.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);
                BUSTER_TEST(arguments, unrelated_selection.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                           unrelated_emit.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);
            }

            BusterX86MetadataPhysicalOperand st0 =
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 0, 80);
            BusterX86MetadataPhysicalOperand st8 =
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 8, 80);
            BusterX86MetadataPhysicalOperand wrong_width =
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL, 0, 64);
            BusterX86MetadataPhysicalOperand wrong_class =
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
            BusterX86MetadataPhysicalOperand memory = x86_64_metadata_test_physical_mem_base(0, 80, 0);
            BusterX86MetadataEmitResult st8_result = x86_64_metadata_test_emit_form(
                S8("FADD"), 9084, &st8, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
            BusterX86MetadataEmitResult wrong_width_result = x86_64_metadata_test_emit_form(
                S8("FADD"), 9084, &wrong_width, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
            BusterX86MetadataEmitResult wrong_class_result = x86_64_metadata_test_emit_form(
                S8("FADD"), 9084, &wrong_class, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
            BusterX86MetadataEmitResult memory_result = x86_64_metadata_test_emit_form(
                S8("FADD"), 9084, &memory, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
            BusterX86MetadataEmitResult wrong_count_result = x86_64_metadata_test_emit_form(
                S8("FADD"), 9084, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
            BusterX86MetadataEmitResult wrong_mnemonic_result = x86_64_metadata_test_emit_form(
                S8("FMUL"), 9084, &st0, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
            BUSTER_TEST(arguments, st8_result.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
            BUSTER_TEST(arguments, wrong_width_result.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
            BUSTER_TEST(arguments, wrong_class_result.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
            BUSTER_TEST(arguments, memory_result.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
            BUSTER_TEST(arguments, wrong_count_result.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
            BUSTER_TEST(arguments, wrong_mnemonic_result.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM);

            // The topology rule is SPECIAL-only: a fixed REG ordinary form
            // still binds its non-SPECIAL operand exactly as before.
            BusterX86MetadataPhysicalOperand mov_imm_operands[2] = {
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 3, 8),
                x86_64_metadata_test_physical_imm(0x7f, 8),
            };
            BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(
                                       S8("MOV"), 9531, mov_imm_operands, BUSTER_ARRAY_LENGTH(mov_imm_operands),
                                       (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
                                       (u8 const[]){0xc6, 0xc3, 0x7f}, 3));
        }

        BusterX86MetadataPhysicalOperand vmov_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        u8 vmov_plain_bytes[] = {0x62, 0xf1, 0x7d, 0x08, 0x6f, 0xc1};
        BusterX86MetadataPhysicalAttributes masked = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK, .has_mask_register = true, .mask_register = 1,
        };
        BusterX86MetadataPhysicalAttributes zeroed = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK | BUSTER_X86_METADATA_DECORATOR_ZEROING,
            .has_mask_register = true, .mask_register = 1, .zeroing = true,
        };
        u8 vmov_masked_bytes[] = {0x62, 0xf1, 0x7d, 0x09, 0x6f, 0xc1};
        u8 vmov_zeroed_bytes[] = {0x62, 0xf1, 0x7d, 0x89, 0x6f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vmov_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vmov_plain_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vmov_plain_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vmov_operands, 2, masked, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vmov_masked_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vmov_masked_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vmov_operands, 2, zeroed, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vmov_zeroed_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vmov_zeroed_bytes)));
        BusterX86MetadataPhysicalOperand vmov_explicit_mask_operands[3] = {
            vmov_operands[0], x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 2, 64), vmov_operands[1],
        };
        BusterX86MetadataEmitResult vmov_mask_mismatch = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5583, vmov_explicit_mask_operands, 3, masked, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
            (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, vmov_mask_mismatch.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
        BusterX86MetadataPhysicalAttributes explicit_mask = masked;
        explicit_mask.mask_register = 2;
        u8 vmov_explicit_mask_bytes[] = {0x62, 0xf1, 0x7d, 0x0a, 0x6f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vmov_explicit_mask_operands, 3,
                                                                 explicit_mask, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
                                                                 vmov_explicit_mask_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vmov_explicit_mask_bytes)));
        BusterX86MetadataEmitResult zero_k0 = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5583, vmov_operands, 2,
            (BusterX86MetadataPhysicalAttributes){.decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK | BUSTER_X86_METADATA_DECORATOR_ZEROING,
                                                   .zeroing = true},
            wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, zero_k0.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand round_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        BusterX86MetadataPhysicalAttributes rounding = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING,
            .rounding_mode = BUSTER_X86_METADATA_ROUNDING_DOWN,
        };
        u8 rounding_bytes[] = {0x62, 0xf1, 0xff, 0x38, 0x2f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCOMXSD"), 4276, round_operands, 2, rounding, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), rounding_bytes,
                                                                 BUSTER_ARRAY_LENGTH(rounding_bytes)));
        u8 fixed_round_len128_plain_bytes[] = {0x62, 0xf1, 0xff, 0x08, 0x2f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCOMXSD"), 4275, round_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_len128_plain_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_len128_plain_bytes)));

        BusterX86MetadataPhysicalOperand fixed_round_len512_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 2, 512),
        };
        BusterX86MetadataPhysicalAttributes fixed_round_len512_rounding = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING,
            .rounding_mode = BUSTER_X86_METADATA_ROUNDING_NEAREST,
        };
        u8 fixed_round_len512_plain_bytes[] = {0x62, 0xf5, 0x74, 0x48, 0x58, 0xc2};
        u8 fixed_round_len512_rounding_bytes[] = {0x62, 0xf5, 0x74, 0x18, 0x58, 0xc2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPH"), 4479, fixed_round_len512_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_len512_plain_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_len512_plain_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPH"), 4480, fixed_round_len512_operands, 3,
                                                                 fixed_round_len512_rounding, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_len512_rounding_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_len512_rounding_bytes)));
        BusterX86MetadataPhysicalOperand fixed_round_len512_wrong_width[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 1, 256),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 2, 256),
        };
        BusterX86MetadataEmitResult fixed_round_len512_wrong_width_result = x86_64_metadata_test_emit_form(
            S8("VADDPH"), 4480, fixed_round_len512_wrong_width, 3, fixed_round_len512_rounding, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, fixed_round_len512_wrong_width_result.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);

        // Fixed-round/BCRC=1 EVEX rows carry EVEX.b=1 without a caller
        // visible SAE or rounding decorator.  These four metadata-level
        // oracles cover both AVX10.2 BF16 conversions and the AVX-512 integer
        // conversions. llvm-mc confirms the opcode and non-b prefix fields
        // through the BCRC=0 siblings; XED's fixed-round contract supplies
        // the otherwise caller-inexpressible EVEX.b=1 variant here.
        BusterX86MetadataPhysicalOperand fixed_round_bf16_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
        };
        u8 fixed_round_bf16_ibs_bytes[] = {0x62, 0xf5, 0x7f, 0x58, 0x68, 0xc1};
        u8 fixed_round_bf16_iubs_bytes[] = {0x62, 0xf5, 0x7f, 0x58, 0x6a, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTTBF162IBS"), 4190,
                                                                 fixed_round_bf16_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_bf16_ibs_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_bf16_ibs_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTTBF162IUBS"), 4197,
                                                                 fixed_round_bf16_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_bf16_iubs_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_bf16_iubs_bytes)));

        BusterX86MetadataPhysicalOperand fixed_round_dq2pd_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 1, 256),
        };
        u8 fixed_round_dq2pd_bytes[] = {0x62, 0xf1, 0x7e, 0x58, 0xe6, 0xc1};
        u8 fixed_round_udq2pd_bytes[] = {0x62, 0xf1, 0x7e, 0x58, 0x7a, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTDQ2PD"), 6986,
                                                                 fixed_round_dq2pd_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_dq2pd_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_dq2pd_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTUDQ2PD"), 7122,
                                                                 fixed_round_dq2pd_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fixed_round_udq2pd_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fixed_round_udq2pd_bytes)));

        // BCRC=0 remains an ordinary b=0 EVEX control, and explicit
        // decorators remain rejected by forms that do not advertise them.
        u8 ordinary_bf16_bytes[] = {0x62, 0xf5, 0x7f, 0x48, 0x68, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTTBF162IBS"), 4189,
                                                                 fixed_round_bf16_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ordinary_bf16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ordinary_bf16_bytes)));
        BusterX86MetadataPhysicalAttributes explicit_sae = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_SAE,
            .sae = true,
        };
        BusterX86MetadataPhysicalAttributes explicit_rounding = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING,
            .rounding_mode = BUSTER_X86_METADATA_ROUNDING_DOWN,
        };
        BusterX86MetadataEmitResult fixed_round_sae = x86_64_metadata_test_emit_form(
            S8("VCVTDQ2PD"), 6986, fixed_round_dq2pd_operands, 2, explicit_sae, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult fixed_round_rounding = x86_64_metadata_test_emit_form(
            S8("VCVTDQ2PD"), 6986, fixed_round_dq2pd_operands, 2, explicit_rounding, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand explicit_rounding_form_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
        };
        BusterX86MetadataEmitResult missing_rounding_control = x86_64_metadata_test_emit_form(
            S8("VCVTDQ2PS"), 6989, explicit_rounding_form_operands, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, fixed_round_sae.status == BUSTER_X86_METADATA_ENCODE_DECORATOR &&
                                   fixed_round_rounding.status == BUSTER_X86_METADATA_ENCODE_DECORATOR &&
                                   missing_rounding_control.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);

        BusterX86MetadataPhysicalOperand apx_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 3, 64),
        };
        u8 apx_nf0_bytes[] = {0x62, 0xf4, 0xfc, 0x08, 0x01, 0xd8};
        u8 apx_nf1_bytes[] = {0x62, 0xf4, 0xfc, 0x0c, 0x01, 0xd8};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADD"), 559, apx_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), apx_nf0_bytes,
                                                                 BUSTER_ARRAY_LENGTH(apx_nf0_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADD"), 561, apx_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){.no_flags = true}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), apx_nf1_bytes,
                                                                 BUSTER_ARRAY_LENGTH(apx_nf1_bytes)));
        BusterX86MetadataPhysicalQuery no_flags_query = x86_64_metadata_test_physical_query(
            S8("ADD"), apx_operands, 2, (BusterX86MetadataPhysicalAttributes){.no_flags = true}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult no_flags_select = buster_x86_metadata_select_form(no_flags_query);
        BusterX86MetadataForm no_flags_form = {0};
        BUSTER_TEST(arguments, no_flags_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && no_flags_select.form_id == 561 &&
                                   buster_x86_metadata_form(no_flags_select.form_id, &no_flags_form) &&
                                   (no_flags_form.apx_flags & BUSTER_X86_METADATA_APX_NF));
        BusterX86MetadataEmitResult no_flags_classic = x86_64_metadata_test_emit_form(
            S8("ADD"), 3053, vaddps_operands, 3, (BusterX86MetadataPhysicalAttributes){.no_flags = true}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, no_flags_classic.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);

        BusterX86MetadataPhysicalOperand ccmp_byte_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 8),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 14, 8),
        };
        BusterX86MetadataPhysicalAttributes ccmp_dfv0 = {.has_dfv = true, .dfv = 0};
        BusterX86MetadataPhysicalAttributes ccmp_dfv2 = {.has_dfv = true, .dfv = 2};
        BusterX86MetadataPhysicalAttributes ccmp_dfv15 = {.has_dfv = true, .dfv = 15};
        u8 ccmpb_dfv0_bytes[] = {0x62, 0x74, 0x04, 0x02, 0x38, 0xf2};
        u8 ccmpb_dfv2_bytes[] = {0x62, 0x74, 0x14, 0x02, 0x38, 0xf2};
        u8 ccmpb_dfv15_bytes[] = {0x62, 0x74, 0x7c, 0x02, 0x38, 0xf2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CCMPB"), 779, ccmp_byte_operands, 2,
                                                                 ccmp_dfv0, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ccmpb_dfv0_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ccmpb_dfv0_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CCMPB"), 779, ccmp_byte_operands, 2,
                                                                 ccmp_dfv2, wildcard, BUSTER_ARRAY_LENGTH(wildcard), ccmpb_dfv2_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ccmpb_dfv2_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CCMPB"), 779, ccmp_byte_operands, 2,
                                                                 ccmp_dfv15, wildcard, BUSTER_ARRAY_LENGTH(wildcard), ccmpb_dfv15_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ccmpb_dfv15_bytes)));

        BusterX86MetadataPhysicalOperand ccmp_qword_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 30, 64),
        };
        u8 ccmpl_bytes[] = {0x62, 0x64, 0x84, 0x0c, 0x39, 0xf2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CCMPL"), 851, ccmp_qword_operands, 2,
                                                                 ccmp_dfv0, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ccmpl_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ccmpl_bytes)));

        BusterX86MetadataPhysicalOperand ctest_qword_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 14, 64),
        };
        u8 ctestz_bytes[] = {0x62, 0x74, 0x84, 0x04, 0x85, 0xf2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("CTESTZ"), 1697, ctest_qword_operands, 2,
                                                                 ccmp_dfv0, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ctestz_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ctestz_bytes)));

        BusterX86MetadataEmitResult ccmp_missing_dfv = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult ccmp_invalid_dfv = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2,
            (BusterX86MetadataPhysicalAttributes){.has_dfv = true, .dfv = 16}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult ccmp_nonzero_without_dfv = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2,
            (BusterX86MetadataPhysicalAttributes){.dfv = 1}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult ordinary_form_with_dfv = x86_64_metadata_test_emit_form(
            S8("ADD"), 559, apx_operands, 2, ccmp_dfv0, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ccmp_missing_dfv.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   ccmp_invalid_dfv.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   ccmp_nonzero_without_dfv.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   ordinary_form_with_dfv.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        u32 rol_one_form_id = UINT32_MAX;
        BusterX86MetadataForm rol_one_form = {0};
        bool rol_one_contract = x86_64_metadata_test_iform_id(S8("ROL_GPR8i8_ONE_APX"), &rol_one_form_id) &&
                                buster_x86_metadata_form(rol_one_form_id, &rol_one_form) &&
                                x86_64_metadata_test_pattern_has_token(rol_one_form.pattern, S8("ONE()"));
        u32 rol_one_implicit_immediate_count = 0;
        bool rol_one_visible_immediate = false;
        if (rol_one_contract)
        {
            for (u32 operand_index = 0; operand_index < rol_one_form.operand_count; operand_index += 1)
            {
                BusterX86MetadataOperand rol_one_metadata_operand = {0};
                if (!buster_x86_metadata_operand(rol_one_form_id, operand_index, &rol_one_metadata_operand))
                {
                    rol_one_contract = false;
                    break;
                }
                if (rol_one_metadata_operand.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE &&
                    (rol_one_metadata_operand.access & BUSTER_X86_METADATA_ACCESS_IMPLICIT))
                    rol_one_implicit_immediate_count += 1;
                rol_one_visible_immediate |= rol_one_metadata_operand.kind == BUSTER_X86_METADATA_OPERAND_IMMEDIATE &&
                                             rol_one_metadata_operand.visible;
            }
        }
        BUSTER_TEST(arguments, rol_one_contract && rol_one_implicit_immediate_count == 1 && !rol_one_visible_immediate);
        BusterX86MetadataPhysicalOperand rol_one_operand =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 8);
        // This normalized iform is the EVEX APX row; the public handwritten
        // APX path separately uses the shorter REX2 spelling.
        u8 rol_one_bytes[] = {0x62, 0xfc, 0x7c, 0x08, 0xd0, 0xc0};
        BUSTER_TEST(arguments, rol_one_contract && x86_64_metadata_test_emit_exact(S8("ROL"), rol_one_form_id, &rol_one_operand, 1,
                                                                                      (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                      BUSTER_ARRAY_LENGTH(wildcard), rol_one_bytes,
                                                                                      BUSTER_ARRAY_LENGTH(rol_one_bytes)));

        u32 lzcnt_form_id = UINT32_MAX;
        u32 tzcnt_form_id = UINT32_MAX;
        u32 movss_ignore66_form_id = UINT32_MAX;
        u32 cmpxchg8b_immune66_form_id = UINT32_MAX;
        BusterX86MetadataForm lzcnt_form = {0};
        BusterX86MetadataForm tzcnt_form = {0};
        BusterX86MetadataForm movss_ignore66_form = {0};
        BusterX86MetadataForm cmpxchg8b_immune66_form = {0};
        bool lzcnt_token_form = x86_64_metadata_test_find_token_form(
            S8("LZCNT"), S8("LZCNT=1"), BUSTER_X86_METADATA_OPERAND_REGISTER, &lzcnt_form_id, &lzcnt_form);
        bool tzcnt_token_form = x86_64_metadata_test_find_token_form(
            S8("TZCNT"), S8("TZCNT=1"), BUSTER_X86_METADATA_OPERAND_REGISTER, &tzcnt_form_id, &tzcnt_form);
        bool movss_ignore66_token_form = x86_64_metadata_test_find_token_form(
            S8("MOVSS"), S8("IGNORE66()"), BUSTER_X86_METADATA_OPERAND_REGISTER, &movss_ignore66_form_id,
            &movss_ignore66_form);
        bool cmpxchg8b_immune66_token_form = x86_64_metadata_test_find_token_form(
            S8("CMPXCHG8B"), S8("IMMUNE66()"), BUSTER_X86_METADATA_OPERAND_MEMORY, &cmpxchg8b_immune66_form_id,
            &cmpxchg8b_immune66_form);
        BusterX86MetadataPhysicalOperand lzcnt_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 32),
        };
        BusterX86MetadataPhysicalOperand tzcnt_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 3, 32),
        };
        BusterX86MetadataPhysicalOperand movss_ignore66_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
        };
        BusterX86MetadataPhysicalOperand cmpxchg8b_immune66_operand = x86_64_metadata_test_physical_mem_base(0, 64, 0);
        u8 lzcnt_metadata_bytes[] = {0xf3, 0x0f, 0xbd, 0xca};
        u8 tzcnt_metadata_bytes[] = {0xf3, 0x0f, 0xbc, 0xc3};
        u8 movss_ignore66_bytes[] = {0xf3, 0x0f, 0x10, 0xc1};
        u8 cmpxchg8b_immune66_bytes[] = {0x0f, 0xc7, 0x08};
        BUSTER_TEST(arguments, lzcnt_token_form && x86_64_metadata_test_emit_exact(S8("LZCNT"), lzcnt_form_id, lzcnt_operands, 2,
                                                                                       (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                       BUSTER_ARRAY_LENGTH(wildcard), lzcnt_metadata_bytes,
                                                                                       BUSTER_ARRAY_LENGTH(lzcnt_metadata_bytes)));
        BUSTER_TEST(arguments, tzcnt_token_form && x86_64_metadata_test_emit_exact(S8("TZCNT"), tzcnt_form_id, tzcnt_operands, 2,
                                                                                       (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                       BUSTER_ARRAY_LENGTH(wildcard), tzcnt_metadata_bytes,
                                                                                       BUSTER_ARRAY_LENGTH(tzcnt_metadata_bytes)));

        // AMD/HSW duplicate BSR/BSF rows carry the same opcode but explicitly
        // reject F3.  Each normalized no-F3 row must emit the ordinary bytes,
        // and front-door selection must resolve the duplicates deterministically
        // instead of reporting an ambiguity.
        BusterX86MetadataPhysicalOperand bsr_register_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32),
        };
        BusterX86MetadataPhysicalOperand bsr_memory_operands[2] = {
            bsr_register_operands[0], x86_64_metadata_test_physical_mem_base(1, 32, 0),
        };
        BusterX86MetadataPhysicalOperand bsf_register_operands[2] = {
            bsr_register_operands[0], bsr_register_operands[1],
        };
        BusterX86MetadataPhysicalOperand bsf_memory_operands[2] = {
            bsf_register_operands[0], bsr_memory_operands[1],
        };
        u8 bsr_register_bytes[] = {0x0f, 0xbd, 0xc1};
        u8 bsr_memory_bytes[] = {0x0f, 0xbd, 0x01};
        u8 bsf_register_bytes[] = {0x0f, 0xbc, 0xc1};
        u8 bsf_memory_bytes[] = {0x0f, 0xbc, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSR"), 445, bsr_memory_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bsr_memory_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bsr_memory_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSR"), 446, bsr_register_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bsr_register_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bsr_register_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSR"), 8104, bsr_memory_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bsr_memory_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bsr_memory_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSR"), 8105, bsr_register_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bsr_register_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bsr_register_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSF"), 8687, bsf_memory_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bsf_memory_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bsf_memory_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("BSF"), 8688, bsf_register_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), bsf_register_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bsf_register_bytes)));
        BusterX86MetadataEmitResult bsr_f3_rejected = x86_64_metadata_test_emit_form(
            S8("BSR"), 445, bsr_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){.rep = true}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, bsr_f3_rejected.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);
        BusterX86MetadataPhysicalQuery bsr_query = x86_64_metadata_test_physical_query(
            S8("BSR"), bsr_register_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataPhysicalQuery bsf_query = bsr_query;
        bsf_query.mnemonic = S8("BSF");
        BusterX86MetadataSelectResult bsr_selection = buster_x86_metadata_select_form(bsr_query);
        BusterX86MetadataSelectResult bsf_selection = buster_x86_metadata_select_form(bsf_query);
        u8 bsr_selected_bytes[8] = {0};
        u8 bsf_selected_bytes[8] = {0};
        BusterX86MetadataEmitResult bsr_selected = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = bsr_query, .form_id = bsr_selection.form_id, .output = bsr_selected_bytes,
                                         .output_capacity = BUSTER_ARRAY_LENGTH(bsr_selected_bytes)});
        BusterX86MetadataEmitResult bsf_selected = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = bsf_query, .form_id = bsf_selection.form_id, .output = bsf_selected_bytes,
                                         .output_capacity = BUSTER_ARRAY_LENGTH(bsf_selected_bytes)});
        BUSTER_TEST(arguments, bsr_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   bsr_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(bsr_selected_bytes, bsr_selected.byte_count, bsr_register_bytes,
                                                                     BUSTER_ARRAY_LENGTH(bsr_register_bytes)) &&
                                   bsf_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   bsf_selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(bsf_selected_bytes, bsf_selected.byte_count, bsf_register_bytes,
                                                                     BUSTER_ARRAY_LENGTH(bsf_register_bytes)));
        BUSTER_TEST(arguments, movss_ignore66_token_form && x86_64_metadata_test_emit_exact(
                                   S8("MOVSS"), movss_ignore66_form_id, movss_ignore66_operands, 2,
                                   (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
                                   movss_ignore66_bytes, BUSTER_ARRAY_LENGTH(movss_ignore66_bytes)));
        BUSTER_TEST(arguments, cmpxchg8b_immune66_token_form && x86_64_metadata_test_emit_exact(
                                   S8("CMPXCHG8B"), cmpxchg8b_immune66_form_id, &cmpxchg8b_immune66_operand, 1,
                                   (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
                                   cmpxchg8b_immune66_bytes, BUSTER_ARRAY_LENGTH(cmpxchg8b_immune66_bytes)));

        u32 endbr64_form_id = UINT32_MAX;
        u32 cldemote_form_id = UINT32_MAX;
        u32 prefetchit0_form_id = UINT32_MAX;
        u32 prefetchit1_form_id = UINT32_MAX;
        u32 prefetchrst2_form_id = UINT32_MAX;
        bool selector_forms = x86_64_metadata_test_iform_id(S8("ENDBR64"), &endbr64_form_id) &&
                              x86_64_metadata_test_iform_id(S8("CLDEMOTE_MEMu8"), &cldemote_form_id) &&
                              x86_64_metadata_test_iform_id(S8("PREFETCHIT0_MEMu8"), &prefetchit0_form_id) &&
                              x86_64_metadata_test_iform_id(S8("PREFETCHIT1_MEMu8"), &prefetchit1_form_id) &&
                              x86_64_metadata_test_iform_id(S8("PREFETCHRST2_MEMu8"), &prefetchrst2_form_id);
        BusterX86MetadataPhysicalOperand selector_memory = x86_64_metadata_test_physical_mem_base(0, 8, 0);
        BusterX86MetadataPhysicalOperand prefetchit_memory = x86_64_metadata_test_physical_mem_rip((String8){0}, 0, 8);
        prefetchit_memory.memory.has_symbol = false;
        u8 endbr64_bytes[] = {0xf3, 0x0f, 0x1e, 0xfa};
        u8 cldemote_bytes[] = {0x0f, 0x1c, 0x00};
        u8 prefetchit0_bytes[] = {0x0f, 0x18, 0x3d, 0x00, 0x00, 0x00, 0x00};
        u8 prefetchit1_bytes[] = {0x0f, 0x18, 0x35, 0x00, 0x00, 0x00, 0x00};
        u8 prefetchrst2_bytes[] = {0x0f, 0x18, 0x20};
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("ENDBR64"), endbr64_form_id, 0, 0,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), endbr64_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(endbr64_bytes)));
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("CLDEMOTE"), cldemote_form_id, &selector_memory, 1,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), cldemote_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(cldemote_bytes)));
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("PREFETCHIT0"), prefetchit0_form_id, &prefetchit_memory, 1,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), prefetchit0_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(prefetchit0_bytes)));
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("PREFETCHIT1"), prefetchit1_form_id, &prefetchit_memory, 1,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), prefetchit1_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(prefetchit1_bytes)));
        BUSTER_TEST(arguments, selector_forms && x86_64_metadata_test_emit_exact(S8("PREFETCHRST2"), prefetchrst2_form_id, &selector_memory, 1,
                                                                                    (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                                    BUSTER_ARRAY_LENGTH(wildcard), prefetchrst2_bytes,
                                                                                    BUSTER_ARRAY_LENGTH(prefetchrst2_bytes)));

        // CET=0 rows are typed legacy-NOP aliases.  Their raw signature is
        // preserved for audit, but they remain unavailable to the public
        // encoder: no source query can select mandatory F3 without ambiguity
        // against the generic 0f1e NOP form, and the bytes become ENDBR/RDSSP
        // under CET.  ENCDELETE rows are similarly retained as explicit
        // deleted/noncanonical aliases and must stay blocked.
        u32 const cet0_form_ids[] = {7965, 7966, 7967, 7968};
        bool cet0_contract = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cet0_form_ids); index += 1)
        {
            u32 form_id = cet0_form_ids[index];
            BusterX86MetadataForm form = {0};
            BusterX86MetadataOperand operands[2] = {0};
            BusterX86MetadataPhysicalOperand physical[16] = {0};
            char8 mnemonic_buffer[128] = {0};
            BusterX86MetadataPhysicalQuery query = {0};
            bool retrieved = buster_x86_metadata_form(form_id, &form) && buster_x86_metadata_operand(form_id, 0, operands + 0) &&
                             buster_x86_metadata_operand(form_id, 1, operands + 1) &&
                             x86_64_metadata_test_build_gate_query(form_id, &query, physical, mnemonic_buffer);
            BusterX86MetadataEmitResult emitted = retrieved
                                                       ? buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                                                             .physical = query,
                                                             .form_id = form_id,
                                                             .output = (u8[32]){0},
                                                             .output_capacity = 32,
                                                         })
                                                       : (BusterX86MetadataEmitResult){.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT};
            BusterX86MetadataSelectResult selected = retrieved ? buster_x86_metadata_select_form(query)
                                                               : (BusterX86MetadataSelectResult){.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT};
            bool raw_signature = form.fixed_byte_count == 2 && form.fixed_bytes[0] == 0x0f && form.fixed_bytes[1] == 0x1e &&
                                 form.mandatory_prefix == 0xf3 && form.map == BUSTER_X86_METADATA_MAP_0F &&
                                 (form.field_flags & (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_REGISTER)) ==
                                     (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_REGISTER);
            bool selected_generic = selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS;
            selected_generic &= selected.form_id != 7965 && selected.form_id != 7966 && selected.form_id != 7967 && selected.form_id != 7968;
            cet0_contract &= retrieved && x86_64_metadata_test_string_equal(form.iclass, S8("NOP")) &&
                             x86_64_metadata_test_string_equal(form.isa_set, S8("PPRO")) &&
                             x86_64_metadata_test_string_equal(form.extension, S8("BASE")) &&
                             form.coverage_class == BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS &&
                             x86_64_metadata_test_pattern_has_token(form.pattern, S8("CET=0")) && raw_signature &&
                             operands[0].visible && operands[1].visible && operands[0].physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
                             operands[1].physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR &&
                             (operands[0].physical_width_flags & (1u << (index < 3 ? 2 : 3))) != 0 &&
                             (operands[1].physical_width_flags & (1u << (index < 3 ? 2 : 3))) != 0 &&
                             emitted.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA && selected_generic;
        }
        BUSTER_TEST(arguments, cet0_contract);

        u32 const encdelete_form_ids[] = {7970, 7971, 7972, 7973, 7974, 7975};
        bool encdelete_contract = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(encdelete_form_ids); index += 1)
        {
            u32 form_id = encdelete_form_ids[index];
            BusterX86MetadataForm form = {0};
            BusterX86MetadataPhysicalOperand physical[16] = {0};
            char8 mnemonic_buffer[128] = {0};
            BusterX86MetadataPhysicalQuery query = {0};
            bool retrieved = buster_x86_metadata_form(form_id, &form) &&
                             x86_64_metadata_test_build_gate_query(form_id, &query, physical, mnemonic_buffer);
            BusterX86MetadataEmitResult emitted = retrieved
                                                       ? buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                                                             .physical = query,
                                                             .form_id = form_id,
                                                             .output = (u8[32]){0},
                                                             .output_capacity = 32,
                                                         })
                                                       : (BusterX86MetadataEmitResult){.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT};
            encdelete_contract &= retrieved && x86_64_metadata_test_string_equal(form.iclass, S8("NOP")) &&
                                  x86_64_metadata_test_string_equal(form.isa_set, S8("PPRO")) &&
                                  x86_64_metadata_test_string_equal(form.extension, S8("BASE")) &&
                                  x86_64_metadata_test_pattern_has_token(form.pattern, S8("ENCDELETE")) &&
                                  emitted.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA;
        }
        BUSTER_TEST(arguments, encdelete_contract);

        // Form 7976 is the noncanonical NOP alias for 0f 1c with a memory
        // operand and a ModRM.reg field in the bounded range 1..7.  It is
        // metadata-only (the public selector must continue to prefer the
        // architectural CLDEMOTE spelling), but direct emission must retain
        // every legal register field and reject the CLDEMOTE value zero.
        BusterX86MetadataForm nop_range_form = {0};
        BusterX86MetadataPhysicalOperand nop_range_operands[2] = {0};
        nop_range_operands[0] = x86_64_metadata_test_physical_mem_base(0, 32, 0);
        bool nop_range_shape = buster_x86_metadata_form(7976, &nop_range_form) &&
                               x86_64_metadata_test_string_equal(nop_range_form.iclass, S8("NOP")) &&
                               x86_64_metadata_test_pattern_has_token(nop_range_form.pattern, S8("REG[1-7]"));
        bool nop_range_bytes = true;
        for (u16 reg = 1; reg <= 7; reg += 1)
        {
            nop_range_operands[1] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, reg, 32);
            u8 expected[3] = {0x0f, 0x1c, (u8)(reg << 3)};
            nop_range_bytes &= x86_64_metadata_test_emit_exact(
                S8("NOP"), 7976, nop_range_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                BUSTER_ARRAY_LENGTH(wildcard), expected, BUSTER_ARRAY_LENGTH(expected));
        }
        nop_range_operands[1] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32);
        BusterX86MetadataEmitResult nop_range_reg0 = x86_64_metadata_test_emit_form(
            S8("NOP"), 7976, nop_range_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[8]){0}, 8, 0, 0);
        nop_range_operands[1] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 32);
        BusterX86MetadataEmitResult nop_range_reg8 = x86_64_metadata_test_emit_form(
            S8("NOP"), 7976, nop_range_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, nop_range_shape && nop_range_bytes &&
                                   nop_range_reg0.status == BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING &&
                                   nop_range_reg8.status == BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING);

        u32 const residual_neighbors[] = {7969, 7976, 7977, 7978};
        bool neighbor_exclusions = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(residual_neighbors); index += 1)
        {
            BusterX86MetadataForm form = {0};
            bool retrieved = buster_x86_metadata_form(residual_neighbors[index], &form);
            neighbor_exclusions &= retrieved && !x86_64_metadata_test_pattern_has_token(form.pattern, S8("CET=0")) &&
                                   !x86_64_metadata_test_pattern_has_token(form.pattern, S8("ENCDELETE"));
        }
        BUSTER_TEST(arguments, neighbor_exclusions);
        // The value-0 rows are legacy aliases that share bytes with the
        // value-1 architectural forms.  Their refining-prefix spellings are
        // decode-only, so keep every value-0 cohort row blocked and exercise
        // the real value-1 rows directly.
        BusterX86MetadataPhysicalOperand lzcnt_zero_memory_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32),
            x86_64_metadata_test_physical_mem_base(0, 32, 0),
        };
        BusterX86MetadataPhysicalOperand tzcnt_zero_memory_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32),
            x86_64_metadata_test_physical_mem_base(0, 32, 0),
        };
        BusterX86MetadataPhysicalOperand cldemote_zero_operands[2] = {
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
        };
        BusterX86MetadataPhysicalOperand ibhf_zero_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 7, 64),
        };
        BusterX86MetadataPhysicalOperand prefetchrst_zero_operand = x86_64_metadata_test_physical_mem_base(0, 64, 0);
        BusterX86MetadataPhysicalOperand prefetchit_zero_operand = x86_64_metadata_test_physical_mem_base(0, 64, 0);
        u8 ibhf_bytes[] = {0xf3, 0x48, 0x0f, 0x1e, 0xf8};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("IBHF"), 8691, 0, 0,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), ibhf_bytes,
                                                                 BUSTER_ARRAY_LENGTH(ibhf_bytes)));
        BusterX86MetadataEmitResult lzcnt_zero_memory_blocked = x86_64_metadata_test_emit_form(
            S8("BSR"), 8106, lzcnt_zero_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult lzcnt_zero_register_blocked = x86_64_metadata_test_emit_form(
            S8("BSR"), 8107, lzcnt_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult tzcnt_zero_memory_blocked = x86_64_metadata_test_emit_form(
            S8("BSF"), 8689, tzcnt_zero_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult tzcnt_zero_register_blocked = x86_64_metadata_test_emit_form(
            S8("BSF"), 8690, tzcnt_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult cldemote_zero_blocked = x86_64_metadata_test_emit_form(
            S8("NOP"), 7978, cldemote_zero_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult ibhf_zero_blocked = x86_64_metadata_test_emit_form(
            S8("NOP"), 8692, ibhf_zero_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult prefetchrst_zero_blocked = x86_64_metadata_test_emit_form(
            S8("NOP"), 9572, &prefetchrst_zero_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult prefetchit0_zero_blocked = x86_64_metadata_test_emit_form(
            S8("NOP"), 9573, &prefetchit_zero_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult prefetchit1_zero_blocked = x86_64_metadata_test_emit_form(
            S8("NOP"), 9574, &prefetchit_zero_operand, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, lzcnt_zero_memory_blocked.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   lzcnt_zero_register_blocked.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   tzcnt_zero_memory_blocked.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   tzcnt_zero_register_blocked.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   cldemote_zero_blocked.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   ibhf_zero_blocked.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   prefetchrst_zero_blocked.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   prefetchit0_zero_blocked.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA &&
                                   prefetchit1_zero_blocked.status == BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA);

        // A value-0 row must not be reachable through the value-1 mnemonic,
        // and feature-gated value-1 forms still report their missing ISA.
        BusterX86MetadataEmitResult ibhf_wrong_mnemonic = x86_64_metadata_test_emit_form(
            S8("NOP"), 8691, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
            (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult ibhf_zero_wrong_mnemonic = x86_64_metadata_test_emit_form(
            S8("IBHF"), 8692, ibhf_zero_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        String8 no_boolean_features[] = {S8("i386")};
        BusterX86MetadataEmitResult lzcnt_missing_feature = x86_64_metadata_test_emit_form(
            S8("LZCNT"), 8103, lzcnt_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, no_boolean_features,
            BUSTER_ARRAY_LENGTH(no_boolean_features), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult ibhf_missing_feature = x86_64_metadata_test_emit_form(
            S8("IBHF"), 8691, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, no_boolean_features,
            BUSTER_ARRAY_LENGTH(no_boolean_features), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult cldemote_missing_feature = x86_64_metadata_test_emit_form(
            S8("CLDEMOTE"), cldemote_form_id, &selector_memory, 1, (BusterX86MetadataPhysicalAttributes){0}, no_boolean_features,
            BUSTER_ARRAY_LENGTH(no_boolean_features), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult prefetchrst_missing_feature = x86_64_metadata_test_emit_form(
            S8("PREFETCHRST2"), prefetchrst2_form_id, &selector_memory, 1, (BusterX86MetadataPhysicalAttributes){0}, no_boolean_features,
            BUSTER_ARRAY_LENGTH(no_boolean_features), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult prefetchit_missing_feature = x86_64_metadata_test_emit_form(
            S8("PREFETCHIT0"), prefetchit0_form_id, &prefetchit_memory, 1, (BusterX86MetadataPhysicalAttributes){0}, no_boolean_features,
            BUSTER_ARRAY_LENGTH(no_boolean_features), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ibhf_wrong_mnemonic.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM &&
                                   ibhf_zero_wrong_mnemonic.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM &&
                                   lzcnt_missing_feature.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   ibhf_missing_feature.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   cldemote_missing_feature.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   prefetchrst_missing_feature.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   prefetchit_missing_feature.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);

        BusterX86MetadataEmitResult ccmp_wrong_operand = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779,
            (BusterX86MetadataPhysicalOperand[2]){
                x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 2, 128),
                ccmp_byte_operands[1],
            },
            2, ccmp_dfv0, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ccmp_wrong_operand.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
        BusterX86MetadataEmitResult ccmp_decorator = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2,
            (BusterX86MetadataPhysicalAttributes){
                .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK,
                .has_mask_register = true,
                .mask_register = 1,
                .has_dfv = true,
                .dfv = 0,
            },
            wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ccmp_decorator.status == BUSTER_X86_METADATA_ENCODE_DECORATOR);
        String8 no_apx_features[1] = {S8("avx512f")};
        BusterX86MetadataEmitResult ccmp_feature = x86_64_metadata_test_emit_form(
            S8("CCMPB"), 779, ccmp_byte_operands, 2, ccmp_dfv0, no_apx_features,
            BUSTER_ARRAY_LENGTH(no_apx_features), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ccmp_feature.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);

        BusterX86MetadataPhysicalOperand kadd_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 1, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 2, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 3, 64),
        };
        u8 kadd_bytes[] = {0xc5, 0xec, 0x4a, 0xcb};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("KADDW"), 6874, kadd_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), kadd_bytes,
                                                                 BUSTER_ARRAY_LENGTH(kadd_bytes)));

        // MASK_B's scalar suffix is an element qualifier; both visible
        // operands remain architectural 64-bit k registers.  The APX EVEX
        // W/pp bits come from each row's pattern (V66/W0, VF2/W0, V66/W1,
        // VNP/W1, VNP/W0), not from that architectural register width.
        BusterX86MetadataPhysicalOperand kmov_mask_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 1, 64),
        };
        u8 kmovb_apx_bytes[] = {0x62, 0xf1, 0x7d, 0x08, 0x90, 0xc1};
        BusterX86MetadataPhysicalOperand kmovd_gpr_mask_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 1, 64),
        };
        u8 kmovd_vf2_apx_bytes[] = {0x62, 0xf1, 0x7f, 0x08, 0x93, 0xc1};
        u8 kmovd_apx_bytes[] = {0x62, 0xf1, 0xfd, 0x08, 0x90, 0xc1};
        BusterX86MetadataPhysicalOperand kmovq_mask_gpr_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
        };
        u8 kmovq_vf2_apx_bytes[] = {0x62, 0xf1, 0xff, 0x08, 0x92, 0xc0};
        u8 kmovw_apx_bytes[] = {0x62, 0xf1, 0x7c, 0x08, 0x90, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("KMOVB"), 1850, kmov_mask_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), kmovb_apx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(kmovb_apx_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("KMOVD"), 1855, kmovd_gpr_mask_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), kmovd_vf2_apx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(kmovd_vf2_apx_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("KMOVD"), 1857, kmov_mask_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), kmovd_apx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(kmovd_apx_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("KMOVQ"), 1862, kmovq_mask_gpr_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), kmovq_vf2_apx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(kmovq_vf2_apx_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("KMOVW"), 1865, kmov_mask_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), kmovw_apx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(kmovw_apx_bytes)));

        // The same schema distinction applies to the AVX512 VEX rows.  Keep
        // this collateral ledger explicit so a future width change cannot
        // silently regress or hide the three non-APX analogues.
        u32 kmov_vex_ids[] = {6881, 6886, 7860};
        String8 kmov_vex_mnemonics[] = {S8_INITIALIZER("KMOVB"), S8_INITIALIZER("KMOVD"), S8_INITIALIZER("KMOVW")};
        u8 kmov_vex_bytes[][5] = {
            {0xc5, 0xf9, 0x90, 0xc1, 0},
            {0xc4, 0xe1, 0xf9, 0x90, 0xc1},
            {0xc5, 0xf8, 0x90, 0xc1, 0},
        };
        u8 kmov_vex_lengths[] = {4, 5, 4};
        for (u32 kmov_index = 0; kmov_index < BUSTER_ARRAY_LENGTH(kmov_vex_ids); kmov_index += 1)
        {
            BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(
                                       kmov_vex_mnemonics[kmov_index], kmov_vex_ids[kmov_index], kmov_mask_operands,
                                       BUSTER_ARRAY_LENGTH(kmov_mask_operands), (BusterX86MetadataPhysicalAttributes){0},
                                       wildcard, BUSTER_ARRAY_LENGTH(wildcard), kmov_vex_bytes[kmov_index],
                                       kmov_vex_lengths[kmov_index]));
        }

        u8 kmov_vex_output[32] = {0};
        BusterX86MetadataPhysicalOperand kmov_wrong_class_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            kmov_mask_operands[1],
        };
        BusterX86MetadataEmitResult kmov_wrong_class = x86_64_metadata_test_emit_form(
            S8("KMOVB"), 1850, kmov_wrong_class_operands, BUSTER_ARRAY_LENGTH(kmov_wrong_class_operands),
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), kmov_vex_output,
            BUSTER_ARRAY_LENGTH(kmov_vex_output), 0, 0);
        BusterX86MetadataPhysicalOperand kmov_wrong_width_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 0, 32),
            kmov_mask_operands[1],
        };
        BusterX86MetadataEmitResult kmov_wrong_width = x86_64_metadata_test_emit_form(
            S8("KMOVB"), 1850, kmov_wrong_width_operands, BUSTER_ARRAY_LENGTH(kmov_wrong_width_operands),
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), kmov_vex_output,
            BUSTER_ARRAY_LENGTH(kmov_vex_output), 0, 0);
        BusterX86MetadataPhysicalOperand kmov_wrong_kind_operands[2] = {
            kmov_mask_operands[0], x86_64_metadata_test_physical_mem_base(0, 8, 0),
        };
        BusterX86MetadataEmitResult kmov_wrong_kind = x86_64_metadata_test_emit_form(
            S8("KMOVB"), 1850, kmov_wrong_kind_operands, BUSTER_ARRAY_LENGTH(kmov_wrong_kind_operands),
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), kmov_vex_output,
            BUSTER_ARRAY_LENGTH(kmov_vex_output), 0, 0);
        BUSTER_TEST(arguments, kmov_wrong_class.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
        BUSTER_TEST(arguments, kmov_wrong_width.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
        BUSTER_TEST(arguments, kmov_wrong_kind.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);

        BusterX86MetadataPhysicalOperand tile_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM, 0, 1024),
            x86_64_metadata_test_physical_mem_base(3, 32, 0),
        };
        u8 tile_bytes[] = {0xc4, 0xe2, 0x7b, 0x4b, 0x04, 0x23};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("TILELOADD"), 483, tile_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tile_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tile_bytes)));

        // The standard VEX AMX tile-memory rows omit XED's displacement
        // field annotation, but still use the ordinary ModRM/SIB MOD=01/10
        // address encoding.  Keep nonzero displacement and forced-SIB
        // queries on the canonical VEX rows rather than falling through to
        // the feature-gated APX/EVEX siblings.
        BusterX86MetadataPhysicalOperand tileloadd_disp_memory =
            x86_64_metadata_test_physical_mem_base(13, 32, 32);
        tileloadd_disp_memory.memory.has_index = true;
        tileloadd_disp_memory.memory.index =
            (BusterX86MetadataPhysicalRegister){.index = 14, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        tileloadd_disp_memory.memory.scale = 2;
        BusterX86MetadataPhysicalOperand tileloadd_disp_operands[2] = {tile_operands[0], tileloadd_disp_memory};
        u8 tileloadd_disp_bytes[] = {0xc4, 0x82, 0x7b, 0x4b, 0x44, 0x75, 0x20};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("TILELOADD"), 483, tileloadd_disp_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tileloadd_disp_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tileloadd_disp_bytes)));
        BusterX86MetadataPhysicalQuery tileloadd_disp_query = x86_64_metadata_test_physical_query(
            S8("TILELOADD"), tileloadd_disp_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        tileloadd_disp_query.source_semantics = true;
        BusterX86MetadataSelectResult tileloadd_disp_selection = buster_x86_metadata_select_form(tileloadd_disp_query);
        u8 tileloadd_disp_output[16] = {0};
        BusterX86MetadataEmitResult tileloadd_disp_encode = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = tileloadd_disp_query, .output = tileloadd_disp_output,
                                           .output_capacity = BUSTER_ARRAY_LENGTH(tileloadd_disp_output)});
        BUSTER_TEST(arguments, tileloadd_disp_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   tileloadd_disp_selection.form_id == 483 && tileloadd_disp_encode.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   tileloadd_disp_encode.form_id == 483 &&
                                   x86_64_metadata_test_bytes_equal(tileloadd_disp_output, tileloadd_disp_encode.byte_count,
                                                                     tileloadd_disp_bytes, BUSTER_ARRAY_LENGTH(tileloadd_disp_bytes)));

        BusterX86MetadataPhysicalOperand tileloaddt1_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM, 1, 1024),
            x86_64_metadata_test_physical_mem_base(15, 32, 64),
        };
        u8 tileloaddt1_bytes[] = {0xc4, 0xc2, 0x79, 0x4b, 0x4c, 0x27, 0x40};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("TILELOADDT1"), 484, tileloaddt1_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tileloaddt1_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tileloaddt1_bytes)));
        BusterX86MetadataPhysicalQuery tileloaddt1_query = x86_64_metadata_test_physical_query(
            S8("TILELOADDT1"), tileloaddt1_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        tileloaddt1_query.source_semantics = true;
        BusterX86MetadataSelectResult tileloaddt1_selection = buster_x86_metadata_select_form(tileloaddt1_query);
        u8 tileloaddt1_output[16] = {0};
        BusterX86MetadataEmitResult tileloaddt1_encode = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = tileloaddt1_query, .output = tileloaddt1_output,
                                           .output_capacity = BUSTER_ARRAY_LENGTH(tileloaddt1_output)});
        BUSTER_TEST(arguments, tileloaddt1_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   tileloaddt1_selection.form_id == 484 && tileloaddt1_encode.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   tileloaddt1_encode.form_id == 484 &&
                                   x86_64_metadata_test_bytes_equal(tileloaddt1_output, tileloaddt1_encode.byte_count,
                                                                     tileloaddt1_bytes, BUSTER_ARRAY_LENGTH(tileloaddt1_bytes)));

        BusterX86MetadataPhysicalOperand tilestored_disp_memory =
            x86_64_metadata_test_physical_mem_base(12, 32, 128);
        tilestored_disp_memory.memory.has_index = true;
        tilestored_disp_memory.memory.index =
            (BusterX86MetadataPhysicalRegister){.index = 13, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        tilestored_disp_memory.memory.scale = 4;
        BusterX86MetadataPhysicalOperand tilestored_disp_operands[2] = {
            tilestored_disp_memory,
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM, 2, 1024),
        };
        u8 tilestored_disp_bytes[] = {0xc4, 0x82, 0x7a, 0x4b, 0x94, 0xac, 0x80, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("TILESTORED"), 486, tilestored_disp_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tilestored_disp_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tilestored_disp_bytes)));
        BusterX86MetadataPhysicalQuery tilestored_query = x86_64_metadata_test_physical_query(
            S8("TILESTORED"), tilestored_disp_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        tilestored_query.source_semantics = true;
        BusterX86MetadataSelectResult tilestored_selection = buster_x86_metadata_select_form(tilestored_query);
        u8 tilestored_output[16] = {0};
        BusterX86MetadataEmitResult tilestored_encode = buster_x86_metadata_encode(
            (BusterX86MetadataEncodeQuery){.physical = tilestored_query, .output = tilestored_output,
                                           .output_capacity = BUSTER_ARRAY_LENGTH(tilestored_output)});
        BUSTER_TEST(arguments, tilestored_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   tilestored_selection.form_id == 486 && tilestored_encode.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   tilestored_encode.form_id == 486 &&
                                   x86_64_metadata_test_bytes_equal(tilestored_output, tilestored_encode.byte_count,
                                                                     tilestored_disp_bytes, BUSTER_ARRAY_LENGTH(tilestored_disp_bytes)));

        // These exact byte oracles exercise EVEX memory extension, APX width
        // selection, VSIB V' routing, tuple displacement compression, and
        // the XOP/APX families without relying on a host assembler.
        BusterX86MetadataPhysicalOperand push16_form =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 16, 16);
        u8 push16_form_bytes[] = {0x66, 0xd5, 0x10, 0x50};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PUSH"), 9723, &push16_form, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), push16_form_bytes,
                                                                 BUSTER_ARRAY_LENGTH(push16_form_bytes)));
        BusterX86MetadataEmitResult rex2_without_apx = x86_64_metadata_test_emit_form(
            S8("PUSH"), 9723, &push16_form, 1, (BusterX86MetadataPhysicalAttributes){0}, 0, 0, (u8[1]){0}, 0, 0, 0);
        String8 apx_feature[1] = {S8("apx")};
        BUSTER_TEST(arguments, rex2_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   x86_64_metadata_test_emit_exact(S8("PUSH"), 9723, &push16_form, 1,
                                                                    (BusterX86MetadataPhysicalAttributes){0}, apx_feature,
                                                                    BUSTER_ARRAY_LENGTH(apx_feature), push16_form_bytes,
                                                                    BUSTER_ARRAY_LENGTH(push16_form_bytes)));

        BusterX86MetadataPhysicalOperand vaddps_memory_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_mem_base(16, 32, 0),
        };
        u8 vaddps_r16_bytes[] = {0x62, 0xf9, 0x74, 0x48, 0x58, 0x00};
        String8 avx512_only[1] = {S8("avx512f")};
        String8 avx512_apx[2] = {S8("avx512f"), S8("apx")};
        BusterX86MetadataEmitResult vaddps_r16_without_apx = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, vaddps_memory_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, avx512_only,
            BUSTER_ARRAY_LENGTH(avx512_only), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, vaddps_r16_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, vaddps_memory_operands, 3,
                                                                    (BusterX86MetadataPhysicalAttributes){0}, avx512_apx,
                                                                    BUSTER_ARRAY_LENGTH(avx512_apx), vaddps_r16_bytes,
                                                                    BUSTER_ARRAY_LENGTH(vaddps_r16_bytes)));

        BusterX86MetadataPhysicalOperand vaddps_complex_memory = x86_64_metadata_test_physical_mem_base(24, 32, 64);
        vaddps_complex_memory.memory.has_index = true;
        vaddps_complex_memory.memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 25, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        vaddps_complex_memory.memory.scale = 4;
        vaddps_complex_memory.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand vaddps_complex_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], vaddps_complex_memory};
        u8 vaddps_complex_bytes[] = {0x62, 0x99, 0x70, 0x48, 0x58, 0x44, 0x88, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, vaddps_complex_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, avx512_apx,
                                                                 BUSTER_ARRAY_LENGTH(avx512_apx), vaddps_complex_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vaddps_complex_bytes)));
        BusterX86MetadataPhysicalOperand vaddps_index_memory = x86_64_metadata_test_physical_mem_index(25, 4, 32, 64);
        vaddps_index_memory.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand vaddps_index_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], vaddps_index_memory};
        u8 vaddps_index_bytes[] = {0x62, 0xb1, 0x70, 0x48, 0x58, 0x04, 0x8d, 0x40, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, vaddps_index_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, avx512_apx,
                                                                 BUSTER_ARRAY_LENGTH(avx512_apx), vaddps_index_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vaddps_index_bytes)));
        BusterX86MetadataPhysicalOperand vaddps_broadcast_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_mem_base(0, 32, 0),
        };
        BusterX86MetadataPhysicalAttributes broadcast_attributes = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_BROADCAST,
            .broadcast_elements = 16,
        };
        u8 vaddps_broadcast_bytes[] = {0x62, 0xf1, 0x74, 0x58, 0x58, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, vaddps_broadcast_operands, 3,
                                                                 broadcast_attributes, avx512_only,
                                                                 BUSTER_ARRAY_LENGTH(avx512_only), vaddps_broadcast_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vaddps_broadcast_bytes)));
        BusterX86MetadataPhysicalAttributes invalid_broadcast_attributes = broadcast_attributes;
        invalid_broadcast_attributes.broadcast_elements = 7;
        BusterX86MetadataEmitResult invalid_broadcast_count = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, vaddps_broadcast_operands, 3, invalid_broadcast_attributes, avx512_only,
            BUSTER_ARRAY_LENGTH(avx512_only), (u8[8]){0}, 8, 0, 0);
        BUSTER_TEST(arguments, invalid_broadcast_count.status == BUSTER_X86_METADATA_ENCODE_DECORATOR);

        // EMX_BROADCAST_* is XED's implicit widening description, not source
        // syntax.  It stays in the normalized operand ledger for provenance,
        // but is hidden from source matching and must not request EVEX.b.
        BUSTER_TEST(arguments, x86_64_metadata_test_emx_operand_hidden(3239, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emx_operand_hidden(8568, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emx_operand_hidden(2957, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emx_operand_hidden(5135, 3));

        BusterX86MetadataPhysicalOperand emx_avx_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_mem_base(0, 32, 0),
        };
        u8 emx_avx_bytes[] = {0xc4, 0xe2, 0x7d, 0x18, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VBROADCASTSS"), 3239, emx_avx_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), emx_avx_bytes,
                                                                 BUSTER_ARRAY_LENGTH(emx_avx_bytes)));

        BusterX86MetadataPhysicalOperand emx_avx2_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_mem_base(0, 8, 0),
        };
        u8 emx_avx2_bytes[] = {0xc4, 0xe2, 0x7d, 0x78, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VPBROADCASTB"), 8568, emx_avx2_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), emx_avx2_bytes,
                                                                 BUSTER_ARRAY_LENGTH(emx_avx2_bytes)));

        BusterX86MetadataPhysicalOperand emx_avx_ne_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_mem_base(0, 16, 0),
        };
        u8 emx_avx_ne_bytes[] = {0xc4, 0xe2, 0x7e, 0xb1, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VBCSTNEBF162PS"), 2957, emx_avx_ne_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), emx_avx_ne_bytes,
                                                                 BUSTER_ARRAY_LENGTH(emx_avx_ne_bytes)));

        BusterX86MetadataPhysicalOperand emx_evex_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK, 1, 64),
            x86_64_metadata_test_physical_mem_base(0, 32, 0),
        };
        BusterX86MetadataPhysicalAttributes emx_evex_attributes = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK,
            .has_mask_register = true,
            .mask_register = 1,
        };

        BUSTER_TEST(arguments, x86_64_metadata_test_mem128_forms());
        String8 mem128_features[2] = {S8("avx512f"), S8("avx512vl")};
        BusterX86MetadataPhysicalOperand vpslld_ymm_memory = x86_64_metadata_test_physical_mem_base(0, 0, 16);
        vpslld_ymm_memory.memory.source_width = 128;
        BusterX86MetadataPhysicalOperand vpslld_ymm_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 1, 256),
            vpslld_ymm_memory,
        };
        u8 vpslld_ymm_disp8_bytes[] = {0x62, 0xf1, 0x75, 0x28, 0xf2, 0x40, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VPSLLD"), 6460, vpslld_ymm_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, mem128_features,
                                                                 BUSTER_ARRAY_LENGTH(mem128_features), vpslld_ymm_disp8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vpslld_ymm_disp8_bytes)));
        vpslld_ymm_memory.memory.displacement = 17;
        vpslld_ymm_memory.memory.has_displacement = true;
        vpslld_ymm_operands[2] = vpslld_ymm_memory;
        u8 vpslld_ymm_disp32_bytes[] = {0x62, 0xf1, 0x75, 0x28, 0xf2, 0x80, 0x11, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VPSLLD"), 6460, vpslld_ymm_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, mem128_features,
                                                                 BUSTER_ARRAY_LENGTH(mem128_features), vpslld_ymm_disp32_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vpslld_ymm_disp32_bytes)));

        BusterX86MetadataPhysicalOperand vpsrld_zmm_memory = x86_64_metadata_test_physical_mem_base(0, 0, 32);
        vpsrld_zmm_memory.memory.source_width = 128;
        BusterX86MetadataPhysicalOperand vpsrld_zmm_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            vpsrld_zmm_memory,
        };
        u8 vpsrld_zmm_disp8_bytes[] = {0x62, 0xf1, 0x75, 0x48, 0xd2, 0x40, 0x02};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VPSRLD"), 7725, vpsrld_zmm_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, mem128_features,
                                                                 BUSTER_ARRAY_LENGTH(mem128_features), vpsrld_zmm_disp8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vpsrld_zmm_disp8_bytes)));

        // AVX10.2 AUX BF4 memory forms use an exact four-bit element size
        // while their tuple displacement remains a byte-sized half-vector.
        // Keep the in-repo XED metadata form ids and byte oracles explicit
        // here: LLVM does not yet accept the VCVTBF42HF8 mnemonic.
        String8 bf4_features_128[2] = {S8("avx10.2"), S8("avx10-v1-aux")};
        String8 bf4_features_512[3] = {S8("avx10.2"), S8("avx10-v1-aux"), S8("avx10-512")};
        BusterX86MetadataPhysicalOperand bf4_xmm_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
        };
        BusterX86MetadataPhysicalOperand bf4_ymm_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_mem_base(0, 128, 0),
        };
        BusterX86MetadataPhysicalOperand bf4_zmm_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_mem_base(0, 256, 0),
        };
        u8 bf4_xmm_bytes[] = {0x62, 0xf5, 0x7c, 0x08, 0x37, 0x00};
        u8 bf4_ymm_bytes[] = {0x62, 0xf5, 0x7c, 0x28, 0x37, 0x00};
        u8 bf4_zmm_bytes[] = {0x62, 0xf5, 0x7c, 0x48, 0x37, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3777, bf4_xmm_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, bf4_features_128,
                                                                 BUSTER_ARRAY_LENGTH(bf4_features_128), bf4_xmm_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bf4_xmm_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3779, bf4_ymm_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, bf4_features_128,
                                                                 BUSTER_ARRAY_LENGTH(bf4_features_128), bf4_ymm_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bf4_ymm_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3781, bf4_zmm_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, bf4_features_512,
                                                                 BUSTER_ARRAY_LENGTH(bf4_features_512), bf4_zmm_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bf4_zmm_bytes)));
        BusterX86MetadataPhysicalAttributes bf4_mask = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK,
            .has_mask_register = true,
            .mask_register = 1,
        };
        u8 emx_evex_bytes[] = {0x62, 0xf2, 0x7d, 0x29, 0x18, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VBROADCASTSS"), 5135, emx_evex_operands, 3,
                                                                 emx_evex_attributes, (String8[2]){S8("avx512f"), S8("avx512vl")},
                                                                 2, emx_evex_bytes,
                                                                 BUSTER_ARRAY_LENGTH(emx_evex_bytes)));

        u8 bf4_xmm_mask_bytes[] = {0x62, 0xf5, 0x7c, 0x09, 0x37, 0x00};
        u8 bf4_ymm_mask_bytes[] = {0x62, 0xf5, 0x7c, 0x29, 0x37, 0x00};
        u8 bf4_zmm_mask_bytes[] = {0x62, 0xf5, 0x7c, 0x49, 0x37, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3777, bf4_xmm_operands, 2, bf4_mask,
                                                                 bf4_features_128, BUSTER_ARRAY_LENGTH(bf4_features_128),
                                                                 bf4_xmm_mask_bytes, BUSTER_ARRAY_LENGTH(bf4_xmm_mask_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3779, bf4_ymm_operands, 2, bf4_mask,
                                                                 bf4_features_128, BUSTER_ARRAY_LENGTH(bf4_features_128),
                                                                 bf4_ymm_mask_bytes, BUSTER_ARRAY_LENGTH(bf4_ymm_mask_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3781, bf4_zmm_operands, 2, bf4_mask,
                                                                 bf4_features_512, BUSTER_ARRAY_LENGTH(bf4_features_512),
                                                                 bf4_zmm_mask_bytes, BUSTER_ARRAY_LENGTH(bf4_zmm_mask_bytes)));

        BusterX86MetadataPhysicalOperand bf4_xmm_wrong_width = bf4_xmm_operands[1];
        bf4_xmm_wrong_width.width = 0;
        bf4_xmm_wrong_width.memory.source_width = 128;
        BusterX86MetadataPhysicalOperand bf4_wrong_xmm_operands[2] = {bf4_xmm_operands[0], bf4_xmm_wrong_width};
        BusterX86MetadataPhysicalQuery bf4_wrong_xmm_query = x86_64_metadata_test_physical_query(
            S8("VCVTBF42HF8"), bf4_wrong_xmm_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, bf4_features_128,
            BUSTER_ARRAY_LENGTH(bf4_features_128));
        bf4_wrong_xmm_query.source_semantics = true;
        u8 bf4_wrong_output[16] = {0};
        BusterX86MetadataRelocation bf4_wrong_relocations[2] = {0};
        BusterX86MetadataEmitResult bf4_wrong_xmm = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = bf4_wrong_xmm_query,
            .form_id = 3777,
            .output = bf4_wrong_output,
            .output_capacity = sizeof(bf4_wrong_output),
            .relocations = bf4_wrong_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(bf4_wrong_relocations),
        });
        BUSTER_TEST(arguments, bf4_wrong_xmm.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);

        BusterX86MetadataPhysicalOperand bf4_ymm_wrong_width = bf4_ymm_operands[1];
        bf4_ymm_wrong_width.width = 0;
        bf4_ymm_wrong_width.memory.source_width = 256;
        BusterX86MetadataPhysicalOperand bf4_wrong_ymm_operands[2] = {bf4_ymm_operands[0], bf4_ymm_wrong_width};
        BusterX86MetadataPhysicalQuery bf4_wrong_ymm_query = x86_64_metadata_test_physical_query(
            S8("VCVTBF42HF8"), bf4_wrong_ymm_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, bf4_features_128,
            BUSTER_ARRAY_LENGTH(bf4_features_128));
        bf4_wrong_ymm_query.source_semantics = true;
        BusterX86MetadataEmitResult bf4_wrong_ymm = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = bf4_wrong_ymm_query,
            .form_id = 3779,
            .output = bf4_wrong_output,
            .output_capacity = sizeof(bf4_wrong_output),
            .relocations = bf4_wrong_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(bf4_wrong_relocations),
        });
        BUSTER_TEST(arguments, bf4_wrong_ymm.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);

        BusterX86MetadataPhysicalOperand bf4_zmm_wrong_width = bf4_zmm_operands[1];
        bf4_zmm_wrong_width.width = 0;
        bf4_zmm_wrong_width.memory.source_width = 512;
        BusterX86MetadataPhysicalOperand bf4_wrong_zmm_operands[2] = {bf4_zmm_operands[0], bf4_zmm_wrong_width};
        BusterX86MetadataPhysicalQuery bf4_wrong_zmm_query = x86_64_metadata_test_physical_query(
            S8("VCVTBF42HF8"), bf4_wrong_zmm_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, bf4_features_512,
            BUSTER_ARRAY_LENGTH(bf4_features_512));
        bf4_wrong_zmm_query.source_semantics = true;
        BusterX86MetadataEmitResult bf4_wrong_zmm = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
            .physical = bf4_wrong_zmm_query,
            .form_id = 3781,
            .output = bf4_wrong_output,
            .output_capacity = sizeof(bf4_wrong_output),
            .relocations = bf4_wrong_relocations,
            .relocation_capacity = BUSTER_ARRAY_LENGTH(bf4_wrong_relocations),
        });
        BUSTER_TEST(arguments, bf4_wrong_zmm.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);

        BusterX86MetadataPhysicalOperand bf4_xmm_disp8 = bf4_xmm_operands[1];
        bf4_xmm_disp8.memory.displacement = 8;
        bf4_xmm_disp8.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand bf4_xmm_disp8_operands[2] = {bf4_xmm_operands[0], bf4_xmm_disp8};
        u8 bf4_xmm_disp8_bytes[] = {0x62, 0xf5, 0x7c, 0x08, 0x37, 0x40, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3777, bf4_xmm_disp8_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, bf4_features_128,
                                                                 BUSTER_ARRAY_LENGTH(bf4_features_128), bf4_xmm_disp8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bf4_xmm_disp8_bytes)));
        BusterX86MetadataPhysicalOperand bf4_ymm_disp8 = bf4_ymm_operands[1];
        bf4_ymm_disp8.memory.displacement = 16;
        bf4_ymm_disp8.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand bf4_ymm_disp8_operands[2] = {bf4_ymm_operands[0], bf4_ymm_disp8};
        u8 bf4_ymm_disp8_bytes[] = {0x62, 0xf5, 0x7c, 0x28, 0x37, 0x40, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3779, bf4_ymm_disp8_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, bf4_features_128,
                                                                 BUSTER_ARRAY_LENGTH(bf4_features_128), bf4_ymm_disp8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bf4_ymm_disp8_bytes)));
        BusterX86MetadataPhysicalOperand bf4_zmm_disp8 = bf4_zmm_operands[1];
        bf4_zmm_disp8.memory.displacement = 32;
        bf4_zmm_disp8.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand bf4_zmm_disp8_operands[2] = {bf4_zmm_operands[0], bf4_zmm_disp8};
        u8 bf4_zmm_disp8_bytes[] = {0x62, 0xf5, 0x7c, 0x48, 0x37, 0x40, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3781, bf4_zmm_disp8_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, bf4_features_512,
                                                                 BUSTER_ARRAY_LENGTH(bf4_features_512), bf4_zmm_disp8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bf4_zmm_disp8_bytes)));
        BusterX86MetadataPhysicalOperand bf4_xmm_max_disp8 = bf4_xmm_operands[1];
        bf4_xmm_max_disp8.memory.displacement = 1016;
        bf4_xmm_max_disp8.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand bf4_xmm_max_disp8_operands[2] = {bf4_xmm_operands[0], bf4_xmm_max_disp8};
        u8 bf4_xmm_max_disp8_bytes[] = {0x62, 0xf5, 0x7c, 0x08, 0x37, 0x40, 0x7f};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3777, bf4_xmm_max_disp8_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, bf4_features_128,
                                                                 BUSTER_ARRAY_LENGTH(bf4_features_128), bf4_xmm_max_disp8_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bf4_xmm_max_disp8_bytes)));
        BusterX86MetadataPhysicalOperand bf4_xmm_disp32 = bf4_xmm_operands[1];
        bf4_xmm_disp32.memory.displacement = 1024;
        bf4_xmm_disp32.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand bf4_xmm_disp32_operands[2] = {bf4_xmm_operands[0], bf4_xmm_disp32};
        u8 bf4_xmm_disp32_bytes[] = {0x62, 0xf5, 0x7c, 0x08, 0x37, 0x80, 0x00, 0x04, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VCVTBF42HF8"), 3777, bf4_xmm_disp32_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, bf4_features_128,
                                                                 BUSTER_ARRAY_LENGTH(bf4_features_128), bf4_xmm_disp32_bytes,
                                                                 BUSTER_ARRAY_LENGTH(bf4_xmm_disp32_bytes)));

        BusterX86MetadataPhysicalOperand bf4_full_disp = bf4_xmm_operands[1];
        bf4_full_disp.memory.displacement = 4;
        bf4_full_disp.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand bf4_full_disp_operands[2] = {bf4_xmm_operands[0], bf4_full_disp};
        u8 bf4_full_disp_bytes[] = {0x62, 0xf5, 0x7c, 0x08, 0x37, 0x80, 0x04, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(
                                   S8("VCVTBF42HF8"), 3777, bf4_full_disp_operands, 2,
                                   (BusterX86MetadataPhysicalAttributes){0}, bf4_features_128,
                                   BUSTER_ARRAY_LENGTH(bf4_features_128), bf4_full_disp_bytes,
                                   BUSTER_ARRAY_LENGTH(bf4_full_disp_bytes)));

        BusterX86MetadataPhysicalOperand tile_memory_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM, 0, 1024),
            x86_64_metadata_test_physical_mem_base(16, 32, 0),
        };
        u8 tile_r16_bytes[] = {0x62, 0xfa, 0x7f, 0x08, 0x4b, 0x04, 0x20};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("TILELOADD"), 490, tile_memory_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tile_r16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tile_r16_bytes)));
        BusterX86MetadataPhysicalOperand tile_complex_memory = x86_64_metadata_test_physical_mem_base(24, 32, 64);
        tile_complex_memory.memory.has_index = true;
        tile_complex_memory.memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 25, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        tile_complex_memory.memory.scale = 4;
        tile_complex_memory.memory.has_displacement = true;
        BusterX86MetadataPhysicalOperand tile_complex_operands[2] = {tile_memory_operands[0], tile_complex_memory};
        u8 tile_complex_bytes[] = {0x62, 0x9a, 0x7b, 0x08, 0x4b, 0x44, 0x88, 0x40};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("TILELOADD"), 490, tile_complex_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tile_complex_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tile_complex_bytes)));

        BusterX86MetadataPhysicalOperand high_vvvv[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 16, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
        };
        u8 high_vvvv_bytes[] = {0x62, 0xf1, 0x7c, 0x40, 0x58, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6938, high_vvvv, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), high_vvvv_bytes,
                                                                 BUSTER_ARRAY_LENGTH(high_vvvv_bytes)));

        BusterX86MetadataPhysicalOperand apx_add64[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_imm(1, 64),
        };
        u8 apx_add64_bytes[] = {0x62, 0xf4, 0xfc, 0x08, 0x81, 0xc0, 0x01, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADD"), 607, apx_add64, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), apx_add64_bytes,
                                                                 BUSTER_ARRAY_LENGTH(apx_add64_bytes)));
        BusterX86MetadataPhysicalOperand apx_add16[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 16),
            x86_64_metadata_test_physical_imm(1, 16),
        };
        u8 apx_add16_bytes[] = {0x62, 0xf4, 0x7d, 0x08, 0x81, 0xc0, 0x01, 0x00};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ADD"), 611, apx_add16, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), apx_add16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(apx_add16_bytes)));
        BusterX86MetadataPhysicalOperand andn_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 1, 32),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 2, 32),
        };
        u8 andn_bytes[] = {0x62, 0xf2, 0x7c, 0x08, 0xf2, 0xc2};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("ANDN"), 731, andn_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), andn_bytes,
                                                                 BUSTER_ARRAY_LENGTH(andn_bytes)));

        BusterX86MetadataPhysicalOperand fma4_operands[4] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 2, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 3, 128),
        };
        u8 fma4_bytes[] = {0xc4, 0xe3, 0x71, 0x68, 0xc2, 0x30};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VFMADDPS"), 64, fma4_operands, 4,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), fma4_bytes,
                                                                 BUSTER_ARRAY_LENGTH(fma4_bytes)));
        // AMD's 3DNow rows place the operation selector after ModRM.  Keep
        // that post-ModRM opcode ordering explicit for the register form.
        BusterX86MetadataPhysicalOperand pi2fw_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX, 0, 64),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX, 1, 64),
        };
        u8 pi2fw_bytes[] = {0x0f, 0x0f, 0xc1, 0x0c};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("PI2FW"), 373, pi2fw_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), pi2fw_bytes,
                                                                 BUSTER_ARRAY_LENGTH(pi2fw_bytes)));
        // VPERMIL2 packs its explicit 4-bit immediate into the low nibble of
        // the selector byte; it is not a second trailing byte.
        BusterX86MetadataPhysicalOperand vpermil2_operands[5] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 1, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 2, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 3, 128),
            x86_64_metadata_test_physical_imm(3, 8),
        };
        u8 vpermil2_bytes[] = {0xc4, 0xe3, 0x71, 0x48, 0xc2, 0x33};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VPERMIL2PS"), 160, vpermil2_operands, 5,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vpermil2_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vpermil2_bytes)));
        BusterX86MetadataEmitResult fma4_missing_selector = x86_64_metadata_test_emit_form(
            S8("VFMADDPS"), 64, fma4_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, fma4_missing_selector.status != BUSTER_X86_METADATA_ENCODE_SUCCESS);

        BusterX86MetadataPhysicalOperand gather_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM, 0, 256),
            x86_64_metadata_test_physical_mem_base(0, 64, 0),
        };
        gather_operands[1].memory.vsib = true;
        gather_operands[1].memory.has_index = true;
        gather_operands[1].memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 1, .width = 128, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM};
        BusterX86MetadataPhysicalAttributes gather_attributes = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK, .has_mask_register = true, .mask_register = 1,
        };
        u8 gather_bytes[] = {0x62, 0xf2, 0xfd, 0x29, 0x92, 0x04, 0x08};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VGATHERDPD"), 5508, gather_operands, 2, gather_attributes,
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), gather_bytes,
                                                                 BUSTER_ARRAY_LENGTH(gather_bytes)));
        gather_operands[1].memory.index.index = 4;
        u8 gather_index4_bytes[] = {0x62, 0xf2, 0xfd, 0x29, 0x92, 0x04, 0x20};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VGATHERDPD"), 5508, gather_operands, 2, gather_attributes,
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), gather_index4_bytes,
                                                                 BUSTER_ARRAY_LENGTH(gather_index4_bytes)));
        gather_operands[1].memory.index.index = 17;
        u8 gather_high_bytes[] = {0x62, 0xf2, 0xfd, 0x21, 0x92, 0x04, 0x08};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VGATHERDPD"), 5508, gather_operands, 2, gather_attributes,
                                                                 wildcard, BUSTER_ARRAY_LENGTH(wildcard), gather_high_bytes,
                                                                 BUSTER_ARRAY_LENGTH(gather_high_bytes)));

        // AVX2 gather rows carry the mask in REG0/ModRM.reg, the destination
        // in REG1/VEX.vvvv, and the independent VSIB index in memory.index.
        // Keep all four aliases distinct so both VEX.L and VEX.vvvv routing
        // are covered (the YMM-index forms intentionally have XMM outputs).
        BusterX86MetadataPhysicalOperand vex_gather_ymm_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 2, 128),
            x86_64_metadata_test_physical_mem_base(0, 32, 0),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 3, 128),
        };
        vex_gather_ymm_operands[1].memory.vsib = true;
        vex_gather_ymm_operands[1].memory.has_index = true;
        vex_gather_ymm_operands[1].memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 1, .width = 256, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM};
        u8 vgatherqps_ymm_bytes[] = {0xc4, 0xe2, 0x65, 0x93, 0x14, 0x08};
        u8 vpgatherqd_ymm_bytes[] = {0xc4, 0xe2, 0x65, 0x91, 0x14, 0x08};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VGATHERQPS"), 8311, vex_gather_ymm_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vgatherqps_ymm_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vgatherqps_ymm_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VPGATHERQD"), 8319, vex_gather_ymm_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vpgatherqd_ymm_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vpgatherqd_ymm_bytes)));

        BusterX86MetadataPhysicalOperand vex_gather_xmm_operands[3] = {
            vex_gather_ymm_operands[0], vex_gather_ymm_operands[1], vex_gather_ymm_operands[2]};
        vex_gather_xmm_operands[1].memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 1, .width = 128, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM};
        u8 vgatherqps_xmm_bytes[] = {0xc4, 0xe2, 0x61, 0x93, 0x14, 0x08};
        u8 vpgatherqd_xmm_bytes[] = {0xc4, 0xe2, 0x61, 0x91, 0x14, 0x08};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VGATHERQPS"), 8312, vex_gather_xmm_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vgatherqps_xmm_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vgatherqps_xmm_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VPGATHERQD"), 8320, vex_gather_xmm_operands, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), vpgatherqd_xmm_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vpgatherqd_xmm_bytes)));

        BusterX86MetadataPhysicalOperand wrong_vsib_class_operands[3] = {
            vex_gather_ymm_operands[0], vex_gather_ymm_operands[1], vex_gather_ymm_operands[2]};
        wrong_vsib_class_operands[1].memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 1, .width = 128, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM};
        BusterX86MetadataEmitResult wrong_vsib_class = x86_64_metadata_test_emit_form(
            S8("VGATHERQPS"), 8311, wrong_vsib_class_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, wrong_vsib_class.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING);

        BusterX86MetadataPhysicalOperand non_vsib_index_operands[3] = {
            vex_gather_ymm_operands[0], vex_gather_ymm_operands[1], vex_gather_ymm_operands[2]};
        non_vsib_index_operands[1].memory.vsib = false;
        non_vsib_index_operands[1].memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 1, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        BusterX86MetadataEmitResult non_vsib_index = x86_64_metadata_test_emit_form(
            S8("VGATHERQPS"), 8311, non_vsib_index_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, non_vsib_index.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING);

        BusterX86MetadataPhysicalOperand missing_vsib_index_operands[3] = {
            vex_gather_ymm_operands[0], vex_gather_ymm_operands[1], vex_gather_ymm_operands[2]};
        missing_vsib_index_operands[1].memory.has_index = false;
        BusterX86MetadataEmitResult missing_vsib_index = x86_64_metadata_test_emit_form(
            S8("VGATHERQPS"), 8311, missing_vsib_index_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, missing_vsib_index.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand high_vsib_index_operands[3] = {
            vex_gather_ymm_operands[0], vex_gather_ymm_operands[1], vex_gather_ymm_operands[2]};
        high_vsib_index_operands[1].memory.index.index = 16;
        BusterX86MetadataEmitResult high_vsib_index = x86_64_metadata_test_emit_form(
            S8("VGATHERQPS"), 8311, high_vsib_index_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, high_vsib_index.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING);

        BusterX86MetadataPhysicalOperand ordinary_vsib = vaddps_memory_operands[2];
        ordinary_vsib.memory.vsib = true;
        ordinary_vsib.memory.has_index = true;
        ordinary_vsib.memory.index = (BusterX86MetadataPhysicalRegister){
            .index = 1, .width = 256, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM};
        BusterX86MetadataPhysicalOperand ordinary_vsib_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], ordinary_vsib};
        BusterX86MetadataEmitResult ordinary_vsib_result = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, ordinary_vsib_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ordinary_vsib_result.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING);
        ordinary_vsib.memory.has_index = false;
        BusterX86MetadataPhysicalOperand vsib_without_index_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], ordinary_vsib};
        BusterX86MetadataEmitResult vsib_without_index = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, vsib_without_index_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, vsib_without_index.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand tuple_disp1[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], x86_64_metadata_test_physical_mem_base(0, 32, 1)};
        BusterX86MetadataPhysicalOperand tuple_disp64[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], x86_64_metadata_test_physical_mem_base(0, 32, 64)};
        u8 tuple_disp1_bytes[] = {0x62, 0xf1, 0x74, 0x48, 0x58, 0x80, 0x01, 0x00, 0x00, 0x00};
        u8 tuple_disp64_bytes[] = {0x62, 0xf1, 0x74, 0x48, 0x58, 0x40, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, tuple_disp1, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tuple_disp1_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tuple_disp1_bytes)));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VADDPS"), 6940, tuple_disp64, 3,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), tuple_disp64_bytes,
                                                                 BUSTER_ARRAY_LENGTH(tuple_disp64_bytes)));
        BusterX86MetadataPhysicalOperand tuple_symbol_memory = x86_64_metadata_test_physical_mem_base(0, 32, 0);
        tuple_symbol_memory.memory.symbol = S8("data");
        tuple_symbol_memory.memory.addend = 7;
        tuple_symbol_memory.memory.has_symbol = true;
        BusterX86MetadataPhysicalOperand tuple_symbol_operands[3] = {
            vaddps_memory_operands[0], vaddps_memory_operands[1], tuple_symbol_memory};
        u8 tuple_symbol_bytes[32] = {0};
        BusterX86MetadataRelocation tuple_symbol_relocations[2] = {0};
        BusterX86MetadataEmitResult tuple_symbol = x86_64_metadata_test_emit_form(
            S8("VADDPS"), 6940, tuple_symbol_operands, 3, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), tuple_symbol_bytes, BUSTER_ARRAY_LENGTH(tuple_symbol_bytes), tuple_symbol_relocations,
            BUSTER_ARRAY_LENGTH(tuple_symbol_relocations));
        BUSTER_TEST(arguments, tuple_symbol.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && tuple_symbol.byte_count == 10 &&
                                   tuple_symbol.relocation_count == 1 && tuple_symbol_relocations[0].offset == 6 &&
                                   tuple_symbol_relocations[0].width == 4 &&
                                   tuple_symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_SIGN_EXTENDED &&
                                   tuple_symbol_relocations[0].addend == 7);

        BusterX86MetadataPhysicalOperand addr32_symbol_memory = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
            .width = 8,
            .memory = {.address_size = 32, .scale = 1, .symbol = S8("data"), .has_symbol = true},
        };
        BusterX86MetadataPhysicalOperand addr32_symbol_operands[2] = {
            addr32_symbol_memory, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataPhysicalQuery addr32_symbol_query = x86_64_metadata_test_physical_query(
            S8("MOV"), addr32_symbol_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        addr32_symbol_query.address_size = 32;
        BusterX86MetadataRelocation addr32_symbol_relocations[2] = {0};
        BusterX86MetadataEmitResult addr32_symbol_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = addr32_symbol_query, .form_id = 9532, .output = (u8[32]){0},
                                         .output_capacity = 32, .relocations = addr32_symbol_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(addr32_symbol_relocations)});
        BusterX86MetadataPhysicalOperand addr64_symbol_memory = addr32_symbol_memory;
        addr64_symbol_memory.memory.address_size = 64;
        BusterX86MetadataPhysicalOperand addr64_symbol_operands[2] = {
            addr64_symbol_memory, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataPhysicalQuery addr64_symbol_query = x86_64_metadata_test_physical_query(
            S8("MOV"), addr64_symbol_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataRelocation addr64_symbol_relocations[2] = {0};
        BusterX86MetadataEmitResult addr64_symbol_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = addr64_symbol_query, .form_id = 9532, .output = (u8[32]){0},
                                         .output_capacity = 32, .relocations = addr64_symbol_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(addr64_symbol_relocations)});
        BUSTER_TEST(arguments, addr32_symbol_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   addr32_symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_ZERO_EXTENDED &&
                                   addr64_symbol_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   addr64_symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_SIGN_EXTENDED);
        BusterX86MetadataPhysicalOperand overflowing_memory = addr64_symbol_memory;
        overflowing_memory.memory.addend = INT64_MAX;
        overflowing_memory.memory.displacement = 1;
        BusterX86MetadataPhysicalOperand overflowing_operands[2] = {
            overflowing_memory, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataRelocation overflowing_relocations[2] = {0};
        BusterX86MetadataEmitResult overflowing_addend = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, overflowing_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[32]){0}, 32, overflowing_relocations,
            BUSTER_ARRAY_LENGTH(overflowing_relocations));
        BUSTER_TEST(arguments, overflowing_addend.status == BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE &&
                                   overflowing_addend.diagnostic_value == INT64_MAX);

        BusterX86MetadataPhysicalOperand addr32_memory = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
            .width = 8,
            .memory = {.address_size = 32, .scale = 1, .displacement = (s64)UINT32_C(0xfffffff0), .has_displacement = true},
        };
        BusterX86MetadataPhysicalOperand addr32_operands[2] = {
            addr32_memory, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataPhysicalQuery addr32_query = x86_64_metadata_test_physical_query(
            S8("MOV"), addr32_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        addr32_query.address_size = 32;
        u8 addr32_bytes[32] = {0};
        BusterX86MetadataEmitResult addr32_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = addr32_query, .form_id = 9532, .output = addr32_bytes,
                                         .output_capacity = BUSTER_ARRAY_LENGTH(addr32_bytes)});
        BUSTER_TEST(arguments, addr32_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && addr32_emit.byte_count == 9 &&
                                   x86_64_metadata_test_bytes_equal(addr32_bytes, addr32_emit.byte_count,
                                                                     (u8[]){0x67, 0xc6, 0x04, 0x25, 0xf0, 0xff, 0xff, 0xff, 0x01}, 9));

        BusterX86MetadataForm evv_form = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form(463, &evv_form) &&
                                   evv_form.prefix_kind == BUSTER_X86_METADATA_PREFIX_EVEX);

        // BSRINIT's VV1/norexr_prefix row is normalized to the VEX family;
        // keep the exact token checks beside the VEX byte checks so a future
        // broad substring match cannot silently lose the typed constraint.
        BusterX86MetadataForm vv1_norexr_form = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form(30, &vv1_norexr_form) &&
                                   vv1_norexr_form.prefix_kind == BUSTER_X86_METADATA_PREFIX_VEX &&
                                   vv1_norexr_form.encoder_family == BUSTER_X86_METADATA_ENCODER_VEX &&
                                   x86_64_metadata_test_pattern_has_token(vv1_norexr_form.pattern, S8("VV1")) &&
                                   x86_64_metadata_test_pattern_has_token(vv1_norexr_form.pattern, S8("norexr_prefix")) &&
                                   !x86_64_metadata_test_pattern_has_token(vv1_norexr_form.pattern, S8("norexw_prefix")));


        BusterX86MetadataPhysicalQuery system_query = x86_64_metadata_test_physical_query(
            S8("HLT"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        u8 system_bytes[32] = {0};
        BusterX86MetadataEmitResult system_default = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = system_query, .form_id = 10069, .output = system_bytes, .output_capacity = sizeof(system_bytes)});
        system_query.include_privileged = true;
        BusterX86MetadataEmitResult system_opt_in = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = system_query, .form_id = 10069, .output = system_bytes, .output_capacity = sizeof(system_bytes)});
        BUSTER_TEST(arguments, system_default.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   system_default.required_feature.length == 0);
        BUSTER_TEST(arguments, system_opt_in.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && system_opt_in.byte_count == 1 &&
                                   system_bytes[0] == 0xf4 && system_opt_in.required_feature.length == 0);
    }

    {
        String8 wildcard[1] = {S8("*")};
        String8 svm_features[1] = {S8("svm")};
        // Fixed REG/RM fields and fixed implicit registers are emitted from
        // the schema even when no dynamic binding owns that field.
        u8 vmmcall_bytes[8] = {0};
        BusterX86MetadataEmitResult vmmcall = x86_64_metadata_test_emit_form(
            S8("VMMCALL"), 448, 0, 0, (BusterX86MetadataPhysicalAttributes){0}, svm_features, BUSTER_ARRAY_LENGTH(svm_features),
            vmmcall_bytes, sizeof(vmmcall_bytes), 0, 0);
        BUSTER_TEST(arguments, vmmcall.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && vmmcall.byte_count == 3 &&
                                   vmmcall_bytes[0] == 0x0f && vmmcall_bytes[1] == 0x01 && vmmcall_bytes[2] == 0xd9);
        BusterX86MetadataPhysicalOperand div_operand =
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64);
        u8 div_bytes[] = {0x48, 0xf7, 0xf0};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("DIV"), 9468, &div_operand, 1,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), div_bytes,
                                                                 BUSTER_ARRAY_LENGTH(div_bytes)));

        BusterX86MetadataPhysicalOperand fixed_al_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8),
            x86_64_metadata_test_physical_imm(1, 8),
        };
        BusterX86MetadataPhysicalQuery fixed_al_query = x86_64_metadata_test_physical_query(
            S8("ADD"), fixed_al_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        // A hidden fixed AL is introspectable only when callers opt into the
        // complete implicit operand list; source matching must not make an
        // ordinary ADD AL spelling look like an extra source operand.
        u8 fixed_source_bytes[32] = {0};
        BusterX86MetadataEmitResult fixed_al_source = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = fixed_al_query, .form_id = 9625, .output = fixed_source_bytes,
                                         .output_capacity = sizeof(fixed_source_bytes)});
        fixed_al_query.include_implicit = true;
        u8 fixed_bytes[32] = {0};
        BusterX86MetadataEmitResult fixed_al = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = fixed_al_query, .form_id = 9625, .output = fixed_bytes, .output_capacity = sizeof(fixed_bytes)});
        fixed_al_operands[0] = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 3, 8);
        BusterX86MetadataEmitResult fixed_bl = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = fixed_al_query, .form_id = 9625, .output = fixed_bytes, .output_capacity = sizeof(fixed_bytes)});
        BUSTER_TEST(arguments, fixed_al_source.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
        BUSTER_TEST(arguments, fixed_al.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && fixed_al.byte_count == 2 && fixed_bytes[0] == 0x04 &&
                                   fixed_bytes[1] == 0x01);
        BUSTER_TEST(arguments, fixed_bl.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);

        BusterX86MetadataPhysicalOperand low_byte_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 4, 8),
            x86_64_metadata_test_physical_imm(1, 8),
        };
        u8 low_byte_rex[] = {0x40, 0xb4, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10017, low_byte_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), low_byte_rex,
                                                                 BUSTER_ARRAY_LENGTH(low_byte_rex)));
        low_byte_operands[0].reg.high_byte = true;
        u8 high_byte_no_rex[] = {0xb4, 0x01};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("MOV"), 10017, low_byte_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), high_byte_no_rex,
                                                                 BUSTER_ARRAY_LENGTH(high_byte_no_rex)));
        BusterX86MetadataPhysicalOperand high_byte_apx[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 4, 8),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 8, 8),
        };
        high_byte_apx[0].reg.high_byte = true;
        BusterX86MetadataEmitResult high_byte_rex = x86_64_metadata_test_emit_form(
            S8("ADD"), 547, high_byte_apx, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, high_byte_rex.status == BUSTER_X86_METADATA_ENCODE_HIGH_BYTE_WITH_REX);
    }

    {
        String8 wildcard[1] = {S8("*")};
        // Relocations are format-neutral and use the final instruction end
        // as P for every PC-relative width, including trailing immediates.
        BusterX86MetadataPhysicalOperand branch_literal = x86_64_metadata_test_physical_relative(0, 8);
        BusterX86MetadataPhysicalQuery branch_literal_query = x86_64_metadata_test_physical_query(
            S8("JMP"), &branch_literal, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult branch_literal_select = buster_x86_metadata_select_form(branch_literal_query);
        u8 branch_literal_bytes[32] = {0};
        BusterX86MetadataEmitResult branch_literal_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = branch_literal_query, .form_id = branch_literal_select.form_id,
                                         .output = branch_literal_bytes, .output_capacity = sizeof(branch_literal_bytes)});
        BUSTER_TEST(arguments, branch_literal_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   branch_literal_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && branch_literal_emit.byte_count == 2 &&
                                   branch_literal_emit.relocation_count == 0 && branch_literal_bytes[0] == 0xeb && branch_literal_bytes[1] == 0);

        BusterX86MetadataPhysicalOperand branch = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE, .width = 8, .symbol = S8("target"), .addend = 7,
            .has_symbol = true,
        };
        BusterX86MetadataPhysicalQuery branch_query = x86_64_metadata_test_physical_query(
            S8("JMP"), &branch, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult branch_select = buster_x86_metadata_select_form(branch_query);
        u8 branch_bytes[32] = {0};
        BusterX86MetadataRelocation branch_relocations[2] = {0};
        BusterX86MetadataEmitResult branch_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = branch_query, .form_id = branch_select.form_id, .output = branch_bytes,
                                         .output_capacity = sizeof(branch_bytes), .relocations = branch_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(branch_relocations)});
        BUSTER_TEST(arguments, branch_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && branch_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   branch_emit.byte_count == 2 && branch_emit.relocation_count == 1 && branch_relocations[0].offset == 1 &&
                                   branch_relocations[0].width == 1 && branch_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC8 &&
                                   branch_relocations[0].addend == 6);

        BusterX86MetadataPhysicalOperand branch32 = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE, .width = 32, .symbol = S8("target"),
            .addend = 0, .has_symbol = true,
        };
        BusterX86MetadataPhysicalQuery branch32_query = x86_64_metadata_test_physical_query(
            S8("JMP"), &branch32, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult branch32_select = buster_x86_metadata_select_form(branch32_query);
        u8 branch32_bytes[32] = {0};
        BusterX86MetadataRelocation branch32_relocations[2] = {0};
        BusterX86MetadataEmitResult branch32_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = branch32_query, .form_id = branch32_select.form_id,
                                         .output = branch32_bytes, .output_capacity = sizeof(branch32_bytes),
                                         .relocations = branch32_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(branch32_relocations)});
        BUSTER_TEST(arguments, branch32_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && branch32_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   branch32_emit.byte_count == 5 && branch32_relocations[0].offset == 1 &&
                                   branch32_relocations[0].width == 4 && branch32_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 &&
                                   branch32_relocations[0].addend == -4);

        // BRANCH_HINT() is a typed legacy CS/DS prefix control.  The raw
        // cohort's ordinary spelling remains unprefixed, while CS and DS
        // select the architecturally defined not-taken/taken hints.
        BusterX86MetadataPhysicalOperand jz_short = x86_64_metadata_test_physical_relative(0, 8);
        BusterX86MetadataPhysicalAttributes no_hint = {0};
        BusterX86MetadataPhysicalAttributes not_taken = {.branch_hint = BUSTER_X86_METADATA_BRANCH_HINT_NOT_TAKEN};
        BusterX86MetadataPhysicalAttributes taken = {.branch_hint = BUSTER_X86_METADATA_BRANCH_HINT_TAKEN};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JZ"), 9805, &jz_short, 1, no_hint, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), (u8[]){0x74, 0x00}, 2));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JZ"), 9805, &jz_short, 1, not_taken, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), (u8[]){0x2e, 0x74, 0x00}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JZ"), 9805, &jz_short, 1, taken, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), (u8[]){0x3e, 0x74, 0x00}, 3));
        BusterX86MetadataPhysicalOperand jz_short_min = x86_64_metadata_test_physical_relative(-128, 8);
        BusterX86MetadataPhysicalOperand jz_short_max = x86_64_metadata_test_physical_relative(127, 8);
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JZ"), 9805, &jz_short_min, 1, not_taken, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), (u8[]){0x2e, 0x74, 0x80}, 3));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JZ"), 9805, &jz_short_max, 1, taken, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard), (u8[]){0x3e, 0x74, 0x7f}, 3));
        BusterX86MetadataPhysicalOperand jz_short_below = x86_64_metadata_test_physical_relative(-129, 8);
        BusterX86MetadataPhysicalOperand jz_short_above = x86_64_metadata_test_physical_relative(128, 8);
        BUSTER_TEST(arguments, buster_x86_metadata_emit_form(
                                   (BusterX86MetadataEmitQuery){.physical = x86_64_metadata_test_physical_query(
                                                                    S8("JZ"), &jz_short_below, 1, not_taken, wildcard,
                                                                    BUSTER_ARRAY_LENGTH(wildcard)),
                                                                .form_id = 9805, .output = (u8[8]){0}, .output_capacity = 8})
                                   .status == BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE &&
                               x86_64_metadata_test_emit_form(
                                   S8("JZ"), 9805, &jz_short_above, 1, taken, wildcard, BUSTER_ARRAY_LENGTH(wildcard),
                                   (u8[8]){0}, 8, 0, 0)
                                       .status == BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE);
        BusterX86MetadataPhysicalOperand jz_near = x86_64_metadata_test_physical_relative(0, 32);
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JZ"), 10249, &jz_near, 1, no_hint, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard),
                                                                 (u8[]){0x0f, 0x84, 0x00, 0x00, 0x00, 0x00}, 6));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JZ"), 10249, &jz_near, 1, not_taken, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard),
                                                                 (u8[]){0x2e, 0x0f, 0x84, 0x00, 0x00, 0x00, 0x00}, 7));
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("JZ"), 10249, &jz_near, 1, taken, wildcard,
                                                                 BUSTER_ARRAY_LENGTH(wildcard),
                                                                 (u8[]){0x3e, 0x0f, 0x84, 0x00, 0x00, 0x00, 0x00}, 7));
        BusterX86MetadataPhysicalOperand jz_symbol = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
            .width = 8,
            .symbol = S8("target"),
            .has_symbol = true,
        };
        BusterX86MetadataRelocation jz_symbol_relocations[2] = {0};
        BusterX86MetadataPhysicalQuery jz_symbol_query = x86_64_metadata_test_physical_query(
            S8("JZ"), &jz_symbol, 1, not_taken, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        u8 jz_symbol_bytes[8] = {0};
        BusterX86MetadataEmitResult jz_symbol_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = jz_symbol_query, .form_id = 9805, .output = jz_symbol_bytes,
                                         .output_capacity = sizeof(jz_symbol_bytes), .relocations = jz_symbol_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(jz_symbol_relocations)});
        BUSTER_TEST(arguments, jz_symbol_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && jz_symbol_emit.byte_count == 3 &&
                                   jz_symbol_emit.relocation_count == 1 && jz_symbol_relocations[0].offset == 2 &&
                                   jz_symbol_relocations[0].width == 1 && jz_symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC8 &&
                                   jz_symbol_relocations[0].addend == -1);
        BusterX86MetadataFormKey jz_key = {0};
        u8 jz_exact_query_bytes[8] = {0};
        BusterX86MetadataRelocation jz_exact_query_relocations[2] = {0};
        BusterX86MetadataEmitResult jz_exact_query_emit = buster_x86_metadata_form_key(9805, &jz_key)
                                                               ? x86_64_metadata_test_emit_exact_query(
                                                                     jz_key, &jz_symbol, 1, not_taken, wildcard,
                                                                     BUSTER_ARRAY_LENGTH(wildcard), jz_exact_query_bytes,
                                                                     BUSTER_ARRAY_LENGTH(jz_exact_query_bytes),
                                                                     jz_exact_query_relocations,
                                                                     BUSTER_ARRAY_LENGTH(jz_exact_query_relocations))
                                                               : (BusterX86MetadataEmitResult){0};
        BUSTER_TEST(arguments, jz_exact_query_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   jz_exact_query_emit.byte_count == jz_symbol_emit.byte_count &&
                                   jz_exact_query_emit.relocation_count == jz_symbol_emit.relocation_count &&
                                   x86_64_metadata_test_bytes_equal(jz_exact_query_bytes, jz_exact_query_emit.byte_count, jz_symbol_bytes,
                                                                     jz_symbol_emit.byte_count) &&
                                   jz_exact_query_relocations[0].offset == 2 && jz_exact_query_relocations[0].width == 1 &&
                                   jz_exact_query_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC8 &&
                                   jz_exact_query_relocations[0].symbol.length == jz_symbol_relocations[0].symbol.length &&
                                   memcmp(jz_exact_query_relocations[0].symbol.pointer, jz_symbol_relocations[0].symbol.pointer,
                                          jz_exact_query_relocations[0].symbol.length) == 0);
        BusterX86MetadataPhysicalOperand jz_near_symbol = {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
            .width = 32,
            .symbol = S8("target32"),
            .has_symbol = true,
        };
        BusterX86MetadataRelocation jz_near_symbol_relocations[2] = {0};
        BusterX86MetadataPhysicalQuery jz_near_symbol_query = x86_64_metadata_test_physical_query(
            S8("JZ"), &jz_near_symbol, 1, taken, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        u8 jz_near_symbol_bytes[8] = {0};
        BusterX86MetadataEmitResult jz_near_symbol_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = jz_near_symbol_query, .form_id = 10249, .output = jz_near_symbol_bytes,
                                         .output_capacity = sizeof(jz_near_symbol_bytes),
                                         .relocations = jz_near_symbol_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(jz_near_symbol_relocations)});
        BUSTER_TEST(arguments, jz_near_symbol_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   jz_near_symbol_emit.byte_count == 7 && jz_near_symbol_emit.relocation_count == 1 &&
                                   jz_near_symbol_relocations[0].offset == 3 && jz_near_symbol_relocations[0].width == 4 &&
                                   jz_near_symbol_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 &&
                                   jz_near_symbol_relocations[0].addend == -4);
        BusterX86MetadataPhysicalQuery jz_bad_hint_query = x86_64_metadata_test_physical_query(
            S8("JZ"), &jz_short, 1, (BusterX86MetadataPhysicalAttributes){.branch_hint = BUSTER_X86_METADATA_BRANCH_HINT_COUNT}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BUSTER_TEST(arguments, buster_x86_metadata_select_form(jz_bad_hint_query).status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);
        BusterX86MetadataPhysicalQuery mov_bad_hint_query = x86_64_metadata_test_physical_query(
            S8("MOV"), 0, 0, not_taken, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BUSTER_TEST(arguments, buster_x86_metadata_select_form(mov_bad_hint_query).status != BUSTER_X86_METADATA_ENCODE_SUCCESS);
        BusterX86MetadataPhysicalOperand branch_auto = x86_64_metadata_test_physical_relative(0, 0);
        BusterX86MetadataPhysicalQuery branch_auto_query = x86_64_metadata_test_physical_query(
            S8("JMP"), &branch_auto, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult branch_auto_select = buster_x86_metadata_select_form(branch_auto_query);
        BUSTER_TEST(arguments, branch_auto_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   branch_auto_select.selected_byte_count == 2);
        BusterX86MetadataEmitResult branch_direct_width_mismatch = x86_64_metadata_test_emit_form(
            S8("JMP"), branch_literal_select.form_id, &branch32, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[32]){0}, 32, 0, 0);
        BUSTER_TEST(arguments, branch_direct_width_mismatch.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);

        BusterX86MetadataPhysicalOperand rip_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
            x86_64_metadata_test_physical_mem_rip(S8("data"), 3, 64),
        };
        BusterX86MetadataPhysicalQuery rip_query = x86_64_metadata_test_physical_query(
            S8("MOV"), rip_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult rip_select = buster_x86_metadata_select_form(rip_query);
        u8 rip_bytes[32] = {0};
        BusterX86MetadataRelocation rip_relocations[2] = {0};
        BusterX86MetadataEmitResult rip_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = rip_query, .form_id = rip_select.form_id, .output = rip_bytes,
                                         .output_capacity = sizeof(rip_bytes), .relocations = rip_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(rip_relocations)});
        BUSTER_TEST(arguments, rip_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && rip_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   rip_emit.byte_count == 7 && rip_emit.relocation_count == 1 && rip_relocations[0].offset == 3 &&
                                   rip_relocations[0].width == 4 && rip_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 &&
                                   rip_relocations[0].addend == -1);

        BusterX86MetadataPhysicalOperand trailing_operands[2] = {
            x86_64_metadata_test_physical_mem_rip(S8("data"), 3, 64),
            x86_64_metadata_test_physical_imm(127, 8),
        };
        BusterX86MetadataPhysicalQuery trailing_query = x86_64_metadata_test_physical_query(
            S8("ADD"), trailing_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult trailing_select = buster_x86_metadata_select_form(trailing_query);
        u8 trailing_bytes[32] = {0};
        BusterX86MetadataRelocation trailing_relocations[2] = {0};
        BusterX86MetadataEmitResult trailing_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = trailing_query, .form_id = trailing_select.form_id, .output = trailing_bytes,
                                         .output_capacity = sizeof(trailing_bytes), .relocations = trailing_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(trailing_relocations)});
        // The qword memory operand supplies the data width for ADD's
        // variable-width r/m form, so REX.W precedes ModRM and shifts the
        // trailing PC32 relocation to offset 3.
        BUSTER_TEST(arguments, trailing_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && trailing_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   trailing_emit.relocation_count == 1 && trailing_relocations[0].offset == 3 &&
                                   trailing_relocations[0].width == 4 && trailing_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_PC32 &&
                                   trailing_relocations[0].addend == -2);

        BusterX86MetadataPhysicalOperand absolute = x86_64_metadata_test_physical_absolute(S8("target"), 64);
        BusterX86MetadataPhysicalQuery absolute_query = x86_64_metadata_test_physical_query(
            S8("JMPABS"), &absolute, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        u8 absolute_bytes[32] = {0};
        BusterX86MetadataRelocation absolute_relocations[2] = {0};
        BusterX86MetadataEmitResult absolute_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = absolute_query, .form_id = 2931, .output = absolute_bytes,
                                         .output_capacity = sizeof(absolute_bytes), .relocations = absolute_relocations,
                                         .relocation_capacity = BUSTER_ARRAY_LENGTH(absolute_relocations)});
        BUSTER_TEST(arguments, absolute_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && absolute_emit.byte_count == 11 &&
                                   absolute_emit.relocation_count == 1 && absolute_relocations[0].offset == 3 &&
                                   absolute_relocations[0].width == 8 && absolute_relocations[0].kind == BUSTER_X86_METADATA_RELOCATION_ABSOLUTE64 &&
                                   absolute_relocations[0].addend == 0);
        BusterX86MetadataPhysicalOperand absolute32 = x86_64_metadata_test_physical_absolute(S8("target"), 32);
        BusterX86MetadataEmitResult absolute_width_mismatch = x86_64_metadata_test_emit_form(
            S8("JMPABS"), 2931, &absolute32, 1, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[32]){0}, 32, 0, 0);
        BUSTER_TEST(arguments, absolute_width_mismatch.status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH);
    }

    {
        String8 wildcard[1] = {S8("*")};
        String8 avx512_features[2] = {S8("avx512f"), S8("avx512vl")};
        String8 avx512_apx_features[3] = {S8("avx512f"), S8("avx512vl"), S8("apx")};
        BusterX86MetadataPhysicalOperand vector16_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 16, 128),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 17, 128),
        };
        u8 vector16_bytes[] = {0x62, 0xa1, 0x7d, 0x08, 0x6f, 0xc1};
        BUSTER_TEST(arguments, x86_64_metadata_test_emit_exact(S8("VMOVDQA32"), 5583, vector16_operands, 2,
                                                                 (BusterX86MetadataPhysicalAttributes){0}, avx512_features,
                                                                 BUSTER_ARRAY_LENGTH(avx512_features), vector16_bytes,
                                                                 BUSTER_ARRAY_LENGTH(vector16_bytes)));

        BusterX86MetadataPhysicalOperand egpr_memory_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128),
            x86_64_metadata_test_physical_mem_base(16, 32, 0),
        };
        BusterX86MetadataEmitResult egpr_without_apx = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, egpr_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, avx512_features,
            BUSTER_ARRAY_LENGTH(avx512_features), (u8[1]){0}, 0, 0, 0);
        u8 egpr_bytes[32] = {0};
        BusterX86MetadataEmitResult egpr_with_apx = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, egpr_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, avx512_apx_features,
            BUSTER_ARRAY_LENGTH(avx512_apx_features), egpr_bytes, sizeof(egpr_bytes), 0, 0);
        BUSTER_TEST(arguments, egpr_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);
        BUSTER_TEST(arguments, egpr_with_apx.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && egpr_with_apx.byte_count == 6 &&
                                   x86_64_metadata_test_bytes_equal(egpr_bytes, egpr_with_apx.byte_count,
                                                                     (u8[]){0x62, 0xf9, 0x7d, 0x08, 0x6f, 0x00}, 6));
        egpr_memory_operands[1] = x86_64_metadata_test_physical_mem_base(31, 32, 0);
        BusterX86MetadataEmitResult egpr31_without_apx = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, egpr_memory_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, avx512_features,
            BUSTER_ARRAY_LENGTH(avx512_features), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, egpr31_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE);

        BusterX86MetadataPhysicalOperand legacy_base16[2] = {
            x86_64_metadata_test_physical_mem_base(16, 8, 0), x86_64_metadata_test_physical_imm(1, 8),
        };
        u8 legacy_egpr_bytes[8] = {0};
        BusterX86MetadataEmitResult legacy_egpr = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, legacy_base16, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), legacy_egpr_bytes, BUSTER_ARRAY_LENGTH(legacy_egpr_bytes), 0, 0);
        BusterX86MetadataEmitResult legacy_egpr_without_apx = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, legacy_base16, 2, (BusterX86MetadataPhysicalAttributes){0}, 0, 0, (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, legacy_egpr_without_apx.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                                   x86_64_metadata_test_string_equal(legacy_egpr_without_apx.required_feature, S8("APX_F")));
        BUSTER_TEST(arguments, legacy_egpr.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && legacy_egpr.byte_count == 5 &&
                                   x86_64_metadata_test_bytes_equal(legacy_egpr_bytes, legacy_egpr.byte_count,
                                                                     (u8[]){0xd5, 0x10, 0xc6, 0x00, 0x01}, 5));
        BusterX86MetadataPhysicalOperand malformed_base_width = x86_64_metadata_test_physical_mem_base(3, 8, 0);
        malformed_base_width.memory.base.width = 8;
        BusterX86MetadataEmitResult malformed_base_width_result = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532,
            (BusterX86MetadataPhysicalOperand[]){malformed_base_width, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        malformed_base_width.memory.base.width = 64;
        malformed_base_width.memory.base.high_byte = true;
        BusterX86MetadataEmitResult malformed_base_high_byte = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532,
            (BusterX86MetadataPhysicalOperand[]){malformed_base_width, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand malformed_index_width = x86_64_metadata_test_physical_mem_index(1, 2, 8, 0);
        malformed_index_width.memory.index.width = 8;
        BusterX86MetadataEmitResult malformed_index_width_result = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532,
            (BusterX86MetadataPhysicalOperand[]){malformed_index_width, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        malformed_index_width.memory.index.width = 64;
        malformed_index_width.memory.index.high_byte = true;
        BusterX86MetadataEmitResult malformed_index_high_byte = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532,
            (BusterX86MetadataPhysicalOperand[]){malformed_index_width, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, malformed_base_width_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   malformed_base_high_byte.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   malformed_index_width_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   malformed_index_high_byte.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand base_less = x86_64_metadata_test_physical_mem_index(1, 4, 32, 16);
        BusterX86MetadataPhysicalOperand base_less_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 32), base_less,
        };
        BusterX86MetadataPhysicalQuery base_less_query = x86_64_metadata_test_physical_query(
            S8("MOV"), base_less_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult base_less_select = buster_x86_metadata_select_form(base_less_query);
        u8 base_less_bytes[32] = {0};
        BusterX86MetadataEmitResult base_less_emit = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = base_less_query, .form_id = base_less_select.form_id,
                                         .output = base_less_bytes, .output_capacity = sizeof(base_less_bytes)});
        BUSTER_TEST(arguments, base_less_select.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   x86_64_metadata_test_bytes_equal(base_less_bytes, base_less_emit.byte_count,
                                                                     (u8[]){0x8b, 0x04, 0x8d, 0x10, 0x00, 0x00, 0x00}, 7));

        BusterX86MetadataPhysicalOperand invalid_index = x86_64_metadata_test_physical_mem_index(4, 2, 8, 0);
        invalid_index.memory.has_base = true;
        invalid_index.memory.base = (BusterX86MetadataPhysicalRegister){.index = 3, .width = 64,
                                                                          .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR};
        BusterX86MetadataPhysicalOperand invalid_index_operands[2] = {invalid_index, x86_64_metadata_test_physical_imm(1, 8)};
        BusterX86MetadataEmitResult invalid_sib = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, invalid_index_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, invalid_sib.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand overflow_memory = x86_64_metadata_test_physical_mem_index(1, 1, 32, INT64_C(0x100000000));
        BusterX86MetadataPhysicalOperand overflow_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128), overflow_memory,
        };
        BusterX86MetadataEmitResult overflow_emit = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, overflow_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalQuery overflow_query = x86_64_metadata_test_physical_query(
            S8("VMOVDQA32"), overflow_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataSelectResult overflow_select = buster_x86_metadata_select_form(overflow_query);
        BUSTER_TEST(arguments, overflow_emit.status == BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE &&
                                   overflow_emit.diagnostic_value == INT64_C(0x100000000) &&
                                   overflow_select.status == BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE &&
                                   overflow_select.diagnostic_value == INT64_C(0x100000000));

        BusterX86MetadataPhysicalOperand mismatch_memory = x86_64_metadata_test_physical_mem_base(3, 32, 0);
        BusterX86MetadataPhysicalOperand mismatch_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128), mismatch_memory,
        };
        BusterX86MetadataPhysicalQuery mismatch_query = x86_64_metadata_test_physical_query(
            S8("VMOVDQA32"), mismatch_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        mismatch_query.address_size = 32;
        BusterX86MetadataEmitResult address_size_mismatch = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = mismatch_query, .form_id = 5584, .output = (u8[32]){0}, .output_capacity = 32});
        mismatch_memory.memory.has_segment = true;
        mismatch_memory.memory.segment = BUSTER_X86_METADATA_SEGMENT_NONE;
        mismatch_operands[1] = mismatch_memory;
        BusterX86MetadataPhysicalQuery bad_segment_query = x86_64_metadata_test_physical_query(
            S8("VMOVDQA32"), mismatch_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataEmitResult bad_segment = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = bad_segment_query, .form_id = 5584, .output = (u8[32]){0}, .output_capacity = 32});
        BUSTER_TEST(arguments, address_size_mismatch.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   bad_segment.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        mismatch_memory.memory.has_segment = false;
        mismatch_memory.memory.segment = BUSTER_X86_METADATA_SEGMENT_DS;
        mismatch_operands[1] = mismatch_memory;
        bad_segment_query.operands = mismatch_operands;
        bad_segment = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = bad_segment_query, .form_id = 5584, .output = (u8[32]){0}, .output_capacity = 32});
        BUSTER_TEST(arguments, bad_segment.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand vsib_memory = x86_64_metadata_test_physical_mem_base(3, 32, 0);
        vsib_memory.memory.vsib = true;
        BusterX86MetadataPhysicalOperand vsib_operands[2] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM, 0, 128), vsib_memory,
        };
        BusterX86MetadataEmitResult ordinary_vsib = x86_64_metadata_test_emit_form(
            S8("VMOVDQA32"), 5584, vsib_operands, 2, (BusterX86MetadataPhysicalAttributes){0}, wildcard,
            BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, ordinary_vsib.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);
    }

    {
        String8 wildcard[1] = {S8("*")};
        BusterX86MetadataPhysicalQuery decorator_query = x86_64_metadata_test_physical_query(
            S8("VADDPS"), 0, 0, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataPhysicalAttributes mask_flag_only = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK,
        };
        BusterX86MetadataPhysicalAttributes mask_payload_only = {
            .mask_register = 1,
            .has_mask_register = true,
        };
        BusterX86MetadataPhysicalAttributes broadcast_flag_only = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_BROADCAST,
        };
        BusterX86MetadataPhysicalAttributes broadcast_payload_only = {.broadcast_elements = 16};
        BusterX86MetadataPhysicalAttributes rounding_flag_only = {
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING,
        };
        BusterX86MetadataPhysicalAttributes rounding_payload_only = {.rounding_mode = BUSTER_X86_METADATA_ROUNDING_DOWN};
        BusterX86MetadataPhysicalAttributes sae_flag_only = {.decorator_flags = BUSTER_X86_METADATA_DECORATOR_SAE};
        BusterX86MetadataPhysicalAttributes sae_payload_only = {.sae = true};
        BusterX86MetadataPhysicalQuery coherent_mask_query = decorator_query;
        coherent_mask_query.attributes = (BusterX86MetadataPhysicalAttributes){
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK,
            .has_mask_register = true, .mask_register = 1,
        };
        BusterX86MetadataPhysicalQuery coherent_broadcast_query = decorator_query;
        coherent_broadcast_query.attributes = (BusterX86MetadataPhysicalAttributes){
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_BROADCAST, .broadcast_elements = 16,
        };
        BusterX86MetadataPhysicalOperand coherent_broadcast_operands[3] = {
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 0, 512),
            x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM, 1, 512),
            x86_64_metadata_test_physical_mem_base(0, 32, 0),
        };
        coherent_broadcast_query.operands = coherent_broadcast_operands;
        coherent_broadcast_query.operand_count = BUSTER_ARRAY_LENGTH(coherent_broadcast_operands);
        BusterX86MetadataPhysicalQuery coherent_rounding_query = decorator_query;
        coherent_rounding_query.attributes = (BusterX86MetadataPhysicalAttributes){
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_ROUNDING, .rounding_mode = BUSTER_X86_METADATA_ROUNDING_DOWN,
        };
        BusterX86MetadataPhysicalQuery coherent_sae_query = decorator_query;
        coherent_sae_query.attributes = (BusterX86MetadataPhysicalAttributes){
            .decorator_flags = BUSTER_X86_METADATA_DECORATOR_SAE, .sae = true,
        };
        BusterX86MetadataSelectResult coherent_mask = buster_x86_metadata_select_form(coherent_mask_query);
        BusterX86MetadataSelectResult coherent_broadcast = buster_x86_metadata_select_form(coherent_broadcast_query);
        BusterX86MetadataSelectResult coherent_rounding = buster_x86_metadata_select_form(coherent_rounding_query);
        BusterX86MetadataSelectResult coherent_sae = buster_x86_metadata_select_form(coherent_sae_query);
        BusterX86MetadataPhysicalQuery duplicate_query = decorator_query;
        duplicate_query.attributes = mask_flag_only;
        BusterX86MetadataSelectResult mask_flag_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = mask_payload_only;
        BusterX86MetadataSelectResult mask_payload_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = broadcast_flag_only;
        BusterX86MetadataSelectResult broadcast_flag_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = broadcast_payload_only;
        BusterX86MetadataSelectResult broadcast_payload_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = rounding_flag_only;
        BusterX86MetadataSelectResult rounding_flag_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = rounding_payload_only;
        BusterX86MetadataSelectResult rounding_payload_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = sae_flag_only;
        BusterX86MetadataSelectResult sae_flag_only_result = buster_x86_metadata_select_form(duplicate_query);
        duplicate_query.attributes = sae_payload_only;
        BusterX86MetadataSelectResult sae_payload_only_result = buster_x86_metadata_select_form(duplicate_query);
        BUSTER_TEST(arguments, coherent_mask.status != BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   coherent_broadcast.status != BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   coherent_rounding.status != BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   coherent_sae.status != BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   mask_flag_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   mask_payload_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   broadcast_flag_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   broadcast_payload_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   rounding_flag_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   rounding_payload_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   sae_flag_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   sae_payload_only_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand immediate = x86_64_metadata_test_physical_imm(1, 8);
        immediate.has_symbol = true;
        immediate.symbol = S8("target");
        BusterX86MetadataEmitResult both_immediate_states = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){
                              x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8), immediate},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand neither_immediate_states = x86_64_metadata_test_physical_imm(0, 8);
        neither_immediate_states.has_value = false;
        BusterX86MetadataEmitResult neither_immediate = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){
                              x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8), neither_immediate_states},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, both_immediate_states.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   neither_immediate.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);

        BusterX86MetadataPhysicalOperand malformed_register = x86_64_metadata_test_physical_reg(
            BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8);
        malformed_register.reg.high_byte = true;
        BusterX86MetadataEmitResult malformed_high_byte = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){malformed_register, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        malformed_register = x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8);
        malformed_register.reg.width = 16;
        BusterX86MetadataEmitResult mismatched_register_width = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){malformed_register, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand malformed_symbol = x86_64_metadata_test_physical_imm(1, 8);
        malformed_symbol.symbol = S8("target");
        malformed_symbol.symbol.length = 0;
        BusterX86MetadataEmitResult malformed_symbol_result = x86_64_metadata_test_emit_form(
            S8("MOV"), 10017, (BusterX86MetadataPhysicalOperand[]){x86_64_metadata_test_physical_reg(
                                                                        BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 8),
                                                                    malformed_symbol},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataPhysicalOperand scaled_without_index = x86_64_metadata_test_physical_mem_base(3, 8, 0);
        scaled_without_index.memory.scale = 4;
        BusterX86MetadataEmitResult scaled_without_index_result = x86_64_metadata_test_emit_form(
            S8("MOV"), 9532, (BusterX86MetadataPhysicalOperand[]){scaled_without_index, x86_64_metadata_test_physical_imm(1, 8)}, 2,
            (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, malformed_high_byte.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   mismatched_register_width.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   malformed_symbol_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   scaled_without_index_result.status == BUSTER_X86_METADATA_ENCODE_ADDRESSING);

        BUSTER_TEST(arguments, buster_x86_metadata_instruction_length_status(15) == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
                                   buster_x86_metadata_instruction_length_status(16) ==
                                       BUSTER_X86_METADATA_ENCODE_INSTRUCTION_LENGTH);

        BusterX86MetadataPhysicalAttributes rep_conflict = {.rep = true, .repne = true};
        BusterX86MetadataEmitResult rep_conflict_result = x86_64_metadata_test_emit_form(
            S8("VMMCALL"), 448, 0, 0, rep_conflict, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BusterX86MetadataEmitResult lock_register = x86_64_metadata_test_emit_form(
            S8("NOT"), 9459, &(BusterX86MetadataPhysicalOperand){
                                  .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
                                  .reg = {.index = 0, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR}},
            1, (BusterX86MetadataPhysicalAttributes){.lock = true}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, rep_conflict_result.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                                   lock_register.status == BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION);

        BusterX86MetadataEmitResult wrong_mnemonic = x86_64_metadata_test_emit_form(
            S8("ADD"), 10018, (BusterX86MetadataPhysicalOperand[]){
                              x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
                              x86_64_metadata_test_physical_imm(1, 64)},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard), (u8[1]){0}, 0, 0, 0);
        BUSTER_TEST(arguments, wrong_mnemonic.status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM);

        BusterX86MetadataPhysicalQuery output_query = x86_64_metadata_test_physical_query(
            S8("MOV"), (BusterX86MetadataPhysicalOperand[]){
                           x86_64_metadata_test_physical_reg(BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR, 0, 64),
                           x86_64_metadata_test_physical_imm(1, 64)},
            2, (BusterX86MetadataPhysicalAttributes){0}, wildcard, BUSTER_ARRAY_LENGTH(wildcard));
        BusterX86MetadataEmitResult output_short = buster_x86_metadata_emit_form(
            (BusterX86MetadataEmitQuery){.physical = output_query, .form_id = 10018, .output = 0, .output_capacity = 0});
        BUSTER_TEST(arguments, output_short.status == BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY && output_short.required_byte_count == 10);

    }
    return result;
}
#endif
