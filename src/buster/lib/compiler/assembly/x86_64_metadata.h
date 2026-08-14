#pragma once

#include <buster/lib/base.h>

#define BUSTER_X86_METADATA_MAX_OPERAND_SHAPE 32
#define BUSTER_X86_METADATA_ANY_U8 0xff
#define BUSTER_X86_METADATA_OPERAND_ANY BUSTER_X86_METADATA_ANY_U8
#define BUSTER_X86_METADATA_ADDRESS_SIZE_ANY 0

typedef enum BusterX86MetadataCoverageClass
{
    BUSTER_X86_METADATA_COVERAGE_DIRECT,
    BUSTER_X86_METADATA_COVERAGE_NORMALIZED,
    BUSTER_X86_METADATA_COVERAGE_NOT64,
    BUSTER_X86_METADATA_COVERAGE_PRIVILEGED,
    BUSTER_X86_METADATA_COVERAGE_RESERVED,
    BUSTER_X86_METADATA_COVERAGE_UNSUPPORTED_TOKEN,
    BUSTER_X86_METADATA_COVERAGE_UNCLASSIFIED,
    BUSTER_X86_METADATA_COVERAGE_DECODE_ALIAS,
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
    BUSTER_X86_METADATA_TEST_DECODE_ALIAS_SCHEMA,
    BUSTER_X86_METADATA_TEST_CLASS_COUNT,
} BusterX86MetadataTestClass;

typedef enum BusterX86MetadataReason
{
    BUSTER_X86_METADATA_REASON_NONE,
    BUSTER_X86_METADATA_REASON_MODE_NOT64,
    BUSTER_X86_METADATA_REASON_CPL0,
    BUSTER_X86_METADATA_REASON_UNKNOWN_PATTERN_TOKEN,
    BUSTER_X86_METADATA_REASON_UNKNOWN_OPERAND_TOKEN,
    BUSTER_X86_METADATA_REASON_DECODE_ALIAS,
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

typedef enum BusterX86MetadataPhysicalClass
{
    BUSTER_X86_METADATA_PHYSICAL_CLASS_NONE,
    // The compact snapshot keeps the source atom but does not publish a
    // generated physical class for every spelling.  UNKNOWN is distinct from
    // SPECIAL so callers can tell a known architectural pseudo-register from
    // an unclassified token and refine the latter with has_atom.
    BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_BND,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_IMMEDIATE,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_RELATIVE,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_ABSOLUTE,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_BASE,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_ADDRESS_GENERATOR,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_PSEUDO,
    BUSTER_X86_METADATA_PHYSICAL_CLASS_COUNT,
} BusterX86MetadataPhysicalClass;

enum
{
    BUSTER_X86_METADATA_PHYSICAL_CLASS_ANY = BUSTER_X86_METADATA_ANY_U8,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_8 = 1u << 0,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_16 = 1u << 1,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_32 = 1u << 2,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_64 = 1u << 3,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_80 = 1u << 4,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_128 = 1u << 5,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_256 = 1u << 6,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_512 = 1u << 7,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_1024 = 1u << 8,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_UNKNOWN = 1u << 15,
    BUSTER_X86_METADATA_PHYSICAL_WIDTH_ANY = 0,
};

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

typedef enum BusterX86MetadataExecutionMode
{
    // The normal resolver mode.  This API describes x86-64 instruction
    // candidates; include_not64 is an explicit opt-in for inspecting rows
    // classified NOT64, while declared MODE16/MODE32 rows use their typed
    // execution modes below.
    BUSTER_X86_METADATA_EXECUTION_MODE_64,
    // An explicit inspection mode.  Explicit NOT64 rows still require
    // include_not64; this value only says not to prefer the MODE_64 bit when
    // inspecting a deliberately widened snapshot query.
    BUSTER_X86_METADATA_EXECUTION_MODE_ANY,
    // Legacy execution modes are explicit inspection/encoding modes.  They
    // select forms whose XED row is declared MODE_16 or MODE_32 without
    // widening those rows into the x86-64 front door.
    BUSTER_X86_METADATA_EXECUTION_MODE_16,
    BUSTER_X86_METADATA_EXECUTION_MODE_32,
    BUSTER_X86_METADATA_EXECUTION_MODE_COUNT,
} BusterX86MetadataExecutionMode;

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
    // the generated ABI, but the one-time decode flattens it into a
    // contiguous host array, so prefer buster_x86_metadata_string_span() and
    // scan the bytes directly; buster_x86_metadata_string_byte() remains for
    // one-off reads and costs a call per byte.
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
    // These are normalized views derived from the imported XED operand
    // vocabulary.  A zero/UNKNOWN width is deliberate when the source token
    // is symbolic; callers can still match the physical class without
    // guessing an operand-size choice.
    u8 physical_class;
    u16 physical_width_flags;
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

// A form key combines the snapshot-local dense row ID with the durable hash
// emitted from the imported XED source.  Callers that persist an exact form
// must retain both fields: the ID permits direct table access while the hash
// prevents a stale key from silently selecting a different row after a
// metadata regeneration.
typedef struct BusterX86MetadataFormKey BusterX86MetadataFormKey;
struct BusterX86MetadataFormKey
{
    u32 form_id;
    u64 stable_hash;
};
BUSTER_CT_CHECK(sizeof(BusterX86MetadataFormKey) == 16);

// Short aliases mirror the A64 exact bridge naming used by backend clients
// while keeping the architecture-qualified type as the canonical ABI name.
typedef BusterX86MetadataFormKey X64ExactFormKey;

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
    BUSTER_X86_METADATA_VALIDATION_ENCODING_FIELDS,
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

typedef struct BusterX86MetadataOperandSignature BusterX86MetadataOperandSignature;
struct BusterX86MetadataOperandSignature
{
    // Resolution consumes operands in generated metadata order.  By default
    // only visible operands are considered; set include_implicit on the query
    // when the caller intentionally supplies the complete metadata sequence.
    // An immediate/relative signature with no physical width is intentionally
    // retained across symbolic width families; value-range checking and final
    // encoding selection are separate later stages.
    String8 atom;
    String8 width;
    u8 kind;
    u8 field_source;
    u8 access;
    u8 slot;
    u8 physical_class;
    u16 physical_width_flags;
    bool has_atom;
    bool has_width;
    bool has_field_source;
    bool has_access;
    bool has_slot;
    bool has_physical_class;
    bool has_physical_width;
    bool has_visible;
    u8 visible;
    u8 reserved[2];
};

typedef struct BusterX86MetadataFeatureInput BusterX86MetadataFeatureInput;
struct BusterX86MetadataFeatureInput
{
    // Names are borrowed and never modified.  Matching is case-insensitive.
    // Raw snapshot ISA names remain accepted for compatibility, while known
    // composite ISA names are conservatively mapped to the canonical target
    // feature spellings (for example AMX_TILE -> amx-tile and AVX10.2 forms
    // require both avx10.2 and their specific auxiliary feature).  A literal
    // "*" explicitly accepts any feature and is intended only for callers
    // that deliberately bypass target-feature filtering.
    String8 const* names;
    u32 count;
};

typedef enum BusterX86MetadataResolveStatus
{
    BUSTER_X86_METADATA_RESOLVE_INVALID_INPUT,
    BUSTER_X86_METADATA_RESOLVE_UNKNOWN_MNEMONIC,
    BUSTER_X86_METADATA_RESOLVE_WRONG_OPERAND_COUNT,
    BUSTER_X86_METADATA_RESOLVE_EXECUTION_MODE_MISMATCH,
    BUSTER_X86_METADATA_RESOLVE_OPERAND_CLASS_WIDTH_MISMATCH,
    BUSTER_X86_METADATA_RESOLVE_ADDRESSING_FIELD_MISMATCH,
    BUSTER_X86_METADATA_RESOLVE_UNSUPPORTED_DECORATOR,
    BUSTER_X86_METADATA_RESOLVE_UNAVAILABLE_TARGET_FEATURE,
    BUSTER_X86_METADATA_RESOLVE_AMBIGUOUS_OR_UNSUPPORTED_METADATA,
    BUSTER_X86_METADATA_RESOLVE_OUTPUT_CAPACITY,
    BUSTER_X86_METADATA_RESOLVE_SUCCESS,
    BUSTER_X86_METADATA_RESOLVE_STATUS_COUNT,
} BusterX86MetadataResolveStatus;

typedef struct BusterX86MetadataResolveQuery BusterX86MetadataResolveQuery;
struct BusterX86MetadataResolveQuery
{
    String8 mnemonic;
    BusterX86MetadataOperandSignature const* operands;
    u32 operand_count;
    BusterX86MetadataFeatureInput features;
    u16 decorator_flags;
    u16 apx_flags;
    u16 amx_flags;
    // These are exact required/forbidden bits from the generated field_flags
    // contract.  They cover MODRM/SIB/VSIB, memory/register, displacement,
    // immediate, relative, and FIELD_END; no addressing property is inferred
    // from address_size alone.
    u16 required_field_flags;
    u16 forbidden_field_flags;
    u8 address_size;
    u8 execution_mode;
    bool include_implicit;
    bool include_privileged;
    bool include_not64;
    u8 reserved;
};

typedef struct BusterX86MetadataResolveResult BusterX86MetadataResolveResult;
struct BusterX86MetadataResolveResult
{
    BusterX86MetadataResolveStatus status;
    u32* form_ids;
    u32 form_id_capacity;
    u32 candidate_count;
    u32 required_candidate_count;
    BusterX86MetadataCandidateRange mnemonic_candidates;
    BusterX86MetadataString required_feature;
    u16 unsupported_decorator_flags;
    u16 unsupported_apx_flags;
    u16 unsupported_amx_flags;
    u16 reserved;
};

// The physical API is deliberately separate from OperandSignature.  A
// signature is useful for allocation-free candidate filtering; these values
// carry the architectural bits needed to select and emit one exact form.
typedef enum BusterX86MetadataPhysicalOperandKind
{
    BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
    BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
    BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
    BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
    BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE,
    BUSTER_X86_METADATA_PHYSICAL_OPERAND_KIND_COUNT,
} BusterX86MetadataPhysicalOperandKind;

typedef enum BusterX86MetadataPhysicalSegment
{
    BUSTER_X86_METADATA_SEGMENT_NONE,
    BUSTER_X86_METADATA_SEGMENT_ES,
    BUSTER_X86_METADATA_SEGMENT_CS,
    BUSTER_X86_METADATA_SEGMENT_SS,
    BUSTER_X86_METADATA_SEGMENT_DS,
    BUSTER_X86_METADATA_SEGMENT_FS,
    BUSTER_X86_METADATA_SEGMENT_GS,
    BUSTER_X86_METADATA_SEGMENT_COUNT,
} BusterX86MetadataPhysicalSegment;

typedef struct BusterX86MetadataPhysicalRegister BusterX86MetadataPhysicalRegister;
struct BusterX86MetadataPhysicalRegister
{
    // index is the architectural register number within physical_class.  For
    // control/debug/segment/special registers it is the architectural number
    // as well, rather than a host register encoding.
    u16 index;
    u16 width;
    u8 physical_class;
    bool high_byte;
    u8 reserved[3];
};

typedef struct BusterX86MetadataPhysicalMemory BusterX86MetadataPhysicalMemory;
struct BusterX86MetadataPhysicalMemory
{
    BusterX86MetadataPhysicalRegister base;
    BusterX86MetadataPhysicalRegister index;
    s64 displacement;
    s64 addend;
    String8 symbol;
    u8 address_size;
    u8 scale;
    u8 segment;
    bool has_base;
    bool has_index;
    bool has_displacement;
    bool rip_relative;
    bool has_symbol;
    bool has_segment;
    bool vsib;
    // Source-level aggregate qualifiers (xmmword/ymmword/zmmword) are
    // distinct from the scalar element width used by the encoding schema.
    u16 source_width;
    u8 reserved[1];
};

typedef struct BusterX86MetadataPhysicalOperand BusterX86MetadataPhysicalOperand;
struct BusterX86MetadataPhysicalOperand
{
    BusterX86MetadataPhysicalOperandKind kind;
    u16 width;
    u8 reserved[1];
    BusterX86MetadataPhysicalRegister reg;
    BusterX86MetadataPhysicalMemory memory;
    // For immediate/relative/absolute operands, value is a signed
    // mathematical value when has_value is true.  Unsigned encodings may
    // instead use unsigned_value with has_unsigned_value true; that field is
    // a complete u64 bit pattern and is not obtained by converting through
    // s64.  A symbol is the third, mutually exclusive, value state.
    s64 value;
    u64 unsigned_value;
    s64 addend;
    String8 symbol;
    bool has_symbol;
    bool has_value;
    bool has_unsigned_value;
    u8 reserved2[5];
};

typedef enum BusterX86MetadataRoundingMode
{
    BUSTER_X86_METADATA_ROUNDING_NONE,
    BUSTER_X86_METADATA_ROUNDING_NEAREST,
    BUSTER_X86_METADATA_ROUNDING_DOWN,
    BUSTER_X86_METADATA_ROUNDING_UP,
    BUSTER_X86_METADATA_ROUNDING_ZERO,
    BUSTER_X86_METADATA_ROUNDING_COUNT,
} BusterX86MetadataRoundingMode;

// Legacy x86 conditional branches accept CS/DS segment-prefix spellings as
// static branch hints.  Keep this as a typed control rather than folding it
// into the unrelated REP/segment-memory attributes: NONE emits the ordinary
// branch, NOT_TAKEN emits CS (2e), and TAKEN emits DS (3e).
typedef enum BusterX86MetadataBranchHint
{
    BUSTER_X86_METADATA_BRANCH_HINT_NONE,
    BUSTER_X86_METADATA_BRANCH_HINT_NOT_TAKEN,
    BUSTER_X86_METADATA_BRANCH_HINT_TAKEN,
    BUSTER_X86_METADATA_BRANCH_HINT_COUNT,
} BusterX86MetadataBranchHint;

typedef struct BusterX86MetadataPhysicalAttributes BusterX86MetadataPhysicalAttributes;
struct BusterX86MetadataPhysicalAttributes
{
    u16 decorator_flags;
    u16 apx_flags;
    u16 amx_flags;
    u8 mask_register;
    u8 broadcast_elements;
    u8 rounding_mode;
    bool has_mask_register;
    bool zeroing;
    bool sae;
    bool no_flags;
    bool lock;
    bool rep;
    bool repne;
    // A segment prefix attached to a form's hidden/suppressed memory
    // operand.  This is distinct from PhysicalMemory.segment, which models
    // an explicitly written memory operand.
    u8 implicit_segment;
    u8 branch_hint;
    // CET indirect-branch tracking is a typed source/query prefix.  It uses
    // the legacy 0x3e byte, but is distinct from the conditional branch-hint
    // interpretation of that byte.
    bool notrack;
    u8 dfv;
    bool has_dfv;
};

typedef struct BusterX86MetadataPhysicalQuery BusterX86MetadataPhysicalQuery;
struct BusterX86MetadataPhysicalQuery
{
    String8 mnemonic;
    BusterX86MetadataPhysicalOperand const* operands;
    u32 operand_count;
    BusterX86MetadataFeatureInput features;
    BusterX86MetadataPhysicalAttributes attributes;
    u8 address_size;
    u8 execution_mode;
    bool include_privileged;
    bool include_not64;
    bool include_implicit;
    bool source_semantics;
    u8 reserved;
};

typedef enum BusterX86MetadataEncodeStatus
{
    BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
    BUSTER_X86_METADATA_ENCODE_UNKNOWN_MNEMONIC,
    BUSTER_X86_METADATA_ENCODE_UNKNOWN_FORM,
    BUSTER_X86_METADATA_ENCODE_WRONG_OPERAND_COUNT,
    BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH,
    BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE,
    BUSTER_X86_METADATA_ENCODE_REGISTER_ENCODING,
    BUSTER_X86_METADATA_ENCODE_HIGH_BYTE_WITH_REX,
    BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION,
    BUSTER_X86_METADATA_ENCODE_ADDRESSING,
    BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE,
    BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE,
    BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE,
    BUSTER_X86_METADATA_ENCODE_DECORATOR,
    BUSTER_X86_METADATA_ENCODE_MISSING_SCHEMA,
    BUSTER_X86_METADATA_ENCODE_AMBIGUOUS,
    BUSTER_X86_METADATA_ENCODE_INSTRUCTION_LENGTH,
    BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY,
    BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY,
    BUSTER_X86_METADATA_ENCODE_SUCCESS,
    BUSTER_X86_METADATA_ENCODE_INVALID_EXPRESSION,
    BUSTER_X86_METADATA_ENCODE_STATUS_COUNT,
} BusterX86MetadataEncodeStatus;

typedef struct BusterX86MetadataSelectResult BusterX86MetadataSelectResult;
struct BusterX86MetadataSelectResult
{
    BusterX86MetadataEncodeStatus status;
    u32 form_id;
    u64 stable_hash;
    u32 candidate_count;
    u32 selected_byte_count;
    u32 diagnostic_operand;
    s64 diagnostic_value;
    BusterX86MetadataString required_feature;
};

typedef enum BusterX86MetadataRelocationKind
{
    BUSTER_X86_METADATA_RELOCATION_ABSOLUTE8,
    BUSTER_X86_METADATA_RELOCATION_ABSOLUTE16,
    BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32,
    BUSTER_X86_METADATA_RELOCATION_ABSOLUTE64,
    // A 32-bit absolute address field has different downstream semantics in
    // 64-bit addressing (sign extension) and 32-bit addressing
    // (zero extension).  Keep the generic ABSOLUTE32 kind for ordinary
    // immediate fields, and use these two for address displacements.
    BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_SIGN_EXTENDED,
    BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_ZERO_EXTENDED,
    BUSTER_X86_METADATA_RELOCATION_PC8,
    BUSTER_X86_METADATA_RELOCATION_PC16,
    BUSTER_X86_METADATA_RELOCATION_PC32,
    BUSTER_X86_METADATA_RELOCATION_PC64,
    BUSTER_X86_METADATA_RELOCATION_KIND_COUNT,
} BusterX86MetadataRelocationKind;

typedef struct BusterX86MetadataRelocation BusterX86MetadataRelocation;
enum
{
    BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY = 8,
};

struct BusterX86MetadataRelocation
{
    u32 offset;
    u8 width;
    u8 kind;
    u16 reserved;
    s64 addend;
    String8 symbol;
};

typedef struct BusterX86MetadataEmitQuery BusterX86MetadataEmitQuery;
struct BusterX86MetadataEmitQuery
{
    BusterX86MetadataPhysicalQuery physical;
    u32 form_id;
    u8* output;
    u32 output_capacity;
    BusterX86MetadataRelocation* relocations;
    u32 relocation_capacity;
};

// Compiler-facing exact emission keeps the durable form identity and the
// physical values in one query object.  Unlike BusterX86MetadataEmitQuery it
// has no source mnemonic or duplicate dense form ID: the exact emitter fills
// the internal canonical mnemonic from `key` and validates the row digest
// before encoding.  Operand, feature-name, symbol, output, and relocation
// storage are all borrowed for the duration of the call; relocation records
// remain format-neutral until a downstream object/assembly adapter consumes
// them.
typedef struct BusterX86MetadataExactQuery BusterX86MetadataExactQuery;
struct BusterX86MetadataExactQuery
{
    BusterX86MetadataFormKey key;
    BusterX86MetadataPhysicalOperand const* operands;
    u32 operand_count;
    BusterX86MetadataFeatureInput features;
    BusterX86MetadataPhysicalAttributes attributes;
    u8 address_size;
    u8 execution_mode;
    bool include_privileged;
    bool include_not64;
    bool include_implicit;
    u8 reserved;
    u8* output;
    u32 output_capacity;
    BusterX86MetadataRelocation* relocations;
    u32 relocation_capacity;
};

// A plan is a serially validated exact-form identity.  It is intentionally a
// dense value rather than a heap/arena handle: the metadata module resolves it
// to immutable process-lifetime state after prewarm, while the stable hash
// keeps stale generated snapshots from selecting a different row.
typedef struct BusterX86MetadataExactPlan BusterX86MetadataExactPlan;
struct BusterX86MetadataExactPlan
{
    u32 form_id;
    u64 stable_hash;
};
BUSTER_CT_CHECK(sizeof(BusterX86MetadataExactPlan) == 16);

// A compact opaque handle for a machine exact plan that was resolved during
// the serial prewarm phase.  Callers must obtain this value from
// buster_x86_metadata_machine_exact_token_for_plan(); its representation is
// not part of the metadata ABI and must not be inspected or modified.
typedef struct BusterX86MetadataMachineExactToken BusterX86MetadataMachineExactToken;
struct BusterX86MetadataMachineExactToken
{
    u16 slot_plus_one;
    u8 policy_flags;
    u8 integrity;
};
BUSTER_CT_CHECK(sizeof(BusterX86MetadataMachineExactToken) == 4);

// The machine exact bridge has no source/query policy fields: these values
// are fixed and validated when its token is prepared.  Operands and output
// storage remain borrowed for the duration of one emission call.
typedef struct BusterX86MetadataMachineExactQuery BusterX86MetadataMachineExactQuery;
struct BusterX86MetadataMachineExactQuery
{
    BusterX86MetadataPhysicalOperand const* operands;
    u32 operand_count;
    u8* output;
    u32 output_capacity;
    BusterX86MetadataRelocation* relocations;
    u32 relocation_capacity;
};

typedef struct BusterX86MetadataEmitResult BusterX86MetadataEmitResult;
struct BusterX86MetadataEmitResult
{
    BusterX86MetadataEncodeStatus status;
    u32 form_id;
    u64 stable_hash;
    u32 byte_count;
    u32 relocation_count;
    u32 required_byte_count;
    u32 required_relocation_count;
    u32 diagnostic_operand;
    s64 diagnostic_value;
    BusterX86MetadataString required_feature;
};

typedef enum BusterX86MetadataCoverageDisposition
{
    BUSTER_X86_METADATA_COVERAGE_EMITTED,
    BUSTER_X86_METADATA_COVERAGE_BLOCKED,
    BUSTER_X86_METADATA_COVERAGE_DISPOSITION_COUNT,
} BusterX86MetadataCoverageDisposition;

typedef enum BusterX86MetadataCoverageBlocker
{
    BUSTER_X86_METADATA_BLOCKER_NONE,
    BUSTER_X86_METADATA_BLOCKER_NOT64,
    BUSTER_X86_METADATA_BLOCKER_PRIVILEGED,
    BUSTER_X86_METADATA_BLOCKER_PATTERN_SEMANTICS,
    BUSTER_X86_METADATA_BLOCKER_OPCODE_FIELDS,
    BUSTER_X86_METADATA_BLOCKER_PREFIX_FIELDS,
    BUSTER_X86_METADATA_BLOCKER_ADDRESSING_FIELDS,
    BUSTER_X86_METADATA_BLOCKER_IMMEDIATE_FIELDS,
    BUSTER_X86_METADATA_BLOCKER_DECORATOR_FIELDS,
    BUSTER_X86_METADATA_BLOCKER_OPERAND_SEMANTICS,
    BUSTER_X86_METADATA_BLOCKER_RESERVED_SNAPSHOT,
    BUSTER_X86_METADATA_BLOCKER_UNCLASSIFIED,
    BUSTER_X86_METADATA_BLOCKER_DECODE_ALIAS,
    BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT,
} BusterX86MetadataCoverageBlocker;

typedef struct BusterX86MetadataCoverageLedgerEntry BusterX86MetadataCoverageLedgerEntry;
struct BusterX86MetadataCoverageLedgerEntry
{
    u32 form_id;
    u64 stable_hash;
    u8 coverage_class;
    u8 encoder_family;
    u8 disposition;
    u8 blocker;
    // Policy exclusion is kept separate from encoding capability.  A
    // privileged row may be byte-emittable when the caller opts in even
    // though ordinary x86-64 queries must reject it.
    bool encoder_capable;
    bool policy_excluded;
    u8 reserved[2];
};

typedef struct BusterX86MetadataCoverageAuditResult BusterX86MetadataCoverageAuditResult;
struct BusterX86MetadataCoverageAuditResult
{
    bool complete;
    bool duplicate_form_id;
    bool duplicate_stable_hash;
    u8 reserved[2];
    u32 required_entry_count;
    u32 entry_count;
    u32 normalized_entry_count;
    u32 emitted_count;
    u32 blocked_count;
    u32 encoder_capable_count;
    u32 policy_excluded_count;
    u32 explicitly_unsupported_count;
    u32 schema_inexpressible_count;
    u32 disposition_counts[BUSTER_X86_METADATA_COVERAGE_DISPOSITION_COUNT];
    u32 blocker_counts[BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT];
    u32 family_counts[BUSTER_X86_METADATA_ENCODER_COUNT];
    u32 family_emitted_counts[BUSTER_X86_METADATA_ENCODER_COUNT];
    u32 family_blocked_counts[BUSTER_X86_METADATA_ENCODER_COUNT];
};

// Decodes the generated tables and fills every demand-filled cache over them,
// on the calling thread. Call before lane_run: the module's queries otherwise
// write those caches on first use and read them back with plain loads, which
// is only sound while nothing else can be reading. A gang that reaches one
// unwarmed reports through BUSTER_CHECK_SERIAL_INITIALIZATION instead of
// racing. The first call costs a full decode plus one normalization and
// pattern parse per form; later calls are idempotent no-ops, so a program that
// never queries x86 metadata should not call it.
BUSTER_F_DECL void buster_x86_metadata_prewarm(void);
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
// Contiguous view of a pool string, valid for the life of the process. Empty
// when the record is out of range. Prefer this to a per-byte loop over
// buster_x86_metadata_string_byte(), which costs a call and a decode-guard
// test for every byte examined.
BUSTER_F_DECL String8 buster_x86_metadata_string_span(BusterX86MetadataString string);
BUSTER_F_DECL bool buster_x86_metadata_form(u32 form_id, BusterX86MetadataForm* result);
// Forms addressed through a key validate the dense ID against its durable
// source hash before exposing the row or entering the exact emitter.
BUSTER_F_DECL bool buster_x86_metadata_form_key(u32 form_id, BusterX86MetadataFormKey* result);
BUSTER_F_DECL bool buster_x86_metadata_form_key_from_id(u32 form_id, BusterX86MetadataFormKey* result);
BUSTER_F_DECL bool buster_x86_metadata_form_key_valid(BusterX86MetadataFormKey key);
BUSTER_F_DECL bool buster_x86_metadata_form_key_from_stable_hash(u64 stable_hash, BusterX86MetadataFormKey* result);
BUSTER_F_DECL bool buster_x86_metadata_lookup_form_key(BusterX86MetadataFormKey key, BusterX86MetadataForm* result);
// Returns whether the generated form is one of the four legacy MOV moffs
// encodings (A0-A3).  The pattern details remain private to the metadata
// decoder; callers only need this classification when deciding whether a
// metadata-selected form is a source-level extension.
BUSTER_F_DECL bool buster_x86_metadata_form_is_moffs(u32 form_id);
// The normalized XED iform carries DFV as a source role while generated
// visible operands intentionally omit it.  The source assembler uses this
// cross-translation-unit query to expose that schema fact without mnemonic
// exceptions.
BUSTER_F_DECL bool buster_x86_metadata_form_requires_dfv(u32 form_id);
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
// Resolves a typed signature to every matching snapshot-local form ID in
// generated canonical order. This filters candidates only; it does not choose
// an encoding. Use each returned form's stable_hash for durable identity.
BUSTER_F_DECL BusterX86MetadataResolveResult buster_x86_metadata_resolve(BusterX86MetadataResolveQuery query, u32* form_ids,
                                                                          u32 form_id_capacity);
// Select evaluates the candidate forms in metadata order and chooses the
// shortest valid encoding, breaking equal lengths by snapshot form ID.  It
// does not allocate and does not retain any operand or feature storage.
BUSTER_F_DECL BusterX86MetadataSelectResult buster_x86_metadata_select_form(BusterX86MetadataPhysicalQuery query);
// Emit encodes one form selected from the same physical query.  The output
// and relocation arrays are caller-owned; symbols in relocations are borrowed
// from the query and remain format-neutral.
BUSTER_F_DECL BusterX86MetadataEmitResult buster_x86_metadata_emit_form(BusterX86MetadataEmitQuery query);
// Trusted exact-form emission bypasses mnemonic lookup, candidate scanning,
// and source-level mnemonic/operand diagnostics.  Physical operand shape,
// address/immediate ranges, output capacities, instruction length, feature
// policy, and relocation validation remain in the shared encoder transform.
BUSTER_F_DECL BusterX86MetadataEmitResult buster_x86_metadata_emit_form_exact(BusterX86MetadataEmitQuery query,
                                                                                BusterX86MetadataFormKey key);
BUSTER_F_DECL BusterX86MetadataEmitResult buster_x86_metadata_emit_form_key(BusterX86MetadataEmitQuery query,
                                                                              BusterX86MetadataFormKey key);
BUSTER_F_DECL BusterX86MetadataEmitResult buster_x86_metadata_emit_exact(BusterX86MetadataEmitQuery query,
                                                                          BusterX86MetadataFormKey key);
// Minimal compiler ABI for trusted exact emission.  This is the preferred
// entry point for machine callers: no mnemonic lookup, candidate scan, or
// source-level operand projection is performed, and the durable key appears
// exactly once in the input object.
BUSTER_F_DECL BusterX86MetadataEmitResult buster_x86_metadata_emit_exact_query(BusterX86MetadataExactQuery query);
// Prepare one durable key into immutable exact state.  Call this only from
// the serial prewarm phase after buster_x86_metadata_prewarm(); duplicate
// preparation of the same key is idempotent.  No heap or callback is used.
BUSTER_F_DECL bool buster_x86_metadata_exact_plan_prepare(BusterX86MetadataFormKey key,
                                                           BusterX86MetadataExactPlan* result);
// Resolve a durable key to an immutable exact plan prepared during the
// serial metadata prewarm.  This returns false before prewarm, for unknown or
// stale keys, and for rows whose pattern/operand schema cannot be prepared.
BUSTER_F_DECL bool buster_x86_metadata_exact_plan_for_key(BusterX86MetadataFormKey key,
                                                           BusterX86MetadataExactPlan* result);
// Resolve an already validated plan to a compact immutable machine token.
// This serial-prewarm operation validates the fixed machine policy (64-bit
// address/execution mode, ordinary coverage, no implicit/privileged/not64
// policy, and the supplied target feature input); the returned token is safe
// to copy to worker lanes and remains valid for the process lifetime.
BUSTER_F_DECL bool buster_x86_metadata_machine_exact_token_for_plan(
    BusterX86MetadataExactPlan plan, BusterX86MetadataFeatureInput features,
    BusterX86MetadataMachineExactToken* result);
// Fast exact emission for a previously prepared plan.  The checked
// `buster_x86_metadata_emit_exact_query` remains the public fallback; this
// path skips repeated form-key and parsed-pattern lookup while retaining
// physical-query, capacity, feature, and relocation validation.
BUSTER_F_DECL BusterX86MetadataEmitResult buster_x86_metadata_emit_exact_prevalidated(BusterX86MetadataExactPlan plan,
                                                                                       BusterX86MetadataExactQuery query);
// Machine-trusted exact emission for a token created during serial prewarm.
// The token resolves directly to immutable normalized metadata, so this path
// skips durable-key lookup, generic physical-query validation, and the fixed
// machine feature/mode/coverage checks.  It still performs the full dynamic
// form transform (register, range, addressing, dynamic EGPR/APX, and
// instruction-length checks) plus output/relocation capacity checks.  Invalid
// or forged tokens fail closed without touching output.
BUSTER_F_DECL BusterX86MetadataEmitResult buster_x86_metadata_emit_exact_machine(
    BusterX86MetadataMachineExactToken token, BusterX86MetadataMachineExactQuery query);
// Build the deterministic physical query used by coverage consumers.  The
// returned operands, feature slot, and mnemonic buffer are caller-owned and
// are only borrowed by the query; they must remain live until the caller has
// finished selecting/emitting the form.  This is deliberately a production
// seam: source-completion audits and the ordinary metadata encoder must use
// the same canonical physical shape rather than maintaining parallel probes.
BUSTER_F_DECL bool buster_x86_metadata_canonical_query(u32 form_id, BusterX86MetadataPhysicalQuery* query,
                                                        BusterX86MetadataPhysicalOperand operands[16],
                                                        String8 features[1], char8 mnemonic_buffer[128]);
// The architectural instruction-length guard is shared by final emission and
// tests that exercise the boundary without requiring an impossible hardware
// encoding to be synthesized.
BUSTER_F_DECL BusterX86MetadataEncodeStatus buster_x86_metadata_instruction_length_status(u32 byte_count);
// Audit classifies every snapshot row deterministically.  Normalized rows
// are either EMITTED or have exactly one blocker; non-normalized rows are
// retained in the ledger with their explicit mode/privilege/reserved blocker.
BUSTER_F_DECL BusterX86MetadataCoverageAuditResult buster_x86_metadata_coverage_audit(
    BusterX86MetadataCoverageLedgerEntry* entries, u32 entry_capacity);
// Computes a stable, padding-free digest over the decoded forms, operands, and
// production coverage-ledger rows. `entry_count` is the number of form rows to
// include in the digest; `entry_capacity` is the number of rows available in
// `entries`, so callers can safely provide a partial/NULL ledger for a
// diagnostic digest. A complete audit passes both counts as the form count.
// The digest uses a little-endian FNV-1a stream and is independent of C
// padding or pointer addresses.
BUSTER_F_DECL u64 buster_x86_metadata_coverage_digest(BusterX86MetadataCoverageLedgerEntry const* entries, u32 entry_count,
                                                       u32 entry_capacity);

#if BUSTER_INCLUDE_TESTS
// Test-only access to the same constraint-aware physical query used by the
// coverage ledger.  Keeping this seam beside the ledger prevents source
// reachability tests from falling back to unconstrained zero operands.
bool buster_x86_metadata_test_canonical_query(u32 form_id, BusterX86MetadataPhysicalQuery* query,
                                              BusterX86MetadataPhysicalOperand operands[16], String8 features[1],
                                              char8 mnemonic_buffer[128]);

typedef enum BusterX86MetadataValidationPatchKind
{
    BUSTER_X86_METADATA_PATCH_FORM_SOURCE_OFFSET,
    BUSTER_X86_METADATA_PATCH_FORM_ICLASS_OFFSET,
    BUSTER_X86_METADATA_PATCH_FORM_STABLE_HASH,
    BUSTER_X86_METADATA_PATCH_FORM_OPERAND_RANGE,
    BUSTER_X86_METADATA_PATCH_FORM_COVERAGE_CLASS,
    BUSTER_X86_METADATA_PATCH_FORM_PREFIX_KIND,
    BUSTER_X86_METADATA_PATCH_FORM_FIELD_FLAGS,
    BUSTER_X86_METADATA_PATCH_FORM_DECORATOR_FLAGS,
    BUSTER_X86_METADATA_PATCH_FORM_APX_FLAGS,
    BUSTER_X86_METADATA_PATCH_FORM_AMX_FLAGS,
    BUSTER_X86_METADATA_PATCH_FORM_MODE_FLAGS,
    BUSTER_X86_METADATA_PATCH_FORM_ENCODING_WIDTHS,
    BUSTER_X86_METADATA_PATCH_FORM_MANDATORY_PREFIX,
    BUSTER_X86_METADATA_PATCH_FORM_RESERVED,
    BUSTER_X86_METADATA_PATCH_FORM_RESERVED2,
    BUSTER_X86_METADATA_PATCH_OPERAND_RESERVED,
    BUSTER_X86_METADATA_PATCH_OPERAND_KIND,
    BUSTER_X86_METADATA_PATCH_OPERAND_FIELD_SOURCE,
    BUSTER_X86_METADATA_PATCH_OPERAND_ACCESS,
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

bool buster_x86_metadata_validate_patch(BusterX86MetadataValidationPatch patch,
                                                            BusterX86MetadataValidationResult* result);
bool buster_x86_metadata_test_execution_mode_matches(u16 mode_flags, u8 coverage_class,
                                                                         bool include_not64, u8 execution_mode);
bool buster_x86_metadata_test_eamode_alias_forms(u32 first_form_id, u32 second_form_id);
bool buster_x86_metadata_test_fixed_bsrinit_no_zeroing(void);
#endif
