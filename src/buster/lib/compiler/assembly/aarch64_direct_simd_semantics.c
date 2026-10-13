/*
 * Typed direct AdvSIMD encoder/decoder over the generated direct-SIMD rows.
 *
 * Ownership: the direct-SIMD public API (aarch64_direct_simd_semantics.h),
 * its target-requirement masks and spelling-owned fixed fields.  Row access,
 * arrangement vocabulary and value-table interpreters come from
 * aarch64_simd_semantics through buster_a64_direct_simd_catalog.
 *
 * Map:
 * - buster_a64_direct_simd_requirement_*: target feature requirements.
 * - buster_a64_direct_simd_fixed_field_local: fixed size/Q field overrides.
 * - buster_a64_direct_simd_encode_operand, buster_a64_direct_simd_encode:
 *   operand values to semantic fields to a canonical-checked word.
 * - buster_a64_direct_simd_decode_register,
 *   buster_a64_direct_simd_decode_internal: word to typed operands.
 * - buster_a64_direct_simd_validate: generated-catalog invariants.
 */

#include <buster/lib/compiler/assembly/aarch64_direct_simd_semantics.h>
#include <buster/lib/compiler/assembly/generated/aarch64-direct-simd.generated.h>

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(buster_a64_direct_simd_generated_rows) == BUSTER_A64_DIRECT_SIMD_ROW_COUNT);
BUSTER_CT_CHECK(BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS == BUSTER_A64_SIMD_MAX_OPERANDS);

BUSTER_GLOBAL_LOCAL BusterA64SIMDCatalog const buster_a64_direct_simd_catalog = {
    .rows = buster_a64_direct_simd_generated_rows,
    .bindings = buster_a64_direct_simd_generated_arrangement_bindings,
    .string_pool = buster_a64_direct_simd_generated_string_pool,
    .row_count = BUSTER_A64_DIRECT_SIMD_ROW_COUNT,
};

BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_NEON == 11);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_AES == 111);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_DOTPROD == 117);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_FP_ARMV8 == 119);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_FP16FML == 120);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_FPTOINT == 121);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_FULLFP16 == 122);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_RDM == 132);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_SHA2 == 134);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_SHA3 == 135);

/* TargetCpuFeature stores feature N at bit N-1.  Keep these masks as static
 * data so requirement lookup has no initialization path or per-row storage. */
BUSTER_GLOBAL_LOCAL TargetCpuFeatures const buster_a64_direct_simd_requirement_masks[BUSTER_A64_DIRECT_SIMD_REQUIREMENT_COUNT] = {
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NONE] = {{0, 0, 0, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_AES] = {{0, UINT64_C(1) << 46, 0, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_SHA2] = {{0, 0, UINT64_C(1) << 5, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_SHA3] = {{0, 0, UINT64_C(1) << 6, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON] = {{UINT64_C(1) << 10, 0, 0, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FULLFP16] = {{UINT64_C(1) << 10, UINT64_C(1) << 57, 0, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FP] = {{0, UINT64_C(1) << 54, 0, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FULLFP16] = {{0, UINT64_C(1) << 57, 0, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FP16FML] = {{UINT64_C(1) << 10, UINT64_C(1) << 55, 0, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FPTOINT] = {{UINT64_C(1) << 10, UINT64_C(1) << 56, 0, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD] = {{UINT64_C(1) << 10, UINT64_C(1) << 52, 0, 0}},
    [BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM] = {{UINT64_C(1) << 10, 0, UINT64_C(1) << 3, 0}},
};

bool buster_a64_direct_simd_requirement_features(u8 requirement, TargetCpuFeatures* result)
{
    if (!result || requirement <= BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NONE ||
        requirement >= BUSTER_A64_DIRECT_SIMD_REQUIREMENT_COUNT)
    {
        return false;
    }
    *result = buster_a64_direct_simd_requirement_masks[requirement];
    return true;
}

bool buster_a64_direct_simd_requirement_supported(Target target, u8 requirement)
{
    TargetCpuFeatures required = {0};
    return requirement > BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NONE && target.cpu_arch == CPU_ARCH_AARCH64 &&
           target_cpu_features_are_valid(target) &&
           buster_a64_direct_simd_requirement_features(requirement, &required) &&
           target_cpu_features_subset(required, target_cpu_features_effective(target));
}

BUSTER_GLOBAL_LOCAL bool buster_a64_direct_simd_find_field_cstr(BusterA64SemanticForm form, char8 const* name, u32* local)
{
    if (!name || !local)
    {
        return false;
    }
    BusterA64SemanticString semantic_name = {0};
    for (u32 index = 0; index < form.field_count; index += 1)
    {
        BusterA64SemanticString candidate = {0};
        if (!buster_a64_simd_field_name(form, index, &candidate))
        {
            return false;
        }
        if (buster_a64_simd_semantic_equal_cstr(candidate, name))
        {
            semantic_name = candidate;
            break;
        }
    }
    return semantic_name.length != 0 && buster_a64_simd_find_field(form, semantic_name, local);
}

BUSTER_GLOBAL_LOCAL bool buster_a64_direct_simd_fixed_field_local(BusterA64SemanticForm form, u8 kind, u32* local)
{
    if (!local)
    {
        return false;
    }
    switch ((BusterA64DirectSIMDFixedField)kind)
    {
    case BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_SIZE:
        return buster_a64_direct_simd_find_field_cstr(form, "size", local);
    case BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT:
        return buster_a64_direct_simd_find_field_cstr(form, "Q", local);
    default:
        return false;
    }
}

u32 buster_a64_direct_simd_schema_version(void)
{
    return BUSTER_A64_DIRECT_SIMD_SCHEMA_VERSION;
}
u32 buster_a64_direct_simd_row_count(void)
{
    return BUSTER_A64_DIRECT_SIMD_ROW_COUNT;
}
u32 buster_a64_direct_simd_transform_row_count(void)
{
    return BUSTER_A64_DIRECT_SIMD_TRANSFORM_ROW_COUNT;
}
u32 buster_a64_direct_simd_executable_row_count(void)
{
    return BUSTER_A64_DIRECT_SIMD_EXECUTABLE_ROW_COUNT;
}
u32 buster_a64_direct_simd_max_operands(void)
{
    return BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS;
}
u32 buster_a64_direct_simd_arrangement_binding_count(void)
{
    return BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_BINDING_COUNT;
}

bool buster_a64_direct_simd_row(u32 row_index, BusterA64SIMDRowInfo* result)
{
    return buster_a64_simd_catalog_row_info(&buster_a64_direct_simd_catalog, row_index, result);
}

bool buster_a64_direct_simd_find_source_digest(u64 source_digest, u32* row_index)
{
    return buster_a64_simd_catalog_find_source_digest(&buster_a64_direct_simd_catalog, source_digest, row_index);
}

bool buster_a64_direct_simd_arrangement_binding(u32 row_index, u32 operand_index, BusterA64SIMDArrangementBinding* result)
{
    return buster_a64_simd_catalog_arrangement_binding(&buster_a64_direct_simd_catalog, row_index, operand_index, result);
}

BUSTER_GLOBAL_LOCAL bool buster_a64_direct_simd_assign_operand_fields(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                                                      BusterA64SemanticVMValue value, u32* fields, u64* assigned)
{
    u64 payload = 0;
    u32 offset = 0;
    bool is_list = value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST;
    bool payload_ok = buster_a64_simd_value_uint(value, false, &payload);
    bool offset_ok = buster_a64_simd_member_offset(operand.symbol, &offset);
    if (!payload_ok || payload > 31 || !offset_ok || (!is_list && payload < offset))
    {
        return false;
    }
    if (is_list && offset >= value.aux2)
    {
        return false;
    }
    u32 raw = is_list ? (u32)payload : (u32)((payload - offset) & 31u);
    for (u32 index = 0; index < operand.field_index_count; index += 1)
    {
        u32 field_id = 0, local = 0;
        if (!buster_a64_semantic_operand_field_index(operand.id, index, &field_id) || !buster_a64_simd_field_local(form, field_id, &local) ||
            !buster_a64_simd_assign_field(form, local, raw, fields, assigned))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_direct_simd_register_value_ok(BusterA64SemanticOperand operand, BusterA64SemanticVMValue value)
{
    if (value.payload > 31)
    {
        return false;
    }
    bool index_vector = operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) != 0 &&
                        (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) != 0 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) != 0;
    if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST || (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER) != 0)
    {
        if (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST)
        {
            return false;
        }
    }
    else if (index_vector)
    {
        /* TBL/TBX's presentation `simd_lane` kind is a metadata error: Vm is
         * an index vector and the encoding has no immediate lane selector. */
        if (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR)
        {
            return false;
        }
    }
    else if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE)
    {
        if (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE)
        {
            return false;
        }
    }
    else if (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR &&
             value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR)
    {
        return false;
    }
    if ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) != 0 && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR)
    {
        return false;
    }
    if ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) != 0 && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR &&
        value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST && (!index_vector && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE))
    {
        return false;
    }
    BusterA64SIMDArrangement arrangement = (BusterA64SIMDArrangement)value.aux;
    if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR && !buster_a64_simd_arrangement_is_scalar(arrangement))
    {
        return false;
    }
    if ((value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST ||
         value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE) &&
        !buster_a64_simd_arrangement_is_vector(arrangement))
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool buster_a64_direct_simd_encode_operand(u32 row_index, BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand,
                                                               BusterA64SemanticVMValue const* values, BusterA64SemanticVMValue value, u32* fields,
                                                               u64* assigned)
{
    if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
    {
        return false;
    }
    switch (operand.kind)
    {
    case BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT:
    case BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR:
    case BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR:
        return buster_a64_simd_inverse_tables(form, operand, value, false, fields, assigned);
    case BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER:
    case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST:
    case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE:
    {
        bool value_ok = buster_a64_direct_simd_register_value_ok(operand, value);
        bool arrangement_ok =
            value_ok && buster_a64_simd_catalog_register_arrangement_ok(&buster_a64_direct_simd_catalog, row_index, form, operand_index, values, value);
        bool fields_ok = arrangement_ok && buster_a64_direct_simd_assign_operand_fields(form, operand, value, fields, assigned);
        return value_ok && arrangement_ok && fields_ok;
    }
    case BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER:
        if (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER || value.payload > 31 ||
            (value.flags & ~(BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP)) != 0 ||
            ((value.flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR) != 0 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED) == 0))
        {
            return false;
        }
        return buster_a64_direct_simd_assign_operand_fields(form, operand, value, fields, assigned);
    case BUSTER_A64_SEMANTIC_OPERAND_GPR_WIDTH_SELECTOR:
        return (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER ||
                value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE) &&
               buster_a64_simd_inverse_tables(form, operand, value, false, fields, assigned);
    case BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE:
    {
        u64 immediate = 0;
        u32 field_id = 0, local = 0;
        if (!buster_a64_simd_value_uint(value, false, &immediate) || immediate > UINT32_MAX || operand.field_index_count == 0 ||
            !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) || !buster_a64_simd_field_local(form, field_id, &local))
        {
            return false;
        }
        return buster_a64_simd_assign_field(form, local, (u32)immediate, fields, assigned);
    }
    case BUSTER_A64_SEMANTIC_OPERAND_CONDITION:
        return value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION && value.payload <= 15 &&
               buster_a64_direct_simd_assign_operand_fields(form, operand, value, fields, assigned);
    case BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT:
    {
        u64 constant = 0;
        return operand.symbol.length == 1 && buster_a64_semantic_string_byte(operand.symbol, 0) == '2' && buster_a64_simd_value_uint(value, false, &constant) &&
               constant == 2;
    }
    default:
        return false;
    }
}

BUSTER_GLOBAL_LOCAL bool buster_a64_direct_simd_decode_register(u32 row_index, BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand,
                                                                BusterA64SemanticVMFields const* fields, BusterA64SemanticVMValue const* values,
                                                                BusterA64SemanticVMValue* result)
{
    if (!fields || !values || !result || operand.field_index_count == 0)
    {
        return false;
    }
    u32 field_id = 0, local = 0, offset = 0;
    if (!buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) || !buster_a64_simd_field_local(form, field_id, &local) ||
        !buster_a64_simd_member_offset(operand.symbol, &offset))
    {
        return false;
    }
    bool is_list = operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST || (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER) != 0;
    u32 number = is_list ? fields->values[local] : (fields->values[local] + offset) & 31u;
    BusterA64SIMDArrangement arrangement = BUSTER_A64_SIMD_ARRANGEMENT_INVALID;
    bool arrangement_selected = false;
    BusterA64SIMDGeneratedArrangementBinding const* binding = buster_a64_simd_catalog_binding(&buster_a64_direct_simd_catalog, row_index, operand_index);
    if (binding && binding->selector_index != BUSTER_A64_SIMD_BINDING_NONE)
    {
        if (binding->selector_index >= form.operand_count || values[binding->selector_index].kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT)
        {
            return false;
        }
        arrangement = (BusterA64SIMDArrangement)values[binding->selector_index].aux;
        if (arrangement == BUSTER_A64_SIMD_ARRANGEMENT_INVALID || arrangement >= BUSTER_A64_SIMD_ARRANGEMENT_COUNT)
        {
            return false;
        }
        arrangement_selected = true;
    }
    if (!arrangement_selected)
    {
        arrangement_selected = buster_a64_simd_infer_assembly_arrangement(form, operand_index, operand, false, &arrangement);
    }
    if (!arrangement_selected && !buster_a64_simd_infer_scalar(operand.symbol, &arrangement))
    {
        arrangement = BUSTER_A64_SIMD_ARRANGEMENT_Q;
    }
    bool scalar = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) != 0;
    if (!scalar && arrangement_selected && buster_a64_simd_arrangement_is_scalar(arrangement))
    {
        scalar = true;
    }
    if (!scalar && !arrangement_selected && operand.symbol.length > 1)
    {
        char8 first = buster_a64_semantic_string_byte(operand.symbol, 1);
        scalar = first == 'd' || first == 'n' || first == 'm' || first == 'a' || first == 'b' || first == 'H' || first == 'S' || first == 'D';
    }
    if (is_list)
    {
        u32 count = offset + 1;
        for (u32 index = 0; index < form.operand_count; index += 1)
        {
            BusterA64SemanticOperand candidate = {0};
            u32 candidate_offset = 0;
            if (!buster_a64_semantic_operand(form.operand_first + index, &candidate) || !buster_a64_simd_member_offset(candidate.symbol, &candidate_offset))
            {
                return false;
            }
            if (candidate.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST && candidate_offset + 1 > count)
            {
                count = candidate_offset + 1;
            }
        }
        *result = buster_a64_simd_value_list(number, count, arrangement);
    }
    else if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) != 0 &&
             (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) != 0 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) != 0)
    {
        /* TBL/TBX Vm is an index vector; the presentation metadata calls it
         * simd_lane but there is no encoded element lane to recover. */
        *result = buster_a64_simd_value_vector(number, arrangement);
    }
    else if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE)
    {
        *result = buster_a64_simd_value_lane(number, arrangement, 0);
    }
    else
    {
        *result = buster_a64_simd_value_register(number, arrangement, scalar);
    }
    BUSTER_UNUSED(local);
    return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

BusterA64SIMDStatus buster_a64_direct_simd_encode(Target target, BusterA64DirectSIMDInstruction const* instruction, u32* word)
{
    if (!instruction || !word || instruction->operand_count > BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS)
    {
        return BUSTER_A64_SIMD_STATUS_INVALID_ARGUMENT;
    }
    BusterA64SIMDGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_simd_catalog_row(&buster_a64_direct_simd_catalog, instruction->row_index, &row))
    {
        return BUSTER_A64_SIMD_STATUS_BOUNDS;
    }
    if (!row->executable)
    {
        return BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
    }
    if (!buster_a64_semantic_form(row->semantic_form_id, &form) || form.operand_count != instruction->operand_count ||
        form.operand_count > BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS)
    {
        return BUSTER_A64_SIMD_STATUS_INVALID_ARGUMENT;
    }
    u32 fields[64] = {0};
    u64 assigned = 0;
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand) ||
            !buster_a64_direct_simd_encode_operand(instruction->row_index, form, index, operand, instruction->operands, instruction->operands[index], fields,
                                                   &assigned))
        {
            return BUSTER_A64_SIMD_STATUS_RANGE;
        }
    }
    if (instruction->fixed_field_kind != BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_NONE)
    {
        u32 local = 0;
        if (!buster_a64_direct_simd_fixed_field_local(form, instruction->fixed_field_kind, &local) ||
            !buster_a64_simd_assign_field(form, local, instruction->fixed_field_value, fields, &assigned))
        {
            return BUSTER_A64_SIMD_STATUS_RANGE;
        }
    }
    BusterA64SemanticVMFields vm_fields = {.count = form.field_count};
    for (u32 index = 0; index < form.field_count; index += 1)
    {
        vm_fields.values[index] = fields[index];
    }
    u32 candidate = 0;
    BusterA64SemanticVMStatus vm_status = buster_a64_semantic_vm_encode_fields(form.id, &vm_fields, &candidate);
    if (vm_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK)
    {
        return buster_a64_simd_vm_status(vm_status);
    }
    BusterAarch64CanonicalDecodeResult canonical = {0};
    BusterAarch64CanonicalDecodeStatus canonical_status = buster_aarch64_canonical_decode(target, candidate, &canonical);
    if (canonical_status != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS)
    {
        return buster_a64_simd_canonical_status(canonical_status);
    }
    if (canonical.arm_row_digest != row->source_digest)
    {
        return BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH;
    }
    *word = candidate;
    return BUSTER_A64_SIMD_STATUS_OK;
}

BUSTER_GLOBAL_LOCAL BusterA64SIMDStatus buster_a64_direct_simd_decode_internal(Target target, u32 requested_row, bool row_requested, u32 word,
                                                                               BusterA64DirectSIMDResult* result)
{
    if (!result)
    {
        return BUSTER_A64_SIMD_STATUS_INVALID_ARGUMENT;
    }
    BusterAarch64CanonicalDecodeResult canonical = {0};
    BusterAarch64CanonicalDecodeStatus canonical_status = buster_aarch64_canonical_decode(target, word, &canonical);
    if (canonical_status != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS)
    {
        return buster_a64_simd_canonical_status(canonical_status);
    }
    u32 row_index = 0;
    if (!buster_a64_simd_catalog_find_source_digest(&buster_a64_direct_simd_catalog, canonical.arm_row_digest, &row_index) ||
        (row_requested && row_index != requested_row))
    {
        return BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH;
    }
    BusterA64SIMDGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_simd_catalog_row(&buster_a64_direct_simd_catalog, row_index, &row) || !row->executable ||
        !buster_a64_semantic_form(row->semantic_form_id, &form))
    {
        return BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
    }
    BusterA64SemanticVMResult decoded = {0};
    BusterA64SemanticVMStatus vm_status = buster_a64_semantic_vm_decode_fields(form.id, word, &decoded);
    if (vm_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK)
    {
        return buster_a64_simd_vm_status(vm_status);
    }
    BusterA64SemanticVMValue values[BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS] = {0};
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand))
        {
            return BUSTER_A64_SIMD_STATUS_BOUNDS;
        }
        if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT || operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR ||
            operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR)
        {
            if (!buster_a64_simd_decode_arrangement(form, operand, &decoded.fields, &values[index]))
            {
                return BUSTER_A64_SIMD_STATUS_RESERVED;
            }
        }
    }
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand))
        {
            return BUSTER_A64_SIMD_STATUS_BOUNDS;
        }
        switch (operand.kind)
        {
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR:
            /* Arrangement selectors were evaluated in the first pass. */
            break;
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE:
            if (!buster_a64_direct_simd_decode_register(row_index, form, index, operand, &decoded.fields, values, &values[index]))
            {
                return BUSTER_A64_SIMD_STATUS_RANGE;
            }
            break;
        case BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER:
        {
            u32 field_id = 0, local = 0;
            if (operand.field_index_count == 0 || !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) ||
                !buster_a64_simd_field_local(form, field_id, &local))
            {
                return BUSTER_A64_SIMD_STATUS_BOUNDS;
            }
            values[index] = buster_a64_simd_value_gpr(decoded.fields.values[local], (operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_X64) != 0 ? 64 : 32,
                                                      decoded.fields.values[local] == 31 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED) != 0);
        }
        break;
        case BUSTER_A64_SEMANTIC_OPERAND_GPR_WIDTH_SELECTOR:
        {
            values[index] = buster_a64_semantic_vm_value_invalid();
            for (u32 transform_index = 0; transform_index < operand.transform_count; transform_index += 1)
            {
                BusterA64SemanticVMValue candidate = buster_a64_semantic_vm_value_invalid();
                if (buster_a64_semantic_vm_eval_transform(form.id, operand.transform_first + transform_index, &decoded.fields, &candidate) ==
                        BUSTER_A64_SEMANTIC_VM_STATUS_OK &&
                    candidate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION)
                {
                    values[index] = candidate;
                    break;
                }
            }
            if (values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
            {
                return BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
            }
        }
        break;
        case BUSTER_A64_SEMANTIC_OPERAND_CONDITION:
        {
            u32 field_id = 0, local = 0;
            if (operand.field_index_count == 0 || !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) ||
                !buster_a64_simd_field_local(form, field_id, &local))
            {
                return BUSTER_A64_SIMD_STATUS_BOUNDS;
            }
            values[index] = buster_a64_semantic_vm_value_condition(decoded.fields.values[local]);
        }
        break;
        case BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT:
            values[index] = buster_a64_semantic_vm_value_unsigned(2, 2);
            break;
        default:
            return BUSTER_A64_SIMD_STATUS_UNSUPPORTED;
        }
        if (values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
        {
            return BUSTER_A64_SIMD_STATUS_RANGE;
        }
    }
    u32 reencoded = 0;
    vm_status = buster_a64_semantic_vm_encode_fields(form.id, &decoded.fields, &reencoded);
    if (vm_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK || reencoded != word)
    {
        return BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH;
    }
    BusterA64DirectSIMDResult candidate = {.status = BUSTER_A64_SIMD_STATUS_OK, .row_index = row_index, .word = word, .operand_count = form.operand_count};
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        candidate.operands[index] = values[index];
    }
    *result = candidate;
    return BUSTER_A64_SIMD_STATUS_OK;
}

BusterA64SIMDStatus buster_a64_direct_simd_decode(Target target, u32 word, BusterA64DirectSIMDResult* result)
{
    return buster_a64_direct_simd_decode_internal(target, 0, false, word, result);
}

BusterA64SIMDStatus buster_a64_direct_simd_decode_row(Target target, u32 row_index, u32 word, BusterA64DirectSIMDResult* result)
{
    if (row_index >= BUSTER_A64_DIRECT_SIMD_ROW_COUNT)
    {
        return BUSTER_A64_SIMD_STATUS_BOUNDS;
    }
    return buster_a64_direct_simd_decode_internal(target, row_index, true, word, result);
}

bool buster_a64_direct_simd_validate(void)
{
    if (BUSTER_A64_DIRECT_SIMD_ROW_COUNT != 390u || BUSTER_A64_DIRECT_SIMD_TRANSFORM_ROW_COUNT != 263u || BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS != 8u ||
        BUSTER_A64_DIRECT_SIMD_CROSS_OWNER_GAP_COUNT != 8u)
    {
        return false;
    }
    u32 previous_form = 0;
    for (u32 index = 0; index < BUSTER_A64_DIRECT_SIMD_ROW_COUNT; index += 1)
    {
        BusterA64SIMDGeneratedRow const* row = 0;
        BusterA64SemanticForm form = {0};
        if (!buster_a64_simd_catalog_row(&buster_a64_direct_simd_catalog, index, &row) || !buster_a64_semantic_form(row->semantic_form_id, &form) ||
            form.owner != BUSTER_A64_SEMANTIC_OWNER_DIRECT_SIMD || form.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL ||
            form.status != BUSTER_A64_SEMANTIC_STATUS_DEFINED || (index != 0 && row->semantic_form_id <= previous_form) ||
            row->operand_count != form.operand_count)
        {
            return false;
        }
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterA64SIMDGeneratedArrangementBinding const* binding = buster_a64_simd_catalog_binding(&buster_a64_direct_simd_catalog, index, operand_index);
            if (!binding || binding->selector_index == BUSTER_A64_SIMD_BINDING_NONE)
            {
                continue;
            }
            if (binding->selector_index >= form.operand_count || (binding->direction != -1 && binding->direction != 1))
            {
                return false;
            }
            BusterA64SemanticOperand selector = {0};
            if (!buster_a64_semantic_operand(form.operand_first + binding->selector_index, &selector) ||
                (selector.kind != BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT && selector.kind != BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR &&
                 selector.kind != BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR))
            {
                return false;
            }
            if ((binding->direction == 1 && binding->selector_index != operand_index + 1) ||
                (binding->direction == -1 && (operand_index == 0 || (u32)binding->selector_index + 1u != operand_index)))
            {
                return false;
            }
        }
        previous_form = row->semantic_form_id;
    }
    return true;
}
