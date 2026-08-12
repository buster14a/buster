#pragma once

#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>

/*
 * Bounded A64 alias projection for the pinned Apple-M1 semantic snapshot.
 * Alias rows are selected by generated form/target links; this API never
 * dispatches through callbacks or mnemonic-specific code.  Encode/decode
 * outputs are transactional: callers' output storage is untouched on error.
 */

#define BUSTER_A64_ALIAS_MAX_OPERANDS 8u
#define BUSTER_A64_ALIAS_MAX_FIELDS 16u

typedef enum BusterA64AliasStatus
{
    BUSTER_A64_ALIAS_STATUS_OK,
    BUSTER_A64_ALIAS_STATUS_INVALID_ARGUMENT,
    BUSTER_A64_ALIAS_STATUS_BOUNDS,
    BUSTER_A64_ALIAS_STATUS_UNSUPPORTED,
    BUSTER_A64_ALIAS_STATUS_RESERVED,
    BUSTER_A64_ALIAS_STATUS_AMBIGUOUS,
    BUSTER_A64_ALIAS_STATUS_RANGE,
    BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH,
    BUSTER_A64_ALIAS_STATUS_CONDITION_FALSE,
    BUSTER_A64_ALIAS_STATUS_CAPACITY,
} BusterA64AliasStatus;

typedef enum BusterA64AliasTargetOwner
{
    BUSTER_A64_ALIAS_TARGET_MEMORY,
    BUSTER_A64_ALIAS_TARGET_SCALAR_INTEGER,
    BUSTER_A64_ALIAS_TARGET_DIRECT_GPR,
    BUSTER_A64_ALIAS_TARGET_GENERAL_NONMEMORY,
    BUSTER_A64_ALIAS_TARGET_SYSTEM,
    BUSTER_A64_ALIAS_TARGET_COMPLEX_SIMD_FP,
    BUSTER_A64_ALIAS_TARGET_DIRECT_SIMD,
    BUSTER_A64_ALIAS_TARGET_OWNER_INVALID = 255,
} BusterA64AliasTargetOwner;

typedef struct BusterA64AliasRowInfo BusterA64AliasRowInfo;
struct BusterA64AliasRowInfo
{
    u32 alias_ordinal;
    u32 alias_form_id;
    u32 target_form_id;
    u64 alias_source_digest;
    u64 target_source_digest;
    u32 fixed_mask;
    u32 fixed_value;
    s32 preference_rank;
    u8 target_owner;
    u8 operand_count;
    u8 field_count;
    u8 condition_token_count;
    u8 preference_condition_token_count;
    String8 alias_id;
    String8 target_id;
};

typedef struct BusterA64AliasInstruction BusterA64AliasInstruction;
struct BusterA64AliasInstruction
{
    u32 alias_form_id;
    u8 operand_count;
    u8 reserved[3];
    BusterA64SemanticVMValue operands[BUSTER_A64_ALIAS_MAX_OPERANDS];
};

typedef struct BusterA64AliasResult BusterA64AliasResult;
struct BusterA64AliasResult
{
    BusterA64AliasStatus status;
    u32 alias_form_id;
    u32 target_form_id;
    u32 word;
    u8 operand_count;
    u8 reserved[3];
    BusterA64SemanticVMValue operands[BUSTER_A64_ALIAS_MAX_OPERANDS];
};

BUSTER_F_DECL u32 buster_a64_alias_projection_schema_version(void);
BUSTER_F_DECL u32 buster_a64_alias_count(void);
BUSTER_F_DECL u32 buster_a64_alias_canonical_count(void);
BUSTER_F_DECL String8 buster_a64_alias_denominator_sha256(void);
BUSTER_F_DECL bool buster_a64_alias_row(u32 alias_ordinal, BusterA64AliasRowInfo* result);
BUSTER_F_DECL bool buster_a64_alias_row_by_form(u32 alias_form_id, BusterA64AliasRowInfo* result);
BUSTER_F_DECL bool buster_a64_alias_find(String8 id, u32 ordinal, u32* alias_form_id);
BUSTER_F_DECL bool buster_a64_alias_condition_supported(u32 alias_form_id);
BUSTER_F_DECL bool buster_a64_alias_preference_supported(u32 alias_form_id);

/* Operands are projected through their semantic field references.  The
 * generic path accepts register/immediate values whose operand has one field;
 * transform-bearing or multi-field operands fail closed as UNSUPPORTED until
 * their owner module supplies an inverse transform. */
BUSTER_F_DECL BusterA64AliasStatus buster_a64_alias_encode(Target target,
                                                           BusterA64AliasInstruction const* instruction,
                                                           u32* word);
BUSTER_F_DECL BusterA64AliasStatus buster_a64_alias_decode_row(Target target,
                                                               u32 alias_form_id, u32 word,
                                                               BusterA64AliasResult* result);
BUSTER_F_DECL BusterA64AliasStatus buster_a64_alias_decode(Target target, u32 word,
                                                           BusterA64AliasResult* result);
BUSTER_F_DECL bool buster_a64_alias_validate(void);
