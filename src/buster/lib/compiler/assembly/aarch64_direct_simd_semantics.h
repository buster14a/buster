#pragma once

#include <buster/lib/compiler/assembly/aarch64_simd_semantics.h>

/*
 * Typed, bounded direct AdvSIMD bindings for the pinned Apple-M1 A64 corpus.
 * The row index is the generated direct-SIMD ordinal (0..389); it is not a
 * machine opcode and is stable only for the pinned source snapshot.  Values
 * use the semantic VM vocabulary, with `aux` carrying a SIMD arrangement code
 * for register/list/lane values.
 *
 * The eight TBL/TBX `<Vm>.<Ta>` rows are recorded upstream as `simd_lane`,
 * but their encoding has no numeric element-lane field: this API deliberately
 * presents those operands as SIMD_VECTOR index registers.
 */

#define BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS 8u

/* Compact, interned target requirements carried by public direct-SIMD
 * spellings.  Keeping the mask in one table avoids putting a 32-byte
 * TargetCpuFeatures value in every spelling row while still allowing a
 * conjunction such as AdvSIMD && FP16 to be checked as a subset. */
typedef enum BusterA64DirectSIMDRequirement
{
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NONE,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_AES,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_SHA2,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_SHA3,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FULLFP16,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FP,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_FULLFP16,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FP16FML,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_FPTOINT,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_DOTPROD,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_NEON_RDM,
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_COUNT,
} BusterA64DirectSIMDRequirement;

/* Spelling-owned fixed semantic fields.  The encoder resolves the selected
 * kind against the semantic form's field names, so generated field ordering
 * is not part of the parser/encoder contract. */
typedef enum BusterA64DirectSIMDFixedField
{
    BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_NONE,
    BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_SIZE,
    BUSTER_A64_DIRECT_SIMD_FIXED_FIELD_COUNT,
} BusterA64DirectSIMDFixedField;

typedef struct BusterA64DirectSIMDInstruction BusterA64DirectSIMDInstruction;
struct BusterA64DirectSIMDInstruction
{
    u32 row_index;
    u8 operand_count;
    /* A spelling-owned fixed semantic field override.  NONE is used by
     * decoded instructions, which therefore retain every canonical raw field
     * value. */
    u8 fixed_field_kind;
    u8 fixed_field_value;
    u8 reserved[1];
    BusterA64SemanticVMValue operands[BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS];
};

typedef struct BusterA64DirectSIMDResult BusterA64DirectSIMDResult;
struct BusterA64DirectSIMDResult
{
    BusterA64SIMDStatus status;
    u32 row_index;
    u32 word;
    u32 operand_count;
    BusterA64SemanticVMValue operands[BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS];
};

BUSTER_F_DECL u32 buster_a64_direct_simd_schema_version(void);
BUSTER_F_DECL u32 buster_a64_direct_simd_row_count(void);
BUSTER_F_DECL u32 buster_a64_direct_simd_transform_row_count(void);
BUSTER_F_DECL u32 buster_a64_direct_simd_executable_row_count(void);
BUSTER_F_DECL u32 buster_a64_direct_simd_max_operands(void);
BUSTER_F_DECL u32 buster_a64_direct_simd_arrangement_binding_count(void);
BUSTER_F_DECL bool buster_a64_direct_simd_requirement_features(u8 requirement, TargetCpuFeatures* result);
BUSTER_F_DECL bool buster_a64_direct_simd_requirement_supported(Target target, u8 requirement);
BUSTER_F_DECL bool buster_a64_direct_simd_row(u32 row_index, BusterA64SIMDRowInfo* result);
BUSTER_F_DECL bool buster_a64_direct_simd_find_source_digest(u64 source_digest, u32* row_index);
BUSTER_F_DECL bool buster_a64_direct_simd_arrangement_binding(u32 row_index, u32 operand_index, BusterA64SIMDArrangementBinding* result);

/* Form-directed typed operations. Outputs are transactional: on failure the
 * caller's word/result remains byte-for-byte unchanged. */
BUSTER_F_DECL BusterA64SIMDStatus buster_a64_direct_simd_encode(Target target, BusterA64DirectSIMDInstruction const* instruction, u32* word);
BUSTER_F_DECL BusterA64SIMDStatus buster_a64_direct_simd_decode(Target target, u32 word, BusterA64DirectSIMDResult* result);
BUSTER_F_DECL BusterA64SIMDStatus buster_a64_direct_simd_decode_row(Target target, u32 row_index, u32 word, BusterA64DirectSIMDResult* result);
BUSTER_F_DECL bool buster_a64_direct_simd_validate(void);
