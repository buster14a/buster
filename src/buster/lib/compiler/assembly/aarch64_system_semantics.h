#pragma once

#include <buster/lib/base.h>
#include <buster/lib/target.h>

// Typed raw-field semantics for the variable canonical Arm A64 system rows
// in the pinned Apple-M1 closure.  The 15 fixed canonical system spellings
// are intentionally owned by arm_m1_fixed; aliases and named system-register
// dictionaries remain outside this table.

typedef enum BusterAarch64SystemForm
{
    BUSTER_AARCH64_SYSTEM_FORM_BRK,
    BUSTER_AARCH64_SYSTEM_FORM_CLREX,
    BUSTER_AARCH64_SYSTEM_FORM_DCPS1,
    BUSTER_AARCH64_SYSTEM_FORM_DCPS2,
    BUSTER_AARCH64_SYSTEM_FORM_DCPS3,
    BUSTER_AARCH64_SYSTEM_FORM_DMB,
    BUSTER_AARCH64_SYSTEM_FORM_DSB,
    BUSTER_AARCH64_SYSTEM_FORM_HINT,
    BUSTER_AARCH64_SYSTEM_FORM_HLT,
    BUSTER_AARCH64_SYSTEM_FORM_HVC,
    BUSTER_AARCH64_SYSTEM_FORM_ISB,
    BUSTER_AARCH64_SYSTEM_FORM_MRS,
    BUSTER_AARCH64_SYSTEM_FORM_MSR_PSTATE,
    BUSTER_AARCH64_SYSTEM_FORM_MSR,
    BUSTER_AARCH64_SYSTEM_FORM_SMC,
    BUSTER_AARCH64_SYSTEM_FORM_SVC,
    BUSTER_AARCH64_SYSTEM_FORM_SYSL,
    BUSTER_AARCH64_SYSTEM_FORM_SYS,
    BUSTER_AARCH64_SYSTEM_FORM_COUNT,
} BusterAarch64SystemForm;

typedef enum BusterAarch64SystemFieldKind
{
    BUSTER_AARCH64_SYSTEM_FIELD_IMMEDIATE,
    BUSTER_AARCH64_SYSTEM_FIELD_REGISTER,
    BUSTER_AARCH64_SYSTEM_FIELD_OP1,
    BUSTER_AARCH64_SYSTEM_FIELD_CRN,
    BUSTER_AARCH64_SYSTEM_FIELD_CRM,
    BUSTER_AARCH64_SYSTEM_FIELD_O0,
    BUSTER_AARCH64_SYSTEM_FIELD_OP2,
    BUSTER_AARCH64_SYSTEM_FIELD_COUNT,
} BusterAarch64SystemFieldKind;

typedef enum BusterAarch64SystemRowFlags
{
    BUSTER_AARCH64_SYSTEM_ROW_NONE = 0,
    BUSTER_AARCH64_SYSTEM_ROW_HINT = 1u << 0,
    BUSTER_AARCH64_SYSTEM_ROW_PSTATE = 1u << 1,
    BUSTER_AARCH64_SYSTEM_ROW_SYSTEM_REGISTER = 1u << 2,
} BusterAarch64SystemRowFlags;

typedef struct BusterAarch64SystemString BusterAarch64SystemString;
struct BusterAarch64SystemString
{
    u32 offset;
    u32 length;
};

typedef struct BusterAarch64SystemFieldSchema BusterAarch64SystemFieldSchema;
struct BusterAarch64SystemFieldSchema
{
    BusterAarch64SystemString name;
    u8 kind;
    u8 width;
    u8 instruction_lsb;
    u8 value_lsb;
    s64 minimum;
    s64 maximum;
};

typedef struct BusterAarch64SystemSemanticRecord BusterAarch64SystemSemanticRecord;
struct BusterAarch64SystemSemanticRecord
{
    BusterAarch64SystemString id;
    BusterAarch64SystemString encoding_name;
    BusterAarch64SystemString mnemonic;
    BusterAarch64SystemString assembly;
    u64 row_digest;
    u32 fixed_mask;
    u32 fixed_value;
    u32 field_mask;
    u8 form;
    u8 field_count;
    u8 optional_field_mask;
    u8 default_value;
    u8 flags;
    // If constraint_field is not UINT8_MAX, bit N in constraint_mask permits
    // value N for that raw field.  The pinned barrier rows use this metadata
    // for their architectural CRm allocations; unconstrained rows use the
    // UINT8_MAX/zero pair.
    u8 constraint_field;
    u16 constraint_mask;
    u8 reserved[2];
};

typedef struct BusterAarch64SystemOperandValue BusterAarch64SystemOperandValue;
struct BusterAarch64SystemOperandValue
{
    s64 value;
    u8 kind;
    u8 width;
    u8 reserved[6];
};

typedef struct BusterAarch64SystemInstruction BusterAarch64SystemInstruction;
struct BusterAarch64SystemInstruction
{
    u16 row;
    u8 field_count;
    u8 defaulted_mask;
    BusterAarch64SystemOperandValue fields[6];
};

#define BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT 18u
#define BUSTER_AARCH64_SYSTEM_FIXED_CANONICAL_COUNT 15u
#define BUSTER_AARCH64_SYSTEM_CANONICAL_COUNT \
    (BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT + BUSTER_AARCH64_SYSTEM_FIXED_CANONICAL_COUNT)
#define BUSTER_AARCH64_SYSTEM_SEMANTIC_DIGEST "18c9e62f0ab26bca5192dafd7cc05c2956d0ba6be7519e82159de1640f071e81"

BUSTER_F_DECL u32 buster_aarch64_system_semantic_count(void);
BUSTER_F_DECL bool buster_aarch64_system_semantic_row(u32 row, BusterAarch64SystemSemanticRecord* result);
BUSTER_F_DECL bool buster_aarch64_system_semantic_field(u32 row, u32 field, BusterAarch64SystemFieldSchema* result);
BUSTER_F_DECL bool buster_aarch64_system_semantic_string(BusterAarch64SystemString string, String8* result);
BUSTER_F_DECL u8 buster_aarch64_system_semantic_string_byte(BusterAarch64SystemString string, u32 index);
BUSTER_F_DECL bool buster_aarch64_system_semantic_lookup(String8 id, u32* row);
BUSTER_F_DECL bool buster_aarch64_system_semantic_validate(void);
BUSTER_F_DECL char const* buster_aarch64_system_semantic_digest(void);

// MRS/MSR expose Arm's one-bit o0 encoding field.  The architectural system
// register spelling uses op0 values 2 and 3; these checked helpers keep that
// mapping explicit without owning a named register dictionary.
BUSTER_F_DECL bool buster_aarch64_system_op0_encode(u32 op0, u32* o0);
BUSTER_F_DECL bool buster_aarch64_system_op0_decode(u32 o0, u32* op0);

// The raw table is target-gated to the explicit Apple-M1 profile (or a native
// target whose detected model is Apple M1) with a valid effective feature set;
// generic AArch64 targets are deliberately rejected.  Hint values that denote
// unsupported M1 extensions or reserved encodings are rejected.  `fields` are
// the Arm source fields in the order published by the canonical row.  Optional
// fields retain their slot and are marked in defaulted_mask (CLREX/ISB default
// to #15; SYS Rt defaults to XZR/Rt=31).  Generated row metadata carries the
// pinned DMB/DSB/ISB CRm allocation masks; CLREX remains valid for every
// four-bit CRm value.
BUSTER_F_DECL bool buster_aarch64_system_semantic_encode(Target target, BusterAarch64SystemInstruction const* instruction, u32* word);
// Decode a word as one explicitly selected variable canonical form.  This is
// the path that retains HINT #imm support when its word is also a fixed
// spelling such as NOP or WFE.
BUSTER_F_DECL bool buster_aarch64_system_semantic_decode_form(Target target, u32 row, u32 word,
                                                              BusterAarch64SystemInstruction* instruction);
// Word-first decoding delegates canonical precedence to the Arm canonical
// decoder.  Fixed rows therefore own overlapping words; this succeeds only
// when that selected digest belongs to one of the 18 variable rows.
BUSTER_F_DECL bool buster_aarch64_system_semantic_decode(Target target, u32 word, BusterAarch64SystemInstruction* instruction);

// Lightweight target and denominator gates used by integration/tests.
BUSTER_F_DECL bool buster_aarch64_system_semantic_target_supported(Target target);
BUSTER_F_DECL u32 buster_aarch64_system_semantic_fixed_canonical_count(void);
BUSTER_F_DECL u32 buster_aarch64_system_semantic_canonical_count(void);
