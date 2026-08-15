#pragma once

#include <buster/lib/base.h>

/*
 * Bounded semantic bindings for the pinned Apple-M1 A64 snapshot.  Every
 * returned record is a value type: callers never receive a pointer into the
 * generated string pool or table arrays.  Resolve a BusterA64SemanticString
 * through buster_a64_semantic_string_byte() when a textual value is needed.
 */

typedef struct BusterA64SemanticString BusterA64SemanticString;
struct BusterA64SemanticString
{
    u32 offset;
    u32 length;
};

typedef enum BusterA64SemanticOwner
{
    BUSTER_A64_SEMANTIC_OWNER_FIXED32,
    BUSTER_A64_SEMANTIC_OWNER_DIRECT_GPR,
    BUSTER_A64_SEMANTIC_OWNER_SCALAR_INTEGER,
    BUSTER_A64_SEMANTIC_OWNER_DIRECT_SIMD,
    BUSTER_A64_SEMANTIC_OWNER_SYSTEM,
    BUSTER_A64_SEMANTIC_OWNER_MEMORY,
    BUSTER_A64_SEMANTIC_OWNER_GENERAL_NONMEMORY,
    BUSTER_A64_SEMANTIC_OWNER_COMPLEX_SIMD_FP,
    BUSTER_A64_SEMANTIC_OWNER_ALIAS,
    BUSTER_A64_SEMANTIC_OWNER_COUNT,
} BusterA64SemanticOwner;

typedef enum BusterA64SemanticFormKind
{
    BUSTER_A64_SEMANTIC_FORM_CANONICAL,
    BUSTER_A64_SEMANTIC_FORM_ALIAS,
} BusterA64SemanticFormKind;

typedef enum BusterA64SemanticStatus
{
    BUSTER_A64_SEMANTIC_STATUS_DEFINED,
    BUSTER_A64_SEMANTIC_STATUS_UNDEFINED,
} BusterA64SemanticStatus;

typedef enum BusterA64SemanticOperandKind
{
    BUSTER_A64_SEMANTIC_OPERAND_GPR_REGISTER,
    BUSTER_A64_SEMANTIC_OPERAND_GPR_WIDTH_SELECTOR,
    BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER,
    BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT,
    BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR,
    BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST,
    BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE,
    BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE,
    BUSTER_A64_SEMANTIC_OPERAND_FP_IMMEDIATE,
    BUSTER_A64_SEMANTIC_OPERAND_CONDITION,
    BUSTER_A64_SEMANTIC_OPERAND_NZCV_FLAGS,
    BUSTER_A64_SEMANTIC_OPERAND_SHIFT,
    BUSTER_A64_SEMANTIC_OPERAND_EXTEND,
    BUSTER_A64_SEMANTIC_OPERAND_ROTATE,
    BUSTER_A64_SEMANTIC_OPERAND_MEMORY_BASE,
    BUSTER_A64_SEMANTIC_OPERAND_MEMORY_OFFSET,
    BUSTER_A64_SEMANTIC_OPERAND_MEMORY_DATA_REGISTER,
    BUSTER_A64_SEMANTIC_OPERAND_LABEL_FIXUP,
    BUSTER_A64_SEMANTIC_OPERAND_SYSTEM_REGISTER,
    BUSTER_A64_SEMANTIC_OPERAND_SYSTEM_OPERATION,
    BUSTER_A64_SEMANTIC_OPERAND_BARRIER_OPTION,
    BUSTER_A64_SEMANTIC_OPERAND_PREFETCH_OPERATION,
    BUSTER_A64_SEMANTIC_OPERAND_FIXED_CONSTANT,
    BUSTER_A64_SEMANTIC_OPERAND_OTHER,
    BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR,
} BusterA64SemanticOperandKind;

/* Operand kinds/flags are presentation hints in this snapshot.  Consumers
 * must not use them to encode or decode until this status becomes
 * authoritative in a future semantic implementation. */
typedef enum BusterA64SemanticClassificationStatus
{
    BUSTER_A64_SEMANTIC_CLASSIFICATION_PRESENTATION_ONLY,
} BusterA64SemanticClassificationStatus;

/* Stable operand-flag bits.  These mirror SEMANTIC_FLAG_NAMES in the
 * generator; they are intentionally not assigned from per-row flag order. */
typedef u64 BusterA64SemanticOperandFlag;
/* Flag constants intentionally use macros: C enum values are int-sized. */
#define BUSTER_A64_SEMANTIC_FLAG_ARRANGEMENT_SELECTOR (UINT64_C(1) << 0)
#define BUSTER_A64_SEMANTIC_FLAG_BARRIER (UINT64_C(1) << 1)
#define BUSTER_A64_SEMANTIC_FLAG_BITMASK_TRANSFORM (UINT64_C(1) << 2)
#define BUSTER_A64_SEMANTIC_FLAG_BRANCH_REL14 (UINT64_C(1) << 3)
#define BUSTER_A64_SEMANTIC_FLAG_BRANCH_REL19 (UINT64_C(1) << 4)
#define BUSTER_A64_SEMANTIC_FLAG_BRANCH_REL26 (UINT64_C(1) << 5)
#define BUSTER_A64_SEMANTIC_FLAG_CONDITION_FIELD (UINT64_C(1) << 6)
#define BUSTER_A64_SEMANTIC_FLAG_CONDITION_INVERTED (UINT64_C(1) << 7)
#define BUSTER_A64_SEMANTIC_FLAG_DEFAULT_ZERO (UINT64_C(1) << 8)
#define BUSTER_A64_SEMANTIC_FLAG_EXTEND_OPTION (UINT64_C(1) << 9)
#define BUSTER_A64_SEMANTIC_FLAG_FIXED_LITERAL_2 (UINT64_C(1) << 10)
#define BUSTER_A64_SEMANTIC_FLAG_FP_IMM8_ENCODING (UINT64_C(1) << 11)
#define BUSTER_A64_SEMANTIC_FLAG_FP_OR_FIXED_POINT (UINT64_C(1) << 12)
#define BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_W32 (UINT64_C(1) << 13)
#define BUSTER_A64_SEMANTIC_FLAG_GPR_WIDTH_X64 (UINT64_C(1) << 14)
#define BUSTER_A64_SEMANTIC_FLAG_INDEX_IMMEDIATE (UINT64_C(1) << 15)
#define BUSTER_A64_SEMANTIC_FLAG_MEMORY_BASE (UINT64_C(1) << 16)
#define BUSTER_A64_SEMANTIC_FLAG_MEMORY_OFFSET (UINT64_C(1) << 17)
#define BUSTER_A64_SEMANTIC_FLAG_MEMORY_WRITEBACK (UINT64_C(1) << 18)
#define BUSTER_A64_SEMANTIC_FLAG_NZCV_4BIT (UINT64_C(1) << 19)
#define BUSTER_A64_SEMANTIC_FLAG_OPTIONAL (UINT64_C(1) << 20)
#define BUSTER_A64_SEMANTIC_FLAG_OPTIONAL_DEFAULTS_X30 (UINT64_C(1) << 21)
#define BUSTER_A64_SEMANTIC_FLAG_PAGE_RELATIVE (UINT64_C(1) << 22)
#define BUSTER_A64_SEMANTIC_FLAG_PC_RELATIVE (UINT64_C(1) << 23)
#define BUSTER_A64_SEMANTIC_FLAG_PC_RELATIVE_ADR (UINT64_C(1) << 24)
#define BUSTER_A64_SEMANTIC_FLAG_PREFETCH (UINT64_C(1) << 25)
#define BUSTER_A64_SEMANTIC_FLAG_ROTATE_IMMEDIATE (UINT64_C(1) << 26)
#define BUSTER_A64_SEMANTIC_FLAG_SHIFT_LEFT (UINT64_C(1) << 27)
#define BUSTER_A64_SEMANTIC_FLAG_SHIFT_RIGHT (UINT64_C(1) << 28)
#define BUSTER_A64_SEMANTIC_FLAG_SHIFT_ROTATE (UINT64_C(1) << 29)
#define BUSTER_A64_SEMANTIC_FLAG_SIGNED (UINT64_C(1) << 30)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_INDEX_REGISTER (UINT64_C(1) << 31)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_LANE_INDEX (UINT64_C(1) << 32)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_LIST_MEMBER (UINT64_C(1) << 33)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_SCALAR (UINT64_C(1) << 34)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_VECTOR (UINT64_C(1) << 35)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_B8 (UINT64_C(1) << 36)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_D64 (UINT64_C(1) << 37)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_H16 (UINT64_C(1) << 38)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_Q128 (UINT64_C(1) << 39)
#define BUSTER_A64_SEMANTIC_FLAG_SIMD_WIDTH_S32 (UINT64_C(1) << 40)
#define BUSTER_A64_SEMANTIC_FLAG_SP_ALLOWED (UINT64_C(1) << 41)
#define BUSTER_A64_SEMANTIC_FLAG_SYSTEM_ENCODING (UINT64_C(1) << 42)
#define BUSTER_A64_SEMANTIC_FLAG_UNSIGNED (UINT64_C(1) << 43)
#define BUSTER_A64_SEMANTIC_FLAG_WRITEBACK_POST_INDEX (UINT64_C(1) << 44)
#define BUSTER_A64_SEMANTIC_FLAG_WRITEBACK_PRE_INDEX (UINT64_C(1) << 45)
#define BUSTER_A64_SEMANTIC_FLAG_ZR_ALLOWED (UINT64_C(1) << 46)

typedef enum BusterA64SemanticTransformKind
{
    BUSTER_A64_SEMANTIC_TRANSFORM_CONCAT,
    BUSTER_A64_SEMANTIC_TRANSFORM_SLICE,
    BUSTER_A64_SEMANTIC_TRANSFORM_INTEGER_DECODE,
    BUSTER_A64_SEMANTIC_TRANSFORM_TEXT,
    BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE,
    BUSTER_A64_SEMANTIC_TRANSFORM_OVERLAY_REQUIRED,
    BUSTER_A64_SEMANTIC_TRANSFORM_SHARED_DECODE,
} BusterA64SemanticTransformKind;

/* Typed operations emitted for every normalized VM program.  The generated
 * expression string remains available as a diagnostic rendering, but C
 * consumers can inspect these records without reparsing JSON. */
typedef enum BusterA64SemanticProgramOp
{
    BUSTER_A64_SEMANTIC_PROGRAM_FIELD,
    BUSTER_A64_SEMANTIC_PROGRAM_UINT_CONCAT,
    BUSTER_A64_SEMANTIC_PROGRAM_SIGN_EXTEND,
    BUSTER_A64_SEMANTIC_PROGRAM_SCALE_MUL,
    BUSTER_A64_SEMANTIC_PROGRAM_SCALE_DIV,
    BUSTER_A64_SEMANTIC_PROGRAM_SCALE_POW2,
    BUSTER_A64_SEMANTIC_PROGRAM_ADD_CONST,
    BUSTER_A64_SEMANTIC_PROGRAM_SUB_FROM_CONST,
    BUSTER_A64_SEMANTIC_PROGRAM_REGISTER_ADD_MOD,
    BUSTER_A64_SEMANTIC_PROGRAM_LITERAL,
    BUSTER_A64_SEMANTIC_PROGRAM_TEXT_FACTOR,
    BUSTER_A64_SEMANTIC_PROGRAM_SHARED_DECODE,
} BusterA64SemanticProgramOp;

typedef enum BusterA64SemanticProgramOperandKind
{
    BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_FIELD,
    BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_ARRANGEMENT,
    BUSTER_A64_SEMANTIC_PROGRAM_OPERAND_LITERAL,
} BusterA64SemanticProgramOperandKind;

typedef struct BusterA64SemanticSegment BusterA64SemanticSegment;
struct BusterA64SemanticSegment
{
    u32 id;
    u8 instruction_lsb;
    u8 width;
    u8 value_lsb;
    u8 reserved;
};

typedef struct BusterA64SemanticField BusterA64SemanticField;
struct BusterA64SemanticField
{
    u32 id;
    BusterA64SemanticString name;
    u32 source_mask;
    u32 segment_first;
    u16 segment_count;
    u8 width;
    u8 reserved;
};

typedef struct BusterA64SemanticOperand BusterA64SemanticOperand;
struct BusterA64SemanticOperand
{
    u32 id;
    u32 form_id;
    BusterA64SemanticString link;
    BusterA64SemanticString symbol;
    u32 field_first;
    u32 field_indices_first;
    u32 transform_first;
    u16 field_count;
    u16 field_index_count;
    u16 transform_count;
    u8 kind;
    u32 kind_mask;
    u64 flags;
    u8 position;
    u8 classification_status;
    u8 reserved;
    BusterA64SemanticString role;
    BusterA64SemanticString direction;
};

typedef enum BusterA64SemanticValueKind
{
    BUSTER_A64_SEMANTIC_VALUE_INTEGER,
    BUSTER_A64_SEMANTIC_VALUE_BITS,
    BUSTER_A64_SEMANTIC_VALUE_ENUM,
    BUSTER_A64_SEMANTIC_VALUE_EXPRESSION,
    BUSTER_A64_SEMANTIC_VALUE_PROGRAM,
} BusterA64SemanticValueKind;

typedef struct BusterA64SemanticValueAtom BusterA64SemanticValueAtom;
struct BusterA64SemanticValueAtom
{
    u32 id;
    u8 kind;
    s64 integer;
    BusterA64SemanticString text;
    u32 program_first;
    u16 program_count;
};

typedef struct BusterA64SemanticProgramInstruction BusterA64SemanticProgramInstruction;
struct BusterA64SemanticProgramInstruction
{
    u32 id;
    BusterA64SemanticString field;
    BusterA64SemanticString text;
    u32 operand_first;
    u16 operand_count;
    s32 value;
    u16 high;
    u16 low;
    /* For a sliced field, width == high - low + 1.  An unsliced field uses
     * high == low == UINT16_MAX and width == 0; resolve its full width from
     * the owning form's field descriptor. */
    u16 width;
    u16 modulus;
    u8 op;
};

typedef struct BusterA64SemanticProgramOperand BusterA64SemanticProgramOperand;
struct BusterA64SemanticProgramOperand
{
    u32 id;
    BusterA64SemanticString field;
    BusterA64SemanticString text;
    s32 value;
    u16 high;
    u16 low;
    u16 width;
    u8 kind;
};

typedef struct BusterA64SemanticValue BusterA64SemanticValue;
struct BusterA64SemanticValue
{
    u32 id;
    u32 key_first;
    u32 result_first;
    u16 key_count;
    u16 result_count;
};

typedef struct BusterA64SemanticTransform BusterA64SemanticTransform;
struct BusterA64SemanticTransform
{
    u32 id;
    BusterA64SemanticString expression;
    u32 source;
    u32 p0;
    u32 p1;
    u32 part_first;
    u32 value_first;
    u32 table_id;
    u32 program_first;
    u16 part_count;
    u16 value_count;
    u16 program_count;
    u8 kind;
    bool invertible;
    u16 reserved;
};

typedef struct BusterA64SemanticTableHeader BusterA64SemanticTableHeader;
struct BusterA64SemanticTableHeader
{
    u32 id;
    u32 key_header_first;
    u16 key_header_count;
    BusterA64SemanticString result_header;
};

typedef struct BusterA64SemanticAlias BusterA64SemanticAlias;
struct BusterA64SemanticAlias
{
    u32 form_id;
    BusterA64SemanticString target_file;
    BusterA64SemanticString target_id;
    BusterA64SemanticString target_encoding_id;
    u32 condition_first;
    u32 preference_condition_first;
    u32 preference_first;
    u16 condition_count;
    u16 preference_condition_count;
    u16 preference_count;
    s32 preference_rank;
};

typedef struct BusterA64SemanticAliasPreference BusterA64SemanticAliasPreference;
struct BusterA64SemanticAliasPreference
{
    u32 id;
    BusterA64SemanticString alias_file;
    BusterA64SemanticString alias_id;
    u32 condition_first;
    u16 condition_count;
    s32 rank;
};

typedef struct BusterA64SemanticConstraint BusterA64SemanticConstraint;
struct BusterA64SemanticConstraint
{
    u32 form_id;
    u32 feature_first;
    u32 program_first;
    u16 feature_count;
    u16 program_count;
};

typedef struct BusterA64SemanticForm BusterA64SemanticForm;
struct BusterA64SemanticForm
{
    u32 id;
    BusterA64SemanticString name;
    BusterA64SemanticString mnemonic;
    BusterA64SemanticString assembly;
    u64 source_digest;
    u32 fixed_mask;
    u32 fixed_value;
    u32 field_first;
    u32 operand_first;
    u32 transform_first;
    u16 field_count;
    u16 operand_count;
    u16 transform_count;
    u8 owner;
    u8 kind;
    u8 raw_layout_resolved;
    u8 status;
};

// Decode the generated semantic blobs once, on the calling thread, before any
// gang queries the module. See the module rule in AGENTS.md.
BUSTER_F_DECL void buster_aarch64_semantics_prewarm(void);
BUSTER_F_DECL u32 buster_a64_semantic_schema_version(void);
BUSTER_F_DECL u32 buster_a64_semantic_form_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_field_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_segment_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_operand_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_operand_field_index_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_transform_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_transform_part_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_program_instruction_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_program_operand_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_parsed_program_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_value_program_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_value_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_value_atom_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_string_pool_size(void);
BUSTER_F_DECL u32 buster_a64_semantic_table_header_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_table_key_header_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_alias_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_alias_condition_token_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_alias_preference_condition_token_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_alias_preference_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_constraint_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_constraint_feature_tag_count(void);
BUSTER_F_DECL u32 buster_a64_semantic_constraint_program_token_count(void);

BUSTER_F_DECL bool buster_a64_semantic_string(u32 offset, BusterA64SemanticString* result);
BUSTER_F_DECL char8 buster_a64_semantic_string_byte(BusterA64SemanticString string, u32 index);
BUSTER_F_DECL bool buster_a64_semantic_form(u32 id, BusterA64SemanticForm* result);
BUSTER_F_DECL bool buster_a64_semantic_field(u32 id, BusterA64SemanticField* result);
BUSTER_F_DECL bool buster_a64_semantic_segment(u32 id, BusterA64SemanticSegment* result);
BUSTER_F_DECL bool buster_a64_semantic_operand(u32 id, BusterA64SemanticOperand* result);
BUSTER_F_DECL bool buster_a64_semantic_operand_field_index(u32 operand_id, u32 ordinal, u32* field_id);
BUSTER_F_DECL bool buster_a64_semantic_transform(u32 id, BusterA64SemanticTransform* result);
BUSTER_F_DECL bool buster_a64_semantic_transform_part(u32 id, u32 ordinal, BusterA64SemanticString* result);
BUSTER_F_DECL bool buster_a64_semantic_transform_value(u32 id, u32 ordinal, BusterA64SemanticValue* result);
BUSTER_F_DECL bool buster_a64_semantic_program_instruction(u32 id, BusterA64SemanticProgramInstruction* result);
BUSTER_F_DECL bool buster_a64_semantic_program_operand(u32 id, u32 ordinal, BusterA64SemanticProgramOperand* result);
BUSTER_F_DECL bool buster_a64_semantic_transform_program_instruction(u32 transform_id, u32 ordinal, BusterA64SemanticProgramInstruction* result);
BUSTER_F_DECL bool buster_a64_semantic_value_atom(u32 id, BusterA64SemanticValueAtom* result);
BUSTER_F_DECL bool buster_a64_semantic_value_atom_program_instruction(u32 atom_id, u32 ordinal, BusterA64SemanticProgramInstruction* result);
BUSTER_F_DECL bool buster_a64_semantic_table_header(u32 id, BusterA64SemanticTableHeader* result);
BUSTER_F_DECL bool buster_a64_semantic_table_key_header(u32 id, u32 ordinal, BusterA64SemanticString* result);
BUSTER_F_DECL bool buster_a64_semantic_table_result_header(u32 id, BusterA64SemanticString* result);
BUSTER_F_DECL bool buster_a64_semantic_transform_table_header(u32 transform_id, u32* table_id);
BUSTER_F_DECL bool buster_a64_semantic_alias(u32 form_id, BusterA64SemanticAlias* result);
BUSTER_F_DECL bool buster_a64_semantic_alias_descriptor(u32 form_id, BusterA64SemanticAlias* result);
BUSTER_F_DECL bool buster_a64_semantic_alias_by_ordinal(u32 ordinal, BusterA64SemanticAlias* result);
BUSTER_F_DECL bool buster_a64_semantic_alias_condition_token(u32 form_id, u32 ordinal, BusterA64SemanticString* result);
BUSTER_F_DECL bool buster_a64_semantic_alias_preference_condition_token(u32 form_id, u32 ordinal, BusterA64SemanticString* result);
BUSTER_F_DECL bool buster_a64_semantic_alias_preference(u32 form_id, u32 ordinal, BusterA64SemanticAliasPreference* result);
BUSTER_F_DECL bool buster_a64_semantic_alias_preference_condition_token_by_id(u32 preference_id, u32 ordinal, BusterA64SemanticString* result);
BUSTER_F_DECL bool buster_a64_semantic_constraint(u32 form_id, BusterA64SemanticConstraint* result);
BUSTER_F_DECL bool buster_a64_semantic_constraint_feature_tag(u32 form_id, u32 ordinal, BusterA64SemanticString* result);
BUSTER_F_DECL bool buster_a64_semantic_constraint_program_token(u32 form_id, u32 ordinal, BusterA64SemanticString* result);

BUSTER_F_DECL bool buster_a64_semantic_find_form(String8 name, u32 ordinal, u32* id);
BUSTER_F_DECL bool buster_a64_semantic_find_mnemonic(String8 mnemonic, u32 ordinal, u32* id);
BUSTER_F_DECL bool buster_a64_semantic_validate(void);

/* Compatibility hook for build/import drivers that want a named check. */
BUSTER_F_DECL bool arm_a64_semantic_import_validate(void);
