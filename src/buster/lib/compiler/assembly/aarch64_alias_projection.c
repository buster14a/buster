#include <buster/lib/compiler/assembly/aarch64_alias_projection.h>
#include <buster/lib/compiler/assembly/generated/aarch64-alias-projection.generated.h>

static bool alias_string_equal(String8 left, String8 right)
{
    if (left.length != right.length)
    {
        return false;
    }
    for (u64 index = 0; index < left.length; index += 1)
    {
        if (left.pointer[index] != right.pointer[index])
        {
            return false;
        }
    }
    return true;
}

static String8 alias_generated_string(u32 offset, u32 length)
{
    String8 result;
    if (offset >= BUSTER_A64_ALIAS_PROJECTION_STRING_POOL_SIZE || length > BUSTER_A64_ALIAS_PROJECTION_STRING_POOL_SIZE - offset)
    {
        result = (String8){0};
    }
    else
    {
        result = (String8){(char8*)buster_a64_alias_projection_string_pool + offset, length};
    }

    return result;
}

static bool alias_row_generated(u32 ordinal, BusterA64AliasGeneratedRow const** result)
{
    if (!result || ordinal >= BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT)
    {
        return false;
    }
    BusterA64AliasGeneratedRow const* row = buster_a64_alias_projection_rows + ordinal;
    if (row->alias_ordinal != ordinal || row->alias_form_index >= BUSTER_A64_ALIAS_PROJECTION_FORM_COUNT ||
        row->target_form_index >= BUSTER_A64_ALIAS_PROJECTION_FORM_COUNT || row->operand_count > BUSTER_A64_ALIAS_MAX_OPERANDS ||
        row->field_count > BUSTER_A64_ALIAS_MAX_FIELDS || row->condition_count > BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS ||
        row->preference_condition_count > BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS)
    {
        return false;
    }
    if (!alias_generated_string(row->alias_id_offset, row->alias_id_length).pointer ||
        !alias_generated_string(row->target_id_offset, row->target_id_length).pointer)
    {
        return false;
    }
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
    if (ordinal && result)
    {
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
    }

    return false;
}

static bool alias_field_local(BusterA64SemanticForm form, u32 field_id, u32* local)
{
    bool result;
    if (!local || field_id < form.field_first || field_id >= form.field_first + form.field_count)
    {
        result = false;
    }
    else
    {
        *local = field_id - form.field_first;
        result = true;
    }

    return result;
}

static bool alias_field_name_equal(BusterA64SemanticString left, String8 right)
{
    if (left.length != right.length)
    {
        return false;
    }
    for (u32 index = 0; index < left.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(left, index) != right.pointer[index])
        {
            return false;
        }
    }
    return true;
}

static bool alias_find_field(BusterA64SemanticForm form, String8 name, u32* local)
{
    if (local)
    {
        for (u32 index = 0; index < form.field_count; index += 1)
        {
            BusterA64SemanticField field = {0};
            if (buster_a64_semantic_field(form.field_first + index, &field) && alias_field_name_equal(field.name, name))
            {
                *local = index;
                return true;
            }
        }
    }

    return false;
}

static u32 alias_width_mask(u32 width)
{
    u32 result;
    if (width >= 32)
    {
        result = UINT32_MAX;
    }
    else if (width == 0)
    {
        result = 0;
    }
    else
    {
        result = (UINT32_C(1) << width) - 1;
    }

    return result;
}

static bool alias_field_value_from_word(BusterA64SemanticForm form, u32 local, u32 word, u32* value)
{
    if (!value || local >= form.field_count)
    {
        return false;
    }
    BusterA64SemanticField field = {0};
    if (!buster_a64_semantic_field(form.field_first + local, &field))
    {
        return false;
    }
    u32 result = 0;
    for (u32 index = 0; index < field.segment_count; index += 1)
    {
        BusterA64SemanticSegment segment = {0};
        if (!buster_a64_semantic_segment(field.segment_first + index, &segment) || segment.width > 32 || segment.instruction_lsb + segment.width > 32 ||
            segment.value_lsb + segment.width > 32)
        {
            return false;
        }
        result |= ((word >> segment.instruction_lsb) & alias_width_mask(segment.width)) << segment.value_lsb;
    }
    *value = result;
    return true;
}

static bool alias_canonical_index(u64 source_digest, u32* index, BusterAarch64CanonicalFormInfo* result)
{
    if (!index)
    {
        return false;
    }
    u32 matches = 0;
    BusterAarch64CanonicalFormInfo candidate = {0};
    for (u32 form_index = 0; form_index < buster_aarch64_canonical_form_count(); form_index += 1)
    {
        if (buster_aarch64_canonical_form(form_index, &candidate) && candidate.arm_row_digest == source_digest)
        {
            matches += 1;
            *index = form_index;
            if (result)
            {
                *result = candidate;
            }
        }
    }
    return matches == 1;
}

static bool alias_assign_field(BusterA64SemanticForm form, u32 local, u32 value, u32* fields, u64* assigned)
{
    if (!fields || !assigned || local >= form.field_count)
    {
        return false;
    }
    BusterA64SemanticField field = {0};
    if (!buster_a64_semantic_field(form.field_first + local, &field) || value & ~field.source_mask)
    {
        return false;
    }
    u64 bit = UINT64_C(1) << local;
    if ((*assigned & bit) != 0)
    {
        return fields[local] == value;
    }
    fields[local] = value;
    *assigned |= bit;
    return true;
}

static bool alias_value_uint(BusterA64SemanticVMValue value, u32* result)
{
    if (!result)
    {
        return false;
    }
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
        if (value.payload > UINT32_MAX)
        {
            return false;
        }
        *result = (u32)value.payload;
        return true;
    default:
        return false;
    }
}

static bool alias_operand_value_supported(BusterA64SemanticOperand operand, BusterA64SemanticVMValue value)
{
    if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID || operand.transform_count != 0 || operand.field_index_count == 0 || operand.field_index_count > 1)
    {
        return false;
    }
    switch (operand.kind)
    {
    case BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER:
        if (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER || value.payload > 31)
        {
            return false;
        }
        if (value.width != ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_W32) ? 32 : 64))
        {
            return false;
        }
        if (value.payload != 31)
        {
            return (value.flags & (BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR)) == 0;
        }
        bool sp = (value.flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP) != 0;
        bool zr = (value.flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR) != 0;
        return sp != zr && ((sp && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SP_ALLOWED)) ||
                            (zr && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED)));
    case BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE:
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
    case BUSTER_A64_SEMANTIC_OPERAND_MEMORY_BASE:
        if (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER || value.payload > 31 || value.width != 64)
        {
            return false;
        }
        if (value.payload != 31)
        {
            return (value.flags & (BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR)) == 0;
        }
        return (value.flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP) != 0 &&
               (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SP_ALLOWED) != 0 &&
               (value.flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR) == 0;
    default:
        return value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER ||
               value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR;
    }
}

static bool alias_target_valid(Target target)
{
    bool result;
    if (target.cpu_arch != CPU_ARCH_AARCH64 || !target_cpu_features_are_valid(target))
    {
        result = false;
    }
    else if (target.cpu_model == CPU_MODEL_A64_APPLE_M1)
    {
        result = true;
    }
    else
    {
        result = target.cpu_model == CPU_MODEL_NATIVE && target_native.cpu_arch == CPU_ARCH_AARCH64 && target_native.cpu_model == CPU_MODEL_A64_APPLE_M1;
    }

    return result;
}

static bool alias_target_word_matches(Target target, u32 canonical_index, u64 target_digest, u32 word)
{
    BusterAarch64CanonicalDecodeResult canonical = {0};
    return buster_aarch64_canonical_decode(target, word, &canonical) == BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS &&
           canonical.form_index == canonical_index && canonical.arm_row_digest == target_digest;
}

static bool alias_gpr_value_allowed(BusterA64SemanticOperand operand, u32 value, BusterA64SemanticVMValue *result)
{
    if (!result || (operand.kind != BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER && operand.kind != BUSTER_A64_SEMANTIC_OPERAND_MEMORY_BASE) || value > 31)
    {
        return false;
    }
    u8 width = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_W32) ? 32 : 64;
    bool sp = value == 31 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SP_ALLOWED) != 0;
    bool zr = value == 31 && !sp && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED) != 0;
    if (value == 31 && !sp && !zr)
    {
        return false;
    }
    *result = buster_a64_semantic_vm_value_gpr(value, width, sp, zr);
    return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

static bool alias_pack_word(BusterA64SemanticForm form, u32 const* fields, u64 assigned, u32* word)
{
    if (!fields || !word)
    {
        return false;
    }
    u32 result = form.fixed_value;
    for (u32 local = 0; local < form.field_count; local += 1)
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(form.field_first + local, &field))
        {
            return false;
        }
        u32 value = fields[local];
        if ((assigned & (UINT64_C(1) << local)) == 0)
        {
            if (!alias_field_value_from_word(form, local, form.fixed_value, &value))
            {
                return false;
            }
        }
        if (value & ~field.source_mask)
        {
            return false;
        }
        for (u32 segment_index = 0; segment_index < field.segment_count; segment_index += 1)
        {
            BusterA64SemanticSegment segment = {0};
            if (!buster_a64_semantic_segment(field.segment_first + segment_index, &segment))
            {
                return false;
            }
            result |= ((value >> segment.value_lsb) & alias_width_mask(segment.width)) << segment.instruction_lsb;
        }
    }
    if ((result & form.fixed_mask) != form.fixed_value)
    {
        return false;
    }
    *word = result;
    return true;
}

static bool alias_pattern(String8 token, u32* value, u32* mask)
{
    if (!value || !mask || token.length == 0 || token.length > 32)
    {
        return false;
    }
    u32 candidate = 0, candidate_mask = 0;
    for (u32 index = 0; index < token.length; index += 1)
    {
        char8 c = token.pointer[index];
        if (c == 'x' || c == 'X')
        {
            candidate <<= 1;
            candidate_mask <<= 1;
            continue;
        }
        if (c != '0' && c != '1')
        {
            return false;
        }
        candidate = (candidate << 1) | (u32)(c - '0');
        candidate_mask = (candidate_mask << 1) | 1u;
    }
    *value = candidate;
    *mask = candidate_mask;
    return true;
}

static bool alias_token_value(BusterA64SemanticForm alias_form, BusterA64SemanticForm target_form, u32 const* alias_fields, u32 const* target_fields,
                              String8 token, u32* value, u32* mask)
{
    if (value && mask && token.pointer)
    {
        if (alias_pattern(token, value, mask))
        {
            return true;
        }
        u32 local = 0;
        if (alias_find_field(alias_form, token, &local))
        {
            BusterA64SemanticField field = {0};
            if (!buster_a64_semantic_field(alias_form.field_first + local, &field))
            {
                return false;
            }
            *value = alias_fields[local];
            *mask = alias_width_mask(field.width);
            return true;
        }
        if (alias_find_field(target_form, token, &local))
        {
            BusterA64SemanticField field = {0};
            if (!buster_a64_semantic_field(target_form.field_first + local, &field))
            {
                return false;
            }
            *value = target_fields[local];
            *mask = alias_width_mask(field.width);
            return true;
        }
        /* Some Arm alias predicates refer to constraint symbols rather than raw
         * fields (for example the atomic-memory A bit).  The canonical raw
         * decoder has already validated those constraints before predicate
         * evaluation; expose the only bounded constraint symbol used by the
         * executable alias subset as its fixed zero value. */
        if (alias_string_equal(token, S8("A")))
        {
            *value = 0;
            *mask = 1;
            return true;
        }
    }

    return false;
}

static bool alias_program_token(u32 form_id, bool preference, u32 index, char8* storage, u32 capacity, String8* result)
{
    if (!storage || !result || capacity == 0)
    {
        return false;
    }
    BusterA64SemanticString token_string = {0};
    bool token_ok = preference ? buster_a64_semantic_alias_preference_condition_token(form_id, index, &token_string)
                               : buster_a64_semantic_alias_condition_token(form_id, index, &token_string);
    if (!token_ok || token_string.length >= capacity)
    {
        return false;
    }
    for (u32 character = 0; character < token_string.length; character += 1)
    {
        storage[character] = buster_a64_semantic_string_byte(token_string, character);
    }
    *result = (String8){storage, token_string.length};
    return true;
}

/* The source metadata contains Arm pseudocode programs, including function
 * calls, arithmetic, parentheses, and short-circuit operators.  The generic
 * projection intentionally implements only direct comparisons, conjunctions,
 * and the bounded ``field IN {bit-pattern}`` form.  Keeping this preflight
 * separate from evaluation ensures that an unsupported OR/function expression
 * can never be reported as supported just because one branch happened to
 * evaluate true for a particular word. */
static bool alias_program_atom_tokens(u32 form_id, bool preference, u32 index, u32 token_count,
                                      char8* lhs_storage, char8* operator_storage, char8* rhs_storage,
                                      char8* brace_storage, char8* close_storage, String8* lhs, String8* operator,
                                      String8* rhs, u32* atom_length)
{
    if (!lhs_storage || !operator_storage || !rhs_storage || !brace_storage || !close_storage || !lhs || !operator || !rhs || !atom_length)
    {
        return false;
    }
    *atom_length = 0;
    if (index + 3 <= token_count && alias_program_token(form_id, preference, index, lhs_storage, BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS, lhs) &&
        alias_program_token(form_id, preference, index + 1, operator_storage, BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS, operator) &&
        alias_program_token(form_id, preference, index + 2, rhs_storage, BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS, rhs) &&
        (alias_string_equal(*operator, S8("==")) || alias_string_equal(*operator, S8("!="))))
    {
        *atom_length = 3;
        return true;
    }
    if (index + 5 > token_count ||
        !alias_program_token(form_id, preference, index, lhs_storage, BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS, lhs) ||
        !alias_program_token(form_id, preference, index + 1, operator_storage, BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS, operator) ||
        !alias_string_equal(*operator, S8("IN")))
    {
        return false;
    }
    String8 brace = {0}, close = {0};
    if (!alias_program_token(form_id, preference, index + 2, brace_storage, BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS, &brace) ||
        !alias_program_token(form_id, preference, index + 3, rhs_storage, BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS, rhs) ||
        !alias_program_token(form_id, preference, index + 4, close_storage, BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS, &close) ||
        !alias_string_equal(brace, S8("{")) || !alias_string_equal(close, S8("}")))
    {
        return false;
    }
    *atom_length = 5;
    return alias_pattern(*rhs, &(u32){0}, &(u32){0});
}

static bool alias_program_syntax_supported(u32 form_id, bool preference)
{
    BusterA64SemanticForm alias_semantic_form = {0};
    BusterA64SemanticAlias alias = {0};
    BusterA64AliasRowInfo info = {0};
    BusterA64SemanticForm target_form = {0};
    if (alias_form(form_id, &alias_semantic_form) && buster_a64_semantic_alias_descriptor(form_id, &alias) && buster_a64_alias_row_by_form(form_id, &info) &&
        buster_a64_semantic_form(info.target_form_id, &target_form))
    {
        u32 token_count = preference ? alias.preference_condition_count : alias.condition_count;
        if (token_count == 0)
        {
            return true;
        }
        char8 token_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
        String8 token = {0};
        if (token_count == 1)
        {
            return alias_program_token(form_id, preference, 0, token_storage, sizeof(token_storage), &token) &&
                   (alias_string_equal(token, S8("Unconditionally")) || alias_string_equal(token, S8("Never")));
        }
        u32 index = 0;
        while (index < token_count)
        {
            char8 lhs_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            char8 operator_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            char8 rhs_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            char8 brace_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            char8 close_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            String8 lhs = {0}, operator = {0}, rhs = {0};
            u32 atom_length = 0;
            if (!alias_program_atom_tokens(form_id, preference, index, token_count, lhs_storage, operator_storage, rhs_storage, brace_storage, close_storage,
                                           &lhs, &operator, &rhs, &atom_length))
            {
                return false;
            }
            u32 value = 0, mask = 0, local = 0;
            bool lhs_supported = alias_pattern(lhs, &value, &mask) || alias_find_field(alias_semantic_form, lhs, &local) || alias_find_field(target_form, lhs, &local) ||
                                 alias_string_equal(lhs, S8("A"));
            bool rhs_supported = alias_pattern(rhs, &value, &mask) || alias_find_field(alias_semantic_form, rhs, &local) || alias_find_field(target_form, rhs, &local) ||
                                 alias_string_equal(rhs, S8("A"));
            if (!lhs_supported || !rhs_supported)
            {
                return false;
            }
            index += atom_length;
            if (index == token_count)
            {
                return true;
            }
            if (!alias_program_token(form_id, preference, index, token_storage, sizeof(token_storage), &token) || !alias_string_equal(token, S8("&&")))
            {
                return false;
            }
            index += 1;
        }
    }

    return false;
}

static bool alias_eval_program(u32 form_id, u32 word, bool preference, bool* supported, bool* result)
{
    if (!supported || !result)
    {
        return false;
    }
    *supported = false;
    *result = false;
    BusterA64SemanticForm alias_semantic_form = {0};
    BusterA64SemanticAlias alias = {0};
    if (!alias_form(form_id, &alias_semantic_form) || !buster_a64_semantic_alias_descriptor(form_id, &alias))
    {
        return false;
    }
    if (alias_program_syntax_supported(form_id, preference))
    {
        BusterA64SemanticForm target_form = {0};
        BusterA64AliasRowInfo info = {0};
        if (!buster_a64_alias_row_by_form(form_id, &info) || !buster_a64_semantic_form(info.target_form_id, &target_form))
        {
            return false;
        }
        u32 alias_values[BUSTER_A64_ALIAS_MAX_FIELDS] = {0};
        u32 target_values[BUSTER_A64_ALIAS_MAX_FIELDS] = {0};
        u32 target_canonical_index = 0;
        if (alias_semantic_form.field_count > BUSTER_A64_ALIAS_MAX_FIELDS || target_form.field_count > BUSTER_A64_ALIAS_MAX_FIELDS ||
            !alias_canonical_index(info.target_source_digest, &target_canonical_index, 0) ||
            !buster_aarch64_canonical_raw_decode(target_canonical_index, word, target_values, target_form.field_count))
        {
            return false;
        }
        for (u32 index = 0; index < alias_semantic_form.field_count; index += 1)
        {
            if (!alias_field_value_from_word(alias_semantic_form, index, word, &alias_values[index]))
            {
                return false;
            }
        }
        u32 token_count = preference ? alias.preference_condition_count : alias.condition_count;
        if (token_count == 0)
        {
            *supported = true;
            *result = true;
            return true;
        }
        if (token_count == 1)
        {
            char8 token_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            String8 token = {0};
            if (!alias_program_token(form_id, preference, 0, token_storage, sizeof(token_storage), &token))
            {
                return false;
            }
            *supported = true;
            *result = alias_string_equal(token, S8("Unconditionally"));
            return true;
        }
        bool expression = true;
        u32 index = 0;
        while (index < token_count)
        {
            char8 lhs_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            char8 operator_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            char8 rhs_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            char8 brace_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            char8 close_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            String8 lhs = {0}, operator = {0}, rhs = {0};
            u32 atom_length = 0;
            if (!alias_program_atom_tokens(form_id, preference, index, token_count, lhs_storage, operator_storage, rhs_storage, brace_storage, close_storage,
                                           &lhs, &operator, &rhs, &atom_length))
            {
                return false;
            }
            u32 lhs_value = 0, lhs_mask = 0, rhs_value = 0, rhs_mask = 0;
            if (!alias_token_value(alias_semantic_form, target_form, alias_values, target_values, lhs, &lhs_value, &lhs_mask) ||
                !alias_token_value(alias_semantic_form, target_form, alias_values, target_values, rhs, &rhs_value, &rhs_mask))
            {
                return false;
            }
            bool equal = ((lhs_value ^ rhs_value) & lhs_mask & rhs_mask) == 0 && ((lhs_mask & rhs_mask) != 0);
            bool atom_result = alias_string_equal(operator, S8("==")) ? equal :
                               alias_string_equal(operator, S8("!=")) ? !equal :
                               ((lhs_value ^ rhs_value) & lhs_mask & rhs_mask) == 0;
            expression = expression && atom_result;
            index += atom_length;
            if (index == token_count)
            {
                break;
            }
            char8 conjunction_storage[BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS] = {0};
            String8 conjunction = {0};
            if (!alias_program_token(form_id, preference, index, conjunction_storage, sizeof(conjunction_storage), &conjunction) ||
                !alias_string_equal(conjunction, S8("&&")))
            {
                return false;
            }
            index += 1;
        }
        *supported = true;
        *result = expression;
    }

    return true;
}

u32 buster_a64_alias_projection_schema_version(void)
{
    return BUSTER_A64_ALIAS_PROJECTION_SCHEMA_VERSION;
}
u32 buster_a64_alias_count(void)
{
    return BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT;
}
u32 buster_a64_alias_canonical_count(void)
{
    return BUSTER_A64_ALIAS_PROJECTION_CANONICAL_COUNT;
}
u32 buster_a64_alias_generic_executable_count(void)
{
    return BUSTER_A64_ALIAS_PROJECTION_GENERIC_EXECUTABLE_COUNT;
}
String8 buster_a64_alias_denominator_sha256(void)
{
    static char8 digest[] = BUSTER_A64_ALIAS_PROJECTION_DENOMINATOR_SHA256;
    return (String8){digest, sizeof(digest) - 1};
}

bool buster_a64_alias_row(u32 alias_ordinal, BusterA64AliasRowInfo* result)
{
    if (!result)
    {
        return false;
    }
    BusterA64AliasGeneratedRow const* row = 0;
    if (!alias_row_generated(alias_ordinal, &row))
    {
        return false;
    }
    *result = (BusterA64AliasRowInfo){.alias_ordinal = alias_ordinal,
                                      .alias_form_id = row->alias_form_index,
                                      .target_form_id = row->target_form_index,
                                      .alias_source_digest = row->alias_source_digest,
                                      .target_source_digest = row->target_source_digest,
                                      .fixed_mask = row->fixed_mask,
                                      .fixed_value = row->fixed_value,
                                      .preference_rank = row->preference_rank,
                                      .target_owner = row->target_owner,
                                      .operand_count = row->operand_count,
                                      .field_count = row->field_count,
                                      .condition_token_count = row->condition_count,
                                      .preference_condition_token_count = row->preference_condition_count,
                                      .alias_id = alias_generated_string(row->alias_id_offset, row->alias_id_length),
                                      .target_id = alias_generated_string(row->target_id_offset, row->target_id_length)};
    return true;
}

bool buster_a64_alias_row_by_form(u32 alias_form_id, BusterA64AliasRowInfo* result)
{
    u32 ordinal = 0;
    BusterA64AliasGeneratedRow const* row = 0;
    return result && alias_row_for_form(alias_form_id, &ordinal, &row) && buster_a64_alias_row(ordinal, result);
}

bool buster_a64_alias_find(String8 id, u32 ordinal, u32* alias_form_id)
{
    if (alias_form_id)
    {
        u32 seen = 0;
        for (u32 index = 0; index < BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT; index += 1)
        {
            BusterA64AliasRowInfo row = {0};
            if (buster_a64_alias_row(index, &row) && alias_string_equal(row.alias_id, id))
            {
                if (seen == ordinal)
                {
                    *alias_form_id = row.alias_form_id;
                    return true;
                }
                seen += 1;
            }
        }
    }

    return false;
}

bool buster_a64_alias_condition_supported(u32 alias_form_id)
{
    return alias_program_syntax_supported(alias_form_id, false);
}

bool buster_a64_alias_preference_supported(u32 alias_form_id)
{
    return alias_program_syntax_supported(alias_form_id, true);
}

BusterA64AliasStatus buster_a64_alias_encode(Target target, BusterA64AliasInstruction const* instruction, u32* word)
{
    if (!instruction || !word || instruction->operand_count > BUSTER_A64_ALIAS_MAX_OPERANDS)
    {
        return BUSTER_A64_ALIAS_STATUS_INVALID_ARGUMENT;
    }
    if (!alias_target_valid(target))
    {
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    }
    BusterA64AliasRowInfo info = {0};
    BusterA64SemanticForm form = {0};
    if (!buster_a64_alias_row_by_form(instruction->alias_form_id, &info) || !alias_form(instruction->alias_form_id, &form))
    {
        return BUSTER_A64_ALIAS_STATUS_BOUNDS;
    }
    if (instruction->operand_count != form.operand_count || form.field_count > BUSTER_A64_ALIAS_MAX_FIELDS)
    {
        return BUSTER_A64_ALIAS_STATUS_INVALID_ARGUMENT;
    }
    u32 alias_fields[BUSTER_A64_ALIAS_MAX_FIELDS] = {0};
    u64 assigned = 0;
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        u32 field_id = 0, local = 0, value = 0;
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand) || !alias_operand_value_supported(operand, instruction->operands[index]) ||
            !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) || !alias_field_local(form, field_id, &local) ||
            !alias_value_uint(instruction->operands[index], &value) || !alias_assign_field(form, local, value, alias_fields, &assigned))
        {
            return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
        }
    }
    u32 candidate = 0;
    if (!alias_pack_word(form, alias_fields, assigned, &candidate) || (candidate & info.fixed_mask) != info.fixed_value)
    {
        return BUSTER_A64_ALIAS_STATUS_RANGE;
    }
    BusterA64SemanticForm target_form = {0};
    u32 target_canonical_index = 0;
    if (!buster_a64_semantic_form(info.target_form_id, &target_form) || target_form.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL ||
        !alias_canonical_index(info.target_source_digest, &target_canonical_index, 0) ||
        !buster_aarch64_canonical_raw_decode(target_canonical_index, candidate, alias_fields, target_form.field_count) ||
        !alias_target_word_matches(target, target_canonical_index, info.target_source_digest, candidate))
    {
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    }
    bool supported = false, condition = false;
    if (!alias_eval_program(instruction->alias_form_id, candidate, false, &supported, &condition))
    {
        return BUSTER_A64_ALIAS_STATUS_BOUNDS;
    }
    if (!supported)
    {
        return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
    }
    if (!condition)
    {
        return BUSTER_A64_ALIAS_STATUS_CONDITION_FALSE;
    }
    *word = candidate;
    return BUSTER_A64_ALIAS_STATUS_OK;
}

BusterA64AliasStatus buster_a64_alias_decode_row(Target target, u32 alias_form_id, u32 word, BusterA64AliasResult* result)
{
    if (!result)
    {
        return BUSTER_A64_ALIAS_STATUS_INVALID_ARGUMENT;
    }
    if (!alias_target_valid(target))
    {
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    }
    BusterA64AliasRowInfo info = {0};
    BusterA64SemanticForm form = {0}, target_form = {0};
    if (!buster_a64_alias_row_by_form(alias_form_id, &info) || !alias_form(alias_form_id, &form) ||
        !buster_a64_semantic_form(info.target_form_id, &target_form) || (word & info.fixed_mask) != info.fixed_value ||
        form.operand_count > BUSTER_A64_ALIAS_MAX_OPERANDS || target_form.field_count > BUSTER_A64_ALIAS_MAX_FIELDS)
    {
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    }
    u32 target_fields[BUSTER_A64_ALIAS_MAX_FIELDS] = {0};
    u32 target_canonical_index = 0;
    if (!alias_canonical_index(info.target_source_digest, &target_canonical_index, 0) ||
        !buster_aarch64_canonical_raw_decode(target_canonical_index, word, target_fields, target_form.field_count) ||
        !alias_target_word_matches(target, target_canonical_index, info.target_source_digest, word))
    {
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    }
    bool supported = false, condition = false;
    if (!alias_eval_program(alias_form_id, word, false, &supported, &condition))
    {
        return BUSTER_A64_ALIAS_STATUS_BOUNDS;
    }
    if (!supported)
    {
        return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
    }
    if (!condition)
    {
        return BUSTER_A64_ALIAS_STATUS_CONDITION_FALSE;
    }
    bool preference_supported = false, preference = false;
    if (!alias_eval_program(alias_form_id, word, true, &preference_supported, &preference))
    {
        return BUSTER_A64_ALIAS_STATUS_BOUNDS;
    }
    if (!preference_supported)
    {
        return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
    }
    if (!preference)
    {
        return BUSTER_A64_ALIAS_STATUS_CONDITION_FALSE;
    }
    BusterA64AliasResult candidate = {.status = BUSTER_A64_ALIAS_STATUS_OK,
                                      .alias_form_id = alias_form_id,
                                      .target_form_id = info.target_form_id,
                                      .word = word,
                                      .operand_count = (u8)form.operand_count};
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        u32 field_id = 0, local = 0, value = 0;
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand) || operand.transform_count != 0 || operand.field_index_count != 1 ||
            !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) || !alias_field_local(form, field_id, &local) ||
            !alias_field_value_from_word(form, local, word, &value))
        {
            return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
        }
        if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER || operand.kind == BUSTER_A64_SEMANTIC_OPERAND_MEMORY_BASE)
        {
            if (!alias_gpr_value_allowed(operand, value, &candidate.operands[index]))
            {
                return BUSTER_A64_ALIAS_STATUS_UNSUPPORTED;
            }
        }
        else
        {
            candidate.operands[index] = buster_a64_semantic_vm_value_unsigned(value, 32);
        }
        if (candidate.operands[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
        {
            return BUSTER_A64_ALIAS_STATUS_RANGE;
        }
    }
    *result = candidate;
    return BUSTER_A64_ALIAS_STATUS_OK;
}

BusterA64AliasStatus buster_a64_alias_decode(Target target, u32 word, BusterA64AliasResult* result)
{
    if (!result)
    {
        return BUSTER_A64_ALIAS_STATUS_INVALID_ARGUMENT;
    }
    if (!alias_target_valid(target))
    {
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    }
    BusterA64AliasResult candidate = {0};
    bool found = false;
    for (u32 index = 0; index < BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT; index += 1)
    {
        BusterA64AliasRowInfo row = {0};
        if (!buster_a64_alias_row(index, &row) || (word & row.fixed_mask) != row.fixed_value)
        {
            continue;
        }
        BusterA64AliasResult probe = {0};
        BusterA64AliasStatus status = buster_a64_alias_decode_row(target, row.alias_form_id, word, &probe);
        if (status != BUSTER_A64_ALIAS_STATUS_OK)
        {
            continue;
        }
        BusterA64AliasRowInfo current = {0};
        if (found)
        {
            buster_a64_alias_row_by_form(candidate.alias_form_id, &current);
        }
        if (!found || row.preference_rank < current.preference_rank ||
            (row.preference_rank == current.preference_rank && row.alias_form_id < current.alias_form_id))
        {
            candidate = probe;
            found = true;
        }
    }
    if (!found)
    {
        return BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH;
    }
    *result = candidate;
    return BUSTER_A64_ALIAS_STATUS_OK;
}

bool buster_a64_alias_validate(void)
{
    if (BUSTER_A64_ALIAS_PROJECTION_FORM_COUNT != buster_a64_semantic_form_count() ||
        BUSTER_A64_ALIAS_PROJECTION_CANONICAL_COUNT + BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT != BUSTER_A64_ALIAS_PROJECTION_FORM_COUNT)
    {
        return false;
    }
    u32 census[7] = {0};
    for (u32 index = 0; index < BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT; index += 1)
    {
        BusterA64AliasRowInfo row = {0};
        BusterA64SemanticForm form = {0}, target = {0};
        if (!buster_a64_alias_row(index, &row) || !alias_form(row.alias_form_id, &form) || !buster_a64_semantic_form(row.target_form_id, &target) ||
            target.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL || target.source_digest != row.target_source_digest ||
            form.source_digest != row.alias_source_digest || row.target_owner >= 7)
        {
            return false;
        }
        census[row.target_owner] += 1;
    }
    return census[0] == 64 && census[1] == 59 && census[2] == 21 && census[3] == 10 && census[4] == 9 && census[5] == 7 && census[6] == 2;
}
