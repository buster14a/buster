#pragma once

#include <buster/lib/base.h>
#include <buster/lib/compiler/assembly/aarch64_semantics.h>

/*
 * The semantic VM is a typed, bounded transform engine.  It deliberately
 * does not pretend that a field binding is an instruction implementation:
 * row coverage and semantic-executable coverage are separate counters in the
 * generated manifest.
 */

typedef enum BusterA64SemanticVMValueKind
{
    BUSTER_A64_SEMANTIC_VM_VALUE_INVALID,
    BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER,
    BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER,
    BUSTER_A64_SEMANTIC_VM_VALUE_BITS,
    BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION,
    BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER,
    BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER,
    BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR,
    BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR,
    BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT,
    BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST,
    BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE,
    BUSTER_A64_SEMANTIC_VM_VALUE_INTEGER_IMMEDIATE,
    BUSTER_A64_SEMANTIC_VM_VALUE_FP_IMMEDIATE,
    BUSTER_A64_SEMANTIC_VM_VALUE_MEMORY,
    BUSTER_A64_SEMANTIC_VM_VALUE_CONDITION,
    BUSTER_A64_SEMANTIC_VM_VALUE_NZCV,
    BUSTER_A64_SEMANTIC_VM_VALUE_SHIFT,
    BUSTER_A64_SEMANTIC_VM_VALUE_EXTEND,
    BUSTER_A64_SEMANTIC_VM_VALUE_ROTATE,
    BUSTER_A64_SEMANTIC_VM_VALUE_SYSTEM_REGISTER,
    BUSTER_A64_SEMANTIC_VM_VALUE_SYSTEM_OPERATION,
    BUSTER_A64_SEMANTIC_VM_VALUE_BARRIER_OPTION,
    BUSTER_A64_SEMANTIC_VM_VALUE_PREFETCH_OPERATION,
    BUSTER_A64_SEMANTIC_VM_VALUE_LABEL_FIXUP,
    BUSTER_A64_SEMANTIC_VM_VALUE_MODIFIER,
    BUSTER_A64_SEMANTIC_VM_VALUE_RESERVED,
    BUSTER_A64_SEMANTIC_VM_VALUE_COUNT,
} BusterA64SemanticVMValueKind;

enum
{
    BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP = 1u << 0,
    BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR = 1u << 1,
    BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_WIDE = 1u << 2,
    BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_WILDCARD = 1u << 3,
    BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_PAGE = 1u << 4,
};

typedef struct BusterA64SemanticVMValue BusterA64SemanticVMValue;
struct BusterA64SemanticVMValue
{
    u8 kind;
    u8 width;
    u16 flags;
    u32 aux;
    u32 aux2;
    u64 payload;
    u64 mask;
    BusterA64SemanticString text;
};
BUSTER_CT_CHECK(sizeof(BusterA64SemanticVMValue) == 40);

typedef enum BusterA64SemanticVMStatus
{
    BUSTER_A64_SEMANTIC_VM_STATUS_OK,
    BUSTER_A64_SEMANTIC_VM_STATUS_INVALID_ARGUMENT,
    BUSTER_A64_SEMANTIC_VM_STATUS_BOUNDS,
    BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED,
    BUSTER_A64_SEMANTIC_VM_STATUS_RESERVED,
    BUSTER_A64_SEMANTIC_VM_STATUS_AMBIGUOUS,
    BUSTER_A64_SEMANTIC_VM_STATUS_RANGE,
    BUSTER_A64_SEMANTIC_VM_STATUS_TARGET_MISMATCH,
} BusterA64SemanticVMStatus;

typedef enum BusterA64SemanticVMOp
{
    BUSTER_A64_SEMANTIC_VM_OP_INVALID,
    BUSTER_A64_SEMANTIC_VM_OP_FIELD,
    BUSTER_A64_SEMANTIC_VM_OP_EXTRACT,
    BUSTER_A64_SEMANTIC_VM_OP_CONCAT,
    BUSTER_A64_SEMANTIC_VM_OP_UINT_EXTEND,
    BUSTER_A64_SEMANTIC_VM_OP_SIGNED_EXTEND,
    BUSTER_A64_SEMANTIC_VM_OP_ADD_CONST,
    BUSTER_A64_SEMANTIC_VM_OP_SUB_CONST,
    BUSTER_A64_SEMANTIC_VM_OP_SUB_FROM_CONST,
    BUSTER_A64_SEMANTIC_VM_OP_SCALE_MUL,
    BUSTER_A64_SEMANTIC_VM_OP_SCALE_DIV,
    BUSTER_A64_SEMANTIC_VM_OP_FIXED_LITERAL,
    BUSTER_A64_SEMANTIC_VM_OP_DEFAULT,
    BUSTER_A64_SEMANTIC_VM_OP_OPTIONAL,
    BUSTER_A64_SEMANTIC_VM_OP_TABLE_EXACT_WILDCARD,
    BUSTER_A64_SEMANTIC_VM_OP_RESERVED_REJECT,
    BUSTER_A64_SEMANTIC_VM_OP_PC_RELATIVE,
    BUSTER_A64_SEMANTIC_VM_OP_PAGE_RELATIVE,
    BUSTER_A64_SEMANTIC_VM_OP_CONDITION_INVERT,
    BUSTER_A64_SEMANTIC_VM_OP_REGISTER_ADD_MOD32,
    BUSTER_A64_SEMANTIC_VM_OP_BITWISE_NOT,
    BUSTER_A64_SEMANTIC_VM_OP_MOVN,
    BUSTER_A64_SEMANTIC_VM_OP_LOGICAL_IMMEDIATE,
    BUSTER_A64_SEMANTIC_VM_OP_FP_IMMEDIATE,
    BUSTER_A64_SEMANTIC_VM_OP_ADVSIMD_IMMEDIATE,
    BUSTER_A64_SEMANTIC_VM_OP_SYSOP_LOOKUP,
    BUSTER_A64_SEMANTIC_VM_OP_ALIAS_MAP,
    BUSTER_A64_SEMANTIC_VM_OP_ALIAS_INJECT,
    BUSTER_A64_SEMANTIC_VM_OP_ALIAS_CONDITION,
    BUSTER_A64_SEMANTIC_VM_OP_SLICE,
    BUSTER_A64_SEMANTIC_VM_OP_INTEGER_DECODE,
    BUSTER_A64_SEMANTIC_VM_OP_SHARED_DECODE,
    BUSTER_A64_SEMANTIC_VM_OP_COUNT,
} BusterA64SemanticVMOp;

typedef struct BusterA64SemanticVMFields BusterA64SemanticVMFields;
struct BusterA64SemanticVMFields
{
    u32 count;
    u32 values[64];
};
BUSTER_CT_CHECK(sizeof(BusterA64SemanticVMFields) == 260);

typedef struct BusterA64SemanticVMInstruction BusterA64SemanticVMInstruction;
struct BusterA64SemanticVMInstruction
{
    u8 op;
    u8 input_count;
    u8 width;
    u8 flags;
    u32 low;
    u32 high;
    u64 constant;
    u64 auxiliary;
};

typedef struct BusterA64SemanticVMResult BusterA64SemanticVMResult;
struct BusterA64SemanticVMResult
{
    BusterA64SemanticVMStatus status;
    u32 form_id;
    u32 word;
    u32 field_count;
    BusterA64SemanticVMFields fields;
};

typedef struct BusterA64SemanticVMAliasInfo BusterA64SemanticVMAliasInfo;
struct BusterA64SemanticVMAliasInfo
{
    u32 alias_form_id;
    u32 target_form_id;
    u16 injected_field_count;
    u16 same_field_count;
    u16 preference_count;
    u8 condition_supported;
    u8 reserved;
    u32 condition_digest;
};

BUSTER_F_DECL u32 buster_a64_semantic_vm_schema_version(void);
BUSTER_F_DECL u32 buster_a64_semantic_vm_form_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_vm_transform_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_vm_raw_codec_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_vm_transform_row_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_vm_semantic_executable_count(void);
BUSTER_F_DECL u8 buster_a64_semantic_vm_row_coverage(u32 form_id);
BUSTER_F_DECL u8 buster_a64_semantic_vm_row_gap_reason(u32 form_id);

BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_semantic_vm_value_invalid(void);
/* Integer constructors are fail-closed: an operand whose value is not
 * representable at the requested width returns VALUE_INVALID rather than
 * silently truncating or wrapping the payload. */
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_semantic_vm_value_unsigned(u64 value, u8 width);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_semantic_vm_value_signed(s64 value, u8 width);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_semantic_vm_value_bits(u64 value, u64 mask, u8 width);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_semantic_vm_value_gpr(u32 number, u8 width, bool sp, bool zr);
BUSTER_F_DECL BusterA64SemanticVMValue buster_a64_semantic_vm_value_condition(u32 condition);

BUSTER_F_DECL BusterA64SemanticVMStatus buster_a64_semantic_vm_apply(BusterA64SemanticVMInstruction instruction,
                                                                      BusterA64SemanticVMValue const* inputs,
                                                                      u32 input_count, u64 pc, u64 place,
                                                                      BusterA64SemanticVMValue* output);

BUSTER_F_DECL BusterA64SemanticVMStatus buster_a64_semantic_vm_decode_fields(u32 form_id, u32 word,
                                                                              BusterA64SemanticVMResult* result);
BUSTER_F_DECL BusterA64SemanticVMStatus buster_a64_semantic_vm_encode_fields(u32 form_id,
                                                                              BusterA64SemanticVMFields const* fields,
                                                                              u32* word);
BUSTER_F_DECL BusterA64SemanticVMStatus buster_a64_semantic_vm_eval_transform(u32 form_id, u32 transform_id,
                                                                               BusterA64SemanticVMFields const* fields,
                                                                               BusterA64SemanticVMValue* output);
BUSTER_F_DECL BusterA64SemanticVMStatus buster_a64_semantic_vm_alias(u32 form_id,
                                                                      BusterA64SemanticVMAliasInfo* result);
BUSTER_F_DECL bool buster_a64_semantic_vm_validate(void);
