#include <buster/lib/compiler/assembly/aarch64_alias_projection.h>
#include <buster/lib/compiler/assembly/generated/aarch64-alias-projection.generated.h>

static bool alias_string_equal(String8 left, String8 right)
{
    if (left.length != right.length) return false;
    for (u64 index = 0; index < left.length; index += 1)
        if (left.pointer[index] != right.pointer[index]) return false;
    return true;
}

static String8 alias_generated_string(u32 offset, u32 length)
{
    if (offset >= BUSTER_A64_ALIAS_PROJECTION_STRING_POOL_SIZE ||
        length > BUSTER_A64_ALIAS_PROJECTION_STRING_POOL_SIZE - offset)
        return (String8){0};
    return (String8){(char8*)buster_a64_alias_projection_string_pool + offset, length};
}

static bool alias_row_generated(u32 ordinal, BusterA64AliasGeneratedRow const** result)
{
    if (!result || ordinal >= BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT) return false;
    BusterA64AliasGeneratedRow const* row = buster_a64_alias_projection_rows + ordinal;
    if (row->alias_ordinal != ordinal || row->alias_form_index >= BUSTER_A64_ALIAS_PROJECTION_FORM_COUNT ||
        row->target_form_index >= BUSTER_A64_ALIAS_PROJECTION_FORM_COUNT || row->operand_count > BUSTER_A64_ALIAS_MAX_OPERANDS ||
        row->field_count > BUSTER_A64_ALIAS_MAX_FIELDS || row->condition_count > BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS ||
        row->preference_condition_count > BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS)
        return false;
    if (!alias_generated_string(row->alias_id_offset, row->alias_id_length).pointer ||
        !alias_generated_string(row->target_id_offset, row->target_id_length).pointer)
        return false;
    *result = row;
    return true;
}

static bool alias_form(u32 form_id, BusterA64SemanticForm* result)
{
    return result && buster_a64_semantic_form(form_id, result) && result->kind == BUSTER_A64_SEMANTIC_FORM_ALIAS &&
           result->status == BUSTER_A64_SEMANTIC_STATUS_DEFINED;
}

static bool alias_row_for_form(u32 form_id, u32* ordinal, BusterA64AliasGeneratedRow const** result)
{
    if (!ordinal || !result) return false;
    for (u32 index = 0; index < BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT; index += 1)
    {
        BusterA64AliasGeneratedRow const* row = 0;
        if (alias_row_generated(index, &row) && row->alias_form_index == form_id)
        {
            *ordinal = index;
            *result = row;
            return true;
        }
    }
    return false;
}

static bool alias_field_local(BusterA64SemanticForm form, u32 field_id, u32* local)
{
    if (!local || field_id < form.field_first || field_id >= form.field_first + form.field_count) return false;
    *local = field_id - form.field_first;
    return true;
}

static bool alias_field_name_equal(BusterA64SemanticString left, String8 right)
{
    if (left.length != right.length) return false;
    for (u32 index = 0; index < left.length; index += 1)
        if (buster_a64_semantic_string_byte(left, index) != right.pointer[index]) return false;
    return true;
}

static bool alias_find_field(BusterA64SemanticForm form, String8 name, u32* local)
{
    if (!local) return false;
    for (u32 index = 0; index < form.field_count; index += 1)
    {
        BusterA64SemanticField field = {0};
        if (buster_a64_semantic_field(form.field_first + index, &field) && alias_field_name_equal(field.name, name))
        {
            *local = index;
            return true;
        }
    }
    return false;
}

static u32 alias_width_mask(u32 width)
{
    if (width >= 32) return UINT32_MAX;
    if (width == 0) return 0;
    return (UINT32_C(1) << width) - 1;
}

static bool alias_field_value_from_word(BusterA64SemanticForm form, u32 local, u32 word, u32* value)
{
    if (!value || local >= form.field_count) return false;
    BusterA64SemanticField field = {0};
    if (!buster_a64_semantic_field(form.field_first + local, &field)) return false;
    u32 result = 0;
    for (u32 index = 0; index < field.segment_count; index += 1)
    {
        BusterA64SemanticSegment segment = {0};
        if (!buster_a64_semantic_segment(field.segment_first + index, &segment) || segment.width > 32 ||
            segment.instruction_lsb + segment.width > 32 || segment.value_lsb + segment.width > 32)
            return false;
        result |= ((word >> segment.instruction_lsb) & alias_width_mask(segment.width)) << segment.value_lsb;
    }
    *value = result;
    return true;
}

static bool alias_canonical_index(u64 source_digest, u32* index, BusterAarch64CanonicalFormInfo* result)
{
    if (!index) return false;
    u32 matches = 0;
    BusterAarch64CanonicalFormInfo candidate = {0};
    for (u32 form_index = 0; form_index < buster_aarch64_canonical_form_count(); form_index += 1)
    {
        if (buster_aarch64_canonical_form(form_index, &candidate) && candidate.arm_row_digest == source_digest)
        {
            matches += 1;
            *index = form_index;
            if (result) *result = candidate;
        }
    }
    return matches == 1;
}

static bool alias_assign_field(BusterA64SemanticForm form, u32 local, u32 value, u32* fields, u64* assigned)
{
    if (!fields || !assigned || local >= form.field_count) return false;
    BusterA64SemanticField field = {0};
    if (!buster_a64_semantic_field(form.field_first + local, &field) || value & ~field.source_mask) return false;
    u64 bit = UINT64_C(1) << local;
    if ((*assigned & bit) != 0) return fields[local] == value;
    fields[local] = value;
    *assigned |= bit;
    return true;
}

static bool alias_value_uint(BusterA64SemanticVMValue value, u32* result)
{
    if (!result) return false;
    switch (value.kind)
    {
    case BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER:
    case BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER:
    case BUSTER_A64_SEMANTIC_VM_VALUE_BITS:
    case BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION:
    case BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER:
    case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER:
    case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR:
    case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR:
    case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT:
    case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST:
    case BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE:
    case BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE:
    case BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION:
        if (value.payload > UINT32_MAX) return false;
        *result = (u32)value.payload;
        return true;
    default:
        return false;
    }
}

static bool alias_operand_value_supported(BusterA64SemanticOperand operand, BusterA64SemanticVMValue value)
{
    if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID || operand.transform_count != 0 || operand.field_index_count == 0 ||
        operand.field_index_count > 1) return false;
    switch (operand.kind)
    {
    case BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER:
        return value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER && value.payload <= 31;
    case BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE:
    case BUSTER_A64_SEMANTIC_OPERAND_MEMORY_BASE:
    case BUSTER_A64_SEMANTIC_OPERAND_MEMORY_DATA_REGISTER:
    case BUSTER_A64_SEMANTIC_OPERAND_MEMORY_OFFSET:
    case BUSTER_A64_SEMANTIC_OPERAND_SYSTEM_REGISTER:
    case BUSTER_A64_SEMANTIC_OPERAND_SYSTEM_OPERATION:
    case BUSTER_A64_SEMANTIC_OPERAND_BARRIER_OPTION:
    case BUSTER_A64_SEMANTIC_OPERAND_PREFETCH_OPERATION:
    case BUSTER_A64_SEMANTIC_OPERAND_CONDITION:
    case BUSTER_A64_SEMANTIC_OPERAND_SHIFT:
    case BUSTER_A64_SEMANTIC_OPERAND_EXTEND:
    case BUSTER_A64_SEMANTIC_OPERAND_ROTATE:
    case BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT:
        return alias_value_uint(value, &(u32){0});
    default:
        return value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER ||
               value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER ||
               value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR ||
               value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR;
    }
}

static bool alias_pack_word(BusterA64SemanticForm form, u32 const* fields, u64 assigned, u32* word)
{
    if (!fields || !word) return false;
    u32 result = form.fixed_value;
    for (u32 local = 0; local < form.field_count; local += 1)
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(form.field_first + local, &field)) return false;
        u32 value = fields[local];
        if ((assigned & (UINT64_C(1) << local)) == 0)
        {
            if (!alias_field_value_from_word(form, local, form.fixed_value, &value)) return false;
        }
        if (value & ~field.source_mask) return false;
        for (u32 segment_index = 0; segment_index < field.segment_count; segment_index += 1)
        {
            BusterA64SemanticSegment segment = {0};
            if (!buster_a64_semantic_segment(field.segment_first + segment_index, &segment)) return false;
            result |= ((value >> segment.value_lsb) & alias_width_mask(segment.width)) << segment.instruction_lsb;
        }
    }
    if ((result & form.fixed_mask) != form.fixed_value) return false;
    *word = result;
    return true;
}

static bool alias_pattern(String8 token, u32* value, u32* mask)
{
    if (!value || !mask || token.length == 0 || token.length > 32) return false;
    u32 candidate = 0, candidate_mask = 0;
    for (u32 index = 0; index < token.length; index += 1)
    {
        char8 c = token.pointer[index];
        if (c == 'x' || c == 'X') { candidate <<= 1; candidate_mask <<= 1; continue; }
        if (c != '0' && c != '1') return false;
        candidate = (candidate << 1) | (u32)(c - '0');
        candidate_mask = (candidate_mask << 1) | 1u;
    }
    *value = candidate;
    *mask = candidate_mask;
    return true;
}

static bool alias_token_value(BusterA64SemanticForm alias_form, BusterA64SemanticForm target_form,
                              u32 const* alias_fields, u32 const* target_fields, String8 token, u32* value, u32* mask)
{
    if (!value || !mask || !token.pointer) return false;
    if (alias_pattern(token, value, mask)) return true;
    u32 local = 0;
    if (alias_find_field(alias_form, token, &local))
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(alias_form.field_first + local, &field)) return false;
        *value = alias_fields[local]; *mask = alias_width_mask(field.width); return true;
    }
    if (alias_find_field(target_form, token, &local))
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(target_form.field_first + local, &field)) return false;
        *value = target_fields[local]; *mask = alias_width_mask(field.width); return true;
    }
    return false;
}

static bool alias_eval_program(u32 form_id, u32 word, bool preference, bool* supported, bool* result)
{
    if (!supported || !result) return false;
    *supported = false; *result = false;
    BusterA64SemanticForm alias_semantic_form = {0};
    BusterA64SemanticAlias alias = {0};
    if (!alias_form(form_id, &alias_semantic_form) || !buster_a64_semantic_alias_descriptor(form_id, &alias)) return false;
    BusterA64SemanticForm target_form = {0};
    BusterA64AliasRowInfo info = {0};
    if (!buster_a64_alias_row_by_form(form_id, &info) || !buster_a64_semantic_form(info.target_form_id, &target_form)) return false;
    u32 alias_values[BUSTER_A64_ALIAS_MAX_FIELDS] = {0};
    u32 target_values[BUSTER_A64_ALIAS_MAX_FIELDS] = {0};
    u32 target_canonical_index = 0;
    if (alias_semantic_form.field_count > BUSTER_A64_ALIAS_MAX_FIELDS || target_form.field_count > BUSTER_A64_ALIAS_MAX_FIELDS ||
        !alias_canonical_index(info.target_source_digest, &target_canonical_index, 0) ||
        !buster_aarch64_canonical_raw_decode(target_canonical_index, word, target_values, target_form.field_count)) return false;
    for (u32 index = 0; index < alias_semantic_form.field_count; index += 1)
        if (!alias_field_value_from_word(alias_semantic_form, index, word, &alias_values[index])) return false;
    u32 token_count = preference ? alias.preference_condition_count : alias.condition_count;
    if (token_count == 0) { *supported = true; *result = true; return true; }
    bool expression = true;
    bool have_value = false;
    bool invert = false;
    bool have_operator = false;
    bool operator_not_equal = false;
    u32 pending_lhs = 0, pending_lhs_mask = 0;
    BusterA64SemanticString token_string = {0};
    for (u32 index = 0; index < token_count; index += 1)
    {
        bool token_ok = preference ? buster_a64_semantic_alias_preference_condition_token(form_id, index, &token_string) :
                                      buster_a64_semantic_alias_condition_token(form_id, index, &token_string);
        if (!token_ok) return false;
        String8 token = {(char8*)0, token_string.length};
        char8 token_bytes[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
        if (token.length >= BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS) return false;
        for (u32 c = 0; c < token.length; c += 1) token_bytes[c] = buster_a64_semantic_string_byte(token_string, c);
        token.pointer = token_bytes;
        if (alias_string_equal(token, S8("Unconditionally"))) { if (token_count != 1) return false; *supported = true; *result = true; return true; }
        if (alias_string_equal(token, S8("Never"))) { if (token_count != 1) return false; *supported = true; *result = false; return true; }
        if (alias_string_equal(token, S8("!"))) { invert = !invert; continue; }
        if (alias_string_equal(token, S8("==")) || alias_string_equal(token, S8("!=")))
        {
            if (!have_value || have_operator) return false;
            have_operator = true;
            operator_not_equal = token_bytes[0] == '!';
            continue;
        }
        if (alias_string_equal(token, S8("&&")) || alias_string_equal(token, S8("||")))
        {
            if (!have_value || !have_operator) return false;
            have_value = false; have_operator = false; operator_not_equal = false; pending_lhs = 0; pending_lhs_mask = 0;
            continue;
        }
        u32 value = 0, mask = 0;
        if (!alias_token_value(alias_semantic_form, target_form, alias_values, target_values, token, &value, &mask)) return false;
        if (!have_value)
        {
            pending_lhs = value; pending_lhs_mask = mask; have_value = true;
        }
        else if (have_operator)
        {
            /* A simple comparison is the only non-trivial condition form
             * executed here.  The token stream is bounded and no parser
             * recursion is involved. */
            bool equal = ((pending_lhs ^ value) & pending_lhs_mask & mask) == 0 &&
                         ((pending_lhs_mask & mask) != 0);
            expression = expression && ((equal ^ operator_not_equal) ^ invert);
            invert = false; have_operator = false; have_value = false; operator_not_equal = false;
        }
        else return false;
    }
    if (have_value || have_operator) return false;
    *supported = true; *result = expression; return true;
}

u32 buster_a64_alias_projection_schema_version(void) { return BUSTER_A64_ALIAS_PROJECTION_SCHEMA_VERSION; }
u32 buster_a64_alias_count(void) { return BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT; }
u32 buster_a64_alias_canonical_count(void) { return BUSTER_A64_ALIAS_PROJECTION_CANONICAL_COUNT; }
String8 buster_a64_alias_denominator_sha256(void)
{
    static char8 digest[] = BUSTER_A64_ALIAS_PROJECTION_DENOMINATOR_SHA256;
    return (String8){digest, sizeof(digest) - 1};
}

bool buster_a64_alias_row(u32 alias_ordinal, BusterA64AliasRowInfo* result)
{
    if (!result) return false;
    BusterA64AliasGeneratedRow const* row = 0;
    if (!alias_row_generated(alias_ordinal, &row)) return false;
    *result = (BusterA64AliasRowInfo){.alias_ordinal = alias_ordinal, .alias_form_id = row->alias_form_index,
                                      .target_form_id = row->target_form_index, .alias_source_digest = row->alias_source_digest,
                                      .target_source_digest = row->target_source_digest, .fixed_mask = row->fixed_mask,
                                      .fixed_value = row->fixed_value, .preference_rank = row->preference_rank,
                                      .target_owner = row->target_owner, .operand_count = row->operand_count,
                                      .field_count = row->field_count, .condition_token_count = row->condition_count,
                                      .preference_condition_token_count = row->preference_condition_count,
                                      .alias_id = alias_generated_string(row->alias_id_offset, row->alias_id_length),
                                      .target_id = alias_generated_string(row->target_id_offset, row->target_id_length)};
    return true;
}

bool buster_a64_alias_row_by_form(u32 alias_form_id, BusterA64AliasRowInfo* result)
{
    u32 ordinal = 0; BusterA64AliasGeneratedRow const* row = 0;
    return result && alias_row_for_form(alias_form_id, &ordinal, &row) && buster_a64_alias_row(ordinal, result);
}

bool buster_a64_alias_find(String8 id, u32 ordinal, u32* alias_form_id)
{
    if (!alias_form_id) return false;
    u32 seen = 0;
    for (u32 index = 0; index < BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT; index += 1)
    {
        BusterA64AliasRowInfo row = {0};
        if (buster_a64_alias_row(index, &row) && alias_string_equal(row.alias_id, id))
        {
            if (seen == ordinal) { *alias_form_id = row.alias_form_id; return true; }
            seen += 1;
        }
    }
    return false;
}

bool buster_a64_alias_condition_supported(u32 alias_form_id)
{
    bool supported = false, result = false;
    return alias_eval_program(alias_form_id, 0, false, &supported, &result) && supported;
}

bool buster_a64_alias_preference_supported(u32 alias_form_id)
{
    bool supported = false, result = false;
    return alias_eval_program(alias_form_id, 0, true, &supported, &result) && supported;
}

BusterA64AliasStatus buster_a64_alias_encode(Target target, BusterA64AliasInstruction const* instruction, u32* word)
{
    if (!instruction || !word || instruction->operand_count > BUSTER_A64_ALIAS_MAX_OPERANDS || target.cpu_arch != CPU_ARCH_AARCH64)
        return BUSTER_A64_ALIAS_STATUS_INVALID_ARGUMENT;
    BusterA64AliasRowInfo info = {0};
    BusterA64SemanticForm form = {0};
    if (!buster_a64_alias_row_by_form(instruction->alias_form_id, &info) || !alias_form(instruction->alias_form_id, &form))
        return BUSTER_A64_ALIAS_STATUS_BOUNDS;
    if (instruction->operand_count != form.operand_count || form.field_count > BUSTER_A64_ALIAS_MAX_FIELDS)
        return BUSTER_A64_ALIAS_STATUS_INVALID_ARGUMENT;
    u32 fields[BUSTER_A64_ALIAS_MAX_FIELDS] = {0};
    u64 assigned = 0;
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        u32 field_id = 0, local = 0, value = 0;
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand) || !alias_operand_value_supported(operand, instruction->operands[index]) ||
            !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) || !alias_field_local(form, field_id, &local) ||
            !alias_value_uint(instruction->operands[index], &value) || !alias_assign_field(form, local, value, fields, &assigned))
            return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
    }
    u32 candidate = 0;
    if (!alias_pack_word(form, fields, assigned, &candidate) || (candidate & info.fixed_mask) != info.fixed_value)
        return BUSTER_A64_ALIAS_STATUS_RANGE;
    BusterA64SemanticForm target_form = {0};
    u32 target_canonical_index = 0;
    if (!buster_a64_semantic_form(info.target_form_id, &target_form) || target_form.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL ||
        !alias_canonical_index(info.target_source_digest, &target_canonical_index, 0) ||
        !buster_aarch64_canonical_raw_decode(target_canonical_index, candidate, fields, target_form.field_count))
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    bool supported = false, condition = false;
    if (!alias_eval_program(instruction->alias_form_id, candidate, false, &supported, &condition)) return BUSTER_A64_ALIAS_STATUS_BOUNDS;
    if (!supported) return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
    if (!condition) return BUSTER_A64_ALIAS_STATUS_CONDITION_FALSE;
    *word = candidate;
    return BUSTER_A64_ALIAS_STATUS_OK;
}

BusterA64AliasStatus buster_a64_alias_decode_row(Target target, u32 alias_form_id, u32 word, BusterA64AliasResult* result)
{
    if (!result || target.cpu_arch != CPU_ARCH_AARCH64) return BUSTER_A64_ALIAS_STATUS_INVALID_ARGUMENT;
    BusterA64AliasRowInfo info = {0};
    BusterA64SemanticForm form = {0}, target_form = {0};
    if (!buster_a64_alias_row_by_form(alias_form_id, &info) || !alias_form(alias_form_id, &form) ||
        !buster_a64_semantic_form(info.target_form_id, &target_form) || (word & info.fixed_mask) != info.fixed_value ||
        form.operand_count > BUSTER_A64_ALIAS_MAX_OPERANDS || target_form.field_count > BUSTER_A64_ALIAS_MAX_FIELDS)
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    u32 target_fields[BUSTER_A64_ALIAS_MAX_FIELDS] = {0};
    u32 target_canonical_index = 0;
    if (!alias_canonical_index(info.target_source_digest, &target_canonical_index, 0) ||
        !buster_aarch64_canonical_raw_decode(target_canonical_index, word, target_fields, target_form.field_count))
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    bool supported = false, condition = false;
    if (!alias_eval_program(alias_form_id, word, false, &supported, &condition)) return BUSTER_A64_ALIAS_STATUS_BOUNDS;
    if (!supported) return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
    if (!condition) return BUSTER_A64_ALIAS_STATUS_CONDITION_FALSE;
    BusterA64AliasResult candidate = {.status = BUSTER_A64_ALIAS_STATUS_OK, .alias_form_id = alias_form_id,
                                      .target_form_id = info.target_form_id, .word = word, .operand_count = form.operand_count};
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        u32 field_id = 0, local = 0, value = 0;
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand) || operand.transform_count != 0 || operand.field_index_count != 1 ||
            !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) || !alias_field_local(form, field_id, &local) ||
            !alias_field_value_from_word(form, local, word, &value)) return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
        if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER)
        {
            u8 width = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_W32) ? 32 : 64;
            candidate.operands[index] = buster_a64_semantic_vm_value_gpr(value, width, false,
                                                                          value == 31 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED));
        }
        else if (!alias_value_uint(buster_a64_semantic_vm_value_unsigned(value, 32), &value))
            return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
        else candidate.operands[index] = buster_a64_semantic_vm_value_unsigned(value, 32);
        if (candidate.operands[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) return BUSTER_A64_ALIAS_STATUS_RANGE;
    }
    *result = candidate;
    return BUSTER_A64_ALIAS_STATUS_OK;
}

BusterA64AliasStatus buster_a64_alias_decode(Target target, u32 word, BusterA64AliasResult* result)
{
    if (!result) return BUSTER_A64_ALIAS_STATUS_INVALID_ARGUMENT;
    BusterA64AliasResult candidate = {0};
    bool found = false;
    for (u32 index = 0; index < BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT; index += 1)
    {
        BusterA64AliasRowInfo row = {0};
        if (!buster_a64_alias_row(index, &row) || (word & row.fixed_mask) != row.fixed_value) continue;
        BusterA64AliasResult probe = {0};
        BusterA64AliasStatus status = buster_a64_alias_decode_row(target, row.alias_form_id, word, &probe);
        if (status != BUSTER_A64_ALIAS_STATUS_OK) continue;
        BusterA64AliasRowInfo current = {0};
        if (found) buster_a64_alias_row_by_form(candidate.alias_form_id, &current);
        if (!found || row.preference_rank < current.preference_rank ||
            (row.preference_rank == current.preference_rank && row.alias_form_id < current.alias_form_id))
        {
            candidate = probe; found = true;
        }
    }
    if (!found) return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    *result = candidate;
    return BUSTER_A64_ALIAS_STATUS_OK;
}

bool buster_a64_alias_validate(void)
{
    if (BUSTER_A64_ALIAS_PROJECTION_FORM_COUNT != buster_a64_semantic_form_count() ||
        BUSTER_A64_ALIAS_PROJECTION_CANONICAL_COUNT + BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT != BUSTER_A64_ALIAS_PROJECTION_FORM_COUNT)
        return false;
    u32 census[7] = {0};
    for (u32 index = 0; index < BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT; index += 1)
    {
        BusterA64AliasRowInfo row = {0};
        BusterA64SemanticForm form = {0}, target = {0};
        if (!buster_a64_alias_row(index, &row) || !alias_form(row.alias_form_id, &form) ||
            !buster_a64_semantic_form(row.target_form_id, &target) || target.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL ||
            target.source_digest != row.target_source_digest || form.source_digest != row.alias_source_digest || row.target_owner >= 7)
            return false;
        census[row.target_owner] += 1;
    }
    return census[0] == 64 && census[1] == 59 && census[2] == 21 && census[3] == 10 && census[4] == 9 && census[5] == 7 && census[6] == 2;
}
