#pragma once

#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>

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
    BUSTER_A64_DIRECT_SIMD_REQUIREMENT_COUNT,
} BusterA64DirectSIMDRequirement;

typedef enum BusterA64DirectSIMDStatus
{
    BUSTER_A64_DIRECT_SIMD_STATUS_OK,
    BUSTER_A64_DIRECT_SIMD_STATUS_INVALID_ARGUMENT,
    BUSTER_A64_DIRECT_SIMD_STATUS_BOUNDS,
    BUSTER_A64_DIRECT_SIMD_STATUS_UNSUPPORTED,
    BUSTER_A64_DIRECT_SIMD_STATUS_RESERVED,
    BUSTER_A64_DIRECT_SIMD_STATUS_AMBIGUOUS,
    BUSTER_A64_DIRECT_SIMD_STATUS_RANGE,
    BUSTER_A64_DIRECT_SIMD_STATUS_TARGET_MISMATCH,
    BUSTER_A64_DIRECT_SIMD_STATUS_CAPACITY,
} BusterA64DirectSIMDStatus;

/* Arrangement codes are presentation values, not encoded Q/size bits. */
typedef enum BusterA64DirectSIMDArrangement
{
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_H,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_Q,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1B,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2B,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4B,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1H,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2H,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4H,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8H,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1S,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2S,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_1D,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2D,
    BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_COUNT,
} BusterA64DirectSIMDArrangement;

typedef struct BusterA64DirectSIMDRowInfo BusterA64DirectSIMDRowInfo;
struct BusterA64DirectSIMDRowInfo
{
    u32 row_index;
    u32 semantic_form_id;
    u64 source_digest;
    String8 id;
    String8 assembly;
    u8 operand_count;
    u8 executable;
    u8 transform_bearing;
    u8 reserved;
};

/* Generated syntax relation: +1 binds an uppercase vector register to the
 * following selector; -1 binds a scalar d/n/m register to the preceding one. */
typedef struct BusterA64DirectSIMDArrangementBinding BusterA64DirectSIMDArrangementBinding;
struct BusterA64DirectSIMDArrangementBinding
{
    u8 selector_index;
    s8 direction;
};

typedef struct BusterA64DirectSIMDInstruction BusterA64DirectSIMDInstruction;
struct BusterA64DirectSIMDInstruction
{
    u32 row_index;
    u8 operand_count;
    u8 reserved[3];
    BusterA64SemanticVMValue operands[BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS];
};

typedef struct BusterA64DirectSIMDResult BusterA64DirectSIMDResult;
struct BusterA64DirectSIMDResult
{
    BusterA64DirectSIMDStatus status;
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
BUSTER_F_DECL bool buster_a64_direct_simd_row(u32 row_index, BusterA64DirectSIMDRowInfo* result);
BUSTER_F_DECL bool buster_a64_direct_simd_find_source_digest(u64 source_digest, u32* row_index);
BUSTER_F_DECL bool buster_a64_direct_simd_arrangement_binding(u32 row_index, u32 operand_index, BusterA64DirectSIMDArrangementBinding* result);
BUSTER_F_DECL bool buster_a64_direct_simd_arrangement_from_string(String8 text, BusterA64DirectSIMDArrangement* result);
BUSTER_F_DECL String8 buster_a64_direct_simd_arrangement_string(BusterA64DirectSIMDArrangement arrangement);

/* Constructors reject out-of-range register/arrangement/lane values. */
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_direct_simd_value_arrangement(BusterA64DirectSIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_direct_simd_value_register(u32 number,
                                                                             BusterA64DirectSIMDArrangement arrangement,
                                                                             bool scalar);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_direct_simd_value_vector(u32 number,
                                                                            BusterA64DirectSIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_direct_simd_value_scalar(u32 number,
                                                                            BusterA64DirectSIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_direct_simd_value_list(u32 first, u32 count,
                                                                          BusterA64DirectSIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_direct_simd_value_lane(u32 number,
                                                                          BusterA64DirectSIMDArrangement arrangement,
                                                                          u32 lane);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_direct_simd_value_gpr(u32 number, u8 width, bool zr);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_direct_simd_value_gpr_width(u8 width);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_direct_simd_value_immediate(u64 value, u8 width);

/* Form-directed typed operations. Outputs are transactional: on failure the
 * caller's word/result remains byte-for-byte unchanged. */
BUSTER_F_DECL BusterA64DirectSIMDStatus buster_a64_direct_simd_encode(Target target,
                                                                       BusterA64DirectSIMDInstruction const* instruction,
                                                                       u32* word);
BUSTER_F_DECL BusterA64DirectSIMDStatus buster_a64_direct_simd_decode(Target target, u32 word,
                                                                       BusterA64DirectSIMDResult* result);
BUSTER_F_DECL BusterA64DirectSIMDStatus buster_a64_direct_simd_decode_row(Target target, u32 row_index, u32 word,
                                                                           BusterA64DirectSIMDResult* result);
BUSTER_F_DECL bool buster_a64_direct_simd_validate(void);
