#include <buster/lib/compiler/assembly/aarch64_memory_semantics.h>
#include <buster/lib/compiler/assembly/generated/aarch64-memory-semantics.generated.h>

/* This module deliberately keeps all state in bounded automatic objects.  In
 * particular, no caller-provided text is retained in a VM value: symbolic
 * values are represented by the small enums below and are compared against
 * the generated semantic string pool during encode/decode. */

static char8 buster_a64_memory_arrangement_b[] = "B";
static char8 buster_a64_memory_arrangement_h[] = "H";
static char8 buster_a64_memory_arrangement_s[] = "S";
static char8 buster_a64_memory_arrangement_d[] = "D";
static char8 buster_a64_memory_arrangement_q[] = "Q";
static char8 buster_a64_memory_arrangement_1b[] = "1B";
static char8 buster_a64_memory_arrangement_2b[] = "2B";
static char8 buster_a64_memory_arrangement_4b[] = "4B";
static char8 buster_a64_memory_arrangement_8b[] = "8B";
static char8 buster_a64_memory_arrangement_16b[] = "16B";
static char8 buster_a64_memory_arrangement_1h[] = "1H";
static char8 buster_a64_memory_arrangement_2h[] = "2H";
static char8 buster_a64_memory_arrangement_4h[] = "4H";
static char8 buster_a64_memory_arrangement_8h[] = "8H";
static char8 buster_a64_memory_arrangement_1s[] = "1S";
static char8 buster_a64_memory_arrangement_2s[] = "2S";
static char8 buster_a64_memory_arrangement_4s[] = "4S";
static char8 buster_a64_memory_arrangement_1d[] = "1D";
static char8 buster_a64_memory_arrangement_2d[] = "2D";

static char8 buster_a64_memory_extend_uxtb[] = "UXTB";
static char8 buster_a64_memory_extend_uxth[] = "UXTH";
static char8 buster_a64_memory_extend_uxtw[] = "UXTW";
static char8 buster_a64_memory_extend_uxtx[] = "UXTX";
static char8 buster_a64_memory_extend_sxtb[] = "SXTB";
static char8 buster_a64_memory_extend_sxth[] = "SXTH";
static char8 buster_a64_memory_extend_sxtw[] = "SXTW";
static char8 buster_a64_memory_extend_sxtx[] = "SXTX";
static char8 buster_a64_memory_extend_lsl[] = "LSL";

static char8* buster_a64_memory_prefetch_names[] = {
    "PLDL1KEEP", "PLDL1STRM", "PLDL2KEEP", "PLDL2STRM", "PLDL3KEEP", "PLDL3STRM", "PLDSLCKEEP", "PLDSLCSTRM",
    "PLIL1KEEP", "PLIL1STRM", "PLIL2KEEP", "PLIL2STRM", "PLIL3KEEP", "PLIL3STRM", "PLISLCKEEP", "PLISLCSTRM",
    "PSTL1KEEP", "PSTL1STRM", "PSTL2KEEP", "PSTL2STRM", "PSTL3KEEP", "PSTL3STRM", "PSTSLCKEEP", "PSTSLCSTRM", "IR",
};

static String8
buster_a64_memory_arrangement_text(BusterA64MemoryArrangement arrangement)
{
    switch (arrangement)
    {
        case BUSTER_A64_MEMORY_ARRANGEMENT_B: return (String8){buster_a64_memory_arrangement_b, 1};
        case BUSTER_A64_MEMORY_ARRANGEMENT_H: return (String8){buster_a64_memory_arrangement_h, 1};
        case BUSTER_A64_MEMORY_ARRANGEMENT_S: return (String8){buster_a64_memory_arrangement_s, 1};
        case BUSTER_A64_MEMORY_ARRANGEMENT_D: return (String8){buster_a64_memory_arrangement_d, 1};
        case BUSTER_A64_MEMORY_ARRANGEMENT_Q: return (String8){buster_a64_memory_arrangement_q, 1};
        case BUSTER_A64_MEMORY_ARRANGEMENT_1B: return (String8){buster_a64_memory_arrangement_1b, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_2B: return (String8){buster_a64_memory_arrangement_2b, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_4B: return (String8){buster_a64_memory_arrangement_4b, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_8B: return (String8){buster_a64_memory_arrangement_8b, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_16B: return (String8){buster_a64_memory_arrangement_16b, 3};
        case BUSTER_A64_MEMORY_ARRANGEMENT_1H: return (String8){buster_a64_memory_arrangement_1h, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_2H: return (String8){buster_a64_memory_arrangement_2h, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_4H: return (String8){buster_a64_memory_arrangement_4h, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_8H: return (String8){buster_a64_memory_arrangement_8h, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_1S: return (String8){buster_a64_memory_arrangement_1s, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_2S: return (String8){buster_a64_memory_arrangement_2s, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_4S: return (String8){buster_a64_memory_arrangement_4s, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_1D: return (String8){buster_a64_memory_arrangement_1d, 2};
        case BUSTER_A64_MEMORY_ARRANGEMENT_2D: return (String8){buster_a64_memory_arrangement_2d, 2};
        default: return (String8){0};
    }
}

static String8
buster_a64_memory_extend_text(BusterA64MemoryExtend extend)
{
    switch (extend)
    {
        case BUSTER_A64_MEMORY_EXTEND_UXTB: return (String8){buster_a64_memory_extend_uxtb, 4};
        case BUSTER_A64_MEMORY_EXTEND_UXTH: return (String8){buster_a64_memory_extend_uxth, 4};
        case BUSTER_A64_MEMORY_EXTEND_UXTW: return (String8){buster_a64_memory_extend_uxtw, 4};
        case BUSTER_A64_MEMORY_EXTEND_UXTX: return (String8){buster_a64_memory_extend_uxtx, 4};
        case BUSTER_A64_MEMORY_EXTEND_SXTB: return (String8){buster_a64_memory_extend_sxtb, 4};
        case BUSTER_A64_MEMORY_EXTEND_SXTH: return (String8){buster_a64_memory_extend_sxth, 4};
        case BUSTER_A64_MEMORY_EXTEND_SXTW: return (String8){buster_a64_memory_extend_sxtw, 4};
        case BUSTER_A64_MEMORY_EXTEND_SXTX: return (String8){buster_a64_memory_extend_sxtx, 4};
        case BUSTER_A64_MEMORY_EXTEND_LSL: return (String8){buster_a64_memory_extend_lsl, 3};
        default: return (String8){0};
    }
}

static String8
buster_a64_memory_prefetch_text(u32 operation)
{
    if (operation >= BUSTER_ARRAY_LENGTH(buster_a64_memory_prefetch_names)) { return (String8){0};
}
    char8* text = buster_a64_memory_prefetch_names[operation];
    u64 length = 0;
    while (text[length] != 0) { length += 1;
}
    return (String8){text, length};
}

static bool
buster_a64_memory_string_equal(String8 left, String8 right)
{
    if (left.length != right.length) { return false;
}
    for (u64 index = 0; index < left.length; index += 1) {
        if (left.pointer[index] != right.pointer[index]) { return false;
}
}
    return true;
}

static bool
buster_a64_memory_semantic_equal(BusterA64SemanticString left, BusterA64SemanticString right)
{
    if (left.length != right.length) { return false;
}
    for (u32 index = 0; index < left.length; index += 1) {
        if (buster_a64_semantic_string_byte(left, index) != buster_a64_semantic_string_byte(right, index)) { return false;
}
}
    return true;
}

static bool
buster_a64_memory_semantic_equal_cstr(BusterA64SemanticString left, char8 const* right)
{
    if (!right) { return false;
}
    u32 length = 0;
    while (right[length] != 0) { length += 1;
}
    if (left.length != length) { return false;
}
    for (u32 index = 0; index < length; index += 1) {
        if (buster_a64_semantic_string_byte(left, index) != right[index]) { return false;
}
}
    return true;
}

static bool
buster_a64_memory_semantic_equals_string(BusterA64SemanticString left, String8 right)
{
    if (left.length != right.length || (left.length != 0 && !right.pointer)) { return false;
}
    for (u32 index = 0; index < left.length; index += 1) {
        if (buster_a64_semantic_string_byte(left, index) != right.pointer[index]) { return false;
}
}
    return true;
}

static bool
buster_a64_memory_row_valid(u32 row_index, BusterA64MemoryGeneratedRow const** result)
{
    if (!result || row_index >= BUSTER_A64_MEMORY_ROW_COUNT || row_index >= BUSTER_ARRAY_LENGTH(buster_a64_memory_generated_rows)) { return false;
}
    BusterA64MemoryGeneratedRow const* row = buster_a64_memory_generated_rows + row_index;
    if (row->row_index != row_index || row->semantic_form_id >= buster_a64_semantic_form_count() ||
        row->operand_count > BUSTER_A64_MEMORY_MAX_OPERANDS || row->family >= BUSTER_A64_MEMORY_FAMILY_COUNT ||
        row->address_mode >= BUSTER_A64_MEMORY_ADDRESS_COUNT || row->overlap_policy >= BUSTER_A64_MEMORY_OVERLAP_COUNT) { return false;
}
    *result = row;
    return true;
}

u32 buster_a64_memory_schema_version(void) { return BUSTER_A64_MEMORY_SCHEMA_VERSION; }
u32 buster_a64_memory_row_count(void) { return BUSTER_A64_MEMORY_ROW_COUNT; }
u32 buster_a64_memory_transform_row_count(void) { return BUSTER_A64_MEMORY_TRANSFORM_ROW_COUNT; }
u32 buster_a64_memory_feature_gated_row_count(void) { return BUSTER_A64_MEMORY_FEATURE_GATED_ROW_COUNT; }
u32 buster_a64_memory_max_operands(void) { return BUSTER_A64_MEMORY_MAX_OPERANDS; }
u32 buster_a64_memory_arrangement_binding_count(void) { return BUSTER_A64_MEMORY_ARRANGEMENT_BINDING_COUNT; }

bool
buster_a64_memory_row(u32 row_index, BusterA64MemoryRowInfo* result)
{
    if (!result) { return false;
}
    BusterA64MemoryGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_memory_row_valid(row_index, &row) || !buster_a64_semantic_form(row->semantic_form_id, &form)) { return false;
}
    *result = (BusterA64MemoryRowInfo){.row_index = row_index,
                                       .semantic_form_id = row->semantic_form_id,
                                       .source_digest = row->source_digest,
                                       .name = form.name,
                                       .assembly = form.assembly,
                                       .operand_count = row->operand_count,
                                       .family = row->family,
                                       .address_mode = row->address_mode,
                                       .overlap_policy = row->overlap_policy,
                                       .candidate = row->candidate,
                                       .transform_bearing = (u8)(form.transform_count != 0)};
    return true;
}

bool
buster_a64_memory_find_source_digest(u64 source_digest, u32* row_index)
{
    if (!row_index) { return false;
}
    u32 count = 0;
    u32 selected = 0;
    for (u32 index = 0; index < BUSTER_A64_MEMORY_ROW_COUNT; index += 1)
    {
        if (buster_a64_memory_generated_rows[index].source_digest == source_digest)
        {
            count += 1;
            selected = index;
        }
    }
    if (count != 1) { return false;
}
    *row_index = selected;
    return true;
}

bool
buster_a64_memory_arrangement_binding(u32 row_index, u32 operand_index, BusterA64MemoryArrangementBinding* result)
{
    if (!result || row_index >= BUSTER_A64_MEMORY_ROW_COUNT || operand_index >= BUSTER_A64_MEMORY_GENERATED_MAX_OPERANDS) { return false;
}
    BusterA64MemoryGeneratedRow const* row = 0;
    if (!buster_a64_memory_row_valid(row_index, &row) || operand_index >= row->operand_count) { return false;
}
    BusterA64MemoryGeneratedArrangementBinding binding = buster_a64_memory_generated_arrangement_bindings[row_index][operand_index];
    if (binding.selector_index == BUSTER_A64_MEMORY_ARRANGEMENT_BINDING_NONE || binding.selector_index >= row->operand_count ||
        (binding.direction != 1 && binding.direction != -1)) { return false;
}
    *result = (BusterA64MemoryArrangementBinding){.selector_index = binding.selector_index, .direction = binding.direction};
    return true;
}

bool
buster_a64_memory_arrangement_from_string(String8 text, BusterA64MemoryArrangement* result)
{
    if (!result) { return false;
}
    for (u32 index = 1; index < BUSTER_A64_MEMORY_ARRANGEMENT_COUNT; index += 1)
    {
        if (buster_a64_memory_string_equal(text, buster_a64_memory_arrangement_text((BusterA64MemoryArrangement)index)))
        {
            *result = (BusterA64MemoryArrangement)index;
            return true;
        }
    }
    return false;
}

String8
buster_a64_memory_arrangement_string(BusterA64MemoryArrangement arrangement)
{
    return buster_a64_memory_arrangement_text(arrangement);
}

static u8
buster_a64_memory_scalar_width(BusterA64MemoryArrangement arrangement)
{
    switch (arrangement)
    {
        case BUSTER_A64_MEMORY_ARRANGEMENT_B: case BUSTER_A64_MEMORY_ARRANGEMENT_1B: case BUSTER_A64_MEMORY_ARRANGEMENT_2B:
        case BUSTER_A64_MEMORY_ARRANGEMENT_4B: case BUSTER_A64_MEMORY_ARRANGEMENT_8B: case BUSTER_A64_MEMORY_ARRANGEMENT_16B: return 8;
        case BUSTER_A64_MEMORY_ARRANGEMENT_H: case BUSTER_A64_MEMORY_ARRANGEMENT_1H: case BUSTER_A64_MEMORY_ARRANGEMENT_2H:
        case BUSTER_A64_MEMORY_ARRANGEMENT_4H: case BUSTER_A64_MEMORY_ARRANGEMENT_8H: return 16;
        case BUSTER_A64_MEMORY_ARRANGEMENT_S: case BUSTER_A64_MEMORY_ARRANGEMENT_1S: case BUSTER_A64_MEMORY_ARRANGEMENT_2S:
        case BUSTER_A64_MEMORY_ARRANGEMENT_4S: return 32;
        case BUSTER_A64_MEMORY_ARRANGEMENT_D: case BUSTER_A64_MEMORY_ARRANGEMENT_1D: case BUSTER_A64_MEMORY_ARRANGEMENT_2D: return 64;
        case BUSTER_A64_MEMORY_ARRANGEMENT_Q: return 128;
        default: return 0;
    }
}

static u8
buster_a64_memory_lane_count(BusterA64MemoryArrangement arrangement)
{
    switch (arrangement)
    {
        case BUSTER_A64_MEMORY_ARRANGEMENT_B: case BUSTER_A64_MEMORY_ARRANGEMENT_H: case BUSTER_A64_MEMORY_ARRANGEMENT_S:
        case BUSTER_A64_MEMORY_ARRANGEMENT_D: case BUSTER_A64_MEMORY_ARRANGEMENT_1B: case BUSTER_A64_MEMORY_ARRANGEMENT_1H:
        case BUSTER_A64_MEMORY_ARRANGEMENT_1S: case BUSTER_A64_MEMORY_ARRANGEMENT_1D: case BUSTER_A64_MEMORY_ARRANGEMENT_Q: return 1;
        case BUSTER_A64_MEMORY_ARRANGEMENT_2B: case BUSTER_A64_MEMORY_ARRANGEMENT_2H: case BUSTER_A64_MEMORY_ARRANGEMENT_2S:
        case BUSTER_A64_MEMORY_ARRANGEMENT_2D: return 2;
        case BUSTER_A64_MEMORY_ARRANGEMENT_4B: case BUSTER_A64_MEMORY_ARRANGEMENT_4H: case BUSTER_A64_MEMORY_ARRANGEMENT_4S: return 4;
        case BUSTER_A64_MEMORY_ARRANGEMENT_8B: case BUSTER_A64_MEMORY_ARRANGEMENT_8H: return 8;
        case BUSTER_A64_MEMORY_ARRANGEMENT_16B: return 16;
        default: return 0;
    }
}

static u8
buster_a64_memory_register_width(BusterA64MemoryArrangement arrangement)
{
    u8 scalar_width = buster_a64_memory_scalar_width(arrangement);
    u8 lanes = buster_a64_memory_lane_count(arrangement);
    if (arrangement == BUSTER_A64_MEMORY_ARRANGEMENT_Q) { return 128;
}
    if (!scalar_width || !lanes || scalar_width > 128 / lanes) { return 0;
}
    return (u8)(scalar_width * lanes);
}

static bool buster_a64_memory_assign_field(BusterA64SemanticForm form, u32 local, u32 value, u32* fields, u64* assigned);

static bool
buster_a64_memory_lane_symbol_at(BusterA64SemanticString text, u32 offset, BusterA64SemanticString symbol)
{
    if (offset > text.length || symbol.length > text.length - offset) { return false;
}
    for (u32 index = 0; index < symbol.length; index += 1) {
        if (buster_a64_semantic_string_byte(text, offset + index) != buster_a64_semantic_string_byte(symbol, index)) { return false;
}
}
    return true;
}

static u8
buster_a64_memory_lane_element_width(BusterA64SemanticForm form, BusterA64SemanticOperand operand)
{
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_B8) { return 8;
}
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_H16) { return 16;
}
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_S32) { return 32;
}
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_D64) { return 64;
}
    /* Lane-index metadata often carries the XML's `simd_widths` in the
     * presentation rather than the stable operand flags.  Recover that
     * fixed suffix from the exact syntax occurrence (`<Vt>.B[<index>]`). */
    u32 occurrence = 0;
    for (u32 prior = 0; prior < form.operand_count; prior += 1)
    {
        BusterA64SemanticOperand candidate = {0};
        if (!buster_a64_semantic_operand(form.operand_first + prior, &candidate)) { return 0;
}
        if (prior < form.operand_count && candidate.symbol.length == operand.symbol.length &&
            buster_a64_memory_semantic_equal(candidate.symbol, operand.symbol))
        {
            if (prior == operand.position) { break;
}
            occurrence += 1;
        }
    }
    u32 seen = 0;
    for (u32 offset = 0; offset + operand.symbol.length <= form.assembly.length; offset += 1)
    {
        if (!buster_a64_memory_lane_symbol_at(form.assembly, offset, operand.symbol)) { continue;
}
        if (seen++ != occurrence) { continue;
}
        u32 lower = offset > 16 ? offset - 16 : 0;
        for (u32 back = offset; back > lower; back -= 1)
        {
            char8 byte = buster_a64_semantic_string_byte(form.assembly, back - 1);
            if (byte == 'B' || byte == 'b') { return 8;
}
            if (byte == 'H' || byte == 'h') { return 16;
}
            if (byte == 'S' || byte == 's') { return 32;
}
            if (byte == 'D' || byte == 'd') { return 64;
}
            if (byte == ',') { break;
}
        }
    }
    return 0;
}

static bool
buster_a64_memory_lane_arrangement_from_operand(BusterA64SemanticForm form, BusterA64SemanticOperand operand, bool q,
                                                 BusterA64MemoryArrangement* arrangement)
{
    if (!arrangement) { return false;
}
    u8 element_width = buster_a64_memory_lane_element_width(form, operand);
    if (element_width == 8) {
        *arrangement = q ? BUSTER_A64_MEMORY_ARRANGEMENT_16B : BUSTER_A64_MEMORY_ARRANGEMENT_8B;
    } else if (element_width == 16) {
        *arrangement = q ? BUSTER_A64_MEMORY_ARRANGEMENT_8H : BUSTER_A64_MEMORY_ARRANGEMENT_4H;
    } else if (element_width == 32) {
        *arrangement = q ? BUSTER_A64_MEMORY_ARRANGEMENT_4S : BUSTER_A64_MEMORY_ARRANGEMENT_2S;
    } else if (element_width == 64) {
        *arrangement = q ? BUSTER_A64_MEMORY_ARRANGEMENT_2D : BUSTER_A64_MEMORY_ARRANGEMENT_1D;
    } else { return false;
}
    return buster_a64_memory_register_width(*arrangement) == (q ? 128 : 64);
}

static bool
buster_a64_memory_lane_q_for_arrangement(BusterA64MemoryArrangement arrangement, bool* q)
{
    if (!q) { return false;
}
    switch (arrangement)
    {
        case BUSTER_A64_MEMORY_ARRANGEMENT_8B:
        case BUSTER_A64_MEMORY_ARRANGEMENT_4H:
        case BUSTER_A64_MEMORY_ARRANGEMENT_2S:
        case BUSTER_A64_MEMORY_ARRANGEMENT_1D:
            *q = false;
            return true;
        case BUSTER_A64_MEMORY_ARRANGEMENT_16B:
        case BUSTER_A64_MEMORY_ARRANGEMENT_8H:
        case BUSTER_A64_MEMORY_ARRANGEMENT_4S:
        case BUSTER_A64_MEMORY_ARRANGEMENT_2D:
            *q = true;
            return true;
        default: return false;
    }
}

static bool
buster_a64_memory_lane_field_q(BusterA64SemanticForm form, u32 const* fields, bool* q)
{
    if (!fields || !q) { return false;
}
    for (u32 index = 0; index < form.field_count; index += 1)
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(form.field_first + index, &field)) { return false;
}
        if (buster_a64_memory_semantic_equal_cstr(field.name, "Q"))
        {
            if (field.width != 1 || fields[index] > 1) { return false;
}
            *q = fields[index] != 0;
            return true;
        }
    }
    return false;
}

static bool
buster_a64_memory_lane_assign_q(BusterA64SemanticForm form, bool q, u32* fields, u64* assigned)
{
    if (!fields || !assigned) { return false;
}
    for (u32 index = 0; index < form.field_count; index += 1)
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(form.field_first + index, &field)) { return false;
}
        if (buster_a64_memory_semantic_equal_cstr(field.name, "Q")) {
            return field.width == 1 && buster_a64_memory_assign_field(form, index, q ? 1u : 0u, fields, assigned);
}
    }
    return false;
}

static bool
buster_a64_memory_lane_validate_immediate(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                           u32 const* fields, u64 lane)
{
    bool q = false;
    if (!buster_a64_memory_lane_field_q(form, fields, &q)) { return false;
}
    BusterA64MemoryArrangement arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_INVALID;
    if (!buster_a64_memory_lane_arrangement_from_operand(form, operand, q, &arrangement)) { return false;
}
    return lane < buster_a64_memory_lane_count(arrangement);
}

BusterA64SemanticVMValue
buster_a64_memory_value_arrangement(BusterA64MemoryArrangement arrangement)
{
    if (arrangement <= BUSTER_A64_MEMORY_ARRANGEMENT_INVALID || arrangement >= BUSTER_A64_MEMORY_ARRANGEMENT_COUNT) {
        return buster_a64_semantic_vm_value_invalid();
}
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT,
                                      .width = 8, .aux = (u32)arrangement, .payload = (u64)arrangement};
}

BusterA64SemanticVMValue
buster_a64_memory_value_register(u32 number, BusterA64MemoryArrangement arrangement, bool scalar)
{
    u8 width = scalar ? buster_a64_memory_scalar_width(arrangement) : buster_a64_memory_register_width(arrangement);
    if (number > 31 || !width || arrangement <= BUSTER_A64_MEMORY_ARRANGEMENT_INVALID || arrangement >= BUSTER_A64_MEMORY_ARRANGEMENT_COUNT) {
        return buster_a64_semantic_vm_value_invalid();
}
    return (BusterA64SemanticVMValue){.kind = scalar ? BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR : BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR,
                                      .width = width, .aux = (u32)arrangement, .payload = number};
}

BusterA64SemanticVMValue buster_a64_memory_value_vector(u32 number, BusterA64MemoryArrangement arrangement)
{
    return buster_a64_memory_value_register(number, arrangement, false);
}

BusterA64SemanticVMValue buster_a64_memory_value_scalar(u32 number, BusterA64MemoryArrangement arrangement)
{
    return buster_a64_memory_value_register(number, arrangement, true);
}

BusterA64SemanticVMValue
buster_a64_memory_value_list(u32 first, u32 count, BusterA64MemoryArrangement arrangement)
{
    u8 width = buster_a64_memory_register_width(arrangement);
    if (first > 31 || count == 0 || count > 4 || first + count > 32 || !width ||
        arrangement <= BUSTER_A64_MEMORY_ARRANGEMENT_INVALID || arrangement >= BUSTER_A64_MEMORY_ARRANGEMENT_COUNT) {
        return buster_a64_semantic_vm_value_invalid();
}
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST,
                                      .width = width, .aux = (u32)arrangement, .aux2 = count, .payload = first};
}

BusterA64SemanticVMValue
buster_a64_memory_value_lane(u32 number, BusterA64MemoryArrangement arrangement, u32 lane)
{
    u8 width = buster_a64_memory_register_width(arrangement);
    u32 count = buster_a64_memory_lane_count(arrangement);
    if (number > 31 || !width || !count || lane >= count) { return buster_a64_semantic_vm_value_invalid();
}
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE,
                                      .width = width, .aux = (u32)arrangement, .aux2 = lane, .payload = number};
}

BusterA64SemanticVMValue
buster_a64_memory_value_gpr(u32 number, u8 width, bool sp, bool zr)
{
    return buster_a64_semantic_vm_value_gpr(number, width, sp, zr);
}

BusterA64SemanticVMValue
buster_a64_memory_value_immediate(s64 value, u8 width, bool is_signed)
{
    BusterA64SemanticVMValue result = is_signed ? buster_a64_semantic_vm_value_signed(value, width) :
                                                  (value < 0 ? buster_a64_semantic_vm_value_invalid() : buster_a64_semantic_vm_value_unsigned((u64)value, width));
    if (result.kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) { result.kind = BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE;
}
    return result;
}

BusterA64SemanticVMValue
buster_a64_memory_value_extend(BusterA64MemoryExtend extend)
{
    if (extend <= BUSTER_A64_MEMORY_EXTEND_INVALID || extend >= BUSTER_A64_MEMORY_EXTEND_COUNT) {
        return buster_a64_semantic_vm_value_invalid();
}
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_EXTEND,
                                      .width = 4, .aux = (u32)extend, .payload = (u64)extend};
}

BusterA64SemanticVMValue
buster_a64_memory_value_prefetch(u32 operation)
{
    if (operation > 31) { return buster_a64_semantic_vm_value_invalid();
}
    return (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_PREFETCH_OPERATION,
                                      .width = 5, .payload = operation};
}

static bool
buster_a64_memory_value_uint(BusterA64SemanticVMValue value, u64* result)
{
    if (!result) { return false;
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
        case BUSTER_A64_SEMANTIC_VM_VALUE_SHIFT:
        case BUSTER_A64_SEMANTIC_VM_VALUE_EXTEND:
        case BUSTER_A64_SEMANTIC_VM_VALUE_PREFETCH_OPERATION:
        case BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION:
            *result = value.payload;
            return true;
        default: return false;
    }
}

static bool
buster_a64_memory_value_sint(BusterA64SemanticVMValue value, s64* result)
{
    if (!result) { return false;
}
    if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE)
    {
        *result = (s64)value.payload;
        return true;
    }
    u64 unsigned_value = 0;
    if (buster_a64_memory_value_uint(value, &unsigned_value) && unsigned_value <= (u64)INT64_MAX)
    {
        *result = (s64)unsigned_value;
        return true;
    }
    return false;
}

static BusterA64MemoryStatus
buster_a64_memory_vm_status(BusterA64SemanticVMStatus status)
{
    switch (status)
    {
        case BUSTER_A64_SEMANTIC_VM_STATUS_OK: return BUSTER_A64_MEMORY_STATUS_OK;
        case BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT: return BUSTER_A64_MEMORY_STATUS_INVALID_ARGUMENT;
        case BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS: return BUSTER_A64_MEMORY_STATUS_BOUNDS;
        case BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED: return BUSTER_A64_MEMORY_STATUS_UNSUPPORTED;
        case BUSTER_A64_SEMANTIC_VM_STATUS_RESERVED: return BUSTER_A64_MEMORY_STATUS_RESERVED;
        case BUSTER_A64_SEMANTIC_VM_STATUS_AMBIGUOUS: return BUSTER_A64_MEMORY_STATUS_AMBIGUOUS;
        case BUSTER_A64_SEMANTIC_VM_STATUS_RANGE: return BUSTER_A64_MEMORY_STATUS_RANGE;
        case BUSTER_A64_SEMANTIC_VM_STATUS_TARGET_MISMATCH: return BUSTER_A64_MEMORY_STATUS_TARGET_MISMATCH;
        default: return BUSTER_A64_MEMORY_STATUS_UNSUPPORTED;
    }
}

static BusterA64MemoryStatus
buster_a64_memory_canonical_status(BusterAarch64CanonicalDecodeStatus status)
{
    switch (status)
    {
        case BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS: return BUSTER_A64_MEMORY_STATUS_OK;
        case BUSTER_AARCH64_CANONICAL_DECODE_UNALLOCATED: return BUSTER_A64_MEMORY_STATUS_RANGE;
        case BUSTER_AARCH64_CANONICAL_DECODE_UNSUPPORTED_FEATURE: return BUSTER_A64_MEMORY_STATUS_TARGET_MISMATCH;
        case BUSTER_AARCH64_CANONICAL_DECODE_AMBIGUOUS: return BUSTER_A64_MEMORY_STATUS_AMBIGUOUS;
        default: return BUSTER_A64_MEMORY_STATUS_UNSUPPORTED;
    }
}

static bool
buster_a64_memory_field_local(BusterA64SemanticForm form, u32 field_id, u32* local)
{
    if (!local || field_id < form.field_first || field_id >= form.field_first + form.field_count) { return false;
}
    *local = field_id - form.field_first;
    return true;
}

static bool
buster_a64_memory_assign_field(BusterA64SemanticForm form, u32 local, u32 value, u32* fields, u64* assigned)
{
    if (!fields || !assigned || local >= form.field_count || local >= 64) { return false;
}
    BusterA64SemanticField field = {0};
    if (!buster_a64_semantic_field(form.field_first + local, &field) || field.width == 0 || field.width > 32) { return false;
}
    u32 mask = field.width == 32 ? UINT32_MAX : ((UINT32_C(1) << field.width) - 1);
    if ((value & ~mask) != 0) { return false;
}
    u64 bit = UINT64_C(1) << local;
    if ((*assigned & bit) != 0) { return fields[local] == value;
}
    fields[local] = value;
    *assigned |= bit;
    return true;
}

static bool
buster_a64_memory_member_offset(BusterA64SemanticString symbol, u32* offset)
{
    if (!offset) { return false;
}
    *offset = 0;
    u32 plus = UINT32_MAX;
    for (u32 index = 0; index < symbol.length; index += 1) {
        if (buster_a64_semantic_string_byte(symbol, index) == '+') { plus = index; break; }
}
    if (plus == UINT32_MAX) { return true;
}
    if (plus + 1 >= symbol.length) { return false;
}
    u32 value = 0;
    for (u32 index = plus + 1; index < symbol.length; index += 1)
    {
        char8 digit = buster_a64_semantic_string_byte(symbol, index);
        if (digit < '0' || digit > '9') { return false;
}
        value = value * 10u + (u32)(digit - '0');
    }
    if (value > 31) { return false;
}
    *offset = value;
    return true;
}

static bool
buster_a64_memory_parse_bits(BusterA64SemanticString text, u32* value, u32* mask, u8* width)
{
    if (!value || !mask || !width || text.length == 0 || text.length > 32) { return false;
}
    u32 parsed_value = 0;
    u32 parsed_mask = 0;
    for (u32 index = 0; index < text.length; index += 1)
    {
        char8 digit = buster_a64_semantic_string_byte(text, index);
        if (digit != '0' && digit != '1' && digit != 'x' && digit != 'X') { return false;
}
        parsed_value <<= 1;
        parsed_mask <<= 1;
        if (digit == '0' || digit == '1') { parsed_mask |= 1;
}
        if (digit == '1') { parsed_value |= 1;
}
    }
    *value = parsed_value;
    *mask = parsed_mask;
    *width = (u8)text.length;
    return true;
}

static bool
buster_a64_memory_find_field(BusterA64SemanticForm form, BusterA64SemanticString name, u32* local)
{
    if (!local) { return false;
}
    u32 matches = 0;
    for (u32 index = 0; index < form.field_count; index += 1)
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(form.field_first + index, &field)) { return false;
}
        if (buster_a64_memory_semantic_equal(field.name, name))
        {
            matches += 1;
            *local = index;
        }
    }
    return matches == 1;
}

static bool
buster_a64_memory_operand_field_local(BusterA64SemanticForm form, BusterA64SemanticOperand operand, u32 ordinal, u32* local)
{
    u32 field_id = 0;
    return buster_a64_semantic_operand_field_index(operand.id, ordinal, &field_id) &&
           buster_a64_memory_field_local(form, field_id, local);
}

static bool
buster_a64_memory_atom_matches(BusterA64SemanticValueAtom atom, BusterA64SemanticVMValue desired)
{
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM)
    {
        return (desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_EXTEND && desired.aux < BUSTER_A64_MEMORY_EXTEND_COUNT &&
                buster_a64_memory_semantic_equals_string(atom.text, buster_a64_memory_extend_text((BusterA64MemoryExtend)desired.aux))) ||
               ((desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER ||
                 desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR ||
                 desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE) &&
                desired.aux < BUSTER_A64_MEMORY_ARRANGEMENT_COUNT &&
                buster_a64_memory_semantic_equals_string(atom.text, buster_a64_memory_arrangement_text((BusterA64MemoryArrangement)desired.aux))) ||
               (desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION && buster_a64_memory_semantic_equal(atom.text, desired.text));
    }
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER)
    {
        u64 value = 0;
        return atom.integer >= 0 && buster_a64_memory_value_uint(desired, &value) && value == (u64)atom.integer;
    }
    if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS)
    {
        u32 expected = 0, mask = 0;
        u8 width = 0;
        u64 actual = 0;
        return buster_a64_memory_parse_bits(atom.text, &expected, &mask, &width) &&
               buster_a64_memory_value_uint(desired, &actual) && ((u32)actual & mask) == (expected & mask);
    }
    return false;
}

static bool
buster_a64_memory_value_matches(BusterA64SemanticVMValue actual, BusterA64SemanticVMValue desired)
{
    if (actual.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION)
    {
        if (desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION) {
            return buster_a64_memory_semantic_equal(actual.text, desired.text);
}
        return ((desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_EXTEND && desired.aux < BUSTER_A64_MEMORY_EXTEND_COUNT) &&
                buster_a64_memory_semantic_equals_string(actual.text, buster_a64_memory_extend_text((BusterA64MemoryExtend)desired.aux))) ||
               ((desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_PREFETCH_OPERATION && desired.payload < BUSTER_ARRAY_LENGTH(buster_a64_memory_prefetch_names)) &&
                buster_a64_memory_semantic_equals_string(actual.text, buster_a64_memory_prefetch_text((u32)desired.payload))) ||
               ((desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER ||
                 desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR ||
                 desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE) &&
                desired.aux < BUSTER_A64_MEMORY_ARRANGEMENT_COUNT &&
                buster_a64_memory_semantic_equals_string(actual.text, buster_a64_memory_arrangement_text((BusterA64MemoryArrangement)desired.aux)));
    }
    u64 actual_uint = 0;
    u64 desired_uint = 0;
    if (buster_a64_memory_value_uint(actual, &actual_uint) && buster_a64_memory_value_uint(desired, &desired_uint))
    {
        if (actual_uint != desired_uint) { return false;
}
        if (desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER || desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE)
        {
            s64 signed_value = 0;
            if (!buster_a64_memory_value_sint(desired, &signed_value)) { return false;
}
            return (s64)actual_uint == signed_value;
        }
        return true;
    }
    s64 actual_signed = 0;
    s64 desired_signed = 0;
    return buster_a64_memory_value_sint(actual, &actual_signed) && buster_a64_memory_value_sint(desired, &desired_signed) &&
           actual_signed == desired_signed;
}

static bool
buster_a64_memory_apply_table_entry(BusterA64SemanticForm form, BusterA64SemanticTransform transform,
                                    BusterA64SemanticValue entry, u32* fields, u64* assigned)
{
    BusterA64SemanticTableHeader table = {0};
    if (!buster_a64_semantic_table_header(transform.table_id, &table) || entry.key_count != table.key_header_count) { return false;
}
    for (u32 index = 0; index < entry.key_count; index += 1)
    {
        BusterA64SemanticString key_name = {0};
        BusterA64SemanticValueAtom atom = {0};
        u32 local = 0;
        if (!buster_a64_semantic_table_key_header(transform.table_id, index, &key_name) ||
            !buster_a64_semantic_value_atom(entry.key_first + index, &atom) || !buster_a64_memory_find_field(form, key_name, &local)) { return false;
}
        if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER)
        {
            if (atom.integer < 0 || atom.integer > UINT32_MAX || !buster_a64_memory_assign_field(form, local, (u32)atom.integer, fields, assigned)) { return false;
}
        }
        else if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS)
        {
            u32 expected = 0, mask = 0;
            u8 width = 0;
            BusterA64SemanticField field = {0};
            if (!buster_a64_memory_parse_bits(atom.text, &expected, &mask, &width) ||
                !buster_a64_semantic_field(form.field_first + local, &field) || width > field.width) { return false;
}
            u64 bit = UINT64_C(1) << local;
            if ((*assigned & bit) != 0)
            {
                if ((fields[local] & mask) != (expected & mask)) { return false;
}
            }
            else if (!buster_a64_memory_assign_field(form, local, expected, fields, assigned)) { return false;
}
        }
        else { return false;
}
    }
    return true;
}

static bool
buster_a64_memory_inverse_tables(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                 BusterA64SemanticVMValue desired, u32* fields, u64* assigned)
{
    bool found_transform = false;
    for (u32 transform_index = 0; transform_index < operand.transform_count; transform_index += 1)
    {
        u32 transform_id = operand.transform_first + transform_index;
        BusterA64SemanticTransform transform = {0};
        if (!buster_a64_semantic_transform(transform_id, &transform)) { return false;
}
        if (transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE) { continue;
}
        found_transform = true;
        u32 matches = 0;
        u32 selected[64] = {0};
        u64 selected_assigned = 0;
        for (u32 entry_index = 0; entry_index < transform.value_count; entry_index += 1)
        {
            BusterA64SemanticValue entry = {0};
            BusterA64SemanticValueAtom atom = {0};
            if (!buster_a64_semantic_transform_value(transform_id, entry_index, &entry) || entry.result_count != 1 ||
                !buster_a64_semantic_value_atom(entry.result_first, &atom)) { return false;
}
            if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM && buster_a64_memory_semantic_equal_cstr(atom.text, "RESERVED")) { continue;
}
            if (!buster_a64_memory_atom_matches(atom, desired)) { continue;
}
            u32 candidate[64] = {0};
            for (u32 field_index = 0; field_index < form.field_count; field_index += 1) { candidate[field_index] = fields[field_index];
}
            u64 candidate_assigned = *assigned;
            if (!buster_a64_memory_apply_table_entry(form, transform, entry, candidate, &candidate_assigned)) { continue;
}
            matches += 1;
            if (matches == 1)
            {
                for (u32 field_index = 0; field_index < form.field_count; field_index += 1) { selected[field_index] = candidate[field_index];
}
                selected_assigned = candidate_assigned;
            }
            else
            {
                for (u32 field_index = 0; field_index < form.field_count; field_index += 1)
                {
                    if (selected[field_index] != candidate[field_index] ||
                        ((selected_assigned ^ candidate_assigned) & (UINT64_C(1) << field_index)) != 0) { return false;
}
                }
            }
        }
        if (matches == 0) { return false;
}
        for (u32 field_index = 0; field_index < form.field_count; field_index += 1) { fields[field_index] = selected[field_index];
}
        *assigned = selected_assigned;
    }
    return found_transform;
}

static bool
buster_a64_memory_assign_direct_operand_fields(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                                BusterA64SemanticVMValue value, u32* fields, u64* assigned)
{
    u64 payload = 0;
    if (!buster_a64_memory_value_uint(value, &payload) || payload > 31) { return false;
}
    u32 offset = 0;
    if (!buster_a64_memory_member_offset(operand.symbol, &offset)) { return false;
}
    bool list = value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST;
    if (list)
    {
        if (value.aux2 == 0 || offset >= value.aux2 || payload + value.aux2 > 32) { return false;
}
    }
    else if (payload < offset) { return false;
}
    u32 raw = list ? (u32)payload : (u32)(payload - offset);
    for (u32 field_index = 0; field_index < operand.field_index_count; field_index += 1)
    {
        u32 local = 0;
        if (!buster_a64_memory_operand_field_local(form, operand, field_index, &local) ||
            !buster_a64_memory_assign_field(form, local, raw, fields, assigned)) { return false;
}
    }
    return true;
}

static bool
buster_a64_memory_register_value_ok(BusterA64SemanticOperand operand, BusterA64SemanticVMValue value)
{
    if (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER || value.payload > 31 ||
        (value.flags & ~(BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR)) != 0 ||
        ((value.flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP) && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SP_ALLOWED) == 0) ||
        ((value.flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR) && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED) == 0) ||
        ((value.flags & (BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP | BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR)) && value.payload != 31)) { return false;
}
    if ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_W32) && value.width != 32) { return false;
}
    if ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_X64) && value.width != 64) { return false;
}
    return true;
}

static bool
buster_a64_memory_simd_value_ok(BusterA64SemanticOperand operand, BusterA64SemanticVMValue value)
{
    if (value.payload > 31) { return false;
}
    if (value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR &&
        value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST &&
        value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE) { return false;
}
    if ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR &&
        !((operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER) && value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST)) { return false;
}
    if ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR) && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR &&
        value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST && value.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE) { return false;
}
    if (value.aux == 0 || value.aux >= BUSTER_A64_MEMORY_ARRANGEMENT_COUNT) { return false;
}
    BusterA64MemoryArrangement arrangement = (BusterA64MemoryArrangement)value.aux;
    u8 scalar_width = buster_a64_memory_scalar_width(arrangement);
    u8 register_width = buster_a64_memory_register_width(arrangement);
    if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR)
    {
        if (!scalar_width || arrangement == BUSTER_A64_MEMORY_ARRANGEMENT_Q || value.width != scalar_width) { return false;
}
    }
    else if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR ||
             value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST || value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE)
    {
        bool scalar_list = value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) != 0;
        if ((!scalar_list && register_width != 64 && register_width != 128) || value.width != (scalar_list ? scalar_width : register_width)) { return false;
}
        if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST &&
            (value.aux2 == 0 || value.aux2 > 4 || value.payload + value.aux2 > 32)) { return false;
}
    }
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_B8) { if (scalar_width != 8) { return false;
}
}
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_H16) { if (scalar_width != 16) { return false;
}
}
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_S32) { if (scalar_width != 32) { return false;
}
}
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_D64) { if (scalar_width != 64) { return false;
}
}
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_Q128) { if (register_width != 128) { return false;
}
}
    return true;
}

static bool
buster_a64_memory_inverse_enumerate(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                    BusterA64SemanticVMValue desired, u32* fields, u64* assigned)
{
    u32 locals[16] = {0};
    u8 widths[16] = {0};
    u32 local_count = 0;
    for (u32 index = 0; index < operand.field_index_count; index += 1)
    {
        u32 local = 0;
        BusterA64SemanticField field = {0};
        if (!buster_a64_memory_operand_field_local(form, operand, index, &local) ||
            !buster_a64_semantic_field(form.field_first + local, &field)) { return false;
}
        bool duplicate = false;
        for (u32 previous = 0; previous < local_count; previous += 1) { duplicate = duplicate || locals[previous] == local;
}
        if (!duplicate)
        {
            if (local_count >= BUSTER_ARRAY_LENGTH(locals)) { return false;
}
            locals[local_count] = local;
            widths[local_count] = field.width;
            local_count += 1;
        }
    }
    if (local_count == 0 || operand.transform_count == 0) { return false;
}
    u64 total = 1;
    for (u32 index = 0; index < local_count; index += 1)
    {
        if (widths[index] > 16 || total > UINT64_C(262144) / (UINT64_C(1) << widths[index])) { return false;
}
        total *= UINT64_C(1) << widths[index];
    }
    u32 selected[64] = {0};
    u64 selected_assigned = 0;
    u32 matches = 0;
    for (u64 code = 0; code < total; code += 1)
    {
        u64 cursor = code;
        u32 candidate[64] = {0};
        for (u32 index = 0; index < form.field_count; index += 1) { candidate[index] = fields[index];
}
        u64 candidate_assigned = *assigned;
        bool valid = true;
        for (u32 index = 0; index < local_count; index += 1)
        {
            u32 value = (u32)(cursor & ((UINT64_C(1) << widths[index]) - 1));
            cursor >>= widths[index];
            if (!buster_a64_memory_assign_field(form, locals[index], value, candidate, &candidate_assigned)) { valid = false; break; }
        }
        if (!valid) { continue;
}
        bool transform_match = false;
        /* A transform chain is an ordered presentation pipeline.  Earlier
         * transforms expose intermediate field encodings (for example the
         * raw S:imm9 concat); only the final transform is the architectural
         * operand value and may be used for inversion. */
        u32 transform_index = operand.transform_count - 1;
        BusterA64SemanticVMValue actual = buster_a64_semantic_vm_value_invalid();
        BusterA64SemanticVMFields vm_fields = {.count = form.field_count};
        for (u32 field_index = 0; field_index < form.field_count; field_index += 1) { vm_fields.values[field_index] = candidate[field_index];
}
        BusterA64SemanticVMStatus status = buster_a64_semantic_vm_eval_transform(form.id, operand.transform_first + transform_index, &vm_fields, &actual);
        if (status == BUSTER_A64_SEMANTIC_VM_STATUS_OK && buster_a64_memory_value_matches(actual, desired)) { transform_match = true;
}
        if (!transform_match) { continue;
}
        matches += 1;
        if (matches == 1)
        {
            for (u32 index = 0; index < form.field_count; index += 1) { selected[index] = candidate[index];
}
            selected_assigned = candidate_assigned;
        }
        else
        {
            for (u32 index = 0; index < form.field_count; index += 1) {
                if (selected[index] != candidate[index] || ((selected_assigned ^ candidate_assigned) & (UINT64_C(1) << index)) != 0) { return false;
}
}
        }
    }
    if (matches != 1) { return false;
}
    for (u32 index = 0; index < form.field_count; index += 1) { fields[index] = selected[index];
}
    *assigned = selected_assigned;
    return true;
}

static bool
buster_a64_memory_inverse_operand(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                  BusterA64SemanticVMValue desired, u32* fields, u64* assigned)
{
    if (operand.transform_count != 0)
    {
        if (buster_a64_memory_inverse_tables(form, operand, desired, fields, assigned)) { return true;
}
        return buster_a64_memory_inverse_enumerate(form, operand, desired, fields, assigned);
    }
    return buster_a64_memory_assign_direct_operand_fields(form, operand, desired, fields, assigned);
}

static bool
buster_a64_memory_encode_lane_operand(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                       BusterA64SemanticVMValue desired, u32* fields, u64* assigned)
{
    if (!fields || !assigned) { return false;
}
    u64 lane = 0;
    bool typed = desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE;
    if (typed)
    {
        if (desired.aux == 0 || desired.aux >= BUSTER_A64_MEMORY_ARRANGEMENT_COUNT ||
            desired.width != buster_a64_memory_register_width((BusterA64MemoryArrangement)desired.aux) ||
            (desired.width != 64 && desired.width != 128)) { return false;
}
        bool q = false;
        if (!buster_a64_memory_lane_q_for_arrangement((BusterA64MemoryArrangement)desired.aux, &q)) { return false;
}
        BusterA64MemoryArrangement expected = BUSTER_A64_MEMORY_ARRANGEMENT_INVALID;
        if (!buster_a64_memory_lane_arrangement_from_operand(form, operand, q, &expected) || expected != (BusterA64MemoryArrangement)desired.aux) { return false;
}
        lane = desired.aux2;
        if (lane >= buster_a64_memory_lane_count((BusterA64MemoryArrangement)desired.aux)) { return false;
}
        if (!buster_a64_memory_lane_assign_q(form, q, fields, assigned)) { return false;
}
    }
    else
    {
        if (!buster_a64_memory_value_uint(desired, &lane) || lane > 31) { return false;
}
    }
    BusterA64SemanticVMValue immediate = buster_a64_memory_value_immediate((s64)lane, 8, false);
    if (immediate.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) { return false;
}
    bool assigned_fields = operand.transform_count != 0 ?
        buster_a64_memory_inverse_operand(form, operand, immediate, fields, assigned) :
        buster_a64_memory_assign_direct_operand_fields(form, operand, immediate, fields, assigned);
    return assigned_fields && buster_a64_memory_lane_validate_immediate(form, operand, fields, lane);
}

static bool
buster_a64_memory_arrangement_selector_index(BusterA64SemanticForm form, u32 operand_index, u32* selector_index)
{
    if (!selector_index || operand_index >= form.operand_count) { return false;
}
    *selector_index = UINT32_MAX;
    for (u32 row_index = 0; row_index < BUSTER_A64_MEMORY_ROW_COUNT; row_index += 1)
    {
        BusterA64MemoryGeneratedRow const* row = 0;
        if (!buster_a64_memory_row_valid(row_index, &row) || row->semantic_form_id != form.id) { continue;
}
        if (operand_index >= BUSTER_A64_MEMORY_GENERATED_MAX_OPERANDS) { return false;
}
        BusterA64MemoryGeneratedArrangementBinding binding = buster_a64_memory_generated_arrangement_bindings[row_index][operand_index];
        if (binding.selector_index == BUSTER_A64_MEMORY_ARRANGEMENT_BINDING_NONE) { return false;
}
        if (binding.selector_index >= form.operand_count || (binding.direction != 1 && binding.direction != -1)) { return false;
}
        *selector_index = binding.selector_index;
        return true;
    }
    return false;
}

static bool
buster_a64_memory_bound_arrangement(BusterA64SemanticForm form, u32 operand_index,
                                    BusterA64SemanticVMValue const* values, BusterA64MemoryArrangement* arrangement)
{
    if (!values || !arrangement) { return false;
}
    u32 selector_index = UINT32_MAX;
    if (!buster_a64_memory_arrangement_selector_index(form, operand_index, &selector_index) ||
        values[selector_index].kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT ||
        values[selector_index].aux == 0 || values[selector_index].aux >= BUSTER_A64_MEMORY_ARRANGEMENT_COUNT) { return false;
}
    *arrangement = (BusterA64MemoryArrangement)values[selector_index].aux;
    return true;
}

static bool
buster_a64_memory_infer_scalar_arrangement(BusterA64SemanticString symbol, BusterA64MemoryArrangement* arrangement)
{
    if (!arrangement || symbol.length < 2) { return false;
}
    char8 letter = buster_a64_semantic_string_byte(symbol, 1);
    if (letter == 'B' || letter == 'b') { *arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_B;
    } else if (letter == 'H' || letter == 'h') { *arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_H;
    } else if (letter == 'S' || letter == 's') { *arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_S;
    } else if (letter == 'D' || letter == 'd') { *arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_D;
    } else if (letter == 'Q' || letter == 'q') { *arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_Q;
    } else { return false;
}
    return true;
}

static bool
buster_a64_memory_encode_operand(BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand,
                                 BusterA64SemanticVMValue const* values, BusterA64SemanticVMValue desired,
                                 u32* fields, u64* assigned)
{
    BUSTER_UNUSED(operand_index);
    BUSTER_UNUSED(values);
    if (desired.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) { return false;
}
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX) {
        return buster_a64_memory_encode_lane_operand(form, operand, desired, fields, assigned);
}
    if ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_MEMORY_OFFSET) &&
        operand.kind != BUSTER_A64_SEMANTIC_OPERAND_MEMORY_BASE && operand.kind != BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER)
    {
        return buster_a64_memory_inverse_operand(form, operand, desired, fields, assigned);
    }
    switch (operand.kind)
    {
        case BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER:
        case BUSTER_A64_SEMANTIC_OPERAND_MEMORY_BASE:
            return buster_a64_memory_register_value_ok(operand, desired) &&
                   buster_a64_memory_assign_direct_operand_fields(form, operand, desired, fields, assigned);
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE:
            if (!buster_a64_memory_simd_value_ok(operand, desired)) { return false;
}
            if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST && desired.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST) { return false;
}
            if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE && desired.kind != BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE) { return false;
}
            /* Scalar/vector forms without an explicit selector carry their
             * arrangement in the mnemonic family.  The raw register field
             * is independent of that presentation, so leave the selector
             * check to rows that actually expose one. */
            return buster_a64_memory_assign_direct_operand_fields(form, operand, desired, fields, assigned);
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR:
        case BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR:
            return buster_a64_memory_inverse_operand(form, operand, desired, fields, assigned);
        case BUSTER_A64_SEMANTIC_OPERAND_EXTEND:
        case BUSTER_A64_SEMANTIC_OPERAND_PREFETCH_OPERATION:
            return buster_a64_memory_inverse_operand(form, operand, desired, fields, assigned);
        case BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE:
        case BUSTER_A64_SEMANTIC_OPERAND_MEMORY_OFFSET:
        case BUSTER_A64_SEMANTIC_OPERAND_SHIFT:
        {
            BusterA64SemanticVMValue effective = desired;
            if (operand.transform_count != 0) { return buster_a64_memory_inverse_operand(form, operand, effective, fields, assigned);
}
            u64 unsigned_value = 0;
            s64 signed_value = 0;
            bool signed_field = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIGNED) != 0;
            if (signed_field)
            {
                if (!buster_a64_memory_value_sint(effective, &signed_value) || operand.field_index_count == 0) { return false;
}
                u32 local = 0;
                BusterA64SemanticField field = {0};
                if (!buster_a64_memory_operand_field_local(form, operand, 0, &local) || !buster_a64_semantic_field(form.field_first + local, &field) || field.width == 0 || field.width > 32) { return false;
}
                u64 mask = field.width == 32 ? UINT64_C(0xffffffff) : (UINT64_C(1) << field.width) - 1;
                return buster_a64_memory_assign_field(form, local, (u32)((u64)signed_value & mask), fields, assigned);
            }
            if (!buster_a64_memory_value_uint(effective, &unsigned_value) || unsigned_value > UINT32_MAX || operand.field_index_count == 0) { return false;
}
            return buster_a64_memory_assign_direct_operand_fields(form, operand, effective, fields, assigned);
        }
        case BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT:
            return false;
        default:
            return false;
    }
}

static bool
buster_a64_memory_validate_overlap(BusterA64SemanticForm form, BusterA64MemoryGeneratedRow const* row,
                                   BusterA64SemanticVMValue const* values)
{
    if (!row || !values) { return false;
}
    /* Every SIMD list is a bounded non-wrapping sequence.  Member symbols
     * are checked again here so callers cannot smuggle an inconsistent list
     * through a shared raw field. */
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        u32 offset = 0;
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand) || !buster_a64_memory_member_offset(operand.symbol, &offset)) { return false;
}
        if (values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST)
        {
            if (values[index].aux2 == 0 || values[index].aux2 > 4 || values[index].payload + values[index].aux2 > 32 || offset >= values[index].aux2) { return false;
}
        }
    }
    if (row->overlap_policy == BUSTER_A64_MEMORY_OVERLAP_BASE_DISJOINT)
    {
        u32 base = UINT32_MAX;
        for (u32 index = 0; index < form.operand_count; index += 1)
        {
            BusterA64SemanticOperand operand = {0};
            if (!buster_a64_semantic_operand(form.operand_first + index, &operand)) { return false;
}
            if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_MEMORY_BASE) { base = (u32)values[index].payload;
}
        }
        if (base != UINT32_MAX)
        {
            for (u32 index = 0; index < form.operand_count; index += 1)
            {
                BusterA64SemanticOperand operand = {0};
                if (!buster_a64_semantic_operand(form.operand_first + index, &operand)) { return false;
}
                if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_MEMORY_BASE) { continue;
}
                if ((values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER || values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER ||
                     values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR || values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR ||
                     values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST) && values[index].payload == base) { return false;
}
            }
        }
    }
    if (row->overlap_policy == BUSTER_A64_MEMORY_OVERLAP_PAIR_DISJOINT)
    {
        u32 first[2] = {UINT32_MAX, UINT32_MAX};
        u32 count = 0;
        for (u32 index = 0; index < form.operand_count && count < 2; index += 1)
        {
            BusterA64SemanticOperand operand = {0};
            u32 offset = 0;
            if (!buster_a64_semantic_operand(form.operand_first + index, &operand) || !buster_a64_memory_member_offset(operand.symbol, &offset)) { return false;
}
            if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_MEMORY_BASE) { continue;
}
            if (offset != 0) { continue;
}
            if (values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER || values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER ||
                values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR || values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR) {
                first[count++] = (u32)values[index].payload;
}
        }
        if (count == 2 && (first[0] == first[1] || first[0] + 1 == first[1] || first[1] + 1 == first[0])) { return false;
}
    }
    return true;
}

static bool
buster_a64_memory_decode_arrangement_value(BusterA64SemanticVMValue actual, BusterA64SemanticVMValue* result)
{
    if (!result) { return false;
}
    if (actual.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT)
    {
        if (actual.payload >= BUSTER_A64_MEMORY_ARRANGEMENT_COUNT) { return false;
}
        *result = buster_a64_memory_value_arrangement((BusterA64MemoryArrangement)actual.payload);
        return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
    }
    if (actual.kind != BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION) { return false;
}
    char8 buffer[8] = {0};
    if (actual.text.length >= BUSTER_ARRAY_LENGTH(buffer)) { return false;
}
    for (u32 index = 0; index < actual.text.length; index += 1) { buffer[index] = buster_a64_semantic_string_byte(actual.text, index);
}
    BusterA64MemoryArrangement arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_INVALID;
    if (!buster_a64_memory_arrangement_from_string((String8){buffer, actual.text.length}, &arrangement)) { return false;
}
    *result = buster_a64_memory_value_arrangement(arrangement);
    return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

static bool
buster_a64_memory_decode_extend_value(BusterA64SemanticVMValue actual, BusterA64SemanticVMValue* result)
{
    if (!result || actual.kind != BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION) { return false;
}
    for (u32 index = 1; index < BUSTER_A64_MEMORY_EXTEND_COUNT; index += 1)
    {
        if (buster_a64_memory_semantic_equals_string(actual.text, buster_a64_memory_extend_text((BusterA64MemoryExtend)index)))
        {
            *result = buster_a64_memory_value_extend((BusterA64MemoryExtend)index);
            return true;
        }
    }
    return false;
}

static bool
buster_a64_memory_decode_prefetch_value(BusterA64SemanticVMValue actual, BusterA64SemanticVMValue* result)
{
    if (!result) { return false;
}
    if (actual.kind != BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION) { return false;
}
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(buster_a64_memory_prefetch_names); index += 1)
    {
        if (buster_a64_memory_semantic_equals_string(actual.text, buster_a64_memory_prefetch_text(index)))
        {
            *result = buster_a64_memory_value_prefetch(index);
            return true;
        }
    }
    return false;
}

static bool
buster_a64_memory_decode_symbolic(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                  BusterA64SemanticVMFields const* fields, BusterA64SemanticVMValue* result)
{
    if (!fields || !result || operand.transform_count == 0) { return false;
}
    /* Transform records are an ordered pipeline.  The records preceding the
     * last one are field-level intermediates (concat/slice inputs), not
     * independently valid architectural values.  Decoding an intermediate
     * made scaled immediates and lane indexes appear to have the wrong type
     * and silently lost their bounds. */
    u32 transform_id = operand.transform_first + operand.transform_count - 1;
    BusterA64SemanticVMValue actual = buster_a64_semantic_vm_value_invalid();
    BusterA64SemanticVMStatus status = buster_a64_semantic_vm_eval_transform(form.id, transform_id, fields, &actual);
    if (status != BUSTER_A64_SEMANTIC_VM_STATUS_OK) { return false;
}
    if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT ||
        operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR ||
        operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR) {
        return buster_a64_memory_decode_arrangement_value(actual, result);
}
    if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_EXTEND) {
        return buster_a64_memory_decode_extend_value(actual, result);
}
    if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_PREFETCH_OPERATION)
    {
        if (buster_a64_memory_decode_prefetch_value(actual, result)) { return true;
}
        *result = actual;
        return true;
    }
    if (actual.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER || actual.kind == BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER ||
        actual.kind == BUSTER_A64_SEMANTIC_VM_VALUE_BITS || actual.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE) {
        actual.kind = BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE;
}
    *result = actual;
    return actual.kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

static bool
buster_a64_memory_decode_gpr(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                             BusterA64SemanticVMFields const* fields, BusterA64SemanticVMValue* result)
{
    if (!fields || !result || operand.field_index_count == 0) { return false;
}
    u32 local = 0;
    if (!buster_a64_memory_operand_field_local(form, operand, 0, &local)) { return false;
}
    u32 offset = 0;
    if (!buster_a64_memory_member_offset(operand.symbol, &offset)) { return false;
}
    u32 number = (fields->values[local] + offset) & 31u;
    u8 width = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_W32) ? 32 : 64;
    bool memory_base = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_MEMORY_BASE) != 0;
    bool sp = memory_base && number == 31 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SP_ALLOWED) != 0;
    bool zr = !memory_base && number == 31 && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED) != 0;
    *result = buster_a64_memory_value_gpr(number, width, sp, zr);
    return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

static bool
buster_a64_memory_decode_simd(BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand,
                              BusterA64SemanticVMFields const* fields, BusterA64SemanticVMValue const* values,
                              BusterA64SemanticVMValue* result)
{
    if (!fields || !values || !result || operand.field_index_count == 0) { return false;
}
    u32 local = 0;
    if (!buster_a64_memory_operand_field_local(form, operand, 0, &local)) { return false;
}
    u32 offset = 0;
    if (!buster_a64_memory_member_offset(operand.symbol, &offset)) { return false;
}
    bool list = operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST || (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER);
    u32 number = list ? fields->values[local] : (fields->values[local] + offset) & 31u;
    BusterA64MemoryArrangement arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_INVALID;
    if (!buster_a64_memory_bound_arrangement(form, operand_index, values, &arrangement)) {
        if (!buster_a64_memory_infer_scalar_arrangement(operand.symbol, &arrangement)) { arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_Q;
}
}
    bool scalar = (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR) != 0;
    if (list)
    {
        u32 count = offset + 1;
        for (u32 index = 0; index < form.operand_count; index += 1)
        {
            BusterA64SemanticOperand candidate = {0};
            u32 candidate_offset = 0;
            if (!buster_a64_semantic_operand(form.operand_first + index, &candidate) || !buster_a64_memory_member_offset(candidate.symbol, &candidate_offset)) { return false;
}
            if (candidate.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST || (candidate.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER)) {
                if (candidate_offset + 1 > count) { count = candidate_offset + 1;
}
}
        }
        *result = buster_a64_memory_value_list(number, count, arrangement);
    }
    else if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE) {
        return false;
    } else
    {
        *result = buster_a64_memory_value_register(number, arrangement, scalar);
    }
    return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

static bool
buster_a64_memory_decode_immediate(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                                   BusterA64SemanticVMFields const* fields, BusterA64SemanticVMValue* result)
{
    if (!fields || !result) { return false;
}
    if (operand.transform_count != 0 && buster_a64_memory_decode_symbolic(form, operand, fields, result)) { return true;
}
    if (operand.field_index_count == 0) { return false;
}
    u32 local = 0;
    BusterA64SemanticField field = {0};
    if (!buster_a64_memory_operand_field_local(form, operand, 0, &local) || !buster_a64_semantic_field(form.field_first + local, &field)) { return false;
}
    u32 raw = fields->values[local];
    if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIGNED)
    {
        if (field.width == 0 || field.width > 32) { return false;
}
        u32 sign = UINT32_C(1) << (field.width - 1);
        s64 value = (raw & sign) ? (s64)(raw | ~((UINT32_C(1) << field.width) - 1)) : (s64)raw;
        *result = buster_a64_memory_value_immediate(value, field.width, true);
    }
    else { *result = buster_a64_memory_value_immediate(raw, field.width, false);
}
    return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

static bool
buster_a64_memory_decode_lane(BusterA64SemanticForm form, BusterA64SemanticOperand operand,
                               BusterA64SemanticVMFields const* fields, BusterA64SemanticVMValue* result)
{
    if (!fields || !result) { return false;
}
    BusterA64SemanticVMValue index_value = buster_a64_semantic_vm_value_invalid();
    if (!buster_a64_memory_decode_immediate(form, operand, fields, &index_value)) { return false;
}
    u64 lane = 0;
    if (!buster_a64_memory_value_uint(index_value, &lane) || lane > 31) { return false;
}
    bool q = false;
    if (!buster_a64_memory_lane_field_q(form, fields->values, &q)) { return false;
}
    BusterA64MemoryArrangement arrangement = BUSTER_A64_MEMORY_ARRANGEMENT_INVALID;
    if (!buster_a64_memory_lane_arrangement_from_operand(form, operand, q, &arrangement)) { return false;
}
    u32 count = buster_a64_memory_lane_count(arrangement);
    if (!count || lane >= count || buster_a64_memory_register_width(arrangement) != (q ? 128 : 64)) { return false;
}
    *result = buster_a64_memory_value_lane(0, arrangement, (u32)lane);
    return result->kind != BUSTER_A64_SEMANTIC_VM_VALUE_INVALID;
}

BusterA64MemoryStatus
buster_a64_memory_encode(Target target, BusterA64MemoryInstruction const* instruction, u32* word)
{
    if (!instruction || !word || instruction->operand_count > BUSTER_A64_MEMORY_MAX_OPERANDS) { return BUSTER_A64_MEMORY_STATUS_INVALID_ARGUMENT;
}
    BusterA64MemoryGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_memory_row_valid(instruction->row_index, &row)) { return BUSTER_A64_MEMORY_STATUS_BOUNDS;
}
    if (!row->candidate) { return BUSTER_A64_MEMORY_STATUS_UNSUPPORTED;
}
    if (!buster_a64_semantic_form(row->semantic_form_id, &form) || form.owner != BUSTER_A64_SEMANTIC_OWNER_MEMORY ||
        form.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL || form.status != BUSTER_A64_SEMANTIC_STATUS_DEFINED ||
        form.operand_count != instruction->operand_count || form.operand_count > BUSTER_A64_MEMORY_MAX_OPERANDS) {
        return BUSTER_A64_MEMORY_STATUS_INVALID_ARGUMENT;
}
    u32 fields[64] = {0};
    u64 assigned = 0;
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand)) { return BUSTER_A64_MEMORY_STATUS_BOUNDS;
}
        if (!buster_a64_memory_encode_operand(form, index, operand, instruction->operands, instruction->operands[index], fields, &assigned)) {
            return BUSTER_A64_MEMORY_STATUS_RANGE;
}
    }
    /* Arrangement and index width are coupled semantic constraints.  A
     * shared field may have both Wm and Xm presentation operands; at least
     * one alternative must agree with an explicit extension. */
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand)) { return BUSTER_A64_MEMORY_STATUS_BOUNDS;
}
        if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_EXTEND)
        {
            BusterA64SemanticVMValue extend = instruction->operands[index];
            if (extend.kind != BUSTER_A64_SEMANTIC_VM_VALUE_EXTEND || extend.aux >= BUSTER_A64_MEMORY_EXTEND_COUNT) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
            bool wide = extend.aux == BUSTER_A64_MEMORY_EXTEND_UXTX || extend.aux == BUSTER_A64_MEMORY_EXTEND_SXTX || extend.aux == BUSTER_A64_MEMORY_EXTEND_LSL;
            bool index_match = false;
            for (u32 candidate_index = 0; candidate_index < form.operand_count; candidate_index += 1)
            {
                BusterA64SemanticOperand candidate = {0};
                if (!buster_a64_semantic_operand(form.operand_first + candidate_index, &candidate)) { return BUSTER_A64_MEMORY_STATUS_BOUNDS;
}
                if (candidate_index == index || candidate.kind != BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER) { continue;
}
                if ((candidate.flags & (BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_W32 | BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_X64)) == 0) { continue;
}
                BusterA64SemanticVMValue value = instruction->operands[candidate_index];
                if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER && ((value.width == 64) == wide)) { index_match = true;
}
            }
            if (!index_match && (operand.flags & BUSTER_A64_SEMANTIC_FLAG_EXTEND_OPTION)) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
        }
    }
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand)) { return BUSTER_A64_MEMORY_STATUS_BOUNDS;
}
        if (operand.kind != BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER && operand.kind != BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST &&
            operand.kind != BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE && !(operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX)) { continue;
}
        BusterA64MemoryArrangement selected = BUSTER_A64_MEMORY_ARRANGEMENT_INVALID;
        if (buster_a64_memory_bound_arrangement(form, index, instruction->operands, &selected) && instruction->operands[index].aux != selected) {
            return BUSTER_A64_MEMORY_STATUS_RANGE;
}
    }
    if (!buster_a64_memory_validate_overlap(form, row, instruction->operands)) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
    BusterA64SemanticVMFields vm_fields = {.count = form.field_count};
    for (u32 index = 0; index < form.field_count; index += 1) { vm_fields.values[index] = fields[index];
}
    u32 candidate_word = 0;
    BusterA64SemanticVMStatus vm_status = buster_a64_semantic_vm_encode_fields(form.id, &vm_fields, &candidate_word);
    if (vm_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK) { return buster_a64_memory_vm_status(vm_status);
}
    BusterAarch64CanonicalDecodeResult canonical = {0};
    BusterAarch64CanonicalDecodeStatus canonical_status = buster_aarch64_canonical_decode(target, candidate_word, &canonical);
    if (canonical_status != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS) { return buster_a64_memory_canonical_status(canonical_status);
}
    if (canonical.arm_row_digest != row->source_digest) { return BUSTER_A64_MEMORY_STATUS_TARGET_MISMATCH;
}
    *word = candidate_word;
    return BUSTER_A64_MEMORY_STATUS_OK;
}

static BusterA64MemoryStatus
buster_a64_memory_decode_internal(Target target, u32 requested_row, bool row_requested, u32 word,
                                  BusterA64MemoryResult* result)
{
    if (!result) { return BUSTER_A64_MEMORY_STATUS_INVALID_ARGUMENT;
}
    BusterAarch64CanonicalDecodeResult canonical = {0};
    BusterAarch64CanonicalDecodeStatus canonical_status = buster_aarch64_canonical_decode(target, word, &canonical);
    if (canonical_status != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS) { return buster_a64_memory_canonical_status(canonical_status);
}
    u32 row_index = 0;
    if (!buster_a64_memory_find_source_digest(canonical.arm_row_digest, &row_index) || (row_requested && row_index != requested_row)) {
        return BUSTER_A64_MEMORY_STATUS_TARGET_MISMATCH;
}
    BusterA64MemoryGeneratedRow const* row = 0;
    BusterA64SemanticForm form = {0};
    if (!buster_a64_memory_row_valid(row_index, &row) || !row->candidate || !buster_a64_semantic_form(row->semantic_form_id, &form) ||
        form.owner != BUSTER_A64_SEMANTIC_OWNER_MEMORY || form.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL ||
        form.status != BUSTER_A64_SEMANTIC_STATUS_DEFINED) { return BUSTER_A64_MEMORY_STATUS_UNSUPPORTED;
}
    BusterA64SemanticVMResult decoded = {0};
    BusterA64SemanticVMStatus vm_status = buster_a64_semantic_vm_decode_fields(form.id, word, &decoded);
    if (vm_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK) { return buster_a64_memory_vm_status(vm_status);
}
    BusterA64SemanticVMValue values[BUSTER_A64_MEMORY_MAX_OPERANDS] = {0};
    /* Arrangement selectors are evaluated first because vector/list/lane
     * values carry the selector as a presentation invariant. */
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand)) { return BUSTER_A64_MEMORY_STATUS_BOUNDS;
}
        if (operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT ||
            operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR ||
            operand.kind == BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR)
        {
            if (!buster_a64_memory_decode_symbolic(form, operand, &decoded.fields, &values[index])) { return BUSTER_A64_MEMORY_STATUS_RESERVED;
}
        }
    }
    for (u32 index = 0; index < form.operand_count; index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        if (!buster_a64_semantic_operand(form.operand_first + index, &operand)) { return BUSTER_A64_MEMORY_STATUS_BOUNDS;
}
        if (operand.flags & BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX)
        {
            if (!buster_a64_memory_decode_lane(form, operand, &decoded.fields, &values[index])) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
            continue;
        }
        if ((operand.flags & BUSTER_A64_SEMANTIC_FLAG_MEMORY_OFFSET) &&
            operand.kind != BUSTER_A64_SEMANTIC_OPERAND_MEMORY_BASE && operand.kind != BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER)
        {
            if (!buster_a64_memory_decode_immediate(form, operand, &decoded.fields, &values[index])) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
            continue;
        }
        switch (operand.kind)
        {
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT:
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR:
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR:
                break;
            case BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER:
            case BUSTER_A64_SEMANTIC_OPERAND_MEMORY_BASE:
                if (!buster_a64_memory_decode_gpr(form, operand, &decoded.fields, &values[index])) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
                break;
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER:
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST:
            case BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE:
                if (!buster_a64_memory_decode_simd(form, index, operand, &decoded.fields, values, &values[index])) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
                break;
            case BUSTER_A64_SEMANTIC_OPERAND_EXTEND:
            case BUSTER_A64_SEMANTIC_OPERAND_PREFETCH_OPERATION:
                if (!buster_a64_memory_decode_symbolic(form, operand, &decoded.fields, &values[index])) { return BUSTER_A64_MEMORY_STATUS_RESERVED;
}
                break;
            case BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE:
            case BUSTER_A64_SEMANTIC_OPERAND_MEMORY_OFFSET:
            case BUSTER_A64_SEMANTIC_OPERAND_SHIFT:
                if (!buster_a64_memory_decode_immediate(form, operand, &decoded.fields, &values[index])) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
                break;
            default:
                return BUSTER_A64_MEMORY_STATUS_UNSUPPORTED;
        }
        if (values[index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
    }
    if (!buster_a64_memory_validate_overlap(form, row, values)) { return BUSTER_A64_MEMORY_STATUS_RANGE;
}
    u32 reencoded = 0;
    vm_status = buster_a64_semantic_vm_encode_fields(form.id, &decoded.fields, &reencoded);
    if (vm_status != BUSTER_A64_SEMANTIC_VM_STATUS_OK || reencoded != word) { return BUSTER_A64_MEMORY_STATUS_TARGET_MISMATCH;
}
    BusterA64MemoryResult candidate = {.status = BUSTER_A64_MEMORY_STATUS_OK,
                                       .row_index = row_index,
                                       .word = word,
                                       .operand_count = form.operand_count};
    for (u32 index = 0; index < form.operand_count; index += 1) { candidate.operands[index] = values[index];
}
    *result = candidate;
    return BUSTER_A64_MEMORY_STATUS_OK;
}

BusterA64MemoryStatus
buster_a64_memory_decode(Target target, u32 word, BusterA64MemoryResult* result)
{
    return buster_a64_memory_decode_internal(target, 0, false, word, result);
}

BusterA64MemoryStatus
buster_a64_memory_decode_row(Target target, u32 row_index, u32 word, BusterA64MemoryResult* result)
{
    if (row_index >= BUSTER_A64_MEMORY_ROW_COUNT) { return BUSTER_A64_MEMORY_STATUS_BOUNDS;
}
    return buster_a64_memory_decode_internal(target, row_index, true, word, result);
}

bool
buster_a64_memory_validate(void)
{
    if (BUSTER_A64_MEMORY_SCHEMA_VERSION != 1u || BUSTER_A64_MEMORY_ROW_COUNT != 559u ||
        BUSTER_A64_MEMORY_TRANSFORM_ROW_COUNT != 234u || BUSTER_A64_MEMORY_FEATURE_GATED_ROW_COUNT != 423u ||
        BUSTER_A64_MEMORY_LITERAL_CONTROL_OVERLAP_COUNT != 3u) { return false;
}
    u32 previous_form = 0;
    u32 arrangement_binding_count = 0;
    for (u32 index = 0; index < BUSTER_A64_MEMORY_ROW_COUNT; index += 1)
    {
        BusterA64MemoryGeneratedRow const* row = 0;
        BusterA64SemanticForm form = {0};
        if (!buster_a64_memory_row_valid(index, &row) || !buster_a64_semantic_form(row->semantic_form_id, &form) ||
            form.owner != BUSTER_A64_SEMANTIC_OWNER_MEMORY || form.kind != BUSTER_A64_SEMANTIC_FORM_CANONICAL ||
            form.status != BUSTER_A64_SEMANTIC_STATUS_DEFINED || form.raw_layout_resolved == 0 ||
            row->operand_count != form.operand_count || (index != 0 && row->semantic_form_id <= previous_form)) { return false;
}
        previous_form = row->semantic_form_id;
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterA64SemanticOperand operand = {0};
            if (!buster_a64_semantic_operand(form.operand_first + operand_index, &operand) || operand.field_index_count == 0) { return false;
}
            BusterA64MemoryGeneratedArrangementBinding binding = buster_a64_memory_generated_arrangement_bindings[index][operand_index];
            if (binding.selector_index != BUSTER_A64_MEMORY_ARRANGEMENT_BINDING_NONE)
            {
                if (binding.selector_index >= form.operand_count || (binding.direction != 1 && binding.direction != -1)) { return false;
}
                arrangement_binding_count += 1;
            }
        }
    }
    return arrangement_binding_count == BUSTER_A64_MEMORY_ARRANGEMENT_BINDING_COUNT;
}
