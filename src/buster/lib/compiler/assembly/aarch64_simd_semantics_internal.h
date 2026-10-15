#pragma once

/*
 * Private seam shared by the AArch64 semantic bindings: the direct and
 * complex AdvSIMD bindings, and alias projection for the field helpers.
 *
 * The generated row and arrangement-binding layouts are declared here once;
 * tools/generate_aarch64_{direct,complex}_simd.py emit tables of these types.
 * A binding passes its tables as a BusterA64SIMDCatalog, so the shared row,
 * table and arrangement interpreters are parameterized by generated data
 * rather than by callbacks.
 *
 * Map:
 * - BusterA64SIMDGeneratedRow, BusterA64SIMDGeneratedArrangementBinding,
 *   BusterA64SIMDCatalog: generated data shapes.
 * - buster_a64_simd_catalog_*: row validation, public row view, digest
 *   lookup and arrangement-selector relations.
 * - buster_a64_simd_semantic_equal*, _field_*, _find_field, _assign_field,
 *   _member_offset, _parse_bits, _symbol_matches, _value_uint: helpers over
 *   the semantic metadata vocabulary.
 * - buster_a64_simd_scalar_width, _lane_count, _register_width,
 *   _arrangement_is_*, _infer_*: arrangement shape and presentation recovery.
 * - buster_a64_simd_inverse_tables, _decode_arrangement: value-table
 *   interpreters.
 * - buster_a64_simd_vm_status, _canonical_status: status translation.
 */

#include <buster/lib/compiler/assembly/aarch64_simd_semantics.h>

#define BUSTER_A64_SIMD_BINDING_NONE 255u

typedef struct BusterA64SIMDGeneratedRow BusterA64SIMDGeneratedRow;
struct BusterA64SIMDGeneratedRow
{
    u32 row_index;
    u32 semantic_form_id;
    u64 source_digest;
    u32 id_offset;
    u16 id_length;
    u32 assembly_offset;
    u16 assembly_length;
    u8 operand_count;
    u8 executable;
};
BUSTER_CT_CHECK(sizeof(BusterA64SIMDGeneratedRow) == 32);

typedef struct BusterA64SIMDGeneratedArrangementBinding BusterA64SIMDGeneratedArrangementBinding;
struct BusterA64SIMDGeneratedArrangementBinding
{
    u8 selector_index;
    s8 direction;
};
BUSTER_CT_CHECK(sizeof(BusterA64SIMDGeneratedArrangementBinding) == 2);

typedef struct BusterA64SIMDCatalog BusterA64SIMDCatalog;
struct BusterA64SIMDCatalog
{
    BusterA64SIMDGeneratedRow const* rows;
    BusterA64SIMDGeneratedArrangementBinding const (*bindings)[BUSTER_A64_SIMD_MAX_OPERANDS];
    char8 const* string_pool;
    u32 row_count;
};

BUSTER_F_DECL bool buster_a64_simd_catalog_row(BusterA64SIMDCatalog const* catalog, u32 row_index, BusterA64SIMDGeneratedRow const** result);
BUSTER_F_DECL bool buster_a64_simd_catalog_row_info(BusterA64SIMDCatalog const* catalog, u32 row_index, BusterA64SIMDRowInfo* result);
BUSTER_F_DECL bool buster_a64_simd_catalog_find_source_digest(BusterA64SIMDCatalog const* catalog, u64 source_digest, u32* row_index);
BUSTER_F_DECL BusterA64SIMDGeneratedArrangementBinding const* buster_a64_simd_catalog_binding(BusterA64SIMDCatalog const* catalog, u32 row_index,
                                                                                              u32 operand_index);
BUSTER_F_DECL bool buster_a64_simd_catalog_arrangement_binding(BusterA64SIMDCatalog const* catalog, u32 row_index, u32 operand_index,
                                                               BusterA64SIMDArrangementBinding* result);
BUSTER_F_DECL bool buster_a64_simd_catalog_register_arrangement_ok(BusterA64SIMDCatalog const* catalog, u32 row_index, BusterA64SemanticForm form,
                                                                   u32 operand_index, BusterA64SemanticVMValue const* values, BusterA64SemanticVMValue value);

BUSTER_F_DECL bool buster_a64_simd_semantic_equal_cstr(BusterA64SemanticString left, char8 const* right);
BUSTER_F_DECL bool buster_a64_simd_semantic_equal(BusterA64SemanticString left, BusterA64SemanticString right);
BUSTER_F_DECL bool buster_a64_simd_field_local(BusterA64SemanticForm form, u32 field_id, u32* local);
BUSTER_F_DECL bool buster_a64_simd_field_name(BusterA64SemanticForm form, u32 local, BusterA64SemanticString* name);
BUSTER_F_DECL bool buster_a64_simd_find_field(BusterA64SemanticForm form, BusterA64SemanticString name, u32* local);
BUSTER_F_DECL bool buster_a64_simd_assign_field(BusterA64SemanticForm form, u32 local, u32 value, u32* fields, u64* assigned);
BUSTER_F_DECL bool buster_a64_simd_member_offset(BusterA64SemanticString symbol, u32* offset);
BUSTER_F_DECL bool buster_a64_simd_parse_bits(BusterA64SemanticString text, u32* value, u32* mask, u8* width);
BUSTER_F_DECL bool buster_a64_simd_symbol_matches(BusterA64SemanticString text, u32 offset, BusterA64SemanticString symbol);
/* Only the complex binding presents rotation values; the direct binding
 * rejects them as unsigned operands. */
BUSTER_F_DECL bool buster_a64_simd_value_uint(BusterA64SemanticVMValue value, bool rotate_allowed, u64* result);

BUSTER_F_DECL u8 buster_a64_simd_scalar_width(BusterA64SIMDArrangement arrangement);
BUSTER_F_DECL u8 buster_a64_simd_lane_count(BusterA64SIMDArrangement arrangement);
BUSTER_F_DECL u8 buster_a64_simd_register_width(BusterA64SIMDArrangement arrangement);
BUSTER_F_DECL bool buster_a64_simd_arrangement_is_scalar(BusterA64SIMDArrangement arrangement);
BUSTER_F_DECL bool buster_a64_simd_arrangement_is_vector(BusterA64SIMDArrangement arrangement);
BUSTER_F_DECL bool buster_a64_simd_infer_scalar(BusterA64SemanticString symbol, BusterA64SIMDArrangement* arrangement);
/* `lane_suffix` also ends a presentation suffix at `[`, as in <Vm>.S[<index>].
 * The complex binding sets it; the direct binding keeps its narrower set. */
BUSTER_F_DECL bool buster_a64_simd_infer_assembly_arrangement(BusterA64SemanticForm form, u32 operand_index, BusterA64SemanticOperand operand, bool lane_suffix,
                                                              BusterA64SIMDArrangement* arrangement);

BUSTER_F_DECL bool buster_a64_simd_inverse_tables(BusterA64SemanticForm form, BusterA64SemanticOperand operand, BusterA64SemanticVMValue desired,
                                                  bool rotate_allowed, u32* fields, u64* assigned);
BUSTER_F_DECL bool buster_a64_simd_decode_arrangement(BusterA64SemanticForm form, BusterA64SemanticOperand operand, BusterA64SemanticVMFields const* fields,
                                                      BusterA64SemanticVMValue* result);

BUSTER_F_DECL BusterA64SIMDStatus buster_a64_simd_vm_status(BusterA64SemanticVMStatus status);
BUSTER_F_DECL BusterA64SIMDStatus buster_a64_simd_canonical_status(BusterAarch64CanonicalDecodeStatus status);
