#pragma once

#include <buster/lib/base.h>
#include <buster/lib/target.h>

// Exact architectural forms used at the AArch64 MC boundary. These IDs are
// deliberately distinct from MachineOpcode: machine rows may still be
// pseudos or semantic operations, while every value here denotes one real
// 32-bit instruction encoding.
typedef u16 A64Opcode;
enum
{
    A64_OPCODE_INVALID,
    A64_OPCODE_NOP,
    A64_OPCODE_B,
    A64_OPCODE_BL,
    A64_OPCODE_B_COND,
    A64_OPCODE_RET,
    A64_OPCODE_BR,
    A64_OPCODE_BLR,
    A64_OPCODE_LDR_LITERAL_64,
    A64_OPCODE_ADRP,
    A64_OPCODE_COUNT,
};

typedef enum A64MCOperandKind
{
    A64_MC_OPERAND_NONE,
    A64_MC_OPERAND_REGISTER,
    A64_MC_OPERAND_IMMEDIATE,
    A64_MC_OPERAND_PC_RELATIVE,
    A64_MC_OPERAND_COUNT,
} A64MCOperandKind;

typedef struct A64MCOperand A64MCOperand;
struct A64MCOperand
{
    s64 value;
    u8 kind;
    u8 reserved[7];
};
BUSTER_CT_CHECK(sizeof(A64MCOperand) == 16);

#define A64_MC_MAX_OPERANDS 2

typedef struct A64MCInst A64MCInst;
struct A64MCInst
{
    A64MCOperand operands[A64_MC_MAX_OPERANDS];
    A64Opcode opcode;
    u8 operand_count;
    u8 reserved[5];
};
BUSTER_CT_CHECK(sizeof(A64MCInst) == 40);

typedef enum A64PCRelativeLayout
{
    A64_PC_RELATIVE_NONE,
    A64_PC_RELATIVE_IMM26,
    A64_PC_RELATIVE_IMM19,
    A64_PC_RELATIVE_ADRP,
    A64_PC_RELATIVE_LAYOUT_COUNT,
} A64PCRelativeLayout;

typedef struct A64OpcodeDescriptor A64OpcodeDescriptor;
struct A64OpcodeDescriptor
{
    u32 fixed_mask;
    u32 fixed_value;
    u8 operand_count;
    u8 pc_relative_operand;
    u8 pc_relative_layout;
    u8 reserved;
};
BUSTER_CT_CHECK(sizeof(A64OpcodeDescriptor) == 12);

BUSTER_F_DECL A64OpcodeDescriptor const* a64_opcode_descriptor(A64Opcode opcode);

// Shared symmetric transform for A64's signed, scaled immediates. `bits`
// describes the encoded two's-complement field and `scale_log2` the number
// of implicit low zero bits in the architectural value.
BUSTER_F_DECL bool a64_signed_scaled_immediate_encode(s64 value, u8 bits, u8 scale_log2, u32* encoded);
BUSTER_F_DECL bool a64_signed_scaled_immediate_decode(u32 encoded, u8 bits, u8 scale_log2, s64* value);

// Encode or decode one exact real instruction. The current tranche covers
// the shared control-flow and PC-relative kernel; later generated opcodes can
// extend the descriptor table without changing this interface.
BUSTER_F_DECL bool a64_mc_encode(A64MCInst const* instruction, u32* word);
BUSTER_F_DECL bool a64_mc_decode(u32 word, A64MCInst* instruction);

// Patch only the PC-relative field of an already encoded exact form. Fixed
// bits and all non-PC-relative operands must already match `opcode`.
BUSTER_F_DECL bool a64_pc_relative_patch(A64Opcode opcode, u32 word, s64 displacement, u32* patched);

// Compute target + addend - place without requiring an intermediate term to
// fit in s64. Extreme unsigned addresses and signed addends may cancel to a
// small, valid architectural displacement.
BUSTER_F_DECL bool a64_pc_relative_displacement(u64 target, u64 place, s64 addend, s64* displacement);

// ADRP uses page addresses and a split signed imm21. This helper performs the
// page rounding and checked subtraction without relying on signed overflow.
BUSTER_F_DECL bool a64_adrp_encode(u32 destination_register, u64 instruction_address, u64 target_address, u32* word);

// Architectural condition values 0..13 form inverse pairs. AL/NV are valid
// four-bit fields but have no conditional inverse for relaxation.
BUSTER_F_DECL bool a64_condition_invert(u32 condition, u32* inverse);

// Pointer-free descriptors for the checked-in AArch64 metadata snapshot.  The
// generated table types intentionally do not cross this public boundary.  A
// string is represented by its stable logical pool range; callers can inspect
// it a byte at a time with buster_aarch64_metadata_string_byte().
typedef struct BusterAarch64MetadataString BusterAarch64MetadataString;
struct BusterAarch64MetadataString
{
    u32 offset;
    u32 length;
};

typedef struct BusterAarch64MetadataSegment BusterAarch64MetadataSegment;
struct BusterAarch64MetadataSegment
{
    u32 id;
    u8 instruction_lsb;
    u8 width;
    u8 value_lsb;
    u8 reserved;
};

typedef struct BusterAarch64MetadataField BusterAarch64MetadataField;
struct BusterAarch64MetadataField
{
    u32 id;
    BusterAarch64MetadataString name;
    u32 segment_first;
    u32 source_mask;
    u8 width;
    u8 segment_count;
    u8 transform;
    u8 relocation;
    u8 relocation_end;
    u8 shift;
    u8 flags;
    u8 reserved;
};

typedef struct BusterAarch64MetadataOperand BusterAarch64MetadataOperand;
struct BusterAarch64MetadataOperand
{
    u32 id;
    BusterAarch64MetadataString syntax;
    BusterAarch64MetadataString type;
    BusterAarch64MetadataString name;
    u32 field_index;
    s32 immediate_min;
    s32 immediate_max;
    u16 register_width;
    u32 tied_to;
    u16 address_base_index;
    u16 address_offset_index;
    u8 direction;
    u8 kind;
    u8 flags;
    u8 scale;
    u8 immediate_flags;
    u8 address_kind;
    u8 address_flags;
    u8 reserved;
};

typedef struct BusterAarch64MetadataForm BusterAarch64MetadataForm;
struct BusterAarch64MetadataForm
{
    // `id` is stable only for the checked-in snapshot.  source_hash is the
    // durable identity of the imported source row; name/signature hashes are
    // exposed separately for semantic and lookup layers.
    u32 id;
    u64 source_hash;
    u64 name_hash;
    u64 signature_hash;
    BusterAarch64MetadataString name;
    BusterAarch64MetadataString mnemonic;
    BusterAarch64MetadataString assembly;
    BusterAarch64MetadataString output_operands;
    BusterAarch64MetadataString input_operands;
    u32 field_first;
    u32 operand_first;
    u32 predicate_first;
    u32 normalized_form_id;
    u16 field_count;
    u16 operand_count;
    u16 predicate_count;
    u16 reserved0;
    u32 fixed_mask;
    u32 fixed_value;
    u8 coverage_class;
    u8 encoder_family;
    u8 test_class;
    u8 reason_id;
    u8 assembly_flags;
    u8 address_kind;
    u8 address_flags;
    u8 reserved1;
    u16 address_base_index;
    u16 address_offset_index;
    // This only describes whether the raw 32-bit field layout is complete.
    // Semantic encoder coverage is represented independently by coverage_class
    // and encoder_family.
    bool raw_layout_complete;
    // Exact importer decision for the pinned Apple-M1 profile.  This is not
    // semantic encoder readiness; target-feature evaluation remains separate.
    bool apple_m1_profile_member;
    bool provisionally_apple_m1;
    // Independent predicate-expression validity bit.  A malformed predicate
    // may coexist with an earlier parse reason, so callers must not infer it
    // from reason_id. This consumes the byte that was reserved in schema 5 so
    // the public structure retains its established size and field offsets.
    bool predicate_parse_error;
};
BUSTER_CT_CHECK(sizeof(BusterAarch64MetadataForm) == 120);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BusterAarch64MetadataForm, provisionally_apple_m1) == 118);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BusterAarch64MetadataForm, predicate_parse_error) == 119);

typedef struct BusterAarch64MetadataCounts BusterAarch64MetadataCounts;
struct BusterAarch64MetadataCounts
{
    u32 form_count;
    u32 field_count;
    u32 segment_count;
    u32 operand_count;
    u32 predicate_count;
    u32 string_pool_size;
    u32 apple_m1_supported_count;
    u32 apple_m1_raw_layout_complete_count;
    u32 apple_m1_raw_layout_incomplete_count;
};

// Coverage classification is independent from raw bit-layout completeness.
// Keep these values aligned with the generated snapshot while exposing a
// stable public vocabulary to semantic encoder and test layers.
typedef enum BusterAarch64MetadataCoverageClass
{
    BUSTER_AARCH64_METADATA_COVERAGE_DIRECT,
    BUSTER_AARCH64_METADATA_COVERAGE_NORMALIZED,
    BUSTER_AARCH64_METADATA_COVERAGE_ALIAS,
    BUSTER_AARCH64_METADATA_COVERAGE_PRIVILEGED_SYSTEM,
    BUSTER_AARCH64_METADATA_COVERAGE_RESERVED_UNENCODABLE,
    BUSTER_AARCH64_METADATA_COVERAGE_UNSUPPORTED_TOKEN,
    BUSTER_AARCH64_METADATA_COVERAGE_UNCLASSIFIED,
    BUSTER_AARCH64_METADATA_COVERAGE_CLASS_COUNT,
} BusterAarch64MetadataCoverageClass;

// Import diagnostics are part of the public descriptor contract so callers
// never need to include the generated packed-table implementation header.
typedef enum BusterAarch64MetadataReason
{
    BUSTER_AARCH64_METADATA_REASON_NONE,
    BUSTER_AARCH64_METADATA_REASON_ALIAS_OF_CANONICAL,
    BUSTER_AARCH64_METADATA_REASON_SYSTEM_OR_PRIVILEGED,
    BUSTER_AARCH64_METADATA_REASON_UNMAPPED_VARIABLE,
    BUSTER_AARCH64_METADATA_REASON_CONFLICTING_BIT_ASSIGNMENT,
    BUSTER_AARCH64_METADATA_REASON_MALFORMED_DAG,
    BUSTER_AARCH64_METADATA_REASON_MALFORMED_TEMPLATE,
    BUSTER_AARCH64_METADATA_REASON_UNKNOWN_FIELD,
    BUSTER_AARCH64_METADATA_REASON_UNKNOWN_PREDICATE,
    BUSTER_AARCH64_METADATA_REASON_MISSING_OPERAND,
    BUSTER_AARCH64_METADATA_REASON_INVALID_JSON,
    BUSTER_AARCH64_METADATA_REASON_NULL_FIELD,
    BUSTER_AARCH64_METADATA_REASON_UNPROVEN_FIELD_SEMANTICS,
    BUSTER_AARCH64_METADATA_REASON_UNPROVEN_OPERAND_KIND,
    BUSTER_AARCH64_METADATA_REASON_UNPROVEN_IMMEDIATE_RANGE,
    BUSTER_AARCH64_METADATA_REASON_UNPROVEN_MEMORY_FORM,
    BUSTER_AARCH64_METADATA_REASON_UNPROVEN_TIED_OPERAND,
    BUSTER_AARCH64_METADATA_REASON_UNPROVEN_CORRESPONDENCE,
    BUSTER_AARCH64_METADATA_REASON_UNSUPPORTED_ADDRESS_GRAMMAR,
    BUSTER_AARCH64_METADATA_REASON_COUNT,
} BusterAarch64MetadataReason;

typedef enum BusterAarch64MetadataTarget
{
    BUSTER_AARCH64_METADATA_TARGET_APPLE_M1,
    BUSTER_AARCH64_METADATA_TARGET_COUNT,
} BusterAarch64MetadataTarget;

// Generated table sizes and bounded descriptor accessors.
BUSTER_F_DECL u32 buster_aarch64_metadata_schema_version(void);
BUSTER_F_DECL u32 buster_aarch64_metadata_form_count(void);
BUSTER_F_DECL u32 buster_aarch64_metadata_field_count(void);
BUSTER_F_DECL u32 buster_aarch64_metadata_segment_count(void);
BUSTER_F_DECL u32 buster_aarch64_metadata_operand_count(void);
BUSTER_F_DECL u32 buster_aarch64_metadata_predicate_count(void);
BUSTER_F_DECL u32 buster_aarch64_metadata_string_pool_size(void);
BUSTER_F_DECL BusterAarch64MetadataCounts buster_aarch64_metadata_counts(void);
BUSTER_F_DECL bool buster_aarch64_metadata_string(u32 offset, BusterAarch64MetadataString* result);
BUSTER_F_DECL u8 buster_aarch64_metadata_string_byte(BusterAarch64MetadataString string, u32 index);
BUSTER_F_DECL bool buster_aarch64_metadata_form(u32 form_id, BusterAarch64MetadataForm* result);
BUSTER_F_DECL bool buster_aarch64_metadata_field(u32 form_id, u32 field_index, BusterAarch64MetadataField* result);
BUSTER_F_DECL bool buster_aarch64_metadata_segment(u32 form_id, u32 field_index, u32 segment_index, BusterAarch64MetadataSegment* result);
BUSTER_F_DECL bool buster_aarch64_metadata_operand(u32 form_id, u32 operand_index, BusterAarch64MetadataOperand* result);
BUSTER_F_DECL bool buster_aarch64_metadata_predicate(u32 form_id, u32 predicate_index, BusterAarch64MetadataString* result);

// The target classifier is deliberately conservative: an empty predicate
// list is supported, every predicate must be one of the explicitly modelled
// AArch64 predicates, and an unknown predicate makes the form unsupported.
// Invalid targets and non-AArch64 targets are rejected before effective
// features are inspected. HasEL3 is a model/platform capability, not an
// ordinary extension feature: it is true only for Apple M1 or a native target
// which has already resolved to Apple M1.
BUSTER_F_DECL bool buster_aarch64_metadata_form_supported_for_target(u32 form_id, Target target);
BUSTER_F_DECL bool buster_aarch64_metadata_form_supported(u32 form_id, BusterAarch64MetadataTarget target);
BUSTER_F_DECL bool buster_aarch64_metadata_form_provisionally_apple_m1_supported(u32 form_id);
BUSTER_F_DECL bool buster_aarch64_metadata_form_has_complete_raw_layout(u32 form_id);

// Raw field bit plumbing. Values are packed source-field u32 values, before
// any semantic register/immediate/relocation transform. The caller supplies
// exactly form.field_count values; raw-layout-incomplete, unmapped, malformed, or
// overlapping layouts are rejected in both directions.
BUSTER_F_DECL bool buster_aarch64_metadata_raw_encode(u32 form_id, u32 const* field_values, u32 field_count, u32* word);
BUSTER_F_DECL bool buster_aarch64_metadata_raw_decode(u32 form_id, u32 word, u32* field_values, u32 field_count);

// Fast production path for the importer-named Apple-M1 forms.  Unlike the
// generic metadata API above, this path reads only direct generated plan
// tables; it never decodes packed metadata or repeats structural layout
// validation. Input count, null pointers, and every source-field mask remain
// checked on each call.
BUSTER_F_DECL u32 buster_aarch64_production_plan_form_count(void);
BUSTER_F_DECL u32 buster_aarch64_production_plan_field_count(void);
BUSTER_F_DECL u32 buster_aarch64_production_plan_segment_count(void);
BUSTER_F_DECL bool buster_aarch64_production_raw_encode(u32 form_id, u32 const* field_values, u32 field_count, u32* word);

// Short aliases used by assembly semantic layers that already speak in terms
// of generated forms. They are wrappers, not a second metadata implementation.
BUSTER_F_DECL u32 a64_generated_form_count(void);
BUSTER_F_DECL u32 a64_generated_field_count(void);
BUSTER_F_DECL bool a64_generated_form(u32 form_id, BusterAarch64MetadataForm* result);
BUSTER_F_DECL bool a64_generated_raw_encode(u32 form_id, u32 const* field_values, u32 field_count, u32* word);
BUSTER_F_DECL bool a64_generated_raw_decode(u32 form_id, u32 word, u32* field_values, u32 field_count);
BUSTER_F_DECL bool a64_generated_production_raw_encode(u32 form_id, u32 const* field_values, u32 field_count, u32* word);

#if BUSTER_INCLUDE_TESTS
// Test seam for the fail-closed generated predicate-error bit. The checked
// production snapshot deliberately contains no malformed predicate rows.
BUSTER_F_DECL bool buster_aarch64_metadata_test_predicate_parse_error_fails_closed(Target target);
BUSTER_F_DECL void buster_aarch64_metadata_test_reset_packed_access_counter(void);
BUSTER_F_DECL u32 buster_aarch64_metadata_test_packed_access_count(void);
#endif
