#pragma once

#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>

/* Form-directed typed bindings for the canonical Apple-M1 A64 memory
 * closure.  Row indices are ordinals in the pinned generated denominator;
 * they are not opcodes and must not be used as machine-code identifiers. */

#define BUSTER_A64_MEMORY_MAX_OPERANDS 16u

typedef enum BusterA64MemoryStatus
{
    BUSTER_A64_MEMORY_STATUS_OK,
    BUSTER_A64_MEMORY_STATUS_INVALID_ARGUMENT,
    BUSTER_A64_MEMORY_STATUS_BOUNDS,
    BUSTER_A64_MEMORY_STATUS_UNSUPPORTED,
    BUSTER_A64_MEMORY_STATUS_RESERVED,
    BUSTER_A64_MEMORY_STATUS_AMBIGUOUS,
    BUSTER_A64_MEMORY_STATUS_RANGE,
    BUSTER_A64_MEMORY_STATUS_TARGET_MISMATCH,
    BUSTER_A64_MEMORY_STATUS_CAPACITY,
} BusterA64MemoryStatus;

typedef enum BusterA64MemoryFamily
{
    BUSTER_A64_MEMORY_FAMILY_SCALAR,
    BUSTER_A64_MEMORY_FAMILY_PAIR,
    BUSTER_A64_MEMORY_FAMILY_EXCLUSIVE,
    BUSTER_A64_MEMORY_FAMILY_ORDERED,
    BUSTER_A64_MEMORY_FAMILY_ATOMIC,
    BUSTER_A64_MEMORY_FAMILY_SIMD_STRUCTURE,
    BUSTER_A64_MEMORY_FAMILY_COUNT,
} BusterA64MemoryFamily;

typedef enum BusterA64MemoryAddressMode
{
    BUSTER_A64_MEMORY_ADDRESS_BASE,
    BUSTER_A64_MEMORY_ADDRESS_SIGNED_OFFSET,
    BUSTER_A64_MEMORY_ADDRESS_SCALED_OFFSET,
    BUSTER_A64_MEMORY_ADDRESS_REGISTER_OFFSET,
    BUSTER_A64_MEMORY_ADDRESS_PRE_INDEX,
    BUSTER_A64_MEMORY_ADDRESS_POST_INDEX,
    BUSTER_A64_MEMORY_ADDRESS_SIMD_LANE,
    BUSTER_A64_MEMORY_ADDRESS_COUNT,
} BusterA64MemoryAddressMode;

typedef enum BusterA64MemoryOverlapPolicy
{
    BUSTER_A64_MEMORY_OVERLAP_NONE,
    BUSTER_A64_MEMORY_OVERLAP_BASE_DISJOINT,
    BUSTER_A64_MEMORY_OVERLAP_ADJACENT_OR_LIST,
    BUSTER_A64_MEMORY_OVERLAP_PAIR_DISJOINT,
    BUSTER_A64_MEMORY_OVERLAP_COUNT,
} BusterA64MemoryOverlapPolicy;

/* Presentation arrangement codes.  They intentionally do not expose the
 * source encoding's Q/size bit assignments. */
typedef enum BusterA64MemoryArrangement
{
    BUSTER_A64_MEMORY_ARRANGEMENT_INVALID,
    BUSTER_A64_MEMORY_ARRANGEMENT_B,
    BUSTER_A64_MEMORY_ARRANGEMENT_H,
    BUSTER_A64_MEMORY_ARRANGEMENT_S,
    BUSTER_A64_MEMORY_ARRANGEMENT_D,
    BUSTER_A64_MEMORY_ARRANGEMENT_Q,
    BUSTER_A64_MEMORY_ARRANGEMENT_1B,
    BUSTER_A64_MEMORY_ARRANGEMENT_2B,
    BUSTER_A64_MEMORY_ARRANGEMENT_4B,
    BUSTER_A64_MEMORY_ARRANGEMENT_8B,
    BUSTER_A64_MEMORY_ARRANGEMENT_16B,
    BUSTER_A64_MEMORY_ARRANGEMENT_1H,
    BUSTER_A64_MEMORY_ARRANGEMENT_2H,
    BUSTER_A64_MEMORY_ARRANGEMENT_4H,
    BUSTER_A64_MEMORY_ARRANGEMENT_8H,
    BUSTER_A64_MEMORY_ARRANGEMENT_1S,
    BUSTER_A64_MEMORY_ARRANGEMENT_2S,
    BUSTER_A64_MEMORY_ARRANGEMENT_4S,
    BUSTER_A64_MEMORY_ARRANGEMENT_1D,
    BUSTER_A64_MEMORY_ARRANGEMENT_2D,
    BUSTER_A64_MEMORY_ARRANGEMENT_COUNT,
} BusterA64MemoryArrangement;

typedef enum BusterA64MemoryExtend
{
    BUSTER_A64_MEMORY_EXTEND_INVALID,
    BUSTER_A64_MEMORY_EXTEND_UXTB,
    BUSTER_A64_MEMORY_EXTEND_UXTH,
    BUSTER_A64_MEMORY_EXTEND_UXTW,
    BUSTER_A64_MEMORY_EXTEND_UXTX,
    BUSTER_A64_MEMORY_EXTEND_SXTB,
    BUSTER_A64_MEMORY_EXTEND_SXTH,
    BUSTER_A64_MEMORY_EXTEND_SXTW,
    BUSTER_A64_MEMORY_EXTEND_SXTX,
    BUSTER_A64_MEMORY_EXTEND_LSL,
    BUSTER_A64_MEMORY_EXTEND_COUNT,
} BusterA64MemoryExtend;

typedef struct BusterA64MemoryRowInfo BusterA64MemoryRowInfo;
struct BusterA64MemoryRowInfo
{
    u32 row_index;
    u32 semantic_form_id;
    u64 source_digest;
    BusterA64SemanticString name;
    BusterA64SemanticString assembly;
    u8 operand_count;
    u8 family;
    u8 address_mode;
    u8 overlap_policy;
    u8 candidate;
    u8 transform_bearing;
    u8 reserved[2];
};

typedef struct BusterA64MemoryInstruction BusterA64MemoryInstruction;
struct BusterA64MemoryInstruction
{
    u32 row_index;
    u8 operand_count;
    u8 reserved[3];
    BusterA64SemanticVMValue operands[BUSTER_A64_MEMORY_MAX_OPERANDS];
};

typedef struct BusterA64MemoryResult BusterA64MemoryResult;
struct BusterA64MemoryResult
{
    BusterA64MemoryStatus status;
    u32 row_index;
    u32 word;
    u32 operand_count;
    BusterA64SemanticVMValue operands[BUSTER_A64_MEMORY_MAX_OPERANDS];
};

typedef struct BusterA64MemoryArrangementBinding BusterA64MemoryArrangementBinding;
struct BusterA64MemoryArrangementBinding
{
    u8 selector_index;
    s8 direction;
};
BUSTER_CT_CHECK(sizeof(BusterA64MemoryArrangementBinding) == 2);

BUSTER_F_DECL u32 buster_a64_memory_schema_version(void);
BUSTER_F_DECL u32 buster_a64_memory_row_count(void);
BUSTER_F_DECL u32 buster_a64_memory_transform_row_count(void);
BUSTER_F_DECL u32 buster_a64_memory_feature_gated_row_count(void);
BUSTER_F_DECL u32 buster_a64_memory_max_operands(void);
BUSTER_F_DECL u32 buster_a64_memory_arrangement_binding_count(void);
BUSTER_F_DECL bool buster_a64_memory_row(u32 row_index, BusterA64MemoryRowInfo* result);
BUSTER_F_DECL bool buster_a64_memory_find_source_digest(u64 source_digest, u32* row_index);
BUSTER_F_DECL bool buster_a64_memory_arrangement_binding(u32 row_index, u32 operand_index,
                                                         BusterA64MemoryArrangementBinding* result);

BUSTER_F_DECL bool buster_a64_memory_arrangement_from_string(String8 text, BusterA64MemoryArrangement* result);
BUSTER_F_DECL String8 buster_a64_memory_arrangement_string(BusterA64MemoryArrangement arrangement);

BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_arrangement(BusterA64MemoryArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_register(u32 number, BusterA64MemoryArrangement arrangement,
                                                                        bool scalar);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_vector(u32 number, BusterA64MemoryArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_scalar(u32 number, BusterA64MemoryArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_list(u32 first, u32 count, BusterA64MemoryArrangement arrangement);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_lane(u32 number, BusterA64MemoryArrangement arrangement,
                                                                     u32 lane);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_gpr(u32 number, u8 width, bool sp, bool zr);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_immediate(s64 value, u8 width, bool is_signed);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_extend(BusterA64MemoryExtend extend);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_memory_value_prefetch(u32 operation);

/* Encode/decode are transactional: output memory is unchanged on failure. */
BUSTER_F_DECL BusterA64MemoryStatus buster_a64_memory_encode(Target target,
                                                             BusterA64MemoryInstruction const* instruction, u32* word);
BUSTER_F_DECL BusterA64MemoryStatus buster_a64_memory_decode(Target target, u32 word, BusterA64MemoryResult* result);
BUSTER_F_DECL BusterA64MemoryStatus buster_a64_memory_decode_row(Target target, u32 row_index, u32 word,
                                                                 BusterA64MemoryResult* result);
BUSTER_F_DECL bool buster_a64_memory_validate(void);
