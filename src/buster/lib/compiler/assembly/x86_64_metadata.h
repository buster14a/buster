#pragma once

#include <buster/lib/base.h>

#define BUSTER_X86_METADATA_MAX_OPERAND_SHAPE 32
#define BUSTER_X86_METADATA_ANY_U8 0xff

typedef enum BusterX86MetadataCoverageClass
{
    BUSTER_X86_METADATA_COVERAGE_DIRECT,
    BUSTER_X86_METADATA_COVERAGE_NORMALIZED,
    BUSTER_X86_METADATA_COVERAGE_NOT64,
    BUSTER_X86_METADATA_COVERAGE_PRIVILEGED,
    BUSTER_X86_METADATA_COVERAGE_RESERVED,
    BUSTER_X86_METADATA_COVERAGE_UNSUPPORTED_TOKEN,
    BUSTER_X86_METADATA_COVERAGE_UNCLASSIFIED,
    BUSTER_X86_METADATA_COVERAGE_COUNT,
} BusterX86MetadataCoverageClass;

typedef enum BusterX86MetadataPrefixKind
{
    BUSTER_X86_METADATA_PREFIX_LEGACY,
    BUSTER_X86_METADATA_PREFIX_REX,
    BUSTER_X86_METADATA_PREFIX_REX2,
    BUSTER_X86_METADATA_PREFIX_VEX,
    BUSTER_X86_METADATA_PREFIX_XOP,
    BUSTER_X86_METADATA_PREFIX_EVEX,
    BUSTER_X86_METADATA_PREFIX_COUNT,
} BusterX86MetadataPrefixKind;

typedef enum BusterX86MetadataEncoderFamily
{
    BUSTER_X86_METADATA_ENCODER_LEGACY,
    BUSTER_X86_METADATA_ENCODER_REX,
    BUSTER_X86_METADATA_ENCODER_REX2,
    BUSTER_X86_METADATA_ENCODER_VEX,
    BUSTER_X86_METADATA_ENCODER_XOP,
    BUSTER_X86_METADATA_ENCODER_EVEX,
    BUSTER_X86_METADATA_ENCODER_AMX,
    BUSTER_X86_METADATA_ENCODER_SYSTEM,
    BUSTER_X86_METADATA_ENCODER_COUNT,
} BusterX86MetadataEncoderFamily;

typedef enum BusterX86MetadataTestClass
{
    BUSTER_X86_METADATA_TEST_SCHEMA,
    BUSTER_X86_METADATA_TEST_PRIVILEGED_SCHEMA,
    BUSTER_X86_METADATA_TEST_NOT64_SCHEMA,
    BUSTER_X86_METADATA_TEST_CLASS_COUNT,
} BusterX86MetadataTestClass;

typedef enum BusterX86MetadataReason
{
    BUSTER_X86_METADATA_REASON_NONE,
    BUSTER_X86_METADATA_REASON_MODE_NOT64,
    BUSTER_X86_METADATA_REASON_CPL0,
    BUSTER_X86_METADATA_REASON_UNKNOWN_PATTERN_TOKEN,
    BUSTER_X86_METADATA_REASON_UNKNOWN_OPERAND_TOKEN,
    BUSTER_X86_METADATA_REASON_COUNT,
} BusterX86MetadataReason;

typedef enum BusterX86MetadataOperandKind
{
    BUSTER_X86_METADATA_OPERAND_NONE,
    BUSTER_X86_METADATA_OPERAND_REGISTER,
    BUSTER_X86_METADATA_OPERAND_MEMORY,
    BUSTER_X86_METADATA_OPERAND_IMMEDIATE,
    BUSTER_X86_METADATA_OPERAND_RELATIVE,
    BUSTER_X86_METADATA_OPERAND_ABSOLUTE,
    BUSTER_X86_METADATA_OPERAND_BASE,
    BUSTER_X86_METADATA_OPERAND_SEGMENT,
    BUSTER_X86_METADATA_OPERAND_ADDRESS_GENERATOR,
    BUSTER_X86_METADATA_OPERAND_PSEUDO,
    BUSTER_X86_METADATA_OPERAND_KIND_COUNT,
} BusterX86MetadataOperandKind;

enum
{
    BUSTER_X86_METADATA_ACCESS_READ = 1u << 0,
    BUSTER_X86_METADATA_ACCESS_WRITE = 1u << 1,
    BUSTER_X86_METADATA_ACCESS_COND = 1u << 2,
    BUSTER_X86_METADATA_ACCESS_SUPPRESSED = 1u << 3,
    BUSTER_X86_METADATA_ACCESS_IMPLICIT = 1u << 4,
};

typedef enum BusterX86MetadataMap
{
    BUSTER_X86_METADATA_MAP_LEGACY,
    BUSTER_X86_METADATA_MAP_0F,
    BUSTER_X86_METADATA_MAP_0F38,
    BUSTER_X86_METADATA_MAP_0F3A,
    BUSTER_X86_METADATA_MAP_4,
    BUSTER_X86_METADATA_MAP_5,
    BUSTER_X86_METADATA_MAP_6,
    BUSTER_X86_METADATA_MAP_7,
    BUSTER_X86_METADATA_MAP_X8,
    BUSTER_X86_METADATA_MAP_X9,
    BUSTER_X86_METADATA_MAP_XA,
    BUSTER_X86_METADATA_MAP_COUNT,
} BusterX86MetadataMap;

typedef enum BusterX86MetadataTupleKind
{
    BUSTER_X86_METADATA_TUPLE_NONE,
    BUSTER_X86_METADATA_TUPLE_FULL,
    BUSTER_X86_METADATA_TUPLE_HALF,
    BUSTER_X86_METADATA_TUPLE_QUARTER,
    BUSTER_X86_METADATA_TUPLE_EIGHTH,
    BUSTER_X86_METADATA_TUPLE_SCALAR,
    BUSTER_X86_METADATA_TUPLE_TUPLE1,
    BUSTER_X86_METADATA_TUPLE_TUPLE1_4X,
    BUSTER_X86_METADATA_TUPLE_TUPLE1_BYTE,
    BUSTER_X86_METADATA_TUPLE_TUPLE1_WORD,
    BUSTER_X86_METADATA_TUPLE_TUPLE2,
    BUSTER_X86_METADATA_TUPLE_TUPLE4,
    BUSTER_X86_METADATA_TUPLE_TUPLE8,
    BUSTER_X86_METADATA_TUPLE_COUNT,
} BusterX86MetadataTupleKind;

typedef enum BusterX86MetadataFieldSource
{
    BUSTER_X86_METADATA_FIELD_SOURCE_NONE,
    BUSTER_X86_METADATA_FIELD_SOURCE_REG,
    BUSTER_X86_METADATA_FIELD_SOURCE_RM,
    BUSTER_X86_METADATA_FIELD_SOURCE_VVVV,
    BUSTER_X86_METADATA_FIELD_SOURCE_MASK,
    BUSTER_X86_METADATA_FIELD_SOURCE_FIXED,
    BUSTER_X86_METADATA_FIELD_SOURCE_IMMEDIATE,
    BUSTER_X86_METADATA_FIELD_SOURCE_RELATIVE,
    BUSTER_X86_METADATA_FIELD_SOURCE_COUNT,
} BusterX86MetadataFieldSource;

enum
{
    BUSTER_X86_METADATA_MODE_16 = 1u << 0,
    BUSTER_X86_METADATA_MODE_32 = 1u << 1,
    BUSTER_X86_METADATA_MODE_64 = 1u << 2,
    BUSTER_X86_METADATA_MODE_NOT64 = 1u << 3,
    BUSTER_X86_METADATA_MODE_EA16 = 1u << 4,
    BUSTER_X86_METADATA_MODE_EA32 = 1u << 5,
    BUSTER_X86_METADATA_MODE_EA64 = 1u << 6,
    BUSTER_X86_METADATA_MODE_EANOT16 = 1u << 7,
};

enum
{
    BUSTER_X86_METADATA_FIELD_MODRM = 1u << 0,
    BUSTER_X86_METADATA_FIELD_SIB = 1u << 1,
    BUSTER_X86_METADATA_FIELD_VSIB = 1u << 2,
    BUSTER_X86_METADATA_FIELD_MEMORY = 1u << 3,
    BUSTER_X86_METADATA_FIELD_REGISTER = 1u << 4,
    BUSTER_X86_METADATA_FIELD_DISPLACEMENT = 1u << 5,
    BUSTER_X86_METADATA_FIELD_IMMEDIATE = 1u << 6,
    BUSTER_X86_METADATA_FIELD_RELATIVE = 1u << 7,
    BUSTER_X86_METADATA_FIELD_END = 1u << 8,
};

enum
{
    BUSTER_X86_METADATA_DECORATOR_MASK = 1u << 0,
    BUSTER_X86_METADATA_DECORATOR_ZEROING = 1u << 1,
    BUSTER_X86_METADATA_DECORATOR_BROADCAST = 1u << 2,
    BUSTER_X86_METADATA_DECORATOR_ROUNDING = 1u << 3,
    BUSTER_X86_METADATA_DECORATOR_SAE = 1u << 4,
};

enum
{
    BUSTER_X86_METADATA_APX = 1u << 0,
    BUSTER_X86_METADATA_APX_ND = 1u << 1,
    BUSTER_X86_METADATA_APX_NF = 1u << 2,
    BUSTER_X86_METADATA_APX_NDD = 1u << 3,
    BUSTER_X86_METADATA_APX_SCC = 1u << 4,
    BUSTER_X86_METADATA_APX_EGPR = 1u << 5,
};

enum
{
    BUSTER_X86_METADATA_AMX_TILE_REGISTER = 1u << 0,
    BUSTER_X86_METADATA_AMX_TILE_MEMORY = 1u << 1,
    BUSTER_X86_METADATA_AMX_TILE_ROW = 1u << 2,
    BUSTER_X86_METADATA_AMX_TILE_COLUMN = 1u << 3,
};

typedef struct BusterX86MetadataString BusterX86MetadataString;
struct BusterX86MetadataString
{
    // Logical offset into the generated string pool. The pool is chunked in
    // the generated ABI, so callers use buster_x86_metadata_string_byte()
    // rather than assuming one contiguous host pointer.
    u32 offset;
    u32 length;
};

typedef struct BusterX86MetadataOperand BusterX86MetadataOperand;
struct BusterX86MetadataOperand
{
    BusterX86MetadataString atom;
    BusterX86MetadataString width;
    u8 slot;
    u8 visible;
    u8 kind;
    u8 access;
    u8 field_source;
    u8 reserved[3];
};

typedef struct BusterX86MetadataForm BusterX86MetadataForm;
struct BusterX86MetadataForm
{
    // This is the generated snapshot row ID. It is bounds-stable only for the
    // checked-in snapshot; use stable_hash as the durable form identity.
    u32 id;
    u64 stable_hash;
    BusterX86MetadataString source;
    BusterX86MetadataString iclass;
    BusterX86MetadataString iform;
    BusterX86MetadataString isa_set;
    BusterX86MetadataString category;
    BusterX86MetadataString extension;
    BusterX86MetadataString attributes;
    BusterX86MetadataString cpl;
    BusterX86MetadataString exceptions;
    BusterX86MetadataString flags;
    BusterX86MetadataString disasm;
    BusterX86MetadataString disasm_intel;
    BusterX86MetadataString disasm_att;
    BusterX86MetadataString real_opcode;
    BusterX86MetadataString uname;
    BusterX86MetadataString comment;
    BusterX86MetadataString version;
    BusterX86MetadataString pattern;
    BusterX86MetadataString operands;
    BusterX86MetadataString operand_annotation;
    BusterX86MetadataString tuple;
    BusterX86MetadataString element_size;
    BusterX86MetadataString reason;
    u32 operand_first;
    u16 operand_count;
    u8 coverage_class;
    u8 encoder_family;
    u8 test_class;
    u8 prefix_kind;
    u8 map;
    u8 fixed_byte_count;
    u8 fixed_bytes[16];
    u8 mandatory_prefix;
    u16 field_flags;
    u16 decorator_flags;
    u16 apx_flags;
    u16 amx_flags;
    u16 mode_flags;
    u8 displacement_width;
    u8 displacement_scale;
    u8 immediate_width;
    u8 immediate_signed;
    u8 relocation_base;
    u8 tuple_kind;
    u32 tuple_offset;
    u32 element_size_offset;
    u32 token_count;
    u16 reason_id;
};

typedef struct BusterX86MetadataCoverage BusterX86MetadataCoverage;
struct BusterX86MetadataCoverage
{
    u32 id;
    u64 source_hash;
    BusterX86MetadataString source;
    u32 normalized_form_id;
    u8 coverage_class;
    u8 encoder_family;
    u8 test_class;
    u16 reason_id;
    BusterX86MetadataString reason;
};

typedef struct BusterX86MetadataCounts BusterX86MetadataCounts;
struct BusterX86MetadataCounts
{
    u32 total_form_count;
    u32 normalized_form_count;
    u32 coverage_count;
    u32 coverage_class_counts[BUSTER_X86_METADATA_COVERAGE_COUNT];
    u32 reason_counts[BUSTER_X86_METADATA_REASON_COUNT];
};

typedef enum BusterX86MetadataValidationError
{
    BUSTER_X86_METADATA_VALIDATION_NONE,
    BUSTER_X86_METADATA_VALIDATION_SCHEMA_VERSION,
    BUSTER_X86_METADATA_VALIDATION_COUNT,
    BUSTER_X86_METADATA_VALIDATION_STRING_OFFSET,
    BUSTER_X86_METADATA_VALIDATION_STRING_TERMINATION,
    BUSTER_X86_METADATA_VALIDATION_FORM_HASH,
    BUSTER_X86_METADATA_VALIDATION_COVERAGE_HASH,
    BUSTER_X86_METADATA_VALIDATION_ENUM,
    BUSTER_X86_METADATA_VALIDATION_OPERAND_RANGE,
    BUSTER_X86_METADATA_VALIDATION_COVERAGE_FORM_ID,
    BUSTER_X86_METADATA_VALIDATION_COVERAGE_SOURCE,
    BUSTER_X86_METADATA_VALIDATION_COVERAGE_REASON,
    BUSTER_X86_METADATA_VALIDATION_COVERAGE_CLASSIFICATION,
    BUSTER_X86_METADATA_VALIDATION_RESERVED,
    BUSTER_X86_METADATA_VALIDATION_INDEX_CAPACITY,
    BUSTER_X86_METADATA_VALIDATION_ERROR_COUNT,
} BusterX86MetadataValidationError;

typedef struct BusterX86MetadataValidationResult BusterX86MetadataValidationResult;
struct BusterX86MetadataValidationResult
{
    bool valid;
    BusterX86MetadataValidationError error;
    u32 index;
    u32 detail;
};

typedef struct BusterX86MetadataCandidateRange BusterX86MetadataCandidateRange;
struct BusterX86MetadataCandidateRange
{
    // The candidate IDs are sorted by generated snapshot form ID inside the
    // range. first is an immutable generated-index offset. Snapshot IDs are
    // not durable across regeneration; use each form's stable_hash for that.
    u32 first;
    u32 count;
    u8 index_kind;
    u8 reserved[3];
};

typedef struct BusterX86MetadataCoverageRange BusterX86MetadataCoverageRange;
struct BusterX86MetadataCoverageRange
{
    // These are raw coverage-row IDs, sorted by coverage row ID. They are not
    // normalized form IDs and must be passed to buster_x86_metadata_coverage.
    u32 first;
    u32 count;
};

typedef struct BusterX86MetadataOperandShape BusterX86MetadataOperandShape;
struct BusterX86MetadataOperandShape
{
    u8 kind;
    u8 visible;
    u8 reserved[2];
};

typedef struct BusterX86MetadataFilter BusterX86MetadataFilter;
struct BusterX86MetadataFilter
{
    bool require_64_bit;
    bool privileged_only;
    bool exclude_privileged;
    bool exclude_not64;
    bool exclude_reserved;
    bool exclude_unsupported_token;
    bool has_coverage_class_mask;
    bool has_operand_count;
    bool has_visible_operand_count;
    bool has_prefix_kind;
    bool has_encoder_family;
    bool has_isa_set;
    u8 coverage_class_mask;
    u8 prefix_kind;
    u8 encoder_family;
    u16 operand_count;
    u16 visible_operand_count;
    u16 operand_shape_count;
    BusterX86MetadataString isa_set;
    BusterX86MetadataOperandShape operand_shape[BUSTER_X86_METADATA_MAX_OPERAND_SHAPE];
};

typedef struct BusterX86MetadataCandidateIterator BusterX86MetadataCandidateIterator;
struct BusterX86MetadataCandidateIterator
{
    BusterX86MetadataCandidateRange candidates;
    BusterX86MetadataFilter filter;
    u32 position;
};

BUSTER_F_DECL u32 buster_x86_metadata_schema_version(void);
BUSTER_F_DECL u32 buster_x86_metadata_form_count(void);
BUSTER_F_DECL u32 buster_x86_metadata_normalized_form_count(void);
BUSTER_F_DECL u32 buster_x86_metadata_coverage_count(void);
BUSTER_F_DECL u32 buster_x86_metadata_operand_count(void);
BUSTER_F_DECL u32 buster_x86_metadata_string_pool_size(void);
BUSTER_F_DECL BusterX86MetadataCounts buster_x86_metadata_counts(void);
BUSTER_F_DECL bool buster_x86_metadata_validate(BusterX86MetadataValidationResult* result);
BUSTER_F_DECL bool buster_x86_metadata_string(u32 offset, BusterX86MetadataString* result);
BUSTER_F_DECL u8 buster_x86_metadata_string_byte(BusterX86MetadataString string, u32 index);
BUSTER_F_DECL bool buster_x86_metadata_form(u32 form_id, BusterX86MetadataForm* result);
BUSTER_F_DECL bool buster_x86_metadata_operand(u32 form_id, u32 operand_index, BusterX86MetadataOperand* result);
BUSTER_F_DECL bool buster_x86_metadata_coverage(u32 coverage_id, BusterX86MetadataCoverage* result);
// Mnemonic lookup is ASCII case-insensitive and uses the first token of each
// Intel, AT&T, or generic disassembly spelling; dialect aliases share a range.
// Iclass and iform lookup are separate exact normalized indexes.
BUSTER_F_DECL BusterX86MetadataCandidateRange buster_x86_metadata_lookup_mnemonic(String8 mnemonic);
BUSTER_F_DECL BusterX86MetadataCandidateRange buster_x86_metadata_lookup_iclass(String8 iclass);
BUSTER_F_DECL BusterX86MetadataCandidateRange buster_x86_metadata_lookup_iform(String8 iform);
BUSTER_F_DECL BusterX86MetadataCandidateRange buster_x86_metadata_lookup_form_hash(u64 stable_hash);
BUSTER_F_DECL BusterX86MetadataCoverageRange buster_x86_metadata_lookup_coverage_hash(u64 source_hash);
BUSTER_F_DECL BusterX86MetadataCandidateIterator buster_x86_metadata_filter(BusterX86MetadataCandidateRange candidates,
                                                                            BusterX86MetadataFilter filter);
BUSTER_F_DECL bool buster_x86_metadata_candidate_at(BusterX86MetadataCandidateRange candidates, u32 position, u32* form_id);
BUSTER_F_DECL bool buster_x86_metadata_coverage_candidate_at(BusterX86MetadataCoverageRange candidates, u32 position,
                                                               u32* coverage_id);
BUSTER_F_DECL bool buster_x86_metadata_candidate_next(BusterX86MetadataCandidateIterator* iterator, u32* form_id);

#if BUSTER_INCLUDE_TESTS
typedef enum BusterX86MetadataValidationPatchKind
{
    BUSTER_X86_METADATA_PATCH_FORM_SOURCE_OFFSET,
    BUSTER_X86_METADATA_PATCH_FORM_ICLASS_OFFSET,
    BUSTER_X86_METADATA_PATCH_FORM_STABLE_HASH,
    BUSTER_X86_METADATA_PATCH_FORM_OPERAND_RANGE,
    BUSTER_X86_METADATA_PATCH_FORM_COVERAGE_CLASS,
    BUSTER_X86_METADATA_PATCH_FORM_PREFIX_KIND,
    BUSTER_X86_METADATA_PATCH_FORM_RESERVED,
    BUSTER_X86_METADATA_PATCH_FORM_RESERVED2,
    BUSTER_X86_METADATA_PATCH_OPERAND_RESERVED,
    BUSTER_X86_METADATA_PATCH_COVERAGE_SOURCE_HASH,
    BUSTER_X86_METADATA_PATCH_COVERAGE_FORM_ID,
    BUSTER_X86_METADATA_PATCH_COVERAGE_CLASS,
    BUSTER_X86_METADATA_PATCH_COVERAGE_SOURCE_OFFSET,
    BUSTER_X86_METADATA_PATCH_COVERAGE_REASON_OFFSET,
    BUSTER_X86_METADATA_PATCH_COVERAGE_REASON_ID,
    BUSTER_X86_METADATA_PATCH_COVERAGE_ENCODER_FAMILY,
    BUSTER_X86_METADATA_PATCH_COVERAGE_TEST_CLASS,
    BUSTER_X86_METADATA_PATCH_INDEX_CAPACITY,
    BUSTER_X86_METADATA_PATCH_COUNT,
} BusterX86MetadataValidationPatchKind;

typedef struct BusterX86MetadataValidationPatch BusterX86MetadataValidationPatch;
struct BusterX86MetadataValidationPatch
{
    BusterX86MetadataValidationPatchKind kind;
    u32 index;
    u64 value;
};

BUSTER_TEST_F_DECL bool buster_x86_metadata_validate_patch(BusterX86MetadataValidationPatch patch,
                                                            BusterX86MetadataValidationResult* result);
#endif
