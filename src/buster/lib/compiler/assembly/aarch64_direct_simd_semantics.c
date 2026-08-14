#include <buster/lib/compiler/assembly/aarch64_direct_simd_semantics.h>
#include <buster/lib/compiler/assembly/generated/aarch64-direct-simd.generated.h>

BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_NEON == 11);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_AES == 111);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_FP_ARMV8 == 119);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_FP16FML == 120);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_FPTOINT == 121);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_FULLFP16 == 122);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_SHA2 == 134);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_AARCH64_SHA3 == 135);

/* TargetCpuFeature stores feature N at bit N-1.  Keep these masks as static
 * data so requirement lookup has no initialization path or per-row storage. */
static TargetCpuFeatures const buster_a64_direct_simd_requirement_masks[BUSTER_A64_DIRECT_SIMD_REQUIREMENT_COUNT] = {
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

static char8 buster_a64_direct_simd_arrangement_b[] = "B";
static char8 buster_a64_direct_simd_arrangement_h[] = "H";
static char8 buster_a64_direct_simd_arrangement_s[] = "S";
static char8 buster_a64_direct_simd_arrangement_d[] = "D";
static char8 buster_a64_direct_simd_arrangement_q[] = "Q";
static char8 buster_a64_direct_simd_arrangement_1b[] = "1B";
static char8 buster_a64_direct_simd_arrangement_2b[] = "2B";
static char8 buster_a64_direct_simd_arrangement_4b[] = "4B";
static char8 buster_a64_direct_simd_arrangement_8b[] = "8B";
static char8 buster_a64_direct_simd_arrangement_16b[] = "16B";
static char8 buster_a64_direct_simd_arrangement_1h[] = "1H";
static char8 buster_a64_direct_simd_arrangement_2h[] = "2H";
static char8 buster_a64_direct_simd_arrangement_4h[] = "4H";
static char8 buster_a64_direct_simd_arrangement_8h[] = "8H";
static char8 buster_a64_direct_simd_arrangement_1s[] = "1S";
static char8 buster_a64_direct_simd_arrangement_2s[] = "2S";
static char8 buster_a64_direct_simd_arrangement_4s[] = "4S";
static char8 buster_a64_direct_simd_arrangement_1d[] = "1D";
static char8 buster_a64_direct_simd_arrangement_2d[] = "2D";

static String8 buster_a64_direct_simd_arrangement_text(BusterA64DirectSIMDArrangement arrangement)
{
    switch (arrangement)
    {
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B:
        return (String8){buster_a64_direct_simd_arrangement_b, 1};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_H:
        return (String8){buster_a64_direct_simd_arrangement_h, 1};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S:
        return (String8){buster_a64_direct_simd_arrangement_s, 1};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D:
        return (String8){buster_a64_direct_simd_arrangement_d, 1};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q:
        return (String8){buster_a64_direct_simd_arrangement_q, 1};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1B:
        return (String8){buster_a64_direct_simd_arrangement_1b, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2B:
        return (String8){buster_a64_direct_simd_arrangement_2b, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4B:
        return (String8){buster_a64_direct_simd_arrangement_4b, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B:
        return (String8){buster_a64_direct_simd_arrangement_8b, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B:
        return (String8){buster_a64_direct_simd_arrangement_16b, 3};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1H:
        return (String8){buster_a64_direct_simd_arrangement_1h, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2H:
        return (String8){buster_a64_direct_simd_arrangement_2h, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4H:
        return (String8){buster_a64_direct_simd_arrangement_4h, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8H:
        return (String8){buster_a64_direct_simd_arrangement_8h, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1S:
        return (String8){buster_a64_direct_simd_arrangement_1s, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2S:
        return (String8){buster_a64_direct_simd_arrangement_2s, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S:
        return (String8){buster_a64_direct_simd_arrangement_4s, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1D:
        return (String8){buster_a64_direct_simd_arrangement_1d, 2};
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2D:
        return (String8){buster_a64_direct_simd_arrangement_2d, 2};
    default:
        return (String8){0};
    }
}

static bool buster_a64_direct_simd_string_equal(String8 left, String8 right)
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

static bool buster_a64_direct_simd_semantic_equal_cstr(BusterA64SemanticString left, char8 const* right)
{
    if (!right)
    {
        return false;
    }
    u32 length = 0;
    while (right[length] != 0)
    {
        length += 1;
    }
    if (left.length != length)
    {
        return false;
    }
    for (u32 index = 0; index < length; index += 1)
    {
        if (buster_a64_semantic_string_byte(left, index) != right[index])
        {
            return false;
        }
    }
    return true;
}

static bool buster_a64_direct_simd_semantic_equal(BusterA64SemanticString left, BusterA64SemanticString right)
{
    if (left.length != right.length)
    {
        return false;
    }
    for (u32 index = 0; index < left.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(left, index) != buster_a64_semantic_string_byte(right, index))
        {
            return false;
        }
    }
    return true;
}

static bool buster_a64_direct_simd_value_uint(BusterA64SemanticVMValue value, u64* result)
{
    if (!result)
    {
        return false;
    }
    switch (value.kind)
    {
    case BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER:
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
        *result = value.payload;
        return true;
    default:
        return false;
    }
}

static bool buster_a64_direct_simd_field_local(BusterA64SemanticForm form, u32 field_id, u32* local)
{
    if (!local || field_id < form.field_first || field_id >= form.field_first + form.field_count)
    {
        return false;
    }
    *local = field_id - form.field_first;
    return true;
}

static bool buster_a64_direct_simd_field_name(BusterA64SemanticForm form, u32 local, BusterA64SemanticString* name)
{
    BusterA64SemanticField field = {0};
    if (!name || local >= form.field_count || !buster_a64_semantic_field(form.field_first + local, &field))
    {
        return false;
    }
    *name = field.name;
    return true;
}

static bool buster_a64_direct_simd_find_field(BusterA64SemanticForm form, BusterA64SemanticString name, u32* local)
{
    if (!local)
    {
        return false;
    }
    u32 count = 0;
    for (u32 index = 0; index < form.field_count; index += 1)
    {
        BusterA64SemanticString candidate = {0};
        if (!buster_a64_direct_simd_field_name(form, index, &candidate))
        {
            return false;
        }
        if (buster_a64_direct_simd_semantic_equal(name, candidate))
        {
            count += 1;
            *local = index;
        }
    }
    return count == 1;
}

static bool buster_a64_direct_simd_assign_field(BusterA64SemanticForm form, u32 local, u32 value, u32* fields, u64* assigned)
{
    if (!fields || !assigned || local >= form.field_count || local >= 64)
    {
        return false;
    }
    BusterA64SemanticField field = {0};
    if (!buster_a64_semantic_field(form.field_first + local, &field) || field.width == 0 || field.width > 32)
    {
        return false;
    }
    u32 mask = field.width == 32 ? UINT32_MAX : ((UINT32_C(1) << field.width) - 1);
    if ((value & ~mask) != 0)
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

static bool buster_a64_direct_simd_member_offset(BusterA64SemanticString symbol, u32* offset)
{
    if (!offset)
    {
        return false;
    }
    *offset = 0;
    u32 plus = UINT32_MAX;
    for (u32 index = 0; index < symbol.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(symbol, index) == '+')
        {
            plus = index;
            break;
        }
    }
    if (plus == UINT32_MAX)
    {
        return true;
    }
    if (plus + 1 >= symbol.length)
    {
        return false;
    }
    u32 end = symbol.length;
    if (end != 0 && buster_a64_semantic_string_byte(symbol, end - 1) == '>')
    {
        end -= 1;
    }
    if (plus + 1 >= end)
    {
        return false;
    }
    u32 value = 0;
    for (u32 index = plus + 1; index < end; index += 1)
    {
        char8 digit = buster_a64_semantic_string_byte(symbol, index);
        if (digit < '0' || digit > '9')
        {
            return false;
        }
        value = value * 10u + (u32)(digit - '0');
    }
    if (value > 31)
    {
        return false;
    }
    *offset = value;
    return true;
}

static bool buster_a64_direct_simd_parse_bits(BusterA64SemanticString text, u32* value, u32* mask, u8* width)
{
    if (!value || !mask || !width || text.length == 0 || text.length > 32)
    {
        return false;
    }
    u32 result_value = 0, result_mask = 0;
    for (u32 index = 0; index < text.length; index += 1)
    {
        char8 digit = buster_a64_semantic_string_byte(text, index);
        u32 shift = text.length - index - 1;
        if (digit == '0' || digit == '1')
        {
            result_mask |= UINT32_C(1) << shift;
            if (digit == '1')
            {
                result_value |= UINT32_C(1) << shift;
            }
        }
        else if (digit != 'x' && digit != 'X')
        {
            return false;
        }
    }
    *value = result_value;
    *mask = result_mask;
    *width = (u8)text.length;
    return true;
}

static bool buster_a64_direct_simd_row_valid(u32 row_index, BusterA64DirectSIMDGeneratedRow const** result)
{
    if (!result || row_index >= BUSTER_A64_DIRECT_SIMD_ROW_COUNT || row_index >= BUSTER_ARRAY_LENGTH(buster_a64_direct_simd_generated_rows))
    {
        return false;
    }
    BusterA64DirectSIMDGeneratedRow const* row = buster_a64_direct_simd_generated_rows + row_index;
    if (row->row_index != row_index || row->semantic_form_id >= buster_a64_semantic_form_count() || row->operand_count > BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS)
    {
        return false;
    }
    *result = row;
    return true;
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

bool buster_a64_direct_simd_row(u32 row_index, BusterA64DirectSIMDRowInfo* result)
{
    if (!result)
    {
        return false;
    }
    BusterA64DirectSIMDGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_direct_simd_row_valid(row_index, &row) || !buster_a64_semantic_form(row->semantic_form_id, &form))
    {
        return false;
    }
    *result = (BusterA64DirectSIMDRowInfo){.row_index = row_index,
                                           .semantic_form_id = row->semantic_form_id,
                                           .source_digest = row->source_digest,
                                           .id = {(char8*)buster_a64_direct_simd_generated_string_pool + row->id_offset, row->id_length},
                                           .assembly = {(char8*)buster_a64_direct_simd_generated_string_pool + row->assembly_offset, row->assembly_length},
                                           .operand_count = row->operand_count,
                                           .executable = row->executable,
                                           .transform_bearing = (u8)(form.transform_count != 0)};
    return true;
}

bool buster_a64_direct_simd_find_source_digest(u64 source_digest, u32* row_index)
{
    if (!row_index)
    {
        return false;
    }
    u32 count = 0, selected = 0;
    for (u32 index = 0; index < BUSTER_A64_DIRECT_SIMD_ROW_COUNT; index += 1)
    {
        if (buster_a64_direct_simd_generated_rows[index].source_digest == source_digest)
        {
            count += 1;
            selected = index;
        }
    }
    if (count != 1)
    {
        return false;
    }
    *row_index = selected;
    return true;
}

static BusterA64DirectSIMDGeneratedArrangementBinding const* buster_a64_direct_simd_generated_binding(u32 row_index, u32 operand_index)
{
    if (row_index >= BUSTER_A64_DIRECT_SIMD_ROW_COUNT || operand_index >= BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS)
    {
        return 0;
    }
    return &buster_a64_direct_simd_generated_arrangement_bindings[row_index][operand_index];
}

bool buster_a64_direct_simd_arrangement_binding(u32 row_index, u32 operand_index, BusterA64DirectSIMDArrangementBinding* result)
{
    if (!result)
    {
        return false;
    }
    BusterA64DirectSIMDGeneratedRow const* row = 0;
    BusterA64DirectSIMDGeneratedArrangementBinding const* binding = buster_a64_direct_simd_generated_binding(row_index, operand_index);
    if (!binding || !buster_a64_direct_simd_row_valid(row_index, &row) || operand_index >= row->operand_count ||
        binding->selector_index == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_BINDING_NONE)
    {
        return false;
    }
    *result = (BusterA64DirectSIMDArrangementBinding){.selector_index = binding->selector_index, .direction = binding->direction};
    return true;
}

bool buster_a64_direct_simd_arrangement_from_string(String8 text, BusterA64DirectSIMDArrangement* result)
{
    if (!result)
    {
        return false;
    }
    for (u32 index = 1; index < BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_COUNT; index += 1)
    {
        if (buster_a64_direct_simd_string_equal(text, buster_a64_direct_simd_arrangement_text((BusterA64DirectSIMDArrangement)index)))
        {
            *result = (BusterA64DirectSIMDArrangement)index;
            return true;
        }
    }
    return false;
}

String8 buster_a64_direct_simd_arrangement_string(BusterA64DirectSIMDArrangement arrangement)
{
    return buster_a64_direct_simd_arrangement_text(arrangement);
}

static u8 buster_a64_direct_simd_scalar_width(BusterA64DirectSIMDArrangement arrangement)
{
    switch (arrangement)
    {
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B:
        return 8;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_H:
        return 16;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S:
        return 32;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D:
        return 64;
    default:
        return 0;
    }
}

static u8 buster_a64_direct_simd_lane_count(BusterA64DirectSIMDArrangement arrangement)
{
    switch (arrangement)
    {
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1B:
        return 1;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_H:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1H:
        return 1;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1S:
        return 1;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1D:
        return 1;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2B:
        return 2;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4B:
        return 4;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B:
        return 8;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B:
        return 16;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2H:
        return 2;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4H:
        return 4;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8H:
        return 8;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2S:
        return 2;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S:
        return 4;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2D:
        return 2;
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q:
        return 1;
    default:
        return 0;
    }
}

static u8 buster_a64_direct_simd_register_width(BusterA64DirectSIMDArrangement arrangement)
{
    if (arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q)
    {
        return 128;
    }
    switch (arrangement)
    {
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1B:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2B:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4B:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B:
        return (u8)(8u * buster_a64_direct_simd_lane_count(arrangement));
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1H:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2H:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4H:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8H:
        return (u8)(16u * buster_a64_direct_simd_lane_count(arrangement));
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1S:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2S:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S:
        return (u8)(32u * buster_a64_direct_simd_lane_count(arrangement));
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1D:
    case BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2D:
        return (u8)(64u * buster_a64_direct_simd_lane_count(arrangement));
    default:
        return 0;
    }
}

static bool buster_a64_direct_simd_arrangement_is_scalar(BusterA64DirectSIMDArrangement arrangement)
{
    return arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B || arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_H ||
           arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S || arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D;
}

static bool buster_a64_direct_simd_arrangement_is_vector(BusterA64DirectSIMDArrangement arrangement)
{
    return arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q || buster_a64_direct_simd_register_width(arrangement) != 0;
}

BusterA64SemanticVMValue buster_a64_direct_simd_value_arrangement(BusterA64DirectSIMDArrangement arrangement)
{
    if (arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID || arrangement >= BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_COUNT)
    {
        return buster_a64_semantic_vm_value_invalid();
    }
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT,
                                      .width = 8,
                                      .aux = (u32)arrangement,
                                      .payload = (u64)arrangement};
}

BusterA64SemanticVMValue buster_a64_direct_simd_value_register(u32 number, BusterA64DirectSIMDArrangement arrangement, bool scalar)
{
    u8 width = scalar ? buster_a64_direct_simd_scalar_width(arrangement) : buster_a64_direct_simd_register_width(arrangement);
    if (number > 31 || width == 0 || arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID || arrangement >= BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_COUNT)
    {
        return buster_a64_semantic_vm_value_invalid();
    }
    return (BusterA64SemanticVMValue){.kind = scalar ? BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR : BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR,
                                      .width = width,
                                      .aux = (u32)arrangement,
                                      .payload = number};
}
BusterA64SemanticVMValue buster_a64_direct_simd_value_vector(u32 number, BusterA64DirectSIMDArrangement arrangement)
{
    return buster_a64_direct_simd_value_register(number, arrangement, false);
}
BusterA64SemanticVMValue buster_a64_direct_simd_value_scalar(u32 number, BusterA64DirectSIMDArrangement arrangement)
{
    return buster_a64_direct_simd_value_register(number, arrangement, true);
}

BusterA64SemanticVMValue buster_a64_direct_simd_value_list(u32 first, u32 count, BusterA64DirectSIMDArrangement arrangement)
{
    u8 width = buster_a64_direct_simd_register_width(arrangement);
    if (first > 31 || count == 0 || count > 4 || width == 0 || arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID ||
        arrangement >= BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_COUNT)
    {
        return buster_a64_semantic_vm_value_invalid();
    }
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST,
                                      .width = width,
                                      .aux = (u32)arrangement,
                                      .aux2 = count,
                                      .payload = first};
}

BusterA64SemanticVMValue buster_a64_direct_simd_value_lane(u32 number, BusterA64DirectSIMDArrangement arrangement, u32 lane)
{
    u8 width = buster_a64_direct_simd_register_width(arrangement);
    u32 count = buster_a64_direct_simd_lane_count(arrangement);
    if (number > 31 || width == 0 || count == 0 || lane >= count)
    {
        return buster_a64_semantic_vm_value_invalid();
    }
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE,
                                      .width = width,
                                      .aux = (u32)arrangement,
                                      .aux2 = lane,
                                      .payload = number};
}
BusterA64SemanticVMValue buster_a64_direct_simd_value_gpr(u32 number, u8 width, bool zr)
{
    return buster_a64_semantic_vm_value_gpr(number, width, false, zr);
}
BusterA64SemanticVMValue buster_a64_direct_simd_value_gpr_width(u8 width)
{
    return (width == 32 || width == 64) ? buster_a64_semantic_vm_value_unsigned(width, 7) : buster_a64_semantic_vm_value_invalid();
}
BusterA64SemanticVMValue buster_a64_direct_simd_value_immediate(u64 value, u8 width)
{
    BusterA64SemanticVMValue result = buster_a64_semantic_vm_value_unsigned(value, width);
    if (result.kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
    {
        result.kind = BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE;
    }
    return result;
}

static bool buster_a64_direct_simd_result_matches(BusterA64SemanticValueAtom atom, BusterA64SemanticVMValue desired)
{
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM)
    {
        if (desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION)
        {
            return buster_a64_direct_simd_semantic_equal(atom.text, desired.text);
        }
        if (desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE)
        {
            if (buster_a64_direct_simd_semantic_equal_cstr(atom.text, "W"))
            {
                return desired.payload == 32;
            }
            if (buster_a64_direct_simd_semantic_equal_cstr(atom.text, "X"))
            {
                return desired.payload == 64;
            }
        }
        if (desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR ||
            desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST ||
            desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE)
        {
            String8 text = buster_a64_direct_simd_arrangement_text((BusterA64DirectSIMDArrangement)desired.aux);
            if (text.length != atom.text.length)
            {
                return false;
            }
            for (u64 index = 0; index < text.length; index += 1)
            {
                if (text.pointer[index] != buster_a64_semantic_string_byte(atom.text, (u32)index))
                {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER)
    {
        u64 value = 0;
        return atom.integer >= 0 && buster_a64_direct_simd_value_uint(desired, &value) && value == (u64)atom.integer;
    }
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS)
    {
        u32 expected = 0, mask = 0;
        u8 width = 0;
        u64 value = 0;
        return buster_a64_direct_simd_parse_bits(atom.text, &expected, &mask, &width) && buster_a64_direct_simd_value_uint(desired, &value) &&
               (value & mask) == (expected & mask);
    }
    return false;
}

static bool buster_a64_direct_simd_apply_table_entry(BusterA64SemanticForm form, BusterA64SemanticTransform transform, BusterA64SemanticValue entry,
                                                     u32* fields, u64* assigned)
{
    BusterA64SemanticTableHeader table = {0};
    if (!buster_a64_semantic_table_header(transform.table_id, &table) || entry.key_count != table.key_header_count)
    {
        return false;
    }
    for (u32 index = 0; index < entry.key_count; index += 1)
    {
        BusterA64SemanticString key_name = {0};
        BusterA64SemanticValueAtom atom = {0};
        u32 local = 0;
        if (!buster_a64_semantic_table_key_header(transform.table_id, index, &key_name) || !buster_a64_semantic_value_atom(entry.key_first + index, &atom) ||
            !buster_a64_direct_simd_find_field(form, key_name, &local))
        {
            return false;
        }
        if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER)
        {
            if (atom.integer < 0 || atom.integer > UINT32_MAX || !buster_a64_direct_simd_assign_field(form, local, (u32)atom.integer, fields, assigned))
            {
                return false;
            }
        }
        else if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS)
        {
            u32 expected = 0, mask = 0;
            u8 width = 0;
            BusterA64SemanticField field = {0};
            if (!buster_a64_direct_simd_parse_bits(atom.text, &expected, &mask, &width) || !buster_a64_semantic_field(form.field_first + local, &field) ||
                width > field.width)
            {
                return false;
            }
            if ((*assigned & (UINT64_C(1) << local)) != 0)
            {
                if ((fields[local] & mask) != (expected & mask))
                {
                    return false;
                }
            }
            else if (!buster_a64_direct_simd_assign_field(form, local, expected, fields, assigned))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    return true;
}

static bool buster_a64_direct_simd_inverse_tables(BusterA64SemanticForm form, BusterA64SemanticOperand operand, BusterA64SemanticVMValue desired, u32* fields,
                                                  u64* assigned)
{
    for (u32 index = 0; index < operand.transform_count; index += 1)
    {
        u32 transform_id = operand.transform_first + index;
        BusterA64SemanticTransform transform = {0};
        if (!buster_a64_semantic_transform(transform_id, &transform))
        {
            return false;
        }
        if (transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE)
        {
            continue;
        }
        BusterA64SemanticString result_header = {0};
        if (!buster_a64_semantic_table_result_header(transform.table_id, &result_header))
        {
            return false;
        }
        u32 matches = 0;
        u32 selected[64] = {0};
        u64 selected_mask = 0;
        for (u32 entry_index = 0; entry_index < transform.value_count; entry_index += 1)
        {
            BusterA64SemanticValue entry = {0};
            BusterA64SemanticValueAtom result_atom = {0};
            if (!buster_a64_semantic_transform_value(transform_id, entry_index, &entry) || entry.result_count != 1 ||
                !buster_a64_semantic_value_atom(entry.result_first, &result_atom))
            {
                return false;
            }
            if (result_atom.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM && buster_a64_direct_simd_semantic_equal_cstr(result_atom.text, "RESERVED"))
            {
                continue;
            }
            if (!buster_a64_direct_simd_result_matches(result_atom, desired))
            {
                continue;
            }
            u32 candidate[64] = {0};
            for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
            {
                candidate[field_index] = fields[field_index];
            }
            u64 candidate_mask = *assigned;
            if (!buster_a64_direct_simd_apply_table_entry(form, transform, entry, candidate, &candidate_mask))
            {
                continue;
            }
            matches += 1;
            if (matches == 1)
            {
                for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
                {
                    selected[field_index] = candidate[field_index];
                }
                selected_mask = candidate_mask;
            }
            else
            {
                for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
                {
                    if (selected[field_index] != candidate[field_index] || ((selected_mask ^ candidate_mask) & (UINT64_C(1) << field_index)) != 0)
                    {
                        return false;
                    }
                }
            }
        }
        if (matches == 0)
        {
            return false;
        }
        for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
        {
            fields[field_index] = selected[field_index];
        }
        *assigned = selected_mask;
    }
    return true;
}

static bool buster_a64_direct_simd_assign_operand_fields(BusterA64SemanticForm form, BusterA64SemanticOperand operand, BusterA64SemanticVMValue value,
                                                         u32* fields, u64* assigned)
{
    u64 payload = 0;
    u32 offset = 0;
    bool is_list = value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST;
    bool payload_ok = buster_a64_direct_simd_value_uint(value, &payload);
    bool offset_ok = buster_a64_direct_simd_member_offset(operand.symbol, &offset);
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
        if (!buster_a64_semantic_operand_field_index(operand.id, index, &field_id) || !buster_a64_direct_simd_field_local(form, field_id, &local) ||
            !buster_a64_direct_simd_assign_field(form, local, raw, fields, assigned))
        {
            return false;
        }
    }
    return true;
}

static bool buster_a64_direct_simd_register_value_ok(BusterA64SemanticOperand operand, BusterA64SemanticVMValue value)
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
    BusterA64DirectSIMDArrangement arrangement = (BusterA64DirectSIMDArrangement)value.aux;
    if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR && !buster_a64_direct_simd_arrangement_is_scalar(arrangement))
    {
        return false;
    }
    if ((value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST ||
         value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE) &&
        !buster_a64_direct_simd_arrangement_is_vector(arrangement))
    {
        return false;
    }
    return true;
}

static bool buster_a64_direct_simd_register_arrangement_ok(u32 row_index, BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticVMValue const* values,
                                                           BusterA64SemanticVMValue value)
{
    if (!values || operand_index >= form.operand_count)
    {
        return false;
    }
    BusterA64DirectSIMDGeneratedArrangementBinding const* binding = buster_a64_direct_simd_generated_binding(row_index, operand_index);
    if (!binding || binding->selector_index == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_BINDING_NONE)
    {
        return true;
    }
    if (binding->selector_index >= form.operand_count)
    {
        return false;
    }
    BusterA64SemanticVMValue arrangement = values[binding->selector_index];
    return arrangement.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT && arrangement.aux == value.aux;
}

static bool buster_a64_direct_simd_encode_operand(u32 row_index, BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand,
                                                  BusterA64SemanticVMValue const* values, BusterA64SemanticVMValue value, u32* fields, u64* assigned)
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
        return buster_a64_direct_simd_inverse_tables(form, operand, value, fields, assigned);
    case BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER:
    case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST:
    case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE:
    {
        bool value_ok = buster_a64_direct_simd_register_value_ok(operand, value);
        bool arrangement_ok = value_ok && buster_a64_direct_simd_register_arrangement_ok(row_index, form, operand_index, values, value);
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
               buster_a64_direct_simd_inverse_tables(form, operand, value, fields, assigned);
    case BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE:
    {
        u64 immediate = 0;
        u32 field_id = 0, local = 0;
        if (!buster_a64_direct_simd_value_uint(value, &immediate) || immediate > UINT32_MAX || operand.field_index_count == 0 ||
            !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) || !buster_a64_direct_simd_field_local(form, field_id, &local))
        {
            return false;
        }
        return buster_a64_direct_simd_assign_field(form, local, (u32)immediate, fields, assigned);
    }
    case BUSTER_A64_SEMANTIC_OPERAND_CONDITION:
        return value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION && value.payload <= 15 &&
               buster_a64_direct_simd_assign_operand_fields(form, operand, value, fields, assigned);
    case BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT:
    {
        u64 constant = 0;
        return operand.symbol.length == 1 && buster_a64_semantic_string_byte(operand.symbol, 0) == '2' && buster_a64_direct_simd_value_uint(value, &constant) &&
               constant == 2;
    }
    default:
        return false;
    }
}

static bool buster_a64_direct_simd_decode_arrangement(BusterA64SemanticForm form, BusterA64SemanticOperand operand, BusterA64SemanticVMFields const* fields,
                                                      BusterA64SemanticVMValue* result)
{
    for (u32 index = 0; index < operand.transform_count; index += 1)
    {
        u32 transform_id = operand.transform_first + index;
        BusterA64SemanticTransform transform = {0};
        BusterA64SemanticVMValue value = buster_a64_semantic_vm_value_invalid();
        if (!buster_a64_semantic_transform(transform_id, &transform))
        {
            return false;
        }
        if (transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE && transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_SHARED_DECODE)
        {
            continue;
        }
        BusterA64SemanticVMStatus status = buster_a64_semantic_vm_eval_transform(form.id, transform_id, fields, &value);
        if (status != BUSTER_A64_SEMANTIC_VM_STATUS_OK)
        {
            return false;
        }
        if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT)
        {
            *result = value;
            return true;
        }
        if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION)
        {
            char8 buffer[8] = {0};
            BusterA64DirectSIMDArrangement arrangement = BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID;
            if (value.text.length >= BUSTER_ARRAY_LENGTH(buffer))
            {
                return false;
            }
            for (u32 char_index = 0; char_index < value.text.length && char_index < BUSTER_ARRAY_LENGTH(buffer); char_index += 1)
            {
                buffer[char_index] = buster_a64_semantic_string_byte(value.text, char_index);
            }
            if (!buster_a64_direct_simd_arrangement_from_string((String8){buffer, value.text.length}, &arrangement))
            {
                return false;
            }
            *result = buster_a64_direct_simd_value_arrangement(arrangement);
            return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
        }
    }
    return false;
}

static bool buster_a64_direct_simd_infer_scalar(BusterA64SemanticString symbol, BusterA64DirectSIMDArrangement* arrangement)
{
    if (!arrangement || symbol.length < 2)
    {
        return false;
    }
    char8 first = buster_a64_semantic_string_byte(symbol, 1);
    if (first == 'H')
    {
        *arrangement = BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_H;
    }
    else if (first == 'S')
    {
        *arrangement = BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S;
    }
    else if (first == 'D' || first == 'd')
    {
        *arrangement = BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D;
    }
    else if (first == 'Q')
    {
        *arrangement = BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q;
    }
    else
    {
        return false;
    }
    return true;
}

static bool buster_a64_direct_simd_symbol_matches(BusterA64SemanticString text, u32 offset, BusterA64SemanticString symbol)
{
    if (offset > text.length || symbol.length > text.length - offset)
    {
        return false;
    }
    for (u32 index = 0; index < symbol.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(text, offset + index) != buster_a64_semantic_string_byte(symbol, index))
        {
            return false;
        }
    }
    return true;
}

/* Recover fixed presentation suffixes/prefixes when a form has no explicit
 * arrangement selector (for example D<n> or <Vn>.2D). */
static bool buster_a64_direct_simd_infer_assembly_arrangement(BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand,
                                                              BusterA64DirectSIMDArrangement* arrangement)
{
    if (!arrangement || operand_index >= form.operand_count)
    {
        return false;
    }
    u32 ordinal = 0;
    for (u32 index = 0; index < operand_index; index += 1)
    {
        BusterA64SemanticOperand previous = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &previous))
        {
            return false;
        }
        if (buster_a64_direct_simd_semantic_equal(previous.symbol, operand.symbol))
        {
            ordinal += 1;
        }
    }
    u32 occurrence = 0;
    for (u32 offset = 0; offset + operand.symbol.length <= form.assembly.length; offset += 1)
    {
        if (!buster_a64_direct_simd_symbol_matches(form.assembly, offset, operand.symbol))
        {
            continue;
        }
        if (occurrence != ordinal)
        {
            occurrence += 1;
            continue;
        }
        if (offset > 0)
        {
            char8 prefix = buster_a64_semantic_string_byte(form.assembly, offset - 1);
            if (prefix == 'B' || prefix == 'H' || prefix == 'S' || prefix == 'D')
            {
                *arrangement = prefix == 'B'   ? BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B
                               : prefix == 'H' ? BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_H
                               : prefix == 'S' ? BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S
                                               : BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D;
                return true;
            }
        }
        u32 suffix_offset = offset + operand.symbol.length;
        if (suffix_offset < form.assembly.length && buster_a64_semantic_string_byte(form.assembly, suffix_offset) == '.')
        {
            u32 end = suffix_offset + 1;
            while (end < form.assembly.length)
            {
                char8 byte = buster_a64_semantic_string_byte(form.assembly, end);
                if (byte == ',' || byte == ' ' || byte == '}' || byte == '\t')
                {
                    break;
                }
                end += 1;
            }
            if (end > suffix_offset + 1)
            {
                String8 suffix = {0};
                /* The semantic string pool is immutable; point into it only
                 * after translating through a bounded local buffer. */
                char8 buffer[8] = {0};
                u32 length = end - suffix_offset - 1;
                if (length < BUSTER_ARRAY_LENGTH(buffer))
                {
                    for (u32 index = 0; index < length; index += 1)
                    {
                        buffer[index] = buster_a64_semantic_string_byte(form.assembly, suffix_offset + 1 + index);
                    }
                    suffix = (String8){buffer, length};
                    if (buster_a64_direct_simd_arrangement_from_string(suffix, arrangement))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }
    return false;
}

static bool buster_a64_direct_simd_decode_register(u32 row_index, BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand,
                                                   BusterA64SemanticVMFields const* fields, BusterA64SemanticVMValue const* values,
                                                   BusterA64SemanticVMValue* result)
{
    if (!fields || !values || !result || operand.field_index_count == 0)
    {
        return false;
    }
    u32 field_id = 0, local = 0, offset = 0;
    if (!buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) || !buster_a64_direct_simd_field_local(form, field_id, &local) ||
        !buster_a64_direct_simd_member_offset(operand.symbol, &offset))
    {
        return false;
    }
    bool is_list = operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST || (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER) != 0;
    u32 number = is_list ? fields->values[local] : (fields->values[local] + offset) & 31u;
    BusterA64DirectSIMDArrangement arrangement = BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID;
    bool arrangement_selected = false;
    BusterA64DirectSIMDGeneratedArrangementBinding const* binding = buster_a64_direct_simd_generated_binding(row_index, operand_index);
    if (binding && binding->selector_index != BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_BINDING_NONE)
    {
        if (binding->selector_index >= form.operand_count || values[binding->selector_index].kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT)
        {
            return false;
        }
        arrangement = (BusterA64DirectSIMDArrangement)values[binding->selector_index].aux;
        if (arrangement == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID || arrangement >= BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_COUNT)
        {
            return false;
        }
        arrangement_selected = true;
    }
    if (!arrangement_selected)
    {
        arrangement_selected = buster_a64_direct_simd_infer_assembly_arrangement(form, operand_index, operand, &arrangement);
    }
    if (!arrangement_selected && !buster_a64_direct_simd_infer_scalar(operand.symbol, &arrangement))
    {
        arrangement = BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q;
    }
    bool scalar = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) != 0;
    if (!scalar && arrangement_selected && buster_a64_direct_simd_arrangement_is_scalar(arrangement))
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
            if (!buster_a64_semantic_operand(form.operand_first + index, &candidate) ||
                !buster_a64_direct_simd_member_offset(candidate.symbol, &candidate_offset))
            {
                return false;
            }
            if (candidate.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST && candidate_offset + 1 > count)
            {
                count = candidate_offset + 1;
            }
        }
        *result = buster_a64_direct_simd_value_list(number, count, arrangement);
    }
    else if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER) != 0 &&
             (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) != 0 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) != 0)
    {
        /* TBL/TBX Vm is an index vector; the presentation metadata calls it
         * simd_lane but there is no encoded element lane to recover. */
        *result = buster_a64_direct_simd_value_vector(number, arrangement);
    }
    else if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE)
    {
        *result = buster_a64_direct_simd_value_lane(number, arrangement, 0);
    }
    else
    {
        *result = buster_a64_direct_simd_value_register(number, arrangement, scalar);
    }
    BUSTER_UNUSED(local);
    return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

static BusterA64DirectSIMDStatus buster_a64_direct_simd_vm_status(BusterA64SemanticVMStatus status)
{
    switch (status)
    {
    case BUSTER_A64_SEMANTIC_VM_STATUS_OK:
        return BUSTER_A64_DIRECT_SIMD_STATUS_OK;
    case BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT:
        return BUSTER_A64_DIRECT_SIMD_STATUS_INVALID_ARGUMENT;
    case BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS:
        return BUSTER_A64_DIRECT_SIMD_STATUS_BOUNDS;
    case BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED:
        return BUSTER_A64_DIRECT_SIMD_STATUS_UNSUPPORTED;
    case BUSTER_A64_SEMANTIC_VM_STATUS_RESERVED:
        return BUSTER_A64_DIRECT_SIMD_STATUS_RESERVED;
    case BUSTER_A64_SEMANTIC_VM_STATUS_AMBIGUOUS:
        return BUSTER_A64_DIRECT_SIMD_STATUS_AMBIGUOUS;
    case BUSTER_A64_SEMANTIC_VM_STATUS_RANGE:
        return BUSTER_A64_DIRECT_SIMD_STATUS_RANGE;
    case BUSTER_A64_SEMANTIC_VM_STATUS_TARGET_MISMATCH:
        return BUSTER_A64_DIRECT_SIMD_STATUS_TARGET_MISMATCH;
    default:
        return BUSTER_A64_DIRECT_SIMD_STATUS_UNSUPPORTED;
    }
}

static BusterA64DirectSIMDStatus buster_a64_direct_simd_canonical_status(BusterAarch64CanonicalDecodeStatus status)
{
    switch (status)
    {
    case BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS:
        return BUSTER_A64_DIRECT_SIMD_STATUS_OK;
    case BUSTER_AARCH64_CANONICAL_DECODE_UNALLOCATED:
        return BUSTER_A64_DIRECT_SIMD_STATUS_RANGE;
    case BUSTER_AARCH64_CANONICAL_DECODE_UNSUPPORTED_FEATURE:
        return BUSTER_A64_DIRECT_SIMD_STATUS_TARGET_MISMATCH;
    case BUSTER_AARCH64_CANONICAL_DECODE_AMBIGUOUS:
        return BUSTER_A64_DIRECT_SIMD_STATUS_AMBIGUOUS;
    default:
        return BUSTER_A64_DIRECT_SIMD_STATUS_UNSUPPORTED;
    }
}

BusterA64DirectSIMDStatus buster_a64_direct_simd_encode(Target target, BusterA64DirectSIMDInstruction const* instruction, u32* word)
{
    if (!instruction || !word || instruction->operand_count > BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS)
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_INVALID_ARGUMENT;
    }
    BusterA64DirectSIMDGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_direct_simd_row_valid(instruction->row_index, &row))
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_BOUNDS;
    }
    if (!row->executable)
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_UNSUPPORTED;
    }
    if (!buster_a64_semantic_form(row->semantic_form_id, &form) || form.operand_count != instruction->operand_count ||
        form.operand_count > BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS)
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_INVALID_ARGUMENT;
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
            return BUSTER_A64_DIRECT_SIMD_STATUS_RANGE;
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
        return buster_a64_direct_simd_vm_status(vm_status);
    }
    BusterAarch64CanonicalDecodeResult canonical = {0};
    BusterAarch64CanonicalDecodeStatus canonical_status = buster_aarch64_canonical_decode(target, candidate, &canonical);
    if (canonical_status != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS)
    {
        return buster_a64_direct_simd_canonical_status(canonical_status);
    }
    if (canonical.arm_row_digest != row->source_digest)
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_TARGET_MISMATCH;
    }
    *word = candidate;
    return BUSTER_A64_DIRECT_SIMD_STATUS_OK;
}

static bool buster_a64_direct_simd_find_row_for_digest(u64 digest, u32* row_index)
{
    return buster_a64_direct_simd_find_source_digest(digest, row_index);
}

static BusterA64DirectSIMDStatus buster_a64_direct_simd_decode_internal(Target target, u32 requested_row, bool row_requested, u32 word,
                                                                        BusterA64DirectSIMDResult* result)
{
    if (!result)
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_INVALID_ARGUMENT;
    }
    BusterAarch64CanonicalDecodeResult canonical = {0};
    BusterAarch64CanonicalDecodeStatus canonical_status = buster_aarch64_canonical_decode(target, word, &canonical);
    if (canonical_status != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS)
    {
        return buster_a64_direct_simd_canonical_status(canonical_status);
    }
    u32 row_index = 0;
    if (!buster_a64_direct_simd_find_row_for_digest(canonical.arm_row_digest, &row_index) || (row_requested && row_index != requested_row))
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_TARGET_MISMATCH;
    }
    BusterA64DirectSIMDGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_direct_simd_row_valid(row_index, &row) || !row->executable || !buster_a64_semantic_form(row->semantic_form_id, &form))
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_UNSUPPORTED;
    }
    BusterA64SemanticVMResult decoded = {0};
    BusterA64SemanticVMStatus vm_status = buster_a64_semantic_vm_decode_fields(form.id, word, &decoded);
    if (vm_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK)
    {
        return buster_a64_direct_simd_vm_status(vm_status);
    }
    BusterA64SemanticVMValue values[BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS] = {0};
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand))
        {
            return BUSTER_A64_DIRECT_SIMD_STATUS_BOUNDS;
        }
        if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT || operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR ||
            operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR)
        {
            if (!buster_a64_direct_simd_decode_arrangement(form, operand, &decoded.fields, &values[index]))
            {
                return BUSTER_A64_DIRECT_SIMD_STATUS_RESERVED;
            }
        }
    }
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand))
        {
            return BUSTER_A64_DIRECT_SIMD_STATUS_BOUNDS;
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
                return BUSTER_A64_DIRECT_SIMD_STATUS_RANGE;
            }
            break;
        case BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER:
        {
            u32 field_id = 0, local = 0;
            if (operand.field_index_count == 0 || !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) ||
                !buster_a64_direct_simd_field_local(form, field_id, &local))
            {
                return BUSTER_A64_DIRECT_SIMD_STATUS_BOUNDS;
            }
            values[index] =
                buster_a64_direct_simd_value_gpr(decoded.fields.values[local], (operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_X64) != 0 ? 64 : 32,
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
                return BUSTER_A64_DIRECT_SIMD_STATUS_UNSUPPORTED;
            }
        }
        break;
        case BUSTER_A64_SEMANTIC_OPERAND_CONDITION:
        {
            u32 field_id = 0, local = 0;
            if (operand.field_index_count == 0 || !buster_a64_semantic_operand_field_index(operand.id, 0, &field_id) ||
                !buster_a64_direct_simd_field_local(form, field_id, &local))
            {
                return BUSTER_A64_DIRECT_SIMD_STATUS_BOUNDS;
            }
            values[index] = buster_a64_semantic_vm_value_condition(decoded.fields.values[local]);
        }
        break;
        case BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT:
            values[index] = buster_a64_semantic_vm_value_unsigned(2, 2);
            break;
        default:
            return BUSTER_A64_DIRECT_SIMD_STATUS_UNSUPPORTED;
        }
        if (values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID)
        {
            return BUSTER_A64_DIRECT_SIMD_STATUS_RANGE;
        }
    }
    u32 reencoded = 0;
    vm_status = buster_a64_semantic_vm_encode_fields(form.id, &decoded.fields, &reencoded);
    if (vm_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK || reencoded != word)
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_TARGET_MISMATCH;
    }
    BusterA64DirectSIMDResult candidate = {
        .status = BUSTER_A64_DIRECT_SIMD_STATUS_OK, .row_index = row_index, .word = word, .operand_count = form.operand_count};
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        candidate.operands[index] = values[index];
    }
    *result = candidate;
    return BUSTER_A64_DIRECT_SIMD_STATUS_OK;
}

BusterA64DirectSIMDStatus buster_a64_direct_simd_decode(Target target, u32 word, BusterA64DirectSIMDResult* result)
{
    return buster_a64_direct_simd_decode_internal(target, 0, false, word, result);
}

BusterA64DirectSIMDStatus buster_a64_direct_simd_decode_row(Target target, u32 row_index, u32 word, BusterA64DirectSIMDResult* result)
{
    if (row_index >= BUSTER_A64_DIRECT_SIMD_ROW_COUNT)
    {
        return BUSTER_A64_DIRECT_SIMD_STATUS_BOUNDS;
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
        BusterA64DirectSIMDGeneratedRow const* row = 0;
        BusterA64SemanticForm form = {0};
        if (!buster_a64_direct_simd_row_valid(index, &row) || !buster_a64_semantic_form(row->semantic_form_id, &form) ||
            form.owner != BUSTER_A64_SEMANTIC_OWNER_DIRECT_SIMD || form.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL ||
            form.status != BUSTER_A64_SEMANTIC_STATUS_DEFINED || (index != 0 && row->semantic_form_id <= previous_form) ||
            row->operand_count != form.operand_count)
        {
            return false;
        }
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterA64DirectSIMDGeneratedArrangementBinding const* binding = buster_a64_direct_simd_generated_binding(index, operand_index);
            if (!binding || binding->selector_index == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_BINDING_NONE)
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
