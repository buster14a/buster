#pragma once

#include <buster/lib/base.h>
#include <buster/lib/target.h>

// This is an internal, table-shaped semantic overlay for the first Apple-M1
// A64 control/fixup slice.  It deliberately does not add a public per-family
// instruction hierarchy: generic assembly code can consume one row and one
// typed operand vector through the adapter below.

typedef enum BusterAarch64ControlOwner
{
    BUSTER_AARCH64_CONTROL_OWNER_GENERAL,
    BUSTER_AARCH64_CONTROL_OWNER_COMPLEX_LITERAL,
    BUSTER_AARCH64_CONTROL_OWNER_COUNT,
} BusterAarch64ControlOwner;

typedef enum BusterAarch64ControlForm
{
    BUSTER_AARCH64_CONTROL_FORM_ADRP,
    BUSTER_AARCH64_CONTROL_FORM_ADR,
    BUSTER_AARCH64_CONTROL_FORM_B,
    BUSTER_AARCH64_CONTROL_FORM_BL,
    BUSTER_AARCH64_CONTROL_FORM_B_COND,
    BUSTER_AARCH64_CONTROL_FORM_CBZ_W,
    BUSTER_AARCH64_CONTROL_FORM_CBNZ_W,
    BUSTER_AARCH64_CONTROL_FORM_CBZ_X,
    BUSTER_AARCH64_CONTROL_FORM_CBNZ_X,
    BUSTER_AARCH64_CONTROL_FORM_TBZ,
    BUSTER_AARCH64_CONTROL_FORM_TBNZ,
    BUSTER_AARCH64_CONTROL_FORM_LDR_W,
    BUSTER_AARCH64_CONTROL_FORM_LDR_X,
    BUSTER_AARCH64_CONTROL_FORM_LDRSW_X,
    BUSTER_AARCH64_CONTROL_FORM_LDR_S,
    BUSTER_AARCH64_CONTROL_FORM_LDR_D,
    BUSTER_AARCH64_CONTROL_FORM_LDR_Q,
    BUSTER_AARCH64_CONTROL_FORM_PRFM,
    BUSTER_AARCH64_CONTROL_FORM_RET,
    BUSTER_AARCH64_CONTROL_FORM_CSEL_W,
    BUSTER_AARCH64_CONTROL_FORM_CSEL_X,
    BUSTER_AARCH64_CONTROL_FORM_CSINC_W,
    BUSTER_AARCH64_CONTROL_FORM_CSINC_X,
    BUSTER_AARCH64_CONTROL_FORM_CSINV_W,
    BUSTER_AARCH64_CONTROL_FORM_CSINV_X,
    BUSTER_AARCH64_CONTROL_FORM_CSNEG_W,
    BUSTER_AARCH64_CONTROL_FORM_CSNEG_X,
    BUSTER_AARCH64_CONTROL_FORM_COUNT,
} BusterAarch64ControlForm;

typedef enum BusterAarch64ControlOperandRole
{
    BUSTER_AARCH64_CONTROL_ROLE_NONE,
    BUSTER_AARCH64_CONTROL_ROLE_DESTINATION,
    BUSTER_AARCH64_CONTROL_ROLE_SOURCE_N,
    BUSTER_AARCH64_CONTROL_ROLE_SOURCE_M,
    BUSTER_AARCH64_CONTROL_ROLE_TEST,
    BUSTER_AARCH64_CONTROL_ROLE_TARGET,
    BUSTER_AARCH64_CONTROL_ROLE_CONDITION,
    BUSTER_AARCH64_CONTROL_ROLE_BIT,
    BUSTER_AARCH64_CONTROL_ROLE_PREFETCH,
    BUSTER_AARCH64_CONTROL_ROLE_COUNT,
} BusterAarch64ControlOperandRole;

typedef enum BusterAarch64ControlOperandKind
{
    BUSTER_AARCH64_CONTROL_OPERAND_NONE,
    BUSTER_AARCH64_CONTROL_OPERAND_REGISTER,
    BUSTER_AARCH64_CONTROL_OPERAND_IMMEDIATE,
    BUSTER_AARCH64_CONTROL_OPERAND_CONDITION,
    BUSTER_AARCH64_CONTROL_OPERAND_PC_RELATIVE,
    BUSTER_AARCH64_CONTROL_OPERAND_COUNT,
} BusterAarch64ControlOperandKind;

typedef enum BusterAarch64ControlRegister31Role
{
    BUSTER_AARCH64_CONTROL_REGISTER31_NONE,
    BUSTER_AARCH64_CONTROL_REGISTER31_ZR,
    BUSTER_AARCH64_CONTROL_REGISTER31_SP,
    BUSTER_AARCH64_CONTROL_REGISTER31_COUNT,
} BusterAarch64ControlRegister31Role;

typedef enum BusterAarch64ControlRegisterClass
{
    BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR,
    BUSTER_AARCH64_CONTROL_REGISTER_CLASS_FP_SIMD,
    BUSTER_AARCH64_CONTROL_REGISTER_CLASS_COUNT,
} BusterAarch64ControlRegisterClass;

typedef enum BusterAarch64ControlPcRelativeLayout
{
    BUSTER_AARCH64_CONTROL_PC_NONE,
    BUSTER_AARCH64_CONTROL_PC_IMM26,
    BUSTER_AARCH64_CONTROL_PC_IMM19,
    BUSTER_AARCH64_CONTROL_PC_IMM14,
    BUSTER_AARCH64_CONTROL_PC_ADRP,
    BUSTER_AARCH64_CONTROL_PC_ADR,
    BUSTER_AARCH64_CONTROL_PC_COUNT,
} BusterAarch64ControlPcRelativeLayout;

typedef enum BusterAarch64ControlFixupKind
{
    BUSTER_AARCH64_CONTROL_FIXUP_NONE,
    BUSTER_AARCH64_CONTROL_FIXUP_BRANCH26,
    BUSTER_AARCH64_CONTROL_FIXUP_CALL26,
    BUSTER_AARCH64_CONTROL_FIXUP_B_COND19,
    BUSTER_AARCH64_CONTROL_FIXUP_COMPARE19,
    BUSTER_AARCH64_CONTROL_FIXUP_TEST14,
    BUSTER_AARCH64_CONTROL_FIXUP_LITERAL19,
    BUSTER_AARCH64_CONTROL_FIXUP_ADRP_PAGE21,
    BUSTER_AARCH64_CONTROL_FIXUP_ADR_BYTE21,
    BUSTER_AARCH64_CONTROL_FIXUP_COUNT,
} BusterAarch64ControlFixupKind;

typedef enum BusterAarch64ControlRelocationPolicy
{
    BUSTER_AARCH64_CONTROL_RELOCATION_NONE,
    BUSTER_AARCH64_CONTROL_RELOCATION_LOCAL_ONLY,
    // Darwin permits an unresolved external relocation only for B/BL.  The
    // policy is intentionally named independently of AssemblyRelocationKind
    // so this internal adapter remains usable by metadata-only consumers.
    BUSTER_AARCH64_CONTROL_RELOCATION_DARWIN_EXTERNAL_BRANCH26,
    BUSTER_AARCH64_CONTROL_RELOCATION_COUNT,
} BusterAarch64ControlRelocationPolicy;

typedef struct BusterAarch64ControlOperandSchema BusterAarch64ControlOperandSchema;
struct BusterAarch64ControlOperandSchema
{
    u8 role;
    u8 kind;
    u8 width; // 0 means polymorphic W/X (TBZ/TBNZ); otherwise 32/64/128.
    u8 register31_role;
    u8 register_class;
    u8 reserved0[3];
    s64 minimum;
    s64 maximum;
};

typedef struct BusterAarch64ControlPcRelative BusterAarch64ControlPcRelative;
struct BusterAarch64ControlPcRelative
{
    u8 layout;
    u8 bits;
    u8 scale_log2;
    u8 split; // 1 for ADR/ADRP immlo:immhi split fields.
    s64 minimum;
    s64 maximum;
    u32 alignment;
};

typedef struct BusterAarch64ControlSemanticRecord BusterAarch64ControlSemanticRecord;
typedef struct BusterAarch64ControlString BusterAarch64ControlString;
struct BusterAarch64ControlString
{
    u32 offset;
    u32 length;
};

typedef struct BusterAarch64ControlCondition BusterAarch64ControlCondition;
struct BusterAarch64ControlCondition
{
    u8 value;
    u8 inverse;
    u8 valid;
    u8 inverse_valid;
    BusterAarch64ControlString name;
};

struct BusterAarch64ControlSemanticRecord
{
    BusterAarch64ControlString id;
    BusterAarch64ControlString encoding_name;
    BusterAarch64ControlString mnemonic;
    BusterAarch64ControlString assembly;
    u64 row_digest;
    u32 fixed_mask;
    u32 fixed_value;
    u32 oracle_word;
    u8 owner;
    u8 form;
    u8 operand_count;
    u8 optional_operand_mask;
    BusterAarch64ControlOperandSchema operands[4];
    BusterAarch64ControlPcRelative pc_relative;
    u8 fixup_kind;
    u8 relocation_policy;
    u8 default_operand;
    u8 reserved;
};

typedef struct BusterAarch64ControlOperandValue BusterAarch64ControlOperandValue;
struct BusterAarch64ControlOperandValue
{
    s64 value;
    u8 kind;
    u8 width;
    u8 register31_role;
    u8 register_class;
    u8 reserved[3];
};

typedef struct BusterAarch64ControlInstruction BusterAarch64ControlInstruction;
struct BusterAarch64ControlInstruction
{
    u16 row;
    u8 operand_count;
    u8 defaulted_mask;
    BusterAarch64ControlOperandValue operands[4];
};

typedef enum BusterAarch64ControlRelocationKind
{
    BUSTER_AARCH64_CONTROL_RELOCATION_KIND_NONE,
    BUSTER_AARCH64_CONTROL_RELOCATION_KIND_BRANCH26,
    BUSTER_AARCH64_CONTROL_RELOCATION_KIND_CALL26,
    BUSTER_AARCH64_CONTROL_RELOCATION_KIND_COUNT,
} BusterAarch64ControlRelocationKind;

typedef struct BusterAarch64ControlFixupRequest BusterAarch64ControlFixupRequest;
struct BusterAarch64ControlFixupRequest
{
    Target target;
    u64 place_address;
    u64 target_address;
    s64 addend;
    bool symbol_defined;
    bool symbol_external;
    u8 reserved[6];
};

typedef struct BusterAarch64ControlFixupResult BusterAarch64ControlFixupResult;
struct BusterAarch64ControlFixupResult
{
    u8 fixup_kind;
    u8 relocation_kind;
    u8 external;
    u8 resolved;
    s64 displacement;
};

// The generated records are sorted by canonical Arm row ID.  The constants
// below are the cross-lens digest recipe (sorted id<TAB>row.digest<LF>).
#define BUSTER_AARCH64_CONTROL_SEMANTIC_ROW_COUNT 27u
#define BUSTER_AARCH64_CONTROL_SEMANTIC_DIGEST "981200c3a2350be0f18eb8785929b30de5f468b5a8faa6419d96b7fa3b5e42a4"
#define BUSTER_AARCH64_CONTROL_GENERAL_LABEL_DIGEST "fff799a7ccd6dbe7adb9bc238b4b5838b6dd02928450e2048a579c645e891a15"
#define BUSTER_AARCH64_CONTROL_CONDITION_SELECT_DIGEST "a6aa6bcea3eaada795da6acbaac131afe8d8eeae58839cf5cb509934dafcb0ad"
#define BUSTER_AARCH64_CONTROL_RET_DIGEST "babf7e807a273b459d0fb4caa94e41b9779c9949ec2f7bf4eda1803aa7a734d6"
#define BUSTER_AARCH64_CONTROL_LITERAL_DIGEST "2a29a4d0e6b7d537dde6523e1859678169a4c93c973e4bd1d4f6e87fa6d6e25e"

BUSTER_F_DECL u32 buster_aarch64_control_semantic_count(void);
BUSTER_F_DECL bool buster_aarch64_control_semantic_row(u32 row, BusterAarch64ControlSemanticRecord* result);
BUSTER_F_DECL bool buster_aarch64_control_semantic_lookup(String8 id, u32* row);
BUSTER_F_DECL bool buster_aarch64_control_semantic_string(BusterAarch64ControlString string, String8* result);
BUSTER_F_DECL u8 buster_aarch64_control_semantic_string_byte(BusterAarch64ControlString string, u32 index);
BUSTER_F_DECL u32 buster_aarch64_control_condition_count(void);
BUSTER_F_DECL bool buster_aarch64_control_condition(u8 value, BusterAarch64ControlCondition* condition);
BUSTER_F_DECL bool buster_aarch64_control_semantic_validate(void);
BUSTER_F_DECL char const* buster_aarch64_control_semantic_digest(void);
BUSTER_F_DECL char const* buster_aarch64_control_semantic_group_digest(BusterAarch64ControlOwner owner);

// Encode/decode are transactional: output objects are assigned only after all
// operand, range, fixed-bit, and condition checks have succeeded.
BUSTER_F_DECL bool buster_aarch64_control_semantic_encode(BusterAarch64ControlInstruction const* instruction, u32* word);
BUSTER_F_DECL bool buster_aarch64_control_semantic_decode(u32 word, BusterAarch64ControlInstruction* instruction);
BUSTER_F_DECL bool buster_aarch64_control_semantic_patch(u32 row, u32 word, s64 displacement, u32* patched);

// Resolve a local label or classify an unresolved symbol.  External Darwin
// relocation is deliberately restricted to B/BL; every other row is local-only.
BUSTER_F_DECL bool buster_aarch64_control_semantic_fixup(u32 row, u32 word, BusterAarch64ControlFixupRequest request,
                                                         u32* patched, BusterAarch64ControlFixupResult* result);
