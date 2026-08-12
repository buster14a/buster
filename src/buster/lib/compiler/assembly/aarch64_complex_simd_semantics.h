#pragma once

#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>

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

typedef enum BusterA64ComplexSIMDStatus
{
    BUSTER_A64_COMPLEX_SIMD_STATUS_OK,
    BUSTER_A64_COMPLEX_SIMD_STATUS_INVALID_ARGUMENT,
    BUSTER_A64_COMPLEX_SIMD_STATUS_BOUNDS,
    BUSTER_A64_COMPLEX_SIMD_STATUS_UNSUPPORTED,
    BUSTER_A64_COMPLEX_SIMD_STATUS_RESERVED,
    BUSTER_A64_COMPLEX_SIMD_STATUS_AMBIGUOUS,
    BUSTER_A64_COMPLEX_SIMD_STATUS_RANGE,
    BUSTER_A64_COMPLEX_SIMD_STATUS_TARGET_MISMATCH,
    BUSTER_A64_COMPLEX_SIMD_STATUS_CAPACITY,
} BusterA64ComplexSIMDStatus;

/* Arrangement codes are presentation values, not encoded Q/size bits. */
typedef enum BusterA64ComplexSIMDArrangement
{
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_INVALID,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_B,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_H,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_S,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_D,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_Q,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_1B,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_2B,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_4B,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_8B,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_16B,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_1H,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_2H,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_4H,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_8H,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_1S,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_2S,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_4S,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_1D,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_2D,
    BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_COUNT,
} BusterA64ComplexSIMDArrangement;

typedef struct BusterA64ComplexSIMDRowInfo BusterA64ComplexSIMDRowInfo;
struct BusterA64ComplexSIMDRowInfo
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
typedef struct BusterA64ComplexSIMDArrangementBinding BusterA64ComplexSIMDArrangementBinding;
struct BusterA64ComplexSIMDArrangementBinding
{
    u8 selector_index;
    s8 direction;
};

typedef struct BusterA64ComplexSIMDInstruction BusterA64ComplexSIMDInstruction;
struct BusterA64ComplexSIMDInstruction
{
    u32 row_index;
    u8 operand_count;
    u8 reserved[3];
    BusterA64SemanticVMValue operands[BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS];
};

typedef struct BusterA64ComplexSIMDResult BusterA64ComplexSIMDResult;
struct BusterA64ComplexSIMDResult
{
    BusterA64ComplexSIMDStatus status;
    u32 row_index;
    u32 word;
    u32 operand_count;
    BusterA64SemanticVMValue operands[BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS];
};

BUSTER_F_DECL u32 buster_a64_complex_simd_schema_version(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_row_count(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_transform_row_count(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_executable_row_count(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_max_operands(void);
BUSTER_F_DECL u32 buster_a64_complex_simd_arrangement_binding_count(void);
BUSTER_F_DECL bool buster_a64_complex_simd_row(u32 row_index, BusterA64ComplexSIMDRowInfo* result);
BUSTER_F_DECL bool buster_a64_complex_simd_find_source_digest(u64 source_digest, u32* row_index);
BUSTER_F_DECL bool buster_a64_complex_simd_arrangement_binding(u32 row_index, u32 operand_index, BusterA64ComplexSIMDArrangementBinding* result);
BUSTER_F_DECL bool buster_a64_complex_simd_arrangement_from_string(String8 text, BusterA64ComplexSIMDArrangement* result);
BUSTER_F_DECL String8 buster_a64_complex_simd_arrangement_string(BusterA64ComplexSIMDArrangement arrangement);

/* Constructors reject out-of-range register/arrangement/lane values. */
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_complex_simd_value_arrangement(BusterA64ComplexSIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_complex_simd_value_register(u32 number,
                                                                             BusterA64ComplexSIMDArrangement arrangement,
                                                                             bool scalar);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_complex_simd_value_vector(u32 number,
                                                                            BusterA64ComplexSIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_complex_simd_value_scalar(u32 number,
                                                                            BusterA64ComplexSIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_complex_simd_value_list(u32 first, u32 count,
                                                                          BusterA64ComplexSIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_complex_simd_value_lane(u32 number,
                                                                          BusterA64ComplexSIMDArrangement arrangement,
                                                                          u32 lane);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_complex_simd_value_gpr(u32 number, u8 width, bool zr);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_complex_simd_value_gpr_width(u8 width);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_complex_simd_value_immediate(u64 value, u8 width);

/* Form-directed typed operations. Outputs are transactional: on failure the
 * caller's word/result remains byte-for-byte unchanged. */
BUSTER_F_DECL BusterA64ComplexSIMDStatus buster_a64_complex_simd_encode(Target target,
                                                                       BusterA64ComplexSIMDInstruction const* instruction,
                                                                       u32* word);
BUSTER_F_DECL BusterA64ComplexSIMDStatus buster_a64_complex_simd_decode(Target target, u32 word,
                                                                       BusterA64ComplexSIMDResult* result);
BUSTER_F_DECL BusterA64ComplexSIMDStatus buster_a64_complex_simd_decode_row(Target target, u32 row_index, u32 word,
                                                                           BusterA64ComplexSIMDResult* result);
BUSTER_F_DECL bool buster_a64_complex_simd_validate(void);
