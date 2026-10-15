#pragma once

/*
 * Shared typed vocabulary of the AArch64 AdvSIMD semantic bindings.
 *
 * Ownership: the arrangement, status, row-information and arrangement-binding
 * types used by both the direct (aarch64_direct_simd_semantics) and complex
 * (aarch64_complex_simd_semantics) bindings, and the semantic VM value
 * constructors that carry an arrangement.  Each binding keeps its own rows,
 * encoder and decoder; this header only names what they have in common.
 *
 * Map:
 * - BusterA64SIMDStatus, BusterA64SIMDArrangement: shared result and
 *   presentation enums.
 * - BusterA64SIMDRowInfo, BusterA64SIMDArrangementBinding: public row and
 *   selector-relation views returned by each binding.
 * - buster_a64_simd_arrangement_from_string / _string: presentation text.
 * - buster_a64_simd_value_*: typed value constructors.
 */

#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>

#define BUSTER_A64_SIMD_MAX_OPERANDS 8u

typedef enum BusterA64SIMDStatus
{
    BUSTER_A64_SIMD_STATUS_OK,
    BUSTER_A64_SIMD_STATUS_INVALID_ARGUMENT,
    BUSTER_A64_SIMD_STATUS_BOUNDS,
    BUSTER_A64_SIMD_STATUS_UNSUPPORTED,
    BUSTER_A64_SIMD_STATUS_RESERVED,
    BUSTER_A64_SIMD_STATUS_AMBIGUOUS,
    BUSTER_A64_SIMD_STATUS_RANGE,
    BUSTER_A64_SIMD_STATUS_TARGET_MISMATCH,
    BUSTER_A64_SIMD_STATUS_CAPACITY,
} BusterA64SIMDStatus;

/* Arrangement codes are presentation values, not encoded Q/size bits. */
typedef enum BusterA64SIMDArrangement
{
    BUSTER_A64_SIMD_ARRANGEMENT_INVALID,
    BUSTER_A64_SIMD_ARRANGEMENT_B,
    BUSTER_A64_SIMD_ARRANGEMENT_H,
    BUSTER_A64_SIMD_ARRANGEMENT_S,
    BUSTER_A64_SIMD_ARRANGEMENT_D,
    BUSTER_A64_SIMD_ARRANGEMENT_Q,
    BUSTER_A64_SIMD_ARRANGEMENT_1B,
    BUSTER_A64_SIMD_ARRANGEMENT_2B,
    BUSTER_A64_SIMD_ARRANGEMENT_4B,
    BUSTER_A64_SIMD_ARRANGEMENT_8B,
    BUSTER_A64_SIMD_ARRANGEMENT_16B,
    BUSTER_A64_SIMD_ARRANGEMENT_1H,
    BUSTER_A64_SIMD_ARRANGEMENT_2H,
    BUSTER_A64_SIMD_ARRANGEMENT_4H,
    BUSTER_A64_SIMD_ARRANGEMENT_8H,
    BUSTER_A64_SIMD_ARRANGEMENT_1S,
    BUSTER_A64_SIMD_ARRANGEMENT_2S,
    BUSTER_A64_SIMD_ARRANGEMENT_4S,
    BUSTER_A64_SIMD_ARRANGEMENT_1D,
    BUSTER_A64_SIMD_ARRANGEMENT_2D,
    BUSTER_A64_SIMD_ARRANGEMENT_COUNT,
} BusterA64SIMDArrangement;

typedef struct BusterA64SIMDRowInfo BusterA64SIMDRowInfo;
struct BusterA64SIMDRowInfo
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
typedef struct BusterA64SIMDArrangementBinding BusterA64SIMDArrangementBinding;
struct BusterA64SIMDArrangementBinding
{
    u8 selector_index;
    s8 direction;
};

BUSTER_F_DECL bool buster_a64_simd_arrangement_from_string(String8 text, BusterA64SIMDArrangement* result);
BUSTER_F_DECL String8 buster_a64_simd_arrangement_string(BusterA64SIMDArrangement arrangement);

/* Constructors reject out-of-range register/arrangement/lane values. */
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_simd_value_arrangement(BusterA64SIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_simd_value_register(u32 number, BusterA64SIMDArrangement arrangement, bool scalar);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_simd_value_vector(u32 number, BusterA64SIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_simd_value_scalar(u32 number, BusterA64SIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_simd_value_list(u32 first, u32 count, BusterA64SIMDArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_simd_value_lane(u32 number, BusterA64SIMDArrangement arrangement, u32 lane);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_simd_value_gpr(u32 number, u8 width, bool zr);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_simd_value_gpr_width(u8 width);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_simd_value_immediate(u64 value, u8 width);
