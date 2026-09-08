#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/target.h>

// Target-neutral facts consumed by the handwritten x86-64 and AArch64
// selectors. machine_type_classes_build projects module types once;
// machine_selection_value_facts_allocate and MachineSelectionRowLayout serve
// the target row walks. machine_selection_validate_function checks only the
// storage/ownership contract of the unvalidated selector entry point.

typedef enum MachineSelectionValidationError
{
    MACHINE_SELECTION_VALIDATION_NONE,
    MACHINE_SELECTION_VALIDATION_INVALID_ARGUMENT,
    MACHINE_SELECTION_VALIDATION_OWNERSHIP,
    MACHINE_SELECTION_VALIDATION_DUPLICATE_DEFINITION,
    MACHINE_SELECTION_VALIDATION_INVALID_VALUE,
    MACHINE_SELECTION_VALIDATION_INVALID_OPCODE,
    MACHINE_SELECTION_VALIDATION_ERROR_COUNT,
} MachineSelectionValidationError;

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

// The normal compiler pipeline validates canonical IR before selection. The
// public unvalidated entry additionally checks row ownership and value ids;
// this is a shape check, not a substitute for the canonical IR verifier.
BUSTER_F_DECL MachineSelectionValidationError machine_selection_validate_function(Arena* arena, IrProgram* program, IrFunction* function);
BUSTER_F_DECL MachineSelectionValueFacts machine_selection_value_facts_allocate(Arena* arena, u32 value_count);
