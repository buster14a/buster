#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/target.h>

// The shared instruction-selection prepass.  It deliberately consumes only
// canonical IR and target-neutral facts so the x86-64 and AArch64 selectors
// can make the same legality and profitability queries without rebuilding
// use/definition information in each target file.

typedef enum MachineSelectionPrepassError
{
    MACHINE_SELECTION_PREPASS_NONE,
    MACHINE_SELECTION_PREPASS_INVALID_ARGUMENT,
    MACHINE_SELECTION_PREPASS_OWNERSHIP,
    MACHINE_SELECTION_PREPASS_DUPLICATE_DEFINITION,
    MACHINE_SELECTION_PREPASS_INVALID_VALUE,
    MACHINE_SELECTION_PREPASS_INVALID_OPCODE,
    MACHINE_SELECTION_PREPASS_ERROR_COUNT,
} MachineSelectionPrepassError;

typedef enum MachineSelectionValueFlag
{
    MACHINE_SELECTION_VALUE_KNOWN_CONSTANT = 1u << 0,
    MACHINE_SELECTION_VALUE_KNOWN_ZERO = 1u << 1,
    MACHINE_SELECTION_VALUE_KNOWN_NONZERO = 1u << 2,
    MACHINE_SELECTION_VALUE_ADDRESS_CANDIDATE = 1u << 3,
    MACHINE_SELECTION_VALUE_PROMOTABLE_LOCAL = 1u << 4,
    MACHINE_SELECTION_VALUE_READ_ONLY = 1u << 5,
} MachineSelectionValueFlag;

typedef enum MachineSelectionResultClass
{
    MACHINE_SELECTION_RESULT_NONE = 0,
    MACHINE_SELECTION_RESULT_SCALAR = 1u << 0,
    MACHINE_SELECTION_RESULT_VECTOR = 1u << 1,
    MACHINE_SELECTION_RESULT_AGGREGATE = 1u << 2,
    MACHINE_SELECTION_RESULT_ADDRESS = 1u << 3,
    MACHINE_SELECTION_RESULT_VOID = 1u << 4,
} MachineSelectionResultClass;

typedef enum MachineSelectionSideEffect
{
    MACHINE_SELECTION_SIDE_EFFECT_NONE = 0,
    MACHINE_SELECTION_SIDE_EFFECT_READ_MEMORY = 1u << 0,
    MACHINE_SELECTION_SIDE_EFFECT_WRITE_MEMORY = 1u << 1,
    MACHINE_SELECTION_SIDE_EFFECT_ATOMIC = 1u << 2,
    MACHINE_SELECTION_SIDE_EFFECT_CALL = 1u << 3,
    MACHINE_SELECTION_SIDE_EFFECT_CONTROL = 1u << 4,
    MACHINE_SELECTION_SIDE_EFFECT_BARRIER = 1u << 5,
    MACHINE_SELECTION_SIDE_EFFECT_UNKNOWN = 1u << 6,
} MachineSelectionSideEffect;

typedef struct MachineSelectionPrepass MachineSelectionPrepass;
struct MachineSelectionPrepass
{
    IrProgram* program;
    IrFunction* function;
    MachineSelectionPrepassError error;
    IrBlockId* instruction_owner_blocks;
    u32* instruction_ordinals;
    IrInstructionId* value_definitions;
    u32* value_definition_blocks;
    u32* value_definition_ordinals;
    u32* value_first_use_ordinals;
    u32* value_last_use_ordinals;
    u32* value_use_counts;
    u32* value_use_blocks;
    u32* value_local_store_counts;
    u64* value_constant_bits;
    u8* value_flags;
    u8* value_promotable_local_widths;
    u8* instruction_result_classes;
    u8* instruction_side_effects;
    u32 instruction_count;
    u32 value_count;
    u32 block_count;
    u32 ordinal_count;
    bool valid;
    u8 reserved[3];
};

// The marker used in block/value arrays when no owner or block-local use is
// known.  It intentionally matches the IR id invalid value.
#define MACHINE_SELECTION_INVALID_INDEX UINT32_MAX
#define MACHINE_SELECTION_MULTIPLE_BLOCKS (UINT32_MAX - 1u)

// Dense value facts accumulated by a validated target selector while it
// already walks the canonical rows for target-specific ordering and local
// promotion. Keeping these as three SoA streams lets that existing pass
// replace the standalone ownership/definition/use traversal.
typedef struct MachineSelectionValueFacts MachineSelectionValueFacts;
struct MachineSelectionValueFacts
{
    u32* definition_blocks;
    u32* use_counts;
    u32* use_blocks;
};

// Program order for one function's rows, accumulated by the walk a target
// selector has to make anyway so that every later prepass counts rows down
// instead of chasing `next` again.  The C lowerer appends a block's rows
// consecutively, so a block's `block_row_counts[b]` rows are the dense id
// range starting at its `first_instruction` and the layout needs no per-row
// storage at all.  Anything that leaves a block's rows out of that shape —
// the selection reordering test relinks two of them on purpose, and a
// producer that interleaved two blocks' appends would too — is why `rows`
// exists: it carries the gathered order for the whole function instead.
typedef struct MachineSelectionRowLayout MachineSelectionRowLayout;
struct MachineSelectionRowLayout
{
    u32* block_row_counts;
    u32* rows;
};

// The id of a block's row number `offset`, where `row_base` is the number of
// rows the enclosing walk has already passed in earlier blocks.  Callers keep
// that running total anyway: it is also the row's zero-based ordinal.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u32 machine_selection_row_id(MachineSelectionRowLayout const* layout, IrBlock const* block,
                                                                                  u32 row_base, u32 offset)
{
    return layout->rows ? layout->rows[row_base + offset] : block->first_instruction.value + offset;
}

// A per-type projection of the IrType facts a selector's row path asks,
// indexed by IrTypeId and built once per codegen module before any function
// selects (machine_type_classes_build). The IrType record is 136 bytes and a
// predicate reads two or three lines of it; sixteen of these share a line,
// and the selectors fetch the record only for what the projection does not
// carry: field offsets, element sizes and aggregate sizes. An id outside the
// table reads as MACHINE_TYPE_CLASS_NONE, which answers every predicate the
// way a null IrType does.
typedef enum MachineTypeClassFlag
{
    MACHINE_TYPE_CLASS_RESOLVED = 1u << 0,
    // A resolved boolean, integer, pointer or enum of at most eight bytes.
    MACHINE_TYPE_CLASS_SCALAR_REGISTER = 1u << 1,
    // A 32- or 64-bit float, resolved or not.
    MACHINE_TYPE_CLASS_FLOAT_SCALAR = 1u << 2,
    // A resolved 64-byte vector.
    MACHINE_TYPE_CLASS_VECTOR_REGISTER = 1u << 3,
    // A pointer, a function, or an integer wider than 32 bits: an operand
    // whose ALU rows take the 64-bit form.
    MACHINE_TYPE_CLASS_WIDE = 1u << 4,
    MACHINE_TYPE_CLASS_SIGNED = 1u << 5,
    // An integer of exactly 128 bits.
    MACHINE_TYPE_CLASS_INTEGER128 = 1u << 6,
    // A struct, union or slice.
    MACHINE_TYPE_CLASS_AGGREGATE = 1u << 7,
} MachineTypeClassFlag;

#define MACHINE_TYPE_CLASS_NO_LOG2 0xffu

typedef struct MachineTypeClass MachineTypeClass;
struct MachineTypeClass
{
    u8 flags;
    // IrTypeKind, or IR_TYPE_COUNT for an id the table does not hold.
    u8 kind;
    // log2 of layout.size for a resolved power-of-two size, else
    // MACHINE_TYPE_CLASS_NO_LOG2.
    u8 size_log2;
    // log2 of bit_width for a power-of-two width, else
    // MACHINE_TYPE_CLASS_NO_LOG2.
    u8 bit_width_log2;
};
BUSTER_CT_CHECK(sizeof(MachineTypeClass) == 4);
#define MACHINE_TYPE_CLASS_NONE                                                                                                                                \
    ((MachineTypeClass){.kind = IR_TYPE_COUNT, .size_log2 = MACHINE_TYPE_CLASS_NO_LOG2, .bit_width_log2 = MACHINE_TYPE_CLASS_NO_LOG2})

BUSTER_F_DECL MachineTypeClass* machine_type_classes_build(Arena* arena, IrTypeTable const* types);

// One module's selection context: what its functions share across their
// selections, built once before the first of them selects
// (machine_select_module_prepare) and read by every one. The x86-64 plans
// fill on first use, which is per lane by construction — a module is
// selected by one lane and a program is never shared between lanes — and
// their pool is sized and allocated by the prepare, so a codegen attempt's
// rewind cannot take a plan away from the slot that names it.
typedef struct MachineX64SignaturePlan MachineX64SignaturePlan;
typedef struct MachineSelectionModule MachineSelectionModule;
struct MachineSelectionModule
{
    MachineTypeClass const* type_classes;
    u32 type_count;
    // x86-64 only: per type id, the index of a function type's plan in the
    // pool (UINT32_MAX for every other type), and the pool itself.
    u32* x64_signature_slots;
    MachineX64SignaturePlan* x64_signature_plans;
    u32 x64_signature_plan_count;
};

BUSTER_F_DECL MachineSelectionPrepass machine_selection_prepass_build(Arena* arena, IrProgram* program, IrFunction* function);
// Production selectors consume only the ID-keyed definition/use facts below.
// Keep the full builder above for diagnostics and the declarative matcher;
// this variant retains validation while omitting unconsumed per-instruction
// classifications, ordinals, and value flags.
BUSTER_F_DECL MachineSelectionPrepass machine_selection_prepass_build_minimal(Arena* arena, IrProgram* program, IrFunction* function);
BUSTER_F_DECL MachineSelectionValueFacts machine_selection_value_facts_allocate(Arena* arena, u32 value_count);
BUSTER_F_DECL bool machine_selection_prepass_value_flag(MachineSelectionPrepass const* prepass, IrValueId value, MachineSelectionValueFlag flag);
BUSTER_F_DECL bool machine_selection_prepass_instruction_owned_by(MachineSelectionPrepass const* prepass, IrInstructionId instruction, u32 block_index);
BUSTER_F_DECL MachineSelectionResultClass machine_selection_result_class(MachineSelectionPrepass const* prepass, IrInstructionId instruction);
BUSTER_F_DECL MachineSelectionSideEffect machine_selection_side_effects(MachineSelectionPrepass const* prepass, IrInstructionId instruction);

// Declarative rule selection.  The generated decision tree consumes a compact
// context assembled from the prepass and returns the first legal FAST rule;
// QUALITY receives the same first rule plus a bounded list of alternatives.
typedef enum MachineSelectionMode
{
    MACHINE_SELECTION_MODE_FAST,
    MACHINE_SELECTION_MODE_QUALITY,
    MACHINE_SELECTION_MODE_COUNT,
} MachineSelectionMode;

typedef enum MachineSelectionRuleStatus
{
    MACHINE_SELECTION_RULE_STATUS_NONE,
    MACHINE_SELECTION_RULE_STATUS_LEGAL,
    MACHINE_SELECTION_RULE_STATUS_QUALITY_ALTERNATIVE,
    MACHINE_SELECTION_RULE_STATUS_FALLBACK,
    MACHINE_SELECTION_RULE_STATUS_COUNT,
} MachineSelectionRuleStatus;

typedef enum MachineSelectionRuleId
{
    MACHINE_SELECTION_RULE_INVALID,
    MACHINE_SELECTION_RULE_CONSTANT,
    MACHINE_SELECTION_RULE_LOCAL,
    MACHINE_SELECTION_RULE_MEMORY,
    MACHINE_SELECTION_RULE_ADDRESS,
    MACHINE_SELECTION_RULE_ARITHMETIC,
    MACHINE_SELECTION_RULE_CALL,
    MACHINE_SELECTION_RULE_CONTROL,
    MACHINE_SELECTION_RULE_VECTOR,
    MACHINE_SELECTION_RULE_SCALAR,
    MACHINE_SELECTION_RULE_LEGACY_FALLBACK,
    MACHINE_SELECTION_RULE_COUNT,
} MachineSelectionRuleId;

typedef struct MachineSelectionRuleContext MachineSelectionRuleContext;
struct MachineSelectionRuleContext
{
    IrOpcode opcode;
    MachineSelectionResultClass result_class;
    MachineSelectionSideEffect side_effects;
    Target target;
    u32 operand_count;
    u32 target_count;
    u32 immediate_count;
    bool known_constant;
    bool address_candidate;
    bool promotable_local;
    bool volatile_access;
    bool vector_features;
    u8 reserved[3];
};

#define MACHINE_SELECTION_MAX_QUALITY_ALTERNATIVES 4u

typedef struct MachineSelectionRuleCandidate MachineSelectionRuleCandidate;
struct MachineSelectionRuleCandidate
{
    MachineSelectionRuleId rule;
    MachineSelectionRuleStatus status;
    u16 rank;
    u16 estimated_cost;
    u16 reserved;
};

typedef struct MachineSelectionDecision MachineSelectionDecision;
struct MachineSelectionDecision
{
    MachineSelectionRuleCandidate selected;
    MachineSelectionRuleCandidate alternatives[MACHINE_SELECTION_MAX_QUALITY_ALTERNATIVES];
    u32 alternative_count;
    MachineSelectionMode mode;
    bool used_legacy_fallback;
    u8 reserved[3];
};

typedef struct MachineSelectionCounters MachineSelectionCounters;
struct MachineSelectionCounters
{
    u64 query_count[MACHINE_SELECTION_MODE_COUNT];
    u64 legal_count[MACHINE_SELECTION_MODE_COUNT];
    u64 fallback_count[MACHINE_SELECTION_MODE_COUNT];
    u64 alternative_count[MACHINE_SELECTION_MODE_COUNT];
    u64 rule_hits[MACHINE_SELECTION_RULE_COUNT];
    u64 rule_rejections[MACHINE_SELECTION_RULE_COUNT];
};

BUSTER_F_DECL MachineSelectionRuleContext machine_selection_rule_context(MachineSelectionPrepass const* prepass, IrInstructionId instruction, Target target);
BUSTER_F_DECL MachineSelectionDecision machine_selection_rule_select(MachineSelectionRuleContext context, MachineSelectionMode mode,
                                                                      MachineSelectionCounters* counters);
BUSTER_F_DECL void machine_selection_counters_reset(MachineSelectionCounters* counters);
