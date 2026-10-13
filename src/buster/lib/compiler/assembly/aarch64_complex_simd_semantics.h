#pragma once

#include <buster/lib/compiler/assembly/aarch64_simd_semantics.h>

/*
 * Typed, bounded complex AdvSIMD bindings for the pinned Apple-M1 A64 corpus.
 * The row index is the generated complex-SIMD ordinal (0..389); it is not a
 * machine opcode and is stable only for the pinned source snapshot.  Values
 * use the semantic VM vocabulary, with `aux` carrying a SIMD arrangement code
 * for register/list/lane values.
 *
 * The eight TBL/TBX `<Vm>.<Ta>` rows are recorded upstream as `simd_lane`,
 * but their encoding has no numeric element-lane field: this API deliberately
 * presents those operands as SIMD_VECTOR index registers.
 */

#define BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS 8u

typedef struct BusterA64ComplexSIMDInstruction BusterA64ComplexSIMDInstruction;
struct BusterA64ComplexSIMDInstruction
{
    u32 row_index;
    u8 operand_count;
    u8 raw_fields_valid;
    u8 reserved[2];
    BusterA64SemanticVMValue operands[BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS];
    BusterA64SemanticVMFields raw_fields;
};

typedef struct BusterA64ComplexSIMDResult BusterA64ComplexSIMDResult;
struct BusterA64ComplexSIMDResult
{
    BusterA64SIMDStatus status;
    u32 row_index;
    u32 word;
    u32 operand_count;
    BusterA64SemanticVMValue operands[BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS];
    u8 raw_fields_valid;
    u8 reserved[3];
    BusterA64SemanticVMFields raw_fields;
};

BUSTER_F_DECL u32 buster_a64_complex_simd_schema_version(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_row_count(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_transform_row_count(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_executable_row_count(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_max_operands(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_arrangement_binding_count(void);
BUSTER_F_DECL bool buster_a64_complex_simd_row(u32 row_index, BusterA64SIMDRowInfo* result);
BUSTER_F_DECL bool buster_a64_complex_simd_find_source_digest(u64 source_digest, u32* row_index);
BUSTER_F_DECL bool buster_a64_complex_simd_arrangement_binding(u32 row_index, u32 operand_index, BusterA64SIMDArrangementBinding* result);

/* Form-directed typed operations. Outputs are transactional: on failure the
 * caller's word/result remains byte-for-byte unchanged. */
BUSTER_F_DECL BusterA64SIMDStatus buster_a64_complex_simd_encode(Target target, BusterA64ComplexSIMDInstruction const* instruction, u32* word);
BUSTER_F_DECL BusterA64SIMDStatus buster_a64_complex_simd_decode(Target target, u32 word, BusterA64ComplexSIMDResult* result);
BUSTER_F_DECL BusterA64SIMDStatus buster_a64_complex_simd_decode_row(Target target, u32 row_index, u32 word, BusterA64ComplexSIMDResult* result);
BUSTER_F_DECL bool buster_a64_complex_simd_validate(void);
